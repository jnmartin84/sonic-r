/**
 * network.c — Network/DirectPlay functions
 *
 * Host/client model with lockstep synchronization.
 * See Network_annotated.c for full protocol documentation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "net_transport.h"
#include "matchmaker.h"
#include "net_interp.h"
#include "net_delta.h"
#ifdef SONICR_DC
#include <kos/thread.h>
#include <kos/mutex.h>
#else
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "endian_util.h"

extern void platform_pump_events(void);

/* =====================================================================
 * Receive thread — packet ring buffer
 *
 * Binary: background thread at 0x4D8D10 calls IDirectPlay4::Receive
 * in a loop and signals Windows events. SDL: pthread reads net_recv()
 * and enqueues packets; main thread dequeues in ApplyNetworkPlayerState.
 * ===================================================================== */

#define NET_QUEUE_SIZE 64

typedef struct {
    char data[NET_MAX_PACKET];
    int  len;
    int  from_slot;
} NetPacket;

static NetPacket        s_packetQueue[NET_QUEUE_SIZE];
static int              s_queueHead;    /* next write position */
static int              s_queueTail;    /* next read position */
#ifdef SONICR_DC
static mutex_t          s_queueMutex = MUTEX_INITIALIZER;
static kthread_t       *s_recvThread;
#else
static pthread_mutex_t  s_queueMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t        s_recvThread;
#endif
static volatile int     s_threadRunning;

#ifdef SONICR_DC
#define QUEUE_LOCK()   mutex_lock(&s_queueMutex)
#define QUEUE_UNLOCK() mutex_unlock(&s_queueMutex)
#else
#define QUEUE_LOCK()   pthread_mutex_lock(&s_queueMutex)
#define QUEUE_UNLOCK() pthread_mutex_unlock(&s_queueMutex)
#endif

/* Enqueue a received packet (called from recv thread only) */
static void net_enqueue(const char *data, int len, int from_slot)
{
    QUEUE_LOCK();
    int next = (s_queueHead + 1) % NET_QUEUE_SIZE;
    if (next != s_queueTail) {  /* not full */
        NetPacket *pkt = &s_packetQueue[s_queueHead];
        if (len > NET_MAX_PACKET) len = NET_MAX_PACKET;
        memcpy(pkt->data, data, (size_t)len);
        pkt->len = len;
        pkt->from_slot = from_slot;
        s_queueHead = next;
    }
    QUEUE_UNLOCK();
}

/* Dequeue one packet (called from main thread only).
 * Returns bytes copied, 0 if empty. */
static int net_dequeue(char *buf, int maxlen, int *from_slot)
{
    QUEUE_LOCK();
    if (s_queueTail == s_queueHead) {
        QUEUE_UNLOCK();
        return 0;
    }
    NetPacket *pkt = &s_packetQueue[s_queueTail];
    int len = pkt->len;
    if (len > maxlen) len = maxlen;
    memcpy(buf, pkt->data, (size_t)len);
    if (from_slot) *from_slot = pkt->from_slot;
    s_queueTail = (s_queueTail + 1) % NET_QUEUE_SIZE;
    QUEUE_UNLOCK();
    return len;
}

/* Receive thread function — binary 0x4D8D10 */
static void *net_recv_thread_func(void *arg)
{
    (void)arg;
    char buf[NET_MAX_PACKET];
    int from_slot;

    while (s_threadRunning) {
        int len = net_recv(buf, sizeof(buf), &from_slot);
        if (len > 0) {
            net_enqueue(buf, len, from_slot);
#ifdef SONICR_DC
        } else {
            thd_sleep(1);
        }
#else
        } else {
            usleep(1000);
        }
#endif
    }
    return NULL;
}

/* Start the receive thread — called from StartNetworkThread (leaf_batch.c) */
void NetRecvThread_Start(void)
{
    if (s_threadRunning) return;
    s_queueHead = 0;
    s_queueTail = 0;
    s_threadRunning = 1;
#ifdef SONICR_DC
    s_recvThread = thd_create(0, net_recv_thread_func, NULL);
#else
    pthread_create(&s_recvThread, NULL, net_recv_thread_func, NULL);
#endif
    DebugLog("Network receive thread started\n");
}

/* Stop the receive thread — called from network shutdown */
void NetRecvThread_Stop(void)
{
    if (!s_threadRunning) return;
    s_threadRunning = 0;
#ifdef SONICR_DC
    thd_join(s_recvThread, NULL);
    s_recvThread = NULL;
#else
    pthread_join(s_recvThread, NULL);
#endif
    DebugLog("Network receive thread stopped\n");
}

/* Level-load sync barrier state */
static int s_levelReadyMask;    /* bitmask: bit N set when slot N reports loaded */
static int s_levelGoReceived;   /* client: set to 1 when host sends LEVEL_GO */

/* Start-game ack state */
static int s_startAckMask;      /* bitmask: bit N set when slot N acks START_GAME */


void NetLevelSyncBarrier(void)
{
    extern void platform_pump_events(void);

    if (g_netSessionActive == 0 || !net_is_active()) return;

    s_levelReadyMask = 0;
    s_levelGoReceived = 0;

    if (net_is_host()) {
        /* Host marks itself ready */
        s_levelReadyMask |= (1 << 0);

        /* Build expected mask: bits for all connected players */
        int expectedMask = 0;
        for (int i = 0; i < g_netPlayerCount && i < NET_MAX_PLAYERS; i++)
            expectedMask |= (1 << i);

        DebugLog("Host: waiting for all peers (expected mask=0x%x)\n", expectedMask);

        /* Pump until everyone reports in */
        while (s_levelReadyMask != expectedMask) {
            ApplyNetworkPlayerState();
            platform_pump_events();
#ifdef SONICR_DC
            thd_sleep(1);
#else
            usleep(1000);
#endif
        }

        DebugLog("Host: all peers ready, sending LEVEL_GO\n");

        /* Broadcast GO repeatedly for 500ms so lossy links don't miss it */
        {
        char _gobuf[4]; wl32(_gobuf, (uint32_t)NET_MSG_LEVEL_GO);
        for (int r = 0; r < 5; r++) {
            net_broadcast(_gobuf, 4);
#ifdef SONICR_DC
            thd_sleep(100);
#else
            usleep(100000);
#endif
        }
        }
    } else {
        /* Client: send READY to host, retrying every 100ms until GO arrives */
        char _readybuf[4]; wl32(_readybuf, (uint32_t)NET_MSG_LEVEL_READY);
        int readyCounter = 0;
        net_send_to_host(_readybuf, 4);
        DebugLog("Client: sent LEVEL_READY, waiting for GO\n");

        /* Wait for host's GO */
        while (!s_levelGoReceived) {
            ApplyNetworkPlayerState();
            platform_pump_events();
#ifdef SONICR_DC
            thd_sleep(1);
#else
            usleep(1000);
#endif
            if (++readyCounter >= 100) {
                net_send_to_host(_readybuf, 4);
                readyCounter = 0;
            }
        }

        DebugLog("Client: received LEVEL_GO, entering race\n");
    }
}

/* Network-specific globals */
extern int    g_netSessionFrame;        /* 0x0068AEFC */
extern int    g_netSessionAlive;        /* 0x00689AF4 */
/* g_currentPlayerIdx at 0x68ACD8 — declared in sonicr_globals.h */
extern int    g_netPlayerRecvd[];       /* 0x0068AEEC — per-player "received" flags (host-side) */
extern HANDLE g_netDataEvent;           /* 0x0068AC70 — data processing sync event */
extern char   g_lobbyPlayerData[];      /* 0x0068A900 — per-player lobby, stride 0xC8 */
extern int    g_netExpectedPlayers;    /* 0x00689AF8 */
extern int    g_netLobbyDataCopy[];    /* 0x0068AE88 — lobby config destination (48 bytes) */
extern char   g_netSessionDesc[];      /* 0x0068ACEC — session descriptor (0x1CC bytes) */

/* Per-player received flags — same memory as g_readyFlags (0x0068AF08) */
int g_readyFlags[16];                       /* 0x0068AF08 — per-slot ready/recv flag */
#define s_playerRecvFlags g_readyFlags

/* Input state buffer for network — outgoing (local → host) */
static unsigned short s_netInputBuffer[MAX_PLAYERS]; /* 0x0068AEDE */

#if 0
/* Sync ack packet buffer — 0x0068AF00..0x0068AF07 */
static struct {
    int            header;      /* 0x68AF00 — packet type magic */
    unsigned short playerIdx;   /* 0x68AF04 — sender's player index */
    unsigned short frameNum;    /* 0x68AF06 — session frame number */
} s_netSyncPacket;
#endif

/* Lobby player data layout */
#define LOBBY_STRIDE     0xC8   /* bytes per player in g_lobbyPlayerData */
#define LOBBY_INPUT_OFF  0x8C   /* offset of input word within each entry */

/* NET_DECO_* layout defines in net_transport.h */
#define NET_DECO_NAME    0x14   /* offset of name glyphs (int[], -1 terminated) */
#define NET_DECO_NAME_MAX 16   /* max glyphs in name area */

/* Raw matchmaker usernames keyed by slot.  Populated at the same time as
 * the decoration table's glyph encoding (which is lossy: glyphs can only
 * represent alphanumerics). Used when building the SLOT_ASSIGN name table
 * for late joiners. */
static char s_netSlotNames[NET_MAX_PLAYERS][MM_MAX_USERNAME];

#define NET_PLATFORM_LEN 16
static char    s_netSlotPlatform[NET_MAX_PLAYERS][NET_PLATFORM_LEN];
static uint8_t s_netSlotRegion[NET_MAX_PLAYERS];

/* Client: 1 once we've received SLOT_ASSIGN from the host. Gates client→host
 * sends so we don't spray pad packets at a host that hasn't accepted us. */
static int s_haveSlotAssign = 0;
int NetClientHasSlot(void) { return s_haveSlotAssign; }

static DWORD s_lastHostPacketMs = 0;
void NetPauseRemote(int slot, int state);

/* Host: per-slot timestamp of last received packet. Used by the host-side
 * stale watchdog to free slots that have gone silent (e.g. crashed clients,
 * NAT remappings) so their slot becomes available to new joiners. */
static DWORD s_hostSlotLastRecvMs[NET_MAX_PLAYERS] = {0};

/* Snapshot networking — peer-authoritative: each player owns their own physics.
 * Server-authoritative: host runs physics for all, clients predict + reconcile. */
#define NET_PEER_AUTHORITATIVE 1

#define NET_SNAP_INTERVAL  1   /* every frame = 30 Hz snapshot rate */
static int      s_netSendCounter;
static uint16_t s_snapshotSeq;
static uint16_t s_clientInputSeq;
static uint16_t s_lastRecvInputSeq[4];
static uint16_t s_lastRecvSnapSeq;

static NetDeltaSendState s_deltaSend[NET_MAX_PLAYERS];
static NetDeltaRecvState s_deltaRecv[NET_MAX_PLAYERS];
static int s_hostKeyframeCountdown;
static int s_clientKeyframeCountdown;

#define KEYFRAME_INTERVAL 30

/* Protocol version, carried in header bytes 10-11 of NET_MSG_SNAPSHOT and
 * NET_MSG_CLIENT_INPUT. The v1.0 release wrote those bytes as zero and never
 * read them, so a peer reporting 0 is v1.0. The wire format itself is
 * unchanged at version 1; the marker exists so the next change can be
 * detected instead of silently mis-decoded. */
#define NET_PROTOCOL_VERSION 1
static unsigned short s_peerProtoVersion[MAX_PLAYERS];
static unsigned char  s_peerProtoSeen[MAX_PLAYERS];

void EnumNetworkSessions(int flag);   /* CHAR_CHANGE broadcast, defined below */

/* Apply a character id learned over the network to a slot this machine does
 * not simulate. charId drives the animation tables; _unk_0x1E0 is what the
 * renderer draws, and InitPlayerSlot copies it from charId once at level
 * init, so a change that lands after that would leave the old model on
 * screen (issue #13). If the slot's frame stream is already live, re-seed it
 * from the new character's table the way TickPlayerAnimation does on an
 * animation change, so the cursor never points into the old character's
 * data. */
static void net_apply_char_id(int slot, short charId)
{
    Player *pl = &g_playerBase[slot];

    if (pl->charId == charId && pl->_unk_0x1E0 == charId) return;

    pl->charId = charId;
    pl->_unk_0x1E0 = charId;

    if (slot < 10 && g_animDataPtrs[slot] != NULL && g_charAnimTables != NULL) {
        void **tables = (void **)g_charAnimTables;
        uintptr_t *animPtrs = (uintptr_t *)tables[charId * 2];
        int animCount = (int)(uintptr_t)tables[charId * 2 + 1];
        int animIdx = pl->animId;

        g_animDataPtrs[slot] = NULL;
        if (animPtrs != NULL && animIdx >= 0 && animIdx < animCount) {
            uintptr_t frameAddr = animPtrs[animIdx];
            if (frameAddr == 0 && animIdx + 1 < animCount) {
                frameAddr = animPtrs[animIdx + 1];
            }
            g_animDataPtrs[slot] = (const short *)frameAddr;
        }
    }
}

