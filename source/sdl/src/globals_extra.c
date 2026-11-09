/**
 * globals_extra.c — Additional global variable definitions
 *
 * These are globals referenced by extern declarations across the codebase
 * but not yet defined in globals.c. Collected here to satisfy the linker.
 */

#include "sonicr_types.h"
#include "player_struct.h"
#include "vertex_struct.h"
#include "collect_effect.h"

/* Animation */
int g_animFrameCounter;
int g_animOffsetTableX[256];
int g_animOffsetTableY[256];
int g_collectAnimTimer;

/* Physics / race */
unsigned short g_autoSteerFlag;         /* 0x0054004C */
int g_bouncePosition;
int g_bounceVelocity;
int g_raceSpeedMult = 1;
int *g_ringSpawnArray;
int g_raceOrder[10];  /* binary accesses [g_numPlayers] as sentinel; needs >5 entries */
int g_rubberBandThreshold;
int g_charTypeTable[10] = { 0, 0, 0, 1, 1, 2, 3, 2, 2, 2 }; /* 0x50157A ROM */

/* Camera / rendering */
int g_fogSortThreshold;
int g_maxSortDepth = 0x3FF;
int *g_rasterFuncTable;
int g_floatMtxDest[16];
int g_parallaxWidth = 256;
int g_parallaxExtraX;
int g_parallaxState[4 * 7];             /* 0x0068B224 — stride 0x1C per viewport */

/* Water reflection scroll phase counters — 0x4FC594..0x4FC5A4 */
int g_waterScrollA = 0x4B;              /* 0x004FC594 — phase 1, +0x13/frame */
int g_waterScrollB = 0x3AA;             /* 0x004FC598 — phase 2, -0x22/frame */
int g_waterScrollC = 0x7E7;             /* 0x004FC59C — phase 3, +0x19/frame */
int g_waterScrollD = 0x456;             /* 0x004FC5A0 — phase 4, -0x2A/frame */
int g_waterScrollE = 0xDFB;             /* 0x004FC5A4 — phase 5, +0x1B/frame */
int g_d3dSurfaceMode = 4;              /* 0x004FC23C — D3D dispatch flag: 4=D3D, 8=software (LoadGameState 0x4cdc9d/0x4cdca9). Read by D3D-vs-SW dispatch in RenderParallaxStripsD3D, RenderWaterReflectionD3D, etc. */
int g_tpageGroupShift = 1;             /* 0x008F6C50 — tpage-index offset: 1=D3D layout, 4=software layout (LoadGameState 0x4cdcbc/0x4cdcc8). Added to constants 13/15/17 to derive g_tpageUIAlt/g_tpageUIAlt2. */
int g_splashCountdown;
int g_surfaceDescSize;

/* Texture pages */
void *g_tpageVertexBuf[52];
void *g_tpageIndexBuf[52];
int g_textureHandles[52];
int g_textureMaterialHandles[52];
int g_tpageCount;
int g_tpageParallax2;
int g_tpageUIAlt;

/* Viewport for track model rendering */
int g_dispProjScaleX;    /* 0x006E9888 */
int g_dispProjScaleY;    /* 0x006E988C */
int g_tpageUIAlt2;

/* Current render camera (for SetViewportClipRect no-arg wrapper) */
int *g_currentRenderCam;

/* File I/O */
FILE *g_fileHandle;

/* Display config — canonical defs are g_dispClipLeft..g_dispHalfHeight in this file below */

/* Input */
int g_diDeviceReady;
void *g_lpDIKeyboardDev;
unsigned short *g_p1JoystickPtr;
unsigned short *g_p2JoystickPtr;
unsigned short *g_p3JoystickPtr;
unsigned short *g_p4JoystickPtr;

/* Ghost replay — needs 4 players × g_ghostMaxFrames × 2 bytes.
 * g_ghostMaxFrames = 0x8000 / numViewports. For 1 player: 0x8000 = 32768.
 * Total: 4 × 32768 × 2 = 262144 bytes. */
int g_ghostTotalFrames;
const char *g_raceSubModeNames[4] = { "NORMAL", "REVERSE", "MIRRORED", "BOTH" };
const char *g_trackDirNames[6]  = { NULL, "ISLAND", "CITY", "RUIN", "FACTORY", "EMERALD" };
const char *g_charGhoNames[10]  = {
    "SONIC.GHO", "TAILS.GHO", "KNUCKLES.GHO", "AMY.GHO", "EGGMAN.GHO",
    "METAL.GHO", "TDOLL.GHO", "MKNUX.GHO", "EGROBO.GHO", "SUPER.GHO"
};

/* Intro / camera.
 * g_introSplineBase is the 64-bit side-store for the 4-byte pointer slot at
 * 0x00902498 — same treatment as g_camWaypointTable (0x902494) and
 * g_trackBoundaryWaypoints (0x90249C), its neighbours in the same clear block.
 * The address comment matters: without one, the alias sweep cannot see this
 * half of the pair, which is how a duplicate int twin survived here. */
void *g_introSplineBase;               /* 0x00902498 */
void *g_podiumCenter;

/* Track geometry */
int g_objectCount;
int g_objectVertexCount;
int g_objectPolygonCount;
int g_sceneryCount;
int g_sceneryPolygonCount;
int g_sceneryVertexCount;
int g_vertexIndexRunning;
int g_polygonIndexRunning;
int g_doubleSidedCount;
int g_totalPositions;
int g_posDataCount1, g_posDataCount2, g_posDataCount3;
/* g_posDataCount4 is 0x00901EE0 — the waypoint count. Keep the address on it:
 * without one the alias sweep can't see it, and a decoy g_unk_901EE0 lived
 * alongside it undetected. The tag-mode AI reads it as the track length. */
int g_posDataCount4;                    /* 0x00901EE0 */
int g_posDataCount5, g_posDataCount6, g_posDataCount7;

/* Track init */
int g_lapDistThreshold;
int g_trackLength;
int g_raceDistance;
int g_raceDistanceAlt;

/* Model loading */
/* Character model metadata, stride 0x50. The first 10 entries are the playable
 * characters; the region runs to 26 entries and what occupies 10..25 is not yet
 * identified — nothing in the port indexes past 9. */
ModelMeta __attribute__((aligned(32))) g_modelMeta[26];      /* 0x007130A4 */
int g_resultModelIndices[40];                                 /* 0x0068153C — per-player model indices for result 3D objects */
int __attribute__((aligned(32))) g_limbMetaTable[768];       /* 0x007133A0 — 128 limbs × 6 ints (need 690 for 115 limbs) */
/* Static storage for big game arrays — originally calloc'd in init.c.
 * In the binary these are fixed BSS addresses. Sizes match init.c allocations.
 * uint32_t for alignment (binary uses int-aligned BSS). */
uint32_t __attribute__((aligned(32))) g_objectStructStorage[4096 * 17];    /* 0x00712D44 — 4K objects × 68 bytes (17 ints) = 272 KB */
SrcVertex __attribute__((aligned(32))) g_vertexArrayStorage[32768];        /* 0x0072A5D8 — 32K vertices × 64 bytes = 2 MB */
uint32_t __attribute__((aligned(32))) g_ringSpawnStorage[4096 * 4];        /* heap in binary — 4K rings × 16 bytes (4 ints) = 64 KB */
uint32_t __attribute__((aligned(32))) g_splineWaypointStorage[4096 * 3];   /* heap in binary — 4K waypoints × 12 bytes (3 ints) = 48 KB */

/* Static storage for polygon/face data — max ~16K polys (1917 model + ~14K track).
 * Original BSS address 0x00712D4C. g_polygonArrayBase points here. */
char __attribute__((aligned(32))) g_polygonStorage[16384 * 48];  /* 768 KB */

/* Character model face data — SEPARATE from g_polygonArrayBase.
 * Original BSS address 0x00713E68. 10 models, max 1917 polys total. */
char __attribute__((aligned(32))) g_charFaceStorage[2048 * 48];  /* 96 KB */
void *g_charFaceBase = g_charFaceStorage;
unsigned char g_polyTypeTable[4096]; /* 0x008F6428 */
/* 3349 verts * 32 levels * 3 channels = 321,504 ints. Round up to 322K. */
int __attribute__((aligned(32))) g_charLightingTable[322000]; /* 0x007BC4A8 — ~1.3MB */

/* Camera orientation vectors (camBlock[0]-[2], set by SetViewportClipRect) */
int g_camOrientX;               /* 0x006E9C84 */
int g_camOrientY;               /* 0x006E9C88 */
int g_camOrientZ;               /* 0x006E9C8C */

/* Camera extra vectors (camBlock[6]-[8], set by SetViewportClipRect) */
int g_camExtra6;                /* 0x006E9C9C */
int g_camExtra7;                /* 0x006E9CA0 */
int g_camExtra8;                /* 0x006E9CA4 */

/* Camera float positions (camBlock[12]-[14] as float, set by SetViewportClipRect) */
float g_camFloatX;              /* 0x006E9CA8 — worldX * (1/4096) */
float g_camFloatY;              /* 0x006E9CAC — worldY * (1/4096) */
float g_camFloatZ;              /* 0x006E9CB0 — worldZ * (1/4096) */
float g_camYawRad;              /* 0x006E9CB4 — camera yaw in radians; write-only copy, never read */
float g_camPitchRad;            /* 0x006E9CB8 — camera pitch in radians; write-only copy, never read */
float g_camFloatExtra5;         /* 0x006E9CBC — spurious copy of RenderCamera+0x44 (_tail[0], a rotation-matrix int); write-only, never read */

/* Camera extra vectors (camBlock[9]-[11], set by SetViewportClipRect) */
int g_camExtra9;                /* 0x006E9CC0 */
int g_camExtra10;               /* 0x006E9CC4 */
int g_camExtra11;               /* 0x006E9CC8 */

/* Software 3D renderer state */
int g_visibleObjectCount;       /* 0x006EAD2C — visible objects rendered this frame */
int g_processedObjectCount;     /* 0x006DA2E8 — total objects processed this frame */
int g_triggeredObjectFound;     /* 0x006E9D1C — set to 1 when trigger hit */

