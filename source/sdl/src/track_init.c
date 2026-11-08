/**
 * track_init.c — Per-track initialization
 *
 * InitLevel dispatches to per-track loaders.
 * Each track loader: sets tpage assignments, loads textures, loads geometry.
 * See sonicr_annotated.c Track Loading section for full documentation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"

#define P_INT(p, off) (*(int *)((char *)(p) + (off)))

/* Per-track init functions (defined in track_per_level.c) */
extern void InitIsland(void);      /* 0x004732BC — Resort Island */
extern void InitCity(void);        /* 0x00473954 — Radical City */
extern void InitFactory(void);     /* 0x00474744 — Reactive Factory */
extern void InitRuin(void);        /* 0x0047405C — Regal Ruin */
extern void InitEmerald(void);     /* 0x00474CE8 — Radiant Emerald */

/* Sub-functions called by InitLevel */
extern void InitPlayersForLevel(void);      /* 0x471eb0 */
extern void ResetAllRaceState(void);        /* 0x471ec0 */
extern void InitTrackCommon(void);          /* 0x4723AC */
extern void InitTrackTextures(void);        /* 0x4754EC */
extern void ParseTerrainHeader(void);       /* 0x4d8904 */
void InitPlayerSpawns(void);                /* 0x47108c */
void InitPlayerSlot(Player *player, int charId);  /* 0x485120 */
void InitRaceState(void);                   /* 0x4751E4 */
void InitFarClipAndFog(int farClip);        /* 0x4704F0 */
void RemapCharacterTpages(void);            /* 0x470460 */
void RemapBalloonTpages(void);              /* 0x470564 */
void InitSoftwareTerrain(void);             /* 0x46F6B4 */
void InitTrackAnimObjects(void);            /* 0x47FA64 */
extern int g_tpageDirty;
extern int g_renderStateBlock[];           /* 0x008F6F60 */
extern int g_checkpointB[10];              /* 0x008FB8F8 */
extern int g_lapDistanceThreshold;         /* 0x008FB920 */
extern int g_checkpointA[10];              /* 0x008FB924 */
extern int g_lapDistanceThreshold2;        /* 0x008FB934 */
extern int g_raceLimitA;                   /* 0x00901DF0 */
extern int g_raceTimerA;                   /* 0x00901DF4 */
extern int g_raceMaskValue;                /* 0x00901E88 */
extern int g_raceLimitB;                   /* 0x00901E94 */
extern int g_raceTimerE98;                 /* 0x00901E98 */
extern int g_raceState9C;                  /* 0x00901E9C */
extern int g_raceStateEBC;                 /* 0x00901EBC */
extern int g_raceStateEC0;                 /* 0x00901EC0 */
extern int g_raceStateEC4;                 /* 0x00901EC4 */
extern int g_gpCamBase[];                  /* 0x00901ED0 */
extern int g_lapDistTrackLen;              /* 0x00902218 */
extern int g_lapDistFarClip;               /* 0x0090221C */
extern int g_lapDistRace;                  /* 0x00902220 */
extern int g_lapDistRaceAlt;               /* 0x00902224 */
extern int g_lapDistExtra;                 /* 0x00902478 */
extern int g_raceCountdownA;               /* 0x0090247C */
extern int g_raceCountdownB;               /* 0x00902480 */
extern short g_lapCountMinusOne;           /* 0x0094AAC2 */

void R_ClearNoColorKey(int tpage);
void SetWeatherTint(void);
void BuildChaseCamera(Player *player, CamStateEntry *camStruct);

static int    s_fogDistTable[16];   /* 0x006D761C — fog distance thresholds */
static float  s_fogRecipTable[16];  /* 0x006D75DC — 1.0 / (dist * fogScale) */
int           g_farClipTimes8;      /* 0x008FB360 — non-static for sprite renderers */

#ifdef SONICR_DC_240P
#define SPLIT_QUALITY_2P  4   /* two viewports   — "far", one below max */
#define SPLIT_QUALITY_3P  3   /* three viewports — "medium" */
#define SPLIT_QUALITY_4P  2   /* four viewports  — "near" */
#else
/* Draw distance for split-screen modes, in g_qualityLevel units (menu range
 * 0-4, 0 = nearest). These REPLACE the menu setting in those modes; a single
 * viewport is never affected. Applied in the far-clip setup below. */
#define SPLIT_QUALITY_2P  4   /* two viewports   — "far", one below max */
#define SPLIT_QUALITY_3P  3   /* three viewports — "med" */
#define SPLIT_QUALITY_4P  2   /* four viewports  — "med" */
#endif

extern void StopCD(void);

extern int g_unk_8FD4F0;                    /* 0x8fd4f0 */
extern int g_unk_901C0C;                    /* 0x901c0c */
extern int g_unk_901C1C;                    /* 0x901c1c */
extern int g_unk_901C20;                    /* 0x901c20 */
extern int g_unk_901C24;                    /* 0x901c24 */
extern int g_bouncePosition2;               /* 0x901c5c */
extern int g_bounceVelocity2;               /* 0x901c60 */
extern unsigned short g_randomRingBuffer[]; /* 0x92498c */
extern void ResetPauseMenuState(void);