static void net_note_peer_version(int playerIdx, unsigned int ver)
{
    if (playerIdx < 0 || playerIdx >= MAX_PLAYERS) return;
    if (s_peerProtoSeen[playerIdx] && s_peerProtoVersion[playerIdx] == ver) return;
    s_peerProtoSeen[playerIdx] = 1;
    s_peerProtoVersion[playerIdx] = (unsigned short)ver;
    DebugLog("Net: player %d speaks protocol %u, ours is %u%s\n",
             playerIdx, ver, (unsigned int)NET_PROTOCOL_VERSION,
             (ver != NET_PROTOCOL_VERSION) ? " (MISMATCH)" : "");
}

static void NetSnapshotReset(void)
{
    int i;
    s_netSendCounter = 0;
    s_snapshotSeq = 1;
    s_clientInputSeq = 1;
    s_lastRecvSnapSeq = 0;
    memset(s_lastRecvInputSeq, 0, sizeof(s_lastRecvInputSeq));
    for (i = 0; i < NET_MAX_PLAYERS; i++) {
        net_delta_reset_send(&s_deltaSend[i]);
        net_delta_reset_recv(&s_deltaRecv[i]);
    }
    s_hostKeyframeCountdown = 0;
    s_clientKeyframeCountdown = 0;
    memset(s_peerProtoSeen, 0, sizeof(s_peerProtoSeen));
    memset(s_peerProtoVersion, 0, sizeof(s_peerProtoVersion));
}

/* Per-player state within a snapshot — matches 0xFFF0002F layout minus 4-byte header */
#define SNAP_HEADER_SIZE  12
typedef struct {
    unsigned char playerIdx;
    unsigned char _pad0[3];
    int   posX, posY, posZ;
    int   velX, velY, velZ;
    unsigned short ringCount;
    unsigned short anglePitch;
    unsigned short angleYaw;
    unsigned short angleRoll;
    unsigned short animFrameIdx;
    unsigned short moveMode;
    unsigned short collisionLayer;
    unsigned short loopMode;
    int   yOffset;
    unsigned short pitchCombo;
    unsigned short surfNormX, surfNormY, surfNormZ;
    int   groundHeight;
    unsigned short unk94;
    unsigned short unkBC;
    unsigned short itemEffectId;
    unsigned short _pad1;
    int   itemHeightMod;
    unsigned short itemEffectState;
    unsigned short _pad2;
    int   effectYMod;
    unsigned short unk78, unk7A;
    int   lap1Time, lap2Time, lap3Time;
    unsigned char lapsCompleted;
    unsigned char racePosition;
    unsigned char collisionCount;
    unsigned char animId;
    unsigned char groundedFlag;
    unsigned char lapCrossFlag;
    unsigned short dynamicSpeedMode;
    int   forwardSpeed;
    int   lateralSpeed;
    unsigned short invincTimer;
    unsigned short brakeCounter;
    unsigned short abilityTimer;
    unsigned char  unk80;
    unsigned char  sfxTrigger;
    unsigned char  unkD6;
    unsigned char  abilityState;
    unsigned char  unk1F0;
    unsigned char  _pad4;
    int   unk9C;
} NetPlayerSnap; /* 128 bytes */

#if 0
static void net_snap_from_player(NetPlayerSnap *s, int idx)
{
    Player *pl = &g_playerBase[idx];
    s->playerIdx       = (unsigned char)idx;
    memset(s->_pad0, 0, sizeof(s->_pad0));
    s->posX            = pl->posX;
    s->posY            = pl->posY;
    s->posZ            = pl->posZ;
    s->velX            = pl->velX;
    s->velY            = pl->velY;
    s->velZ            = pl->velZ;
    s->ringCount       = (unsigned short)pl->ringCount;
    s->anglePitch      = (unsigned short)pl->anglePitch;
    s->angleYaw        = (unsigned short)pl->angleYaw;
    s->angleRoll       = (unsigned short)pl->angleRoll;
    s->animFrameIdx    = (unsigned short)pl->animFrameIdx;
    s->moveMode        = (unsigned short)pl->moveMode;
    s->collisionLayer  = (unsigned short)pl->collisionLayer;
    s->loopMode       = (unsigned short)pl->loopMode;
    s->yOffset         = pl->yOffset;
    s->pitchCombo      = (unsigned short)pl->pitchCombo;
    s->surfNormX       = (unsigned short)pl->surfNormX;
    s->surfNormY       = (unsigned short)pl->surfNormY;
    s->surfNormZ       = (unsigned short)pl->surfNormZ;
    s->groundHeight    = pl->groundHeight;
    s->unk94           = (unsigned short)pl->_unk_0x94;
    s->unkBC           = (unsigned short)pl->_unk_0xBC;
    s->itemEffectId    = (unsigned short)pl->itemEffectId;
    s->_pad1           = 0;
    s->itemHeightMod   = pl->itemHeightMod;
    s->itemEffectState = (unsigned short)pl->itemEffectState;
    s->_pad2           = 0;
    s->effectYMod      = pl->effectYMod;
    s->unk78           = (unsigned short)pl->_unk_0x78;
    s->unk7A           = (unsigned short)pl->_unk_0x7A;
    s->lap1Time        = pl->lap1Time;
    s->lap2Time        = pl->lap2Time;
    s->lap3Time        = pl->lap3Time;
    s->lapsCompleted   = (unsigned char)pl->lapsCompleted;
    s->racePosition    = (unsigned char)pl->racePosition;
    s->collisionCount  = (unsigned char)pl->collisionCount;
    s->animId          = (unsigned char)pl->animId;
    s->groundedFlag    = (unsigned char)pl->groundedFlag;
    s->lapCrossFlag    = (unsigned char)pl->lapCrossFlag;
    s->dynamicSpeedMode = (unsigned short)pl->dynamicSpeedMode;
    s->forwardSpeed    = pl->forwardSpeed;
    s->lateralSpeed    = pl->lateralSpeed;
    s->invincTimer     = (unsigned short)pl->invincTimer;
    s->brakeCounter    = (unsigned short)pl->brakeCounter;
    s->abilityTimer    = (unsigned short)pl->abilityTimer;
    s->unk80           = (unsigned char)pl->_unk_0x80;
    s->sfxTrigger      = (unsigned char)pl->sfxTrigger;
    s->unkD6           = (unsigned char)pl->_unk_0xD6;
    s->abilityState    = (unsigned char)pl->abilityState;
    s->unk1F0          = (unsigned char)pl->_unk_0x1F0;
    s->_pad4           = 0;
    s->unk9C           = pl->_unk_0x9C;
}

static void net_snap_to_player(const NetPlayerSnap *s, Player *pl)
{
    pl->posX             = s->posX;
    pl->posY             = s->posY;
    pl->posZ             = s->posZ;
    pl->velX             = s->velX;
    pl->velY             = s->velY;
    pl->velZ             = s->velZ;
    pl->ringCount        = (short)s->ringCount;
    pl->anglePitch       = (short)s->anglePitch;
    pl->angleYaw         = (short)s->angleYaw;
    pl->angleRoll        = (short)s->angleRoll;
    pl->animFrameIdx     = (short)s->animFrameIdx;
    pl->moveMode         = (short)s->moveMode;
    pl->collisionLayer   = (short)s->collisionLayer;
    pl->loopMode        = (short)s->loopMode;
    pl->yOffset          = s->yOffset;
    pl->pitchCombo       = (short)s->pitchCombo;
    pl->surfNormX        = (short)s->surfNormX;
    pl->surfNormY        = (short)s->surfNormY;
    pl->surfNormZ        = (short)s->surfNormZ;
    pl->groundHeight     = s->groundHeight;
    pl->_unk_0x94        = (short)s->unk94;
    pl->_unk_0xBC        = (short)s->unkBC;
    pl->itemEffectId     = (short)s->itemEffectId;
    pl->itemHeightMod    = s->itemHeightMod;
    pl->itemEffectState  = (short)s->itemEffectState;
    pl->effectYMod       = s->effectYMod;
    pl->_unk_0x78        = (short)s->unk78;
    pl->_unk_0x7A        = (short)s->unk7A;
    pl->lap1Time        = s->lap1Time;
    pl->lap2Time        = s->lap2Time;
    pl->lap3Time        = s->lap3Time;
    pl->lapsCompleted    = (short)s->lapsCompleted;
    pl->collisionCount   = (int)s->collisionCount;
    pl->animId           = (short)s->animId;
    pl->groundedFlag     = (short)s->groundedFlag;
    pl->dynamicSpeedMode = (short)s->dynamicSpeedMode;
    pl->forwardSpeed     = s->forwardSpeed;
    pl->lateralSpeed     = s->lateralSpeed;
    pl->invincTimer      = (short)s->invincTimer;
    pl->brakeCounter     = (short)s->brakeCounter;
    pl->abilityTimer     = (short)s->abilityTimer;
    pl->_unk_0x80        = (short)s->unk80;
    pl->sfxTrigger       = (short)s->sfxTrigger;
    pl->_unk_0xD6        = (short)s->unkD6;
    pl->abilityState     = (short)s->abilityState;
    pl->_unk_0x1F0       = (int)s->unk1F0;
    pl->_unk_0x9C        = s->unk9C;
}
#endif

static int net_build_snapshot(char *buf, int maxlen)
{
    int i, offset;
    uint8_t count = (uint8_t)g_netPlayerCount;
    int is_keyframe = (s_hostKeyframeCountdown == 0);

    {
        uint16_t seq = s_snapshotSeq++;
        uint16_t frame = (uint16_t)g_netSessionFrame;
        wl32(buf + 0, (uint32_t)NET_MSG_SNAPSHOT);
        wl16(buf + 4, seq);
        wl16(buf + 6, frame);
        buf[8] = count;
        buf[9] = is_keyframe ? 1 : 0;
        wl16(buf + 10, NET_PROTOCOL_VERSION);
    }

    offset = SNAP_HEADER_SIZE;
    for (i = 0; i < (int)count; i++) {
        int wrote = net_delta_encode(&s_deltaSend[i], &g_playerBase[i],
                                     i, is_keyframe,
                                     (unsigned char *)buf + offset,
                                     maxlen - offset);
        if (wrote == 0) return 0;
        offset += wrote;
    }

    if (is_keyframe)
        s_hostKeyframeCountdown = KEYFRAME_INTERVAL;
    else
        s_hostKeyframeCountdown--;

    return offset;
}

static void net_apply_snapshot(const char *buf, int len)
{
    uint16_t seq;
    uint8_t count;
    int i, offset;
    int is_keyframe;

    if (len < SNAP_HEADER_SIZE) return;

    seq = rl16u(buf + 4);

    if ((int16_t)(seq - s_lastRecvSnapSeq) <= 0)
        return;
    s_lastRecvSnapSeq = seq;

    count = (uint8_t)buf[8];
    is_keyframe = (buf[9] & 1);
    net_note_peer_version(0, rl16u(buf + 10));   /* snapshots come from the host, player 0 */
    if (count > NET_MAX_PLAYERS) count = NET_MAX_PLAYERS;

    offset = SNAP_HEADER_SIZE;
    for (i = 0; i < (int)count; i++) {
        int playerIdx;
        int consumed;
        Player *pl;

        if (offset >= len) break;
        playerIdx = (int)(unsigned char)buf[offset];
        if (playerIdx >= MAX_PLAYERS) break;

        pl = &g_playerBase[playerIdx];

        if (playerIdx == g_localPlayerIndex) {
#if NET_PEER_AUTHORITATIVE
            /* Peer-auth: local player is self-authoritative.
             * Still need to advance past this player's data in the packet. */
            {
                NetDeltaRecvState tmp;
                if (is_keyframe) {
                    tmp.valid = 0;
                } else {
                    tmp = s_deltaRecv[playerIdx];
                }
                Player dummy;
                consumed = net_delta_decode(&tmp, (const unsigned char *)buf + offset,
                                            len - offset, is_keyframe, &dummy);
                if (consumed == 0) break;
                s_deltaRecv[playerIdx] = tmp;
            }
#else
            consumed = net_delta_decode(&s_deltaRecv[playerIdx],
                                        (const unsigned char *)buf + offset,
                                        len - offset, is_keyframe, pl);
            if (consumed == 0) break;
#endif
            offset += consumed;
            continue;
        }

        {
            short prevLaps = pl->lapsCompleted;
            consumed = net_delta_decode(&s_deltaRecv[playerIdx],
                                        (const unsigned char *)buf + offset,
                                        len - offset, is_keyframe, pl);
            if (consumed == 0) break;
            if (pl->lapsCompleted == 3 && prevLaps < 3) {
                int counter = g_finishOrderCounter;
                pl->trackProgress = (int)(0xFFFFFFFF - (unsigned int)counter);
                g_finishOrderCounter = counter + 1;
            }
        }
        NetInterpRecord(playerIdx);
        offset += consumed;
    }
}

void net_set_slot_name(int slot, const char *name)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return;
    if (name == NULL) name = "";
    strncpy(s_netSlotNames[slot], name, MM_MAX_USERNAME - 1);
    s_netSlotNames[slot][MM_MAX_USERNAME - 1] = '\0';
}

