/**
 * init.c — Initialization functions
 *
 * The simplest game functions — good starting point for compilation.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "gamepad_buttons.h"    /* GCBTN_* — button indices for the pad defaults */
#include <math.h>
#ifdef SONICR_SDL
#include "platform.h"
#endif

/* Externs / forward decls used by InitPadTypes */
extern void SyncJoystickSlots(void);    /* 0x477D94 */
extern void LoadJoystickConfig(void);   /* 0x477D14 */
extern void ResetDemoCycle(void);        /* 0x4CDA63 */
void InitJoystickConfig(void);          /* 0x477C24 */
extern void InitSoundOffsetTables(void);  /* 0x4D1040 */
extern void InitDefaultTimeTables(void);  /* 0x470DC8 */
extern void BuildGridUVTable(void);   /* 0x4767F8 */
extern void InitUVLUT(void);          /* 0x44EA40 */
extern void InitAnimData(void);
extern void LoadCharacterGouraudTables(void);  /* 0x476750 */

extern unsigned short g_randomRingBuffer[];  /* 0x92498C */
extern SrcVertex g_vertexArrayStorage[];
extern uint32_t g_ringSpawnStorage[], g_splineWaypointStorage[];
extern char g_polygonStorage[];
extern char g_joystickSlots[][282];

/* Menu / character-select state seeded by LoadGameState */
extern int g_charSelectIndex[];         /* 0x0068AF84 */
extern int g_charSelection[];           /* 0x0068AFEC */
extern int g_lastMenuSelection;         /* 0x0068AF78 */
extern int g_lastTimeAttackSelection;   /* 0x0068AF7C */
extern int g_lastMultiSelection;        /* 0x0068AF80 */
extern int g_lastCourseSelection;       /* 0x0068AF94 */
extern int g_lsPackedCursor;            /* 0x0068AFA0 */
extern int g_rankPagePersist;           /* 0x0068AFA4 */
extern int g_rankCursorPersist;         /* 0x0068AFA8 */
extern int g_netSavedButtonByte;        /* 0x0068AFAC */
extern int g_netSavedCharId;            /* 0x0068AFE0 */
extern int g_netSavedTrackIdx;          /* 0x0068AFE4 */
extern int g_netSavedModeIdx;           /* 0x0068AFE8 */

/* Display configuration externs */
extern int g_dispClipRight;     /* 0x006E9878 */
extern int g_dispClipBottom;    /* 0x006E987C */
extern int g_dispProjScaleX;        /* 0x006E9888 */
extern int g_dispProjScaleY;        /* 0x006E988C */

extern char g_joystickNameBuf[];        /* 0x00675300 */
extern char g_joystickSlots[][282];     /* 0x0067541A */
extern int g_joystickSlotActive[];      /* 0x00675884 */

/* g_randTable1 is same memory as g_randomRingBuffer (0x0092498C) — char view */
extern unsigned short g_randomRingBuffer[];
#define g_randTable1 ((char *)g_randomRingBuffer)
#define g_randTable2 ((char *)g_randomRingBuffer + 0x800)

static const char s_defaultDeviceName[] = "Microsoft Sidewinder game pad";

/* Default keyboard scan codes for 2 players (DIK_ values).
 * Verified against ROM table at 0x4FF0A4 in SONICR.EXE. */
static const int g_defaultKeyMap[20] = {
    0x1C,  /* P1 Start    — DIK_RETURN */
    0xCB,  /* P1 Left     — DIK_LEFT */
    0xCD,  /* P1 Right    — DIK_RIGHT */
    0xC8,  /* P1 Up       — DIK_UP */
    0xD0,  /* P1 Down     — DIK_DOWN */
    0x39,  /* P1 Jump     — DIK_SPACE */
    0x1E,  /* P1 Accel    — DIK_A */
    0x2C,  /* P1 DriftL   — DIK_Z */
    0x2D,  /* P1 DriftR   — DIK_X */
    0x02,  /* P1 LookBack — DIK_1 */
    0x19,  /* P2 Start    — DIK_P */
    0x24,  /* P2 Left     — DIK_J */
    0x26,  /* P2 Right    — DIK_L */
    0x17,  /* P2 Up       — DIK_I */
    0x25,  /* P2 Down     — DIK_K */
    0x18,  /* P2 Jump     — DIK_O */
    0x16,  /* P2 Accel    — DIK_U */
    0x31,  /* P2 DriftL   — DIK_N */
    0x32,  /* P2 DriftR   — DIK_M */
    0x03,  /* P2 LookBack — DIK_2 */
};

/**
 * InitSonicR_A — 0x004781C0 — 200 bytes
 * Copies 20 default keyboard scan codes into the runtime key mapping array.
 */
void InitSonicR_A(void)
{
    DebugLog("InitSonicR_A\n");

    g_keyMap_P1_Start    = g_defaultKeyMap[0];
    g_keyMap_P1_Left     = g_defaultKeyMap[1];
    g_keyMap_P1_Right    = g_defaultKeyMap[2];
    g_keyMap_P1_Up       = g_defaultKeyMap[3];
    g_keyMap_P1_Down     = g_defaultKeyMap[4];
    g_keyMap_P1_Jump     = g_defaultKeyMap[5];
    g_keyMap_P1_Accel    = g_defaultKeyMap[6];
    g_keyMap_P1_DriftL   = g_defaultKeyMap[7];
    g_keyMap_P1_DriftR   = g_defaultKeyMap[8];
    g_keyMap_P1_LookBack = g_defaultKeyMap[9];
    g_keyMap_P2_Start    = g_defaultKeyMap[10];
    g_keyMap_P2_Left     = g_defaultKeyMap[11];
    g_keyMap_P2_Right    = g_defaultKeyMap[12];
    g_keyMap_P2_Up       = g_defaultKeyMap[13];
    g_keyMap_P2_Down     = g_defaultKeyMap[14];
    g_keyMap_P2_Jump     = g_defaultKeyMap[15];
    g_keyMap_P2_Accel    = g_defaultKeyMap[16];
    g_keyMap_P2_DriftL   = g_defaultKeyMap[17];
    g_keyMap_P2_DriftR   = g_defaultKeyMap[18];
    g_keyMap_P2_LookBack = g_defaultKeyMap[19];
}