/* Externs for globals not in sonicr_globals.h */
extern int g_trackAnimTickRate;
extern unsigned short g_introInputRaw;
extern int g_raceStateBlock10[];
extern int g_unk_901C40, g_unk_901C3C;
extern int g_unk_8F7074;
extern int g_unk_901C7C, g_unk_901C74;
extern int g_unk_901CCC;
extern int g_unk_901CE4, g_unk_901CEC, g_unk_901CE8;
extern int g_vpTimerA[], g_vpTimerB[], g_vpTimerC[], g_vpTimerD[], g_vpTimerE[];
extern int g_unk_901DE8, g_unk_901DEC;
extern int g_raceTimerBlock[];
extern int g_unk_901E8C, g_unk_901E90;
extern int g_unk_901EA0, g_unk_901EA4, g_unk_901EA8, g_unk_901EAC;
extern int g_unk_901EB0, g_unk_901EB4, g_unk_901EB8;
extern int g_unk_901EC8, g_unk_901ECC;
extern int g_unk_901FF4;
extern int g_camWaypointCount;
extern int g_unk_902060, g_unk_90205C, g_unk_902064, g_unk_902068, g_unk_90206C;
extern int g_unk_9020BC;
extern short g_inputStateE0, g_inputStateDE, g_inputStateDC;
extern short g_inputStateE2, g_inputStateE4, g_inputStateE6, g_inputStateE8;
extern short g_inputStateEA;
extern short g_inputStateFC, g_inputStateFE, g_inputState100;
extern short g_inputState102, g_inputState104, g_inputState106;
extern short g_inputState124, g_inputState126, g_inputState128;
extern short g_inputState12A, g_inputState12C, g_inputState12E;
extern short g_inputState1F0, g_inputState1F2;
extern int g_unk_902118, g_unk_90211C, g_unk_902120;
extern int g_unk_9021F4, g_unk_9021F8, g_unk_9021FC, g_unk_902200;
extern int g_unk_902228, g_unk_902234;
extern int g_unk_90247C, g_unk_902480, g_unk_902484, g_unk_902488;
extern int g_unk_90248C, g_unk_902490;
extern int g_unk_9024A4, g_unk_9024A8;
extern int *g_camWaypointTable;
/* g_introSplineBase (0x00902498) is declared in sonicr_globals.h */
extern int g_unk_8FB8B0[];
extern sr_double g_fogDistConst;     /* 0x0052C63C — fog distance scale = 8.0 */
extern unsigned char g_sceneryTpageA[];
extern unsigned char g_sceneryTpageB[];
extern OtherParticle g_particleArray[];
extern int g_nextParticleSlot;
extern void InitWeatherProps_Island(void);  /* 0x0046E450 — in weather.c */
extern void InitWeatherProps_Factory(void);    /* 0x0046E820 — in weather.c */
extern void InitWeatherProps_Emerald(void); /* 0x0046E9E0 — in weather.c */


/**
 * InitLevel — 0x0047571C — 1595 bytes
 * Master track setup function. Dispatches to per-track loader,
 * then does shared post-load work.
 */