const char *net_get_slot_name(int slot)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return "";
    return s_netSlotNames[slot];
}

void net_set_slot_platform(int slot, const char *platform, uint8_t region)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return;
    if (platform == NULL) platform = "";
    strncpy(s_netSlotPlatform[slot], platform, NET_PLATFORM_LEN - 1);
    s_netSlotPlatform[slot][NET_PLATFORM_LEN - 1] = '\0';
    s_netSlotRegion[slot] = region;
}

const char *net_get_slot_platform(int slot)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return "";
    return s_netSlotPlatform[slot];
}

uint8_t net_get_slot_region(int slot)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return 0;
    return s_netSlotRegion[slot];
}

/* MODE A/B selector, bit 0 of the byte at 0x0068A8CA — byte 2 of
 * g_netGameInfoDest[11], inside the 48-byte config buffer UpdateNetworkSync
 * broadcasts whole, so a toggle travels with the next send and an arriving
 * config replaces it (0x4ce9b3 copies it in). F5 in the lobby toggles it
 * (0x48B4D5); 0x4cea18 tests it at race start to pick g_netGameStartState,
 * 3 when set (MODE B) and 1 when clear (MODE A). */
#define NET_LOBBY_MODE_BYTE 0x2E   /* 0x0068A8CA - 0x0068A89C */

static unsigned char *net_lobby_mode_ptr(void)
{
    return (unsigned char *)g_netGameInfoDest + NET_LOBBY_MODE_BYTE;
}

int net_lobby_mode_b(void)
{
    return *net_lobby_mode_ptr() & 1;
}

void net_lobby_set_mode_b(int on)
{
    unsigned char *p = net_lobby_mode_ptr();
    if (on) {
        *p |= 0x01;
    }
    else {
        *p &= (unsigned char)0xFE;
    }
}

void net_lobby_set_mode_byte(unsigned char v)
{
    *net_lobby_mode_ptr() = v;
}

/* Write a name into a decoration entry's glyph area.
 * Glyphs: a=0..z=25, 0=26..9=35. Terminated by -1. */
void net_deco_set_name(char *entry, const char *name)
{
    int *dst = (int *)(entry + NET_DECO_NAME);
    int i = 0;
    while (*name && i < NET_DECO_NAME_MAX - 1) {
        char c = *name++;
        int glyph;
        if (c >= 'a' && c <= 'z')      glyph = c - 'a';
        else if (c >= 'A' && c <= 'Z') glyph = c - 'A';  /* map to same lowercase glyphs */
        else if (c >= '0' && c <= '9') glyph = 26 + (c - '0');
        else { i++; dst++; continue; }  /* skip unknown */
        *dst++ = glyph;
        i++;
    }
    *dst = -1;  /* sentinel */
}

/* Host: forget the player that occupied a slot, once the transport has freed
 * it. The lobby and HUD walk g_netPlayerCount, so that has to shrink as well
 * or the departed player keeps rendering from the stale name cache.
 *
 * The count is recomputed from the highest still-connected slot rather than
 * decremented: if slot 1 leaves while slot 2 stays, a decrement would drop
 * slot 2 below the bound and hide a player who is still here. */
static void host_release_slot_identity(int slot)
{
    if (slot <= 0 || slot >= NET_MAX_PLAYERS) {
        return;
    }

    net_set_slot_name(slot, "");
    net_set_slot_platform(slot, "", 0);

    char *entry = g_netPlayerDecorations + slot * NET_DECO_STRIDE;
    memset(entry, 0, NET_DECO_STRIDE);
    net_deco_set_name(entry, "");

    int highest = 0;
    for (int s = 1; s < NET_MAX_PLAYERS; s++) {
        if (net_slot_is_connected(s)) {
            highest = s;
        }
    }
    g_netPlayerCount = highest + 1;

    /* Push the empty name to the remaining clients — their PEER_NAME handler
     * already treats it as "this slot has no player". */
    enum { PEER_SIZE = 8 + MM_MAX_USERNAME + NET_PLATFORM_LEN + 1 };
    char peerbuf[PEER_SIZE];
    memset(peerbuf, 0, sizeof(peerbuf));
    wl32(peerbuf + 0, (uint32_t)NET_MSG_PEER_NAME);
    wl32(peerbuf + 4, (uint32_t)slot);
    for (int s = 1; s < NET_MAX_PLAYERS; s++) {
        if (net_slot_is_connected(s)) {
            net_send_to(s, peerbuf, PEER_SIZE);
        }
    }

    DebugLog("Host: released slot %d, playerCount=%d\n", slot, g_netPlayerCount);
}

/* Player struct network fields: Player.netActive (0x714), Player.netConnected (0x718) */

/* Forward declarations for sub-functions */
void SendNetworkPacket(const void *data, int len);  /* 0x487628 — see definition below UpdateNetworkSync */
void WaitForNetworkData(void);              /* FUN_004876CC */
void ProcessNetworkGameData(void);          /* FUN_004877C4 */
int  ReadLocalInput(void);                  /* FUN_004774A0 */
void ApplyNetworkPlayerState(void);         /* FUN_004D9568 */

/**
 * UpdateNetworkHost — 0x00487CE8 — 121 bytes
 * Host: collect local input and prepare for broadcast.
 */
void UpdateNetworkHost(void)
{
    if (g_netGameStarted == 0) {
        /* Pre-game: send lobby data (header 0xFF00000F + input).
         * Binary sends from contiguous memory at 0x68AEC8; we pack. */
        {
            char lobbyPkt[16];
            wl32(lobbyPkt, (uint32_t)g_gameDataPacketHeader);
            wl16(lobbyPkt + 4, (uint16_t)g_gameDataPacketFrame);
            { int _i; for (_i = 0; _i < 4; _i++) wl16(lobbyPkt + 6 + _i * 2, g_netRecvInput[_i]); }
            lobbyPkt[14] = 0; lobbyPkt[15] = 0;
            SendNetworkPacket(lobbyPkt, 16);
        }
        WaitForNetworkData();

        /* Set local player data */
        /* s_netInputBuffer[g_localPlayerIndex] = ... */
        /* s_packetHeader = 0xFF00000F; */
        /* s_localInput = ReadLocalInput(); */
    }
    else {
        /* In-game: receive pending packets (client input, keepalives)
         * before collecting and broadcasting combined input.
         * Binary: DirectPlay recv thread handled this implicitly. */
        ApplyNetworkPlayerState();
        ProcessNetworkGameData();
    }

    /* Clear combined input */
    g_combinedInputState = 0;
}

int net_should_run_physics(int playerIdx)
{
    if (g_netSessionActive == 0) return 1;
#if NET_PEER_AUTHORITATIVE
    return (playerIdx == g_localPlayerIndex);
#else
    if (net_is_host()) return 1;
    return (playerIdx == g_localPlayerIndex);
#endif
}

/**
 * SendNetworkHostData — 0x00487D64 — 345 bytes
 * Host: wait for all clients to report, then broadcast combined state.
 * Uses Windows Events for synchronization.
 */
/**
 * net_poll_one — dequeue one packet from the recv thread queue
 * (or net_recv directly if no thread). Returns bytes, 0 if empty.
 */
static int net_poll_one(char *buf, int maxlen, int *from_slot)
{
    return s_threadRunning
        ? net_dequeue(buf, maxlen, from_slot)
        : net_recv(buf, maxlen, from_slot);
}

/* ---- Players leaving during an online race ---------------------------------
 * A player who quits, crashes or loses the connection is taken out of the race
 * on every machine: their character disappears (no model, shadow, name tag,
 * minimap dot or collision), they drop to the bottom of the ranking, and a
 * "<NAME> LEFT THE RACE" notice is shown. The race goes on for the others.
 *  - host: a client is dropped on NET_MSG_LEAVE or after 5 s of silence; the
 *    host then tells the clients with NET_MSG_ACTIVE_MASK (10 Hz);
 *  - client: if the host goes (NET_MSG_HOST_LEFT, or 5 s of silence) nobody can
 *    relay the others any more, so the client finishes the race on its own and
 *    returns to the host/join screen afterwards. */
extern int g_netAcceptJoins;
static unsigned s_goneMask;           /* slots that left this race */
static char     s_noticeText[48];
static char     s_noticeText2[48];
static DWORD    s_noticeUntil;
static void np_player_gone(int slot);  /* online pause: forget a pauser that left */

static void net_notice(const char *l1, const char *l2)
{
    snprintf(s_noticeText, sizeof(s_noticeText), "%s", l1 ? l1 : "");
    snprintf(s_noticeText2, sizeof(s_noticeText2), "%s", l2 ? l2 : "");
    for (char *c = s_noticeText; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    s_noticeUntil = timeGetTime() + 5000;
    if (s_noticeUntil == 0) s_noticeUntil = 1;
}

/* 1 if slot left the current online race (it must not be drawn or collided). */
int NetPlayerGone(int slot)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return 0;
    if (g_netSessionActive == 0 && g_isNetworkGame == 0) return 0;
    if (slot == g_localPlayerIndex) return 0;
    return (s_goneMask >> slot) & 1;
}

/* Called at the start of every race. */
void NetRaceResetPlayers(void)
{
    s_goneMask = 0;
    s_noticeUntil = 0;
}

/* Take slot out of the race on this machine. */
void NetPlayerLeft(int slot, int quiet)
{
    if (slot < 0 || slot >= NET_MAX_PLAYERS || slot == g_localPlayerIndex) return;
    if ((s_goneMask >> slot) & 1) return;
    s_goneMask |= 1u << slot;
    g_playerBase[slot].netActive = 0;
    g_playerBase[slot].netConnected = 0;
    np_player_gone(slot);
    DebugLog("Net: slot %d ('%s') left the race\n", slot, net_get_slot_name(slot));
    if (!quiet) {
        char line[48];
        const char *n = net_get_slot_name(slot);
        snprintf(line, sizeof(line), "%.20s LEFT THE RACE", (n && n[0]) ? n : "A PLAYER");
        net_notice(line, NULL);
    }
}

/* Host: a client left (LEAVE or silence) during the race. The slot's name is
 * kept until the race is over (results, notice); re-hosting starts fresh. */
static void host_drop_racer(int slot)
{
    s_hostSlotLastRecvMs[slot] = 0;
    net_unregister_slot(slot);
    NetPlayerLeft(slot, 0);
}

/* Host: tell the clients who is still in the race. */
static void host_send_active_mask(void)
{
    char b[8];
    unsigned mask = 0;
    for (int i = 0; i < g_netPlayerCount && i < NET_MAX_PLAYERS; i++) {
        if (!((s_goneMask >> i) & 1)) mask |= 1u << i;
    }
    wl32(b + 0, (uint32_t)NET_MSG_ACTIVE_MASK);
    wl32(b + 4, (uint32_t)mask);
    net_broadcast(b, 8);
}

/* Client: the host is gone. Finish the race alone, leave the session now so
 * the post-race flow goes back to the host/join screen. */
static void client_lost_host(const char *why)
{
    DebugLog("Net: %s - finishing the race offline\n", why);
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (i != g_localPlayerIndex && i < (int)g_numPlayers) NetPlayerLeft(i, 1);
    }
    net_notice(why, "THE RACE CONTINUES OFFLINE");
    CloseDirectPlaySession();
}

void SendNetworkHostData(void)
{
    int i;
    DWORD now = timeGetTime();

    for (i = 0; i < g_netPlayerCount; i++) {
        s_playerRecvFlags[i] = (g_playerBase[i].netConnected == 0) ? 1 : 0;

        if (i == g_localPlayerIndex) continue;
        if (g_playerBase[i].netActive == 0) continue;
        if (s_hostSlotLastRecvMs[i] == 0) continue;

        DWORD silent = now - s_hostSlotLastRecvMs[i];
        if (silent > 5000) {
            DebugLog("Player %d disconnected (silent %ums)\n", i, (unsigned)silent);
            host_drop_racer(i);
        } else if (silent > 2000) {
            s_netInputBuffer[i] = 0;
        }
    }

    /* A player who drops out is removed from the race (NetPlayerLeft) and the
     * others keep racing; with nobody left the host finishes the race alone and
     * goes back to its lobby afterwards. */

    g_netSyncEstablished = 1;
}

/**
 * UpdateNetworkClient — 0x00487EC0 — 185 bytes
 * Client: apply received data from host to player structs.
 */
