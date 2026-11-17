/**
 * player_init.c — Player physics parameter initialization
 *
 * FUN_0041e030 @ 0x0041e030 — 386 bytes — InitPlayerPhysicsParams
 *   Zeros player fields 0x108-0x1DE, computes physics params from
 *   character stats table and speed factor (DAT_0054004a).
 *
 * FUN_0041e1c8 @ 0x0041e1c8 — 673 bytes — InitAllPlayersForRace
 *   Sets up g_playerPtrTable, calls InitPlayerPhysicsParams per player,
 *   initializes waypoint/ring buffer fields.
 *
 * EAX = player pointer (Watcom fastcall).
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"

/* Forward declarations for functions defined later in this file */
void InitAIConfig(void);
void InitAllPlayersForRace(void);
void InitRaceState(void);

/* Externs for globals used by InitAIConfig / ComputeBaseSpeed / InitRaceStart */
extern unsigned char *GetAIDataBuffer(void);
extern int FindNearestSegment(int *table, int count, int worldX, int worldZ);
extern void PlaySoundEffect(int soundCmd, int distance, int freqParam); /* 0x482280 */
extern unsigned char *GetAIDataBuffer(void);
extern void BuildChaseCamera(Player *player, CamStateEntry *camStruct);  /* FUN_004240bc */

const int s_trackConfigTable[][2] = {  /* DAT_004FEB00 — 8 bytes per track */
    { 0x11, -1    },  /* track 0 (unused) */
    { 0,    0xC00 },  /* track 1 — Resort Island */
    { 2,    0xE40 },  /* track 2 — Radical City */
    { 0,    0x000 },  /* track 3 — Regal Ruin    (binary idx 4: cfg=0, angle=0x000) */
    { 0,    0xC00 },  /* track 4 — Reactive Factory (binary idx 3: cfg=0, angle=0xC00) */
    { 2,    0xA00 },  /* track 5 — Radiant Emerald */
};

/* Per-track config table — extracted from ROM at 0x4FEB00.
 * Already defined as s_trackConfigTable in InitRaceStart above, but
 * InitPlayerSpawns needs direct access to both fields. */
#define TRACK_CFG(trackId)   s_trackConfigTable[trackId][0]
#define TRACK_ANGLE(trackId) s_trackConfigTable[trackId][1]
/* Speed factor — set by FUN_0041e494 from per-track config,
 * adjusted by FUN_00421270 (rubber banding) each frame */
unsigned short g_aiSpeedFactor;    /* 0x0054004A */
unsigned short g_baseSpeedFactor;  /* 0x00540048 — base value, copied to g_aiSpeedFactor */

/* Pre-computed speed values from FUN_0041e494 (derived from g_baseSpeedFactor + 0x80) */
int g_precompMaxSpeed;   /* DAT_00540038 */
int g_precompSpeed2;     /* DAT_0054003c */
int g_precompSpeed3;     /* DAT_00540040 */
int g_precompSpeed4;     /* DAT_00540044 */
int g_rbUnknown54;       /* DAT_00540054 — per-track value (3,4,3,2), only written */
int g_aiTurnThreshold;   /* DAT_00540060 — per-track AI steering threshold */
int g_gpTopPositions = 0;/* g_gpTopPositions — GP top position flag */
extern int g_difficultyParam;   /* canonical in globals_extra.c */
unsigned char g_difficultyModeC; /* 0x8FB9A0 — difficulty/unlock state byte */
unsigned char g_gpWonBits;       /* 0x8FB9A1 — championship: tracks the player has won */
unsigned char g_gpLostBits;      /* 0x8FB9A2 — championship: tracks the player has lost */

/* DAT_004fa260 — per-track/character speed modifier table.
 * Indexed as [trackId * 16 + charId]. Byte values 0-255.
 * Extracted from SONICR.EXE .data section. */
static const unsigned char s_speedModTable[96] = {
    /* track 0 (unused):  */ 20,20,28,44,44,20,20,22,40,20, 0,0,0,0,0,0,
    /* track 1 (island):  */ 48,64,48,64,64, 0, 0, 0, 0, 0, 0,0,0,0,0,0,
    /* track 2 (city):    */229, 0, 0,16, 0, 0, 0, 0, 0, 0, 0,0,0,0,0,0,
    /* track 3 (ruin):    */ 38,43,43,48,48, 0, 0, 0, 0, 0, 0,0,0,0,0,0,
    /* track 4 (factory): */ 16,16,16,32,32, 0, 0, 0, 0, 0, 0,0,0,0,0,0,
    /* track 5 (emerald): */  8,16,16,40,48, 0, 0, 0, 0, 0, 0,0,0,0,0,0,
};