void InitLevel(void)
{
    StopCD();

    R_ClearNoColorKey(g_tpageCount);

    /* g_isMultiRace (0x008FB94C) — true iff g_raceType == RACE_TYPE_MULTIPLAYER (1).
     * Gates 2P split-screen camera framing, lap distances, leaf/physics branches. */
    g_isMultiRace = (g_raceType == RACE_MULTIPLAYER) ? 1 : 0;

    InitTrackCommon();
    InitTrackTextures();

    /* Dispatch to per-track loader. Each per-track init loads its parallax
     * and calls SetWeatherTint (sets g_weatherR/G/B) BEFORE LoadTrackData
     * tints the gourauds — faithful to the binary (each LoadParallax* /
     * InitEmerald does `call 0x42cd14; call 0x4E0758`). Previously a single
     * SetWeatherTint() was called HERE, after the switch, which tinted with
     * the prior race's weather color (dawn tint bled into the next race). */
    switch (g_trackId) {
        case 1:
            InitIsland();
            break;
        case 2:
            InitCity();
            break;
        case 3:
            InitRuin();
            break;
        case 4:
            InitFactory();
            break;
        case 5:
            InitEmerald();
            break;
    }

    /* Zero ring collection state — original clears the 4th int (offset 12)
     * of each 16-byte ring entry: g_ringSpawnArray[i*4 + 3] = 0.
     * This is the collection/respawn timer field. */
    for (int i = 0; i < g_ringCount; i++) {
        g_ringSpawnArray[i * 4 + 3] = 0;
    }

    ParseTerrainHeader();                      /* binary 0x4757af: call 0x4d8904 */

    /* Race state counters — lines 3208-3209.
     * _DAT_00901ebc/ec0 are lap tracking state, ec4 is a fixed-point scale.
     * Both ebc and ec0 are 0 at race start (zeroed by FUN_00471ec0 earlier). */
    g_raceStateEC0 = g_raceStateEBC;
    g_raceStateEC4 = 0x10000;

    InitPlayerSpawns();

    /* Binary reads existing +0xF2 (set by character select) and passes
     * as EDX to InitPlayerSlot. Player 0's charId set by char select;
     * players 1-4 set by char select AI assignment loop. */

    /* Init player slots — binary: movsx EDX,[player+0xF2]; mov EAX,player; call 0x485120 */
    for (int i = 0; i < 5; i++) {
        InitPlayerSlot(&g_playerBase[i], (int)g_playerBase[i].charId);
    }

    /* Ghost character setup — binary 0x4758a2-0x4758d8:
     * Overrides player 1's charId and copies to player1+0x1E0 (backup charId).
     *
     * 0x4758B5 is `cmp eax, [0x8FB954]` — g_raceType > g_raceSubMode, NOT a
     * human count. Tag and Balloon run under RACE_TIMEATTACK with subMode 2
     * and 3, so 2 > subMode is false there and the override must not fire.
     * A g_numHumans test passes in single-player Tag, which handed AI player 1
     * the ghost's character left over from the previous Time Attack race. */
    if (g_ghostToggle == 1 && g_raceType == RACE_TIMEATTACK
        && g_raceType > g_raceSubMode && g_ghostDataExists != 0)
    {
        g_playerBase[1].charId = (short)g_ghostCharId;                /* 0x4758CC */
        g_playerBase[1]._unk_0x1E0 = g_playerBase[1].charId;      /* 0x4758D8 */
    }

    /* Round (EC0 - EBC) down to nearest multiple of 4 — lines 3221-3223.
     * The Watcom pattern is just signed truncate-toward-zero divide by 4, times 4. */
    g_raceState9C = ((g_raceStateEC0 - g_raceStateEBC) / 4) * 4;

    /* FUN_00471eb0 — 16 bytes — InitAIConfig + InitAllPlayersForRace.
     * Called here in InitLevel (first pass) and again from InitRaceStart
     * (second pass). Both calls are intentional in the original. */
    InitPlayersForLevel();

    /* Binary 0x47590D: zero first two ints of GP camera base */
    g_gpCamBase[0] = 0;
    g_gpCamBase[1] = 0;

    /* Intro countdown — set here, overwritten again by InitRaceStart */
    if (g_demoMode == DEMO_NONE) {
        g_introCountdown = 0xD2;
    }
    else {
        g_introCountdown = 0x50;
    }

    /* BuildChaseCamera per human player — binary 0x475933-0x4759C0 */
    if (g_netSessionActive != 0) {
        /* Multiplayer: init camera for local player only — 0x47593C */
        int idx = g_localPlayerIndex & 0xFFFF;       /* 0x0068ACDC — word in binary */
        BuildChaseCamera(
            &g_playerBase[idx],
            &g_camStateTable[idx]);
    }
    else {
        /* Single player: init camera for each human — 0x475981 */
        for (int h = 0; h < (int)g_numHumans; h++) {
            BuildChaseCamera(
                &g_playerBase[h],
                &g_camStateTable[h]);
        }
    }

    /* FUN_0047fa64 — per-track scenery anim init + object visibility */
    InitTrackAnimObjects();

    InitOtherParticles();

    /* Set per-track distance/length globals */
    g_lapDistExtra = 0;

    switch (g_trackId) {
        case TRACK_RESORT_ISLAND:
        default:
            g_lapDistFarClip = 0x2B00;
            g_lapDistTrackLen = 0x25800;
            g_lapDistRace = g_isMultiRace ? 0x33D0 : 0x3240;
            g_lapDistRaceAlt = 0x30E0;
            break;
        case TRACK_RADICAL_CITY:
            g_lapDistFarClip = 0x29F4;
            g_lapDistTrackLen = 0x25D8F;
            g_lapDistRace = g_isMultiRace ? 0x33B0 : 0x3220;
            g_lapDistRaceAlt = 0x30C0;
            break;
        case TRACK_REGAL_RUIN:
            g_lapDistFarClip = 0x282B;
            g_lapDistTrackLen = 0x3E82E;
            g_lapDistRace = g_isMultiRace ? 0x1FE9 : 0x1F0C;
            g_lapDistRaceAlt = 0x1E1C;
            break;
        case TRACK_REACTIVE_FACTORY:
            g_lapDistFarClip = 0x2846;
            g_lapDistTrackLen = 0x2EC84;
            g_lapDistRace = g_isMultiRace ? 0x2840 : 0x2705;
            g_lapDistRaceAlt = 0x25F0;
            break;
        case TRACK_RADIANT_EMERALD:
            g_lapDistFarClip = 0x282B;
            g_lapDistTrackLen = 0x50000;
            g_lapDistRace = g_isMultiRace ? 0x1AA0 : 0x1F0C;
            g_lapDistRaceAlt = 0x1920;
            break;
    }

    InitRaceState();

    /* Per-config lap distance threshold lookups — lines 3305-3372.
     * Reads from runtime time tables populated by InitDefaultTimeTables. */
    if (g_raceSubMode == SUBMODE_NORMAL && (g_raceType < RACE_TIMEATTACK || g_raceType == 4)) {
        g_lapDistanceThreshold = g_lapTimeA[g_trackId];
        g_lapDistanceThreshold2 = g_lapTimeC[g_trackId];
        for (int i = 0; i < 10; i++) {
            int srcOff = i * (0xA4 / 4);
            g_checkpointA[i] = g_cpTableA[srcOff + (0x8FBC84 - 0x8FBC84) / 4 + g_trackId];
            g_checkpointB[i] = g_cpTableA[srcOff + (0x8FBC98 - 0x8FBC84) / 4 + g_trackId];
        }
    }
    else if (g_raceSubMode == SUBMODE_BALLOON) {
        g_lapDistanceThreshold = g_lapTimeEmerald[g_trackId];
        for (int i = 0; i < 10; i++) {
            int srcOff = i * (0xA4 / 4);
            g_checkpointA[i] = g_cpTableA[srcOff + (0x8FBCFC - 0x8FBC84) / 4 + g_trackId];
        }
    }
    else if (g_raceSubMode == SUBMODE_TAG) {
        g_lapDistanceThreshold = g_lapTime5Lap[g_trackId];
        for (int i = 0; i < 10; i++) {
            int srcOff = i * (0xA4 / 4);
            g_checkpointA[i] = g_cpTableA[srcOff + (0x8FBD10 - 0x8FBC84) / 4 + g_trackId];
        }
    }
    else if (g_raceType == RACE_TIMEATTACK) {
        if (g_raceSubMode == SUBMODE_NORMAL) {
            g_lapDistanceThreshold = g_lapTimeTA0[g_trackId];
            g_lapDistanceThreshold2 = g_lapTimeTAHalf0[g_trackId];
            for (int i = 0; i < 10; i++) {
                int srcOff = i * (0xA4 / 4);
                g_checkpointA[i] = g_cpTableA[srcOff + (0x8FBCAC - 0x8FBC84) / 4 + g_trackId];
                g_checkpointB[i] = g_cpTableA[srcOff + (0x8FBCC0 - 0x8FBC84) / 4 + g_trackId];
            }
        }
        else if (g_raceSubMode == SUBMODE_REVERSE) {
            g_lapDistanceThreshold = g_lapTimeTA1[g_trackId];
            g_lapDistanceThreshold2 = g_lapTimeTAHalf1[g_trackId];
            for (int i = 0; i < 10; i++) {
                int srcOff = i * (0xA4 / 4);
                g_checkpointA[i] = g_cpTableA[srcOff + (0x8FBCD4 - 0x8FBC84) / 4 + g_trackId];
                g_checkpointB[i] = g_cpTableA[srcOff + (0x8FBCE8 - 0x8FBC84) / 4 + g_trackId];
            }
        }
    }

    int effQuality = g_qualityLevel;
#ifdef SONICR_DC
    /* Split-screen draw-distance ceiling — PORT ADDITION, not in the binary.
     *
     * Each viewport is a full geometry pass, so the stock "very far" setting
     * is unaffordable once the screen is divided. Cap the effective quality
     * by viewport count: 2 players at 3 ("far", one below the menu maximum),
     * 3 or 4 players at 1 ("near", one above the minimum).
     *
     * Split-screen modes ALWAYS use these values, whatever the menu says —
     * they are the mode's setting, not a ceiling on the player's. Single
     * viewport is untouched and keeps g_qualityLevel exactly.
     *
     * The forced value goes into a local rather than into g_qualityLevel
     * itself, because that global is persisted (save.c:382/:437) — writing it
     * would overwrite the player's saved single-player preference the first
     * time they raced split-screen. */
    if (g_numViewports > 3) {
        effQuality = SPLIT_QUALITY_4P;
    }
    else if (g_numViewports == 3) {
        effQuality = SPLIT_QUALITY_3P;
    }
    else if (g_numViewports == 2) {
        effQuality = SPLIT_QUALITY_2P;
    }
#endif
    /* Far clip interpolation — inline in original, lines 3373-3384.
     * Interpolates between two clip values based on quality level. */
    int diff = g_clipFar - g_clipNear;
    int s = diff >> 31;
    int quarter = (int)(((diff + s * -4) - (unsigned)((s << 1) < 0)) >> 2);
    int farClip = quarter * effQuality + g_clipNear;
    InitFarClipAndFog(farClip);
    RemapCharacterTpages();

    /* Clear 16 ints of render state at 0x8F6F60 — line 3375-3380 */
    for (int j = 0; j < 16; j++) {
        g_renderStateBlock[j] = 0;
    }

    g_tpageDirty = 0;                                       /* line 3381 */

}