void UpdateNetworkClient(void)
{
    if (g_raceFinished != 0) return;

    ApplyNetworkPlayerState();

    if (g_netGameStarted == 0) {
        /* CLIENT path: read local input, feed local physics, send to host at 10 Hz */
        unsigned short localInput = (unsigned short)ReadLocalInput();
        s_netInputBuffer[g_localPlayerIndex] = localInput;
        g_perPlayerInput[g_localPlayerIndex] = localInput;

        if (g_introCountdown < 3) {
#if NET_PEER_AUTHORITATIVE
            /* Peer-auth: send delta-compressed state every frame */
            s_netSendCounter++;
            if (s_netSendCounter >= NET_SNAP_INTERVAL) {
                s_netSendCounter = 0;
                {
                    int is_kf = (s_clientKeyframeCountdown == 0);
                    char pkt[12 + 1 + NET_DELTA_KEYFRAME_BODY + NET_DELTA_MASK_BYTES + 4];
                    uint16_t seq = s_clientInputSeq++;
                    int bodyLen;
                    wl32(pkt + 0, (uint32_t)NET_MSG_CLIENT_INPUT);
                    wl16(pkt + 4, seq);
                    wl16(pkt + 6, localInput);
                    pkt[8] = (uint8_t)g_localPlayerIndex;
                    pkt[9] = is_kf ? 1 : 0;
                    wl16(pkt + 10, NET_PROTOCOL_VERSION);
                    bodyLen = net_delta_encode(
                        &s_deltaSend[g_localPlayerIndex],
                        &g_playerBase[g_localPlayerIndex],
                        g_localPlayerIndex, is_kf,
                        (unsigned char *)pkt + 12,
                        (int)sizeof(pkt) - 12);
                    if (bodyLen > 0)
                        SendNetworkPacket(pkt, 12 + bodyLen);
                    if (is_kf)
                        s_clientKeyframeCountdown = KEYFRAME_INTERVAL;
                    else
                        s_clientKeyframeCountdown--;
                }
            }
#else
            /* Server-auth: send input every frame
             * Wire layout (12 bytes):
             *   +0x00 int      header     = NET_MSG_CLIENT_INPUT
             *   +0x04 uint16_t seq
             *   +0x06 uint16_t inputBits
             *   +0x08 uint8_t  playerIdx
             *   +0x09 uint8_t  pad        = 0
             *   +0x0A uint16_t protocol version */
            {
                char _ipkt[12];
                wl32(_ipkt + 0, (uint32_t)NET_MSG_CLIENT_INPUT);
                wl16(_ipkt + 4, s_clientInputSeq++);
                wl16(_ipkt + 6, localInput);
                _ipkt[8] = (uint8_t)g_localPlayerIndex;
                _ipkt[9] = 0;
                wl16(_ipkt + 10, NET_PROTOCOL_VERSION);
                SendNetworkPacket(_ipkt, 12);
            }
#endif
        }
    }
    else {
        /* Binary 0x487EE0 — g_netGameStarted != 0.
         * Reads local input, stores in s_netInputBuffer, distributes to
         * g_perPlayerInput.  Unreachable in normal play: host has
         * g_netGameStarted=1 but calls UpdateNetworkHost, not this. */
        unsigned short localInput = (unsigned short)ReadLocalInput();
        s_netInputBuffer[g_localPlayerIndex] = localInput;

        int i;
        for (i = 0; i < g_netPlayerCount; i++) {
            g_perPlayerInput[i] = s_netInputBuffer[i];
        }
    }
}

/**
 * UpdateNetworkSync — 0x00487642 — 48 bytes
 * IDirectPlay4::Send to DPID_ALLPLAYERS (idTo=0). Broadcasts to all players.
 * EAX = idFrom (DPID), EDX = data ptr, EBX = data size.
 * SDL: net_broadcast via UDP.
 */
void UpdateNetworkSync(const void *data, int len)
{
    if (!net_is_active()) return;
    if (net_broadcast(data, len) < 0) {
        DebugLog("Broadcast failed\n");
    }
}

/**
 * SendNetworkPacket — 0x00487628 — 26 bytes
 * IDirectPlay4::Send to DPID_SERVERPLAYER (idTo=1).
 * EAX = idFrom (g_currentPlayerIdx DPID), EDX = data ptr, EBX = data size.
 * SDL: client sends to host, host broadcasts (binary host would self-send).
 */
void SendNetworkPacket(const void *data, int len)
{
    if (!net_is_active()) return;
    if (net_is_host()) {
        net_broadcast(data, len);
    } else {
        /* Don't send anything to the host until they've assigned us a slot.
         * Otherwise a client whose JOIN_REQ was lost (or a stale process
         * still pointed at an old host IP) will spray pad packets at a
         * peer that has no idea who we are. */
        if (!s_haveSlotAssign) return;
        net_send_to_host(data, len);
    }
}

/**
 * CloseDirectPlaySession — 0x004875CC
 * Destroys the local player and closes the DirectPlay session.
 */
void CloseDirectPlaySession(void)
{
    /* Polite goodbye so the host frees our slot immediately instead of
     * waiting for its stale-slot watchdog to fire. Best-effort over UDP. */
    if (net_is_active() && !net_is_host() && s_haveSlotAssign) {
        /* Wire: 4 bytes, int header = NET_MSG_LEAVE */
        char _lbuf[4]; wl32(_lbuf, (uint32_t)NET_MSG_LEAVE);
        net_send_to_host(_lbuf, 4);
    }
    /* Host closing: tell the clients right away (a few times, it is UDP) so
     * they do not wait 5 s for the silence timeout. */
    if (net_is_active() && net_is_host()) {
        char _hbuf[4]; wl32(_hbuf, (uint32_t)NET_MSG_HOST_LEFT);
        for (int _k = 0; _k < 3; _k++) net_broadcast(_hbuf, 4);
    }
    s_haveSlotAssign = 0;
    memset(s_hostSlotLastRecvMs, 0, sizeof(s_hostSlotLastRecvMs));

    /* Host/client discriminator, set to 1 by CreateNetworkSession (main.c).
     * Session-scoped: clearing it here keeps it symmetric with where it is
     * raised, so hosting once and then joining someone else's session in the
     * same process runs as a client rather than taking host branches. */
    g_netGameStarted = 0;

    NetRecvThread_Stop();
    net_close();
    DebugLog("Session Closed & Player Destroyed.\n");
}

/**
 * CloseDirectPlayLobby — FUN_00486928 — 135 bytes
 * Enumerates and closes DirectPlay lobby sessions, releases lobby object.
 * No-op On SDL (no DirectPlay).
 */
void CloseDirectPlayLobby(void)
{
    /* DirectPlay lobby cleanup — stubbed for SDL */
}

/* =====================================================================
 * Network helper stubs
 * ===================================================================== */

/**
 * WaitForNetworkData — 0x004876CC — 247 bytes
 *
 * Client-side frame sync: waits for the host's broadcast, then sends
 * a sync ack packet back to the host (via DPID_SERVERPLAYER).
 * Distributes received input from g_netRecvInput into per-player
 * lobby data (stride 0xC8) and g_perPlayerInput.
 * On timeout: clears g_netSessionAlive and closes the provider handle.
 */
void WaitForNetworkData(void)
{
    int i;
    char buf[NET_MAX_PACKET];
    int from_slot;
    int gotData = 0;

    /* Timeout: 60s on first frame, 5s thereafter — binary: WaitForSingleObject */
    DWORD timeout = (g_netSyncEstablished == 0) ? 60000 : 5000;
    DWORD deadline = timeGetTime() + timeout;

    /* Poll for game data broadcast (0xFF0000F0, 16 bytes) from host */
    while (timeGetTime() < deadline) {
        int len = net_poll_one(buf, sizeof(buf), &from_slot);
        if (len >= 16) {
            unsigned int hdr = rl32u(buf);
            if (hdr == 0xFF0000F0u) {
                /* Game data broadcast from host — extract frame + input */
                g_gameDataPacketFrame = rl16u(buf + 4);
                g_netSessionFrame = (int)g_gameDataPacketFrame;
                if (g_netPlayerCount > 0) {
                    int _n = g_netPlayerCount;
                    if (_n > (len - 6) / 2) _n = (len - 6) / 2;
                    { int _i; for (_i = 0; _i < _n; _i++) g_netRecvInput[_i] = rl16u(buf + 6 + _i * 2); }
                }
                gotData = 1;
                break;
            }
        } else if (len == 0) {
            usleep(1000);  /* 1ms idle */
        }
    }

    if (!gotData) {
        /* Timeout — binary: clears g_netSessionAlive, closes provider */
        g_netSessionAlive = 0;
        DebugLog("WaitForNetworkData: timeout\n");
        return;
    }

    /* Frame consistency check — binary 0x4876F5 */
    {
        int frame = (int)(unsigned short)g_netSessionFrame;
        if (frame != g_netFrameCounter) {
            DebugLog("Frame sync mismatch\n");
        }
    }

    /* Build sync ack packet (8 bytes) and send to host
     * Wire layout:
     *   +0x00 int            header    = 0xFFF00000  (s_netSyncPacket.header)
     *   +0x04 unsigned short playerIdx               (s_netSyncPacket.playerIdx)
     *   +0x06 unsigned short frameNum                 (s_netSyncPacket.frameNum) */
    {
        char _syncbuf[8];
        wl32(_syncbuf + 0, (uint32_t)0xFFF00000u);
        wl16(_syncbuf + 4, (uint16_t)g_localPlayerIndex);
        wl16(_syncbuf + 6, (uint16_t)g_netSessionFrame);
        SendNetworkPacket(_syncbuf, 8);
    }

    /* Distribute received input to per-player arrays */
    if (g_netPlayerCount > 0) {
        for (i = 0; i < g_netPlayerCount; i++) {
            unsigned short input = g_netRecvInput[i];
            *(unsigned short *)(g_lobbyPlayerData + i * LOBBY_STRIDE + LOBBY_INPUT_OFF) = input;
            g_perPlayerInput[i] = input;
        }
    }

    g_netSyncEstablished = 1;
}

/**
 * ProcessNetworkGameData — 0x004877C4 — 407 bytes
 *
 * HOST-side per-frame game data processing.
 * Collects input from all players, fills in last-known data for any
 * that haven't reported, reads local input, builds a 16-byte game data
 * packet (header + frame + per-player input), broadcasts it to all,
 * then distributes input to per-player arrays.
 *
 * Uses g_netPlayerRecvd[] (0x68AEEC) to track which players have
 * reported this frame — separate from g_readyFlags (0x68AF08).
 */
void ProcessNetworkGameData(void)
{
    int localIdx;
    int i;

    /* Binary: SetEvent(g_netDataEvent) — sync with recv thread.
     * SDL: not needed, queue handles synchronization. */

    /* Mark local player as "received" */
    localIdx = (int)(unsigned short)g_localPlayerIndex;     /* word [0x68ACDC] */
    g_netPlayerRecvd[localIdx] = 1;                         /* [localIdx*4 + 0x68AEEC] */

    /* Loop 1: Fill in last-known data for players who haven't reported */
    if (g_netPlayerCount > 0) {
        for (i = 0; i < g_netPlayerCount; i++) {
            if (g_netPlayerRecvd[i] == 0) {                 /* [i*4 + 0x68AEEC] */
                g_netPlayerRecvd[i] = 1;
                s_netInputBuffer[i] = g_netRecvInput[i];    /* [0x68AEDE] ← [0x68AECE] */
            }
        }
    }

    /* Loop 2: For connected players, update recv buffer from input buffer */
    if (g_netPlayerCount > 0) {
        for (i = 0; i < g_netPlayerCount; i++) {
            if (g_playerBase[i].netConnected != 0) {
                g_netRecvInput[i] = s_netInputBuffer[i];    /* [0x68AECE] ← [0x68AEDE] */
            }
        }
    }

    /* Loop 3: Clear received flags for next frame */
    if (g_netPlayerCount > 0) {                             /* [0x68AEE8] */
        for (i = 0; i < g_netPlayerCount; i++) {
            g_netPlayerRecvd[i] = 0;                        /* [i*4 + 0x68AEEC] */
        }
    }

    /* Read local input and store in recv buffer */
    {
        unsigned short input = (unsigned short)ReadLocalInput(); /* call 0x4774A0 */
        localIdx = (int)(unsigned short)g_localPlayerIndex;
        g_netRecvInput[localIdx] = input;                   /* [localIdx*2 + 0x68AECE] */
    }

    /* Broadcast world snapshot at 10 Hz (every 3rd frame) */
    s_netSendCounter++;
    if (s_netSendCounter >= NET_SNAP_INTERVAL) {
        s_netSendCounter = 0;
        char snapBuf[NET_MAX_PACKET];
        int snapLen = net_build_snapshot(snapBuf, sizeof(snapBuf));
        if (snapLen > 0)
            UpdateNetworkSync(snapBuf, snapLen);
        host_send_active_mask();
    }

    /* Loop 5: Distribute received input to per-player arrays */
    if (g_netPlayerCount > 0) {
        for (i = 0; i < g_netPlayerCount; i++) {
            unsigned short input = g_netRecvInput[i];                                     /* [i*2 + 0x68AECE] */
            *(unsigned short *)(g_lobbyPlayerData + i * LOBBY_STRIDE + LOBBY_INPUT_OFF) = input; /* [i*0xC8 + 0x68A98C] */
            g_perPlayerInput[i] = input;                                           /* [i*2 + 0x9020C8] */
        }
    }

    /* Binary: ResetEvent(g_netDataEvent) — not needed on SDL */
}

/**
 * SendNetworkClientData — 0x00487F7C
 * Keepalive (Section 1) + camera sync (Section 2) only.
 * Section 3 (0xFFF0002F per-player state) removed — replaced by
 * snapshot broadcast (host) and NET_MSG_CLIENT_INPUT (client).
 */