/* =====================================================================
 * InitInputMappings — 0x00470CC0 — 264 bytes
 * VALIDATED: capstone 2026-03-25 — all 23 stores + 7 ROM dwords verified
 * Initializes the game options/config block with default values.
 * Also copies ROM key mapping data to the config tail.
 * ===================================================================== */
void InitInputMappings(void)
{
    /* EAX=4, EDX=1, EBX=0x28001e0, EBP=2, ECX=7 */
    extern int g_optCfg_488;

    /* ROM data at 0x4feaec — 7 dwords of key/config mapping data */
    static const int s_cfgRomData[7] = {
        0x08ae4908, 0x2828ae41, (int)0xf7fff796, 0x71f7ff8e,
        (int)0xff59f7ff, (int)0xf7ff49f7, 0x39f7f7f7
    };

    g_difficultyConfig = DIFF_NORMAL; /* 0x8fd444 */
    g_ghostToggle = 1;                /* 0x8fd448 */
    g_weatherConfig = WC_RANDOM;      /* 0x8fd44c */
    g_catchUpToggle = 1;              /* 0x8fd450 */
    g_guideToggle = 1;                /* 0x8fd454 */
    g_minimapConfig = 1;              /* 0x8fd458 */
    g_optResHigh = 0x28001e0;         /* 0x8fd464 — packed 640×480 */
    g_qualityLevel = 2;               /* 0x8fd480 */
    g_resolutionLevel = 4;            /* 0x8fd484 */
    g_optCfg_48c = 2;                 /* 0x8fd48c */
    g_splitScreenMode = 1;            /* 0x8fd45c — 0=horiz, 1=vert */
    g_optCfg_470 = 1;                 /* 0x8fd470 */
    g_optResLow = 0x14000f0;          /* 0x8fd460 — packed 320×240 */
    g_softDoubleBuf = 0;              /* 0x8fd478 */
    g_optMusicVolume = 8;               /* music slider full */
    g_musicEnabled = 1;                 /* 0x8fd4a0 — (g_optMusicVolume != 0) */
    g_optCfg_46c = 0x10;              /* 0x8fd46c */
    g_optCfg_488 = 1;                 /* 0x8fd488 */
    g_renderQuality = 1;                 /* 0x8fd474 */
    g_optCfg_47c = 0;                 /* 0x8fd47c */
    g_stereoEnabled = 1;              /* 0x8fd494 */
    g_vocalsEnabled = 1;              /* 0x8fd498 */
    g_doubleWidthFlag = 0;            /* 0x8fd490 */
#ifdef SONICR_DC
    g_optSfxVolume = 4; /* 0x8fd49c */
#else
    g_optSfxVolume = 4; /* 0x8fd49c */
#endif

    /* Copy ROM config data to tail array (original: sentinel-terminated loop
     * copying 7 dwords from stack buffer to 0x8fd4a4+) */
    {
        int i;
        for (i = 0; i < 7; i++) {
            g_optCfgRomData[i] = s_cfgRomData[i];
        }
        g_optCfgRomData[7] = -1;               /* sentinel */
    }
}

void InitSaveData(void) {
    /* Creates SAVE directory if it doesn't exist */
    /* mkdir("save", 0755); — platform-dependent */
}

void InitCDPlayer(void) {
    OpenCDDevice();
}

void InitDirectInput(void) {
    /* DirectInputCreateA(g_hInstance_dinput, 0x500, &g_lpDirectInput, NULL);
     * Then creates keyboard device and sets data format.
     * Actual device polling is in PollAllInputDevices. */
}

/**
 * InitPadTypes — FUN_00477efc — 519 bytes
 *
 * Top-level joystick/gamepad initialization. Original uses DirectInput:
 * 1. Calls InitJoystickConfig (0x477c24) — loads button mapping defaults
 * 2. Calls FUN_00477d14 — reads JOYSTICK.INF axis config
 * 3. Clears all joystick state globals (0x675c1c-0x675c3c)
 * 4. DirectInputCreateA → enumerates joystick devices
 * 5. For each device: CreateDevice, SetDataFormat, SetCooperativeLevel, Acquire
 * 6. Stores device count to g_initFeatureC, per-device flags to g_joystickDeviceFlags
 * 7. Calls SyncJoystickSlots (0x477d94) — copies device names to player slots
 * 8. Sets g_joystickInitDone = 1
 *
 * SDL port: gamepads handled by SDL_GameController.
 * For Dreamcast: replace with KOS maple bus controller detection.
 * The globals written here (g_initFeatureC, g_joystickSlots, g_joystickDeviceFlags)
 * are read by the input polling system — populate them from the platform API.
 */