/* =====================================================================
 * Track init helpers
 * ===================================================================== */

/**
 * InitRaceState — FUN_004751E4 — 302 bytes
 * Sets g_raceOrder[0] based on race type, clears per-player race state arrays,
 * clears g_postRaceCameraMode.
 *
 * Binary switch on g_raceType ([0x8FB950]):
 *   0 → g_raceOrder[0] = 2
 *   1 → g_raceOrder[0] = 3
 *   2 → g_raceOrder[0] = 5 (if lapConfig==2), else 4
 *   3 → g_raceOrder[0] = 1
 *   0x64..0x67 → g_raceOrder[0] = 6
 *   else → g_raceOrder[0] unchanged
 *
 * Disasm verified with capstone at 0x4751E4-0x475311.
 */
void InitRaceState(void) {
    int edx = g_raceOrder[0];             /* 0x4751EA: preserve current value as default */

    g_introTimer = 0;                     /* 0x4751FD */
    g_raceTimerA = 0;                     /* 0x475203 */
    g_raceTimerE98 = 0;                   /* 0x475209 */
    g_raceFinished = 0;                   /* 0x47520F */

    /* 0x475215-0x475227 */
    g_lapCountMinusOne = (short)(g_configLapCount - 1);
    g_raceLimitA = 6;                     /* 0x475216 */
    g_raceLimitB = 6;                     /* 0x47521C */
    g_raceMaskValue = 0xFFE;              /* 0x47522D */
    g_inputStateEC = 0;                   /* 0x475235 */

    /* 0x475252-0x47526A: set fade/race timer state */
    g_fadeLevel = (int)0xFFFFFF00;
    g_fadeState = 1;
    g_fadeSpeed = 4;
    g_raceCountdownA = 0;
    g_raceCountdownB = 0;

    /* Switch on g_raceType — 0x47524B-0x4752C8 */
    if (g_raceType == RACE_GP) {
        edx = 2;
    }
    else if (g_raceType == RACE_MULTIPLAYER) {
        edx = 3;
    }
    else if (g_raceType == RACE_TIMEATTACK) {
        edx = (g_raceSubMode == SUBMODE_TAG) ? 5 : 4;
    }
    else if (g_raceType == RACE_SPECIAL) {
        edx = 1;
    }
    else if (g_raceType >= 0x64 && g_raceType <= 0x67) {
        edx = 6;
    }

    /* Clear flyover / per-player state arrays — 0x4752C8-0x4752FB */
    for (int i = 0; i < 4; i++) {
        g_flyoverNearest[i] = -1;
        g_flyoverMode[i] = 0;
        g_flyoverParam[i] = 0;
        g_flyoverWpIdx[i] = 0;
        g_flyoverTimer[i] = 0;
        g_playerFinishFlag[i] = 0;
    }

    g_postRaceCameraMode = 0;              /* 0x4752FF */
    g_raceOrder[0] = edx;                 /* 0x475305 */
}