/* Spawn order — podium indices, NOT sequential. Extracted from ROM at 0x4FEB30. */
static const unsigned char s_spawnOrder[] = { 7, 6, 8, 5, 9 };

/* Array of 5 player struct pointers — g_playerPtrTable points here.
 * In the original, these were consecutive globals at 0x008FD4F4+. */
static void *s_playerPtrs[5];

/* Signed divide: val / 256 truncated toward zero (Watcom pattern) */
static inline int sdiv256(int val) { return val / 256; }
/* Signed divide: val / 32 truncated toward zero */
static inline int sdiv32(int val) { return val / 32; }

/**
 * InitPlayerPhysicsParams — FUN_0041e030 — 386 bytes
 *
 * Zeros player fields 0x108 through 0x1DE, then computes physics
 * parameters from character stats table scaled by speed factor.
 */
void InitPlayerPhysicsParams(Player *player)
{
    /* Zero fields from byte 0x108 to 0x1DE */
    memset(&player->paramMaxSpeed, 0, 0x1DE - 0x108);
    player->modelCharId = 0;
    player->_unk_0x1DC = 0;

    if (g_charStatsTable == NULL) {
        return;
    }

    int charId = player->charId;
    int *stats = g_charStatsTable + charId * 10;
    int factor = g_aiSpeedFactor + 0x80;

    /* paramMaxSpeed = SDIV256(SDIV32(stats.MaxSpeed * 0x24) * factor) */
    int val = sdiv32(stats[0] * 0x24);
    player->paramMaxSpeed = sdiv256(val * factor);

    /* paramAccel (derived from TurnRate)
     * = SDIV256(stats.TurnRate * factor) */
    player->paramAccel = sdiv256(stats[1] * factor);

    /* turnRateLimit = (DAT_005016BC[charId] * 3) / 2
     * DAT_005016BC is at stats base + 8 bytes = stats[2] (Friction field)
     * Wait — DAT_005016BC = 0x5016B4 + 8 = g_charStatsTable + 2 ints.
     * That's stats[2] = Friction. But this is used as turn limit, not friction.
     * Actually, 0x5016BC = 0x5016B4 + 0x08. In the stats table (stride 0x28):
     * offset 0x08 = field index 2 = Friction. */
    player->turnRateLimit = (short)((stats[2] * 3) / 2);

    /* paramFriction = stats[4] / 2
     * Binary 0x41E121: [eax*8 + 0x5016C4] = stats[charId*10 + 4] */
    player->paramFriction = stats[4] / 2;

    /* paramDrag = stats[3] * 4
     * Binary 0x41E147: [eax*8 + 0x5016C0] = stats[charId*10 + 3] */
    player->paramDrag = stats[3] << 2;

    /* speedCategory — the car-to-car collision radius (+0x1C6, read by the
     * collision test as [0x1C4]>>16). Binary 0x41e162-0x41e1a7: byte from
     * s_speedModTable (base DAT_004fa260, track-0 row indexed by charId),
     * scaled x9/8 in tag mode else x7/8. This is the ONLY writer of +0x1C6,
     * so it must run for every player — otherwise AI cars keep radius 0 and
     * the ((rA+rB)/4)^2 collision threshold collapses. */
    int charSpeedByte = s_speedModTable[charId];               /* 0x41e169 / 0x41e18b */
    if (g_raceSubMode == SUBMODE_TAG) {
        player->speedCategory = (short)((charSpeedByte * 9) >> 3);   /* 0x41e191 */
    }
    else {
        player->speedCategory = (short)((charSpeedByte * 7) >> 3);   /* 0x41e175 */
    }
}

/**
 * InitAllPlayersForRace — FUN_0041e1c8 — 673 bytes
 *
 * Called once at race start. Sets up player pointer table,
 * initializes physics params for all 5 players, sets initial
 * waypoint/ring buffer state.
 */
