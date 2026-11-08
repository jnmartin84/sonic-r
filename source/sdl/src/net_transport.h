/**
 * net_transport.h — Cross-platform UDP network transport
 *
 * Replaces DirectPlay COM layer with POSIX UDP sockets.
 * Works on macOS (SDL build) and KallistiOS (Dreamcast).
 *
 * Model: one host, up to 3 clients. Host binds a UDP port,
 * clients send to the host address. Host tracks clients by
 * their source sockaddr. All packets are raw UDP datagrams
 * with no framing beyond the game's own headers.
 */

#ifndef NET_TRANSPORT_H
#define NET_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

#define NET_PORT_DEFAULT    7847
#define NET_MAX_PLAYERS     4
#define NET_MAX_PACKET      512    /* delta keyframe: 12 + 4*96 = 396 max */

/* Discovery protocol */
#define NET_DISCOVER_MAGIC  0x534F4E52   /* "SONR" */
#define NET_DISCOVER_REPLY  0x534F4E48   /* "SONH" */

/* Session join protocol */
#define NET_MSG_JOIN_REQ    0xFFF00020   /* client → host: request slot (carries client username) */
#define NET_MSG_SLOT_ASSIGN 0xFFF00010   /* host → client: assigned slot (carries name table) */
#define NET_MSG_START_GAME  0xFFF00030   /* host → client: start race */
#define NET_MSG_START_ACK   0xFFF00035   /* client → host: ack start race */
#define NET_MSG_CHAR_CHANGE 0xFFF00040   /* any → all: character selection changed */
#define NET_MSG_PEER_NAME   0xFFF00050   /* host → all clients: peer joined, here is their name */
#define NET_MSG_LEAVE       0xFFF00060   /* client → host: leaving cleanly, free my slot */
#define NET_MSG_LEVEL_READY 0xFFF00070   /* any → host: finished loading, ready for race */
#define NET_MSG_LEVEL_GO    0xFFF00080   /* host → all: all peers loaded, enter countdown */
#define NET_MSG_PAUSE       0xFFF00090   /* any → all (host relays): race paused (1) / resumed (0) by slot */
#define NET_MSG_ACTIVE_MASK 0xFFF000A0   /* host → clients (10 Hz in race): bit i set = slot i still racing */
#define NET_MSG_HOST_LEFT   0xFFF000B0   /* host → clients: host is closing the session */

/* Snapshot networking (in-race, replaces lockstep) */
#define NET_MSG_SNAPSHOT      0xFFF0005F   /* host → clients: world snapshot (10 Hz) */
#define NET_MSG_CLIENT_INPUT  0xFFF0006F   /* client → host: input packet (10 Hz) */

/* Per-player decoration table layout (g_netPlayerDecorations, base 0x68ACF8) */
#define NET_DECO_STRIDE     0x64    /* bytes per player entry */
#define NET_DECO_DPID       0x54    /* offset of DPID / slot ID (int) */
#define NET_DECO_SLOT       0x58    /* offset of player slot index (u16) */
#define NET_DECO_LOBBYCHAR  0x5C    /* offset of lobby character selection (int) */
#define NET_DECO_CHARID     0x60    /* offset of character ID (int) */

/* Platform icon tpage (slot 51, loaded from NET01.RAW).
 * 256x128, 6 icons in a 6x1 row at top, each 32x32.
 * Icon order: DC-US, DC-JP, DC-EU, Linux, macOS, Windows. */
#define TPAGE_PLATFORM_ICONS  51

/* Reserved store-queue "drain" tpage (slot 49). An 8x8 texture whose every
 * texel is the color-key green, generated at boot (PVR_InitDrainTpage) and
 * frozen so it is always resident. A quad drawn from it is fully punched
 * through (invisible) regardless of position/UVs, so on DC it can be emitted
 * as the last PT primitive each frame to drain the final store-queue transfer
 * that would otherwise be lost. Exempt from both tpage-reset loops. */
#define TPAGE_DRAIN           49

/* Letterbox filler tpage (slot 50). A 32x32 RGB565 tile, loaded from
 * PAD448.TEX and repeated across the bottom 640x32 scanline row in the DC
 * split-screen modes that render 448 lines into a 480-line framebuffer
 * (2P horizontal, 3P, 4P — see RenderHeightForMode). Loaded once at boot by
 * PVR_InitPad448Tpage with no reload path, so it must be skipped by every
 * loop that marks a slot state 6 — state 6 is what hands a slot to
 * PVR_ClearAndReset, which frees the VRAM and drops the state to 0.
 *
 * There are TWO such loops, both in logo_render.c: SetupD3DTexturesBegin
 * (0..51) and SetupMenuTexturesD3D (g_uiTexPage..51). Both now skip all three
 * reserved slots — TPAGE_DRAIN, TPAGE_PAD448 and TPAGE_PLATFORM_ICONS. If a
 * fourth reserved slot is ever added, both loops need it. */
#define TPAGE_PAD448          50

/* Master switch for the letterbox filler.
 *
 * SUPERSEDED 2026-08-14 — the display origin-Y shift moves the scanout window
 * so the unrendered band falls outside the visible area, which is a real fix
 * rather than painting over the gap. This is kept switched off rather than
 * deleted so it is one flip away if the register approach turns up an edge
 * case. With it 0 the slot is never claimed, no VRAM is taken, PAD448.TEX is
 * never opened, and PVR_InitPad448Tpage / Pad448_Draw compile to empty
 * functions. The reserved-slot exclusions above stay in place: they test
 * `state != 0`, so an unclaimed slot never reaches them anyway, and leaving
 * them keeps the "all three reserved slots, both loops" rule intact. */