void SendNetworkClientData(void)
{
    Player *pl;   /* player struct pointer */
    int i;

    /* ================================================================
     * Section 1: Keepalive ping — every 10 frames during intro
     * Packet: 0xFFF0004F, 12 bytes
     * ================================================================ */
    if ((g_totalFrames2 % 10) == 0) {                       /* idiv 0xA; test edx */
        /* Send if intro countdown is 1..3, OR introTimer is nonzero */
        if ((g_introCountdown != 0 && g_introCountdown < 4) /* [0x901CC4] */
            || g_introTimer != 0) {                         /* [0x901CC8] */
            /* Keepalive packet (12 bytes / 0xC):
             *   +0x00 int header    = 0xFFF0004F
             *   +0x04 int dpid      = g_currentPlayerIdx
             *   +0x08 int playerIdx = g_localPlayerIndex (word, zero-extended) */
            char _kabuf[12];
            wl32(_kabuf + 0, (uint32_t)0xFFF0004Fu);
            wl32(_kabuf + 4, (uint32_t)g_currentPlayerIdx);       /* [0x68ACD8] */
            wl32(_kabuf + 8, (uint32_t)(unsigned short)g_localPlayerIndex); /* [0x68ACDC] word */
            /* ebx=0xC, edx=&keepalive, eax=DPID; call UpdateNetworkSync */
            UpdateNetworkSync(_kabuf, 12);
        }
    }

    /* ================================================================
     * Section 2+3: Require g_netGameStarted
     * ================================================================ */
    if (g_netGameStarted == 0) return;                      /* [0x68ACE4] */

    /* ================================================================
     * Section 2: Camera sync — race finished AND post-race camera == 0
     * Packet: 0xFFF0003F, 68 bytes (0x44)
     * Per-player camera position (3 ints) + state bytes + raceFinished
     * ================================================================ */
    if (g_raceFinished != 0 && g_postRaceCameraMode == 0) { /* [0x901C88], [0x901C84] */
        /* Camera sync packet, 68 bytes (0x44):
         *   +0x00 int            header              = 0xFFF0003F
         *   +0x04 int            perPlayerPos[4 * 3] — 3 ints per player (0x50,0x54,0x58)
         *   +0x34 unsigned char  racePos[4]          — racePosition byte per player (0x5C)
         *   +0x38 unsigned char  lapsDone[4]         — lapsCompleted byte per player (0x5E)
         *   +0x3C unsigned char  collCount[4]        — collisionCount byte per player (0x1F4)
         *   +0x40 unsigned short raceFinished        — g_raceFinished
         *   +0x42 unsigned short _pad                = 0 */
        char _cambuf[0x44];
        memset(_cambuf, 0, sizeof(_cambuf));
        wl32(_cambuf + 0x00, (uint32_t)0xFFF0003Fu);

        if (g_numViewports > 0) {
            for (i = 0; i < g_numViewports; i++) {          /* [0x6E9910] */
                pl = &g_playerBase[i];
                _cambuf[0x34 + i] = (unsigned char)pl->lapsCompleted;  /* racePos — 0x5E */
                _cambuf[0x38 + i] = (unsigned char)pl->racePosition;   /* lapsDone — 0x5C */
                _cambuf[0x3C + i] = (unsigned char)pl->collisionCount; /* collCount — 0x1F4 */
                /* 3 ints of position/camera data */
                wl32(_cambuf + 0x04 + i * 12 + 0, (uint32_t)pl->lap1Time); /* 0x50 */
                wl32(_cambuf + 0x04 + i * 12 + 4, (uint32_t)pl->lap2Time); /* 0x54 */
                wl32(_cambuf + 0x04 + i * 12 + 8, (uint32_t)pl->lap3Time); /* 0x58 */
            }
        }

        wl16(_cambuf + 0x40, (uint16_t)g_raceFinished); /* [0x901C88] word */
        /* ebx=0x44, edx=&camPkt, eax=DPID; call UpdateNetworkSync */
        UpdateNetworkSync(_cambuf, 0x44);
        return;
    }

}

/**
 * InitNetworkGame — 0x00487A38 — 570 bytes
 *
 * Transitions from the network lobby to the actual race.
 * Called after all players are ready in the NetworkScreen.
 *
 * 1. Enumerates sessions and copies lobby config data
 * 2. Clears all player active flags, then sets active for connected players
 * 3. Initializes lobby player data and player connectivity flags
 * 4. Finds local player's slot by matching DPID in decoration table
 * 5. Logs all player info (DPIDs, slots, charIds, connectivity)
 * 6. Broadcasts the full session descriptor (0x1CC bytes) to all players
 * 7. Sets g_netSessionActive=1, g_netSessionAlive=1, g_netSessionFrame=0
 */
void InitNetworkGame(void)
{
    int i;

    /* Copy 48 bytes of lobby config: g_netGameInfoDest → g_netLobbyDataCopy */
    {                                                       /* memcpy(0x68AE88, 0x68A89C, 0x30) */
        int *src = g_netGameInfoDest;                       /* 0x68A89C */
        int *dst = g_netLobbyDataCopy;                      /* 0x68AE88 */
        for (i = 0; i < 12; i++)
            dst[i] = src[i];
    }

    /* Clear active flag for ALL 5 player slots */
    for (i = 0; i < 5; i++) {
        g_playerBase[i].netActive = 0;
    }

    /* Set active flag for connected players only */
    for (i = 0; i < g_netPlayerCount; i++) {
        g_playerBase[i].netActive = 1;
    }

    /* Clear lobby data and player connected flags for 4 slots */
    for (i = 0; i < 4; i++) {
        *(int *)(g_lobbyPlayerData + i * LOBBY_STRIDE) = 0;
        g_playerBase[i].netConnected = 0;
    }

    /* Set connected + active + lobby flags for connected players */
    for (i = 0; i < g_netPlayerCount; i++) {
        *(int *)(g_lobbyPlayerData + i * LOBBY_STRIDE + 0x00) = 1;
        g_playerBase[i].netConnected = 1;
        *(int *)(g_lobbyPlayerData + i * LOBBY_STRIDE + 0x04) = 1;
        g_playerBase[i].netActive = 1;
    }

    /* Find local player's slot by matching DPID in decoration table */
    for (i = 0; i < g_netPlayerCount; i++) {                /* stride 0x64 */
        int *decoDpid = (int *)(g_netPlayerDecorations      /* [i*0x64 + 0x68AD4C] */
                        + i * NET_DECO_STRIDE + NET_DECO_DPID);
        if (*decoDpid == g_currentPlayerIdx) {              /* memcmp: repe cmpsb */
            unsigned short slot = *(unsigned short *)        /* [i*0x64 + 0x68AD50] */
                (g_netPlayerDecorations + i * NET_DECO_STRIDE + NET_DECO_SLOT);
            g_localPlayerIndex = (int)slot;                 /* mov [0x68ACDC], ax */
        }
    }

    /* Binary broadcast 460-byte session descriptor here via DirectPlay.
     * In our UDP model, the host already has this data and clients don't
     * need to broadcast it. Skip on client to avoid choking slow links. */
    if (net_is_host())
        UpdateNetworkSync(g_netSessionDesc, 0x1CC);

    /* Set final state for race entry */
    g_netExpectedPlayers = g_netPlayerCount;                /* [0x689AF8] = [0x68AEE8] */
    g_netSessionActive = 1;                                    /* [0x68AF18] = 1 */
    g_netSessionAlive = 1;                                  /* [0x689AF4] = 1 */
    g_netSessionFrame = 0;                                  /* [0x68AEFC] = 0 (word) */

    /* Bridge the binary's address aliasing: in the original EXE,
     * g_netTrackIndex (0x68A8A0) and g_netRaceSubModeIndex (0x68A8A2)
     * overlapped g_netGameInfoDest[1] at the same address.  The lobby
     * writes track/mode into [1]; race init reads g_netTrackIndex.
     * In C these are separate variables, so copy explicitly. */
    g_netTrackIndex       = (int)(short)(g_netGameInfoDest[1] & 0xFFFF);       /* [1] low word */
    g_netRaceSubModeIndex = (int)(short)(g_netGameInfoDest[1] >> 16);        /* [1] high word */
    g_netWeatherType      = (int)(short)(g_netGameInfoDest[2] & 0xFFFF);     /* [2] low word */
    g_netPlayerMode       = (int)(short)(g_netGameInfoDest[2] >> 16);        /* [2] high word */

    /* SDL: host tells clients to start the game — includes track/mode and
     * per-player character IDs so the client can populate player
     * structs before race init (binary propagated these via DirectPlay
     * session/player data that our SDL port doesn't have).
     * Client skips this — it already received START_GAME to get here. */
    if (net_is_host()) {
        /* START_GAME packet layout (16 + NET_MAX_PLAYERS*2 bytes):
         *   +0x00 int   hdr              = NET_MSG_START_GAME
         *   +0x04 int   playerCount
         *   +0x08 short trackIndex
         *   +0x0A short raceSubModeIndex
         *   +0x0C short weatherType
         *   +0x0E short playerMode
         *   +0x10 short charIds[NET_MAX_PLAYERS] — per-player character selection */
        char _startbuf[16 + NET_MAX_PLAYERS * 2];
        wl32(_startbuf + 0x00, (uint32_t)NET_MSG_START_GAME);
        wl32(_startbuf + 0x04, (uint32_t)g_netPlayerCount);
        wl16(_startbuf + 0x08, (uint16_t)g_netTrackIndex);
        wl16(_startbuf + 0x0A, (uint16_t)g_netRaceSubModeIndex);
        wl16(_startbuf + 0x0C, (uint16_t)(short)g_netWeatherType);
        wl16(_startbuf + 0x0E, (uint16_t)(short)g_netPlayerMode);
        for (i = 0; i < NET_MAX_PLAYERS; i++)
            wl16(_startbuf + 0x10 + i * 2, (uint16_t)g_playerBase[i].charId);

        /* Retry until all clients ACK, matching NetLevelSyncBarrier pattern */
        s_startAckMask = (1 << 0);  /* host is slot 0, already "acked" */
        int expectedAck = 0;
        for (i = 0; i < g_netPlayerCount && i < NET_MAX_PLAYERS; i++)
            expectedAck |= (1 << i);

        int retryCount = 0;
        while (s_startAckMask != expectedAck && retryCount < 50) {
            UpdateNetworkSync(_startbuf, sizeof(_startbuf));
            for (int w = 0; w < 100; w++) {
                ApplyNetworkPlayerState();
                platform_pump_events();
#ifdef SONICR_DC
                thd_sleep(1);
#else
                usleep(1000);
#endif
                if (s_startAckMask == expectedAck) break;
            }
            retryCount++;
        }
        DebugLog("Host: START_GAME ack mask=0x%x (expected=0x%x, retries=%d)\n",
                 s_startAckMask, expectedAck, retryCount);
    }

    NetSnapshotReset();
}

/**
 * ApplyNetworkPlayerState — FUN_004D9568
 *
 * Receive-side message dispatcher. Drains the packet queue and
 * dispatches by header magic.
 *
 *   Any phase:
 *     NET_MSG_JOIN_REQ    — client requests slot
 *     NET_MSG_SLOT_ASSIGN — host assigns slot to client
 *     NET_MSG_START_GAME  — host signals race start
 *     NET_MSG_CHAR_CHANGE — character selection update
 *     NET_MSG_CLIENT_INPUT — client input (+ full state in peer-auth)
 *
 *   g_netGameStarted != 0 (host lobby):
 *     0xFF00000F — lobby pad data from client
 *     0xFFF0004F — keepalive ping
 *
 *   g_netGameStarted == 0 (client race):
 *     NET_MSG_SNAPSHOT — world snapshot from host
 *     0xFFF0003F — camera sync
 */