void InitPadTypes(void) {
    /* Step 1 in the binary (0x477EFC → 0x477C24): populate
     * g_joystickConfigWords[] with the per-button-index → pad-bit
     * defaults. Was missing in the SDL port — left g_joystickConfigWords
     * all zeros, which made every gamepad button do nothing. */
    InitJoystickConfig();

    /* Step 2 (0x477F0C → 0x477D14): restore the saved per-slot names and
     * button mappings from JOYSTICK.INF, over the default name that
     * InitJoystickConfig just stamped into all four slots.
     *
     * This has to run BEFORE the device enumeration below, because
     * SyncJoystickSlots decides whether to keep or reset a slot's mapping
     * by comparing the slot's stored device name against the detected one.
     * Without this load the stored name is always the default, the compare
     * always mismatches, and every launch wipes the mapping back to
     * g_joystickConfigWords. On DC this reads SONICR_PAD off the VMU —
     * all four 282-byte slots, so each pad keeps its own mapping. */
    LoadJoystickConfig();

#ifdef SONICR_SDL
    g_initFeatureC = platform_init_gamepads();
    DebugLog("InitPadTypes: %d gamepad(s) detected\n", g_initFeatureC);
#else
    g_initFeatureC = 0;
#endif

    SyncJoystickSlots();
}

/**
 * LoadGameState — 0x004CD904
 * The REAL game init function (mislabeled in the binary).
 * Loads character models, animations, sets game defaults.
 */
void LoadGameState(void)
{
    /* g_tpageBase stays 0 for our GL port. The binary uses 1 to offset
     * g_tpageUIAlt away from g_tpageCount, but that ripples through
     * character rendering code with hardcoded tpage assumptions.
     * Instead, each track init sets g_tpageUIAlt = g_tpageCount + 1
     * to separate playfield tiles from the sky gradient. */

    /* Binary: [0x63ed90]+0x28 checks D3D capability → sets [0x4fc23c] and [0x8f6c50].
     * 16bpp/D3D path: 0x4fc23c=4, 0x8f6c50=1. Our GL build emulates D3D mode. */
    g_d3dSurfaceMode = 4;
    g_tpageGroupShift = 1;

    /* Binary calls 0x4D1040 here — sound buffer offset table init */
    InitSoundOffsetTables();

    /* Menu, character-select and replay defaults — 0x4CD99D-0x4CDA7D.
     * 0x4CDA6B (g_cdAvailable = 0) and the CD calls after this block
     * (0x4D0C8C drive scan, 0x4D0F4C FindSonicRCD) are not run here: the port
     * sets CD state in FindSonicRCD, called from the title and results screens. */
    g_charSelectIndex[1] = 1;               /* 0x4CD99D */
    g_charSelection[1] = 1;                 /* 0x4CD9A3 */
    g_prevNumViewports = 1;                 /* 0x4CD9AB */
    g_superSonicSeed = 0;                   /* 0x4CD9B1 */
    g_lastCourseSelection = 0;              /* 0x4CD9B7 */
    g_lastMultiSelection = 0;               /* 0x4CD9BD */
    g_lastTimeAttackSelection = 0;          /* 0x4CD9C3 */
    g_lastMenuSelection = 0;                /* 0x4CD9C9 */
    g_lsPackedCursor = 0;                   /* 0x4CD9CF */
    g_charSelectIndex[0] = 0;               /* 0x4CD9D5 */
    g_charSelectIndex[2] = 2;               /* 0x4CD9DB */
    g_charSelection[0] = 0;                 /* 0x4CD9E0 */
    g_charSelection[2] = 2;                 /* 0x4CD9E6 */
    g_rankCursorPersist = 0;                /* 0x4CD9EB */
    g_rankPagePersist = 0;                  /* 0x4CD9F1 */
    g_netSavedModeIdx = 0;                  /* 0x4CD9F7 */
    g_netSavedTrackIdx = 0;                 /* 0x4CD9FD */
    g_netSavedCharId = 0;                   /* 0x4CDA03 */
    g_netSavedButtonByte = 0;               /* 0x4CDA09 */
    g_demoMode = DEMO_NONE;                 /* 0x4CDA0F */
    g_replayCharIds[0] = 0;                 /* 0x4CDA15 */
    g_replayCharIds[2] = 2;                 /* 0x4CDA1B */
    g_nextScreenId = 3;                     /* 0x4CDA2F */
    g_charSelectIndex[3] = 3;               /* 0x4CDA37 */
    g_replayCharIds[1] = 1;                 /* 0x4CDA3D */
    g_replayCharIds[4] = 4;                 /* 0x4CDA43 */
    g_trackId = TRACK_RESORT_ISLAND;        /* 0x4CDA48 */
    g_charSelection[3] = 3;                 /* 0x4CDA57 */
    g_replayCharIds[3] = 3;                 /* 0x4CDA5D */
    ResetDemoCycle();                       /* 0x4CDA63: g_demoCycleIndex = -1 → attract plays island.dem first */
    g_ghostDataExists = 0;                  /* 0x4CDA71 */
    g_raceType = 0;                         /* 0x4CDA77 */
    g_raceSubMode = SUBMODE_NORMAL;         /* 0x4CDA7D */
    g_weatherType = WEATHER_CLEAR;
    BuildGridUVTable();
    g_raceSpeedMult = 0x1E;
    InitDefaultTimeTables();

    /* Far clip bounds + quality — binary sets these at 0x4CD8xx after InitDefaultTimeTables.
     * g_clipFar = 0x3FF, g_clipNear = 0xFF, g_maxPolyCount = 0x2800.
     * g_qualityLevel defaults to 4 (highest) for D3D hardware. */
    g_clipFar = 0x3FF; /* binary: 0x3FF; increase for extended draw distance */
    g_clipNear = 0xFF;
    g_maxPolyCount = 0x2800;
    g_qualityLevel = 4;
    /* Binary 0x4CDBFE: build the 256-entry UV lookup table. Must run before any
     * renderer, since the track, character, HUD and sprite paths all index it. */
    InitUVLUT();
    /* Binary 0x4CDC03: reconcile ghost file best times with checkpoint tables */
    LoadAllGhostTimes();
    /* Binary LoadGameState 0x4cd8ea/0x4cd8f0: reset the item-RNG index and point the
     * ring-spawn stream at the random-table memory (0x0092498C). The random tables were
     * already generated by InitRandomTables (0x4ca5fc); g_randTable1/g_randTable2 alias
     * this same memory, so it must NOT be overwritten here. (An earlier port bug filled
     * 16384 Random() shorts over this region, clobbering both tables — the binary does no
     * such fill.) */
    g_randomRingIdx = 0;
    g_ringSpawnReadPtr = g_randomRingBuffer;
    /* Array allocations — must happen before LoadCharacterModels writes into them.
     * In the original binary these are allocated earlier in the init sequence. */
    memset(g_objectStructStorage, 0, sizeof(uint32_t) * 4096 * 17);
    memset(g_vertexArrayStorage, 0, sizeof(SrcVertex) * 32768);
    memset(g_ringSpawnStorage, 0, sizeof(uint32_t) * 4096 * 4);
    memset(g_splineWaypointStorage, 0, sizeof(uint32_t) * 4096 * 3);
    g_objectStructArray = g_objectStructStorage;
    g_vertexArrayBase   = g_vertexArrayStorage;
    g_polygonArrayBase  = g_polygonStorage;
    g_ringSpawnArray    = (int *)g_ringSpawnStorage;
    g_splineWaypoints   = g_splineWaypointStorage;
    LoadCharacterModels();                  /* 0x4cda97 */
    LoadCharacterAnimations();              /* 0x4cda9c */
    InitAnimData();
    /* 0x4cdaa1 — the .GRD tables have to be loaded before the tint, which is
     * an in-place `+=` over g_charLightingTable. InitOptionStuff reloads and
     * re-tints before the main menu, so char select was unaffected, but
     * without this the logos and title screen run on a flat table. */
    LoadCharacterGouraudTables();
    TintCharacterGouraudTables(0, 0, 0);    /* 0x4cdaa8 */

    /* FillVertexColorsByDepth called during LoadGameState init (binary 0x4CD98E).
     * EAX=0xFF, EDX=0, EBX=0, ECX≈0, stack=[0, 0xFF].
     * Fills both g_lightingRamp13bit (13-bit fixed for sort-list renderer)
     * and g_lightingRamp8bit (0-255 for env-map renderer). */
    FillVertexColorsByDepth(0xFF, 0, 0, 0, 0, 0xFF);

    /* Load character textures into tpages 0 and 1.
     * Character model faces use tpage byte directly (no offset added).
     * Verified: disasm 0x455CD3 reads face+0x28 byte with no ADD.
     * Title screen R model overwrites these — charsel must reload. */
    g_tpageCharacters = 2;
    g_tpageCharBase = 2;
    LoadTPageRGB(0, PATH_PLAYER00_RAW);
    LoadTPageRGB(1, PATH_PLAYER01_RAW);
}

