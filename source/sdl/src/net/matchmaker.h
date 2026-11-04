/**
 * matchmaker.h — Reality Jump matchmaker + sign-in API
 */

#ifndef MATCHMAKER_H
#define MATCHMAKER_H

#include <stdint.h>

#ifdef SONICR_DC
#include "platform.h"
#endif

#define SIGNIN_DOMAIN       "signin.sonicr.online"
#define MATCHMAKER_DOMAIN   "matchmaker.sonicr.online"
#define GAME_SLUG           "sonicr"
#define MATCHMAKER_TOKEN_FILE "ONLINE.DAT"

/* Platform / connection strings for session registration */
#ifdef SONICR_DC
#define MM_PLATFORM     "dreamcast"
#elif defined(SONICR_XBOX)
#define MM_PLATFORM     "xbox"
#elif defined(_WIN32)
#define MM_PLATFORM     "windows"
#elif defined(__linux__)
#define MM_PLATFORM     "linux"
#elif defined(__APPLE__)
#define MM_PLATFORM     "macos"
#else
#define MM_PLATFORM     "unknown"
#endif

#ifdef SONICR_DC
#define MM_CONNECTION   (platform_net_is_modem() ? "modem" : "broadband")
#else
#define MM_CONNECTION   "broadband"
#endif

#define MM_MAX_SESSIONS     16
#define MM_MAX_USERNAME     33
#define MM_MAX_TOKEN        64
#define MM_MAX_CODE         16
#define MM_MAX_IP           46

typedef struct {
    int64_t id;
    char    host_name[MM_MAX_USERNAME];
    int     player_count;
    char    ip_address[MM_MAX_IP];
    int     port;
    char    country_code[4];   /* ISO 3166-1 alpha-2 (e.g. "US"), or "" */
    int     ping_ms;           /* RTT in ms; -1 = unknown / unreachable */
    char    platform[16];      /* "dreamcast", "windows", "linux", etc. */
    char    connection[16];    /* "modem" or "broadband" */
} MatchmakerSession;

typedef struct {
    MatchmakerSession sessions[MM_MAX_SESSIONS];
    int count;
    int has_next;
} MatchmakerSessionList;

/* Init / shutdown (call once at startup / exit) */
int  MatchmakerInit(void);
void MatchmakerShutdown(void);

/* Token persistence */
int  MatchmakerLoadToken(void);
void MatchmakerSaveToken(void);
int  MatchmakerHasToken(void);

/* Sign-in flow */
int  MatchmakerRequestCode(void);
int  MatchmakerPollToken(void);
int  MatchmakerRefreshToken(void);

/* QR code for sign-in URL */
int  MatchmakerGenerateQR(uint8_t *qrBuf, int *qrSize);

/* Session management */
int  MatchmakerListSessions(MatchmakerSessionList *out);
int  MatchmakerCreateSession(const char *username, int port);
int  MatchmakerKeepAlive(int player_count, const char *status);
void MatchmakerClearSession(void);

/* UPnP port forwarding */
int  UpnpOpenPort(int port);
void UpnpClosePort(int port);

/* Accessors */
const char *MatchmakerGetCode(void);
const char *MatchmakerGetToken(void);
const char *MatchmakerGetUsername(void);
int64_t     MatchmakerGetSessionId(void);

/* Set a fallback username used when no matchmaker token is in play
 * (e.g. CLI --username on a LAN-only session). The matchmaker username
 * still wins once signed in. */
void MatchmakerSetFallbackUsername(const char *name);

/* Drop the in-memory auth token + username for this session.
 * ONLINE.DAT on disk is left intact so a future launch can still use it.
 * Used by the F3 "skip sign-in, LAN only" path. */
void MatchmakerClearToken(void);

#endif