/* =====================================================================
 * ResetAllRaceState — FUN_00471ec0 — 1259 bytes
 *
 * Zeroes the entire race runtime state block. Called at the top of
 * InitTrackCommon before any per-track setup. Resets pause, fade,
 * timing, collectibles, per-viewport arrays, input state, ghost
 * indices, camera state, and more.
 *
 * No parameters (Watcom fastcall — no EAX input used).
 * ===================================================================== */
void ResetAllRaceState(void)
{
    /* Core race flags (0x471ee7-0x471f07) */
    g_isPaused = 0;                             /* 0x901C30 */
    g_unk_901C0C = 0;                           /* 0x901C0C */
    g_pauseLatch = 0;                           /* 0x901C34 */
    for (int i = 0; i < 8; i++) {
        g_raceStateBlock10[i] = 0;  /* 0x901C10..0x901C2C */
    }

    /* Fade / HUD / bounce / collectible state (0x471f09-0x471f81) */
    g_fadeSpeed = 0;                            /* 0x901C4C */
    g_fadeState = 0;                            /* 0x901C48 */
    g_fadeLevel = 0;                            /* 0x901C44 */
    g_unk_901C40 = 0;
    g_unk_901C3C = 0;
    g_bounceVelocity2 = 0;
    g_bouncePosition2 = 0;
    g_bounceVelocity = 0;                       /* 0x901C58 */
    g_bouncePosition = 0;                       /* 0x901C54 */
    g_unk_8F7074 = 0;
    g_ringAnimFrame = 0;                    /* 0x901C6C */
    g_itemEffectAnimPhase = 0;                  /* 0x901C68 */
    g_emeraldAnimFrame1 = 0;                    /* 0x901C64 */
    g_postRaceCameraMode = 0;                   /* 0x901C84 */
    g_finishOrderCounter = 0;                   /* 0x901C80 */
    g_unk_901C7C = 0;
    g_raceCheckpoint = 0;                       /* 0x901C78 */
    g_unk_901C74 = 0;
    g_p1CollectionCount = 0;                    /* 0x901C70 */
    g_raceFinished = 0;                         /* 0x901C88 */
    g_orbitAngle = 0;                           /* 0x901C50 */

    /* Per-player 3-entry arrays (0x471f89-0x471fa9) */
    for (int i = 0; i < 3; i++) {
        g_effectPosItemBurst[i] = 0;
        g_raceTimerB[i] = 0;
        g_ringCollectPos[i] = 0;
        g_ringRespawnPos[i] = 0;
    }

    /* Timing / race flow (0x471fab-0x471ff8) */
    g_introTimer = 0;                           /* 0x901CC8 */
    g_introCountdown = 0;                       /* 0x901CC4 */
    g_sfxCooldownTimer = 0;
    g_unk_901CCC = 0;
    g_ringSpawnReadPtr = NULL;                  /* 0x901CD0 */
    g_randomRingIdx = 0;                        /* 0x901CD4 */
    g_ghostReadIndex = 0;                       /* 0x901CDC */
    g_ghostWriteIndex = 0;                      /* 0x901CD8 */
    g_unk_901CE4 = 0;
    g_unk_901CEC = 0;
    g_unk_901CE8 = 0;

    /* Per-viewport timing arrays (3 vp × 4 entries × 5 arrays, 0x472002-0x472039) */
    for (int i = 0; i < 12; i++) {
        g_vpTimerA[i] = 0;
        g_vpTimerB[i] = 0;
        g_vpTimerC[i] = 0;
        g_vpTimerD[i] = 0;
        g_vpTimerE[i] = 0;
    }

    /* Lap config / timer arrays (0x47203b-0x47207d) */
    g_raceLimitA = 0;                           /* 0x901DF0 */
    g_unk_901DE8 = 0;
    g_configLapCount = 0;                       /* 0x901DE4 */
    g_unk_901DEC = 0;
    g_raceTimerA = 0;                           /* 0x901DF4 */
    for (int i = 0; i < 36; i++) {
        g_raceTimerBlock[i] = 0;  /* 4 arrays × 9 entries */
    }

    /* Race position / emerald / lap state (0x47207f-0x472121) */
    g_raceMaskValue = 0;
    g_unk_901E8C = 0;
    g_unk_901E90 = 0;
    g_unk_901EA0 = 0;
    g_raceState9C = 0;
    g_raceTimerE98 = 0;
    g_raceLimitB = 0;
    g_unk_901EA4 = 0;
    g_unk_901EA8 = 0;
    g_unk_901EAC = 0;
    g_unk_901EB0 = 0;
    g_unk_901EB4 = 0;
    g_unk_901EB8 = 0;
    g_raceStateEBC = 0;
    g_raceStateEC0 = 0;
    g_raceStateEC4 = 0;
    g_unk_901EC8 = 0;
    g_unk_901ECC = 0;
    for (int i = 0; i < 4; i++) {
        g_gpCamBase[i] = 0;
    }
    /* 0x472109: mov [0x901ee0], ebp — that address is g_posDataCount4, the
     * waypoint count. Safe to clear here: InitLevel runs InitTrackCommon
     * before the per-track switch that reaches the loader (track_init.c:143
     * vs :152), so the loader refills it afterwards. */
    g_posDataCount4 = 0;     g_waypointTablePtr = NULL;
    g_numWaypoints = 0;      g_camWaypointCount = 0;
    /* 0x901EF0 is g_gateWaypointPtr — the binary zeroes the POINTER here. */
    g_gateWaypointPtr = NULL; 
    g_unk_901FF4 = 0;

    /* Flyover / finish flag arrays (0x472129-0x472155) */
    for (int i = 0; i < 4; i++) {
        g_flyoverTimer[i] = 0;
        g_flyoverParam[i] = 0;
        g_flyoverMode[i] = 0;
        g_flyoverNearest[i] = 0;
        g_playerFinishFlag[i] = 0;
        g_flyoverWpIdx[i] = 0;
    }

    /* Race order + misc state (0x472157-0x47219f) */
    g_unk_902060 = 0;
    g_unk_90205C = 0;
    g_unk_902068 = 0;
    g_unk_902064 = 0;
    g_unk_90206C = 0;
    g_raceOrder[0] = 0;      /* 0x902070 */
    g_trackAnimTickRate = 0; /* 0x902058 */
    for (int i = 1; i <= 8; i++) {
        g_raceOrder[i] = 0;
    }

    /* Counters / trail state (0x472193-0x4721c5) */
    g_raceCounterA0 = 0;
    g_particleIdx = 0;
    g_raceCounter98 = 0;
    g_raceCounterA8 = 0;
    g_unk_9020BC = 0;
    g_trailCountB = 0;
    g_trailWriteB = 0;
    g_trailCountA = 0;
    g_trailWriteA = 0;

    /* Input state words (0x4721cb-0x472296) */
    g_inputStateE0 = 0;
    g_inputStateDE = 0;
    g_inputStateDC = 0;
    g_introInputRaw = 0;
    for (int i = 0; i < 4; i++) {
        g_perPlayerInput[i] = 0;
    }
    g_combinedInputState = 0;
    g_inputStateE8 = 0;
    g_inputStateE6 = 0;
    g_inputStateE4 = 0;
    g_inputStateE2 = 0;
    g_inputStateEC = 0;
    g_inputStateEA = 0;
    g_inputState100 = 0;
    g_inputStateFE = 0;
    g_inputStateFC = 0;
    g_inputState106 = 0;
    g_inputState104 = 0;
    g_inputState128 = 0;
    g_inputState126 = 0;
    g_inputState124 = 0;
    g_inputState12C = 0;
    g_inputState1F2 = 0;
    g_inputState1F0 = 0;

    /* Camera / position state dwords (0x4722a9-0x472365) */
    memset(&g_gpSmoothedCam, 0, sizeof(CamStateEntry));
    g_unk_902118 = 0;
    g_unk_90211C = 0;
    g_unk_902120 = 0;
    g_savedCamPitch = 0;
    g_savedCamYaw = 0;
    g_savedCamX = 0;
    g_savedCamY = 0;
    g_savedCamZ = 0;
    g_unk_9021F4 = 0;
    g_unk_9021F8 = 0;
    g_unk_9021FC = 0;
    g_unk_902200 = 0;
    g_lapDistTrackLen = 0;
    g_lapDistFarClip = 0;
    g_lapDistRace = 0;
    g_lapDistRaceAlt = 0;
    g_unk_902228 = 0;
    g_unk_902234 = 0;
    g_lapDistExtra = 0;
    g_unk_90247C = 0;
    g_unk_902480 = 0;
    g_unk_902484 = 0;
    g_unk_902488 = 0;
    g_unk_90248C = 0;
    g_unk_902490 = 0;
    g_camWaypointTable = NULL;
    g_introSplineBase = NULL;          /* 0x472356: mov dword ptr [0x902498], ebp */
    g_trackBoundaryWaypoints = NULL;   /* 0x472351: mov dword ptr [0x90249c], esi */
    g_unk_9024A4 = 0;
    g_unk_9024A8 = 0;

    /* Misc word state (0x47236b-0x47238d) */
    g_inputState12E = 0;
    g_inputState102 = 0;
    g_inputState12A = 0;

    /* Unknown array at 0x8FB8B0 (5 dwords, 0x472394-0x4723a2) */
    for (int i = 0; i < 5; i++) {
        g_unk_8FB8B0[i] = 0;
    }
}