/**
 * UpdateWeather — 0x0046EC0C — 43 bytes
 * Dispatches to per-track weather update.
 * Only tracks 1, 3, and 5 have weather.
 */
void UpdateWeather(void)
{
    if (g_trackId == TRACK_RESORT_ISLAND) {
        TickIslandParticles();
    }
    if (g_trackId == TRACK_REACTIVE_FACTORY) {
        TickFactoryParticles();
    }
    if (g_trackId == TRACK_RADIANT_EMERALD) {
        TickEmeraldParticles();
    }
}

/**
 * UpdateTrackWorld — 0x0047FAFC — 199 bytes
 * Per-track world update: collectibles, particles, animation, water scroll.
 */
void UpdateTrackWorld(void)
{
    if (g_trackId == TRACK_RESORT_ISLAND) {
        UpdateTrackWorld_Island();
        return;
    }

    if (g_trackId == TRACK_RADICAL_CITY) {
        UpdateTrackWorld_City();
        return;
    }

    if (g_trackId == TRACK_REACTIVE_FACTORY) {
        UpdateTrackWorld_Factory();
        return;
    }

    if (g_trackId == TRACK_REGAL_RUIN) {
        UpdateTrackWorld_Ruin();
        return;
    }

    if (g_trackId == TRACK_RADIANT_EMERALD) {
        UpdateTrackWorld_Emerald();
        return;
    }
}

/**
 * AnimateTrackObjects — 0x0047FBC4 — 72 bytes
 * Per-track object animation: writes rotation angles to track objects.
 */
void AnimateTrackObjects(void)
{
    TickObjectAngleCounters();

    if (g_trackId == TRACK_RESORT_ISLAND){
        AnimateTrackObjects_Island();
        return;
    }

    if (g_trackId == TRACK_RADICAL_CITY) {
        AnimateTrackObjects_City();
        return;
    }

    if (g_trackId == TRACK_REACTIVE_FACTORY) {
        AnimateTrackObjects_Factory();
        return;
    }

    if (g_trackId == TRACK_REGAL_RUIN) {
        AnimateTrackObjects_Ruin();
        return;
    }

    if (g_trackId == TRACK_RADIANT_EMERALD) {
        AnimateTrackObjects_Emerald();
    }
}

