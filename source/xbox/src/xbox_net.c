/*
 * xbox_net.c - network bring-up and socket helpers for the Xbox build.
 *
 * nxNetInit() (nxdk) starts lwIP and the nForce NIC driver and then waits up
 * to ten seconds for DHCP, using the dashboard's network settings (DHCP or a
 * static address). That would stall the boot, so it runs on its own thread;
 * xbox_net_state() tells the game when the console has an address.
 *
 * Also emulates SO_RCVTIMEO, which nxdk's lwIP is built without (see
 * include/xbox_sock.h).
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include <lwip/sockets.h>
#include <lwip/netif.h>
#include <lwip/dns.h>
#include <nxdk/net.h>
#include "net/bearssl/bearssl.h"

extern void DebugLog(const char *fmt, ...);
extern struct netif *g_pnetif;

static volatile int s_netState;   /* 0 starting, 1 up, -1 failed */

int xbox_net_state(void)
{
    return s_netState;
}

static int SDLCALL xbox_net_thread(void *arg)
{
    (void)arg;
    int r = nxNetInit(NULL);
    if (r == 0 && g_pnetif && !ip4_addr_isany_val(*netif_ip4_addr(g_pnetif))) {
        char ip[16], gw[16], dns0[16];
        snprintf(ip, sizeof(ip), "%s", ip4addr_ntoa(netif_ip4_addr(g_pnetif)));
        snprintf(gw, sizeof(gw), "%s", ip4addr_ntoa(netif_ip4_gw(g_pnetif)));
        const ip_addr_t *d = dns_getserver(0);
        snprintf(dns0, sizeof(dns0), "%s", d ? ipaddr_ntoa(d) : "-");
        fprintf(stderr, "net: up, IP %s, gateway %s, DNS %s\n", ip, gw, dns0);
        s_netState = 1;
    }
    else {
        fprintf(stderr, "net: no network (nxNetInit %d) - online play unavailable\n", r);
        s_netState = -1;
    }
    fflush(stderr);
    return 0;
}

void xbox_net_start(void)
{
    SDL_Thread *t = SDL_CreateThread(xbox_net_thread, "netinit", NULL);
    if (t) {
        SDL_DetachThread(t);
    }
    else {
        s_netState = -1;
    }
}

/* ---- SO_RCVTIMEO emulation -------------------------------------------- */

#define XB_MAX_SOCK 64
static int s_rcvTimeoutMs[XB_MAX_SOCK];

static int sock_index(int s)
{
    int i = s - LWIP_SOCKET_OFFSET;
    return (i >= 0 && i < XB_MAX_SOCK) ? i : -1;
}

int xb_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
    if (level == SOL_SOCKET && optname == SO_RCVTIMEO && optval) {
        int i = sock_index(s);
        if (i < 0) return -1;
        if (optlen >= (socklen_t)sizeof(struct timeval)) {
            const struct timeval *tv = (const struct timeval *)optval;
            s_rcvTimeoutMs[i] = (int)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
        }
        else {
            s_rcvTimeoutMs[i] = *(const int *)optval;   /* Windows-style DWORD ms */
        }
        return 0;
    }
    return lwip_setsockopt(s, level, optname, optval, optlen);
}

/* Returns 1 if data can be read now, 0 on timeout. */
static int wait_readable(int s)
{
    int i = sock_index(s);
    if (i < 0 || s_rcvTimeoutMs[i] <= 0) return 1;
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(s, &rf);
    struct timeval tv;
    tv.tv_sec = s_rcvTimeoutMs[i] / 1000;
    tv.tv_usec = (s_rcvTimeoutMs[i] % 1000) * 1000;
    return lwip_select(s + 1, &rf, NULL, NULL, &tv) > 0;
}

ssize_t xb_recv(int s, void *mem, size_t len, int flags)
{
    if (!wait_readable(s)) {
        errno = EAGAIN;
        return -1;
    }
    return lwip_recv(s, mem, len, flags);
}

ssize_t xb_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    if (!wait_readable(s)) {
        errno = EAGAIN;
        return -1;
    }
    return lwip_recvfrom(s, mem, len, flags, from, fromlen);
}

int xb_sock_close(int s)
{
    int i = sock_index(s);
    if (i >= 0) s_rcvTimeoutMs[i] = 0;
    return lwip_close(s);
}

/* ---- entropy for BearSSL ---------------------------------------------- */

static inline unsigned long long xb_rdtsc(void)
{
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

/* Timing jitter between the CPU cycle counter and the scheduler/timer, mixed
 * with SHA-256. Each call also folds in a running counter and the previous
 * state, so repeated calls never return the same seed. */
void xb_gather_entropy(unsigned char *out, int len)
{
    static unsigned char s_pool[32];
    static unsigned int s_calls;
    br_sha256_context h;
    br_sha256_init(&h);
    br_sha256_update(&h, s_pool, sizeof(s_pool));
    s_calls++;
    br_sha256_update(&h, &s_calls, sizeof(s_calls));
    for (int i = 0; i < 64; i++) {
        unsigned long long t0 = xb_rdtsc();
        if ((i & 7) == 0) SDL_Delay(1);
        unsigned long long t1 = xb_rdtsc();
        unsigned int ticks = SDL_GetTicks();
        br_sha256_update(&h, &t0, sizeof(t0));
        br_sha256_update(&h, &t1, sizeof(t1));
        br_sha256_update(&h, &ticks, sizeof(ticks));
    }
    br_sha256_out(&h, s_pool);
    for (int off = 0; off < len; off += 32) {
        unsigned char blk[32];
        br_sha256_init(&h);
        br_sha256_update(&h, s_pool, sizeof(s_pool));
        br_sha256_update(&h, &off, sizeof(off));
        br_sha256_out(&h, blk);
        int n = len - off < 32 ? len - off : 32;
        memcpy(out + off, blk, (size_t)n);
    }
}