/**
 * InitTrackCommon — FUN_004723AC — 428 bytes
 *
 * Resets all race-session state: pause flags, trail buffers, ring/item
 * state arrays, particle effect buffers, and the shared menu/race
 * state block at 0x92528C.  Called once at the start of each race via
 * InitLevel → InitTrackCommon.
 *
 * The first thing the binary does is call InitPlayersForLevel (0x471eb0),
 * but our call graph already handles that separately in InitLevel.
 */
void InitTrackCommon(void)
{
    ResetAllRaceState();                                    /* 0x4723b1: call 0x471ec0 */

    /* Emerald sine phases (0x4723d1-0x4723dd) */
    g_emeraldSineOffX = 0x7b;                               /* 0x712d68 = 123 */
    g_emeraldSineOffY = 0x2bc;                              /* 0x712d6c = 700 */
    g_emeraldSineOffZ = 0x7db;                              /* 0x712d70 = 2011 */

    /* Trail source pointers (0x4723e3-0x4723f5) */
    g_trailSrcA = &g_playerBase[0];
    g_trailSrcB = &g_playerBase[1];
    /* 0x8fd4f0: binary writes player[1]+0x71C here but nothing reads it — dead state */
    g_unk_8FD4F0 = (int)(intptr_t)&g_playerBase[1];

    /* Trail buffer flags: zero 4th int of each 0x10-stride entry (0x472402-0x472438) */
    for (int i = 0; i < 5; i++) {
        ((int *)((char *)g_trailBufA + 0x0C + i * 0x10))[0] = 0;
    }
    for (int i = 0; i < 5; i++) {
        ((int *)((char *)g_trailBufB + 0x0C + i * 0x10))[0] = 0;
    }

    /* Pause / race state scalars (0x47243e-0x472468) */
    g_isPaused = 0;                                         /* 0x901c30 */
    g_unk_901C0C = 0;                                       /* 0x901c0c */
    g_raceResult = RACE_RESULT_NONE;                        /* 0x901c10 */
    g_exitRaceFlag = 0;                                     /* 0x901c18 */
    g_unk_901C1C = 0x400;                                   /* set to 1024, not zero */
    g_unk_901C20 = 0;
    g_unk_901C24 = 0;
    g_pauseLatch = 0;                                       /* 0x901c34 */

    /* Race order counters: g_raceOrder[1..8] zeroed (0x47246e-0x47247c) */
    for (int i = 1; i <= 8; i++) {
        g_raceOrder[i] = 0;
    }

    /* Shared state block at 0x92528C — 0x472480-0x472490 walks eax 4..0x200
     * storing zero at [eax + 0x925288], covering 0x92528C..0x925488. Every
     * tenant of that range (menu cursor/scroll, track-object animation, the
     * Factory door and emerald timers, g_prevNumViewports, g_screenResult)
     * aliases into this array, so one loop clears them all as the binary does.
     *
     * The bound is 128, NOT the array size. The array runs to 151 because the
     * scratch region continues to 0x9254E4, but the binary leaves slots [128]
     * and above alone — the sign-physics blocks and the results-screen cursor
     * live up there and initialise themselves. Widening this loop would zero
     * state the original preserves. */
    for (int i = 0; i < 128; i++) {
        g_stateBlock92528C[i] = 0;
    }

    /* Individual scalars (0x47249c-0x004724f0) */
    g_randomRingIdx = 0;
    g_raceCounter98 = 0;
    g_raceCounterA0 = 0;
    g_particleIdx = 0;
    g_raceCounterA8 = 0;
    g_trackEventTimer = 0;
    g_bouncePosition = 0;
    g_bounceVelocity = 0;
    g_bouncePosition2 = 0;
    g_bounceVelocity2 = 0;

    g_ringCollectPos[0] = 0;
    g_raceTimerB[0] = 0;
    g_ringRespawnPos[0] = 0;
    g_effectPosItemBurst[0] = 0;

    /* Player[0] SFX substate = -1 (0x4724f8) */
    g_playerBase[0].sfxTrigger = (short)-1;                /* 0x8fd5de */

    /* Ring spawn read pointer (0x4724ff) */
    g_ringSpawnReadPtr = g_randomRingBuffer;

    ClearRaceStateArrays();                                /* 0x472505-0x472550 */

    g_ringCount = 0;
    g_totalObjects = 0;

    ResetPauseMenuState();
}