void InitAllPlayersForRace(void)
{
    g_avoidanceBias = 0x30;
    g_avoidanceCounter = 0x0C;

    /* Set up player pointer table — array of 5 player struct pointers.
     * In the original binary, g_playerPtrTable = &g_playerBase where
     * g_playerBase and the next 4 pointers were consecutive globals.
     * We use a static array instead. */
    for (int i = 0; i < 5; i++) {
        s_playerPtrs[i] = &g_playerBase[i];
    }
    g_playerPtrTable = s_playerPtrs;

    /* Speed values precomputed by FUN_0041e494 (called from InitRaceStart) */

    /* Init physics for player 0 (human player) */
    Player *p0 = &g_playerBase[0];
    InitPlayerPhysicsParams(p0);

    /* Binary 0x41e20e-0x41e23b writes player0->speedCategory = RAW
     * s_speedModTable[charId] here, but InitPlayerPhysicsParams(p0) above
     * (binary 0x41e29d) immediately overwrites it with the scaled x7/8 value —
     * so that write is dead. speedCategory is now set for all 5 players inside
     * InitPlayerPhysicsParams; no separate player-0 write is needed. */

    /* Compute starting waypoint index (binary 0x41e22d-0x41e28e).
     * Uses FindNearestSegment for tracks 1-2, hardcoded for 3-5.
     * Binary args: EAX=waypointBase, EDX=packed(X|Y), EBX=Z>>12, ECX=segCount */
    int startingWaypoint = 0;
    if (g_waypointDataBase != NULL) {
        int searchX = p0->posX >> 12;                           /* EAX, then shl 16 */
        int searchY = p0->posY >> 12;                           /* EDX before add */
        int searchZ = p0->posZ >> 12;                           /* EBX */
        int segCount = *(int *)((char *)g_segmentOffsetTable);  /* ECX = *[0x540068] */
        int packedXY = searchY + (searchX << 16);               /* EDX = Y + (X<<16) */
        startingWaypoint = FindNearestSegment(
            (int *)g_waypointDataBase, segCount, packedXY, searchZ);
    }
    /* Per-track overrides (binary jump table at 0x41e1b4, verified via
     * capstone 2026-04-14). Only City and Emerald hardcode the starting
     * waypoint; Island, Ruin, and Factory all use the FindNearestSegment
     * result computed above. Prior translation had 0xA6 on tracks 3+4
     * instead of 2 — a swap that caused Factory AI to steer toward a
     * wrong waypoint on frame 1 after GO. */
    switch (g_trackId) {
        case TRACK_RADICAL_CITY:
            startingWaypoint = 0xA6;
            break;
        case TRACK_RADIANT_EMERALD:
            startingWaypoint = 0xA8;
            break;
    }

    /* Init AI players 1-4 — binary loop: ebx=1..4, edx=4..16
     * Player 0 (human) only gets InitPlayerPhysicsParams above. */
    for (int i = 1; i < 5; i++) {
        Player *player = &g_playerBase[i];

        InitPlayerPhysicsParams(player);

        /* AI fields — binary 0x41e2be-0x41e3b0, order matches binary */
        player->aiSteerCurrent = 0x7F;                     /* AI steer target */
        player->waypointIdx = startingWaypoint;             /* waypoint — player+0x1A8 */
        player->aiSegmentIdx = 0;                           /* segment index — player+0x11C */
        player->aiAccumDist = 0;                            /* accumulated distance — player+0x120 */
        player->aiSpeed = 0x3E8;                            /* speed — player+0x124, overwritten below */
        player->aiAccel = g_precompMaxSpeed;                /* acceleration — player+0x128 */
        player->aiSteerTarget = 0x80;                       /* AI direction */

        /* Yaw — from player 0's initial facing (binary reads [0x8fd504]) */
        int initialYaw = g_playerBase[0].angleYaw;
        player->angleYaw = initialYaw;                      /* player+0x10 = full 32-bit yaw */
        player->targetYaw = (unsigned short)initialYaw;     /* 16-bit word, no mask */

        player->speedModifier = (unsigned short)g_precompMaxSpeed;
        player->_unk_0x1BE = (short)g_precompSpeed2;
        player->maxSpeedCap = g_precompSpeed4;              /* max speed cap — player+0x1B0 */
        player->physicsConst0x1B4 = (short)g_precompSpeed3;
        player->speedField = g_aiSpeedFactor;
        player->aiSpeed = g_precompMaxSpeed;                /* final speed — player+0x124 */

        /* Ring buffer init */
        player->ring.head = 0;                              /* ring buffer head (0x158) */
        player->ring.tail = 0;                              /* ring buffer tail (0x15C) */
        player->ring.segId[0] = 0xFF;

        /* Direction */
        player->directionFlip = 0;
        player->_pad_0x1CF = 0;

        /* Tag mode init — binary 0x41e3ee-0x41e45e
         * When g_raceSubMode==2: set direction flags, alternate yaw for odd players */
        if (g_raceSubMode == SUBMODE_TAG) {
            player->turnTimer = 0xB4;                       /* tag mode timer/threshold */
            player->directionFlip = (unsigned char)(i & 1); /* alternating direction */
            player->_pad_0x1CF = 0;
            if ((i & 1) == 1) {
                player->angleYaw += 0x800;                  /* rotate 180° */
                player->angleYaw &= 0xFFF;                  /* mask to 12 bits */
                if (g_trackId == TRACK_RADIANT_EMERALD) {
                    player->waypointIdx = 0xA5;             /* waypoint override for track 5 */
                }
            }
        }
    }
}

