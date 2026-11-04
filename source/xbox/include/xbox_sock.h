/*
 * xbox_sock.h - BSD socket API for the game's network code, on nxdk's lwIP.
 *
 * The POSIX socket headers the net code includes (<sys/socket.h>, <netdb.h>,
 * ...) are shims in this directory that all land here. lwIP's compat macros
 * provide socket()/bind()/sendto()/getaddrinfo()/inet_pton()/fcntl()/close().
 *
 * nxdk builds lwIP without SO_RCVTIMEO, which the NTP clock fetch and UPnP
 * discovery rely on (a blocking recv with a timeout). setsockopt/recv/recvfrom
 * are wrapped to emulate it with select().
 */
#ifndef XBOX_SOCK_H
#define XBOX_SOCK_H

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/inet.h>

int     xb_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
ssize_t xb_recv(int s, void *mem, size_t len, int flags);
ssize_t xb_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
int     xb_sock_close(int s);

#undef setsockopt
#define setsockopt(s, l, n, v, len) xb_setsockopt(s, l, n, v, len)
#undef recv
#define recv(s, m, l, f) xb_recv(s, m, l, f)
#undef recvfrom
#define recvfrom(s, m, l, f, fr, fl) xb_recvfrom(s, m, l, f, fr, fl)
#undef close
#define close(s) xb_sock_close(s)

/* Network bring-up state (src/xbox_net.c): 1 = has an IP, 0 = starting, -1 = failed. */
int xbox_net_state(void);

#endif /* XBOX_SOCK_H */