void ApplyNetworkPlayerState(void)
{
    char buf[NET_MAX_PACKET];
    int from_slot;
    int len;
    int i;

    /* Clear per-player ready flags — binary: loop [0x68AF08..+0x10] = 0 */
    for (i = 0; i < 4; i++)
        s_playerRecvFlags[i] = 0;

    /* Process all pending network messages */
    while ((len = net_poll_one(buf, sizeof(buf), &from_slot)) > 0) {
        if (len < 4) continue;  /* need at least a header */

        unsigned int header = rl32u(buf);

        /* Polite client goodbye — free the slot immediately rather than
         * waiting for the stale-slot watchdog. Handled before the unregistered
         * -slot fatal check so a stray LEAVE doesn't trip it. */
        /* Online pause (see NetPauseTick). */
        if (header == NET_MSG_PAUSE && len >= 12) {
            int slot  = rl32s(buf + 4);
            int state = rl32s(buf + 8);
            if (net_is_host()) {
                net_broadcast(buf, len);          /* relay to the other clients */
            }
            if (slot >= 0 && slot < NET_MAX_PLAYERS && slot != g_localPlayerIndex) {
                NetPauseRemote(slot, state);
            }
            continue;
        }

        if (net_is_host() && header == NET_MSG_LEAVE) {
            if (from_slot >= 1 && from_slot < NET_MAX_PLAYERS) {
                DebugLog("Host: slot %d sent LEAVE\n", from_slot);
                if (!g_netAcceptJoins && g_playerBase[from_slot].netActive) {
                    host_drop_racer(from_slot);          /* mid-race: others go on */
                } else {
                    net_unregister_slot(from_slot);
                    s_hostSlotLastRecvMs[from_slot] = 0;
                    host_release_slot_identity(from_slot);
                }
            }
            continue;
        }

        /* ---- Client: who is still racing / host closing ---- */
        if (!net_is_host() && header == NET_MSG_ACTIVE_MASK && len >= 8) {
            unsigned mask = rl32u(buf + 4);
            for (i = 0; i < (int)g_numPlayers && i < NET_MAX_PLAYERS; i++) {
                if (i != g_localPlayerIndex && !((mask >> i) & 1)
                    && g_playerBase[i].netActive) {
                    NetPlayerLeft(i, 0);
                }
            }
            continue;
        }
        if (!net_is_host() && header == NET_MSG_HOST_LEFT) {
            if (g_netSessionActive && g_introCountdown == 0 && g_postRaceCameraMode == 0
                && g_netDisconnectFlag == 0) {
                client_lost_host("THE HOST LEFT THE RACE");
                return;                                   /* session is closed now */
            }
            continue;
        }

        /* ---- Start-game ack from client ---- */
        if (net_is_host() && header == NET_MSG_START_ACK) {
            if (from_slot >= 0 && from_slot < NET_MAX_PLAYERS) {
                s_startAckMask |= (1 << from_slot);
                DebugLog("Host: slot %d acked START_GAME (mask=0x%x)\n",
                         from_slot, s_startAckMask);
            }
            continue;
        }

        /* ---- Level-load sync: client reports ready ---- */
        if (net_is_host() && header == NET_MSG_LEVEL_READY) {
            if (from_slot >= 0 && from_slot < NET_MAX_PLAYERS) {
                s_levelReadyMask |= (1 << from_slot);
                DebugLog("Host: slot %d level ready (mask=0x%x)\n",
                         from_slot, s_levelReadyMask);
            }
            continue;
        }

        /* ---- Level-load sync: host says GO ---- */
        if (!net_is_host() && header == NET_MSG_LEVEL_GO) {
            s_levelGoReceived = 1;
            DebugLog("Client: received LEVEL_GO\n");
            continue;
        }

        /* Implicit-join used to be a fallback for clients that never sent
         * NET_MSG_JOIN_REQ.  Now that JOIN_REQ is the only supported entry
         * point and carries the joiner's username, ANY non-JOIN_REQ packet
         * from a not-yet-registered slot is a bug — hard-fail so we notice.
         * (JOIN_REQ itself is the legitimate first packet from a new slot,
         * so it must not trip this check.) */
        if (net_is_host() && from_slot >= 1 && from_slot < NET_MAX_PLAYERS
            && from_slot >= g_netPlayerCount
            && header != NET_MSG_JOIN_REQ) {
            /* struct { int hdr; int slot; } reply;
             * reply.hdr  = NET_MSG_SLOT_ASSIGN;
             * reply.slot = from_slot;
             * net_send_to(from_slot, &reply, sizeof(reply));
             * g_netPlayerCount = from_slot + 1;
             * DebugLog("Host: implicit join for slot %d, playerCount=%d\n",
             *          from_slot, g_netPlayerCount);
             * { extern int g_netLobbyPlayerSlot;
             *   g_netLobbyPlayerSlot = from_slot; } */
            fprintf(stderr,
                    "FATAL: packet header 0x%08x from unregistered slot %d before NET_MSG_JOIN_REQ. "
                    "Client must send JOIN_REQ first.\n",
                    header, from_slot);
            abort();
        }

        /* ---- SDL session protocol: join request / slot assignment ---- */
        if (header == NET_MSG_JOIN_REQ && net_is_host()) {
            extern int g_netAcceptJoins;
            if (!g_netAcceptJoins) {
                /* Not in the lobby (race / podium / results): drop it, the
                 * client retries every second until we are back. */
                if (from_slot >= 1 && from_slot < NET_MAX_PLAYERS
                    && !g_playerBase[from_slot].netActive) {
                    net_unregister_slot(from_slot);
                }
                continue;
            }
            /* Host: client requested a slot. from_slot was auto-assigned
             * by net_recv(). Payload carries the client's matchmaker
             * username; we use it to populate the decoration table.
             * Reply with the full name table so the new client knows
             * everyone, then broadcast PEER_NAME so existing clients
             * learn the new joiner's name. */
            if (from_slot >= 1 && from_slot < NET_MAX_PLAYERS && len >= 4 + MM_MAX_USERNAME) {
                /* Pull the client's username out of the request payload. */
                char joinerName[MM_MAX_USERNAME];
                memcpy(joinerName, buf + 4, MM_MAX_USERNAME);
                joinerName[MM_MAX_USERNAME - 1] = '\0';

                char joinerPlatform[NET_PLATFORM_LEN];
                uint8_t joinerRegion = 0;
                memset(joinerPlatform, 0, sizeof(joinerPlatform));
                if (len >= (int)(4 + MM_MAX_USERNAME + NET_PLATFORM_LEN + 1)) {
                    memcpy(joinerPlatform, buf + 4 + MM_MAX_USERNAME, NET_PLATFORM_LEN);
                    joinerPlatform[NET_PLATFORM_LEN - 1] = '\0';
                    joinerRegion = (uint8_t)buf[4 + MM_MAX_USERNAME + NET_PLATFORM_LEN];
                }

                g_netPlayerCount = from_slot + 1;  /* grow as players join */
                DebugLog("Host: client joined as slot %d ('%s' %s r%d), playerCount=%d\n",
                         from_slot, joinerName, joinerPlatform, joinerRegion, g_netPlayerCount);

                /* Populate decoration table entry + raw-name cache for new player */
                {
                    char *entry = g_netPlayerDecorations + from_slot * NET_DECO_STRIDE;
                    *(int *)(entry + NET_DECO_DPID) = from_slot;
                    *(unsigned short *)(entry + NET_DECO_SLOT) = (unsigned short)from_slot;
                    net_deco_set_name(entry, joinerName);
                    net_set_slot_name(from_slot, joinerName);
                    net_set_slot_platform(from_slot, joinerPlatform, joinerRegion);
                }

                /* Build extended SLOT_ASSIGN reply with platform/region info.
                 * Wire layout:
                 *   +0x00 int     hdr
                 *   +0x04 int     slot
                 *   +0x08 char    names[NET_MAX_PLAYERS][MM_MAX_USERNAME]
                 *   +... char    platforms[NET_MAX_PLAYERS][NET_PLATFORM_LEN]
                 *   +... uint8_t regions[NET_MAX_PLAYERS] */
                {
                    enum { REPLY_SIZE = 8
                        + NET_MAX_PLAYERS * MM_MAX_USERNAME
                        + NET_MAX_PLAYERS * NET_PLATFORM_LEN
                        + NET_MAX_PLAYERS };
                    char _replybuf[REPLY_SIZE];
                    int _off;
                    memset(_replybuf, 0, sizeof(_replybuf));
                    wl32(_replybuf + 0, (uint32_t)NET_MSG_SLOT_ASSIGN);
                    wl32(_replybuf + 4, (uint32_t)from_slot);
                    _off = 8;
                    {
                        int s;
                        for (s = 0; s < NET_MAX_PLAYERS; s++) {
                            strncpy(_replybuf + _off + s * MM_MAX_USERNAME,
                                    net_get_slot_name(s), MM_MAX_USERNAME - 1);
                        }
                    }
                    _off += NET_MAX_PLAYERS * MM_MAX_USERNAME;
                    {
                        int s;
                        for (s = 0; s < NET_MAX_PLAYERS; s++) {
                            strncpy(_replybuf + _off + s * NET_PLATFORM_LEN,
                                    net_get_slot_platform(s), NET_PLATFORM_LEN - 1);
                        }
                    }
                    _off += NET_MAX_PLAYERS * NET_PLATFORM_LEN;
                    {
                        int s;
                        for (s = 0; s < NET_MAX_PLAYERS; s++) {
                            _replybuf[_off + s] = (char)net_get_slot_region(s);
                        }
                    }
                    net_send_to(from_slot, _replybuf, REPLY_SIZE);
                }

                /* Broadcast PEER_NAME so already-connected clients learn the
                 * new joiner's name and platform.
                 * Wire layout:
                 *   +0x00 int     hdr      = NET_MSG_PEER_NAME
                 *   +0x04 int     slot
                 *   +0x08 char    name[MM_MAX_USERNAME]
                 *   +... char    platform[NET_PLATFORM_LEN]
                 *   +... uint8_t region */
                {
                    enum { PEER_SIZE = 8 + MM_MAX_USERNAME + NET_PLATFORM_LEN + 1 };
                    char _peerbuf[PEER_SIZE];
                    memset(_peerbuf, 0, sizeof(_peerbuf));
                    wl32(_peerbuf + 0, (uint32_t)NET_MSG_PEER_NAME);
                    wl32(_peerbuf + 4, (uint32_t)from_slot);
                    strncpy(_peerbuf + 8, joinerName, MM_MAX_USERNAME - 1);
                    strncpy(_peerbuf + 8 + MM_MAX_USERNAME, joinerPlatform, NET_PLATFORM_LEN - 1);
                    _peerbuf[8 + MM_MAX_USERNAME + NET_PLATFORM_LEN] = (char)joinerRegion;
                    int s;
                    for (s = 1; s < g_netPlayerCount; s++) {
                        if (s == from_slot) continue;
                        net_send_to(s, _peerbuf, PEER_SIZE);
                    }
                }

                /* Signal lobby state machine — screen_misc.c checks this */
                g_netLobbyPlayerSlot = from_slot;

            }
            continue;
        }

        if (header == NET_MSG_SLOT_ASSIGN && !net_is_host()
            && len >= 8 + NET_MAX_PLAYERS * MM_MAX_USERNAME) {
            /* Client: host told us our slot, plus the full name/platform table
             * for everyone currently in the session. */
            int slot = rl32s(buf + 4);
            if (slot >= 0 && slot < NET_MAX_PLAYERS) {
                g_currentPlayerIdx = slot;
                g_localPlayerIndex = slot;
                /* g_netGameStarted stays 0 on client — DO NOT set to 1 */
                net_set_local_slot(slot);

                const char *names = buf + 8;
                int nameBlockSize = NET_MAX_PLAYERS * MM_MAX_USERNAME;
                int hasPlatforms = (len >= 8 + nameBlockSize
                                    + NET_MAX_PLAYERS * NET_PLATFORM_LEN
                                    + NET_MAX_PLAYERS);
                const char *platforms = hasPlatforms ? buf + 8 + nameBlockSize : NULL;
                const uint8_t *regions = hasPlatforms
                    ? (const uint8_t *)(buf + 8 + nameBlockSize + NET_MAX_PLAYERS * NET_PLATFORM_LEN)
                    : NULL;

                int s;
                for (s = 0; s < NET_MAX_PLAYERS; s++) {
                    char nm[MM_MAX_USERNAME];
                    memcpy(nm, names + s * MM_MAX_USERNAME, MM_MAX_USERNAME);
                    nm[MM_MAX_USERNAME - 1] = '\0';
                    if (nm[0] == '\0') continue;
                    char *entry = g_netPlayerDecorations + s * NET_DECO_STRIDE;
                    *(int *)(entry + NET_DECO_DPID) = s;
                    *(unsigned short *)(entry + NET_DECO_SLOT) = (unsigned short)s;
                    net_deco_set_name(entry, nm);
                    net_set_slot_name(s, nm);
                    if (platforms) {
                        char plat[NET_PLATFORM_LEN];
                        memcpy(plat, platforms + s * NET_PLATFORM_LEN, NET_PLATFORM_LEN);
                        plat[NET_PLATFORM_LEN - 1] = '\0';
                        net_set_slot_platform(s, plat, regions[s]);
                    }
                }
                DebugLog("Client: SLOT_ASSIGN slot=%d, host='%s'\n",
                         slot, net_get_slot_name(0));
                s_haveSlotAssign = 1;
                /* Tell everyone our character now: the CHAR_CHANGE sent when
                 * we joined was dropped (no slot yet), so until the player
                 * changed character the host raced us as the default one. */
                EnumNetworkSessions(1);
            }
            continue;
        }

        if (header == NET_MSG_PEER_NAME && !net_is_host()
            && len >= 8 + MM_MAX_USERNAME) {
            /* Host informed us a peer joined (or changed name). Update the
             * decoration entry + name cache for that slot so lobby/HUD show
             * the real name. */
            int slot = rl32s(buf + 4);
            if (slot >= 0 && slot < NET_MAX_PLAYERS) {
                char nm[MM_MAX_USERNAME];
                memcpy(nm, buf + 8, MM_MAX_USERNAME);
                nm[MM_MAX_USERNAME - 1] = '\0';
                char *entry = g_netPlayerDecorations + slot * NET_DECO_STRIDE;
                *(int *)(entry + NET_DECO_DPID) = slot;
                *(unsigned short *)(entry + NET_DECO_SLOT) = (unsigned short)slot;
                net_deco_set_name(entry, nm);
                net_set_slot_name(slot, nm);
                if (len >= (int)(8 + MM_MAX_USERNAME + NET_PLATFORM_LEN + 1)) {
                    char plat[NET_PLATFORM_LEN];
                    memcpy(plat, buf + 8 + MM_MAX_USERNAME, NET_PLATFORM_LEN);
                    plat[NET_PLATFORM_LEN - 1] = '\0';
                    uint8_t reg = (uint8_t)buf[8 + MM_MAX_USERNAME + NET_PLATFORM_LEN];
                    net_set_slot_platform(slot, plat, reg);
                }
                DebugLog("Client: PEER_NAME slot=%d name='%s'\n", slot, nm);
            }
            continue;
        }

        /* ---- SDL: host says start the game ---- */
        if (header == NET_MSG_START_GAME && !net_is_host() && len >= 8
            && g_netGameStarted == 0) {
            /* Wire-supplied count. g_netPlayerCount bounds loops that index
             * g_playerBase[], s_playerRecvFlags[], g_perPlayerInput[],
             * s_netInputBuffer[] and g_netPlayerRecvd[] without their own
             * guard, so an out-of-range value here is an out-of-bounds write.
             * Clamp rather than assert: this is remote input, and aborting on
             * a malformed packet would just be a denial of service. */
            int playerCount = rl32s(buf + 4);
            if (playerCount < 0 || playerCount > NET_MAX_PLAYERS) {
                fprintf(stderr,
                        "net: START_GAME playerCount %d out of range [0,%d], clamping\n",
                        playerCount, NET_MAX_PLAYERS);
                playerCount = (playerCount < 0) ? 0 : NET_MAX_PLAYERS;
            }
            g_netPlayerCount = playerCount;

            /* Unpack track/mode fields (extended payload).
             * In the binary, these lived at the same address as
             * g_netGameInfoDest[1]/[2] via memory aliasing. */
            if (len >= 16) {
                g_netTrackIndex       = rl16s(buf + 8);
                g_netRaceSubModeIndex = rl16s(buf + 10);
                g_netWeatherType      = (int)rl16s(buf + 12);
                g_netPlayerMode       = (int)rl16s(buf + 14);
            }

            /* Unpack per-player character IDs. Binary propagated these
             * via DirectPlay player data; our SDL port packs them here.
             * Preserve local player's own selection — the host may have
             * a stale charId if the client changed after joining. */
            if (len >= 24) {
                int k;
                for (k = 0; k < NET_MAX_PLAYERS; k++) {
                    /* charId indexes g_modelMeta[], g_rampSpeedTable*[] and
                     * g_charStatsTable + charId*10 at call sites that do not
                     * all bounds-check (camera_per_track.c:1797,
                     * player_camera.c:199, physics_main.c:394/410/519/564).
                     * Clamp remote input rather than trust it. */
                    short cid = rl16s(buf + 16 + k * 2);
                    if (cid < 0 || cid >= CHAR_COUNT) {
                        fprintf(stderr,
                                "net: START_GAME charId %d for slot %d out of range, ignoring\n",
                                (int)cid, k);
                        continue;
                    }
                    net_apply_char_id(k, cid);
                }
                /* Restore local player's own pick */
                net_apply_char_id(g_localPlayerIndex, g_menuPlayer.charId);
            }

            /* Populate decoration table.  Real names should already be in
             * place from SLOT_ASSIGN + PEER_NAME; only fall back to
             * synthetic names if a slot is still nameless (which would
             * indicate a bug in the lobby join sequence). */
            {
                int k;
                for (k = 0; k < playerCount && k < NET_MAX_PLAYERS; k++) {
                    char *entry = g_netPlayerDecorations + k * NET_DECO_STRIDE;
                    *(int *)(entry + NET_DECO_DPID) = k;
                    *(unsigned short *)(entry + NET_DECO_SLOT) = (unsigned short)k;
                    if (net_get_slot_name(k)[0] == '\0') {
                        if (k == 0) {
                            net_deco_set_name(entry, "host");
                            net_set_slot_name(k, "host");
                        } else {
                            char nm[4] = { 'p', (char)('0' + k), '\0', '\0' };
                            net_deco_set_name(entry, nm);
                            net_set_slot_name(k, nm);
                        }
                    }
                }
            }

            /* Run the same init as the host */
            InitNetworkGame();

            /* Last chance for the host to learn our character before its
             * InitPlayerSlot runs: it keeps draining packets until this ACK
             * lands, so send the pick again first (issue #13). */
            EnumNetworkSessions(0);

            /* ACK so host stops retransmitting
             * Wire: 4 bytes, int header = NET_MSG_START_ACK */
            {
                char _ackbuf[4]; wl32(_ackbuf, (uint32_t)NET_MSG_START_ACK);
                net_send_to_host(_ackbuf, 4);
            }

            continue;
        }

        /* ---- Character change (NET_MSG_CHAR_CHANGE) ---- */
        if (header == NET_MSG_CHAR_CHANGE && len >= 8) {
            short slot   = rl16s(buf + 4);
            short charId = rl16s(buf + 6);
            /* charId is used as a table index downstream and not every call
             * site bounds-checks it — see the note in the START_GAME handler. */
            if (charId < 0 || charId >= CHAR_COUNT) {
                fprintf(stderr, "net: CHAR_CHANGE charId %d out of range, dropping\n",
                        (int)charId);
                continue;
            }
            if (slot >= 0 && slot < NET_MAX_PLAYERS) {
                net_apply_char_id(slot, charId);
                /* Also update decoration table so host has it for START_GAME */
                *(int *)(g_netPlayerDecorations + slot * NET_DECO_STRIDE + NET_DECO_LOBBYCHAR) = (int)charId;

                /* Host: relay to other clients so they see the change too */
                if (net_is_host()) {
                    net_broadcast(buf, len);
                }
            }
            continue;
        }

        /* ---- Lobby config broadcast (0xFFF02000) ---- */
        if (header == 0xFFF02000u && len >= 0x30) {
            /* Host sends g_netGameInfoDest (48 bytes) every 32 frames.
             * Binary: DirectPlay delivered this via EnumSessions callback
             * into g_netLobbyConfigBuf, then set g_netReceivedLobbyData=1
             * so the lobby loop in screen_misc.c would unpack it. */
            { int _j; for (_j = 0; _j < 12; _j++) g_netLobbyConfigBuf[_j] = rl32s(buf + _j * 4); }
            g_netReceivedLobbyData = 1;
            continue;
        }

        /* ---- Client input (host receives from clients, any game state) ---- */
        if (header == NET_MSG_CLIENT_INPUT && len >= 12 && net_is_host()) {
            uint16_t seq = rl16u(buf + 4);
            unsigned short inputBits = rl16u(buf + 6);
            uint8_t playerIdx = *(uint8_t *)(buf + 8);
            if (playerIdx < MAX_PLAYERS &&
                g_playerBase[playerIdx].netActive != 0 &&
                (int16_t)(seq - s_lastRecvInputSeq[playerIdx]) > 0) {
                s_lastRecvInputSeq[playerIdx] = seq;
                s_netInputBuffer[playerIdx] = inputBits;
                g_netPlayerRecvd[playerIdx] = 1;
                s_hostSlotLastRecvMs[playerIdx] = timeGetTime();
                net_note_peer_version((int)playerIdx, rl16u(buf + 10));
#if NET_PEER_AUTHORITATIVE
                if (len > 12) {
                    Player *cpl = &g_playerBase[playerIdx];
                    short prevLaps = cpl->lapsCompleted;
                    int is_kf = (buf[9] & 1);
                    net_delta_decode(&s_deltaRecv[playerIdx],
                                     (const unsigned char *)buf + 12,
                                     len - 12, is_kf, cpl);
                    if (cpl->lapsCompleted == 3 && prevLaps < 3) {
                        int counter = g_finishOrderCounter;
                        cpl->trackProgress = (int)(0xFFFFFFFF - (unsigned int)counter);
                        g_finishOrderCounter = counter + 1;
                    }
                    NetInterpRecord((int)playerIdx);
                }
#endif
            }
            continue;
        }

        /* ---- Game messages: dispatch by g_netGameStarted ---- */
        if (g_netGameStarted != 0) {
            /* Lobby phase — binary: cmp [0x68ACE4], 0; jne */

            if (header == 0xFF00000Fu && len >= 16) {
                /* Lobby pad data from client — binary 0x4D9896 */
                int playerIdx = (int)(unsigned short)rl32u(buf + 8);  /* word in binary */
                if (playerIdx >= 0 && playerIdx < MAX_PLAYERS) {
                    /* Check player is active: [playerIdx*0x71C + 0x8FDC08] */
                    if (g_playerBase[playerIdx].netActive != 0) {
                        unsigned short input = rl16u(buf + 0xC);
                        s_netInputBuffer[playerIdx] = input;
                        g_netPlayerRecvd[playerIdx] = 1;
                    }
                }
            }
            else if (header == 0xFFF0004Fu && len >= 12) {
                /* Keepalive — binary 0x4D98DD */
                int playerIdx = rl32s(buf + 8);
                if (playerIdx >= 0 && playerIdx < MAX_PLAYERS) {
                    g_netPlayerAlive[playerIdx] = 1;
                    /* Check if all connected players are alive → ready to race */
                    {
                        int allAlive = 1, k;
                        for (k = 0; k < g_netPlayerCount; k++) {
                            if (g_netPlayerAlive[k] == 0) { allAlive = 0; break; }
                        }
                        if (allAlive) g_netReadyFlag = 1;
                    }
                }
            }
        }
        else {
            /* Race phase — binary 0x4D9908 */

            if (header == NET_MSG_SNAPSHOT && len >= SNAP_HEADER_SIZE) {
                net_apply_snapshot(buf, len);
            }
            else if (header == 0xFFF0003Fu && len >= 0x44) {
                /* Camera sync — 68 bytes — binary 0x4D9BBD
                 * Per-player camera/race data, plus g_raceFinished update.
                 * Layout: header(4) + perPlayerPos[4*3](48) + racePos[4](4)
                 *       + lapsDone[4](4) + collCount[4](4) + raceFinished(2) + pad(2) */
                int savedRaceFinished = g_raceFinished;

                if (g_numViewports > 0) {
                    for (i = 0; i < g_numViewports; i++) {
                        Player *pl = &g_playerBase[i];

                        /* racePosition (byte → word) at pkt+0x34 */
                        pl->lapsCompleted =
                            (short)*(unsigned char *)(buf + 0x34 + i);
                        /* lapsCompleted (byte → word) at pkt+0x38 */
                        pl->racePosition =
                            (short)*(unsigned char *)(buf + 0x38 + i);
                        /* collisionCount (byte → int) at pkt+0x3C */
                        pl->collisionCount =
                            (int)*(unsigned char *)(buf + 0x3C + i);

                        /* 3 ints of position data at pkt+0x04, stride 12 per player */
                        pl->lap1Time = rl32s(buf + 0x04 + i * 12 + 0);
                        pl->lap2Time = rl32s(buf + 0x04 + i * 12 + 4);
                        pl->lap3Time = rl32s(buf + 0x04 + i * 12 + 8);
                    }
                }

                /* Update g_raceFinished — binary: sar eax, 0x10 on word at pkt+0x40 */
                {
                    int tmp = rl32s(buf + 0x3E);
                    g_raceFinished = tmp >> 16;
                }

                /* If race wasn't finished before, init end-game fade — binary 0x4D9C73 */
                if (savedRaceFinished == 0) {
                    g_fadeSpeed = 0x10;  /* 0x00901C4C */
                    g_fadeState = 2;     /* 0x00901C48 — FADE_OUT */
                }

            }
            else if (header == 0xFFF0004Fu && len >= 12) {
                /* Keepalive — binary 0x4D9C98 */
                int playerIdx = rl32s(buf + 8);
                if (playerIdx >= 0 && playerIdx < MAX_PLAYERS) {
                    g_netPlayerAlive[playerIdx] = 1;
                    /* Check if all connected players are alive → ready to race */
                    {
                        int allAlive = 1, k;
                        for (k = 0; k < g_netPlayerCount; k++) {
                            if (g_netPlayerAlive[k] == 0) { allAlive = 0; break; }
                        }
                        if (allAlive) g_netReadyFlag = 1;
                    }
                }
            }
        }

        /* ---- Client-side: update session frame from snapshot ---- */
        if (header == NET_MSG_SNAPSHOT && len >= SNAP_HEADER_SIZE && !net_is_host()) {
            uint16_t frame = rl16u(buf + 6);
            g_gameDataPacketFrame = frame;
            g_netSessionFrame = (int)frame;
            s_lastHostPacketMs = timeGetTime();
        }
    }

    if (!net_is_host() && net_is_active() && s_lastHostPacketMs != 0
        && g_introCountdown == 0 && g_postRaceCameraMode == 0) {
        DWORD now    = timeGetTime();
        DWORD silent = now - s_lastHostPacketMs;
        if (silent > 5000 && g_netDisconnectFlag == 0) {
            DebugLog("Host disconnected (silent for %ums)\n", (unsigned)silent);
            s_lastHostPacketMs = 0;
            client_lost_host("CONNECTION TO THE HOST LOST");
        }
    }

}