/* Race state globals — contiguous block at 0x006DA2EC, zeroed at race start */
int g_raceState2ec;             /* 0x006DA2EC — unknown, zeroed */
int g_triggerFlag;              /* 0x006DA2F0 — trigger check enabled */
int g_objectRenderEnable;       /* 0x006DA2F4 — 1 = normal render, 0 = alt path */
int g_raceState2f8;             /* 0x006DA2F8 — zeroed */
int g_raceState2fc;             /* 0x006DA2FC — zeroed */
int g_raceState300;             /* 0x006DA300 — zeroed */
int g_raceState304;             /* 0x006DA304 — zeroed */
int g_raceState308;             /* 0x006DA308 — zeroed */
int g_raceState30c;             /* 0x006DA30C — zeroed */
int g_raceState310;             /* 0x006DA310 — zeroed */
int g_raceState314;             /* 0x006DA314 — zeroed */
int g_triggerObjectIndex;       /* 0x006DA318 — index of trigger object in objectStructArray */
int g_farClipDepth;             /* 0x008FB35C — max depth for object visibility */
float g_farClipFloat;           /* 0x008FB364 — (float)(g_farClipDepth << 3), used for Z division */
int g_renderQuality;            /* 0x008FD474 — 1 = store camera-space XY */
int g_fogDist0;                 /* 0x006D7624 — fog threshold level 0 */
int g_fogDist1;                 /* 0x006D762C — fog threshold level 1 */
int g_fogDist2;                 /* 0x006D7634 — fog threshold level 2 */
int g_colorTintEnable;          /* 0x0094BCFC — master color tint switch */
int g_colorTintR;               /* 0x0094BD0C — additive R tint */
int g_colorTintG;               /* 0x0094BD10 — additive G tint */
int g_colorTintB;               /* 0x0094BD14 — additive B tint */
int g_renderFlags;              /* 0x008FB828 — bit 0x40 = emerald special mode */
int g_emeraldSineOffX;          /* 0x00712D68 — emerald track sine phase X */
int g_emeraldSineOffY;          /* 0x00712D6C — emerald track sine phase Y */
int g_emeraldSineOffZ;          /* 0x00712D70 — emerald track sine phase Z */

