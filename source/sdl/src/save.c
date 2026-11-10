/**
 * save.c — Save data block and related functions
 *
 * Houses g_saveBlock[554] — the contiguous 2216-byte save data blob
 * (binary 0x8FBA4C-0x8FC2F4). All named field accessors are #defined
 * in sonicr_globals.h. Also contains InitDefaultTimeTables, demo replay,
 * and save/load helpers.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "endian_util.h"

extern int g_optCfg_488;

/* ─── Save Data Block (0x8FBA4C-0x8FC2F4) ─────────────────────────── */
/* Contiguous 2216-byte (554 int) block. The binary saves/loads this
 * as a single blob via fwrite/fread. All named field accessors are
 * #defined in sonicr_globals.h into this array. */
int g_saveBlock[554];

/* =====================================================================
 * InitDefaultTimeTables — FUN_00470DC8 — 539 bytes
 * Initializes all race time/checkpoint tables with defaults computed
 * from g_raceSpeedMult. Called from LoadGameState at startup and
 * from LoadSaveScreen when changing options.
 * ===================================================================== */
void InitDefaultTimeTables(void)
{
    /* Character race availability defaults */
    g_charRaceState[0] = 1;
    g_charRaceState[1] = 1;
    g_charRaceState[2] = 1;
    g_charRaceState[3] = 1;
    g_allCharsRaceUnlock = 0;

    /* Character unlock state (g_charUnlockTable == &g_saveBlock[6]).
     *
     * ALIAS WARNING — this table is initialised here under FOUR different
     * names, only the first of which says "unlock". The complete picture for
     * entries [0..9], in the order the writes happen below:
     *
     *   [0..3] = 2   this block               Sonic, Tails, Knuckles, Amy
     *   [4]    = 1   this block               Eggman  (LOCKED — see below)
     *   [6..9] = 0   g_collectUnlock[0..3]    Tails Doll, Metal Knuckles,
     *                                         Egg Robo, Super Sonic
     *   [9]    = 0   g_allCharsUnlocked       Super Sonic again (redundant)
     *   [5]    = 0   direct, further down     Metal Sonic
     *
     * So only [0..3] are set to 2. [4] gets 1, and 1 != 2 means EGGMAN STARTS
     * LOCKED for human play. Deliberate, and it matches the binary
     * (0x470dd1 `mov ebx,1`, 0x470e11 `mov [0x8fba74], ebx`).
     *
     * How he unlocks: win Radiant Emerald in 1st place. race_setup.c writes
     * `g_gpTrackStatus[trkIdx] = 2` with trkIdx == TRACK_RADIANT_EMERALD == 5
     * (binary 0x4c4852, `mov [edx + 0x8fba4c], 2`), and the check twelve lines
     * later — `if (g_gpTrackStatus[5] == 2) charUnlockTable[4] = 2` (0x4c48ac) —
     * writes 2 into this very slot. The char-select cursor's skip-locked loop
     * (`g_charUnlockTable[n] != 2`, binary 0x48d302) then lets him through.
     *
     * Note the asymmetry, which is real and intended: Eggman is ALWAYS
     * eligible as an AI opponent regardless of this flag, because the AI
     * filter at 0x48D09F is `candidate > 4` and charId 4 never reaches the
     * unlock check. On a fresh save he is in fact guaranteed to appear — the
     * player takes one of Sonic/Tails/Knuckles/Amy, leaving exactly three, so
     * Eggman fills the fourth slot. */
    g_charUnlockTable[0] = 2;
    g_charUnlockTable[1] = 2;
    g_charUnlockTable[2] = 2;
    g_charUnlockTable[3] = 2;
    g_charUnlockTable[CHAR_EGGMAN] = 1;   /* 0x8FBA74 — locked, not 2 */

    /* Zero collectible/track unlock state.
     *
     * ALIAS WARNING — g_collectUnlock is &g_saveBlock[12], so [0..3] ARE
     * g_charUnlockTable[6..9]: Tails Doll, Metal Knuckles, Egg Robo and Super
     * Sonic. This loop is what locks those four on a fresh save. Correct as
     * written, but anything that later "clears collectible state" through this
     * name would silently re-lock four characters.
     *
     * [4] is g_charUnlockState[0] (g_saveBlock[16]), which the
     * g_trackCollectState loop just below zeroes a second time. */
    for (int i = 0; i < 5; i++) {
        g_collectUnlock[i] = 0;
    }
    g_allCharsUnlocked = 0;
    for (int i = 0; i < 5; i++) {
        g_trackCollectState[i] = 0;
    }
    g_lapTimeA[0] = 0;  /* track 0 unused */

    /* binary 0x470e91: mov [0x8fba78], edi */
    g_charUnlockTable[5] = 0;

    /* Compute default times from speed multiplier */
    int lapTime = g_raceSpeedMult * 0x168;   /* 360 */
    int halfLap = g_raceSpeedMult * 0x78;    /* 120 */
    int raceTime = g_raceSpeedMult * 600;

    /* Fill per-track simple time tables (5 entries each) */
    for (int i = 0; i < 5; i++) {
        g_lapTimeB[i] = lapTime;        /* 0x8FBAA8 + i*4 */
        g_lapTimeC[i] = halfLap;        /* 0x8FBABC + i*4 */
        g_lapTimeD[i] = lapTime;        /* 0x8FBAD0 + i*4 */
        g_lapTimeTA1[i] = lapTime;      /* 0x8FBAE4 + i*4 */
        g_lapTimeTAHalf0[i] = halfLap;  /* 0x8FBAF8 + i*4 */
        g_lapTimeTAHalf1[i] = halfLap;  /* 0x8FBB0C + i*4 */
        g_lapTimeEmerald[i] = raceTime; /* 0x8FBB20 + i*4 */
        g_lapTime5Lap[i] = raceTime;    /* 0x8FBB34 + i*4 */
    }

    /* Zero per-player championship standings */
    for (int i = 0; i < 4; i++) {
        g_gpPlayerStandings[i] = 0;
    }

    /* Zero per-checkpoint best times (10 rows × 5 tracks) */
    for (int i = 0; i < 10 * 5; i++) {
        g_checkpointBestTimes[i] = 0;
    }

    /* Zero additional time table */
    for (int i = 0; i < 10; i++) {
        g_raceTimeTable[i] = 0;
    }

    /* Fill per-checkpoint / per-character best-time tables with defaults.
     * Binary 0x00470f27-0x00470fd1: nested loop — outer row 0..9 (characters),
     * inner slot eax = 4, 8, 12, 16, 20 → track slots 1..5 (Island, City,
     * Ruin, Factory, Emerald). Each row writes 8 fields × 5 tracks.
     *
     * Addresses used by DrawResultTotalTime / DrawResultBestLap etc. at
     * 0x4c4ff4+ read these as `[0x8fbcac + 164*charId + 4*g_trackId]`
     * (case 0) and friends. Previously this loop only wrote the eax=4 slot
     * (trackId=1 = Island) per field, leaving City/Ruin/Factory/Emerald
     * at BSS zero — that was the root cause of the results-screen
     * LAP/COURSE RECORD `00:00:00` display and missing rainbow highlight
     * on non-Island tracks. */
    char *base = (char *)g_cpTableA;    /* 0x8FBC84 */
    for (int row = 0; row < 10; row++) {
        int rowOff = row * 0xA4;
        /* Inner loop: 5 track slots per row — eax = 4, 8, 12, 16, 20 */
        for (int slotOff = 4; slotOff <= 0x14; slotOff += 4) {
            int o = rowOff + slotOff;
            *(int *)(base + o + (0x8FBC84 - 0x8FBC84)) = lapTime;  /* 0x8FBC88..98 */
            *(int *)(base + o + (0x8FBC98 - 0x8FBC84)) = halfLap;  /* 0x8FBC9C..AC */
            *(int *)(base + o + (0x8FBCAC - 0x8FBC84)) = lapTime;  /* 0x8FBCB0..C0 */
            *(int *)(base + o + (0x8FBCC0 - 0x8FBC84)) = halfLap;  /* 0x8FBCC4..D4 */
            *(int *)(base + o + (0x8FBCD4 - 0x8FBC84)) = lapTime;  /* 0x8FBCD8..E8 */
            *(int *)(base + o + (0x8FBCE8 - 0x8FBC84)) = halfLap;  /* 0x8FBCEC..FC */
            *(int *)(base + o + (0x8FBCFC - 0x8FBC84)) = raceTime; /* 0x8FBD00..10 */
            *(int *)(base + o + (0x8FBD10 - 0x8FBC84)) = raceTime; /* 0x8FBD14..24 */
        }
        /* Per-row zero — binary 0x470f7e: `mov [eax + 0x8fbd28], esi`
         * at top of outer with saved eax = row*164, writes one int at
         * (row+1)*164 relative to cpTableA base. */
        *(int *)(base + rowOff + (0x8FBD28 - 0x8FBC84)) = 0;
    }

    g_saveStateFlag = 0;
}