/**
 * InitDirectPlayProvider — 0x00486AB4 — 130 bytes
 *
 * Calls CoCreateInstance (import at [0x950144]) to create an IDirectPlay4
 * COM object, stores it at 0x68AC28.  On success, calls IDirectPlay4::
 * InitializeConnection (vtable +0x8C) with the provider at [0x6D9998]
 * and a callback at 0x4869B0.  Logs errors via 0x4868D0.
 * Returns 1 on success, 0 on failure.
 *
 * Pure DirectPlay COM — no equivalent On SDL.
 */
static int InitDirectPlayProvider(void)                    /* 0x486ab4 */
{
    /* CoCreateInstance for IDirectPlay4 — not available On SDL */
    return 0;
}

/**
 * IsDirectPlayAvailable — 0x00487CD8 — 16 bytes
 *
 * Thin boolean wrapper around InitDirectPlayProvider:
 *   call 0x486ab4; test eax,eax; setne al; and eax,0xff; ret
 * Returns 1 if DirectPlay initialised successfully, 0 otherwise.
 */
int IsDirectPlayAvailable(void)                            /* 0x487cd8 */
{
    return InitDirectPlayProvider() != 0;                  /* 0x487cdd */
}

/* =====================================================================
 * EnumNetworkSessions — FUN_00487180 — 51 bytes — called 4x — VALIDATED
 *
 * SDL replacement: broadcasts a NET_MSG_CHAR_CHANGE packet containing
 * the local player's slot and character ID.
 * ===================================================================== */