/**
 * InitFarClipAndFog — FUN_004704f0 — 116 bytes
 * Sets far clip depth and builds a 16-entry fog distance/reciprocal table.
 * in_EAX = far clip depth value (per-track, world-space units).
 */
void InitFarClipAndFog(int farClip)
{
    g_farClipTimes8 = farClip << 3;
    g_farClipFloat = (float)g_farClipTimes8;
    float fogScale = (float)g_fogDistConst;

    g_farClipDepth = farClip;

    int dist = farClip + 1;
    int step = farClip / 32;

    for (int i = 0; i < 16; i++) {
        s_fogDistTable[i] = dist;
        s_fogRecipTable[i] = 1.0f / ((float)dist * fogScale);
        dist -= step;
    }

    /* Set the 3 fog distance globals used by DrawCharacterSpritesSoft */
    g_fogDist0 = s_fogDistTable[2];   /* 0x6D7624 = table base 0x6D761C + 2*4 */
    g_fogDist1 = s_fogDistTable[4];   /* 0x6D762C */
    g_fogDist2 = s_fogDistTable[6];   /* 0x6D7634 */
}

/**
 * InitTrackTextures — FUN_004754EC — 191 bytes
 * " Translation is accurate. VALIDATED. "
 * Sets g_parallaxWidth and g_parallaxExtraX (height) based on render mode.
 * D3D: 0x680 × 0x80 (1664 × 128).
 * Software: scaled from viewport dimensions, capped at 0xD00 × 0x100.
 * Also sets g_parallaxWidthDouble = g_parallaxWidth * 2.
 */