/**
 * ComputeBaseSpeed — FUN_00421608 — 378 bytes
 * Computes g_baseSpeedFactor (DAT_00540048) from difficulty level,
 * character type, track, and game mode.
 *
 * Original: in_EAX = difficulty level (0/1/2).
 */
void ComputeBaseSpeed(int difficulty)
{
    extern int g_gpTopPositions;

    int charId = g_playerBase[0].charId;                     /* 0x8fd5e6 */
    int speedTier = difficulty * 2 + 1;                       /* 1-7 based on difficulty */
    if ((charId == CHAR_SUPER_SONIC && g_trackId == TRACK_RADIANT_EMERALD) || g_raceType == RACE_SPECIAL) {
        speedTier = 5;                                        /* forced tier for Metal Sonic/tag */
    }
    unsigned int unlockState = (unsigned int)g_difficultyParam;  /* 0x8fb97c */
    if (g_demoMode != DEMO_REPLAY) {
        unlockState = (unsigned int)g_difficultyModeC; /* byte at 0x8fb9a0 */
    }
    if (g_demoMode != DEMO_TITLE && unlockState == 0x7f) {
        speedTier++;                                          /* bump tier if fully unlocked */
    }
    int baseSpeed;
    switch (speedTier) {
        case 1:
            baseSpeed = 0x38;
            break;
        case 2:
            baseSpeed = 0x48;
            break;
        case 3:
            baseSpeed = 0x50;
            break;
        case 4:
            baseSpeed = 0x68;
            break;
        case 5:
            baseSpeed = 0x80;
            break;
        case 6:
            baseSpeed = 0xa0;
            break;
        case 7:
            baseSpeed = 0xc0;
            break;
        default:
            baseSpeed = 0x28;
            break;
    }
    /* Per-character/track speed penalty — binary 0x4216a3-0x4216dd */
    int penalty = (unsigned int)s_speedModTable[charId + g_trackId * 0x10] * (speedTier + 1);
    unsigned int speed = baseSpeed - (penalty / 32);

    if (g_gpTopPositions == 1 && g_demoMode != DEMO_TITLE) {          /* binary 0x4216df: ×2/3 */
        speed = (speed & 0xffff) - (int)((speed & 0xffff) / 3);
    }
    if (g_raceSubMode == SUBMODE_TAG) {                                  /* binary 0x421702: halve for tag mode */
        speed = (speed >> 1) & 0x7fff;
    }
    if (g_trackId == TRACK_REGAL_RUIN) {                                     /* binary 0x42170e: Ruin speed reduction */
        /* Binary applies this at [0x8fb8ec]==4. Our port swaps the Ruin/Factory
         * trackIds 3<->4 to their intended meaning (Ruin=3, Factory=4), and
         * s_speedModTable rows 3/4 are correspondingly swapped vs ROM 0x4fa260 —
         * so the Ruin reduction is case 3 HERE, not case 4. Do NOT "fix" to 4;
         * verified against 0x4fa260 (our row 3 == binary row 4 == Ruin). */
        if (difficulty == 2) {
            speed = (speed & 0xffff) - ((int)(speed & 0xffff) >> 2);  /* ×3/4 */
        }
        else {
            speed = (speed & 0xffff) - (int)((speed & 0xffff) / 3);  /* ×2/3 */
        }
    }
    if ((speed & 0xffff) > 200) {
        speed = 200;                /* binary 0x421755: clamp 32-200 */
    }
    if ((speed & 0xffff) < 32) {
        speed = 32;
    }
    g_baseSpeedFactor = (unsigned short)speed;               /* binary 0x421775: mov [0x540048],bx */
}

/**
 * InitPlayersForLevel — FUN_00471eb0 — 16 bytes
 * Wrapper called from InitLevel. Runs AI config then player init.
 */
void InitPlayersForLevel(void)
{
    InitAIConfig();                                      /* FUN_0041e494 */
    InitAllPlayersForRace();                             /* FUN_0041e1c8 */
}

/**
 * InitAIConfig — FUN_0041e494 — 519 bytes
 * Sets up AI data pointers, calls ComputeBaseSpeed, sets auto-steer flag,
 * configures per-track rubber banding, and precomputes speed values.
 */