#define PAD448_ENABLE         0
#define PLATFORM_ICON_SIZE    32
#define PLATFORM_ICON_COUNT   6

#define PLATFORM_ICON_DC_US   0
#define PLATFORM_ICON_DC_JP   1
#define PLATFORM_ICON_DC_EU   2
#define PLATFORM_ICON_LINUX   3
#define PLATFORM_ICON_MACOS   4
#define PLATFORM_ICON_WINDOWS 5
#define PLATFORM_ICON_UNKNOWN 6

/* DC flashrom region codes (match KOS FLASHROM_REGION_*) */
#define NET_REGION_UNKNOWN 0
#define NET_REGION_JAPAN   1
#define NET_REGION_US      2
#define NET_REGION_EUROPE  3

/* Per-slot platform/region accessors (network.c) */
void net_set_slot_platform(int slot, const char *platform, uint8_t region);
const char *net_get_slot_platform(int slot);
uint8_t net_get_slot_region(int slot);

/* Map platform string + region to icon index (0-5). */
static inline int net_platform_icon(const char *platform, uint8_t region)
{
    if (platform == NULL || platform[0] == '\0') {
        return PLATFORM_ICON_UNKNOWN;
    }
    if (platform[0] == 'd') {
        switch (region) {
            case NET_REGION_JAPAN:
                return PLATFORM_ICON_DC_JP;
            case NET_REGION_EUROPE:
                return PLATFORM_ICON_DC_EU;
            default:
                return PLATFORM_ICON_DC_US;
        }
    }
    if (platform[0] == 'w') {
        return PLATFORM_ICON_WINDOWS;
    }
    if (platform[0] == 'l') {
        return PLATFORM_ICON_LINUX;
    }
    if (platform[0] == 'm') {
        return PLATFORM_ICON_MACOS;
    }
    return PLATFORM_ICON_UNKNOWN;
}

/* UV rect for a platform icon in the 256x128 tpage.
 * Icons 0-5 are in a row at (0,0). Icon 6 (unknown) is at (224,96). */
static inline void net_platform_icon_uv(int icon, int *uvX, int *uvY)
{
    if (icon == PLATFORM_ICON_UNKNOWN) {
        *uvX = 224;
        *uvY = 224;
    }
    else {
        *uvX = icon * PLATFORM_ICON_SIZE;
        *uvY = 0;
    }
}

/**
 * Initialize the network subsystem. Call once at startup.
 * Returns 0 on success, -1 on failure.
 */
int net_transport_init(void);

/**
 * Host: bind a UDP socket on the given port and begin accepting clients.
 * Returns 0 on success, -1 on failure.
 */
int net_host_start(int port);

/**
 * Client: set the host address to send packets to.
 * Does NOT establish a connection (UDP is connectionless) — just
 * records the target. Returns 0 on success, -1 on failure.
 */
int net_client_connect(const char *host_ip, int port);

/**
 * Send a packet to the host (client → host).
 * Returns bytes sent, or -1 on error.
 */
int net_send_to_host(const void *data, int len);

/**
 * Send a packet to all connected players (host broadcasts,
 * client sends to host for relay).
 * Returns 0 on success, -1 on error.
 */
int net_broadcast(const void *data, int len);

/**
 * Send a packet to a specific player by slot index.
 * Returns bytes sent, or -1 on error.
 */
int net_send_to(int player_slot, const void *data, int len);

/**
 * Non-blocking receive. Returns bytes received and fills
 * *from_slot with the sender's player slot (-1 if unknown).
 * Returns 0 if no data available, -1 on error.
 */
int net_recv(void *buf, int maxlen, int *from_slot);

/**
 * Register a new client address (host-side, called when a new
 * player's first packet arrives). Returns assigned slot index,
 * or -1 if full.
 */
int net_register_client(int slot);

/**
 * Send a LAN discovery broadcast. Hosts listening will reply.
 */
int net_discover_send(int port);

/**
 * Check for discovery replies. Returns 1 if a host was found
 * and fills host_ip (must be at least 16 bytes). Returns 0 if
 * no reply yet.
 */
int net_discover_check(char *host_ip, int host_ip_len);

/**
 * Returns 1 if the transport is active (host or client), 0 otherwise.
 */
int net_is_active(void);

/**
 * Returns 1 if we are the host, 0 if client.
 */
int net_is_host(void);

/**
 * Returns the local player's slot index (0 for host, 1-3 for clients,
 * -1 if not yet assigned).
 */
int net_local_slot(void);

/**
 * Set the local player's slot index. Called when the host assigns
 * a slot to this client.
 */
void net_set_local_slot(int slot);

/**
 * Shut down the transport, close sockets.
 */
void net_close(void);

/**
 * Host: free a client slot. Future packets from the previous source IP
 * are treated as a fresh unknown sender (subject to the host's join gate).
 */
void net_unregister_slot(int slot);

/**
 * Host: 1 if the slot currently holds a connected player. Slot 0 (the host
 * itself) counts as connected while the session is up.
 */
int net_slot_is_connected(int slot);

/**
 * One-shot ICMP echo probe. Sends an ICMP echo request to ip and waits up
 * to timeout_ms for the reply, returns RTT in milliseconds, or -1 on
 * timeout / unreachable / kernel refusal. Uses unprivileged datagram-mode
 * ICMP on POSIX (the same path /sbin/ping uses on modern macOS/Linux),
 * IcmpSendEcho on Windows. The `port` argument is ignored — kept for
 * caller stability while we evaluate UDP-vs-ICMP behaviour across NATs.
 */
int net_probe_ping(const char *ip, int port, int timeout_ms);

#endif /* NET_TRANSPORT_H */