/* Collectibles / items */
void *g_triggeredObjectPtr;
char g_balloonArray[0x2C * 0x11];  /* stride 0x2C, 17 entries — see WARNING in track_anim_init.c re: 64-bit pointers */
int g_itemBoxArray[0x20 * 8];
unsigned char g_itemAnimTable[100] = { /* 0x00501590 — ROM animation response table (byte array)
    * 5 race positions × 20 random slots. Index = (racePos-1)*20 + randByte/13.
    * 1st place biased toward anim 2-3 (happy), 5th toward 1/4/6 (disappointed). */
    /* racePos 1 */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 5, 5, 6,
    /* racePos 2 */ 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6,
    /* racePos 3 */ 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6,
    /* racePos 4 */ 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 3, 4, 4, 4, 4, 4, 6, 6, 6, 6,
    /* racePos 5 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4, 4, 4, 4, 4, 4, 6, 6, 6, 6,
};
int g_p1CollectionCount;
int g_p2CollectionCount;
unsigned short g_randomRingBuffer[16384]; /* 0x0092498C — full 16K random table (32768 bytes);
                                          * ring spawn logic uses first 513 shorts. */
unsigned short *g_ringSpawnReadPtr;     /* 0x00901CD0 — circular read pointer into g_randomRingBuffer */
int g_randomRingIdx;

/* Sound */
void *g_soundBuffers[64];
int g_soundActive[64];
int g_cdAvailable;

/* Menus */
int g_prevNumViewports = 1;             /* 0x0068AF74 — boot value, 0x4CD9AB */
int g_lastCourseCursor = 0;             /* 0x0068AF94 — boot value, 0x4CD9B7 */
int g_lastMenuSelection;
int g_lastTimeAttackSelection;
int g_menuAnimY;
int g_menuIdleStartTime;                /* 0x0068AFB0 */
int g_menuItemPositions[16] = {
    0x14, 0x5C, 0xA4, 0xC8, 0xEC,  /* DAT_00501BD4: 4-item layout (network hidden) */
    0x0C, 0x48, 0x84, 0xC0, 0xFC,  /* DAT_00501BE8: 5-item layout (network visible) */
    0, 0, 0, 0, 0, 0
};
int g_menuModelIndices[10] = {
    10, 5, 21, 21, 23,  /* DAT_00501BFC: Grand Prix, Time Attack, VS, Network, Options */
    160, 120, 0, 0, 0   /* remaining entries from binary */
};
int g_menuState;
int g_multiModePositions[2] = { 88, 168 };           /* 0x5025EC — from ROM */
int g_multiModeModelIndices[2] = { 22, 8 };           /* 0x5025F4 — from ROM */
int g_timeAttackMenuPositions[16] = { 20, 92, 164, 236 };  /* 0x5025CC — from ROM */
int g_timeAttackModelIndices[10] = { 6, 7, 8, 9 };  /* 0x5025DC — from ROM */
int g_nextScreenId;

/* Character Select — per-player confirm state */
int g_p1Confirmed;                      /* 0x0068AFB8 */
int g_p2Confirmed;                      /* 0x0068AFBC */
int g_p3Confirmed;                      /* 0x0068AFC0 */
int g_p4Confirmed;                      /* 0x0068AFC4 */
/* Character Select — per-player last selection (stride 4) */
int g_charSelectIndex[4];               /* 0x0068AF84 */
int g_charSelection[4];                 /* 0x0068AFEC */

/* The 0x92528C state block — 151 ints (0x92528C..0x9254E4). Menu screens,
 * track-object animation, device enumeration and screen plumbing all alias
 * into it; the map is in sonicr_globals.h. Track init clears the first 128
 * only, matching the loop at 0x472480. */
int g_stateBlock92528C[151];            /* 0x0092528C */

/* Character Select — ROM tables */
int g_charPositionTable[10] = {         /* 0x005025FC — cursor Y per character */
    10, 44, 78, 112, 146, 180, 214, 248, 282, 15
};
int g_charIconUV[18] = {                /* 0x005026A8 — UV pairs for character icons */
    150, 450,   /* char 0 (Sonic) — not used by loop, first icon uses 0xAC,scrollX */
    172, 80,    /* char 1 (Tails) */
    172, 120,   /* char 2 (Knuckles) */
    172, 160,   /* char 3 (Amy) */
    200, 0,     /* char 4 (Eggman) */
    200, 40,    /* char 5 (Metal Sonic) */
    200, 80,    /* char 6 (Tails Doll) */
    200, 120,   /* char 7 (Metal Knuckles) */
    200, 160,   /* char 8 (Egg Robo) */
};
/* 0x00502648 — one 26-int ROM region the binary indexes under TWO bases.
 *
 * g_charSelViewportPos is based at 0x502660, six ints INTO g_aiCharAssignment,
 * so ai[6..9] and vp[0..3] are the same memory. This used to be two separate C
 * arrays that happened to initialise those four ints identically — nothing
 * enforced it, and drift would have been silent.
 *
 * The overlap is LIVE, not theoretical. On a save with nothing unlocked the AI
 * assignment loop (screen_charsel.c, binary 0x48D02D) skips entries 0..4 as
 * locked and reads through 5,6,7,8,9 — so a first-time player's Grand Prix
 * genuinely reads vp[0..3] as character IDs. Conversely vp[0..3] is never read
 * AS viewport data: that index is numViewports*4 + p and numViewports is
 * always >= 1, so the viewport view effectively starts at vp[4].
 *
 * Access via the g_aiCharAssignment / g_charSelViewportPos macros in
 * sonicr_globals.h. Do not reintroduce a second definition of either. */
int g_charSelDataBlock[26] = {          /* 0x00502648 */
    /* ai[0..5] — every unlockable first, then the base characters */
    9, 7, 5, 6, 8, 0,
    /* ai[6..9] == vp[0..3] — the shared ints */
    2, 1, 4, 3,
    /* vp[4..7]   — 1 viewport: centered */
    0, 0, 0, 0,
    /* vp[8..11]  — 2 viewports: left/right */
    -220, 220, 0, 0,
    /* vp[12..15] — 3 viewports */
    -370, 0, 370, 0,
    /* vp[16..19] — 4 viewports */
    -450, -150, 150, 450,
};

/* Network */
int g_netSyncEstablished;
int g_networkAvailable = 1;             /* 0x00689B00 */

/* Previous camera positions (for weather particles) */
int g_prevPrevPosX;
int g_prevPrevPosY;
int g_prevPrevPosZ;

/* Track events */
int g_trackEventActive;

/* TrackSurfaceAI globals */
void *g_playerPtrTable;
/* Avoidance init values from DGROUP 0x4FA150/0x4FA154 = bias 48, counter 12
 * (audit 2026-07-13). Were 1/8 — AI swerve is aiSteerTarget = 0x80 ± bias,
 * so retail swerves ±48 around traffic; 1 was effectively no avoidance. */
int g_avoidanceCounter = 12;
int g_avoidanceBias = 48;
void *g_aiGridSurface;
int g_difficultyConfig;                 /* 0x008FD444 */
void *g_charJumpPowerTable;
void *g_charRampSpeedTable;
short g_rampSpeedTableA[10];            /* 0x00540078 — per-character ramp speed (BSS, currently zero) */
void *g_segmentOffsetTable;

/* AI race distance table — ROM at 0x4FA1C0, 10 chars × 16 bytes */
unsigned char g_charDistTable[160] = {
    0x10,0x30,0x18,0x3F,0x38,0x10,0x1E,0x14,0x28,0x28, 0,0,0,0,0,0,
    0x0E,0x10,0x18,0x3C,0x34,0x10,0x1E,0x14,0x28,0x28, 0,0,0,0,0,0,
    0x0F,0x2C,0x10,0x3E,0x36,0x10,0x1E,0x14,0x28,0x28, 0,0,0,0,0,0,
    0x08,0x18,0x1A,0x10,0x19,0x10,0x1E,0x14,0x28,0x28, 0,0,0,0,0,0,
    0x0D,0x14,0x12,0x16,0x10,0x10,0x1E,0x14,0x28,0x28, 0,0,0,0,0,0,
    0x10,0x30,0x18,0x3F,0x38,0x10,0x1E,0x14,0x28,0x28, 0,0,0,0,0,0,
    0x10,0x30,0x18,0x3F,0x38,0x10,0x10,0x14,0x28,0x28, 0,0,0,0,0,0,
    0x10,0x30,0x18,0x3F,0x38,0x10,0x1E,0x10,0x28,0x28, 0,0,0,0,0,0,
    0x10,0x30,0x18,0x3F,0x38,0x10,0x1E,0x14,0x10,0x28, 0,0,0,0,0,0,
    0x10,0x30,0x18,0x3F,0x38,0x10,0x1E,0x14,0x28,0x10, 0,0,0,0,0,0,
};

/* AdvanceTrackSegment rubber banding globals */
int g_rubberBandLowerBound;     /* 0x00540050 — set per-difficulty (-3, -4, -3, -2) */
int g_rubberBandDivisor;        /* 0x0054005c — set per-difficulty (0x50, 0x60, 0x50, 0x20) */
void *g_itemStateTable;         /* 0x006DA578 — 12-byte item/collectible state records */

/* g_factoryEmeraldBounceTimer1/2 (0x925324, 0x925334) are inside the
 * 0x92528C state block — aliased in sonicr_globals.h. */

/* Race / GP config */
int g_gpDifficultyLevel;        /* 0x006DD834 — GP difficulty (0=easy, 1=normal, 2=hard) */

/* Network game config — 0x0068A89C block (48 bytes each, src and dest) */
int g_netGameInfoDest[12];      /* 0x0068A89C — destination of network config copy */
short g_netTrackIndex;          /* 0x0068A8A0 — network track selection index */
short g_netRaceSubModeIndex;      /* 0x0068A8A2 — network lap config index (×3 = g_raceSubMode) */
int g_netWeatherType;           /* 0x0068A8A4 — network-synced weather type */
int g_netPlayerMode;            /* 0x0068A8A6 — network-synced player mode */
int g_netGameStartState;        /* 0x00501844 — set at race start (0x4CEA18) from bit 0 of
                                 * 0x68A8CA, the lobby MODE A/B bit: 1 = clear, 3 = set */
int g_trackIdTable[] = { 1, 2, 3, 4, 5 }; /* 0x00502754 — maps index 0-4 to track ID 1-5 */

/* Player mode / render config */
int g_timeOfDay;                /* 0x0094BCF8 — TOD_SUNRISE/TOD_DAY/TOD_SUNSET/TOD_NIGHT */
int g_emeraldRenderFlag;        /* 0x008FD488 — 1 = emerald special render mode */
int g_minimapConfig;            /* 0x008FD458 — options state (default 1) */
int g_minimapToggle;            /* 0x008FB818 — options toggle boolean */

/* Animation counters — emerald track */
int g_ringAnimFrame;        /* 0x00901C6C — emerald animation counter (mod 16) */
int g_emeraldAnimFrame1;        /* 0x00901C64 — emerald animation counter (mod 8) */

/* Clip/viewport derived */
int g_clipLeftDouble;           /* 0x006E98E8 — g_clipLeft * 2 */
int g_sortListOffset;           /* 0x008FB814 — zeroed at race start */

/* Viewport camBlock-derived parameters (set by FUN_004cc0e8 / SetViewportClipRect) */
int g_vpClipLeft10;             /* 0x006E98C4 — camBlock[0xB], clip left << 10 */
int g_vpClipRight10;            /* 0x006E98C8 — camBlock[0xC], (clipRight+1)*0x400-1 */
int g_vpClipLeft16;             /* 0x006E9900 — camBlock[0xD], clip left << 16 */
int g_vpClipRight16;            /* 0x006E9904 — camBlock[0xE], (clipRight+1)*0x10000-1 */
int g_vpParam0F;                /* 0x006E98F8 — camBlock[0xF] */
int g_vpParam10;                /* 0x006E98FC — camBlock[0x10] */

/* AI / track surface offset */
int g_aiGridAngleOffset;        /* 0x006EACAC — AI grid angular offset */

/* CD / music playback state */
int g_cdPlaybackState;          /* 0x006D9A40 — 0-3 state machine for CD audio */
int g_cdPlaybackTarget;         /* 0x006D9A3C — target CD track */

/* Network session sync */
int g_netFrameCounter;          /* 0x00689B58 — network frame counter */
int g_netSessionAlive;          /* 0x00689AF4 — 1 while network session is alive, 0 on timeout/disconnect */
int g_netSessionFrame;          /* 0x0068AEFC — current network session frame number (word in binary) */
int g_netPlayerRecvd[4];        /* 0x0068AEEC — per-player "data received this frame" flags (host-side) */
HANDLE g_netDataEvent;          /* 0x0068AC70 — network data processing sync event */
int g_gameDataPacketHeader;     /* 0x0068AEC8 — game data packet header (0xFF0000F0) */
unsigned short g_gameDataPacketFrame; /* 0x0068AECC — game data packet frame number */
unsigned short g_netRecvInput[8]; /* 0x0068AECE — received per-player input from host broadcast */
char g_lobbyPlayerData[4 * 0xC8]; /* 0x0068A900 — per-player lobby data, stride 0xC8 (4 players) */
int g_netExpectedPlayers;       /* 0x00689AF8 — expected player count (set at game start) */
int g_netLobbyDataCopy[12];    /* 0x0068AE88 — copy of lobby config data (48 bytes) */
char g_netSessionDesc[0x1CC];  /* 0x0068ACEC — session descriptor (broadcast at game start) */
int g_netDisconnectFlag;        /* 0x00689BB0 — network disconnect */
int g_netPlayerAlive[4];        /* 0x00689B9C — per-player keepalive flags, stride 4 */
int g_netReadyFlag;             /* 0x00689BAC — 1 if ready for race */
int g_netWaitFlag;              /* 0x006D9AB8 — 1 if waiting for network */

/* Race timing */
int g_raceTimerBase;            /* 0x006D9A34 — timeGetTime()/1000 at race start */
int g_raceStateCounter0;        /* 0x006D9A50 — zeroed at race start */
int g_raceStateCounter1;        /* 0x006D9A54 — zeroed at race start */
int g_introCountdownInit;       /* 0x006D9AD0 — set to 0xFFFFFFFF at race start */
int g_screenshotFlag;           /* 0x006D9A48 — debug screenshot capture flag */
int g_frameSpeedAdjust;         /* 0x008FB824 — frame speed multiplier */
int g_fpsDisplay;               /* 0x006E9D0C — FPS display value, set from g_currentFPS */
int g_fpsHistoryTable[8];       /* 0x006E9CD8+4*i — FPS sample history (7 entries at [1]-[7]) */

/* Sound options + demo state */
int g_musicEnabled;           /* 0x008FD4A0 — music on/off; derived from g_optMusicVolume */
int g_optMusicVolume;           /* music volume 0-8 (0 = off; default 8). Port addition —
                                 * the original Music Volume option is a toggle, so this
                                 * has no VMA. Everything that gates on music still reads
                                 * g_musicEnabled, which tracks (g_optMusicVolume != 0). */
int g_optSfxVolume;             /* 0x008FD49C — SFX volume, 0-8 (0 = off; default 8) */
int g_optSfxVolumeSave;         /* 0x006DA2A0 — g_optSfxVolume saved across a demo/replay */
int g_savedDemoMode;            /* 0x006D9AC0 — saved g_demoMode at race start */
int g_cdTrackTable[] = { 65535, 6, 7, 8, 9, 10 }; /* 0x004FBED8 — per-track CD music base index (ROM 3/4 swapped for runtime trackId) */
int g_cdTrackEmerald = 11;      /* 0x004FBEF0 — emerald/super sonic CD track */
int g_volumeBase = -2500;       /* 0x005041A8 — DirectSound volume base constant */
int g_raceElapsedSec;           /* 0x006D9A38 — abs(timeGetTime/1000 - raceTimerBase) */

/* Race loop state */
int g_splashPrevState;          /* 0x0068AF6C — previous splash screen toggle */
int g_netCharSelectState;       /* 0x0068AFD0 — network char select mode (0-4) */
char g_netPlayerDecorations[5 * 0x64]; /* 0x0068ACF8 — per-player decoration entries, stride 0x64 */
int g_triggerDepthRef;          /* 0x006DA31C — trigger object depth reference */
int g_raceElapsedDisplay;       /* 0x006E9CCC — elapsed race time for display */
int g_raceTimerDisplayBase;     /* 0x006E9CD0 — timer base for display computation */
int g_guideToggle;         /* 0x008FD454 — difficulty adjustment state (0-2) */
/* g_interlaceMode removed — alias for g_softDoubleBuf (0x008FD478) */
int g_introTimer;               /* 0x00901CC8 — intro countdown (60 frames, counts down) */

/* Palette/text buffer arrays — shared between palette copy and results screen */
int g_palArraySrc1[65];         /* 0x0068A5E4 — palette copy source 1 */
int g_resultsTextBuffer[65];    /* 0x00689BBC — results text buffer / palette copy source 2 */
int g_palArrayDst[65];          /* 0x00689CC0 — palette copy destination */

/* Network provider arrays — shared with portrait animation */
int g_netServiceProviders[16];  /* 0x0068A4E0 — available provider GUIDs */
int g_netFilteredProviders[16]; /* 0x0068A6EC — filtered providers / portrait anim counter */

/* Results screen state */
int g_resultsTextLineCount;     /* 0x0068AFD8 — number of text lines in results */
int g_resultsState;             /* 0x0068AFD4 — results screen state (0xa = press start) */
int g_resultsPlayerCount;       /* 0x0068AFDC — number of players in results list */
int g_resultsUnlockFlag;        /* 0x00689A70 — unlock notification flag */
int g_resultsPlayerData[650];   /* 0x00689BB8 — 10 slots × 0x104 bytes; slot 9 used as lobby scratch */
/* g_portraitPlayerCount removed — same as g_netPlayerCount (0x68AEE8) in globals.c */
int g_currentPlayerIdx;         /* 0x0068ACD8 — local player DPID from IDirectPlay4::CreatePlayer */
/* g_playerSlotIds (0x68AD4C) and g_playerCharIds (0x68AD54) REMOVED —
 * these are fields inside g_netPlayerDecorations at offsets 0x54 and 0x5C
 * with stride 0x64. Access via g_netPlayerDecorations + i*0x64 + offset. */
int g_portraitTextBuffer[16];   /* 0x0068A6F0 — current player name glyph buffer */

/* Post-race / results state */
int g_multiplayerWasActive;     /* 0x006D97E8 — set to g_netSessionActive or 1 after race */
int g_specialRacePlacement;     /* 0x006D9AAC — saved _DAT_008fd550 for special race */
int g_racePointsLaps[15];       /* 0x008FB64C — per-player lap times: 5 players × 3 laps */
int g_racePointsTotal[5];      /* 0x008FB67C — per-player total times */
int g_replayPointsLaps[15];     /* 0x008FB8C4 — replay data: 5 players × 3 laps */
int g_replayPointsTotal[5];    /* 0x008FB8D0 — replay data: per-player totals */
int g_replayLapCount;           /* 0x008FB8D4 — replay data: lap count */
int g_replayPlacement;          /* 0x008FB8D8 — replay data: placement */
int g_timeAttackResult;         /* 0x008FB958 — 0=none, 1=placed, 2=new record */
int g_ghostToggle;           /* 0x008FD448 — ghost on/off (default 1) */
int g_savedNumHumans;           /* 0x00925688 — saves g_numHumans for results */
/* g_charUnlockState is now a #define into g_saveBlock (save.c) — 0x8FBA8C */
int g_charUnlockSource[7];     /* 0x006D981C — character unlock source array */
int g_superSonicSeed;          /* 0x0068AF98 — persisted char-select Sonic/Super-Sonic choice (0=Sonic, 0x28=Super Sonic scroll pos). Seeds the scroll on entry; NOT a timer. */
int g_replayCharIds[5];        /* 0x008FB980 — sign-extended charIds per player slot (used by demo/replay) */
int g_dpLobbyObject;           /* 0x0068AC6C — DirectPlay lobby interface (no-op On SDL) */

/* Time attack record tables (in save data region), 100 entries each, indexed
 * charId + (trackId-1)*10 + lapConfig*50 — 2 lapConfigs × 5 tracks × 10 chars.
 * The extents are fixed by the neighbouring addresses: flags run 0x911A08 to
 * 0x911A6C (100 bytes), totals 0x911A6C to 0x911BFC (100 ints), laps 0x911BFC
 * to 0x911D8C (100 ints, ending at g_taGhostBuffer).
 * Which table is which comes from the binary: 0x4CFBDF stores the 3-lap sum
 * into 0x911A6C, while 0x4CFBF9/0x4CFC4F/0x4CFCA5 min-reduce laps 1-3 into
 * 0x911BFC. LoadAllGhostTimes seeds both from the .gho header in that order. */
unsigned char g_taRecordFlags[100];   /* 0x00911A08 — per-char/track/config record flags */
int g_taBestTotalTime[100];           /* 0x00911A6C — best total time per config */
int g_taBestLapTime[100];             /* 0x00911BFC — best single lap time per config */
unsigned short g_taGhostBuffer[0x1600];     /* 0x00911D8C — ghost replay save buffer (playback on restart); size mirrored as GHOST_BUFFER_FRAMES in sonicr_globals.h */
unsigned short g_taGhostSource[0x8000];     /* 0x0091498C — ghost replay recording buffer (0x8000 / numViewports per player) */

/* Race state arrays — cleared at race start / special race / GP advance.
 *
 * There is no array at 0x908E2C or 0x907B38. Those addresses are the clear
 * loops' first store, i.e. base + field offset of an array declared above:
 * 0x908E2C = g_footShadowCtrl (0x908E20) + 0xC, 0x907B38 = g_ringChaseArray
 * (0x907B20) + 0x18. They used to have their own C definitions here, which
 * meant the loops zeroed 2.3KB nothing else read while the real fields kept
 * their values across a race. See ClearRaceStateArrays in race_setup.c. */
CollectEffect __attribute__((aligned(32))) g_collectEffectBuf[COLLECT_EFFECT_COUNT];  /* 0x00907F20 — 64 entries × 60 bytes = 3840 */
intptr_t g_bounceStateArray[12]; /* 0x00907AF0 — 4 entries × 3 slots (type, targetPtr, dataPtr) */
int g_ringChaseArray[32 * 8];  /* 0x00907B20 — 32 ring chase particle entries × 32 bytes */

/* Side storage for ring-chase target pointers. The binary stores a 32-bit
 * pointer in g_ringChaseArray[i].entry[7] (binary [ebx + 0x1c]) using the
 * sign bit as a path flag (-ptr = path A, no validation; +ptr = path B,
 * validate target+0x80). On 64-bit hosts pointers don't fit in the 4-byte
 * int slot, so we shadow the real C pointer here indexed by chase slot,
 * and use entry[7] as a path flag only (-1 = path A, +1 = path B, 0 =
 * unused/empty). Same architectural pattern as g_frameStreamPtr[] in
 * animation.c:36. */
void *g_ringChaseTarget[32];   /* shadow for entry[7] target pointers */

/* Race state globals — cleared at race start */
/* 0x009020EC is g_inputStateEC */
int g_ringCollectPos[3];        /* 0x00901C8C — ring collect effect position (X,Y,Z << 8) */
int g_raceTimerB[3];            /* 0x00901C98 — secondary effect position (X,Y,Z), zeroed at race start */
int g_ringRespawnPos[3];        /* 0x00901CA4 — ring respawn effect position (X,Y,Z << 8) */
int g_effectPosItemBurst[3];    /* 0x00901CB0 — item collect burst effect position (X,Y,Z << 8) */
int g_raceCounter98;            /* 0x00902098 — zeroed at race start */
int g_raceCounter9c;            /* 0x0090209C — zeroed at race start */
int g_raceCounterA0;            /* 0x009020A0 — zeroed at race start */
int g_particleIdx;              /* 0x009020A4 — ring chase particle write index */
int g_raceCounterA8;            /* 0x009020A8 — missile slot counter, zeroed at race start */
/* g_tpageCharBase at 0x8F6C28 is already defined in globals.c */
int g_trackEventTimer;          /* 0x009118E8 — zeroed at race start */

/* =====================================================================
 * Championship scoring data (used by CalculateChampionshipPoints)
 * ===================================================================== */

/* Per-player "improved best" flags, cleared each call */
int g_gpBestChanged[4];           /* 0x006D97F0 — set by CompareAndUpdateBestA */
int g_gpBestChangedAlt[4];        /* 0x006D9800 — set by CompareAndUpdateBestB */
int g_gpBeatAllFlag;              /* 0x006D9810 — set to 1 when player beats all tracks */

/* Per-track championship best time tables (5 entries each, indexed by
 * trackId-1) are #defines into &g_saveBlock[23..58] in sonicr_globals.h —
 * binary 0x8FBAA8-0x8FBB34 lies inside g_saveBlock, which InitDefaultTimeTables
 * seeds through the g_lapTime* views of the same storage. */

/* Per-track GP progress (indexed by trackId) */
/* 0x008FBA74 lives in g_saveBlock (save.c). It was once named g_gpRelayFlag /
 * g_trackUnlockState; it is really g_charUnlockTable[4], Eggman's unlock flag.
 * See the retirement note in sonicr_globals.h. */

/* Per-player championship standings are a #define into &g_saveBlock[79] in
 * sonicr_globals.h — binary 0x8FBB88 lies inside g_saveBlock, so a standalone
 * array here would not share storage with the save-block view. */

/* g_gpCharTrackWins was formerly a standalone int[50] here at 0x008FBB94.
 * Same address-alias bug as g_gpPlayerStandings and g_gpCharRaceCount — binary
 * address 0x8FBB94 is inside g_saveBlock, so the GP win writes in race_setup.c
 * never reached the save block and the save-init clear never cleared what was
 * read. It was also one int short: the index is [charId * 5 + trackId] with
 * 1-based track ids, so Super Sonic (charId 9) on Radiant Emerald (track 5)
 * writes index 50. Now unified via sonicr_globals.h: g_gpCharTrackWins is a
 * #define into &g_saveBlock[82], [51]. */

/* g_gpCharRaceCount was formerly a standalone int[10] here at 0x008FBC60.
 * Same address-alias bug as g_gpCharDetail — binary address 0x8FBC60 is
 * inside g_saveBlock (same region as g_raceTimeTable). Now unified via
 * sonicr_globals.h: g_gpCharRaceCount is a #define into &g_saveBlock[133]. */

/* g_gpCharDetail was formerly a standalone int[410] here at 0x008FBC88.
 * That was an address-alias bug — the binary at 0x008FBC88 sits inside
 * g_saveBlock (one int past g_cpTableA), and save.c/ghost.c/hud_full.c
 * were writing/reading via g_cpTableA while race_setup.c/screen_misc.c
 * were writing/reading via this disjoint copy. Now unified via
 * sonicr_globals.h: g_gpCharDetail is a #define into &g_saveBlock[143]. */

/* GP result flag */
int g_gpResultFlag;               /* 0x006DA618 — 1 = all tracks + all chars unlocked */

/* 64-bit side-storage for anim data cursor pointers.
 * Binary stores a 4-byte pointer at player+0x9C; on 64-bit we use this
 * array indexed by playerIndex instead. Max 10 players (unlock screen). */
const short *g_animDataPtrs[10];

/* Unlock screen state — 0x6DA61C-0x6DA630 */
int g_creditsTpageSlots[4];        /* 0x006DA61C — tpage indices {2,3,4,5} */
int g_creditsStepCounter;          /* 0x006DA62C */
int g_creditsStateFlag;            /* 0x006DA630 */

/* Unlock screen font glyph tables — 0x6DA634
 * 3 blocks × 256 entries × 3 shorts {uvX, uvY, pixelWidth}
 * Indexed by ASCII code. -1 in uvX = unused entry. */
short g_creditsFontGlyphs[3][256][3]; /* 0x006DA634 */

int g_ghostCharIdPrev;          /* ghost replay: previous character ID */

int g_optCurrentPage;           /* 0x0068AFCC — current option menu page index */

/* Track polygon tpage arrays — filled by FUN_00470564 / FUN_00470580 */
unsigned char g_sceneryTpageA[34 * 0x30];  /* 0x00509668 — 34 entries, stride 0x30 */
unsigned char g_sceneryTpageB[14 * 0x30];  /* 0x00509CC8 — 14 entries, stride 0x30 */

/* Vertex lighting tables */
int g_lightingDepthTable[188 * 16] = {   /* 0x0050A294 — per-vertex depth values, stride 0x40 = 16 ints */
    4094, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, 820, 299, 0, 157,
    4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -479, 820, 299, 3, 161,
    4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, -219, 319, -11, -92,
    4094, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -479, 320, 319, 45, 99,
    4094, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -469, -149, 319, 94, -107,
    4093, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -549, -660, 299, 76, -213,
    4089, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -144, -665, 299, -12, -178,
    4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 260, -670, 299, 14, -181,
    4091, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240, -219, 319, -10, -137,
    4093, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1199, -79, 339, 0, 194,
    4091, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -759, -79, 339, 87, -61,
    4094, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -779, 340, 319, 8, 186,
    4091, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -779, 820, 299, 5, 167,
    4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 340, 380, 319, 28, 213,
    4090, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 380, 840, 299, 0, 177,
    4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 380, 319, 0, 202,
    4090, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1120, 840, 299, 0, 136,
    4093, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 720, 840, 299, 16, 150,
    4093, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 720, 240, 319, 94, 246,
    4087, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1379, -650, 299, -3, -282,
    4086, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -954, -655, 299, -3, -282,
    4086, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 700, 19, 339, 58, 121,
    4093, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 900, 140, 319, 117, 482,
    4065, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 680, -180, 319, -18, -245,
    4088, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 600, -339, 319, -12, -213,
    4090, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 960, -400, 319, -26, -159,
    4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 560, -620, 299, 16, -255,
    4087, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 0, 339, 0, 215,
    4090, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 260, 0, 339, -22, -99,
    4094, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 780, -540, 299, 16, -393,
    4077, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1020, 0, 339, -3, -68,
    4095, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1040, -200, 319, -11, -205,
    4090, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 320, -119, 319, -61, -367,
    4079, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -479, 820, -299, 3, 161,
    -4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, 820, -299, 0, 157,
    -4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, 300, -319, 3, 107,
    -4094, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -479, 320, -319, 45, 99,
    -4094, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, -219, -319, -11, -92,
    -4094, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -144, -665, -299, -12, -178,
    -4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -549, -660, -299, 76, -213,
    -4089, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -469, -149, -319, 94, -107,
    -4093, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240, -219, -319, -10, -137,
    -4093, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 260, -670, -299, 14, -181,
    -4091, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -779, 340, -319, 8, 186,
    -4091, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -759, -79, -339, 87, -61,
    -4094, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1199, -79, -339, 0, 194,
    -4091, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -779, 820, -299, 5, 167,
    -4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 380, -319, 0, 202,
    -4090, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 380, 840, -299, 0, 177,
    -4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 340, 380, -319, 28, 213,
    -4090, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 720, 240, -319, 94, 246,
    -4087, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 720, 840, -299, 16, 150,
    -4093, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1120, 840, -299, 0, 136,
    -4093, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -954, -655, -299, -3, -282,
    -4086, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1379, -650, -299, -3, -282,
    -4086, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 700, 19, -339, 58, 121,
    -4093, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 900, 140, -319, 117, 482,
    -4065, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 960, -400, -319, -26, -159,
    -4092, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 600, -339, -319, -12, -213,
    -4090, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 680, -180, -319, -18, -245,
    -4088, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 560, -620, -299, 16, -255,
    -4087, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 260, 0, -339, -22, -99,
    -4094, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 0, -339, 0, 215,
    -4090, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 780, -540, -299, 16, -393,
    -4077, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1020, 0, -339, -3, -68,
    -4095, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1040, -200, -319, -11, -205,
    -4090, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 320, -119, -319, -61, -367,
    -4079, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1680, -820, 100, -39, -3820,
    1477, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 380, -840, 100, 206, -3799,
    1515, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 260, -659, 300, 164, -2970,
    2815, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1359, -640, 299, -34, -3030,
    2755, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -109, -219, 100, 4042, -30,
    661, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -104, 259, 100, 4036, -18,
    695, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, 300, 319, 3859, -4,
    1370, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, -219, 319, 3882, -18,
    1304, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -100, 1019, 100, 4030, -6,
    729, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, 819, 299, 3835, 9,
    1436, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -980, 1019, 100, -2506, 2505,
    2052, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -779, 819, 299, -1694, 1693,
    3322, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, 819, 299, 0, 2884,
    2907, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -100, 1019, 100, 0, 3781,
    1575, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1540, -160, 100, 0, -4042,
    660, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -940, -160, 100, 0, -4042,
    660, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -759, -79, 339, 0, -3883,
    1303, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1199, -79, 339, 0, -3883,
    1303, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -980, 399, 100, -3483, 1500,
    1545, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1540, -160, 100, -2720, 2748,
    1349, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1199, -79, 339, -2241, 2293,
    2548, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -779, 340, 319, -2705, 1224,
    2821, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -940, -160, 100, -2715, 2992,
    667, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1680, -820, 100, -2715, 2992,
    667, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1359, -640, 299, -2632, 2848,
    1317, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -759, -79, 339, -2632, 2848,
    1317, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 500, 1059, 100, -1743, 3525,
    1145, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 380, 839, 299, -1665, 3020,
    2209, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1120, 839, 299, 32, 2792,
    2996, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1500, 1040, 100, 62, 3755,
    1633, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 939, 259, 100, 3132, -2197,
    1460, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1500, 1040, 100, 3132, -2197,
    1460, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1120, 839, 299, 2525, -1717,
    2729, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 720, 240, 319, 2525, -1717,
    2729, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 450, 100, -3836, 1411,
    255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 380, 319, -3814, 1400,
    512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 720, -800, 100, 870, -3659,
    1622, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 940, -720, 100, 1648, -3336,
    1711, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 780, -540, 299, 1179, -2396,
    3105, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 560, -619, 300, 653, -2738,
    2975, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1220, -540, 100, 2648, -2512,
    1856, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 960, -400, 319, 1780, -1661,
    3293, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1340, -60, 100, 3475, 1260,
    1762, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1240, 99, 100, 2542, 2923,
    1329, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 900, 140, 319, 2020, 2541,
    2497, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1020, 0, 339, 2389, 984,
    3178, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 939, 259, 100, 1873, 3487,
    1049, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 720, 240, 319, 1694, 3128,
    2029, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1360, -300, 100, 3518, -786,
    1943, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1040, -200, 319, 2265, -495,
    3376, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 320, -120, -319, -3198, 2558,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240, -219, -319, -1774, 3691,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240, -219, 319, -1774, 3691,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 320, -120, 319, -3198, 2558,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 0, 100, -4096, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 0, 339, -4096, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1680, -820, -99, -38, -3821,
    -1473, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1359, -639, -299, -33, -3036,
    -2749, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 260, -659, -299, 165, -2973,
    -2811, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 380, -840, -99, 207, -3800,
    -1513, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -104, 259, -99, 4037, -18,
    -690, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -109, -219, -99, 4042, -30,
    -656, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, -219, -319, 3885, -18,
    -1296, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, 300, -319, 3862, -4,
    -1361, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -100, 1019, -99, 4031, -6,
    -724, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, 820, -299, 3839, 9,
    -1426, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -980, 1019, -99, 0, 3784,
    -1567, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -100, 1019, -99, 0, 3784,
    -1567, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, 820, -299, 0, 2896,
    -2896, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -779, 820, -299, 0, 2896,
    -2896, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -940, -160, -99, 0, -4043,
    -656, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1540, -160, -99, 0, -4043,
    -656, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1199, -79, -339, 0, -3885,
    -1295, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -759, -79, -339, 0, -3885,
    -1295, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1540, -160, -99, -2722, 2749,
    -1343, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -980, 399, -99, -3486, 1500,
    -1538, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -779, 340, -319, -2715, 1228,
    -2810, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1199, -79, -339, -2247, 2299,
    -2538, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1680, -820, -99, -2716, 2993,
    -663, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -940, -160, -99, -2716, 2993,
    -663, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -759, -79, -339, -2634, 2850,
    -1309, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1359, -639, -299, -2634, 2850,
    -1309, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -980, 1019, -99, -3798, 27,
    -1531, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -779, 820, -299, -2949, 51,
    -2841, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 500, 1059, -99, -1742, 3527,
    -1139, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1500, 1040, -99, 62, 3758,
    -1626, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1120, 840, -299, 32, 2804,
    -2985, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 380, 840, -299, -1666, 3027,
    -2199, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1500, 1040, -99, 3134, -2199,
    -1453, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 940, 259, -99, 3134, -2199,
    -1453, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 720, 240, -319, 2534, -1723,
    -2717, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1120, 840, -299, 2534, -1723,
    -2717, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 450, -99, -3836, 1411,
    -253, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 380, -319, -3815, 1400,
    -508, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 940, -720, -99, 1647, -3339,
    -1705, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 720, -800, -99, 868, -3660,
    -1619, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 560, -620, -299, 650, -2743,
    -2971, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 780, -540, -299, 1179, -2406,
    -3097, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1220, -540, -99, 2651, -2514,
    -1850, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 960, -400, -319, 1789, -1668,
    -3284, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1240, 99, -99, 2543, 2925,
    -1323, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1340, -60, -99, 3478, 1261,
    -1756, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1020, 0, -339, 2399, 987,
    -3169, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 900, 140, -319, 2025, 2546,
    -2487, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 940, 259, -99, 1874, 3489,
    -1043, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 720, 240, -319, 1697, 3134,
    -2018, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1360, -300, -99, 3522, -786,
    -1936, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1040, -200, -319, 2276, -497,
    -3368, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 0, -99, -4096, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 0, -339, -4096, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, -219, -319, 0, 4096,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -179, -219, 319, 0, 4096,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 260, 0, 339, 0, -4096,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 0, 339, 0, -4096,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -19, 0, -339, 0, -4096,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 260, 0, -339, 0, -4096,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 260, 0, -339, -3663, -1831,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 320, -120, -319, -3663, -1831,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 320, -120, 319, -3663, -1831,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 260, 0, 339, -3663, -1831,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 0, 0, 0

};
/* 0x006725CC — per-vertex RGB for env-mapped model 0 (title "R", 188 verts).
  * Sized to match the binary. Trophy models 1..6 don't read this table —
  * SubmitEnvMapVertex uses a constant gray (0xffe0e0e0) for modelIdx != 0. */
int g_lightingRamp8bit[188 * 3];      /* 0x006725CC — 0-255 RGB, 188 entries */
/* Depth vertex color ramps */
int g_lightingRamp13bit[188*3];           /* 0x00671CFC — 13-bit fixed-point RGB, 188 entries */

/* Title screen animation state */
int g_pressStartColorR[4];                 /* 0x00507630 — interpolated R per corner */
int g_pressStartColorG[4];                 /* 0x00507640 — interpolated G per corner */
int g_pressStartColorB[4];                 /* 0x00507650 — interpolated B per corner */
int g_pressStartTargetR[4];                /* 0x00507660 — target R per corner (random) */
int g_pressStartTargetG[4];               /* 0x00507670 — target G per corner */
int g_pressStartTargetB[4];               /* 0x00507680 — target B per corner */
int g_pressStartAngle;                 /* title logo rotation seed angle */
int g_titleLogoAngleTarget = 0x800;  /* 0x00507694 — target track angle for
                                      * smooth pursuit; 0x800 is the ROM value */
int g_titleInputFlag;                /* input latch for title screen */
int g_titleLogoColorLatch;                 /* camera preset button latch */
int g_titleLogoColorPreset;                /* current camera preset index */
/* g_titleLogoEnabled is now a #define into g_saveBlock (save.c) — 0x8FBA78 */

/* Title screen character model indices — 5 bytes at 0x00507628 */
unsigned char g_titleCharIndices[5] = { 2, 3, 4, 5, 0 };

/* R logo color presets — 7 entries × 6 ints at 0x00507698.
 *
 * Base confirmed from the load at 0x4dd5be (add eax, 0x507698; stride
 * 0x18 from the imul at 0x4dd5bb). Entry 0 is (255,0,0 → 0,0,255) — the
 * same pair LoadGameState hands FillVertexColorsByDepth at 0x4CD98E, so
 * preset 0 IS the R's startup colour and the %7 cycle returns to it. */
int g_titleLogoColorPresets[7][6] = {
    { 255,   0,   0,   0,   0, 255 },
    { 255, 127,   0, 127,   0, 255 },
    { 255, 255,   0, 255,   0, 255 },
    {   0, 255,   0, 255,   0,   0 },
    {   0, 255, 255, 255, 127,   0 },
    { 127,   0, 255, 255, 255,   0 },
    { 255,   0, 255,   0, 255,   0 },
};

/* Title screen camera/rotation state at 0x008F7078 */
int g_titleLogoPosX;              /* 0x008F7078 */
int g_titleLogoPosY;              /* 0x008F707C */
int g_titleLogoPosZ;              /* 0x008F7080 */
int g_titleLogoRotYaw;               /* 0x008F7084 */
int g_titleLogoRotPitch;             /* 0x008F7088 */
int g_titleLogoRotRoll;              /* 0x008F708C */

/* CD playback state for title screen */
int g_cdPlaybackActive;          /* 0x006DA294 */

/* Batch vertex/index buffers DELETED (Phase 11) — ~913KB freed.
 * All rendering now uses immediate-mode R_DrawTri/R_DrawQuad. */
char g_tpageStateArray[52];        /* DAT_006260A4 — per-tpage status: 0x04 = texture ready */


/* Terrain collision sub-table pointers (set by ParseTerrainHeader / FUN_004d8904) */
void *g_terVertexTable;         /* 0x006DA55C — vertex data */
void *g_terFaceTable;           /* 0x006DA560 — face/edge data */
void *g_terCollisionMesh;       /* 0x006DA568 — collision mesh entries (16 bytes each) */
int g_terCollisionMeshSize;     /* size in bytes — computed from hdr[4] - hdr[3] */
void *g_terEdgeList;            /* 0x006DA56C — edge vertex list */
void *g_terLoopTable;          /* 0x006DA570 — loop/ride surface table: TerLoopEntry[g_terLoopCount] */
void *g_terUnknown74;           /* 0x006DA574 */
void *g_terGridIndex;           /* 0x006DA57C — grid cell → polygon list offset */
void *g_terGridData;            /* 0x006DA580 — polygon list data (short arrays) */
int g_terScalar5bc;             /* 0x006DA5BC */
int g_terScalar5c0;             /* 0x006DA5C0 */
int g_terScalar5c4;             /* 0x006DA5C4 */
int g_terScalar5c8;             /* 0x006DA5C8 */
int g_terScalar5cc;             /* 0x006DA5CC */
int g_terLoopCount;            /* 0x006DA5D0 — number of loop surface entries */
int g_terScalar5d4;             /* 0x006DA5D4 */
int g_terScalar5d8;             /* 0x006DA5D8 */
int g_terCollectibleCount;      /* 0x006DA5DC — number of collectible entries */

/* Parallax terrain shading (InitSoftwareTerrain / FUN_0046f6b4) */
short *g_terrainShadeBuf;           /* 0x008FB618 — fog-shaded parallax pixel buffer */
int g_terrainShadeValid;            /* 0x008FB61C — 1 if shade buffer allocated OK */
int g_parallaxWidthDouble;          /* 0x008FB624 — g_parallaxWidth * 2 */
float g_fogPerspTable[256];         /* 0x00674EEC — fog perspective table (1 per row) */

/* Display configuration */
int g_dispClipLeft;                 /* 0x006E9870 */
int g_dispClipTop;                  /* 0x006E9874 */
int g_dispClipRight;                /* 0x006E9878 */
int g_dispClipBottom;               /* 0x006E987C */
int g_dispHalfWidth;                /* 0x006E9880 */
int g_dispHalfHeight;               /* 0x006E9884 */
int g_dispCenterX;                  /* 0x006E98A0 — screenWidth / 2 */
int g_dispCenterY;                  /* 0x006E98A4 — screenHeight / 2 */
int g_resolutionLevel = 4;          /* 0x008FD484 — 0..4, default max */
int g_splitScreenMode;              /* 0x008FD45C — 0=horiz, 1=vert split */

/* Game options/config block — 0x8fd444 through 0x8fd4c0.
 * Initialized by InitInputMappings (FUN_00470cc0).
 * These are menu-configurable game settings. */
int g_weatherConfig;                   /* 0x008FD44C — init=0 (8 refs) */
int g_catchUpToggle;                   /* 0x008FD450 — init=1 (7 refs) */
int g_optResLow;                    /* 0x008FD460 — packed low-res 320×240 = 0x14000f0 */
int g_optResHigh;                   /* 0x008FD464 — packed high-res 640×480 = 0x28001e0 */
int g_optCfg_468;                   /* 0x008FD468 — not set by InitInputMappings (6 refs) */
int g_optCfg_46c;                   /* 0x008FD46C — init=0x10 (8 refs) */
int g_optCfg_470;                   /* 0x008FD470 — init=1 (10 refs) */
int g_softDoubleBuf;                /* 0x008FD478 — init=0 (247 refs!) display mode flag */
int g_optCfg_47c;                   /* 0x008FD47C — init=0 (1 ref) */
/* g_qualityLevel is 0x8fd480 — already defined below */
/* g_resolutionLevel is 0x8fd484 — already defined above */
/* g_optCfg_488 removed — alias for g_emeraldRenderFlag (0x008FD488) */
int g_optCfg_48c;                   /* 0x008FD48C — init=2 (1 ref) */
/* g_doubleWidthFlag is 0x8fd490 — already defined below */
int g_stereoEnabled;                /* 0x008FD494 — stereo/mono toggle, init 1 = stereo.
                                     * Menu + save only; no audio path reads it yet. */
int g_vocalsEnabled;                /* 0x008FD498 — vocals on/off toggle */
int g_optCfgRomData[8];             /* 0x008FD4A4 — ROM key/config data + sentinel */

/* Per-viewport camera/render state (stride 0xC8 per viewport, up to 4 viewports) */
int g_viewportConfigArray[4 * 50];  /* 0x006E9924 — 4 viewports × 0xC8 bytes = 200 ints */

/* Viewport config array (stride 0x54 per viewport, 6 slots)
 * Slots 0-3: race viewports (1P, 2P split).
 * Slots 4-5: menu/unlock screen viewports (0x8FB4B8, 0x8FB50C). */
int g_viewportArray[126];           /* 0x008FB368 — 6 viewports × 21 ints */

/* Viewport dimensions (computed by SetupViewportConfig from clip bounds) */
int g_viewportWidth;                /* 0x008FB38C — clipRight - clipLeft + 1 */
int g_viewportHeight;               /* 0x008FB390 — clipBottom - clipTop + 1 */
int g_doubleWidthFlag;              /* 0x008FD490 — non-zero for double-wide parallax (split screen) */

/* .data section constants (doubles in original EXE) */
sr_double g_fogStepConst = 192.0;   /* 0x0052C604 — fog row-step divisor */
sr_double g_fogAmplitude = 150.0;   /* 0x0052C60C — fog perspective numerator */
sr_double g_fogDistConst = 8.0;     /* 0x0052C63C — fog distance scale factor */

/* Object/animation linked list pointers */

/* Lap distance / checkpoint globals */
int g_lapDistFarClip;               /* 0x0090221C — per-track far clip distance */
int g_lapDistTrackLen;              /* 0x00902218 — total track length */
int g_lapDistRace;                  /* 0x00902220 — race distance threshold */
int g_lapDistRaceAlt;               /* 0x00902224 — alternate race distance */
int g_lapDistExtra;                 /* 0x00902478 */
int g_lapDistanceThreshold;         /* 0x008FB920 — current lap distance threshold */
int g_lapDistanceThreshold2;        /* 0x008FB934 — secondary threshold */
int g_checkpointA[10];              /* 0x008FB924 — per-checkpoint distance table A */
int g_checkpointB[10];              /* 0x008FB8F8 — per-checkpoint distance table B */

/* Far clip interpolation */
int g_clipFar;                      /* 0x008FB608 — far clip max */
int g_clipNear;                     /* 0x008FB610 — far clip min */
int g_qualityLevel;                 /* 0x008FD480 — 0..4 quality/resolution level */

/* Render state block cleared at start of each race */
int g_renderStateBlock[16];         /* 0x008F6F60 */
int g_tpageDirty;                   /* texture page dirty flag — cleared after InitLevel */

/* Race state counters (0x00901E area) — lap tracking / progress */
int g_raceState9C;                  /* 0x00901E9C */
int g_raceStateEBC;                 /* 0x00901EBC */
int g_raceStateEC0;                 /* 0x00901EC0 */
int g_raceStateEC4;                 /* 0x00901EC4 */
int g_gpCamBase[4];                 /* 0x00901ED0 — GP camera data (waypoint result at [0]) */
int g_camWaypointCount;             /* 0x00901EEC — waypoint count for camera FindNearestWaypoint */
CamStateEntry g_gpSmoothedCam;      /* 0x009020F0 — GP camera smoothing state (X,Y,Z,angles,rate) */
short g_savedCamPitch;              /* 0x009021E0 — saved from g_gpSmoothedCam by BuildChaseCamera */
short g_savedCamYaw;                /* 0x009021E2 */
int g_savedCamX;                    /* 0x009021E4 */
int g_savedCamY;                    /* 0x009021E8 */
int g_savedCamZ;                    /* 0x009021EC */
int *g_camWaypointTable;            /* 0x00902494 — waypoint table pointer for camera */
/* 0x00902078 is g_raceOrder[2], the per-frame 0/1 toggle written by
 * UpdateLapCounter (0x482028). It used to have a second C home here named
 * g_activeViewportIdx, which nothing wrote — so every reader through that name
 * saw a constant 0 and the shield expiry flicker and the TA ghost skip both
 * silently never fired. Read the address through g_raceOrder[2] only. */

unsigned short g_perPlayerInput[4]; /* 0x009020C8 — per-player input state words */
unsigned short g_introInputRaw;     /* 0x009020DA — raw input source for intro recording */
unsigned short g_introInputMasked;  /* 0x00902210 — masked intro input (current frame) */
unsigned short g_introInputPrev;    /* 0x00902212 — masked intro input (previous frame) */

/* Software renderer palette / blend tables */
unsigned char g_paletteRGB[1024];       /* 0x006D8FB8 — 256 entries × 4 bytes (R,G,B,pad) */
unsigned char g_rgb555Remap[32768];     /* 0x006D97B8 — RGB555→palette remap */
unsigned char *g_blendTablePtr;         /* 0x006D97CC — 65536-byte additive blend output */
unsigned char *g_fogBlendTablePtr;      /* 0x006D97BC — 131072-byte fog/lighting ramp */

/* Tpage surface colorization */
unsigned char *g_tpageSurfacePtrs[64];  /* 0x00625CC0 — tpage surface pointers */

/* Sprite submission */
int *g_depthListBase;                   /* 0x0051E070 */

/* Race state */
short g_configLapCount;                 /* 0x00901DE4 */
short g_lapCountMinusOne;               /* 0x0094AAC2 */
int g_raceTimerA;                       /* 0x00901DF4 */
int g_raceTimerE98;                     /* 0x00901E98 — was misnamed g_raceTimerB (conflicts with 0x901C98) */
int g_raceLimitA;                       /* 0x00901DF0 */
int g_raceLimitB;                       /* 0x00901E94 */
int g_raceMaskValue;                    /* 0x00901E88 */
int *g_gateWaypointPtr;                 /* 0x00901EF0 — gate waypoint data table */
char g_gateHistoryBuf[5 * 0x800];      /* 0x0090E3C0 — position history, stride 0x800 per player */
int g_raceCountdownA;                   /* 0x0090247C */
int g_raceCountdownB;                   /* 0x00902480 */
int g_playerFinishFlag[4];              /* 0x00901FF8 */
int g_splashDisableFlag;                /* 0x0090208C */

/* Lighting */
int g_lightingPhaseGlobal;              /* 0x0094D938 */
int g_lightingParam2;                   /* 0x0094D93C */
int g_weatherR;                         /* 0x0094D940 */
int g_weatherG;                         /* 0x0094D944 */
int g_weatherB;                         /* 0x0094D948 */

/* Collision / SFX cooldown */
int g_sfxCooldownTimer;                 /* 0x00901CC0 — shared SFX cooldown (collision, ramp) */

/* Viewport / splitscreen */
char *g_framebufferBase;                /* 0x006D98F8 */

/* Init state machine */
int g_initFeatureB;                     /* 0x006D9AE8 */
int g_initFeatureC;                     /* 0x00675C1C */

int g_difficultyParam;                  /* 0x008FB97C */
int g_playerCount;                      /* 0x008FB99C */

/* Flyover camera state — UNION with player score arrays (different game states).
 * Binary address 0x902008-0x902048 is shared: flyover during intro, scores during race.
 * Kept SEPARATE to prevent cross-contamination between game states. */
int g_flyoverNearest[5];                /* 0x00902008 */
int g_flyoverMode[5];                   /* 0x00902018 */
int g_flyoverParam[5];                  /* 0x00902028 */
int g_flyoverTimer[5];                  /* 0x00902038 */
int g_flyoverWpIdx[5];                  /* 0x00902048 */
int g_numWaypoints;                     /* 0x00901EE8 */

/* Camera */
CamStateEntry g_camStateTable[4];       /* 0x00902140 — 4 viewports × 40 bytes */
int *g_waypointTablePtr;                /* 0x00901EE4 */

/* ResetAllRaceState (0x471ec0) — race state zeroed at each race start.
 * Placeholder names until function semantics are identified. */
int g_unk_901C0C;                       /* 0x00901C0C */
int g_raceStateBlock10[8];              /* 0x00901C10 — 8 dwords zeroed in loop */
int g_unk_901C1C;                       /* 0x00901C1C — set to 0x400 at race start */
int g_unk_901C20;                       /* 0x00901C20 */
int g_unk_901C24;                       /* 0x00901C24 */
int g_unk_901C3C;                       /* 0x00901C3C */
int g_unk_901C40;                       /* 0x00901C40 */
int g_bouncePosition2;                  /* 0x00901C5C — slot-1 camera bounce pair */
int g_bounceVelocity2;                  /* 0x00901C60 */
int g_unk_901C74;                       /* 0x00901C74 */
int g_unk_901C7C;                       /* 0x00901C7C */
int g_unk_901CCC;                       /* 0x00901CCC */
int g_unk_901CE4;                       /* 0x00901CE4 */
int g_unk_901CE8;                       /* 0x00901CE8 */
int g_unk_901CEC;                       /* 0x00901CEC */
int g_unk_8F7074;                       /* 0x008F7074 — race_timing cycle counter */
int g_vpTimerA[12];                     /* 0x00901CF4 — per-viewport timing (3 vp × 4 entries) */
int g_vpTimerB[12];                     /* 0x00901D24 */
int g_vpTimerC[12];                     /* 0x00901D54 */
int g_vpTimerD[12];                     /* 0x00901D84 */
int g_vpTimerE[12];                     /* 0x00901DB4 */
int g_unk_901DE8;                       /* 0x00901DE8 */
int g_unk_901DEC;                       /* 0x00901DEC */
int g_raceTimerBlock[36];               /* 0x00901DF8 — 4 arrays × 9 entries, zeroed via loop */
int g_unk_901E8C;                       /* 0x00901E8C */
int g_unk_901E90;                       /* 0x00901E90 */
int g_unk_901EA0;                       /* 0x00901EA0 */
int g_unk_901EA4;                       /* 0x00901EA4 */
int g_unk_901EA8;                       /* 0x00901EA8 */
int g_unk_901EAC;                       /* 0x00901EAC */
int g_unk_901EB0;                       /* 0x00901EB0 */
int g_unk_901EB4;                       /* 0x00901EB4 */
int g_unk_901EB8;                       /* 0x00901EB8 */
int g_unk_901EC8;                       /* 0x00901EC8 */
int g_unk_901ECC;                       /* 0x00901ECC */
int g_unk_901FF4;                       /* 0x00901FF4 */
int g_unk_90205C;                       /* 0x00902058+4 */
int g_unk_902060;                       /* 0x00902060 */
int g_unk_902064;                       /* 0x00902064 */
int g_unk_902068;                       /* 0x00902068 */
int g_unk_90206C;                       /* 0x00902068+4 */
int g_unk_9020BC;                       /* 0x009020BC */
short g_inputStateDE;                   /* 0x009020DE */
short g_inputStateDC;                   /* 0x009020DC */
short g_inputStateDA;                   /* 0x009020DA — g_introInputRaw alias? */
short g_inputStateE0;                   /* 0x009020E0 */
short g_inputStateE2;                   /* 0x009020E2 */
short g_inputStateE4;                   /* 0x009020E4 */
short g_inputStateE6;                   /* 0x009020E6 */
short g_inputStateE8;                   /* 0x009020E8 */
short g_inputStateEA;                   /* 0x009020EA */
short g_inputStateEC;                   /* 0x009020EC */
short g_inputStateFC;                   /* 0x009020FC */
short g_inputStateFE;                   /* 0x009020FE */
short g_inputState100;                  /* 0x00902100 */
short g_inputState102;                  /* 0x00902102 */
short g_inputState104;                  /* 0x00902104 */
short g_inputState106;                  /* 0x00902106 */
short g_inputState124;                  /* 0x00902124 */
short g_inputState126;                  /* 0x00902126 */
short g_inputState128;                  /* 0x00902128 */
short g_inputState12A;                  /* 0x0090212A */
short g_inputState12C;                  /* 0x0090212C */
short g_inputState12E;                  /* 0x0090212E */
int g_unk_902118;                       /* 0x00902118 */
int g_unk_90211C;                       /* 0x0090211C */
int g_unk_902120;                       /* 0x00902120 */
short g_inputState1F0;                  /* 0x009021F0 */
short g_inputState1F2;                  /* 0x009021F2 */
int g_unk_9021F4;                       /* 0x009021F4 */
int g_unk_9021F8;                       /* 0x009021F8 */
int g_unk_9021FC;                       /* 0x009021FC */
int g_unk_902200;                       /* 0x00902200 */
int g_unk_902228;                       /* 0x00902228 */
int g_unk_902234;                       /* 0x00902234 */
int g_unk_90247C;                       /* 0x0090247C */
int g_unk_902480;                       /* 0x00902480 */
int g_unk_902484;                       /* 0x00902484 */
int g_unk_902488;                       /* 0x00902488 */
int g_unk_90248C;                       /* 0x0090248C */
int g_unk_902490;                       /* 0x00902490 */
/* 0x00902494 is g_camWaypointTable */
/* 0x00902498 is g_introSplineBase — was also declared as a standalone int here,
 * which split the storage: ResetAllRaceState cleared this int while the spline
 * loader and the two flyover-camera readers used the pointer, so the pointer
 * was never reset between races. */
/* 0x0090249C is g_trackBoundaryWaypoints */
int g_unk_9024A4;                       /* 0x009024A4 */
int g_unk_9024A8;                       /* 0x009024A8 */
int g_unk_8FB8B0[5];                    /* 0x008FB8B0 — 5 dwords zeroed at race start */
int g_unk_8FD4F0;                       /* 0x008FD4F0 — trail/viewport ptr, set to player[1] base */

/* Trail / afterimage */
int g_trailWriteA;                      /* 0x009020AC */
int g_trailWriteB;                      /* 0x009020B4 */
int g_trailCountA;                      /* 0x009020B0 */
int g_trailCountB;                      /* 0x009020B8 */
Player *g_trailSrcA;                    /* 0x008FD4E4 */
Player *g_trailSrcB;                    /* 0x008FD4E8 */
int g_trailBufA[20];                    /* 0x0090E320 — 5 entries × 4 ints (ends at 0x90E370) */
int g_trailBufB[20];                    /* 0x0090E370 — 5 entries × 4 ints (ends at 0x90E3C0) */

/* Engine sound / ghost sprite */
short g_ghostSpriteData[32768];         /* 0x00910BC4 */

/* Joystick/gamepad config structure at 0x675300 */
char g_joystickNameBuf[282];            /* 0x00675300 — template: name + config */
/* Originally 11 entries in the binary (one per button on a Microsoft
 * SideWinder pad). Extended here to 32 for modern controllers that
 * report more buttons via SDL_Joystick (e.g. Logitech Dual Action on
 * macOS reports 16 indices including stick clicks and split bumper/
 * trigger pairs). Entries beyond index 10 are zero-initialised by
 * default and only set if the active defaults or the in-game remap
 * UI assigns them. */
short g_joystickConfigWords[32];        /* 0x00675404 — extended button-bit map */
char g_joystickSlots[4][282];           /* 0x0067541A — 4 player device slots */
int g_joystickSlotActive[4];            /* 0x00675884 — per-slot active flags */
short g_joystickDeviceFlags[8];         /* 0x00675C40 — per-device config flags */
char g_joystickDeviceNames[4][260];     /* 0x00675C54 — detected device names, stride 0x104 */

/* Animation ROM data, descriptor table, collectible/weather globals,
 * and BuildGroundShadowQuad moved to track_anim_init.c */

/* Font glyph ROM data — UV coordinates for text rendering (DrawGlyphString) */
/* Indices: 0-25=A-Z, 26-35=0-9, 36-45=punctuation/special */
int g_romGlyphTable[46 * 4] = { /* 0x00501F84 — 16-byte entries (uvX, uvY, uvW, flags) */
       0,  218,    6,    1,       6,  218,    6,    1,
      12,  218,    5,    1,      17,  218,    6,    1,
      23,  218,    6,    1,      29,  218,    5,    1,
      34,  218,    6,    1,      40,  218,    6,    1,
      46,  218,    3,    1,      49,  218,    5,    1,
      54,  218,    6,    1,      60,  218,    5,    1,
      65,  218,    9,    1,      74,  218,    6,    1,
      80,  218,    6,    1,      86,  218,    6,    1,
      92,  218,    6,    1,      98,  218,    5,    1,
     103,  218,    6,    1,     109,  218,    5,    1,
     114,  218,    6,    1,     120,  218,    6,    1,
     126,  218,    9,    1,     135,  218,    6,    1,
     141,  218,    6,    1,     147,  218,    6,    1,
       0,  198,    6,    1,       6,  198,    5,    1,
      11,  198,    6,    1,      17,  198,    6,    1,
      23,  198,    6,    1,      29,  198,    6,    1,
      35,  198,    6,    1,      41,  198,    6,    0,
      47,  198,    6,    1,      53,  198,    6,    1,
     102,  188,    3,    0,     105,  188,    4,    1,
     109,  188,    3,    1,     152,  188,    4,    0,
     112,  188,    5,    1,     117,  188,    5,    1,
     122,  188,    5,    1,     127,  188,    4,    1,
     131,  188,    4,    1,     140,  188,    4,    1,
};

int g_romCharMap[46 * 3] = { /* 0x00502268 — 12-byte entries (uvX, uvY, uvW) */
       0,  208,    6,       6,  208,    6,
      12,  208,    5,      17,  208,    6,
      23,  208,    6,      29,  208,    6,
      35,  208,    6,      41,  208,    6,
      47,  208,    5,      52,  208,    5,
      57,  208,    6,      63,  208,    5,
      68,  208,    9,      77,  208,    7,
      84,  208,    6,      90,  208,    6,
      96,  208,    6,     102,  208,    6,
     108,  208,    6,     114,  208,    7,
     121,  208,    6,     127,  208,    6,
     133,  208,    9,     142,  208,    6,
     148,  208,    6,     154,  208,    6,
       0,  188,    4,       4,  188,    3,
       7,  188,    6,      13,  188,    6,
      19,  188,    6,      25,  188,    6,
      31,  188,    6,      -1,    0,    0,
      38,  188,    6,      44,  188,    4,
      -1,    0,    0,      48,  188,    6,
      54,  188,    6,      -1,    0,    0,
      60,  188,    5,      65,  188,    7,
      72,  188,    6,      78,  188,    5,
      83,  188,    5,      91,  188,    3,
};

/* UpdatePlayerLapSector globals — 0x4816E4 */
int *g_trackBoundaryWaypoints;      /* 0x0090249C — pointer to 12-int boundary array */
int g_finishOrderCounter;           /* 0x00901C80 */
int g_bestOverallLap;               /* 0x008FB8F4 */
/* g_bestCharLap at 0x008FB8F8 = g_checkpointB (same address, defined above) */

/* TimeRankingScreen persistent state — 0x0068AFxx range */
int g_rankViewH;                    /* 0x0068AFFC — ranking viewport height (set to 0x7B) */
int g_rankViewW;                    /* 0x0068B000 — ranking viewport width (set to 0x384) */
int g_rankPagePersist;              /* 0x0068AFA4 — persistent character page between visits */
int g_rankCursorPersist;            /* 0x0068AFA8 — persistent cursor slot between visits */

/* Menu sprite depth — shared by CharacterSelectScreen, TimeRankingScreen, etc. */
int g_menuDepthBucket;              /* 0x006D763C — depth bucket for D3D Blit2DSprite calls */

/* Shadow rendering globals */

/* InitRaceState globals at 0x4751E4 — use established names from leaf_batch.c.
 * Definitions are in leaf_batch.c (g_raceCounter, g_raceTimerA, g_playerRacePos, etc.) */

/* =====================================================================
 * NetworkScreen lobby state (0x48A8AC)
 *
 * Many addresses here are SHARED with the results screen — the binary
 * reuses the same .bss slots for whichever screen is active. We keep
 * the canonical results-screen names and #define aliases in the screen
 * function itself. New addresses that don't overlap go here.
 * ===================================================================== */
int g_netLobbyPlayerSlot;       /* 0x00689BB4 — incoming player slot counter (init 0xFF, then < 4) */
int g_netMenuState;             /* 0x00689AFC — network menu sub-state */
int g_netJoinedFlag;            /* 0x0068A6E4 — new player joined notification */
int g_netLobbyCounter;          /* 0x0068A898 — lobby tick counter */
int g_netReceivedLobbyData;     /* 0x0068A8FC — received lobby data flag */
int g_netSavedCharId;           /* 0x0068AFE0 — saved character ID across sessions */
int g_netSavedTrackIdx;         /* 0x0068AFE4 — saved track index across lobby visits */
int g_netSavedModeIdx;          /* 0x0068AFE8 — saved game mode index across lobby visits */
int g_netBroadcastCharId;       /* 0x0068ACE0 — last broadcast character ID */
int g_netSavedButtonByte;       /* 0x0068AFAC — save slot for the 16-bit field at 0x68A8CA:
                                 * saved at race start (0x4CEA41), restored on lobby entry
                                 * (0x48AB2B). Stored as a dword by the binary. */
int g_netLobbyConfigBuf[0x30/4]; /* 0x0068A8CC — received lobby config (0x30 bytes) */
int g_netLobbyPortrait[5 * (0x48/4)]; /* 0x0068A730 — per-player portrait data, stride 0x48, 5 slots */
int g_netLobbyCharData[17];     /* 0x0068A854 — character data for lobby (DPID + 16 ints) */
int g_netSoundIndexBuf[287];    /* 0x0068A3D8 — lobby sound index buffer (sentinel-terminated, 1148 bytes to 0x68A854) */

/* NetworkScreen's own podium player. 0x008FF880 is a full Player struct, not a
 * script buffer — the screen animates this rather than the racing player, which
 * is why the podium model must not be fed g_playerBase. Every "menu global"
 * that used to live here was one of its fields; they are now #define aliases in
 * sonicr_globals.h so the offsets can't drift apart again. */
Player g_menuPlayer;            /* 0x008FF880 */

/* Frame timing for network/results screens */
int g_screenBaseTime;           /* 0x006E9D00 — initial timeGetTime() at screen entry */
int g_screenFPS;                /* 0x006E9CDC — computed FPS for frame cap */

/* Character model ROM tables (read-only, extracted from PE) */
/* 0x501898: per-character config pointers (indexed by charId, stride 4) — declared extern where used */
/* 0x50272C: character model index table (indexed by slot, stride 4) — declared extern where used */
/* 0x502754: character tpage override table — declared extern where used */
/* 0x4FBDF8: menu script data table (stride 8) — declared extern where used */
/* 0x5025C4: 2-int local array (character model pair) — extracted inline */
/* 0x5024C4: default sound indices (21 entries + sentinel) — declared in leaf_small.c */
/* 0x502598: service provider table — declared in leaf_batch.c */


/* Misc screen globals */
/* g_netProviderChoice is g_stateBlock92528C[0] — aliased in sonicr_globals.h */
int g_netEnumActive;            /* 0x00689B54 — session enumeration active flag */

/* Per-joystick-button pressed state at 0x00675B0C.
 * Layout: byte at index [slot * 80 + buttonIdx]. Up to 4 slots × 80 buttons.
 * Byte value 0x80 = pressed, 0x00 = released — same convention as
 * g_diKeyboardState so scan loops can treat keyboard and joystick state
 * identically. Written each frame by platform_poll_gamepads, read by
 * PollAllInputDevices (button → bit-pattern OR) and ScanKeyRemap. */
unsigned char g_keyPressState[320];

/* DC viewport-edge scissor: SCISSOR_NONE in single-player / full-screen
 * (PVR handles off-screen culling at tile boundaries for free); set to
 * the inward-facing edge of the active viewport in split-screen modes
 * by the per-viewport setup code. See nearclip.h ScissorEdge. */
int g_scissorEdge;
/* Clip specification for the CPU scissor paths — see nearclip.h.
 * Separate from g_scissorEdge, which also serves as the split-screen flag. */
int g_cpuClipEdge;

/* Original (single-player) far-plane value for the active frame, captured
 * by hud_full.c per-viewport BEFORE the split-screen halving step. The
 * grid LOD quad reads this so it can extend out to the unmodified far
 * plane even though per-tile rendering uses the halved value. */
float g_farClipFullScreenF;
