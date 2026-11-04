#include "net_https.h"

#ifdef SONICR_MATCHMAKER

#include "certificates.h"
#include "bearssl/bearssl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_CLOSE closesocket
#define SOCK_INVALID INVALID_SOCKET
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
typedef int sock_t;
#define SOCK_CLOSE close
#define SOCK_INVALID (-1)
#endif

extern void DebugLog(const char *fmt, ...);

/* ─── NTP fallback for dead-battery RTCs ────────────────────────── */

#define NTP_TIMESTAMP_DELTA 2208988800UL

static const char *s_ntpServers[] = {
    "216.239.35.0",
    "216.239.35.4",
    "216.239.35.8",
    "216.239.35.12",
};
#define NTP_SERVER_COUNT (int)(sizeof(s_ntpServers) / sizeof(s_ntpServers[0]))

static uint64_t s_ntpTime;
static int s_ntpValid;

#pragma pack(push, 1)
typedef struct {
    uint8_t  li_vn_mode;
    uint8_t  stratum;
    uint8_t  poll;
    uint8_t  precision;
    uint32_t rootDelay;
    uint32_t rootDispersion;
    uint32_t refId;
    uint32_t refTm_s, refTm_f;
    uint32_t origTm_s, origTm_f;
    uint32_t rxTm_s, rxTm_f;
    uint32_t txTm_s, txTm_f;
} NtpPacket;
#pragma pack(pop)

static int ntp_fetch(uint64_t *out)
{
    NtpPacket pkt;

    for (int i = 0; i < NTP_SERVER_COUNT; i++) {
        sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == SOCK_INVALID) continue;

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(123);
        addr.sin_addr.s_addr = inet_addr(s_ntpServers[i]);

        memset(&pkt, 0, sizeof(pkt));
        pkt.li_vn_mode = (0 << 6) | (4 << 3) | 3;

#ifdef _WIN32
        DWORD rcvtimeo = 2000;
#else
        struct timeval rcvtimeo;
        rcvtimeo.tv_sec  = 2;
        rcvtimeo.tv_usec = 0;
#endif
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&rcvtimeo, sizeof(rcvtimeo));

        if (sendto(s, (const char *)&pkt, sizeof(pkt), 0,
                   (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            SOCK_CLOSE(s);
            continue;
        }

        int n = (int)recv(s, (char *)&pkt, sizeof(pkt), 0);
        SOCK_CLOSE(s);

        if (n < 48) {
            DebugLog("net_https: NTP %s: got %d bytes\n", s_ntpServers[i], n);
            continue;
        }

        uint32_t txSec = ntohl(pkt.txTm_s);
        if (txSec < NTP_TIMESTAMP_DELTA) continue;

        *out = (uint64_t)(txSec - NTP_TIMESTAMP_DELTA);
        DebugLog("net_https: NTP time from %s: %llu\n", s_ntpServers[i], (unsigned long long)*out);
        return 1;
    }

    DebugLog("net_https: all NTP servers failed\n");
    return 0;
}

static uint64_t get_current_time(void)
{
    if (s_ntpValid) return s_ntpTime;

    if (ntp_fetch(&s_ntpTime)) {
        s_ntpValid = 1;
        DebugLog("net_https: using NTP time %llu\n", (unsigned long long)s_ntpTime);
        return s_ntpTime;
    }

    uint64_t t = (uint64_t)time(NULL);
    DebugLog("net_https: NTP failed, falling back to RTC (%llu)\n", (unsigned long long)t);
    return t;
}

/* ─── URL parsing ───────────────────────────────────────────────── */

typedef struct {
    char protocol[8];
    char host[128];
    char port[8];
    char path[256];
} UrlParts;

static int parse_url(const char *url, UrlParts *out)
{
    memset(out, 0, sizeof(*out));

    const char *slashes = strstr(url, "//");
    if (!slashes || slashes == url) return 0;

    int proto_len = (int)(slashes - url - 1);
    if (proto_len <= 0 || proto_len >= (int)sizeof(out->protocol)) return 0;
    memcpy(out->protocol, url, proto_len);
    out->protocol[proto_len] = '\0';

    const char *after = slashes + 2;
    const char *slash = strchr(after, '/');
    const char *colon = strchr(after, ':');

    if (colon && (!slash || colon < slash)) {
        int host_len = (int)(colon - after);
        if (host_len >= (int)sizeof(out->host)) host_len = (int)sizeof(out->host) - 1;
        memcpy(out->host, after, host_len);
        out->host[host_len] = '\0';

        const char *port_start = colon + 1;
        const char *port_end = slash ? slash : port_start + strlen(port_start);
        int port_len = (int)(port_end - port_start);
        if (port_len >= (int)sizeof(out->port)) port_len = (int)sizeof(out->port) - 1;
        memcpy(out->port, port_start, port_len);
        out->port[port_len] = '\0';
    } else {
        const char *host_end = slash ? slash : after + strlen(after);
        int host_len = (int)(host_end - after);
        if (host_len >= (int)sizeof(out->host)) host_len = (int)sizeof(out->host) - 1;
        memcpy(out->host, after, host_len);
        out->host[host_len] = '\0';

        if (strcmp(out->protocol, "https") == 0)
            strcpy(out->port, "443");
        else
            strcpy(out->port, "80");
    }

    if (!slash || *(slash + 1) == '\0') {
        strcpy(out->path, "/");
    } else {
        strncpy(out->path, slash, sizeof(out->path) - 1);
        out->path[sizeof(out->path) - 1] = '\0';
    }

    return 1;
}