/* Demo replay filename table — extracted from ROM at 0x507604 */
static const char *s_demoFiles[] = {
    PATH_DEMOS "ISLAND.DEM",
    PATH_DEMOS "CITY.DEM",
    PATH_DEMOS "RUIN.DEM",
    PATH_DEMOS "FACTORY.DEM",
    PATH_DEMOS "ISLAND2.DEM",
    PATH_DEMOS "CITY2.DEM",
    PATH_DEMOS "RUIN2.DEM",
    PATH_DEMOS "FACTORY2.DEM",
    PATH_DEMOS "EMERALD2.DEM",
};

/**
 * LoadReplayLog — FUN_004DBED8 — 695 bytes
 * Loads a demo/replay file: header fields (numViewports, trackId, raceType,
 * character IDs, ghostMaxFrames) then bulk ghost frame data per viewport.
 * Returns 1 on success, 0 on failure.
 *
 * Binary: EAX = filename (passed by caller).
 * Validated against capstone disasm 2026-04-18.
 */
int LoadReplayLog(const char *filename)
{
    g_gpDifficultyLevel = g_difficultyConfig;   /* 0x4DBEE2 */

    FILE *fp = fOpen(filename, "rb");       /* EAX=filename, EDX="rb" */
    g_fileHandle = fp;                      /* 0x4DBEF8 */
    if (fp == NULL) {
        return 0;               /* 0x4DBEFF: je epilogue */
    }

    unsigned short val;
    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_numViewports = (int)val; /* → 0x6E9910 */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_numPlayers = (int)val; /* → 0x6E990C */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_numHumans = (int)val; /* → 0x6E9908 */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_trackId = (int)val; /* → 0x8FB8EC */

    /* Demo headers store trackId in the ORIGINAL convention (Factory=3, Ruin=4);
     * our runtime uses Ruin=3, Factory=4. Convert the swapped 3/4 pair — this is
     * data-side handling of a binary-convention field (same family as the
     * 0x50xxxx ROM tables), NOT a g_trackId dispatch swap. Without it, RUIN.DEM
     * (header trk=4=Ruin) loads Factory geometry and FACTORY.DEM (trk=3) loads Ruin. */
    if (g_trackId == 3) {
        g_trackId = 4;
    }
    else if (g_trackId == 4) {
        g_trackId = 3;
    }

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_raceType = (int)val; /* → 0x8FB950 */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_raceSubMode = (int)val; /* → 0x8FB954 */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_difficultyConfig = (int)val; /* → 0x8FD444 */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_weatherType = (int)val; /* → 0x94BCF4 */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_timeOfDay = (int)val; /* → 0x94BCF8 */

    /* Per-player character IDs — binary writes both to player struct charId
     * (word) and to g_replayCharIds[] (sign-extended dword). 0x4DC054-0x4DC128 */
    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_playerBase[0].charId = (short)val;  /* → 0x8FD5E6 */
    g_replayCharIds[0] = (int)(short)val; /* → 0x8FB980 */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_playerBase[1].charId = (short)val;  /* → 0x8FDD02 */
    g_replayCharIds[1] = (int)(short)val; /* → 0x8FB984 */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_playerBase[2].charId = (short)val;  /* → 0x8FE41E */
    g_replayCharIds[2] = (int)(short)val; /* → 0x8FB988 */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_playerBase[3].charId = (short)val;  /* → 0x8FEB3A */
    g_replayCharIds[3] = (int)(short)val; /* → 0x8FB98C */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_playerBase[4].charId = (short)val;  /* → 0x8FF256 */
    g_replayCharIds[4] = (int)(short)val; /* → 0x8FB990 */

    fRead(&val, 1, 2, fp);
    bswap16_inplace(&val);
    g_ghostMaxFrames = (int)val; /* → 0x901CE0 */

    /* Bulk ghost frame data — one block of ghostMaxFrames × 2 bytes per viewport.
     * Binary loop at 0x4DC14C: fread(g_ghostReplayBuffer + vp*ghostMaxFrames*2,
     *                                ghostMaxFrames, 2, fp) */
    for (int i = 0; i < g_numViewports; i++) {
        unsigned short *dest = g_taGhostSource + i * g_ghostMaxFrames;
        fRead(dest, g_ghostMaxFrames, 2, fp);               /* 0x4DC16A */
        bswap16_arr(dest, g_ghostMaxFrames);
    }

    fClose(fp);                                             /* 0x4DC17C */
    return 1;                                               /* 0x4DC181 */
}