/* =====================================================================
 * Display/viewport configuration
 * ===================================================================== */

/**
 * SetScreenDimensions — FUN_004CBEFC — 293 bytes
 * Sets screen center, projection scales, and viewport config block.
 */
void SetScreenDimensions(void)
{
    int scaleX = (g_screenWidth << 8) / 0x140;
    DebugLog("SetScreenDimensions\n");
    g_dispCenterX = g_screenWidth / 2;
    g_dispCenterY = g_screenHeight / 2;
    g_screenScale = g_screenWidth / 0x140;
    g_projScaleX = scaleX;
    g_projScaleY = (g_screenHeight << 8) / 0xf0;

    g_projScaleXCurrent = scaleX;
    g_screenCenterX = g_screenWidth / 2;
    g_screenCenterY = g_screenHeight / 2;
    g_vpClipRight10 = g_screenWidth * 0x400 - 1;
    g_vpClipRight16 = g_screenWidth * 0x10000 - 1;

    /* Populate g_viewportArray[0] with defaults */
    g_viewportArray[0]  = 0;                         /* clipLeft */
    g_viewportArray[1]  = 0;                         /* clipTop */
    g_viewportArray[2]  = g_screenWidth - 1;         /* clipRight */
    g_viewportArray[3]  = g_screenHeight - 1;        /* clipBottom */
    g_viewportArray[4]  = scaleX;                    /* projScaleX */
    g_viewportArray[5]  = scaleX;                    /* projScaleXCurrent */
    g_viewportArray[6]  = g_projScaleY;              /* projScaleY */
    g_viewportArray[7]  = g_screenWidth / 2;         /* centerX */
    g_viewportArray[8]  = g_screenHeight / 2;        /* centerY */
    g_viewportArray[9]  = g_screenWidth;             /* widthFull */
    g_viewportArray[10] = g_screenHeight;            /* heightFull */
    g_viewportArray[0x14] = 0;                       /* sort list offset */
}

/**
 * ComputeViewportBounds — FUN_004CBDC8 — 308 bytes
 * Computes viewport clip bounds from resolution level and screen dimensions.
 */
void ComputeViewportBounds(void)
{
    int halfW = g_screenWidth / 2;
    int halfH = g_screenHeight / 2;
    int qW = (int)((halfW + (halfW >> 31) * -4) - (unsigned int)((halfW >> 31) << 1 < 0)) >> 2;
    int qH = (int)((halfH + (halfH >> 31) * -4) - (unsigned int)((halfH >> 31) << 1 < 0)) >> 2;

    switch (g_resolutionLevel) {
        case 0:
            g_dispHalfHeight = halfH;
            g_dispHalfWidth = halfW;
            break;
        case 1:
            g_dispHalfHeight = halfH + qH;
            g_dispHalfWidth = halfW + qW;
            break;
        case 2:
            g_dispHalfHeight = halfH + qH * 2;
            g_dispHalfWidth = qW * 2 + halfW;
            break;
        case 3:
            g_dispHalfHeight = g_screenHeight - qH;
            g_dispHalfWidth = g_screenWidth - qW;
            break;
        case 4:
            g_dispHalfHeight = g_screenHeight;
            g_dispHalfWidth = g_screenWidth;
            break;
    }
    g_dispClipLeft   = g_dispCenterX - g_dispHalfWidth / 2;
    g_dispClipTop    = g_dispCenterY - g_dispHalfHeight / 2;
    g_dispClipRight  = g_dispCenterX + g_dispHalfWidth / 2 - 1;
    g_dispClipBottom = g_dispCenterY + g_dispHalfHeight / 2 - 1;
    g_dispProjScaleX = (g_dispHalfWidth << 8) / 0x140;
    g_dispProjScaleY = (g_dispHalfHeight << 8) / 0xF0;
}

void SetupViewportConfig(void);   /* defined below */

/* Base (framebuffer) screen height, as opposed to the height we render into.
 * Set alongside g_screenHeight at startup; RenderHeightForMode() derives the
 * per-mode render height from it. */
int g_screenHeightBase = 480;

/**
 * RenderHeightForMode — PORT ADDITION, no binary equivalent.
 *
 * Tile-aligned render height for the current split-screen mode.
 *
 * PVR clips to 32x32 tile boundaries for free, but 480/2 = 240 = 7.5 tiles, so
 * a HORIZONTAL split line cannot be expressed to the hardware at full height.
 * Rounding the half down to a tile multiple — 224 = 7 tiles, so 448 total —
 * makes it expressible, at the cost of 32 scanlines. A VERTICAL split divides
 * the width instead (320 = 10 tiles, already aligned), so it keeps full height.
 *
 * SDL has an arbitrary-rect hardware scissor and needs no alignment, so it
 * always renders full height. See PLAN_DC_HW_TILE_CLIP.md.
 */