/* ─── BearSSL socket callbacks ──────────────────────────────────── */

static int ssl_sock_read(void *ctx, unsigned char *buf, size_t len)
{
    sock_t s = *(sock_t *)ctx;
    for (;;) {
        int rlen = (int)recv(s, (char *)buf, (int)len, 0);
        if (rlen <= 0) {
            if (rlen == 0) return -1;
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) continue;
#else
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
#endif
            return -1;
        }
        return rlen;
    }
}

static int ssl_sock_write(void *ctx, const unsigned char *buf, size_t len)
{
    sock_t s = *(sock_t *)ctx;
    for (;;) {
        int wlen = (int)send(s, (const char *)buf, (int)len, 0);
        if (wlen <= 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (wlen < 0 && err == WSAEWOULDBLOCK) continue;
#else
            if (wlen < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
#endif
            return -1;
        }
        return wlen;
    }
}

/* ─── TCP connect ───────────────────────────────────────────────── */

static sock_t tcp_connect(const char *host, const char *port)
{
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        DebugLog("net_https: getaddrinfo(%s:%s) failed: %d\n", host, port, err);
        return SOCK_INVALID;
    }

    sock_t s = SOCK_INVALID;
    for (rp = res; rp; rp = rp->ai_next) {
        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == SOCK_INVALID) continue;
        if (connect(s, rp->ai_addr, (int)rp->ai_addrlen) == 0) break;
        SOCK_CLOSE(s);
        s = SOCK_INVALID;
    }
    freeaddrinfo(res);

    if (s == SOCK_INVALID) {
        DebugLog("net_https: connect(%s:%s) failed\n", host, port);
    }
    return s;
}

/* ─── Build HTTP request string ─────────────────────────────────── */

static int build_request(char *buf, int bufsize,
                         const char *method, const UrlParts *parts,
                         const char *token, const char *body)
{
    int off = 0;

    off += snprintf(buf + off, bufsize - off, "%s %s HTTP/1.0\r\n", method, parts->path);
    off += snprintf(buf + off, bufsize - off, "Host: %s\r\n", parts->host);
    off += snprintf(buf + off, bufsize - off, "Accept: */*\r\n");
    off += snprintf(buf + off, bufsize - off, "User-Agent: SonicR/1.0\r\n");
    off += snprintf(buf + off, bufsize - off, "Connection: close\r\n");

    if (token && token[0]) {
        off += snprintf(buf + off, bufsize - off, "X-Reality-Jump-Token: %s\r\n", token);
    }

    if (body && body[0]) {
        int bodylen = (int)strlen(body);
        off += snprintf(buf + off, bufsize - off, "Content-Type: application/json\r\n");
        off += snprintf(buf + off, bufsize - off, "Content-Length: %d\r\n", bodylen);
        off += snprintf(buf + off, bufsize - off, "\r\n");
        off += snprintf(buf + off, bufsize - off, "%s", body);
    } else {
        off += snprintf(buf + off, bufsize - off, "\r\n");
    }

    return off;
}

/* ─── Read full response (plain or SSL) ─────────────────────────── */

static int read_response_ssl(br_sslio_context *ioc, br_ssl_client_context *sc,
                             HttpsResponse *resp)
{
    resp->len = 0;
    resp->data[0] = '\0';
    resp->status = 0;

    char buf[2048];
    for (;;) {
        int n = br_sslio_read(ioc, buf, sizeof(buf));
        int err = br_ssl_engine_last_error(&sc->eng);
        if (err != 0 && err != BR_ERR_IO) {
            DebugLog("net_https: SSL read error %d\n", err);
            return -1;
        }
        if (n < 0) break;
        if (n == 0) break;

        int space = HTTPS_RESPONSE_BUF_SIZE - resp->len - 1;
        int copy = (n < space) ? n : space;
        if (copy > 0) {
            memcpy(resp->data + resp->len, buf, copy);
            resp->len += copy;
            resp->data[resp->len] = '\0';
        }
    }

    return 0;
}

static int read_response_plain(sock_t s, HttpsResponse *resp)
{
    resp->len = 0;
    resp->data[0] = '\0';
    resp->status = 0;

    char buf[2048];
    for (;;) {
        int n = (int)recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;

        int space = HTTPS_RESPONSE_BUF_SIZE - resp->len - 1;
        int copy = (n < space) ? n : space;
        if (copy > 0) {
            memcpy(resp->data + resp->len, buf, copy);
            resp->len += copy;
            resp->data[resp->len] = '\0';
        }
    }
    return 0;
}

/* ─── Parse HTTP status from response ───────────────────────────── */