void InitAIConfig(void)
{
    /* AI data pointers from loaded AI buffer.
     * Original uses fixed BSS addresses. Our AI data is loaded into
     * a buffer by LoadAI(). These point into that buffer. */
    unsigned char *aiBase = GetAIDataBuffer();
    if (aiBase != NULL) {
        g_waypointDataBase   = aiBase;              /* line 187: 0x00944A8C base */
        g_segmentOffsetTable = aiBase + 0x4000;     /* line 188: +0x4000 */
        g_aiGridSurface      = aiBase + 0x4200;     /* line 189: +0x4200 */
        g_aiGridGround       = aiBase + 0x5000;     /* line 190: +0x5000 */
        g_charRampSpeedTable = aiBase + 0x5400;     /* line 191: +0x5400 */
    }

    /* Both branches call FUN_00421608 (ComputeBaseSpeed) — the difference is
     * the EAX argument. Binary 0x41e4cc-0x41e4ed:
     *   edx = g_demoMode
     *   if (g_demoMode == 1) eax = edx  (i.e. g_demoMode, == 1 in this branch)
     *   else                 eax = [0x8fd444] = g_difficultyConfig
     * The title/attract branch passes g_demoMode (=1), NOT 0 — so the demo AI
     * runs at difficulty 1 (speedTier 3), matching the demo header. */
    if (g_demoMode == DEMO_TITLE) {
        ComputeBaseSpeed(g_demoMode);
    } else {
        ComputeBaseSpeed(g_difficultyConfig);
    }

    g_aiSpeedFactor = g_baseSpeedFactor;
    g_autoSteerFlag = 0x5A;

    switch (g_trackId) {
        case TRACK_RESORT_ISLAND:
            g_rbUnknown54 = 3;
            g_rubberBandThreshold = 10;
            g_rubberBandDivisor = 0x50;
            g_aiTurnThreshold = 0x4E;
            g_rubberBandLowerBound = -3;
            break;
        case TRACK_RADICAL_CITY:
            g_rbUnknown54 = 4;
            g_rubberBandThreshold = 0x0C;
            g_rubberBandDivisor = 0x60;
            g_aiTurnThreshold = 0x66;
            g_rubberBandLowerBound = -4;
            break;
        case TRACK_REGAL_RUIN:
        case TRACK_REACTIVE_FACTORY:
            g_rbUnknown54 = 3;
            g_rubberBandThreshold = 10;
            g_rubberBandDivisor = 0x50;
            g_aiTurnThreshold = 200;
            g_rubberBandLowerBound = -3;
            break;
        case TRACK_RADIANT_EMERALD:
            g_rbUnknown54 = 2;
            g_rubberBandThreshold = 5;
            g_rubberBandDivisor = 0x20;
            g_aiTurnThreshold = 0x31;
            g_rubberBandLowerBound = -2;
            break;
    }

    /* precompute speed values */
    int speedBase = g_baseSpeedFactor + 0x80;
    g_precompMaxSpeed = speedBase * 0x1194 >> 8;
    g_precompSpeed2 = speedBase * 0x2EE >> 8;
    g_precompSpeed3 = speedBase * 0x226 >> 8;
    int v = speedBase * 0x6000;
    int sign = v >> 31;
    g_precompSpeed4 = (int)((v + sign * -0x100) - (unsigned int)(sign << 7 < 0)) >> 8;
}

/* =====================================================================
 * Player spawn/slot init 
 * ===================================================================== */

/**
 * InitPlayerSpawns — FUN_0047108c — 725 bytes
 * Sets starting positions, heading, and track config for all 5 players
 * from g_podiumCenter data. Handles GP, Time Attack, and multiplayer
 * spawn position overrides.
 */