int RenderHeightForMode(void)
{
#ifdef SONICR_DC
    /* Only modes that divide the HEIGHT need alignment. */
    int splitsVertically = (g_numHumans > 2) ||
                           (g_numHumans == 2 && g_splitScreenMode != 1);
    if (splitsVertically) {
        /* Largest whole-tile multiple fitting in half the screen, doubled.
         *
         * The mask must be in the SAME UNITS as g_screenHeightBase, which is
         * the engine's VIRTUAL height — always 480, since main.c:748 sets it
         * unconditionally, 240P builds included. A PVR tile is 32 REAL pixels,
         * and in 240P the backend halves x/y at submit, so a tile spans 64
         * virtual units there against 32 in the 480 build:
         *
         *   480 build : (480/2) & ~31 = 224 -> 448 virtual = 448 real,
         *               half = 224 real = 7 tiles exactly
         *   240P build: (480/2) & ~63 = 192 -> 384 virtual = 192 real,
         *               half =  96 real = 3 tiles exactly
         *
         * Masking at ~31 in 240P produced 448 virtual = 224 real, whose half
         * is 112 real = 3.5 tiles — not expressible as a tile rect at all,
         * which is why the split viewports did not line up with their clip
         * regions. The old comment here claimed "96*2 = 192" for 320x240; that
         * is the right answer in REAL lines, but the code was computing in
         * virtual ones.
         *
         * Cost in 240P: 192 of 240 real lines, so 48 letterboxed. That is
         * forced — each half must be a multiple of 32 real lines, so 96+96 is
         * the largest symmetric split 240 allows. */
#ifdef SONICR_DC_240P
        const int tileMaskVirtual = ~63;
#else
        const int tileMaskVirtual = ~31;
#endif
        int half = (g_screenHeightBase / 2) & tileMaskVirtual;
        if (half > 0) {
            return half * 2;
        }
    }
#endif
    return g_screenHeightBase;
}

/**
 * ApplyViewportGeometry — PORT ADDITION, no binary equivalent.
 *
 * Picks the render height for the current mode and rebuilds every derived
 * value. Replaces bare SetupViewportConfig() at its call sites.
 *
 * The height is a function of the mode rather than a saved/restored value:
 * every "back to full screen" path in main.c already works by setting
 * g_numHumans = 1 and re-running the viewport setup, so deriving from the mode
 * makes those paths correct with no new hooks — and makes it impossible for a
 * reduced height to stick if some exit path is missed.
 *
 * All three calls are needed, not just SetupViewportConfig: g_projScaleY comes
 * from SetScreenDimensions and the clip bounds from ComputeViewportBounds, so
 * changing the height while rebuilding only the viewport rects would leave the
 * projection stale.
 */
#ifdef SONICR_DC
#include <kos.h>
#endif
void ApplyViewportGeometry(void)
{
    g_screenHeight = RenderHeightForMode();

#ifdef SONICR_DC
    int shift = (g_screenHeightBase - g_screenHeight) / 2;

    if (shift) {
        if (vid_check_cable() != CT_VGA) {
            shift /= 2;                   /* TV raster: half the lines of VGA */
#ifdef SONICR_DC_240P
            shift += 2;
#endif
        }
    }

#ifdef SONICR_DC_240P
    /* KOS 240 non-VGA starts lower on screen than 480 for some reason, adjust for that */
    if (vid_check_cable() != CT_VGA) {
        shift -= 6;
    }
#endif

    unsigned int y  = (unsigned int)(vid_mode->bitmapy + shift);
    PVR_SET(PVR_BITMAP_Y, (y << 16) | y);
#endif

    SetScreenDimensions();
    ComputeViewportBounds();
    SetupViewportConfig();
}

/**
 * SetupViewportConfig — FUN_004CBA28 — 906 bytes
 * Sets up viewport config array from display clip bounds.
 */