static long parse_http_status(const char *data)
{
    if (strncmp(data, "HTTP/1.", 7) != 0 && strncmp(data, "HTTP1.", 6) != 0)
        return -1;

    const char *sp = strchr(data, ' ');
    if (!sp) return -1;
    return atol(sp + 1);
}

/* Strip HTTP headers, shift body to front of resp->data */
static void strip_headers(HttpsResponse *resp)
{
    char *sep = strstr(resp->data, "\r\n\r\n");
    int hdr_end;
    if (sep) {
        hdr_end = (int)(sep - resp->data) + 4;
    } else {
        sep = strstr(resp->data, "\n\n");
        if (sep)
            hdr_end = (int)(sep - resp->data) + 2;
        else
            return;
    }

    int body_len = resp->len - hdr_end;
    if (body_len > 0)
        memmove(resp->data, resp->data + hdr_end, body_len);
    else
        body_len = 0;
    resp->data[body_len] = '\0';
    resp->len = body_len;
}

/* ─── Core request function ─────────────────────────────────────── */

static long do_request(const char *method, const char *url,
                       const char *token, const char *body,
                       HttpsResponse *resp)
{
    resp->len = 0;
    resp->data[0] = '\0';
    resp->status = 0;

    UrlParts parts;
    if (!parse_url(url, &parts)) {
        DebugLog("net_https: bad URL: %s\n", url);
        return -1;
    }

    int is_secure = (strcmp(parts.protocol, "https") == 0);

    sock_t s = tcp_connect(parts.host, parts.port);
    if (s == SOCK_INVALID) return -1;

    char reqbuf[2048];
    int reqlen = build_request(reqbuf, sizeof(reqbuf), method, &parts, token, body);

    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    static unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];

    long status = -1;

    if (is_secure) {
        uint64_t now = get_current_time();

        br_ssl_client_init_full(&sc, &xc, TAs, TAs_NUM);
        br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof(iobuf), 1);
#ifdef SONICR_XBOX
        {
            /* No OS random source on the Xbox (no /dev/urandom, no RDRAND on
             * the Pentium III), so BearSSL has nothing to seed its PRNG with
             * and the handshake would fail. Feed it timing jitter: the CPU
             * cycle counter sampled around short sleeps, plus the clock. */
            extern void xb_gather_entropy(unsigned char *out, int len);
            unsigned char seed[64];
            xb_gather_entropy(seed, (int)sizeof(seed));
            br_ssl_engine_inject_entropy(&sc.eng, seed, sizeof(seed));
        }
#endif

        if (!br_ssl_client_reset(&sc, parts.host, 0)) {
            DebugLog("net_https: SSL reset failed, err=%d\n", sc.eng.err);
            goto cleanup;
        }

        br_sslio_init(&ioc, &sc.eng, ssl_sock_read, &s, ssl_sock_write, &s);

        uint32_t seconds = (uint32_t)(now % 86400);
        uint32_t days    = (uint32_t)(now / 86400) + 719528;
        br_x509_minimal_set_time(&xc, days, seconds);

        br_sslio_flush(&ioc);

        if (br_ssl_engine_current_state(ioc.engine) == BR_SSL_CLOSED) {
            DebugLog("net_https: SSL closed during handshake, err=%d\n", sc.eng.err);
            goto cleanup;
        }

        if (br_sslio_write_all(&ioc, reqbuf, reqlen) < 0) {
            DebugLog("net_https: SSL write failed, err=%d\n", sc.eng.err);
            goto cleanup;
        }
        if (br_sslio_flush(&ioc) < 0) {
            DebugLog("net_https: SSL flush failed\n");
            goto cleanup;
        }

        if (read_response_ssl(&ioc, &sc, resp) < 0) goto cleanup;

    } else {
        int total = 0;
        while (total < reqlen) {
            int n = (int)send(s, reqbuf + total, reqlen - total, 0);
            if (n <= 0) {
                DebugLog("net_https: plain send failed\n");
                goto cleanup;
            }
            total += n;
        }

        read_response_plain(s, resp);
    }

    status = parse_http_status(resp->data);
    strip_headers(resp);
    resp->status = (int)status;

cleanup:
    if (is_secure) br_sslio_close(&ioc);
    SOCK_CLOSE(s);
    return status;
}

/* ─── Public API ────────────────────────────────────────────────── */

int net_https_init(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
#endif
    return 1;
}

void net_https_shutdown(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

long net_https_get(const char *url, const char *token, HttpsResponse *resp)
{
    return do_request("GET", url, token, NULL, resp);
}

long net_https_post(const char *url, const char *token,
                    const char *body, HttpsResponse *resp)
{
    return do_request("POST", url, token, body, resp);
}

#else /* !SONICR_MATCHMAKER */

int  net_https_init(void)    { return 0; }
void net_https_shutdown(void) {}
long net_https_get(const char *url, const char *token, HttpsResponse *resp)
    { (void)url; (void)token; (void)resp; return -1; }
long net_https_post(const char *url, const char *token,
                    const char *body, HttpsResponse *resp)
    { (void)url; (void)token; (void)body; (void)resp; return -1; }

#endif