void EnumNetworkSessions(int flag)
{
    (void)flag;
    if (!net_is_active()) return;

    /* A client has no slot until the host answers JOIN_REQ with SLOT_ASSIGN —
     * net_client_connect leaves net_local_slot() at -1 until then. The socket
     * is live from the moment we connect, so "active" is not "joined", and
     * broadcasting here beforehand makes CHAR_CHANGE the first packet the host
     * ever sees from us. ProcessNetworkMessages rightly treats that as fatal.
     * Reachable because callers key off lobby state alone (screen_misc.c:4349)
     * and ns_lobbyState survives leaving and re-entering the network screen. */
    if (!net_is_host() && net_local_slot() < 0) {
        return;
    }

    /* Wire layout (8 bytes):
     *   +0x00 int   hdr    = NET_MSG_CHAR_CHANGE
     *   +0x04 short slot
     *   +0x06 short charId */
    char _ccbuf[8];
    wl32(_ccbuf + 0, (uint32_t)NET_MSG_CHAR_CHANGE);
    wl16(_ccbuf + 4, (uint16_t)(short)g_localPlayerIndex);
    wl16(_ccbuf + 6, (uint16_t)g_menuPlayer.charId);
    UpdateNetworkSync(_ccbuf, 8);
}

/* FUN_00487674 — 58 bytes — called 1x — VALIDATED
 * Creates background thread for network message processing. */
void StartNetworkThread(void)
{
    NetRecvThread_Start();
}

/* FUN_004871b4 — 67 bytes — called 1x — VALIDATED
 * Opens a DirectPlay session. SDL: transport already connected. */
int OpenNetworkSession(int sessionDesc)
{
    (void)sessionDesc;
    return 1;
}

/* =====================================================================
 * BuildNetworkPhoneNumber — FUN_0048a6d8 — 117 bytes — called 1x — VALIDATED
 *
 * Iterates g_netServiceProviders, maps scancodes to ASCII digits,
 * builds phone number string. Network-only — empty on SDL.
 * ===================================================================== */

static char g_phoneNumberBuf[64];

int BuildNetworkPhoneNumber(void)
{
    int outIdx = 0;

    if (g_netServiceProviders[0] == -1) goto done;

    int i = 0;
    do {
        int entry = g_netServiceProviders[i];
        if ((entry & (int)0xFFFF0000) == 0) {
            int low = entry & 0xFFFF;
            if (low >= 0x1A && low <= 0x23) {
                if (low == 0x1A) {
                    g_phoneNumberBuf[outIdx] = '0';
                } else {
                    g_phoneNumberBuf[outIdx] = (char)(low + 0x16);
                }
                outIdx++;
            }
        }
        i++;
    } while (g_netServiceProviders[i] != -1);

done:
    g_phoneNumberBuf[outIdx] = 0;
    return outIdx;
}


/* =====================================================================
 * Online pause
 *
 * Pausing used to be purely local: the paused machine stopped simulating and
 * sending, and after 5 s the others dropped it as disconnected. Now:
 *  - the player who pauses broadcasts NET_MSG_PAUSE(1) twice a second; the
 *    other machines freeze the race and show "GAME PAUSED BY <name>";
 *  - on resume NET_MSG_PAUSE(0) is sent and every machine runs a 3-2-1
 *    countdown before the race continues;
 *  - if the race stays paused for 120 s, everyone leaves the online race and
 *    goes back to the host/join screen.
 * ===================================================================== */
#define NET_PAUSE_TIMEOUT_MS   120000u
#define NET_PAUSE_COUNTDOWN_MS 3000u

static int   s_npRemoteSlot = -1;     /* slot that paused us, -1 none */
static int   s_npLocalPaused;         /* our own pause is being broadcast */
static DWORD s_npStartMs;             /* when the current pause began */
static DWORD s_npRemoteLastMs;        /* last PAUSE(1) heard from the pauser */
static DWORD s_npLastSendMs;
static DWORD s_npCountdownEndMs;      /* 0 = no countdown */
static int   s_npResumeSends;         /* extra resume packets still to send */
int g_netPauseTimedOut;

static void np_send(int state)
{
    char b[12];
    wl32(b + 0, (uint32_t)NET_MSG_PAUSE);
    wl32(b + 4, (uint32_t)g_localPlayerIndex);
    wl32(b + 8, (uint32_t)state);
    if (net_is_host()) {
        net_broadcast(b, 12);
    }
    else if (s_haveSlotAssign) {
        net_send_to_host(b, 12);
    }
    s_npLastSendMs = timeGetTime();
}

static void np_reset_silence(void)
{
    DWORD now = timeGetTime();
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (s_hostSlotLastRecvMs[i] != 0) s_hostSlotLastRecvMs[i] = now;
    }
    if (s_lastHostPacketMs != 0) s_lastHostPacketMs = now;
}

static void np_start_countdown(void)
{
    s_npCountdownEndMs = timeGetTime() + NET_PAUSE_COUNTDOWN_MS;
    if (s_npCountdownEndMs == 0) s_npCountdownEndMs = 1;
}

void NetPauseRemote(int slot, int state)
{
    if (state) {
        s_npRemoteLastMs = timeGetTime();
        if (s_npRemoteSlot < 0) {
            s_npRemoteSlot = slot;
            s_npStartMs = timeGetTime();
            s_npCountdownEndMs = 0;
            DebugLog("Net: race paused by slot %d ('%s')\n", slot, net_get_slot_name(slot));
        }
    }
    else if (s_npRemoteSlot == slot) {
        s_npRemoteSlot = -1;
        np_start_countdown();
        DebugLog("Net: slot %d resumed, countdown\n", slot);
    }
}

static void np_player_gone(int slot)
{
    if (s_npRemoteSlot == slot) {
        DebugLog("Net: pauser slot %d left - unfreezing\n", slot);
        s_npRemoteSlot = -1;
        np_start_countdown();
    }
}

/* Race frozen by the network (someone else paused, or the resume countdown). */
int NetRaceFrozen(void)
{
    return s_npRemoteSlot >= 0 || s_npCountdownEndMs != 0;
}

void NetPauseReset(void)
{
    s_npRemoteSlot = -1;
    s_npLocalPaused = 0;
    s_npCountdownEndMs = 0;
    s_npResumeSends = 0;
}

/* Called once per race frame in network races, before the simulation. */
void NetPauseTick(void)
{
    if (!net_is_active()) {
        NetPauseReset();
        return;
    }
    DWORD now = timeGetTime();

    /* Our pause started / ended. */
    if (g_isPaused && !s_npLocalPaused) {
        s_npLocalPaused = 1;
        s_npStartMs = now;
        np_send(1);
        DebugLog("Net: we paused the race\n");
    }
    else if (!g_isPaused && s_npLocalPaused) {
        s_npLocalPaused = 0;
        np_send(0);
        s_npResumeSends = 3;
        np_start_countdown();
        DebugLog("Net: we resumed, countdown\n");
    }
    else if (s_npLocalPaused && now - s_npLastSendMs >= 500) {
        np_send(1);                                  /* keep the others frozen */
    }
    else if (s_npResumeSends > 0 && now - s_npLastSendMs >= 200) {
        np_send(0);                                  /* resume is UDP too */
        s_npResumeSends--;
    }

    /* While anything is paused the race loop does not poll the network, so
     * drain it here (pause/resume messages, and so the queue doesn't pile up). */
    if (g_isPaused || NetRaceFrozen()) {
        ApplyNetworkPlayerState();
        /* The pauser went quiet (quit, crashed, cable pulled): stop waiting
         * for it and let the normal disconnect handling take over. */
        if (s_npRemoteSlot >= 0 && timeGetTime() - s_npRemoteLastMs > 6000) {
            DebugLog("Net: pauser slot %d went silent - unfreezing\n", s_npRemoteSlot);
            s_npRemoteSlot = -1;
        }
        else {
            np_reset_silence();                      /* nobody is "silent" now */
        }
    }

    if (s_npCountdownEndMs != 0 && (int)(now - s_npCountdownEndMs) >= 0) {
        s_npCountdownEndMs = 0;
        np_reset_silence();
    }

    /* Paused for too long: end the online race for everyone. */
    if ((s_npLocalPaused || s_npRemoteSlot >= 0) && g_netDisconnectFlag == 0
        && now - s_npStartMs >= NET_PAUSE_TIMEOUT_MS) {
        DebugLog("Net: paused for %us - closing the online race\n",
                 (unsigned)(NET_PAUSE_TIMEOUT_MS / 1000));
        g_netPauseTimedOut = 1;
        g_netDisconnectFlag = 1;
        g_isPaused = 0;
        s_npLocalPaused = 0;
        g_fadeState = FADE_OUT;
        g_fadeSpeed = 0xC;
    }
}

/* Text overlay (called from DrawPauseOverlay). */
void NetPauseDraw(void)
{
    extern void DrawDebugOverlayText(const char *s, int x, int y, int pixSz, uint32_t color);
    char line[64];
    DWORD now = timeGetTime();
    if (s_noticeUntil != 0) {
        if ((int)(now - s_noticeUntil) >= 0) {
            s_noticeUntil = 0;
        } else {
            DrawDebugOverlayText(s_noticeText, (640 - (int)strlen(s_noticeText) * 12) / 2, 120, 2, 0xFFFFFFFF);
            if (s_noticeText2[0])
                DrawDebugOverlayText(s_noticeText2, (640 - (int)strlen(s_noticeText2) * 12) / 2, 146, 2, 0xC0C0C0C0);
        }
    }
    if (!net_is_active()) return;
    unsigned left = 0;
    if (s_npLocalPaused || s_npRemoteSlot >= 0) {
        DWORD el = now - s_npStartMs;
        left = el >= NET_PAUSE_TIMEOUT_MS ? 0 : (NET_PAUSE_TIMEOUT_MS - el + 999) / 1000;
    }
    if (s_npRemoteSlot >= 0 && !g_isPaused) {
        char name[MM_MAX_USERNAME];
        snprintf(name, sizeof(name), "%s", net_get_slot_name(s_npRemoteSlot));
        for (char *c = name; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        snprintf(line, sizeof(line), "GAME PAUSED BY %s", name[0] ? name : "A PLAYER");
        DrawDebugOverlayText(line, (640 - (int)strlen(line) * 18) / 2, 200, 3, 0xFFFFFFFF);
        snprintf(line, sizeof(line), "ONLINE RACE ENDS IN %u S", left);
        DrawDebugOverlayText(line, (640 - (int)strlen(line) * 12) / 2, 240, 2, 0xC0C0C0C0);
    }
    else if (s_npLocalPaused && g_isPaused) {
        snprintf(line, sizeof(line), "ONLINE RACE ENDS IN %u S", left);
        DrawDebugOverlayText(line, (640 - (int)strlen(line) * 12) / 2, 340, 2, 0xFFFFFFFF);
    }
    else if (s_npCountdownEndMs != 0) {
        int ms = (int)(s_npCountdownEndMs - now);
        int n = ms <= 0 ? 1 : (ms + 999) / 1000;
        snprintf(line, sizeof(line), "%d", n);
        DrawDebugOverlayText(line, (640 - 8 * 6) / 2, 200, 8, 0xFFFFFFFF);
        DrawDebugOverlayText("GET READY", (640 - 9 * 18) / 2, 280, 3, 0xFFFFFFFF);
    }
}
