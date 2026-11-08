/**
 * race.c — Race state tracking functions
 *
 * ComputeRacePositions (race position tracker) and
 * UpdatePlayerAnimation (item/collision animation effects).
 * See PhysicsSmall_annotated.c for full documentation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "player_struct.h"
#include "debug_flags.h"

/* Item animation table */
extern unsigned char g_itemAnimTable[]; /* 0x00501590 — ROM animation table, byte[80] */
/* Random ring buffer */
extern unsigned short g_randomRingBuffer[]; /* 0x0092498C — canonical in globals_extra.c */

#define g_randTable2 ((unsigned char *)g_randomRingBuffer + 0x800) /* 0x0092518C */


/**
 * ComputeRacePositions — 0x004812A0 — 551 bytes
 * Computes each player's progress along the track spline and
 * bubble-sorts players by progress to assign 1st/2nd/3rd positions.
 *
 * playerIdx = in_EAX (int — player index, NOT player pointer)
 */
void ComputeRacePositions(int playerIdx)
{
    if (g_postRaceCameraMode != 0) {
        return;
    }

    if (g_playerBase == NULL || g_splineWaypoints == NULL) {
        return;
    }

    Player *players = (Player *)g_playerBase;
    int playerSlot = g_raceOrder[playerIdx];
    Player *player = &players[playerSlot];

    /* Skip if player has finished (completed 3 laps) */
    if (player->lapsCompleted == 3) {
        return;
    }

    /* Compute track progress */
    int playerX = player->posX >> 12;
    int playerY = -(player->posY >> 12);                   /* 0x481302: neg ebx */
    int playerZ = player->posZ >> 12;
    int nearestWP = FindNearestWaypoint((int *)g_splineWaypoints,
                                         playerX, playerY, playerZ,
                                         g_posDataCount4);
    int nextWP = nearestWP + 1;
    if (nextWP > g_posDataCount4) {
        nextWP = 0;
    }
    int *wpData = (int *)((char *)g_splineWaypoints + nextWP * 12);
    /* Sub-waypoint refinement negates posX/posZ before the shift, matching the
     * binary exactly (0x481335 neg edx / 0x48134d neg ebx). This differs from
     * FindNearestWaypoint's wp-player convention — an inconsistency present in
     * the original; the masked 21-bit term is the sub-segment tiebreaker. */
    int dx = wpData[0] - ((-player->posX) >> 12);     /* 0x481333-0x48133c */
    int dz = wpData[2] - ((-player->posZ) >> 12);     /* 0x481349-0x481356 */

    /* Progress = waypoint index * 0x200000 + sub-waypoint distance.
       lapCrossFlag (1 = behind the start/finish line, as on the starting
       grid) now travels in the network delta (field 14) along with
       lapsCompleted, so remote players use the same formula as local ones.
       Using lapsCompleted alone for them counted a remote player who had not
       yet crossed the line (or had backed over it) as almost a full lap
       ahead, so the local player could be shown 2nd while clearly 1st. */
    int lapOffset = (int)player->lapsCompleted - (int)player->lapCrossFlag;
    unsigned int progress = (unsigned int)(nearestWP + lapOffset * g_posDataCount4) * 0x200000 +
                            ((unsigned int)((dx * dx + dz * dz) >> 11) & 0x1FFFFF);

    /* Wrap-around correction — disabled in network games. The correction
       fires incorrectly at lap transitions when lapsCompleted changes
       and lapCrossFlag is in a transient state. In local-only play,
       these transitions are atomic within a single frame; in network
       play (even for the local player) the timing is less predictable. */
    if (!g_isNetworkGame) {
        unsigned int prevProgress = (unsigned int)player->trackProgress;
        /* Both comparisons are SIGNED in the binary (0x481397 jle / 0x4813b7
         * jge). The first check was wrongly unsigned, so a negative delta in
         * the finish zone (lapOffset=-1) fired it, cancelling the correct
         * lap-add and corrupting trackProgress — which then collapsed progress
         * to ~0 at the next lap line (1st→5th). */
        if ((int)(progress - prevProgress) > (int)((g_posDataCount4 - 10) * 0x200000)) {
            progress -= g_posDataCount4 * 0x200000;
        }
        if ((int)(progress - prevProgress) < (g_posDataCount4 - 10) * -0x200000) {
            progress += g_posDataCount4 * 0x200000;
        }
    }

    /* Don't count before the player first crosses the start line. */
    if (player->lapsCompleted == 0 && player->lapCrossFlag != 0)
    {
        progress = 0;
    }

    /* A player who left the online race drops to the bottom of the ranking. */
    { extern int NetPlayerGone(int); if (NetPlayerGone(playerSlot)) progress = 0; }

    /* 0x4813DC: high-water mark for wrong-way detection */
    if (progress > (unsigned int)player->progressHighWater) {
        player->progressHighWater = (int)progress;
    }
    player->trackProgress = (int)progress;

    /* Sort players by progress (only player 0 does the sort) */
    if (g_raceOrder[playerIdx] != 0) {
        return;
    }

    /* Build sort array */
    struct {
        unsigned int progress;
        int index;
    } sortBuf[MAX_PLAYERS];

    for (int i = 0; i < playerIdx; i++) {
        sortBuf[i].progress = (unsigned int)players[i].trackProgress;
        sortBuf[i].index = i;
    }

    /* Bubble sort descending */
    for (int pass = playerIdx - 1; pass > 0; pass--) {
        for (int j = 0; j < pass; j++) {
            if (sortBuf[j].progress <= sortBuf[j + 1].progress) {
                /* Swap */
                unsigned int tmpP = sortBuf[j].progress;
                int tmpI = sortBuf[j].index;
                sortBuf[j].progress = sortBuf[j + 1].progress;
                sortBuf[j].index = sortBuf[j + 1].index;
                sortBuf[j + 1].progress = tmpP;
                sortBuf[j + 1].index = tmpI;
            }
        }
    }

    /* Write race positions */
    for (int i = 0; i < g_numPlayers; i++) {
        int pIdx = sortBuf[i].index;
        if (pIdx < 0 || pIdx >= (int)g_numPlayers) {
            continue;
        }
        players[pIdx].racePosition = (short)(i + 1);
    }

    /* Network races: log the inputs of the ranking every ~10 s so position
     * disagreements between machines can be diagnosed from the logs. */
    if (g_isNetworkGame && (g_totalFrames % 300) == 0) {
        for (int i = 0; i < (int)g_numPlayers && i < 4; i++) {
            DebugLog("pos: p%d laps %d cross %d progress %u -> %d%s\n", i,
                     players[i].lapsCompleted, players[i].lapCrossFlag,
                     (unsigned)players[i].trackProgress, players[i].racePosition,
                     net_should_run_physics(i) ? " (local)" : "");
        }
    }

#if DEBUG_GP_FAST_FIRST
    /* Pair with the 1-lap finishLap override in race_timing.c. */
    if (g_raceType == RACE_GP && g_numPlayers > 0) {
        short prev = players[0].racePosition;
        if (prev != 1) {
            for (int i = 1; i < (int)g_numPlayers; i++) {
                if (players[i].racePosition < prev) {
                    players[i].racePosition++;
                }
            }
            players[0].racePosition = 1;
        }
    }
#endif
}