void InitTrackTextures(void)
{
/*    if (g_renderMode == RENDER_D3D) {
*/      g_parallaxExtraX = 0x80;
        g_parallaxWidth  = 0x680;
/*    }
    else  {
        int width = g_viewportWidth;
        if (g_numHumans == 2 && g_viewportIndex == 1) {
            width = g_screenWidth;
        }
        int height = g_viewportHeight;
        if (g_numHumans == 2 && g_viewportIndex == 0) {
            height = g_screenHeight;
        }
        g_parallaxWidth  = (width * 0xD00) / 0x280;
        g_parallaxExtraX = (height << 8) / 0x1E0;
        if (g_parallaxWidth > 0xD00) {
            g_parallaxWidth = 0xD00;
        }
        if (g_parallaxExtraX > 0x100) {
            g_parallaxExtraX = 0x100;
        }
    }*/
    g_parallaxWidthDouble = g_parallaxWidth * 2;
}

/**
 * InitSceneryTpageA — FUN_00470564 — 27 bytes
 * Fills 34 entries (stride 0x30) at 0x00509668 with g_tpageCharacters value.
 * Sets the tpage byte for scenery polygon group A.
 */
void InitSceneryTpageA(void)
{
    unsigned char val = (unsigned char)g_tpageCharacters;  /* DAT_008f6c30 */
    int off = 0;
    do {
        g_sceneryTpageA[off] = val;
        off += 0x30;
    } while (off != 0x660);
}

/**
 * InitSceneryTpageB — FUN_00470580 — 27 bytes
 * Fills 14 entries (stride 0x30) at 0x00509CC8 with g_tpagePlayfield1 value.
 * Sets the tpage byte for scenery polygon group B.
 */
void InitSceneryTpageB(void)
{
    unsigned char val = (unsigned char)g_tpagePlayfield1;  /* DAT_008f6c34 */
    int off = 0;
    do {
        g_sceneryTpageB[off] = val;
        off += 0x30;
    } while (off != 0x2a0);
}

/**
 * RemapCharacterTpages — FUN_00470460 — 144 bytes
 * Walks all character model polygons and sets byte 0x28 (texture page index)
 * based on g_polyTypeTable. Flips tpage assignment for Amy, TDoll, and Super
 * Sonic character polygon ranges.
 */
void RemapCharacterTpages(void)
{
    /* Binary uses one contiguous polygon array at 0x712D4C for both track and
     * character faces. Our port splits them: g_polygonArrayBase (track) and
     * g_charFaceBase (characters). Character model faces are in g_charFaceBase,
     * so we must update tpage bytes THERE — not in g_polygonArrayBase. */
    char *polyBase = (char *)g_charFaceBase;
    unsigned char *ptypePtr = g_polyTypeTable;
    int i = 0;

    if (g_modelPolygonCount <= 0) {
        return;
    }

    /* Read character polygon range boundaries from model metadata.
     * Each model has a 0x50-byte meta entry; polygon start is at byte offset 8. */
    int amyStart   = g_modelMeta[3].polyStart;  /* _DAT_0071319c */
    int eggStart   = g_modelMeta[4].polyStart;  /* _DAT_007131ec */
    int tdollStart = g_modelMeta[6].polyStart;  /* _DAT_0071328c */
    int mknuxStart = g_modelMeta[7].polyStart;  /* _DAT_007132dc */
    int superStart = g_modelMeta[9].polyStart;  /* _DAT_0071337c */

    char tpageA = (char)g_tpageCharacters;   /* DAT_008f6c30 — for polyType == 0 */
    char tpageB = (char)g_tpagePlayfield1;   /* DAT_008f6c34 — for polyType != 0 */

    int polyOff = 0;  /* byte offset into polygon array (stride 0x30) */
    do {
        char assignedTpage = tpageB;
        if (*ptypePtr == 0) {
            assignedTpage = tpageA;
        }
        *(polyBase + polyOff + 0x28) = assignedTpage;

        /* Flip tpage for Amy (model 3→4), TDoll (model 6→7), Super (model 9+) */
        if (((amyStart < i && i < eggStart) ||
             (tdollStart < i && i < mknuxStart)) ||
            superStart < i)
        {
            if (*(polyBase + polyOff + 0x28) == tpageA) {
                *(polyBase + polyOff + 0x28) = tpageB;
            }
            else {
                *(polyBase + polyOff + 0x28) = tpageA;
            }
        }

        polyOff += 0x30;
        i++;
        ptypePtr++;
    } while (i < g_modelPolygonCount);
}

/**
 * RemapBalloonTpages — 0x00470564 — 26 bytes
 * Points all 34 balloon faces at the character texture page. The model ships
 * with tpage 4 baked in; each track puts PLAYER00 in a different slot, so the
 * binary rewrites the byte right after the slot assignments.
 */
void RemapBalloonTpages(void)
{
    unsigned char *face = (unsigned char *)s_balloonModelPoly;      /* 0x509640 */
    for (int i = 0; i < 34; i++) {
        face[i * 0x30 + 0x28] = (unsigned char)g_tpageCharacters;   /* 0x470570 */
    }
}

/**
 * InitOtherParticles — 0x0046EBB8 — 81 bytes
 * Resets all 64 weather particles and dispatches to per-track init.
 */
void InitOtherParticles(void)
{
    g_nextParticleSlot = 0;

    for (int i = 0; i < 64; i++) {
        g_particleArray[i].active = 0;
        g_particleArray[i].lifetime = 0;
    }

    if (g_trackId == TRACK_RESORT_ISLAND) {
        InitWeatherProps_Island();
    }
    if (g_trackId == TRACK_REACTIVE_FACTORY) {
        InitWeatherProps_Factory();
    }
    if (g_trackId == TRACK_RADIANT_EMERALD) {
        InitWeatherProps_Emerald();
    }
}