static int g_demoCycleIndex;

/**
 * AutoSelectDemo — 0x004DC190 — 77 bytes
 * Cycles through 9 demo replay files to find one that exists.
 * Used by attract mode (title screen idle → auto-play demo).
 * Returns 1 if a valid demo was loaded, 0 if none found.
 */
int AutoSelectDemo(void)
{
    int attempts = 0;
    do {
        g_demoCycleIndex = (g_demoCycleIndex + 1) % 9;
        const char *filename = s_demoFiles[g_demoCycleIndex];
        int valid = LoadReplayLog(filename);
        if (valid != 0) {
            return 1;
        }
        attempts++;
    } while (attempts < 9);
    return 0;
}

/* Binary LoadGameState @0x4cda63 inits g_demoCycleIndex to -1, so the first attract
 * demo is island.dem (index (-1+1)%9 = 0), then city.dem, etc. Without this we start
 * at 0 and play city.dem FIRST — giving City the wrong (first-demo) inherited state. */
void ResetDemoCycle(void)
{
    g_demoCycleIndex = -1;
}

/* =====================================================================
 * Save / Load implementations
 * ===================================================================== */

#include "sonicr_paths.h"

/* =====================================================================
 * SavePadTypesImpl — 0x00477D54 — 63 bytes
 * Saves joystick/gamepad config to JOYSTICK.INF.
 * Binary: fopen("joystick.inf","wb"), fwrite(0x67541a, 0x11a, 4, fp), fclose
 * Writes 4 × 282 = 1128 bytes of joystick slot data. SDL builds append
 * JOY_EXT_TAG and g_joystickSlotCfgExt (see gamepad_buttons.h).
 * Called from Shutdown (0x4CA362).
 * ===================================================================== */