void InitPlayerSpawns(void) {
    int *podium = (int *)g_podiumCenter;
    if (podium == NULL) {
        return;
    }

    /* Basic spawn for all 5 players */
    for (int i = 0; i < 5; i++) {
        Player *player = &g_playerBase[i];
        int srcIdx = (int)s_spawnOrder[i] * 3;

        player->posX = podium[srcIdx + 0] << 12;           /* posX */
        player->posY = podium[srcIdx + 1] * -0x1000;       /* posY (negated) */
        player->posZ = podium[srcIdx + 2] << 12;           /* posZ */

        /* Per-track heading angle and config */
        player->angleYaw = TRACK_ANGLE(g_trackId);
        player->collisionLayer = TRACK_CFG(g_trackId);

        /* Clear velocity/state fields — verified against binary */
        player->lap1Time = 0;   /* +0x50 */
        player->lap2Time = 0;   /* +0x54 */
        player->lap3Time = 0;   /* +0x58 */
        player->anglePitch = 0; /* byte +0x0C */
        player->angleRoll = 0;  /* byte +0x14 */
    }

    /* Multiplayer mode — adjust positions for 2P/4P split screen */
    if (g_raceType == RACE_MULTIPLAYER) {
        Player *pl0 = &g_playerBase[0];
        Player *pl1 = &g_playerBase[1];

        if (g_numPlayers == 2) {
            /* 2-player: use podium positions 6 and 8 directly */
            pl0->posX = podium[6 * 3 + 0] << 12;
            pl0->posY = podium[6 * 3 + 1] * -0x1000;
            pl0->posZ = podium[6 * 3 + 2] << 12;
            pl1->posX = podium[8 * 3 + 0] << 12;
            pl1->posY = podium[8 * 3 + 1] * -0x1000;
            pl1->posZ = podium[8 * 3 + 2] << 12;
        }
        else if (g_numPlayers == 4) {
            /* 4-player: average adjacent podium positions for even spacing */
            Player *pl2 = &g_playerBase[2];
            Player *pl3 = &g_playerBase[3];

            pl0->posX = (podium[6 * 3 + 0] + podium[7 * 3 + 0]) * 0x800;
            pl0->posY = (podium[6 * 3 + 1] + podium[7 * 3 + 1]) * -0x800;
            pl0->posZ = (podium[6 * 3 + 2] + podium[7 * 3 + 2]) * 0x800;
            pl1->posX = (podium[7 * 3 + 0] + podium[8 * 3 + 0]) * 0x800;
            pl1->posY = (podium[7 * 3 + 1] + podium[8 * 3 + 1]) * -0x800;
            pl1->posZ = (podium[7 * 3 + 2] + podium[8 * 3 + 2]) * 0x800;
            pl2->posX = (podium[5 * 3 + 0] + podium[6 * 3 + 0]) * 0x800;
            pl2->posY = (podium[5 * 3 + 1] + podium[6 * 3 + 1]) * -0x800;
            pl2->posZ = (podium[5 * 3 + 2] + podium[6 * 3 + 2]) * 0x800;
            pl3->posX = (podium[8 * 3 + 0] + podium[9 * 3 + 0]) * 0x800;
            pl3->posY = (podium[8 * 3 + 1] + podium[9 * 3 + 1]) * -0x800;
            pl3->posZ = (podium[8 * 3 + 2] + podium[9 * 3 + 2]) * 0x800;
        }
    }

    /* Time Attack — override player 0 start position.
     * Both paths shift inline (verified against binary 0x47126A-0x4712C7). */
    if (g_raceType == RACE_TIMEATTACK && g_raceSubMode != SUBMODE_TAG) {
        Player *pl0 = &g_playerBase[0];
        Player *pl1 = &g_playerBase[1];

        /* reverse time attack: podium index 10 (offset 0x78) */
        if (g_raceSubMode == SUBMODE_REVERSE) {
            pl0->posX = podium[10 * 3 + 0] << 12;
            pl0->posZ = podium[10 * 3 + 2] << 12;
            pl0->posY = -(podium[10 * 3 + 1]) << 12;
            /* Rotate starting heading by 0x800 (180°) */
            pl0->angleYaw = (pl0->angleYaw + 0x800) & 0xFFF;
        }
        /* Normal time attack: podium index 7 (offset 0x54) */
        else {    
            pl0->posX = podium[7 * 3 + 0] << 12;
            pl0->posY = -(podium[7 * 3 + 1]) << 12;
            pl0->posZ = podium[7 * 3 + 2] << 12;
        }

        /* Copy player 0 state to player 1 (ghost) — 0x4712CD */
        pl1->posX = pl0->posX;
        pl1->posY = pl0->posY;
        pl1->posZ = pl0->posZ;
        pl1->anglePitch = pl0->anglePitch;
        pl1->angleYaw = pl0->angleYaw;
        pl1->angleRoll = pl0->angleRoll;
        pl1->collisionLayer = pl0->collisionLayer;
    }

    /* Set per-player race positions (offset +0x5C) — 0x471313-0x471347.
     * Binary: word ptr writes (shorts, NOT ints). Values 1-5 verified from disasm. */
    g_playerBase[1].racePosition = 2;  /* 0x47132C: mov word ptr [0x8fdc6c], bx=2 */
    g_playerBase[2].racePosition = 3;  /* 0x471333: mov word ptr [0x8fe388], cx=3 */
    g_playerBase[3].racePosition = 4;  /* 0x47133A: mov word ptr [0x8feaa4], si=4 */
    g_playerBase[4].racePosition = 5;  /* 0x471341: mov word ptr [0x8ff1c0], eax=5 */
    g_playerBase[0].racePosition = 1;  /* 0x471347: mov word ptr [0x8fd550], edx=1 */
}