/**
 * UpdatePlayerAnimation — 0x00481CD0 — 538 bytes
 * Handles item collection animation responses, sector-based speed
 * boosts, and spring pad bounce oscillation.
 *
 * player = in_EAX (byte-offset access)
 */
void UpdatePlayerAnimation(Player *player)
{
    /* Clear per-frame height modifiers */
    player->effectYMod = 0x0FFFFFFF;                                             /* 0x481cd8 */
    player->itemHeightMod = 0x0FFFFFFF;                                          /* 0x481ce2 */

    /* Item collection trigger — itemResponseTimer set to 0x5A on item pickup */
    if (player->itemResponseTimer == 0x5A) {                                     /* 0x481cf0: P_INT(0x64)>>16 */
        /* Pick random animation response */
        unsigned char randByte = g_randTable2[g_randomRingIdx];
        g_randomRingIdx = (g_randomRingIdx + 1) & 0xFF;

        int racePos = (int)player->racePosition;                                 /* 0x481d30: P_INT(0x5A)>>16 */
        /* Binary 0x481d3d-0x481d50: byte table, index = (racePos-1)*20 + randByte/13 */
        int tableIdx = (racePos - 1) * 20 + randByte / 13;
        player->itemEffectId = (short)g_itemAnimTable[tableIdx];

        /* Vehicle chars with high response: force anim 4 */
        if (player->itemEffectId > 4 &&                                          /* 0x481d60: P_INT(0x62)>>16 */
            player->charId > CHAR_KNUCKLES && player->charId < CHAR_COUNT) {     /* 0x481d6a: P_SHORT(0xF2) */
            player->itemEffectId = 4;
        }

        int itemId = (int)player->itemEffectId;                                  /* 0x481d7c: P_INT(0x62)>>16 */

        if (itemId == 1) {
            player->sfxTrigger = 9; /* sound: ring */
            player->invincTimer = g_raceSpeedMult << 3;
        }

        if (itemId == 2) {
            player->ringCount += 5;
        }

        if (itemId == 3) {
            player->ringCount += 10;
        }

        if (itemId == 4) {
            player->ringCount += 20;
        }

        if (itemId == 5) {
            g_bouncePosition = 0x180;
            player->itemEffectState = -1;
            g_bounceVelocity = 0;
        }

        if (itemId == 6) {
            player->itemEffectState = g_raceSpeedMult * 30;
        }
    }

    /* Compute height modifier from response ID */
    int responseId = (int)player->itemResponseTimer;                             /* 0x481ddc: P_INT(0x64)>>16 */
    if (responseId > 0x2D) {
        int heightMod;

        if (responseId < 0x4B) {
            heightMod = 0x80000;
        }
        else {
            /* Binary 0x481E17: heightMod = (*(int*)(0x921C8C + responseId*0x100)
             * << 4) + 0x40000. 0x921C8C = g_sinTable(0x92568C) - 0x3A00, so the
             * load is g_sinTable[responseId*64 - 3712] — a sine rise arc for
             * the item popup over responseId 0x4B..0x5A (index 1088..2048).
             * (Was a never-populated flat table read = always 0.) */
            heightMod = (g_sinTable[responseId * 64 - 3712] << 4) + 0x40000;
        }

        if (player->itemEffectId != 0) {                                         /* 0x481e0e: P_SHORT(0x64) */
            player->itemHeightMod = -heightMod;
        }
    }

    /* Tick timer */
    if (responseId > 0) {
        player->itemResponseTimer -= 1;                                          /* 0x481e22: P_SHORT(0x66) */
    }

    /* Landing thud sound */
    if (responseId == 0x4B) {
        player->sfxTrigger = 0x1B;                                               /* 0x481e30 */
    }

    /* ---- Spring bounce oscillation ---- */
    int effectState = (int)player->itemEffectState;                              /* 0x481e3c: P_INT(0x80)>>16 */
    if (effectState == 0) {
        return;
    }

    if (player->renderEnabled == 0) {
        return;                                                                  /* 0x481e4a: P_INT(0xEE)>>16 */
    }

    if (effectState >= 0) {
        player->effectYMod = 0;
        return;
    }

    /* Damped sine oscillation — binary 0x481E88: the halving happens only
     * on CROSSING 0x800, judged from the pre-update side (2026-07-13: the
     * old translation paired the sides with inverted velocity signs and
     * halved on fast approach without a crossing). */
    if (g_bouncePosition >= 0x800) {
        g_bounceVelocity -= 6;
        g_bouncePosition += g_bounceVelocity;
        if (g_bouncePosition < 0x800 && g_bounceVelocity < -0x28) {
            g_bounceVelocity /= 2;
        }
    } else {
        g_bounceVelocity += 6;
        g_bouncePosition += g_bounceVelocity;
        if (g_bouncePosition >= 0x800 && g_bounceVelocity > 0x28) {
            g_bounceVelocity /= 2;
        }
    }

    player->effectYMod = g_sinTable[g_bouncePosition] >> 10;
}
