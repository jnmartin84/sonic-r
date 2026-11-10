#include "net_interp.h"
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "platform.h"
#include <stdint.h>
#include <string.h>

extern void DeriveGroundState(Player *player);   /* ground_collision.c */

typedef struct {
    int     posX, posY, posZ;
    int     anglePitch, angleYaw, angleRoll, pitchCombo;
    uint32_t  recvTimeMs;
    int     valid;
} NetSnap;

static NetSnap s_prev[MAX_PLAYERS];
static NetSnap s_curr[MAX_PLAYERS];
static uint32_t  s_intervalMs[MAX_PLAYERS];

#define STALE_MS     1000
#define MIN_INTERVAL 8
#define MAX_INTERVAL 200

static void snap_capture(NetSnap *s, const Player *p)
{
    s->posX        = p->posX;
    s->posY        = p->posY;
    s->posZ        = p->posZ;
    s->anglePitch  = p->anglePitch;
    s->angleYaw    = p->angleYaw;
    s->angleRoll   = p->angleRoll;
    s->pitchCombo  = p->pitchCombo;
}

void NetInterpRecord(int playerIdx)
{
    if (playerIdx < 0 || playerIdx >= MAX_PLAYERS) return;

    uint32_t now = platform_get_time_ms();

    if (s_curr[playerIdx].valid &&
        (now - s_curr[playerIdx].recvTimeMs) > STALE_MS) {
        s_curr[playerIdx].valid = 0;
        s_prev[playerIdx].valid = 0;
    }

    if (s_curr[playerIdx].valid) {
        uint32_t dt = now - s_curr[playerIdx].recvTimeMs;
        if (dt < MIN_INTERVAL) dt = MIN_INTERVAL;
        if (dt > MAX_INTERVAL) dt = MAX_INTERVAL;
        s_intervalMs[playerIdx] = dt;
        s_prev[playerIdx] = s_curr[playerIdx];
    }
    snap_capture(&s_curr[playerIdx], &g_playerBase[playerIdx]);
    s_curr[playerIdx].recvTimeMs = now;
    s_curr[playerIdx].valid = 1;
}

/* Forget every snapshot. Slots are handed out by join order each race, so a
 * slot that was a remote player last race can be the local one now; its old
 * snapshot would otherwise keep pinning that player to where the previous
 * owner finished, and other players would show at last race's positions until
 * their first new packet. Called at race start from NetSnapshotReset. */
void NetInterpReset(void)
{
    memset(s_prev, 0, sizeof s_prev);
    memset(s_curr, 0, sizeof s_curr);
    memset(s_intervalMs, 0, sizeof s_intervalMs);
}

static int lerp_int(int a, int b, int alpha_q12)
{
    return a + (int)(((long long)(b - a) * (long long)alpha_q12) >> 12);
}

/* Yaw and the other angles are 12-bit (0..0xFFF) in the binary; lerp along
 * the shortest arc so a wrap from 0xFFE -> 0x002 doesn't sweep backwards. */
static int lerp_angle12(int a, int b, int alpha_q12)
{
    int diff = (b - a) & 0xFFF;
    if (diff >= 0x800) diff -= 0x1000;
    int out = a + (int)(((long long)diff * (long long)alpha_q12) >> 12);
    return out & 0xFFF;
}

void NetInterpApply(void)
{
    if (g_netSessionActive == 0) return;

    uint32_t now = platform_get_time_ms();

    for (int i = 0; i < MAX_PLAYERS; i++) {
        /* The local player is driven by local physics, never by snapshots. */
        if (i == g_localPlayerIndex) continue;
        if (!s_curr[i].valid) continue;

        Player *p = &g_playerBase[i];

        if (s_prev[i].valid && s_intervalMs[i] != 0) {
            /* alpha = (now - currRecv) / interval in Q12.
             * 0x0000 = show prev, 0x1000 = show curr, up to 0x1800 = extrapolate
             * 50% past curr (coasts on missed packet instead of freezing). */
            long long num   = (long long)((int32_t)(now - s_curr[i].recvTimeMs)) << 12;
            long long alpha = num / (long long)s_intervalMs[i];
            if (alpha < 0)      alpha = 0;
            if (alpha > 0x1800) alpha = 0x1800;
            int a = (int)alpha;

            p->posX       = lerp_int    (s_prev[i].posX,       s_curr[i].posX,       a);
            p->posY       = lerp_int    (s_prev[i].posY,       s_curr[i].posY,       a);
            p->posZ       = lerp_int    (s_prev[i].posZ,       s_curr[i].posZ,       a);
            p->angleYaw   = lerp_angle12(s_prev[i].angleYaw,   s_curr[i].angleYaw,   a);
            p->anglePitch = lerp_angle12(s_prev[i].anglePitch, s_curr[i].anglePitch, a);
            p->angleRoll  = lerp_angle12(s_prev[i].angleRoll,  s_curr[i].angleRoll,  a);
            p->pitchCombo = lerp_angle12(s_prev[i].pitchCombo, s_curr[i].pitchCombo, a);
        }

        /* Only players with a snapshot are remote. Their physics never runs
         * here, so rebuild groundHeight and the surface normal from the
         * position just written — the shadow and footprints read them. */
        DeriveGroundState(p);
    }
}
