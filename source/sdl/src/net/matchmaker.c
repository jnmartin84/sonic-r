/**
 * matchmaker.c — Reality Jump sign-in + matchmaker API via BearSSL
 *
 * Sign-in flow:
 *   1. MatchmakerRequestCode() → POST signin/api/v1/codes/
 *   2. User visits signin.realityjump.co.uk/{code} on another device
 *   3. MatchmakerPollToken() → GET signin/api/v1/codes/{code}/
 *   4. Once auth_token appears, save to ONLINE.DAT
 *   5. Future launches: MatchmakerRefreshToken() to re-validate
 *
 * Matchmaker flow:
 *   - MatchmakerListSessions() → browse available games
 *   - MatchmakerCreateSession() → register as host
 *   - MatchmakerKeepAlive() → periodic ping to stay listed
 *
 * All matchmaker calls send X-Reality-Jump-Token header.
 */

#include "matchmaker.h"
#include "../fileio.h"

#include <string.h>

#ifdef SONICR_MATCHMAKER

#include "../qrcodegen.h"
#include "../sonicr_functions.h"
#include "../net_transport.h"
#include "net_https.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* State───────────── */

static char s_authToken[MM_MAX_TOKEN];
static char s_username[MM_MAX_USERNAME];
static char s_code[MM_MAX_CODE];
static int64_t s_sessionId = -1;
static int s_initialized = 0;
static char s_fallbackUsername[MM_MAX_USERNAME];

/* Minimal JSON helpers────────────────────────────────────── */

static const char *json_find_key(const char *json, const char *key)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    return p;
}

static int json_get_string(const char *json, const char *key, char *out, int maxlen)
{
    const char *p = json_find_key(json, key);
    if (!p) return 0;
    if (*p == 'n' && strncmp(p, "null", 4) == 0) return 0;
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < maxlen - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

static int json_get_int(const char *json, const char *key, int *out)
{
    const char *p = json_find_key(json, key);
    if (!p) return 0;
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        *out = atoi(p);
        return 1;
    }
    return 0;
}

static int json_get_int64(const char *json, const char *key, int64_t *out)
{
    const char *p = json_find_key(json, key);
    if (!p) return 0;
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        *out = strtoll(p, NULL, 10);
        return 1;
    }
    return 0;
}

static int json_get_bool(const char *json, const char *key)
{
    const char *p = json_find_key(json, key);
    if (!p) return 0;
    return (strncmp(p, "true", 4) == 0);
}

static int json_has_non_null(const char *json, const char *key)
{
    const char *p = json_find_key(json, key);
    if (!p) return 0;
    return (*p != 'n' || strncmp(p, "null", 4) != 0);
}

/* Parse a JSON array of session objects. Expects {"sessions": [...]} */
static int json_parse_sessions(const char *json, MatchmakerSessionList *out)
{
    out->count = 0;
    out->has_next = json_get_bool(json, "has_next");

    const char *arr = json_find_key(json, "sessions");
    if (!arr || *arr != '[') return 0;
    arr++;

    while (*arr && out->count < MM_MAX_SESSIONS) {
        const char *obj = strchr(arr, '{');
        if (!obj) break;
        const char *end = strchr(obj, '}');
        if (!end) break;

        int len = (int)(end - obj + 1);
        char tmp[512];
        if (len >= (int)sizeof(tmp)) len = (int)sizeof(tmp) - 1;
        memcpy(tmp, obj, len);
        tmp[len] = '\0';

        MatchmakerSession *s = &out->sessions[out->count];
        memset(s, 0, sizeof(*s));
        json_get_int64(tmp, "id", &s->id);
        /* The /sessions/ POST body sends the host's name under the
         * "username" key; the list endpoint returns it the same way.
         * We also try "host_name" as a fallback in case the server
         * starts to namespace it differently. */
        if (!json_get_string(tmp, "username", s->host_name, MM_MAX_USERNAME))
            json_get_string(tmp, "host_name", s->host_name, MM_MAX_USERNAME);
        json_get_int(tmp, "player_count", &s->player_count);
        json_get_string(tmp, "ip_address", s->ip_address, MM_MAX_IP);
        json_get_int(tmp, "port", &s->port);
        json_get_string(tmp, "host_platform", s->platform, sizeof(s->platform));
        json_get_string(tmp, "host_connection", s->connection, sizeof(s->connection));
        DebugLog("Picker: session %lld user='%s' ip=%s:%d players=%d platform=%s conn=%s\n",
                 (long long)s->id, s->host_name,
                 s->ip_address, s->port, s->player_count,
                 s->platform, s->connection);
        out->count++;

        arr = end + 1;
    }
    return 1;
}