/**
 * InitPlayerSlot — FUN_00485120 — 646 bytes — VALIDATED
 * Zeros most of the player struct fields, copies position to backup,
 * sets character ID. Every field offset, size, and value verified
 * against binary.
 *
 * Watcom fastcall: EAX = player pointer, EDX = charId.
 */
void InitPlayerSlot(Player *player, int charId)
{
    /* Zero state fields — faithful to binary field-by-field */
    player->_unk_0x1A = 0;
    player->_unk_0x1C = 0;
    player->velX = 0;
    player->velY = 0;
    player->velZ = 0;
    player->dynamicSpeedMode = 0;
    player->_unk_0x42 = 0;
    player->forwardSpeed = 0;
    player->lateralSpeed = 0;
    player->trackProgress = 0;
    player->lapsCompleted = 0;
    player->lapCrossFlag = 1;
    player->_unk_0x62 = 0;
    player->itemEffectId = 0;
    player->itemResponseTimer = 0;
    player->yOffset = 0;
    player->brakeCounter = 0;
    player->_unk_0x6E = 0;
    player->groundedFlag = 1;
    player->_unk_0x74 = 0;
    player->jumpCounter = 0;
    player->_unk_0x78 = 0;
    player->_unk_0x7C = 0;
    player->_unk_0x7A = 0;
    player->abilityTimer = 0;
    player->_unk_0x80 = 0;
    player->itemEffectState = 0;
    player->_unk_0x84 = (short)0xFFFF;
    player->_unk_0x86 = 0;
    player->invincTimer = 0;
    player->_unk_0x94 = 0;
    player->airTimer = 0;

    /* Copy position to backup fields */
    player->prevPosX = player->posX;         /* backupX = X */
    player->prevPosY = player->posY;         /* backupY = Y */
    player->prevPosZ = player->posZ;         /* backupZ = Z */
    player->groundHeight = player->posY;     /* another Y copy */

    player->_unk_0x3C = 0;
    player->lapCountInit = (short)g_configLapCount;

    /* Copy initial yaw from field 0x10 to 0x70 */
    player->moveMode = (short)player->angleYaw;

    player->ringCount = 0;
    player->_unk_0x9A = 0;
    /* 0x48523A: mov dword [reg+0x9c], 0 — the binary is clearing the animation
     * frame-stream cursor. On 64-bit that pointer does not fit the 4-byte
     * struct field, so the live cursor is g_animDataPtrs[playerSlot] and
     * _unk_0x9C is dead. Clearing only the dead int left the cursor as the one
     * piece of animation state that survives a player re-init — which matters
     * for SetupSpecialRace, since it re-inits players and jumps straight to
     * the race with no InitLevel. See [[feedback_64bit_ptr_truncation]]. */
    player->_unk_0x9C = 0;
    {
        int animSlot = (int)(player - g_playerBase);
        if (animSlot >= 0 && animSlot < 10) {
            g_animDataPtrs[animSlot] = NULL;
        }
    }
    player->loopMode = 0;
    player->_unk_0xA2 = 0;
    player->hitSurfaceIdx = 0;
    player->hitEdgeIdx = 0;
    player->overSurface = 0;
    player->prevOverSurface = 0;
    player->surfNormX = 0;
    player->surfNormY = 0;
    player->surfNormZ = 0;
    player->_unk_0xBA = 0;
    player->_unk_0xBC = 0;
    player->loopLocalX = 0;
    player->loopLocalZ = 0;
    player->loopVelX = 0;
    player->loopVelZ = 0;
    player->pitchCombo = 0;
    player->yawDelta = 0;
    player->_unk_0xD6 = 0;
    player->_unk_0xDC = 0;
    player->_unk_0xDE = 0;
    player->progressHighWater = 0;
    player->savedVelocity = 0;
    player->prevAnimId = (short)0xFFFF;
    player->sfxTrigger = (short)0xFFFF;
    player->renderEnabled = 0;
    player->animId = 0;

    /* Character ID — from EDX parameter */
    player->charId = (short)charId;

    player->_unk_0xF8 = 0;
    player->_unk_0xFC = 0;
    player->abilityState = 0;

    /* Copy charId to model index field at +0x1E0 */
    short finalCharId = player->charId;
    player->_unk_0x1E4 = 0;
    player->_unk_0x1E8 = 0;
    player->animFrameIdx = 0;
    player->collisionCount = 0;
    player->_unk_0x1F8 = 0;
    player->renderState = 0;
    player->_unk_0x1E0 = finalCharId;
}