void SetupViewportConfig(void)
{
    int *vp = g_viewportArray;

    g_viewportIndex = 1;

    if (g_numHumans <= 1) {
        /* 0x4cba4c: 1P (or init with g_numHumans==0) — all viewports get full screen bounds */
        for (int i = 0; i < 4; i++) {
            vp[i * 21 + 0] = g_dispClipLeft;
            vp[i * 21 + 1] = g_dispClipTop;
            vp[i * 21 + 2] = g_dispClipRight;
            vp[i * 21 + 3] = g_dispClipBottom;
        }
    }
    else if (g_numHumans == 2) {
        /* 0x4cba9c: 2P — split screen */

        /* 0x4cbaad: Vertical split (side by side) */
        if (g_splitScreenMode == 1) {
            g_viewportIndex = 1;
            vp[0]  = g_dispClipLeft;              /* VP0 left */
            vp[1]  = g_dispClipTop;               /* VP0 top */
            vp[2]  = g_dispCenterX - 1;           /* VP0 right = mid-1 */
            vp[3]  = g_dispClipBottom;             /* VP0 bottom */
            vp[21] = g_dispCenterX;               /* VP1 left = mid */
            vp[22] = g_dispClipTop;               /* VP1 top */
            vp[23] = g_dispClipRight;             /* VP1 right */
            vp[24] = g_dispClipBottom;             /* VP1 bottom */
        }
        /* 0x4cbb03: Horizontal split (stacked, default) */
        else {
            g_viewportIndex = 0;
            vp[0]  = g_dispClipLeft;              /* VP0 left */
            vp[1]  = g_dispClipTop;               /* VP0 top */
            vp[2]  = g_dispClipRight;             /* VP0 right */
            vp[3]  = g_dispCenterY - 1;           /* VP0 bottom = mid-1 */
            vp[21] = g_dispClipLeft;              /* VP1 left */
            vp[22] = g_dispCenterY;               /* VP1 top = mid */
            vp[23] = g_dispClipRight;             /* VP1 right */
            vp[24] = g_dispClipBottom;             /* VP1 bottom */
        }
    }
    /* 0x4cbb5c: 3-4P — quadrant split */
    else {
        vp[0] = g_dispClipLeft;
        vp[1] = g_dispClipTop;
        vp[2] = g_dispCenterX - 1;
        vp[3] = g_dispCenterY - 1;
        vp[21] = g_dispCenterX;
        vp[22] = g_dispClipTop;
        vp[23] = g_dispClipRight;
        vp[24] = g_dispCenterY - 1;
        vp[42] = g_dispClipLeft;
        vp[43] = g_dispCenterY;
        vp[44] = g_dispCenterX - 1;
        vp[45] = g_dispClipBottom;
        vp[63] = g_dispCenterX;
        vp[64] = g_dispCenterY;
        vp[65] = g_dispClipRight;
        vp[66] = g_dispClipBottom;
    }

    int numVP = g_numHumans;
    if (g_netSessionActive != 0) {
        numVP = 4;
    }

    if (numVP > 0) {
        int scaleXref = g_screenWidth * 0x100;
        int scaleXdiv = g_screenWidth * 0x140;
        int scaleYref = g_screenHeight * 0x100;
        int scaleYdiv = g_screenHeight * 0xF0;
        int sortBase = 0;
        for (int i = 0; i < numVP; i++) {
            int off = i * 21;

            int clipL = vp[off + 0];
            int clipT = vp[off + 1];
            int clipR = vp[off + 2];
            int clipB = vp[off + 3];

            vp[off + 7] = (clipL + clipR + 1) / 2;
            vp[off + 8] = (clipT + clipB + 1) / 2;
            vp[off + 9] = (clipR - clipL) + 1;
            vp[off + 10] = (clipB - clipT) + 1;

            if (g_numHumans == 2) {
                vp[off + 4] = g_dispProjScaleX;
                vp[off + 5] = g_dispProjScaleX;
            }
            else {
                int vpW = vp[off + 9];
                vp[off + 4] = (vpW * scaleXref) / scaleXdiv;
                vp[off + 5] = (vpW * scaleXref) / scaleXdiv;
            }
            int vpH;
            if (g_numHumans == 2) {
                vpH = g_dispProjScaleY;
            }
            else {
                vpH = (scaleYref * vp[off + 10]) / scaleYdiv;
            }
            vp[off + 6] = vpH;

            vp[off + 11] = clipL << 10;
            vp[off + 12] = (clipR + 1) * 0x400 - 1;
            vp[off + 13] = clipL << 16;
            vp[off + 14] = (clipR + 1) * 0x10000 - 1;
            int pad = g_surfaceStride - vp[off + 9];
            vp[off + 15] = pad;
            vp[off + 16] = pad * 2;
            if (g_netSessionActive == 0) {
                vp[off + 20] = sortBase;
            }
            else {
                vp[off + 20] = 0;
            }
            sortBase += 0x400;
        }
    }

    /* Slots 4 and 5 — the full-screen UI viewport.
     *
     * Everything that draws over the whole screen selects slot 4 (0x8FB4B8):
     * RenderHUD restores it after the per-viewport loop (0x4cd44d) and again
     * before the fade iris (0x4cd484), and every menu screen sets it before
     * drawing. Slot 5 (0x8FB50C) is its companion, used by the credits and
     * unlock slides. The binary gets these from D3D surface init; nothing in
     * this port filled them, so a split-screen race left them stale and
     * anything drawn through them inherited viewport 0's half-screen centre
     * and projection.
     *
     * The values are what a single full-screen viewport gets above:
     * (g_dispHalfWidth << 8) / 0x140 IS g_dispProjScaleX, and the vpW-derived
     * form collapses to the same number when vpW is the whole display. */
    {
        int fullL = g_dispClipLeft;
        int fullT = g_dispClipTop;
        int fullR = g_dispClipRight;
        int fullB = g_dispClipBottom;

        for (int s = 4; s <= 5; s++) {
            int off = s * 21;

            vp[off + 0] = fullL;
            vp[off + 1] = fullT;
            vp[off + 2] = fullR;
            vp[off + 3] = fullB;
            vp[off + 4] = g_dispProjScaleX;
            vp[off + 5] = g_dispProjScaleX;
            vp[off + 6] = g_dispProjScaleY;
            vp[off + 7] = (fullL + fullR + 1) / 2;
            vp[off + 8] = (fullT + fullB + 1) / 2;
            vp[off + 9] = (fullR - fullL) + 1;
            vp[off + 10] = (fullB - fullT) + 1;
            vp[off + 11] = fullL << 10;
            vp[off + 12] = (fullR + 1) * 0x400 - 1;
            vp[off + 13] = fullL << 16;
            vp[off + 14] = (fullR + 1) * 0x10000 - 1;
            int pad = g_surfaceStride - vp[off + 9];
            vp[off + 15] = pad;
            vp[off + 16] = pad * 2;
            vp[off + 20] = 0;
        }
    }

    g_viewportWidth  = vp[9];
    g_viewportHeight = vp[10];
}