/* Init / Shutdown─── */

int MatchmakerInit(void)
{
    if (s_initialized) return 1;
    if (!net_https_init()) return 0;
    s_initialized = 1;
    s_authToken[0] = '\0';
    s_username[0] = '\0';
    s_code[0] = '\0';
    s_sessionId = -1;
    return 1;
}

void MatchmakerShutdown(void)
{
    if (!s_initialized) return;
    net_https_shutdown();
    s_initialized = 0;
}

/* Token Persistence─ */

int MatchmakerLoadToken(void)
{
    FILE *fp = fOpen(MATCHMAKER_TOKEN_FILE, "rb");
    if (!fp) return 0;

    char buf[MM_MAX_TOKEN + MM_MAX_USERNAME + 2];
    memset(buf, 0, sizeof(buf));
    int n = (int)fRead(buf, 1, sizeof(buf) - 1, fp);
    fClose(fp);
    if (n <= 0) return 0;

    /* Format: token\nusername */
    char *nl = strchr(buf, '\n');
    if (!nl) return 0;
    *nl = '\0';
    strncpy(s_authToken, buf, MM_MAX_TOKEN - 1);
    s_authToken[MM_MAX_TOKEN - 1] = '\0';
    strncpy(s_username, nl + 1, MM_MAX_USERNAME - 1);
    s_username[MM_MAX_USERNAME - 1] = '\0';
    return s_authToken[0] != '\0';
}

void MatchmakerSaveToken(void)
{
    if (!s_authToken[0]) return;
    FILE *fp = fOpen(MATCHMAKER_TOKEN_FILE, "wb");
    if (!fp) return;
    /* fWrite (not fprintf) — on DC this FILE* may be a VMU virtual handle. */
    char buf[MM_MAX_TOKEN + MM_MAX_USERNAME + 2];
    int len = snprintf(buf, sizeof(buf), "%s\n%s", s_authToken, s_username);
    if (len > 0) fWrite(buf, 1, (size_t)len, fp);
    fClose(fp);
}

int MatchmakerHasToken(void)
{
    return s_authToken[0] != '\0';
}

/* Sign-in Flow────── */

int MatchmakerRequestCode(void)
{
    HttpsResponse resp;
    long status = net_https_post(
        "https://" SIGNIN_DOMAIN "/api/v1/codes/",
        NULL,
        "{\"game_slug\": \"" GAME_SLUG "\"}",
        &resp
    );
    if (status != 200) return 0;
    return json_get_string(resp.data, "code", s_code, MM_MAX_CODE);
}

int MatchmakerPollToken(void)
{
    if (!s_code[0]) return 0;

    char url[256];
    snprintf(url, sizeof(url),
             "https://" SIGNIN_DOMAIN "/api/v1/codes/%s/", s_code);

    HttpsResponse resp;
    long status = net_https_get(url, NULL, &resp);
    if (status != 200) return 0;

    if (!json_get_bool(resp.data, "is_active")) return -1;
    if (!json_has_non_null(resp.data, "auth_token")) return 0;

    json_get_string(resp.data, "auth_token", s_authToken, MM_MAX_TOKEN);
    json_get_string(resp.data, "username", s_username, MM_MAX_USERNAME);
    MatchmakerSaveToken();
    return 1;
}

int MatchmakerRefreshToken(void)
{
    if (!s_authToken[0]) return 0;

    HttpsResponse resp;
    long status = net_https_post(
        "https://" SIGNIN_DOMAIN "/api/v1/tokens/refresh/",
        s_authToken,
        "{\"game_slug\": \"" GAME_SLUG "\"}",
        &resp
    );
    if (status != 200) return 0;

    json_get_string(resp.data, "auth_token", s_authToken, MM_MAX_TOKEN);
    json_get_string(resp.data, "username", s_username, MM_MAX_USERNAME);
    MatchmakerSaveToken();
    return 1;
}

/* QR Code─────────── */

