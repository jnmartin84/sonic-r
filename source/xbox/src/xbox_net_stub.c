/*
 * xbox_net_stub.c - "no network" implementation of net_transport.h.
 *
 * The desktop transport (net/net_transport.c) is built on BSD sockets. nxdk has
 * lwIP, but wiring that up is its own job, so the first Xbox build ships
 * single-player / split-screen only: every call reports that no session exists.
 * Replace this file with a lwIP-backed transport to add online play.
 */
#include "net_transport.h"

static int     s_localSlot;

/* net_set_slot_platform / net_get_slot_platform / net_get_slot_region live in
 * net/network.c and are not part of the transport, so they are not stubbed. */

int  net_transport_init(void)                                { return -1; }
int  net_host_start(int port)                                { (void)port; return -1; }
int  net_client_connect(const char *host_ip, int port)       { (void)host_ip; (void)port; return -1; }
int  net_send_to_host(const void *data, int len)             { (void)data; (void)len; return -1; }
int  net_broadcast(const void *data, int len)                { (void)data; (void)len; return -1; }
int  net_send_to(int player_slot, const void *data, int len) { (void)player_slot; (void)data; (void)len; return -1; }
int  net_recv(void *buf, int maxlen, int *from_slot)         { (void)buf; (void)maxlen; (void)from_slot; return 0; }
int  net_register_client(int slot)                           { (void)slot; return -1; }
int  net_discover_send(int port)                             { (void)port; return -1; }
int  net_discover_check(char *host_ip, int host_ip_len)      { (void)host_ip; (void)host_ip_len; return 0; }
int  net_is_active(void)                                     { return 0; }
int  net_is_host(void)                                       { return 0; }
int  net_local_slot(void)                                    { return s_localSlot; }
void net_set_local_slot(int slot)                            { s_localSlot = slot; }
void net_close(void)                                         { }
void net_unregister_slot(int slot)                           { (void)slot; }
int  net_slot_is_connected(int slot)                         { (void)slot; return 0; }
int  net_probe_ping(const char *ip, int port, int timeout_ms){ (void)ip; (void)port; (void)timeout_ms; return -1; }