extern char g_joystickSlots[4][282];        /* 0x0067541A */

void SavePadTypesImpl(void)
{
    FILE *fp = fOpen("JOYSTICK.INF", "wb");
    if (fp != NULL) {
        /* Each 0x11a slot: name bytes [0..0x103] + 11 config shorts [0x104..0x119] */
        for (int s = 0; s < 4; s++) {
            bswap16_arr(g_joystickSlots[s] + 0x104, 11);
        }
        fWrite(g_joystickSlots, 0x11a, 4, fp);
        for (int s = 0; s < 4; s++) {
            bswap16_arr(g_joystickSlots[s] + 0x104, 11);
        }
#ifndef SONICR_DC
        /* Port: each pad's bindings past its slot's ten. After the four slots,
         * so the original's 4 x 0x11A read is unaffected; read back by
         * LoadJoystickConfig. The Dreamcast's VMU save stays the four slots. */
        short ext[4][JOY_SLOT_EXT_WORDS];
        memcpy(ext, g_joystickSlotCfgExt, sizeof ext);
        bswap16_arr(ext, 4 * JOY_SLOT_EXT_WORDS);
        fWrite(JOY_EXT_TAG, 1, 4, fp);
        fWrite(ext, sizeof(short), 4 * JOY_SLOT_EXT_WORDS, fp);
#endif
        fClose(fp);
    }
}