int MatchmakerGenerateQR(uint8_t *qrBuf, int *qrSize)
{
    if (!s_code[0]) return 0;

    char url[256];
    snprintf(url, sizeof(url), "https://" SIGNIN_DOMAIN "/%s", s_code);

    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(url, tmp, qrBuf,
            qrcodegen_Ecc_MEDIUM,
            qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
            qrcodegen_Mask_AUTO, true)) {
        return 0;
    }

    *qrSize = qrcodegen_getSize(qrBuf);
    return 1;
}

/* IP geolocation (free ip-api.com, HTTP only on free tier)─── */

#define GEO_CACHE_SIZE 32

static struct {
    char ip[MM_MAX_IP];
    char country[4];
} s_geoCache[GEO_CACHE_SIZE];
static int s_geoCacheCount = 0;

static int geo_lookup_cached(const char *ip, char *out)
{
    for (int i = 0; i < s_geoCacheCount; i++) {
        if (strcmp(s_geoCache[i].ip, ip) == 0) {
            strncpy(out, s_geoCache[i].country, 3);
            out[3] = '\0';
            return 1;
        }
    }
    return 0;
}

static void geo_cache_store(const char *ip, const char *country)
{
    if (s_geoCacheCount >= GEO_CACHE_SIZE) return;
    if (country == NULL) return;

    size_t countryLen = strlen(country) - 1;
    if (countryLen > 3) {
        countryLen = 3;
    }

    strncpy(s_geoCache[s_geoCacheCount].ip, ip, MM_MAX_IP - 1);
    s_geoCache[s_geoCacheCount].ip[MM_MAX_IP - 1] = '\0';
    s_geoCache[s_geoCacheCount].country[0] = '\0';
    s_geoCache[s_geoCacheCount].country[1] = '\0';
    s_geoCache[s_geoCacheCount].country[2] = '\0';
    s_geoCache[s_geoCacheCount].country[3] = '\0';
    strncpy(s_geoCache[s_geoCacheCount].country, country, countryLen);
    s_geoCacheCount++;
}

static int geo_lookup(const char *ip, char *out)
{
    out[0] = '\0';
    if (!ip || !ip[0]) return 0;
    if (geo_lookup_cached(ip, out)) return 1;

    char url[128];
    snprintf(url, sizeof(url),
             "http://ip-api.com/json/%s?fields=countryCode", ip);

    HttpsResponse resp;
    long status = net_https_get(url, NULL, &resp);
    if (status != 200) return 0;

    char country[4];
    if (!json_get_string(resp.data, "countryCode", country, sizeof(country)))
        return 0;

    geo_cache_store(ip, country);
    strncpy(out, country, 3);
    out[3] = '\0';
    return 1;
}

/* Session Management */

int MatchmakerListSessions(MatchmakerSessionList *out)
{
    memset(out, 0, sizeof(*out));

    HttpsResponse resp;
    long status = net_https_get(
        "https://" MATCHMAKER_DOMAIN "/api/v1/games/" GAME_SLUG "/sessions/",
        s_authToken, &resp
    );
    DebugLog("MatchmakerListSessions: HTTP %ld body=%.200s\n", status, resp.data);
    if (status != 200) return 0;
    if (!json_parse_sessions(resp.data, out)) return 0;

    for (int i = 0; i < out->count; i++) {
        geo_lookup(out->sessions[i].ip_address,
                   out->sessions[i].country_code);
        out->sessions[i].ping_ms = net_probe_ping(
            out->sessions[i].ip_address,
            out->sessions[i].port,
            1000);
    }
    return 1;
}

int MatchmakerCreateSession(const char *username, int port)
{
    char body[512];
    snprintf(body, sizeof(body),
        "{\"username\": \"%s\", \"uid\": %u, \"timestamp\": %u, "
        "\"player_count\": 1, \"protocol_id\": 0, "
        "\"private_key\": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"
                         "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0], "
        "\"port\": %d, \"platform\": \"%s\", \"connection\": \"%s\"}",
        username, (unsigned)time(NULL), (unsigned)time(NULL), port,
        MM_PLATFORM, MM_CONNECTION
    );

    HttpsResponse resp;
    long status = net_https_post(
        "https://" MATCHMAKER_DOMAIN "/api/v1/games/" GAME_SLUG "/sessions/",
        s_authToken, body, &resp
    );
    DebugLog("MatchmakerCreateSession: HTTP %ld, body=%s\n", status, resp.data);
    if (status != 200) {
        return 0;
    }

    json_get_int64(resp.data, "id", &s_sessionId);
    DebugLog("MatchmakerCreateSession: id=%lld\n", (long long)s_sessionId);
    return s_sessionId > 0;
}