/* =====================================================================
 * InitJoystickConfig — FUN_00477c24 — 239 bytes — VALIDATED
 * Initializes joystick config with defaults, copies to 4 player slots.
 *
 * NOTE: g_joystickConfigWords[i] is the 16-bit pad-bit pattern that gets
 * OR'd into the player input word when joystick button index `i` is held.
 * Layout is therefore device-specific — the array is a per-button-index
 * table for whatever pad the defaults target.
 *
 * Original (binary 0x4FE...): defaults targeted "Microsoft Sidewinder
 * game pad" (10 buttons + Mode), kept here for reference:
 *
 *   static const short s_origJoystickConfigWords[11] = {
 *       0x600,  // btn 0: Jump+altJump (ACTION/SELECT)
 *       0x100,  // btn 1: Accel       (ACCEL/BACK)
 *       0x200,  // btn 2: alt-jump
 *       0x040,  // btn 3: LookBack    (CAMERA)
 *       0x040,  // btn 4: LookBack
 *       0x040,  // btn 5: LookBack
 *       0x008,  // btn 6: DriftL      (L.BRAKE)
 *       0x080,  // btn 7: DriftR      (R.BRAKE)
 *       0x800,  // btn 8: Start       (PAUSE/START)
 *       0x800,  // btn 9: Start
 *       0x00A,  // btn 10: DriftL+0x02
 *   };
 *
 * Active defaults below are laid out for the SDL_GameController button
 * ordinals (GCBTN_*), which is what platform_sdl.c hands us for any pad in
 * SDL's mapping database — so they are correct on any mapped device rather
 * than on one particular pad. A device with no mapping falls back to raw
 * driver indices, where this layout will be wrong and the user remaps via
 * the in-game joystick remap UI.
 *
 * Only indices below JOY_SLOT_CFG_WORDS are remappable per player; the rest
 * are shared across all four slots, which is why the two shoulders end up on
 * opposite sides of that line.
 * ===================================================================== */
void InitJoystickConfig(void)
{
    const char *src = s_defaultDeviceName;
    char *dst = g_joystickNameBuf;
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';

#ifdef SONICR_DC
    /* DC controller — 7 stable button indices from platform_dc.c:
     *   0=A  1=B  2=X  3=Y  4=L trigger  5=R trigger  6=Start
     * Direction (D-pad / analog stick) is wired separately at the
     * platform layer and doesn't go through this table. */
    for (int k = 0; k < 16; k++) {
        g_joystickConfigWords[k] = 0;
    }
    g_joystickConfigWords[0] = PAD_JUMP;    /* A         → Jump (ACTION) */
    g_joystickConfigWords[1] = PAD_ACCEL;   /* B         → Back (accel bit doubles as menu cancel) */
    g_joystickConfigWords[2] = PAD_ACCEL;   /* X         → Accel */
    g_joystickConfigWords[3] = PAD_CAMERA;  /* Y         → Camera (cycles 3 height/zoom levels) */
    g_joystickConfigWords[4] = PAD_DRIFTL;  /* L trigger → DriftL */
    g_joystickConfigWords[5] = PAD_DRIFTR;  /* R trigger → DriftR */
    g_joystickConfigWords[6] = PAD_START;   /* Start     → Start/Pause */
#else
    /* Mirrors the Dreamcast face layout above — A jump, B and X accelerate,
     * Y camera, shoulders brake — so muscle memory carries between the two
     * ports. Both the shoulder buttons and the analog triggers brake, since
     * which of the two a player reaches for is a matter of habit. */
    for (int k = 0; k < 32; k++) {
        g_joystickConfigWords[k] = 0;
    }
    /* Remappable per player (below JOY_SLOT_CFG_WORDS). */
    g_joystickConfigWords[GCBTN_A]             = PAD_JUMP;   /* Jump (ACTION) */
    g_joystickConfigWords[GCBTN_B]             = PAD_ACCEL;  /* Accel; doubles as menu cancel */
    g_joystickConfigWords[GCBTN_X]             = PAD_ACCEL;  /* Accel — as on DC */
    g_joystickConfigWords[GCBTN_Y]             = PAD_CAMERA; /* Camera (3 height/zoom levels) */
    g_joystickConfigWords[GCBTN_START]         = PAD_START;  /* Start / Pause */
    g_joystickConfigWords[GCBTN_LEFTSHOULDER]  = PAD_DRIFTL; /* L.BRAKE */
    /* Left unbound: GCBTN_BACK (PAD_ACCEL is already the cancel bit on B, and
     * binding it here would make Back accelerate mid-race), GCBTN_GUIDE, and
     * both stick clicks. */

    /* Shared across all four slots — past the per-slot config's capacity. */
    g_joystickConfigWords[GCBTN_RIGHTSHOULDER] = PAD_DRIFTR; /* R.BRAKE */
    g_joystickConfigWords[GCBTN_DPAD_UP]       = PAD_UP;
    g_joystickConfigWords[GCBTN_DPAD_DOWN]     = PAD_DOWN;
    g_joystickConfigWords[GCBTN_DPAD_LEFT]     = PAD_LEFT;
    g_joystickConfigWords[GCBTN_DPAD_RIGHT]    = PAD_RIGHT;
    g_joystickConfigWords[GCBTN_TRIGGER_LEFT]  = PAD_DRIFTL;
    g_joystickConfigWords[GCBTN_TRIGGER_RIGHT] = PAD_DRIFTR;
#endif

    for (int i = 0; i < 4; i++) {
        memcpy(g_joystickSlots[i], g_joystickNameBuf, 282);
        g_joystickSlotActive[i] = 0;
    }
}

/* =====================================================================
 * InitRandomTables — 0x004ca5fc — 83 bytes
 * Generate random lookup tables — 2048 bytes (rand>>6) + 256 bytes (rand/128).
 * Seeds with 0x4BEE (19438). Used by weather particles and similar effects.
 * Binary: called once during WinMain init.
 * ===================================================================== */
void InitRandomTables(void)
{
    int i;
    Srand(0x4BEE);
    for (i = 0; i < 0x800; i++) {
        g_randTable1[i] = (char)(Random() >> 6);
    }
    for (i = 0; i < 0x100; i++) {
        g_randTable2[i] = (char)(Random() / 128);
    }
}