/* =====================================================================
 * LoadGameSettings — 0x00424b70 — 68 bytes
 * Load sonicr.inf — reads 160 bytes into game options config block.
 * Returns 1 always.
 * ===================================================================== */
int LoadGameSettings(void)
{
    FILE *fp = fOpen("SONICR.INF", "rb");
    g_fileHandle = fp;
    if (fp != NULL) {
        int buf[40];
        fRead(buf, 0xa0, 1, fp);
        fClose(fp);
        bswap32_arr(buf, 40);

        g_difficultyConfig = buf[0];
        g_ghostToggle = buf[1];
        g_weatherConfig = buf[2];
        g_catchUpToggle = buf[3];
        g_guideToggle = buf[4];
        g_minimapConfig = buf[5];
        g_splitScreenMode = buf[6];
        g_optResLow = buf[7];
        g_optResHigh = buf[8];
        g_optCfg_468 = buf[9];
        g_optCfg_46c = buf[10];
        g_optCfg_470 = buf[11];
        g_renderQuality = buf[12];
        g_softDoubleBuf = buf[13];
        g_optCfg_47c = buf[14];
        g_qualityLevel = buf[15];
        g_resolutionLevel = buf[16];
        g_optCfg_488 = buf[17];
        g_optCfg_48c = buf[18];
        g_doubleWidthFlag = buf[19];
        g_stereoEnabled = buf[20];
        g_vocalsEnabled = buf[21];
        g_optSfxVolume = buf[22];
        /* Slot 23 carries the 0-8 Music Volume slider. The 1997 layout used it
         * as a 0/1 toggle, so a file from an older build reads back as off (0)
         * or the quietest step (1). */
        g_optMusicVolume = buf[23];
        if (g_optMusicVolume < 0) {
            g_optMusicVolume = 0;
        }
        if (g_optMusicVolume > 8) {
            g_optMusicVolume = 8;
        }
        g_musicEnabled = (g_optMusicVolume != 0);
        Music_SetVolume(g_optMusicVolume);
        for (int i = 0; i < 8; i++) {
            g_optCfgRomData[i] = buf[24 + i];
        }
    }
    return 1;
}

/* =====================================================================
 * SaveGameSettings — 0x00424BB4 — 112 bytes
 * Write counterpart to LoadGameSettings.
 * Packs current resolution, writes 160 bytes to sonicr.inf.
 * ===================================================================== */
void SaveGameSettings(void)
{
    int packed = (g_screenWidth << 16) + g_screenHeight;
    g_optResHigh = packed;

    /* 0xa0 bytes go to disk but only buf[0..31] are assigned below, so the
     * tail has to be cleared or eight words of stack leak into SONICR.INF. */
    int buf[40] = { 0 };
    buf[0] = g_difficultyConfig;
    buf[1] = g_ghostToggle;
    buf[2] = g_weatherConfig;
    buf[3] = g_catchUpToggle;
    buf[4] = g_guideToggle;
    buf[5] = g_minimapConfig;
    buf[6] = g_splitScreenMode;
    buf[7] = g_optResLow;
    buf[8] = g_optResHigh;
    buf[9] = g_optCfg_468;
    buf[10] = g_optCfg_46c;
    buf[11] = g_optCfg_470;
    buf[12] = g_renderQuality;
    buf[13] = g_softDoubleBuf;
    buf[14] = g_optCfg_47c;
    buf[15] = g_qualityLevel;
    buf[16] = g_resolutionLevel;
    buf[17] = g_optCfg_488;
    buf[18] = g_optCfg_48c;
    buf[19] = g_doubleWidthFlag;
    buf[20] = g_stereoEnabled;
    buf[21] = g_vocalsEnabled;
    buf[22] = g_optSfxVolume;
    buf[23] = g_optMusicVolume;     /* 0-8 slider; was a 0/1 toggle pre-slider */
    for (int i = 0; i < 8; i++) {
        buf[24 + i] = g_optCfgRomData[i];
    }

    bswap32_arr(buf, 40);
    FILE *fp = fOpen("SONICR.INF", "wb");
    if (fp != NULL) {
        fWrite(buf, 0xa0, 1, fp);
        fClose(fp);
    }
}