int MatchmakerKeepAlive(int player_count, const char *status)
{
    if (s_sessionId < 0) return 0;

    char url[256];
    snprintf(url, sizeof(url),
        "https://" MATCHMAKER_DOMAIN "/api/v1/games/" GAME_SLUG
        "/sessions/%lld/keep-alive/", (long long)s_sessionId);

    char body[128];
    snprintf(body, sizeof(body),
        "{\"player_count\": %d, \"status\": \"%s\"}",
        player_count, status);

    HttpsResponse resp;
    long st = net_https_post(url, s_authToken, body, &resp);
    DebugLog("MatchmakerKeepAlive: HTTP %ld body=%.200s\n", st, resp.data);
    DebugLog("curl -v -X POST -H \"Content-Type: application/json\" "
             "-H \"X-Reality-Jump-Token: %s\" "
             "-d '%s' \"%s\"\n", s_authToken, body, url);
    return (int)st;
}

void MatchmakerClearSession(void)
{
    s_sessionId = -1;
}

/* UPnP───────────── */

#ifdef SONICR_DC

int  UpnpOpenPort(int port)  { (void)port; return 0; }
void UpnpClosePort(int port) { (void)port; }

#elif defined(SONICR_XBOX)

/* Xbox: no miniupnpc; use the self-contained SSDP/SOAP client in net_upnp.c. */
#include "net_upnp.h"

int  UpnpOpenPort(int port)  { return net_upnp_open_port(port); }
void UpnpClosePort(int port) { net_upnp_close_port(port); }

#else /* desktop: miniupnpc */

#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

static char s_upnpControlUrl[256];
static char s_upnpServiceType[256];
static int  s_upnpActive = 0;

int UpnpOpenPort(int port)
{
    struct UPNPDev *devlist = upnpDiscover(2000, NULL, NULL, 0, 0, 2, NULL);
    if (!devlist) {
        DebugLog("UPnP: no devices found\n");
        return 0;
    }

    struct UPNPUrls urls;
    struct IGDdatas data;
    char lanAddr[64];

#if MINIUPNPC_API_VERSION >= 18
    char wanAddr[64];
    int r = UPNP_GetValidIGD(devlist, &urls, &data,
                              lanAddr, sizeof(lanAddr),
                              wanAddr, sizeof(wanAddr));
#else
    int r = UPNP_GetValidIGD(devlist, &urls, &data,
                              lanAddr, sizeof(lanAddr));
#endif
    freeUPNPDevlist(devlist);
    if (r != 1) {
        DebugLog("UPnP: no valid IGD found (r=%d)\n", r);
        return 0;
    }
    DebugLog("UPnP: LAN addr=%s\n", lanAddr);

    char extIp[64] = {0};
    int extRes = UPNP_GetExternalIPAddress(urls.controlURL,
                                           data.first.servicetype, extIp);
    if (extRes == 0 && extIp[0]) {
        DebugLog("UPnP: router external IP=%s (compare to whatismyip; if different => CGNAT)\n", extIp);
    } else {
        DebugLog("UPnP: GetExternalIPAddress failed (res=%d)\n", extRes);
    }

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%d", port);

    int err = UPNP_AddPortMapping(
        urls.controlURL, data.first.servicetype,
        portStr, portStr, lanAddr,
        "SonicR", "UDP", NULL, "3600");

    if (err == 725 /* OnlyPermanentLeasesSupported */) {
        DebugLog("UPnP: router rejected lease=3600, retrying with lease=0\n");
        err = UPNP_AddPortMapping(
            urls.controlURL, data.first.servicetype,
            portStr, portStr, lanAddr,
            "SonicR", "UDP", NULL, "0");
    }

    if (err == 0) {
        DebugLog("UPnP: AddPortMapping returned OK for port %d\n", port);

        char mIntClient[64] = {0}, mIntPort[8] = {0}, mDesc[80] = {0};
        char mEnabled[8] = {0}, mLease[16] = {0};
        int v = UPNP_GetSpecificPortMappingEntry(
            urls.controlURL, data.first.servicetype,
            portStr, "UDP", NULL,
            mIntClient, mIntPort, mDesc, mEnabled, mLease);
        if (v == 0) {
            DebugLog("UPnP: verify OK -> intClient=%s intPort=%s enabled=%s lease=%s desc=%s\n",
                     mIntClient, mIntPort, mEnabled, mLease, mDesc);
        } else {
            DebugLog("UPnP: VERIFY FAILED (res=%d) — router reported success but mapping is not present\n", v);
        }

        snprintf(s_upnpControlUrl, sizeof(s_upnpControlUrl), "%s", urls.controlURL);
        snprintf(s_upnpServiceType, sizeof(s_upnpServiceType), "%s", data.first.servicetype);
        s_upnpActive = 1;
    } else {
        const char *hint = "";
        switch (err) {
            case 718: hint = " (ConflictInMappingEntry — port already mapped to another host)"; break;
            case 724: hint = " (SamePortValuesRequired)"; break;
            case 725: hint = " (OnlyPermanentLeasesSupported)"; break;
            case 726: hint = " (RemoteHostOnlySupportsWildcard)"; break;
            case 727: hint = " (ExternalPortOnlySupportsWildcard)"; break;
            case 728: hint = " (NoPortMapsAvailable)"; break;
            case 729: hint = " (ConflictWithOtherMechanisms)"; break;
            default:  break;
        }
        DebugLog("UPnP: AddPortMapping failed (err=%d)%s\n", err, hint);
    }

    FreeUPNPUrls(&urls);
    return err == 0;
}