/**
 * InitRaceStart — FUN_00471a04 — 137 bytes
 * Called at race start and race restart (special race, GP advance).
 * Sets intro countdown, zeroes lap times, sets per-track start config,
 * then calls AI setup, player init, camera setup, and race config.
 *
 * Per-track table at DAT_004FEB00: 8 bytes per track entry.
 *   [0] = track config value (stored to player0[0xA4] and player1[0xA4])
 *   [4] = start angle (used elsewhere, not by this function)
 */
void InitRaceStart(void)
{
    if (g_demoMode == DEMO_NONE) {
        g_introCountdown = 0xD2;
    }
    else {
        g_introCountdown = 0x3C;
    }
    g_playerBase[0].lap3Time = 0;
    g_playerBase[0].lap2Time = 0;
    g_playerBase[0].lap1Time = 0;

    /* Player 1 fields zeroed */
    Player *pl1 = &g_playerBase[1];
    pl1->lap3Time = 0;
    pl1->lap2Time = 0;

    /* Player 0 field 0xA4 = track config value from table */
    Player *pl0 = &g_playerBase[0];
    int trackCfg = s_trackConfigTable[g_trackId][0];
    pl0->collisionLayer = trackCfg;

    /* NOTE: starting yaw is set in main.c after InitRaceStart, NOT here. */

    pl1->lap1Time = 0;
    pl1->collisionLayer = trackCfg;

    InitAIConfig();
    InitAllPlayersForRace();
    /* g_camStateTable is the smoothedCam used by RenderScene3D's camera
     * interpolation; initializing it here seeds the smoothing with the correct
     * starting camera position so the flyover transition doesn't snap. */
    BuildChaseCamera(g_playerBase, &g_camStateTable[0]);
    /* BuildChaseCamera → ComputeLookAtAngles writes yaw as a short at
     * camState+0x14, which corrupts the low 16 bits of camState[5].
     * The smoothing divisor reads camState[5] >> 16, so set the high
     * 16 bits to the default detail level (0x20 → divisor = 8). */
    g_camStateTable[0].fovDetail = 0x20;

    InitRaceState();
}

/* =====================================================================
 * ClearPlayerSFXState — FUN_00496ac8 — 190 bytes — called 1x
 *
 * Drains the deferred per-player SFX queue: physics code stashes a sound
 * ID in sfxTrigger during the frame, and this plays it and re-arms the
 * slot.  For anim states 3/4 on Tails with surface type 0xC/0xD, the
 * queued sound is discarded instead of played.
 *
 * The loop bound g_numViewports is the count of local human players, so
 * AI racers are never reached.  In a network game every player has a
 * viewport index but only one is ours, so the gate at 0x496aec skips
 * remote players outright — their sounds happen on their own machine.
 *
 * Player struct offsets: +0xe8 = anim state (high 16), +0xea = sub-state,
 * +0xf2 = character type, +0x96 = surface type (high 16).
 * ===================================================================== */
void ClearPlayerSFXState(void)                            /* 0x496ac8 */
{
    for (int i = 0; i < g_numViewports; i++) {            /* 0x496b54 */
        Player *player = &g_playerBase[i];
        int isLocal;

        if (g_netSessionActive != 0) {                       /* 0x496b62 */
            /* In multiplayer, only the local player is "local" */
            unsigned short localIdx = *(unsigned short *)&g_localPlayerIndex;  /* 0x68acdc */
            isLocal = (i == (int)localIdx) ? 1 : 0;       /* 0x496b77 */
        }
        else {
            isLocal = 1;                                  /* 0x496ae7 */
        }

        if (!isLocal) {                                   /* 0x496aec/0x496aee: je 0x496b4d */
            continue;
        }

        int animState = player->sfxTrigger;               /* 0x496af0 — upper 16 of int at 0xE8 = short at 0xEA */
        if ((animState == 3 || animState == 4) &&         /* 0x496af9 */
            player->charId == CHAR_TAILS &&               /* 0x496b03 */
            (player->animId == 0xC ||                     /* 0x496b18 — upper 16 of int at 0x96 = short at 0x98 */
             player->animId == 0xD))
        {                                                 /* 0x496b1d */
            player->sfxTrigger = (short)0xFFFF;           /* 0x496b22 */
        }

        if (player->sfxTrigger != -1) {                   /* 0x496b2b */
            PlaySoundEffect((unsigned short)player->sfxTrigger, 0, 0); /* 0x496b3f: EAX=sfxTrigger */
            player->sfxTrigger = (short)0xFFFF;           /* 0x496b44 */
        }
    }
}