void UpnpClosePort(int port)
{
    if (!s_upnpActive) return;

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%d", port);
    UPNP_DeletePortMapping(s_upnpControlUrl, s_upnpServiceType,
                           portStr, "UDP", NULL);
    s_upnpActive = 0;
}

#endif /* SONICR_DC */

/* Accessors───────── */

const char *MatchmakerGetCode(void)     { return s_code; }
const char *MatchmakerGetToken(void)    { return s_authToken; }
const char *MatchmakerGetUsername(void)
{
    if (s_username[0]) return s_username;
    return s_fallbackUsername;          /* "" if never set */
}
int64_t     MatchmakerGetSessionId(void) { return s_sessionId; }

void MatchmakerSetFallbackUsername(const char *name)
{
    if (!name) { s_fallbackUsername[0] = '\0'; return; }
    strncpy(s_fallbackUsername, name, MM_MAX_USERNAME - 1);
    s_fallbackUsername[MM_MAX_USERNAME - 1] = '\0';
}

void MatchmakerClearToken(void)
{
    s_authToken[0] = '\0';
    s_username[0] = '\0';
}

#else /* !SONICR_MATCHMAKER */

int  MatchmakerInit(void)                                 { return 0; }   /* match real semantics: 0 = unavailable */
void MatchmakerShutdown(void)                             {}
int  MatchmakerLoadToken(void)                            { return 0; }
void MatchmakerSaveToken(void)                            {}
int  MatchmakerHasToken(void)                             { return 0; }
int  MatchmakerRequestCode(void)                          { return 0; }
int  MatchmakerPollToken(void)                            { return 0; }
int  MatchmakerRefreshToken(void)                         { return 0; }
int  MatchmakerGenerateQR(uint8_t *qrBuf, int *qrSize)    { (void)qrBuf; if (qrSize) *qrSize = 0; return 0; }
int  MatchmakerListSessions(MatchmakerSessionList *out)   { if (out) { out->count = 0; out->has_next = 0; } return 0; }
int  MatchmakerCreateSession(const char *u, int p)        { (void)u; (void)p; return 0; }
int  MatchmakerKeepAlive(int n, const char *s)            { (void)n; (void)s; return 0; }
void MatchmakerClearSession(void)                         {}
int  UpnpOpenPort(int port)                               { (void)port; return 0; }
void UpnpClosePort(int port)                              { (void)port; }
const char *MatchmakerGetCode(void)                       { return ""; }
const char *MatchmakerGetToken(void)                      { return ""; }
static char s_fallbackUsername[33] = {0};
const char *MatchmakerGetUsername(void)                   { return s_fallbackUsername; }
int64_t     MatchmakerGetSessionId(void)                  { return -1; }
void MatchmakerSetFallbackUsername(const char *name)
{
    if (!name) { s_fallbackUsername[0] = '\0'; return; }
    strncpy(s_fallbackUsername, name, sizeof(s_fallbackUsername) - 1);
    s_fallbackUsername[sizeof(s_fallbackUsername) - 1] = '\0';
}
void MatchmakerClearToken(void)                           {}

#endif /* SONICR_MATCHMAKER */
