/**
 * track_anim_init.c — Track animation initialization
 *
 * Per-track scenery animation setup, animated polygon registration,
 * and object visibility configuration.
 *
 * Functions:
 *   ResetAnimCounters     — 0x0047ED20 — 77 bytes
 *   InitTrackAnimObjects  — 0x0047FA64 — 149 bytes (dispatcher)
 *   InitAnimIsland        — 0x0047CA74 — 808 bytes
 *   InitAnimCity          — 0x0047BA80 — 1026 bytes
 *   InitAnimFactory       — 0x00479FFC — 786 bytes
 *   InitAnimRuin          — 0x00479810 — 877 bytes
 *   InitAnimEmerald       — 0x00479498 — 616 bytes
 *   RegisterAnimPolygons  — 0x0047CDEC — 420 bytes
 *   RegisterAnimatedGeometry — 0x0047CF90 — 2180 bytes
 *   RegisterQuadPolygons  — 0x0047D9B8 — 1105 bytes
 *   RegisterQuadBlock     — 0x0047D814 — 420 bytes
 *   InitObjectVisibility  — 0x0047E54C — 2001 bytes
 *   InitCollectibles_SP   — 0x00478F60 — 253 bytes
 *   InitItems_SP          — 0x00479060 — 218 bytes
 *   InitItems_MP          — 0x0047913C — 265 bytes
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include <math.h>

/* Animation ROM data + descriptor table */
static const int s_animRomData[233] = { /* 0x4FF204 */
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x00,0x01,0x02,0x03,0x04,0x05,0x04,0x03,
    0x02,0x01,0x00,0x00,0x01,0x02,0x03,0x02,
    0x01,0x00,0x00,0x01,0x02,0x03,0x03,0x02,
    0x01,0x00,0x00,0x01,0x02,0x03,0x04,0x05,
    0x06,0x07,0x06,0x05,0x04,0x03,0x02,0x01,
    0x00,0x800000,0x400000,0x800000,0x600000,0x800000,0x800000,0x800000,
    0xA00000,0x800000,0x800000,0x800000,0xA00000,0x800000,0xC00000,0x800000,
    0xE00000,0xC00000,0x800000,0xC00000,0xA00000,0xC00000,0xC00000,0xC00000,
    0xE00000,0x000000,0x000000,0x000000,0x400000,0x000000,0x800000,0x000000,
    0xC00000,0x400000,0x000000,0x400000,0x400000,0x400000,0x800000,0x400000,
    0xC00000,0x000000,0x400000,0x000000,0x600000,0x000000,0x800000,0x000000,
    0xA00000,0x000000,0xC00000,0x000000,0xE00000,0x800000,0x400000,0x800000,
    0x600000,0x800000,0x800000,0x800000,0xA00000,0xD00000,0x400000,0xD00000,
    0x700000,0xD00000,0xA00000,0xD00000,0xD00000,0x200000,0x400000,0x200000,
    0x600000,0x200000,0x800000,0x200000,0xA00000,0x200000,0xC00000,0x200000,
    0xE00000,0x400000,0x000000,0x400000,0x280000,0x400000,0x500000,0x400000,
    0x780000,0x600000,0x000000,0x600000,0x280000,0x600000,0x500000,0x600000,
    0x780000,0x000000,0x000000,0x000000,0x400000,0x000000,0x800000,0x000000,
    0xC00000,0x400000,0x000000,0x400000,0x400000,0x400000,0x800000,0x400000,
    0xC00000,0x800000,0x000000,0x800000,0x400000,0x800000,0x800000,0x800000,
    0xC00000,0xC00000,0x000000,0xC00000,0x400000,0xC00000,0x800000,0xC00000,
    0xC00000,0x800000,0x400000,0x800000,0x800000,0xC00000,0x400000,0xC00000,
    0x800000,0xE00000,0x400000,0xE00000,0x800000,0x000000,0xC00000,0x200000,
    0xC00000,0x400000,0xC00000,0x600000,0xC00000,0x800000,0xC00000,0xA00000,
    0xC00000,0xC00000,0xC00000,0xE00000,0xC00000,0x000000,0x800000,0x000000,
    0xC00000,0x400000,0x800000,0x400000,0xC00000,0x000000,0x000000,0xC00000,
    0x400000,0xC00000,0x800000,0xC00000,0xC00000,0x900000,0xC00000,0x900000,
    0xD80000,
};

#define AROM(va) (&s_animRomData[((va) - 0x4FF204) / 4])

intptr_t g_animDescTable[12 * 10] = { /* 0x004FF5A8 */
    0, 8, 0, 1, (intptr_t)AROM(0x4FF28C), (intptr_t)AROM(0x4FF2E8), 5, 0, 0, 0,
    0, 8, 0, 1, (intptr_t)AROM(0x4FF204), (intptr_t)AROM(0x4FF308), 0, 0, 0, 0,
    0, 8, 1, 1, (intptr_t)AROM(0x4FF204), (intptr_t)AROM(0x4FF348), 0xC, 0, 0, 0,
    0, 0xA, 0, 1, (intptr_t)AROM(0x4FF244), (intptr_t)AROM(0x4FF388), 8, 0, 0, 0,
    0, 6, 0, 1, (intptr_t)AROM(0x4FF270), (intptr_t)AROM(0x4FF3B8), 8, 0, 0, 0,
    0, 6, 0, 1, (intptr_t)AROM(0x4FF270), (intptr_t)AROM(0x4FF3D8), 8, 0, 0, 0,
    0, 0xA, 1, 1, (intptr_t)AROM(0x4FF244), (intptr_t)AROM(0x4FF3F8), 8, 0, 0, 0,
    0, 8, 1, 0, (intptr_t)AROM(0x4FF204), (intptr_t)AROM(0x4FF428), 4, 0, 0, 0,
    0, 0x10, 1, 0, (intptr_t)AROM(0x4FF204), (intptr_t)AROM(0x4FF468), 0xE, 0, 0, 0,
    0, 2, 0, 4, (intptr_t)AROM(0x4FF204), (intptr_t)AROM(0x4FF4E8), 2, 0, 0, 0,
    0, 8, 0, 1, (intptr_t)AROM(0x4FF28C), (intptr_t)AROM(0x4FF4F8), 2, 0, 0, 0,
    0, 0xE, 0, 1, (intptr_t)AROM(0x4FF2AC), (intptr_t)AROM(0x4FF518), 3, 0, 0, 0,
};

int g_factoryObjState[46];                     /* 0x0068158C */
int g_factoryObjAngle[46];                     /* 0x00681644 — per-object heading angles */
/* g_randomTable is same memory as g_randomRingBuffer (0x0092498C, globals_extra.c).
 * g_randomRingBuffer[513] covers first 1026 bytes; g_randomTable[16384] = 32768 bytes total.
 * Use g_randomRingBuffer for the canonical 513-short ring; g_randomTable is the full 16K table. */
extern unsigned short g_randomRingBuffer[];  /* canonical: globals_extra.c */
#define g_randomTable g_randomRingBuffer
/* g_randomStream is same address as g_ringSpawnReadPtr (0x00901CD0, globals_extra.c).
 * #define g_randomStream g_ringSpawnReadPtr is in sonicr_globals.h. */
/* g_tpageParticle1/2 are the low byte of g_tpageCharBase/g_tpagePlayfield2 (same address).
 * Declared as aliases — see sonicr_globals.h. */

/* Animation state at 0x9252C8-0x92533C is inside the 0x92528C state block —
 * declared as aliases into g_stateBlock92528C in sonicr_globals.h, so the
 * track-init clear that zeroes the block reaches them the way it does in the
 * binary. Only the storage past the block's end lives here. */

/* City sign physics — two contiguous 10-int blocks (0x92548C-0x9254D8), part
 * of the 0x92528C state block above the track-init clear. Block A (sign type
 * 2) is object 1005's falling sign, block B (sign type != 2) is object 892's.
 * Aliased in sonicr_globals.h. */

int g_animRegCount;             /* 0x00676FE8 */
int g_animRegGeomCount;        /* 0x00676FEC */
intptr_t g_animRegTable[1024 * 10]; /* 0x00676FF0 — 40 bytes per entry (10 ints on 32-bit) */

/* Water animation control structs — 3 structs, each 5 ints (20 bytes).
 * Field layout per struct:
 *   +0x00 = start index in g_animRegTable (written by RegisterAnimatedGeometry)
 *   +0x04 = registered count                (written by RegisterAnimatedGeometry)
 *   +0x08 = start index in g_animRegTable (written by RegisterAnimatedQuadrants)
 *   +0x0C = quad-registered count           (written by RegisterAnimatedQuadrants/RegisterQuadBlock)
 *   +0x10 = reserved / pre-cleared by every InitAnim* before the Register calls
 *
 * Struct bases: 0x680FF0 (ctrl0), 0x681004 (ctrl1), 0x681018 (ctrl2) — stride 0x14 (20 bytes).
 * `g_animCtrl000/014/028` are named after the ABSOLUTE addresses of each struct's +0x10
 * field (the one InitAnim* clears), which fall at 0x681000, 0x681014, 0x681028. */
int g_geometryAnimCtrl[3 * 5];     /* 0x00680FF0 — 3 structs × 5 ints */
#define g_animCtrl000 g_geometryAnimCtrl[4]    /* struct 0 (base 0x680FF0) +0x10 field → 0x681000 */
#define g_animCtrl014 g_geometryAnimCtrl[9]    /* struct 1 (base 0x681004) +0x10 field → 0x681014 */
#define g_animCtrl028 g_geometryAnimCtrl[14]   /* struct 2 (base 0x681018) +0x10 field → 0x681028 */

int g_trackAnimTickRate;        /* 0x00902058 */

/* Radical City slot-machine reel state — backing storage is in camera_per_track.c
 * (see comment on the s_reelPoly[] block at the binary's 0x6816FC address).
 * InitAnimCity writes into this block per the binary 0x0047bc46..0x0047bcf3. */
extern int *s_reelPoly[3];         /* 0x6816FC..0x681707 — billboard poly ptrs */
extern int  s_reelPos[3];          /* 0x681708..0x681713 — init to 0x400000 */
extern int  s_reelVelocity[3];     /* 0x681720..0x68172B — init to 0 */
extern int  s_reelCounter;         /* 0x68172C — init to 0 */
extern int  s_reelState;           /* 0x681730 — init to 0 */

/* Per-track object hide lists — extracted from ROM at 0x5009F0+ */
/* These are -1-terminated arrays of object indices.
 * Each index * 0x44 + 0x2C = offset into g_objectStructArray to set to 0xFFFF. */

/* Time-attack hide lists (raceType==2): hide collectible tokens */
/* Extracted from ROM at 0x500800+ */
static const int s_taHideIsland[]  = { 0x25E,0x25F,0x1F6,0x25C,0x25D,0x1F2,0x259,0x1F4, -1 };
static const int s_taHideCity[]    = { 0x3F7,0x3F6,0x3F5,0x3F4,0x382,0x380,0x381,0x3E0,0x3DF,0x2C3, -1 };
static const int s_taHideRuin[]    = { 0x30B,0x3D5,0x3D6,0x3D7,0x3D8,0x309,0x30D,0x30A,0x3D3,0x3D4, -1 };
static const int s_taHideFactory[] = { 0x30F,0x3DB,0x3DC,0x3DD,0x3DE,0x327,0x328,0x30C,0x3D5,0x3D6, -1 };
static const int s_taHideEmerald[] = { -1 };

static const int *s_taHideLists[] = {
    NULL, s_taHideIsland, s_taHideCity, s_taHideFactory, s_taHideRuin, s_taHideEmerald
};

/* Character-unlock token hide lists — ROM table at 0x500A04.
 *
 * MISLEADING NAME: "gp" in the symbol is a legacy label. This table is
 * actually consulted from TWO race-type branches in InitObjectVisibility:
 *
 *   g_raceType == RACE_MULTIPLAYER (Multiplayer)  — hides the FULL list unconditionally
 *                                    (binary: 0x47E586-0x47E594)
 *   g_raceType == RACE_GP (Grand Prix)   — hides the FIRST 5 entries (the 5
 *                                    Sonic Tokens) when this track's
 *                                    token-challenge rival is already
 *                                    unlocked — see s_tokenRivalChar
 *                                    (binary: 0x47E603-0x47E62A)
 *
 * Content-wise, each list is a strict subset of the matching s_taHide* list
 * (the tokens being hidden are character-unlock pickups). Kept as s_gp*
 * for historical continuity; do NOT assume GP-only from the name.
 * Extracted from ROM at 0x5008AC+.
 */
static const int s_gpHideIsland[]  = { 0x25E,0x25F,0x1F6,0x25C,0x25D,0x1F2, -1 };
static const int s_gpHideCity[]    = { 0x3F7,0x3F6,0x3F5,0x3F4,0x382,0x380,0x381, -1 };
static const int s_gpHideRuin[]    = { 0x30B,0x3D5,0x3D6,0x3D7,0x3D8,0x309,0x30D, -1 };
static const int s_gpHideFactory[] = { 0x30F,0x3DB,0x3DC,0x3DD,0x3DE,0x327,0x328, -1 };
static const int s_gpHideEmerald[] = { -1 };

static const int *s_gpHideLists[] = {
    NULL, s_gpHideIsland, s_gpHideCity, s_gpHideFactory, s_gpHideRuin, s_gpHideEmerald
};

/* Token-challenge rival per track — the same characters SetupSpecialRace
 * picks at 0x471AC4-0x471AF3.
 *
 * The binary gates the 5-token hide on `[g_trackId*4 + 0x8FBA74]` (0x47E621),
 * and 0x8FBA74 is g_charUnlockTable[4], so that slot is really
 * g_charUnlockTable[4 + binary trackId] — the unlock state of the character
 * you race for those tokens. Our g_trackId has Ruin/Factory swapped, so index
 * by CHARACTER instead of by trackId; that also keeps this in lockstep with
 * SetupSpecialRace, which is the only writer of the matching slot (via
 * AdvanceGrandPrixTrack's `g_gpTrackStatus[6 + rivalCharId] = 2`).
 * Entries [0] and [5] are never reached — both readers gate on trackId 1-4. */
static const int s_tokenRivalChar[] = {
    -1, CHAR_METAL_SONIC, CHAR_TAILS_DOLL, CHAR_EGG_ROBO,
    CHAR_METAL_KNUCKLES, -1
};

/* Extracted from ROM at 0x50092C+ */
static const int s_alwaysHideIsland[]  = { 0x223,0x21F,0x21D,0x21E,0x221,0x225,0x224,0x220,0x222, -1 };
static const int s_alwaysHideCity[]    = { 0x3D5,0x3D6,0x3D7,0x3D8,0x3D9,0x3DA,0x3DB,0x3DC,0x3DD, -1 };
static const int s_alwaysHideRuin[]    = { 0x346,0x347,0x348,0x349,0x34A,0x34B,0x34C,0x34D,0x34E, -1 };
static const int s_alwaysHideFactory[] = { 0x3CC,0x3CD,0x3CE,0x3CF,0x3D0,0x3D1,0x3D2,0x3D3,0x3D4, -1 };
static const int s_alwaysHideEmerald[] = { 0x36F,0x370,0x371,0x372,0x373,0x374,0x375,0x376,0x378, -1 };

static const int *s_alwaysHideLists[] = {
    NULL, s_alwaysHideIsland, s_alwaysHideCity, s_alwaysHideFactory,
    s_alwaysHideRuin, s_alwaysHideEmerald
};

/* Per-track pickup ground-shadow setup.
 * Each entry: object position offsets (X,Y,Z in g_objectStructArray),
 * quad buffer index, shadow half-size.
 * halfSize identifies the pickup: 0x28 = Chaos Emerald, 0x1E = Sonic Token
 * (five per track), 0x2D = item pickup.
 * Calls BuildGroundShadowQuad to compute the 4 ground-conformed corners. */
typedef struct { int xOff, yOff, zOff; int bufIdx; int halfSize; } PickupShadowInit;

static const PickupShadowInit s_pickupShadowInitIsland[] = {
    {0x8468,0x846C,0x8470, 5, 0x28}, {0xA118,0xA11C,0xA120, 0, 0x1E},
    {0xA15C,0xA160,0xA164, 1, 0x1E}, {0x8578,0x857C,0x8580, 2, 0x1E},
    {0xA090,0xA094,0xA098, 3, 0x1E}, {0xA0D4,0xA0D8,0xA0DC, 4, 0x1E},
    {0x9FC4,0x9FC8,0x9FCC, 7, 0x2D}, {0x84F0,0x84F4,0x84F8, 8, 0x2D},
    {-1,0,0,0,0}
};
static const PickupShadowInit s_pickupShadowInitCity[] = {
    {0xEE20,0xEE24,0xEE28, 5, 0x28}, {0xEE64,0xEE68,0xEE6C, 6, 0x28},
    {0x10DBC,0x10DC0,0x10DC4, 0, 0x1E}, {0x10D78,0x10D7C,0x10D80, 1, 0x1E},
    {0x10D34,0x10D38,0x10D3C, 2, 0x1E}, {0x10CF0,0x10CF4,0x10CF8, 3, 0x1E},
    {0xEEA8,0xEEAC,0xEEB0, 4, 0x1E}, {0x107A0,0x107A4,0x107A8, 7, 0x2D},
    {0x1075C,0x10760,0x10764, 8, 0x2D}, {0xBBEC,0xBBF0,0xBBF4, 9, 0x2D},
    {-1,0,0,0,0}
};
static const PickupShadowInit s_pickupShadowInitRuin[] = {
    {0xD67C,0xD680,0xD684, 5, 0x28}, {0xD6C0,0xD6C4,0xD6C8, 6, 0x28},
    {0xD01C,0xD020,0xD024, 0, 0x1E}, {0x1064C,0x10650,0x10654, 1, 0x1E},
    {0x10690,0x10694,0x10698, 2, 0x1E}, {0x106D4,0x106D8,0x106DC, 3, 0x1E},
    {0x10718,0x1071C,0x10720, 4, 0x1E}, {0xCF50,0xCF54,0xCF58, 7, 0x2D},
    {0x104B4,0x104B8,0x104BC, 8, 0x2D}, {0x104F8,0x104FC,0x10500, 9, 0x2D},
    {-1,0,0,0,0}
};
static const PickupShadowInit s_pickupShadowInitFactory[] = {
    {0xCE84,0xCE88,0xCE8C, 5, 0x28}, {0xCF94,0xCF98,0xCF9C, 6, 0x28},
    {0xCF0C,0xCF10,0xCF14, 0, 0x1E}, {0x104B4,0x104B8,0x104BC, 1, 0x1E},
    {0x104F8,0x104FC,0x10500, 2, 0x1E}, {0x1053C,0x10540,0x10544, 3, 0x1E},
    {0x10580,0x10584,0x10588, 4, 0x1E}, {0xCEC8,0xCECC,0xCED0, 7, 0x2D},
    {0x1042C,0x10430,0x10434, 8, 0x2D}, {0x10470,0x10474,0x10478, 9, 0x2D},
    {-1,0,0,0,0}
};

static const PickupShadowInit *s_pickupShadowInitTables[] = {
    NULL, s_pickupShadowInitIsland, s_pickupShadowInitCity, s_pickupShadowInitRuin, s_pickupShadowInitFactory, NULL
};

extern void BuildGroundShadowQuad(int worldX, int worldY, int worldZ,
                                   int *out, int halfSize);
extern int g_pickupShadowVerts[];

/* Per-track collectible position tables — extracted from ROM at 0x4FF924 */
/* Each entry is 3 ints: X, Y, Z */
extern int QueryTerrainHeight(int worldX, int worldZ, int minY);

/* Collectible billboard vertex buffers — 17 collectibles × 12 ints each.
 * In the original, these lived at BSS 0x68120C with stride 0x30 (48 bytes = 12 ints).
 * Each buffer holds 4 vertices × (X, Y, Z). */
static int s_collectVertexBuf[17 * 12];
/* Side table for 64-bit-safe vertex buffer pointers (avoids 32-bit truncation in struct) */
int *g_collectVertexPtrs[17];

/* Extracted from ROM at 0x4FF924 — 5 tracks × 17 entries × 3 ints (X, Y, Z).
 * Swap-family table (like 0x4FEB00): the binary indexes it by the runtime g_trackId
 * (0x8FB8EC), but the Regal Ruin / Reactive Factory blocks are stored swapped in ROM —
 * the slot g_trackId=3 (Ruin) selects physically holds Factory's positions, and the
 * g_trackId=4 (Factory) slot holds Ruin's. The two middle blocks are kept in ROM byte
 * order below but labelled by TRUE content, so the lookup table stays in natural
 * trackId order. Balloon-Hunt spawn positions read through this table. */
static const int s_collectPosIsland[17 * 3] = {
    0x165,0x4B,-0x15CC, -0x20D3,0x4B,-0xC27, -0x2319,0x2E4,0x4ED,
    -0x1BF1,0x4B,0x13B2, -0x1449,0xD8,0x187, 0x43F,0x4B,0x92E,
    -0x24F,0x4B,0x13E0, 0x1B36,0xE4,0x24E7, 0x216C,0x4B,0x740,
    0xC8B,0x4B,-0x203, 0x19C6,0x4B,-0xB2D, 0xD65,0x430,-0x1426,
    -0xC93,0x4B,-0x59C, 0x1082,0x4B,0x83D, 0x1FFC,0xD4,0x1A1B,
    -0x11FA,0x4B,-0x1023, -0x4F6,0x5D,0xF7,
};
static const int s_collectPosCity[17 * 3] = {
    -0x1616,0x4B,0x7C5, -0x2167,0x4B,0x970, -0xFC1,0x4B,-0x7B6,
    -0xF37,0x434,-0xD3C, -0x8F7,0x1F3,-0x7C6, 0xD6D,0x57B,0x13BA,
    0xE7B,0x300,0xEF4, 0x18D5,0x4B,0x196, 0x1D98,0x3F6,0x119B,
    0x211F,0x436,-0x613, 0x1095,0x4B,-0x124E, -0x1E1A,0x4B4,0x1897,
    -0x1039,0x4B,-0x146C, -0x10F6,0x199,-0xAD2, 0x60D,0x2CF,-0x11C5,
    -0xF35,0x4B,0xABD, 0xF1F,0x4B,0x8C2,
};
static const int s_collectPosFactory[17 * 3] = {  /* ROM slot for g_trackId=3 — physically Reactive Factory's data */
    -0x29C5,0x4B,-0x13DA, -0x16D9,0x4B,-0xCEE, -0x17F3,0x4B,0xA54,
    -0x1738,0x345,0xC71, -0x138F,0x4B,0x3F8, -0x809,0x4B,0x687,
    0xA0,0xDA,0x17F4, 0x12EB,0x4B,0xFA2, 0x25AD,0x4B,0xE90,
    0x1C97,0x4B,0xEF, 0xE80,0x4B,-0xF3D, 0x1458,0xFF,-0x166C,
    0x2805,0x296,-0xEC4, 0x1B27,0x361,-0x6C2, 0xFE3,0x3BD,-0x1142,
    -0x4B2,0x4B,-0x1780, -0x15F0,0x4B,0x15B,
};
static const int s_collectPosRuin[17 * 3] = {  /* ROM slot for g_trackId=4 — physically Regal Ruin's data */
    -0x2871,0x4B,-0x1DD, -0x1CF8,0x111,0x1018, -0x129D,0x4B,-0xAD7,
    0x675,0x4B,0xE24, 0xB8E,0x4B,0x9A2, -0x83D,0x4B,-0x5A5,
    0xBBF,0x4B,-0x1505, 0x1273,0x4DB,0x436, 0xC4E,0x2DF,0xBB9,
    -0x101A,0x110,0x319, -0x70F,0x2DF,0x1BB4, 0x15EB,0x42F,-0x14E,
    -0x23A1,0x4B,0x2F7, -0x20F4,0x4B,0x1BB2, 0xE70,0x573,0x168E,
    -0x4C,0x4B,0x1488, -0x9AF,0x429,-0x98,
};
static const int s_collectPosEmerald[17 * 3] = {
    -0xC7D,0x66B,-0xC72, 0x7E4,0x79E,0x117D, -0x1DCB,0x39A,0x23B,
    -0x3241,0x650,0x122D, -0x13E4,0xA49,0x16BD, -0x168C,0x765,0x867,
    0x558,0x93,0xDD4, 0x10EF,0x10A,-0x42C, 0x4AF,0x429,0x32C,
    0xD31,0x4B0,0x30D, 0x102D,0x4B0,0x1998, 0x186D,0x79E,0x26BB,
    0xAF0,0x79E,0x1F06, 0x75,0x79E,0x11AD, -0x1320,0x627,-0x145D,
    -0x2E5F,0xD7,0x74E, -0x2392,0x909,0x1B43,
};

static const int *s_collectPosTables[] = {
    NULL, s_collectPosIsland, s_collectPosCity, s_collectPosRuin,
    s_collectPosFactory, s_collectPosEmerald
};

/* Item position table — 17 entries × 3 ints at 0x4FF858 */
/* Extracted from ROM at 0x4FF858 — 17 entries × 3 ints (R, G, B item color/type) */
static const int s_itemPositions[17 * 3] = {
    0x80,0x40,0x00, 0xC0,0x20,0x20, 0xFF,0x00,0x00,
    0xFF,0x40,0x00, 0xFF,0x80,0x00, 0xFF,0xC0,0x00,
    0xFF,0xFF,0x00, 0xC0,0xFF,0x00, 0x80,0xFF,0x00,
    0x40,0xFF,0x80, 0x00,0xFF,0xFF, 0x40,0xC0,0xFF,
    0x80,0x80,0xFF, 0xC0,0x40,0xFF, 0xFF,0x00,0xFF,
    0xFF,0x00,0x80, 0xC0,0x20,0x40,
};


/* Forward declarations */
static void InitAnimIsland(void);
static void InitAnimCity(void);
static void InitAnimFactory(void);
static void InitAnimRuin(void);
static void InitAnimEmerald(void);
void InitObjectVisibility(void);
void InitCollectibles_SP(void);
void InitItems_SP(void);
void InitItems_MP(void);

/* =====================================================================
 * ResetAnimCounters — 0x0047ED20 — 77 bytes
 * ===================================================================== */
void ResetAnimCounters(int objFieldValue)
{
    g_animBobBaseY = objFieldValue;  /* [0x9252B0] = EAX (per-track object struct field) */
    g_menuExtraY = 0;               /* [0x925290] = 0 */
    g_menuScrollX = 0x40;           /* [0x925294] = 0x40 */
    g_menuScrollTarget = 0;         /* [0x925298] = 0 */
    g_menuMaxScroll = 0x40;         /* [0x92529C] = 0x40 */
    g_animStateA0 = 0;              /* [0x9252A0] = 0 */
    g_animStateA4 = 0x40;           /* [0x9252A4] = 0x40 */
    g_animStateA8 = 0;              /* [0x9252A8] = 0 */
    g_animStateAC = 0;              /* [0x9252AC] = 0 */
    g_animStateB4 = 0;              /* [0x9252B4] = 0 */
    g_modelRotation = 0;            /* [0x92528C] = 0 */
}

/* =====================================================================
 * RegisterAnimPolygons — 0x0047CDEC — 420 bytes
 *
 * Watcom fastcall: EAX=tpage, EDX=xStart, ECX=xSize,
 *   EBX=yStart, stack[0]=ySize, stack[1]=animDesc
 * ===================================================================== */
void RegisterAnimPolygons(int tpage, int xStart, int xSize,
                          int yStart, int ySize, intptr_t *animDesc)
{
    int upperX = (xStart + xSize) * 0x10000 - 1;
    int lowerX = xStart * 0x10000;
    int upperY = (yStart + ySize) * 0x10000 - 1;
    int lowerY = yStart * 0x10000;
    int tileW  = xSize << 16;
    int tileH  = ySize << 16;

    /* Binary uses [0x6EAD38] = g_polygonIndexRunning, which is
     * g_modelPolygonCount + g_objectPolygonCount + g_sceneryPolygonCount.
     * Character model polys occupy the start of the array; track and decoration
     * polys follow.  Must scan ALL slots so that late-loaded decoration polys
     * (e.g. Ruin starting-line flames) are found. */
    int totalCount = g_polygonIndexRunning;
    int *poly = (int *)g_polygonArrayBase;

    for (int i = 0; i < totalCount; i++) {
        if (*(unsigned char *)(poly + 10) == (unsigned int)tpage &&
            lowerX <= poly[0] && lowerX <= poly[2] &&
            lowerX <= poly[4] && lowerX <= poly[6] &&
            lowerY <= poly[1] && lowerY <= poly[3] &&
            lowerY <= poly[5] && lowerY <= poly[7] &&
            poly[0] <= upperX && poly[2] <= upperX &&
            poly[4] <= upperX && poly[6] <= upperX &&
            poly[1] <= upperY && poly[3] <= upperY &&
            poly[5] <= upperY && poly[7] <= upperY)
        {
            intptr_t *entry = g_animRegTable + g_animRegCount * 10;
            entry[0] = (intptr_t)poly;
            entry[1] = (poly[0] - xStart * 0x10000) % tileW;
            entry[2] = (poly[1] - yStart * 0x10000) % tileH;
            entry[3] = (poly[2] - xStart * 0x10000) % tileW;
            entry[4] = (poly[3] - yStart * 0x10000) % tileH;
            entry[5] = (poly[4] - xStart * 0x10000) % tileW;
            entry[6] = (poly[5] - yStart * 0x10000) % tileH;
            entry[7] = (poly[6] - xStart * 0x10000) % tileW;
            entry[8] = (poly[7] - yStart * 0x10000) % tileH;
            entry[9] = (intptr_t)animDesc;
            *(unsigned char *)(poly + 10) = (unsigned char)animDesc[6]; /* tpage index */
            *(unsigned short *)(poly + 11) = (unsigned short)g_animRegCount;
            g_animRegCount++;
        }
        poly += 12;
    }
}

/* =====================================================================
 * RegisterQuadBlock — 0x0047D814 — 420 bytes
 * ===================================================================== */
static intptr_t *RegisterQuadBlock(int *controlStruct, int *polyPtr, int polyCount,
                              int minX, int minY, int maxX, int maxY)
{
    intptr_t *entry = g_animRegTable + g_animRegCount * 10;

    for (int i = 0; i < polyCount; i++) {
        if (*(unsigned char *)(polyPtr + 10) == (unsigned int)g_tpageExtra &&
            minX <= polyPtr[0] && minX <= polyPtr[2] &&
            minX <= polyPtr[4] && minX <= polyPtr[6] &&
            minY <= polyPtr[1] && minY <= polyPtr[3] &&
            minY <= polyPtr[5] && minY <= polyPtr[7] &&
            polyPtr[0] <= maxX && polyPtr[2] <= maxX &&
            polyPtr[4] <= maxX && polyPtr[6] <= maxX &&
            polyPtr[1] <= maxY && polyPtr[3] <= maxY &&
            polyPtr[5] <= maxY && polyPtr[7] <= maxY)
        {
            entry[0] = (intptr_t)polyPtr;
            entry[1] = (polyPtr[0] - minX) % 0x400000;
            entry[2] = (polyPtr[1] - minY) % 0x400000;
            entry[3] = (polyPtr[2] - minX) % 0x400000;
            entry[4] = (polyPtr[3] - minY) % 0x400000;
            entry[5] = (polyPtr[4] - minX) % 0x400000;
            entry[6] = (polyPtr[5] - minY) % 0x400000;
            entry[7] = (polyPtr[6] - minX) % 0x400000;
            entry[8] = (polyPtr[7] - minY) % 0x400000;
            g_animRegCount++;
            g_animRegGeomCount++;
            entry += 10;
            *(int *)((char *)controlStruct + 0xC) += 1;
        }
        polyPtr += 12;
    }
    return entry;
}

/* =====================================================================
 * RegisterAnimatedQuadrants — 0x0047D9B8 — 1105 bytes
 * ===================================================================== */
void RegisterAnimatedQuadrants(int *controlStruct, int *obj1, int *obj2,
                            int *obj3, int *obj4)
{
    *(int *)((char *)controlStruct + 0xC) = 0;
    *(int *)((char *)controlStruct + 0x8) = g_animRegCount;

    if (obj1 != NULL) {
        int *polyBase = (int *)((char *)g_polygonArrayBase +
                       (int)(*(unsigned short *)((char *)obj1 + 0x30)) * 0x30);
        int polyCount = (int)(*(unsigned short *)((char *)obj1 + 0x32));
        RegisterQuadBlock(controlStruct, polyBase, polyCount, 0, 0, 0x3FFFFF, 0x3FFFFF);
        RegisterQuadBlock(controlStruct, polyBase, polyCount, 0x400000, 0x400000, 0x7FFFFF, 0x7FFFFF);
        RegisterQuadBlock(controlStruct, polyBase, polyCount, 0x400000, 0x800000, 0x7FFFFF, 0xBFFFFF);
        RegisterQuadBlock(controlStruct, polyBase, polyCount, 0, 0xC00000, 0x3FFFFF, 0xFFFFFF);
    }

    if (obj2 != NULL) {
        int *polyBase = (int *)((char *)g_polygonArrayBase +
                       (int)(*(unsigned short *)((char *)obj2 + 0x30)) * 0x30);
        int polyCount = (int)(*(unsigned short *)((char *)obj2 + 0x32));
        RegisterQuadBlock(controlStruct, polyBase, polyCount, 0, 0, 0x3FFFFF, 0x3FFFFF);
        RegisterQuadBlock(controlStruct, polyBase, polyCount, 0x400000, 0x400000, 0x7FFFFF, 0x7FFFFF);
        RegisterQuadBlock(controlStruct, polyBase, polyCount, 0x400000, 0x800000, 0x7FFFFF, 0xBFFFFF);
        RegisterQuadBlock(controlStruct, polyBase, polyCount, 0, 0xC00000, 0x3FFFFF, 0xFFFFFF);

        if (obj3 != NULL) {
            int *pb3 = (int *)((char *)g_polygonArrayBase +
                       (int)(*(unsigned short *)((char *)obj3 + 0x30)) * 0x30);
            int pc3 = (int)(*(unsigned short *)((char *)obj3 + 0x32));
            RegisterQuadBlock(controlStruct, pb3, pc3, 0, 0, 0x3FFFFF, 0x3FFFFF);
            RegisterQuadBlock(controlStruct, pb3, pc3, 0x400000, 0x400000, 0x7FFFFF, 0x7FFFFF);
            RegisterQuadBlock(controlStruct, pb3, pc3, 0x400000, 0x800000, 0x7FFFFF, 0xBFFFFF);
            RegisterQuadBlock(controlStruct, pb3, pc3, 0, 0xC00000, 0x3FFFFF, 0xFFFFFF);

            if (obj4 != NULL) {
                int *pb4 = (int *)((char *)g_polygonArrayBase +
                           (int)(*(unsigned short *)((char *)obj4 + 0x30)) * 0x30);
                int pc4 = (int)(*(unsigned short *)((char *)obj4 + 0x32));
                RegisterQuadBlock(controlStruct, pb4, pc4, 0, 0, 0x3FFFFF, 0x3FFFFF);
                RegisterQuadBlock(controlStruct, pb4, pc4, 0x400000, 0x400000, 0x7FFFFF, 0x7FFFFF);
                RegisterQuadBlock(controlStruct, pb4, pc4, 0x400000, 0x800000, 0x7FFFFF, 0xBFFFFF);
                RegisterQuadBlock(controlStruct, pb4, pc4, 0, 0xC00000, 0x3FFFFF, 0xFFFFFF);
            }
        }
    }
}

static void ScanAnimatedObject(int *obj, int yBandALo, int yBandAHi,
                            int yBandBLo, int yBandBHi, int *controlStruct)
{
    if (obj == NULL) {
        return;
    }

    int *polyBase = (int *)((char *)g_polygonArrayBase +
                   (int)(*(unsigned short *)((char *)obj + 0x30)) * 0x30);
    int polyCount = (int)(*(unsigned short *)((char *)obj + 0x32));

    for (int p = 0; p < polyCount; p++) {
        if (*(unsigned char *)(polyBase + 10) == (unsigned int)g_tpageExtra &&
            polyBase[0] > 0xFFFFF && polyBase[2] > 0xFFFFF &&
            polyBase[4] > 0xFFFFF && polyBase[6] > 0xFFFFF &&
            polyBase[0] < 0x400000 && polyBase[2] < 0x400000 &&
            polyBase[4] < 0x400000 && polyBase[6] < 0x400000)
        {
            int bandLo = -1;
            if (yBandALo <= polyBase[1] && yBandALo <= polyBase[3] &&
                yBandALo <= polyBase[5] && yBandALo <= polyBase[7] &&
                polyBase[1] <= yBandAHi && polyBase[3] <= yBandAHi &&
                polyBase[5] <= yBandAHi && polyBase[7] <= yBandAHi) {
                bandLo = yBandALo;
            }
            else if (yBandBLo <= polyBase[1] && yBandBLo <= polyBase[3] &&
                       yBandBLo <= polyBase[5] && yBandBLo <= polyBase[7] &&
                       polyBase[1] <= yBandBHi && polyBase[3] <= yBandBHi &&
                       polyBase[5] <= yBandBHi && polyBase[7] <= yBandBHi) {
                bandLo = yBandBLo;
            }

            if (bandLo >= 0) {
                intptr_t *entry = g_animRegTable + g_animRegCount * 10;
                entry[0] = (intptr_t)polyBase;
                entry[1] = (polyBase[0] - 0x100000) % 0x300000;
                entry[2] = (polyBase[1] - bandLo) % 0x180000;
                entry[3] = (polyBase[2] - 0x100000) % 0x300000;
                entry[4] = (polyBase[3] - bandLo) % 0x180000;
                entry[5] = (polyBase[4] - 0x100000) % 0x300000;
                entry[6] = (polyBase[5] - bandLo) % 0x180000;
                entry[7] = (polyBase[6] - 0x100000) % 0x300000;
                entry[8] = (polyBase[7] - bandLo) % 0x180000;
                g_animRegGeomCount++;
                g_animRegCount++;
                controlStruct[1]++;
            }
        }
        polyBase += 12;
    }
}

/* =====================================================================
 * RegisterAnimatedGeometry — 0x0047CF90 — 2180 bytes
 * ===================================================================== */
void RegisterAnimatedGeometry(int mode, int *controlStruct, int *obj1, int *obj2,
                           int *obj3, int *obj4)
{
    int yALo;
    int yAHi;
    int yBLo;
    int yBHi;

    if (mode == 0x32) {
        yALo = 0x500000;
        yAHi = 0x500000 + 0x17FFFF;
        yBLo = 0x900000;
        yBHi = 0x900000 + 0x17FFFF;
    }
    else {
        yALo = 0x680000;
        yAHi = 0x680000 + 0x17FFFF;
        yBLo = 0xA80000;
        yBHi = 0xA80000 + 0x17FFFF;
    }

    controlStruct[1] = 0;
    controlStruct[0] = g_animRegCount;

    ScanAnimatedObject(obj1, yALo, yAHi, yBLo, yBHi, controlStruct);
    ScanAnimatedObject(obj2, yALo, yAHi, yBLo, yBHi, controlStruct);
    ScanAnimatedObject(obj3, yALo, yAHi, yBLo, yBHi, controlStruct);
    ScanAnimatedObject(obj4, yALo, yAHi, yBLo, yBHi, controlStruct);
}

/* Helper: set sky polygon vertices */
static void SetSkyPolyVerts(int objStructOffset)
{
    int idx = *(unsigned short *)((char *)g_objectStructArray + objStructOffset);
    int *poly = (int *)((char *)g_polygonArrayBase + (idx + 0x47) * 0x30);
    poly[0] = 0x800000;
    poly[1] = 0;
    poly[2] = 0xBFFFFF;
    poly[3] = 0;
    poly[4] = 0xBFFFFF;
    poly[5] = 0x1FFFFF;
    poly[6] = 0x800000;
    poly[7] = 0x1FFFFF;
}

/* =====================================================================
 * InitAnimIsland — 0x0047CA74 — 808 bytes
 * ===================================================================== */
static void InitAnimIsland(void)
{
    char *obj = (char *)g_objectStructArray;
    int *polyBase = (int *)g_polygonArrayBase;

    ResetAnimCounters(*(int *)(obj + 0x85C0));  /* Island: obj+0x85C0 */

    g_animStateB8 = 0; 
    g_menuAnimY = 0;
    g_animStateC8 = 0;
    g_animStateD8 = 0;
    g_animStateE0 = 0;
    g_animStateE4 = 0;
    g_animStateE8 = 0;

    SetSkyPolyVerts(0x85CC);

    g_animStateCC = 0x4040;
    g_animStateD4 = 0x6810;
    g_animStateF0 = 0x5010;
    g_trackAnimTickRate = 0x96;

    int idx;
    int *p;
    int tmp;

    /* Swap polygon vertex pairs for waterfall objects */
    idx = *(unsigned short *)(obj + 0x7158);
    p = (int *)((char *)polyBase + (idx + 0xC) * 0x30);

    tmp = p[0];
    p[0] = p[4];
    p[4] = tmp;

    tmp = p[1];
    p[1] = p[5];
    p[5] = tmp;

    tmp = p[2];
    p[2] = p[6];
    p[6] = tmp;

    tmp = p[3];
    p[3] = p[7];
    p[7] = tmp;
    
    idx = *(unsigned short *)(obj + 0x7158);
    p = (int *)((char *)polyBase + (idx + 0x36) * 0x30);

    tmp = p[0];
    p[0] = p[4];
    p[4] = tmp;

    tmp = p[1];
    p[1] = p[5];
    p[5] = tmp;

    tmp = p[2];
    p[2] = p[6];
    p[6] = tmp;

    tmp = p[3];
    p[3] = p[7];
    p[7] = tmp;

    g_animRegCount = 0;
    g_animRegGeomCount = 0;

    intptr_t *desc0 = (g_animDescTable + 0 * 10);
    intptr_t *desc1 = (g_animDescTable + 1 * 10);
    intptr_t *desc2 = (g_animDescTable + 2 * 10);
    desc0[6] = g_tpageExtra;  /* binary: mov [0x4FF5C0], eax — patch tpage to current track's extra */
    RegisterAnimPolygons(g_tpageExtra, 0xF0, 0x10, 0x20, 0x20, desc0);
    RegisterAnimPolygons(5, 0x40, 0x40, 0x20, 0x20, desc1);
    RegisterAnimPolygons(5, 0x80, 0x40, 0x00, 0x40, desc2);

    g_animCtrl028 = 0;
    g_animCtrl014 = 0;
    g_animCtrl000 = 0;

    int *wobj1 = (int *)(obj + 0x7854);
    int *wobj2 = (int *)(obj + 0xA02C);
    RegisterAnimatedGeometry(0x14, &g_geometryAnimCtrl[0], wobj1, wobj2, NULL, NULL);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[0], wobj1, wobj2, NULL, NULL);

    int *wobj3 = (int *)(obj + 0x8514);
    int *wobj4 = (int *)(obj + 0x9FE8);
    RegisterAnimatedGeometry(0x32, &g_geometryAnimCtrl[5], wobj3, wobj4, NULL, NULL);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[5], wobj3, wobj4, NULL, NULL);

    g_animRegCount -= g_animRegGeomCount;
}

/* =====================================================================
 * InitAnimEmerald — 0x00479498 — 616 bytes
 * ===================================================================== */
static void InitAnimEmerald(void)
{
    char *obj = (char *)g_objectStructArray;

    ResetAnimCounters(*(int *)(obj + 0xDC9C));  /* Emerald: obj+0xDC9C */

    g_trackAnimTickRate = 0x96;
    g_animStateB8 = 0;
    g_menuAnimY = 0;
    g_animStateD0 = 0;
    g_animStateF0 = 0;
    g_animStateF4 = 0;
    g_animStateF8 = 0;
    g_animStateFC = 0;
    g_animState300 = 0;
    g_animState304 = 0;
    g_animState308 = 0;
    g_factoryDoorState = 0;
    g_animState310 = 0;
    g_animState314 = 0;

    SetSkyPolyVerts(0xDCA8);

    g_animRegCount = 0;
    g_animRegGeomCount = 0;

    intptr_t *desc0 = (g_animDescTable + 0 * 10);
    desc0[6] = g_tpageExtra;  /* binary: mov [0x4FF5C0], eax */
    RegisterAnimPolygons(g_tpageExtra, 0xF0, 0x10, 0x20, 0x20, desc0);

    g_animCtrl028 = 0;
    g_animCtrl014 = 0;
    g_animCtrl000 = 0;

    /* Binary 0x00479686..0x004796a9: first Register pair — mode 0x32, ctrl0,
     * scanning four object fields (+0xdbf0, +0xdc34, +0xec68, +0xecac). */
    int *wobj1 = (int *)(obj + 0xDBF0);
    int *wobj2 = (int *)(obj + 0xDC34);
    int *wobj3 = (int *)(obj + 0xEC68);
    int *wobj4 = (int *)(obj + 0xECAC);
    RegisterAnimatedGeometry(0x32, &g_geometryAnimCtrl[0], wobj1, wobj2, wobj3, wobj4);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[0], wobj1, wobj2, wobj3, wobj4);

    /* Binary 0x004796ae..0x004796ea: second Register pair — mode 0x32, ctrl1,
     * scanning two object fields (+0xdbac, +0xec24). */
    int *wobj5 = (int *)(obj + 0xDBAC);
    int *wobj6 = (int *)(obj + 0xEC24);
    RegisterAnimatedGeometry(0x32, &g_geometryAnimCtrl[5], wobj5, wobj6, NULL, NULL);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[5], wobj5, wobj6, NULL, NULL);

    g_animRegCount -= g_animRegGeomCount;
}

/* =====================================================================
 * InitAnimRuin — 0x00479810 — 877 bytes
 * ===================================================================== */
static void InitAnimRuin(void)
{
    char *obj = (char *)g_objectStructArray;
    int *polyBase = (int *)g_polygonArrayBase;

    ResetAnimCounters(*(int *)(obj + 0xD064));  /* Ruin: obj+0xD064 */

    g_trackAnimTickRate = 0xB4;
    g_animStateB8 = 0;
    g_menuAnimY = 0;
    g_animStateD0 = 0;
    g_animStateE0 = 0;
    g_animStateE8 = 0;
    g_animStateEC = 0;
    g_animStateF0 = 0;
    g_animStateF4 = 0;
    g_animStateF8 = 0;
    g_animStateFC = 0;
    g_animState300 = 0;
    g_animState304 = 0;

    SetSkyPolyVerts(0xD070);

    int idx;
    int *p;
    /* Starting-line flag poly A — poly index from obj+0xD3E4.
     * Binary 0x004798d6..0x00479928: writes 8 UV dwords + tpage byte. */
    idx = *(unsigned short *)(obj + 0xD3E4);
    p = (int *)((char *)polyBase + idx * 0x30);
    p[0] = 0;
    p[1] = 0xFFFFFF;
    p[2] = 0x1FFFFF;
    p[3] = 0xF80000;
    p[4] = 0;
    p[5] = 0xF00000;
    p[6] = p[4];
    p[7] = p[5];
    /* 0x00479922: mov cl, byte ptr [0x8f6c28]; mov [edx+ebx+0x28], cl */
    ((unsigned char *)p)[0x28] = (unsigned char)g_tpageCharBase;

    /* Starting-line flag poly B — poly index from obj+0x10B68.
     * Binary 0x0047992c..0x0047997e: writes 8 UV dwords + tpage byte. */
    idx = *(unsigned short *)(obj + 0x10B68);
    p = (int *)((char *)polyBase + idx * 0x30);
    p[0] = 0x4FFFFF;
    p[1] = 0xFFFFFF;
    p[2] = 0x300000;
    p[3] = 0xF80000;
    p[4] = 0x4FFFFF;
    p[5] = 0xF00000;
    p[6] = p[4];
    p[7] = p[5];
    /* 0x00479978: mov bl, byte ptr [0x8f6c28]; mov [edx+0x28], bl */
    ((unsigned char *)p)[0x28] = (unsigned char)g_tpageCharBase;

    /* Zero animated pillar rotation fields + copy to adjacent slots */
    *(unsigned short *)(obj + 0xD0E4) = 0;
    *(unsigned short *)(obj + 0x10758) = 0;
    *(unsigned short *)(obj + 0x107E0) = 0;
    *(unsigned short *)(obj + 0x10824) = 0;
    *(unsigned short *)(obj + 0xD1B0) = 0;
    *(unsigned short *)(obj + 0x10868) = 0;
    *(unsigned short *)(obj + 0x108AC) = 0;
    *(unsigned short *)(obj + 0x108F0) = 0;
    *(unsigned short *)(obj + 0xD0E2) = *(unsigned short *)(obj + 0xD0E4);
    *(unsigned short *)(obj + 0xD0E0) = *(unsigned short *)(obj + 0xD0E4);
    *(unsigned short *)(obj + 0x10756) = *(unsigned short *)(obj + 0x10758);
    *(unsigned short *)(obj + 0x10754) = *(unsigned short *)(obj + 0x10758);
    *(unsigned short *)(obj + 0x107DE) = *(unsigned short *)(obj + 0x107E0);
    *(unsigned short *)(obj + 0x107DC) = *(unsigned short *)(obj + 0x107E0);
    *(unsigned short *)(obj + 0x10822) = *(unsigned short *)(obj + 0x10824);
    *(unsigned short *)(obj + 0x10820) = *(unsigned short *)(obj + 0x10824);
    *(unsigned short *)(obj + 0xD1AE) = *(unsigned short *)(obj + 0xD1B0);
    *(unsigned short *)(obj + 0xD1AC) = *(unsigned short *)(obj + 0xD1B0);
    *(unsigned short *)(obj + 0x10866) = *(unsigned short *)(obj + 0x10868);
    *(unsigned short *)(obj + 0x10864) = *(unsigned short *)(obj + 0x10868);
    *(unsigned short *)(obj + 0x108AA) = *(unsigned short *)(obj + 0x108AC);
    *(unsigned short *)(obj + 0x108A8) = *(unsigned short *)(obj + 0x108AC);
    *(unsigned short *)(obj + 0x108EE) = *(unsigned short *)(obj + 0x108F0);
    *(unsigned short *)(obj + 0x108EC) = *(unsigned short *)(obj + 0x108F0);

    g_animRegCount = 0;
    g_animRegGeomCount = 0;

    intptr_t *desc0 = (g_animDescTable + 0 * 10);
    intptr_t *desc1 = (g_animDescTable + 7 * 10);
    desc0[6] = g_tpageExtra;  /* binary: mov [0x4FF5C0], eax */
    RegisterAnimPolygons(g_tpageExtra, 0xF0, 0x10, 0x20, 0x20, desc0);
    RegisterAnimPolygons(4, 0x20, 0x20, 0x18, 0x28, desc1);  /* binary: eax=4, edx=0x20, ecx=0x20, ebx=0x18, ySize=0x28 */

    g_animCtrl028 = 0;
    g_animCtrl014 = 0;
    g_animCtrl000 = 0;

    /* Binary 0x00479ac1..0x00479aa4: first Register pair — mode 0x32, ctrl0,
     * scanning four object fields (+0xcf74, +0x1051c, +0x10560, +0x105a4). */
    int *wobj1 = (int *)(obj + 0xCF74);
    int *wobj2 = (int *)(obj + 0x1051C);
    int *wobj3 = (int *)(obj + 0x10560);
    int *wobj4 = (int *)(obj + 0x105A4);
    RegisterAnimatedGeometry(0x32, &g_geometryAnimCtrl[0], wobj1, wobj2, wobj3, wobj4);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[0], wobj1, wobj2, wobj3, wobj4);

    /* Binary 0x00479b2b..0x00479b67: second Register pair — mode 0x14, ctrl1,
     * scanning two object fields (+0xcfb8, +0x105e8). */
    int *wobj5 = (int *)(obj + 0xCFB8);
    int *wobj6 = (int *)(obj + 0x105E8);
    RegisterAnimatedGeometry(0x14, &g_geometryAnimCtrl[5], wobj5, wobj6, NULL, NULL);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[5], wobj5, wobj6, NULL, NULL);

    g_animRegCount -= g_animRegGeomCount;
}

/* =====================================================================
 * InitAnimFactory — 0x00479FFC — 786 bytes
 * ===================================================================== */
static void InitAnimFactory(void)
{
    char *obj = (char *)g_objectStructArray;
    ResetAnimCounters(*(int *)(obj + 0xCF54));  /* Factory: obj+0xCF54 */

    g_trackAnimTickRate = 0xF0;
    g_animStateB8 = 0;
    g_menuAnimY = 0;
    g_animStateD0 = 0;
    g_animStateE0 = 0;
    g_animStateF0 = 0;
    g_animStateFC = 0;
    g_animState304 = 0;
    g_factoryDoorState = 0;
    g_animState310 = 0;
    g_factoryEmeraldBounceTimer1 = 0;
    g_animState330 = 0;
    g_factoryEmeraldBounceTimer2 = 0;
    g_animState338 = 0;
    g_animState33C = 0;

    SetSkyPolyVerts(0xCF60);

    /* Zero conveyor state (46 ints starting at 0x68158C).
     * Binary 0x0047a17b..0x0047a18d: single loop, clears g_factoryObjState only.
     * g_factoryObjAngle (at 0x00681644) is NOT cleared here in the binary. */
    for (int i = 0; i < 46; i++) {
        g_factoryObjState[i] = 0;
    }

    g_animRegCount = 0;
    g_animRegGeomCount = 0;

    intptr_t *desc0 = (g_animDescTable + 0 * 10);
    intptr_t *desc1 = (g_animDescTable + 8 * 10);
    intptr_t *desc2 = (g_animDescTable + 9 * 10);
    intptr_t *desc3 = (g_animDescTable + 10 * 10);
    intptr_t *desc4 = (g_animDescTable + 11 * 10);
    desc0[6] = g_tpageExtra;  /* binary: mov [0x4FF5C0], eax */
    RegisterAnimPolygons(g_tpageExtra, 0xF0, 0x10, 0x20, 0x20, desc0);
    RegisterAnimPolygons(2, 0xC0, 0x40, 0x00, 0x40, desc1);  /* binary: eax=2, edx=0xC0, ecx=0x40, ebx=0, ySize=0x40 */
    RegisterAnimPolygons(2, 0x80, 0x40, 0x00, 0x40, desc2);  /* binary: eax=2, edx=0x80, ecx=0x40, ebx=0, ySize=0x40 */
    RegisterAnimPolygons(2, 0x60, 0x20, 0x00, 0x40, desc3);  /* binary: eax=2, edx=0x60, ecx=0x20, ebx=0, ySize=0x40 */
    RegisterAnimPolygons(2, 0x20, 0x20, 0x00, 0x40, desc4);  /* binary: eax=2, edx=0x20, ecx=0x20, ebx=0, ySize=0x40 */

    g_animCtrl028 = 0;
    g_animCtrl014 = 0;
    g_animCtrl000 = 0;

    /* Binary 0x0047a231..0x0047a260: first Register pair — mode 0x32, ctrl0,
     * scanning two object fields (+0xcd98, +0x103c8). */
    int *wobj1 = (int *)(obj + 0xCD98);
    int *wobj2 = (int *)(obj + 0x103C8);
    RegisterAnimatedGeometry(0x32, &g_geometryAnimCtrl[0], wobj1, wobj2, NULL, NULL);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[0], wobj1, wobj2, NULL, NULL);

    /* Binary 0x0047a283..0x0047a2bf: second Register pair — mode 0x32, ctrl1,
     * scanning two object fields (+0xcddc, +0x10384). */
    int *wobj3 = (int *)(obj + 0xCDDC);
    int *wobj4 = (int *)(obj + 0x10384);
    RegisterAnimatedGeometry(0x32, &g_geometryAnimCtrl[5], wobj3, wobj4, NULL, NULL);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[5], wobj3, wobj4, NULL, NULL);

    /* Binary 0x0047a2c4..0x0047a2f9: third Register pair — mode 0x14, ctrl2,
     * scanning one object field (+0xcd54). */
    int *wobj5 = (int *)(obj + 0xCD54);
    RegisterAnimatedGeometry(0x14, &g_geometryAnimCtrl[10], wobj5, NULL, NULL, NULL);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[10], wobj5, NULL, NULL, NULL);

    g_animRegCount -= g_animRegGeomCount;
}

/* =====================================================================
 * InitAnimCity — 0x0047BA80 — 1026 bytes
 * ===================================================================== */
static void InitAnimCity(void)
{
    char *obj = (char *)g_objectStructArray;
    int *polyBase = (int *)g_polygonArrayBase;

    ResetAnimCounters(*(int *)(obj + 0xEEF0));  /* City: obj+0xEEF0 */

    g_animState308 = 3;
    g_trackAnimTickRate = 0x82;
    g_animStateB8 = 0;
    g_menuAnimY = 0;
    g_animStateD0 = 0;
    g_animStateE0 = 0;
    g_animStateF0 = 0;
    g_animStateF8 = 0;
    g_animState300 = 0;
    g_animState310 = 0;
    g_animState48C = 0;
    g_animState4B4 = 0;

    SetSkyPolyVerts(0xEEFC);

    /* Zero unknown animated object rotation fields */
    *(unsigned short *)(obj + 0x10B10) = 0;
    *(unsigned short *)(obj + 0xED0C) = 0;
    *(unsigned short *)(obj + 0x10B0E) = *(unsigned short *)(obj + 0x10B10);
    *(unsigned short *)(obj + 0x10B0C) = *(unsigned short *)(obj + 0x10B10);
    *(unsigned short *)(obj + 0xED0A) = *(unsigned short *)(obj + 0xED0C);
    *(unsigned short *)(obj + 0xED08) = *(unsigned short *)(obj + 0xED0C);

    /* unknown animated object billboard polygon/tpage setup — FUN_0047BA80 lines 36473-36521.
     * Sets up animated billboard vertex positions and tpage indices for
     * the rotating City unknown animated objects. Manipulates polygon data at object
     * offset 0xB910 through a chain of pointer reads. */
    int billboardIdx = *(unsigned short *)(obj + 0xB910);
    int *bbPoly = (int *)((char *)polyBase + (billboardIdx + 0x77) * 0x30);
    int *bbPoly2 = bbPoly + 12;  /* next poly = stride 0x30 */

    /* Set billboard tpage to special index 8 */
    *(unsigned char *)(bbPoly + 10) = 8;
    *(unsigned char *)((char *)bbPoly + 0x22 * 4 + 0) = 8;  /* adjacent poly tpage */
    *(unsigned char *)(bbPoly2 + 10) = 8;

    /* Set billboard vertex positions */
    int val = 0x400000;
    bbPoly[6] = val; bbPoly[0] = val;
    bbPoly[0x1C] = val; bbPoly[0x1A] = val;
    bbPoly2[4] = val; bbPoly2[2] = val;

    int val2 = 0x5FFFFF;
    bbPoly[4] = val2; bbPoly[2] = val2;
    bbPoly[0x18] = val2; bbPoly[0x1E] = val2;
    int *bbPoly3 = bbPoly + 0x18;  /* third poly = bbPoly + 0x60 bytes */
    bbPoly2[0] = val2; bbPoly2[6] = val2;

    /* Store the three billboard poly pointers into s_reelPoly[] so
     * UpdateSlotMachineReels knows which polys to scroll UVs on.
     * Binary 0x0047bc46, 0x0047bc53, 0x0047bca3 — store order below
     * mirrors the binary addresses: [0x6816fc]/[0x681700]/[0x681704]. */
    int *bbPoly1 = bbPoly3 - 0x18;  /* recover original bbPoly (was advanced above) */
    s_reelPoly[0] = bbPoly2;        /* 0x6816FC = poly[idx+0x78] */
    s_reelPoly[1] = bbPoly3;        /* 0x681700 = poly[idx+0x79] */
    s_reelPoly[2] = bbPoly1;        /* 0x681704 = poly[idx+0x77] */

    /* Binary 0x0047bcc5..0x0047bcf3: reset billboard UV-scroll state.
     * s_reelPos[0..2] = 0x400000 (center of UV travel); velocities,
     * counter, state all cleared. */
    s_reelPos[0] = 0x400000;
    s_reelPos[1] = 0x400000;
    s_reelPos[2] = 0x400000;
    s_reelVelocity[0] = 0;
    s_reelVelocity[1] = 0;
    s_reelVelocity[2] = 0;
    s_reelCounter = 0;
    s_reelState = 0;

    g_animRegCount = 0;
    g_animRegGeomCount = 0;

    intptr_t *desc0 = (g_animDescTable + 0 * 10);
    intptr_t *desc1 = (g_animDescTable + 3 * 10);
    intptr_t *desc2 = (g_animDescTable + 4 * 10);
    intptr_t *desc3 = (g_animDescTable + 5 * 10);
    intptr_t *desc4 = (g_animDescTable + 6 * 10);
    desc0[6] = g_tpageExtra;  /* binary: mov [0x4FF5C0], eax */
    RegisterAnimPolygons(g_tpageExtra, 0xF0, 0x10, 0x20, 0x20, desc0);
    RegisterAnimPolygons(8, 0x00, 0x20, 0x00, 0x20, desc1);  /* binary: eax=8, edx=0, ecx=0x20, ebx=0, ySize=0x20 */
    RegisterAnimPolygons(8, 0x80, 0x40, 0x20, 0x20, desc2);  /* binary: eax=8, edx=0x80, ecx=0x40, ebx=0x20, ySize=0x20 */
    RegisterAnimPolygons(8, 0xD0, 0x30, 0x10, 0x30, desc3);  /* binary: eax=8, edx=0xD0, ecx=0x30, ebx=0x10, ySize=0x30 */
    RegisterAnimPolygons(3, 0x20, 0x20, 0x20, 0x20, desc4);  /* binary: eax=3, edx=0x20, ecx=0x20, ebx=0x20, ySize=0x20 */

    g_animCtrl028 = 0;
    g_animCtrl014 = 0;
    g_animCtrl000 = 0;

    /* Binary 0x0047bd88..0x0047bdd6: first Register pair — mode 0x32, ctrl0,
     * scanning two object fields (+0xed78, +0x10bc0). */
    int *wobj1 = (int *)(obj + 0xED78);
    int *wobj2 = (int *)(obj + 0x10BC0);
    RegisterAnimatedGeometry(0x32, &g_geometryAnimCtrl[0], wobj1, wobj2, NULL, NULL);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[0], wobj1, wobj2, NULL, NULL);

    /* Binary 0x0047bddb..0x0047be16: second Register pair — mode 0x14, ctrl1,
     * scanning two object fields (+0xed34, +0x10b7c). */
    int *wobj3 = (int *)(obj + 0xED34);
    int *wobj4 = (int *)(obj + 0x10B7C);
    RegisterAnimatedGeometry(0x14, &g_geometryAnimCtrl[5], wobj3, wobj4, NULL, NULL);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[5], wobj3, wobj4, NULL, NULL);

    /* Binary 0x0047be1b..0x0047be6c: third Register pair — mode 0x14, ctrl2,
     * scanning four object fields (+0xedbc, +0x10c04, +0x10c48, +0x10c8c). */
    int *wobj5 = (int *)(obj + 0xEDBC);
    int *wobj6 = (int *)(obj + 0x10C04);
    int *wobj7 = (int *)(obj + 0x10C48);
    int *wobj8 = (int *)(obj + 0x10C8C);
    RegisterAnimatedGeometry(0x14, &g_geometryAnimCtrl[10], wobj5, wobj6, wobj7, wobj8);
    RegisterAnimatedQuadrants(&g_geometryAnimCtrl[10], wobj5, wobj6, wobj7, wobj8);

    g_animRegCount -= g_animRegGeomCount;
}

/* =====================================================================
 * InitTrackAnimObjects — 0x0047FA64 — 149 bytes
 * dispatch + per-track pickup ground-shadow quad setup
 * ===================================================================== */
void InitTrackAnimObjects(void)
{
    /* trackId 3/4: binary dispatch at 0x47fa64 uses the swapped ordering
     * (case 4 -> 0x479810, case 3 -> 0x479ffc) but our statics are already
     * named by CONTENT, not dispatch order. 0x479810's body matches what
     * Ruin needs (writes anim init values into obj[0xd3e4]/[0x10b68] flag
     * polys with the fewer 0x47cdec registrations Ruin uses), and 0x479ffc
     * body matches Factory (5x 0x47cdec calls, 46-dword clear, reads cf54/
     * cf60 Factory slots). So the natural case 3 -> InitAnimRuin, case 4 ->
     * InitAnimFactory mapping is correct. See feedback_no_dispatch_swaps. */
    switch (g_trackId) {
        case TRACK_RESORT_ISLAND:
            InitAnimIsland();
            break;
        case TRACK_RADICAL_CITY:
            InitAnimCity();    
            break;
        case TRACK_REGAL_RUIN:
            InitAnimRuin();    
            break;
        case TRACK_REACTIVE_FACTORY:
            InitAnimFactory();
            break;
        case TRACK_RADIANT_EMERALD:
            InitAnimEmerald();
            break;
    }

    InitObjectVisibility();
}

static void HideObjectList(const int *list)
{
    while (*list != -1) {
        *(unsigned short *)((char *)g_objectStructArray + (*list) * 0x44 + 0x2C) = 0xFFFF;
        list++;
    }
}

/* =====================================================================
 * InitObjectVisibility — 0x0047E54C — 2001 bytes
 *
 * Hides track objects by setting mode (obj+0x2C) to 0xFFFF.
 * Uses per-track -1-terminated index lists in ROM data.
 *
 * ROM tables (extracted from binary):
 *   0x5009F0 + trackId*4 → ptr to time-attack hide list
 *   0x500A04 + trackId*4 → ptr to GP hide list
 *   0x500A18 + trackId*4 → ptr to always-hide list
 * ===================================================================== */
void InitObjectVisibility(void)
{
    if (g_trackId < TRACK_RESORT_ISLAND || g_trackId > TRACK_RADIANT_EMERALD) {
        return;
    }

    /* Time attack: hide collectible tokens */
    if (g_raceType == RACE_TIMEATTACK && s_taHideLists[g_trackId]) {
        HideObjectList(s_taHideLists[g_trackId]);
    }

    /* Multiplayer (raceType==1): hide collectible tokens.
     * Binary: cmp [0x8FB950], 1 at 0x47E586, table at 0x500A04. */
    if (g_raceType == RACE_MULTIPLAYER && s_gpHideLists[g_trackId]) {
        HideObjectList(s_gpHideLists[g_trackId]);
    }

    /* Always-hide list — balloon 3D mesh objects.
     * Hidden unconditionally; only visible in BALLOON submode via AnimateBalloons. */
    if (s_alwaysHideLists[g_trackId]) {
        HideObjectList(s_alwaysHideLists[g_trackId]);
    }

    /* GP mode (raceType == 0): conditionally hide decoration objects based on
     * GP progression and character unlock state.
     * Binary: 0x47E603-0x47E70A (missing from original translation) */
    if (g_raceType == RACE_GP && g_trackId < TRACK_RADIANT_EMERALD) {
        /* If this track's token-challenge rival is already unlocked, hide the
         * first 5 objects from the GP hide list — those are the 5 Sonic Tokens.
         * Binary: 0x47E61E-0x47E647 */
        if (g_charUnlockTable[s_tokenRivalChar[g_trackId]] == 2
            && s_gpHideLists[g_trackId]) {
            const int *list = s_gpHideLists[g_trackId];
            for (int i = 0; i < 5 && list[i] != -1; i++) {
                *(unsigned short *)((char *)g_objectStructArray
                    + list[i] * 0x44 + 0x2C) = 0xFFFF;
            }
        }

        /* Per-track character token hides — binary jump table at 0x47E52C */
        switch (g_trackId) {
            case TRACK_RESORT_ISLAND:  /* Island — 0x47E66D */
                if (g_charUnlockState[0] == 2) {                          /* [0x8FBA8C] */
                    *(unsigned short *)((char *)g_objectStructArray
                        + 0x1F2 * 0x44 + 0x2C) = 0xFFFF;
                }
                break;
            case TRACK_RADICAL_CITY:  /* City — 0x47E69A */
                if (g_charUnlockState[2] == 2) {                          /* [0x8FBA94] */
                    *(unsigned short *)((char *)g_objectStructArray
                        + 0x380 * 0x44 + 0x2C) = 0xFFFF;
                }
                if (g_charUnlockState[1] == 2) {                          /* [0x8FBA90] */
                    *(unsigned short *)((char *)g_objectStructArray
                        + 0x381 * 0x44 + 0x2C) = 0xFFFF;
                }
                break;
            case TRACK_REGAL_RUIN:  /* Ruin — binary case 4 body (0x47E6C0) */
                if (g_charUnlockState[3] == 2) {                          /* [0x8FBA98] */
                    *(unsigned short *)((char *)g_objectStructArray
                        + 0x327 * 0x44 + 0x2C) = 0xFFFF;
                }
                if (g_charUnlockState[4] == 2) {                          /* [0x8FBA9C] */
                    *(unsigned short *)((char *)g_objectStructArray
                        + 0x328 * 0x44 + 0x2C) = 0xFFFF;
                }
                break;
            case TRACK_REACTIVE_FACTORY:  /* Factory — binary case 3 body (0x47E6E6) */
                if (g_charUnlockState[5] == 2) {                         /* [0x8FBAA0] */
                    *(unsigned short *)((char *)g_objectStructArray
                        + 0x30D * 0x44 + 0x2C) = 0xFFFF;
                }
                if (g_charUnlockState[6] == 2) {                         /* [0x8FBAA4] */
                    *(unsigned short *)((char *)g_objectStructArray
                        + 0x309 * 0x44 + 0x2C) = 0xFFFF;
                }
                break;
        }
    }

    /* Collectible and item initialization */
    if (g_netSessionActive == 0) {
        InitCollectibles_SP();
        InitItems_SP();
    }
    else {
        InitItems_MP();
    }

    if (g_trackId >= TRACK_RESORT_ISLAND && g_trackId <= TRACK_REACTIVE_FACTORY) {
        const PickupShadowInit *init = s_pickupShadowInitTables[g_trackId];
        char *obj = (char *)g_objectStructArray;

        while (init->xOff != -1) {
            int wx = *(int *)(obj + init->xOff);
            int wy = *(int *)(obj + init->yOff) + 0x20;  /* Y + 32 */
            int wz = *(int *)(obj + init->zOff);
            BuildGroundShadowQuad(wx, wy, wz,
                &g_pickupShadowVerts[init->bufIdx * 12], init->halfSize);
            init++;
        }
    }
}

/* =====================================================================
 * BuildGroundShadowQuad — FUN_00478B80 — 990 bytes
 *
 * Builds the ground-shadow quad for an object at (worldX, worldY, worldZ):
 * queries terrain height under it, takes the surface normal, and emits 4
 * corners of ±halfSize lying flat on the terrain. Not a billboard — the
 * quad conforms to the ground, which is why it needs the normal.
 *
 * Generic: called for g_objectStructArray pickups (via InitObjectVisibility)
 * and for g_balloonArray entries (via InitCollectibles_SP/InitItems_SP).
 *
 * Arg order matches the binary: p1 = X, p2 = height, p3 = Z. Note this is
 * NOT the order QueryTerrainHeight takes them in — see the call below.
 * ===================================================================== */
void BuildGroundShadowQuad(int worldX, int worldY, int worldZ, int *out, int halfSize)
{
    int terrainH = QueryTerrainHeight(worldX << 12, worldZ << 12, -(worldY << 12));
    int terrY = -(terrainH >> 12);

    int normalX, normalY, normalZ;
    if (terrY == 0) {
        normalX = 0;
        normalY = 0x1000;
        normalZ = 0;
    }
    else {
        /* 0x478BDD: face table (0x6DA560), stride 14, normals at +8/+10/+12.
         * Same walk as DrawMovingPickupShadow. */
        short surfOff = *(short *)((char *)g_trackSurfaceData + g_qtSurfIdx * 0x10);
        int faceOff = (surfOff + g_qtEdgeIdx) * 0xE;
        char *faceData = (char *)g_terFaceTable + faceOff;
        normalX = *(short *)(faceData + 8);
        normalY = -(*(short *)(faceData + 0xA));
        normalZ = *(short *)(faceData + 0xC);
    }

    sr_double pitchRad = sr_atan2((sr_double)normalZ, (sr_double)normalY);
    int pitchAngle = (int)(pitchRad * 2048.0 / M_PI) & 0xFFF;
    int invPitch = (0xFFF - pitchAngle) & 0xFFF;

    int cosPitch = g_cosTable[invPitch] >> 2;
    int sinPitch = g_sinTable[invPitch] >> 2;
    int cross = normalY * cosPitch - sinPitch * normalZ;
    int crossNorm;
    int s = cross >> 31;
    crossNorm = (int)(((cross + s * -0x1000) - (unsigned)((s << 11) < 0)) >> 12);

    sr_double rollRad = sr_atan2((sr_double)normalX, (sr_double)crossNorm);
    int rollAngle = (int)(rollRad * 2048.0 / M_PI) & 0xFFF;
    int invRoll = (0xFFF - rollAngle) & 0xFFF;

    int cosYaw = g_cosTable[pitchAngle] >> 2;
    int sinYaw = g_sinTable[pitchAngle] >> 2;
    int cosRoll = g_cosTable[invRoll] >> 2;
    int sinRoll = g_sinTable[invRoll] >> 2;

    int m00 = (cosRoll * 0x1000) >> 12;
    int m01 = (sinRoll * cosYaw) >> 12;
    int m02 = (sinRoll * sinYaw) >> 12;
    int m10 = 0;
    int __attribute__((unused)) m11 = (sinYaw * -0x1000) >> 12;
    int m12 = (cosYaw * 0x1000) >> 12;

    int neg_halfSize = -halfSize;

#define FIXDIV12_SCO(s) ((s) / 4096)

    out[0] = FIXDIV12_SCO(m00 * neg_halfSize) + worldX;
    out[1] = FIXDIV12_SCO(m01 * neg_halfSize + halfSize * m10) + terrY;
    out[2] = worldZ + FIXDIV12_SCO(m02 * neg_halfSize + halfSize * m12);
    out[3] = FIXDIV12_SCO(m00 * halfSize) + worldX;
    out[4] = FIXDIV12_SCO(m01 * halfSize + halfSize * m10) + terrY;
    out[5] = worldZ + FIXDIV12_SCO(m02 * halfSize + halfSize * m12);
    out[6] = out[3];
    out[7] = FIXDIV12_SCO(m01 * halfSize + m10 * neg_halfSize) + terrY;
    out[8] = worldZ + FIXDIV12_SCO(m02 * halfSize + m12 * neg_halfSize);
    out[9] = out[0];
    out[10] = FIXDIV12_SCO(m01 * neg_halfSize + m10 * neg_halfSize) + terrY;
    out[11] = worldZ + FIXDIV12_SCO(m02 * neg_halfSize + m12 * neg_halfSize);

#undef FIXDIV12_SCO
}

/* =====================================================================
 * InitCollectibles_SP — 0x00478F60 — 253 bytes
 *
 * Copies per-track collectible positions from ROM tables into the
 * runtime collectible array. 17 collectibles per track (stride 0x2C).
 * ROM source: 0x4FF924 + (trackId-1) * 0xCC (3 ints per collectible).
 * Also calls FUN_00478B80(0x23) per collectible to set up object state.
 * ===================================================================== */
void InitCollectibles_SP(void)
{
    if (g_trackId < TRACK_RESORT_ISLAND || g_trackId > TRACK_RADIANT_EMERALD) {
        return;
    }

    const int *src = s_collectPosTables[g_trackId];
    if (src == NULL) {
        return;
    }

    int objOffset = 0;

    for (int i = 0; i < 17; i++) {
        /* Copy position (3 ints) into collectible array at stride 0x2C */
        *(int *)(g_balloonArray + objOffset + 0) = src[i * 3 + 0];
        *(int *)(g_balloonArray + objOffset + 4) = src[i * 3 + 1];
        *(int *)(g_balloonArray + objOffset + 8) = src[i * 3 + 2];
        *(int *)(g_balloonArray + objOffset + 0x18) = -1;  /* state = uncollected */
        /* Store vertex buffer pointer in 64-bit-safe side table.
         * Original stored raw pointer at +0x28 (worked in 32-bit, truncates on 64-bit). */
        g_collectVertexPtrs[i] = &s_collectVertexBuf[i * 12];

        int cx = *(int *)(g_balloonArray + objOffset + 0);
        int cy = *(int *)(g_balloonArray + objOffset + 4) + 0x32;
        int cz = *(int *)(g_balloonArray + objOffset + 8);
        BuildGroundShadowQuad(cx, cy, cz, &s_collectVertexBuf[i * 12], 0x23);

        objOffset += 0x2C;
    }

    /* randomly assign balloons to collectible slots */
    if (g_raceSubMode == SUBMODE_BALLOON) {
        int needed = g_numPlayers * 4 + 1;
        for (int j = 0; j < needed; j++) {
            int slot = j;
            if (needed < 17) {
                do {
                    slot = Random() / 0x788;
                } while (*(int *)(g_balloonArray + slot * 0x2C + 0x18) == 0x5F);
            }
            *(int *)(g_balloonArray + slot * 0x2C + 0x18) = 0x5F;
        }
    }
}

/* =====================================================================
 * InitItems_SP — 0x00479060 — 218 bytes
 *
 * Copies per-track item spawn positions from ROM table at 0x4FF858
 * into the collectible array item fields (offset 0x1C-0x24 per entry),
 * then shuffles them randomly.
 * ===================================================================== */
void InitItems_SP(void)
{
    /* Copy item positions into collectible array */
    for (int i = 0; i < 17; i++) {
        *(int *)(g_balloonArray + i * 0x2C + 0x1C) = s_itemPositions[i * 3 + 0];
        *(int *)(g_balloonArray + i * 0x2C + 0x20) = s_itemPositions[i * 3 + 1];
        *(int *)(g_balloonArray + i * 0x2C + 0x24) = s_itemPositions[i * 3 + 2];
    }

    /* Fisher-Yates shuffle */
    for (int i = 0; i < 32; i++) {
        int a, b;
        do {
            a = Random() / 0x788;
            b = Random() / 0x788;
        } while (a == b);

        /* Swap items at slots a and b */
        int off_a = a * 0x2C, off_b = b * 0x2C;
        int t0 = *(int *)(g_balloonArray + off_a + 0x1C);
        int t1 = *(int *)(g_balloonArray + off_a + 0x20);
        int t2 = *(int *)(g_balloonArray + off_a + 0x24);
        *(int *)(g_balloonArray + off_a + 0x1C) = *(int *)(g_balloonArray + off_b + 0x1C);
        *(int *)(g_balloonArray + off_a + 0x20) = *(int *)(g_balloonArray + off_b + 0x20);
        *(int *)(g_balloonArray + off_a + 0x24) = *(int *)(g_balloonArray + off_b + 0x24);
        *(int *)(g_balloonArray + off_b + 0x1C) = t0;
        *(int *)(g_balloonArray + off_b + 0x20) = t1;
        *(int *)(g_balloonArray + off_b + 0x24) = t2;
    }
}

/* =====================================================================
 * ShuffleCollectiblePlacement — 0x00479248 — 334 bytes
 * (returns through InitItems_MP's epilogue at 0x47923B)
 *
 * Builds the online collectible placement pairs read by InitItems_MP.
 * Called from NetworkScreen (0x48AB3D) right after the lobby config words
 * are set; the host's result goes out with the rest of g_netGameInfoDest.
 *
 * Starts from the identity (entry i = position i, item i), then makes 32
 * random swaps of the item byte between two distinct entries, then 32 of
 * the position byte, so the two are shuffled independently.
 * ===================================================================== */
void ShuffleCollectiblePlacement(void)
{
    unsigned short *pairs = g_collectiblePlacement;

    for (int i = 0; i < 17; i++) {                                 /* 0x47925C-0x479270 */
        pairs[i] = (unsigned short)((i << 8) + i);
    }

    /* Swap item bytes (high) — 0x479276-0x4792FE */
    for (int n = 0; n < 32; n++) {
        int a = Random() / 0x788;                                  /* 0x4792D6 */
        int b;
        do {
            b = Random() / 0x788;                                  /* 0x4792EE */
        } while (b == a);
        int lowA = pairs[a] & 0xFF;
        int lowB = pairs[b] & 0xFF;
        int highA = pairs[a] & 0xFF00;
        int highB = pairs[b] & 0xFF00;
        pairs[a] = (unsigned short)(highB + lowA);                 /* 0x4792BB */
        pairs[b] = (unsigned short)(highA + lowB);                 /* 0x4792C9 */
    }

    /* Swap position bytes (low) — 0x479307-0x479390 */
    for (int n = 0; n < 32; n++) {
        int a = Random() / 0x788;                                  /* 0x479369 */
        int b;
        do {
            b = Random() / 0x788;                                  /* 0x479381 */
        } while (b == a);
        int lowA = pairs[a] & 0xFF;
        int lowB = pairs[b] & 0xFF;
        int highA = pairs[a] & 0xFF00;
        int highB = pairs[b] & 0xFF00;
        pairs[a] = (unsigned short)(highA + lowB);                 /* 0x47934C */
        pairs[b] = (unsigned short)(highB + lowA);                 /* 0x479358 */
    }
}

/* =====================================================================
 * InitItems_MP — 0x0047913C — 265 bytes
 *
 * Multiplayer item placement. Spawns g_numViewports * 4 + 1 collectibles,
 * taking each one's position pair from g_collectiblePlacement (0x68A8A8),
 * which the host's ShuffleCollectiblePlacement filled.
 * ===================================================================== */
void InitItems_MP(void)
{
    int needed = g_numViewports * 4 + 1;

    /* Clear all collectible states to -1 (hidden) */
    for (int i = 0; i < 17; i++) {
        *(int *)(g_balloonArray + i * 0x2C + 0x18) = -1;
    }

    if (needed < 1) {
        return;
    }

    int objOffset = 0;
    int srcIdx = 0;

    for (int i = 0; i < needed; i++) {
        /* Unpack this collectible's placement pair: low byte = position in
         * the track's collectible table, high byte = item position. */
        int collectIdx = (int)(g_collectiblePlacement[srcIdx] & 0xFF);
        int itemIdx = (int)((g_collectiblePlacement[srcIdx] >> 8) & 0xFF);  /* high byte of word */

        /* Binary used flat offset from 0x4FF924 (all tracks contiguous).
         * Our per-track arrays aren't contiguous — use pointer table. */
        const int *trackPos = s_collectPosTables[g_trackId];
        int posOff = collectIdx * 3;
        *(int *)(g_balloonArray + objOffset + 0) = trackPos[posOff + 0];
        *(int *)(g_balloonArray + objOffset + 4) = trackPos[posOff + 1];
        *(int *)(g_balloonArray + objOffset + 8) = trackPos[posOff + 2];

        *(int *)(g_balloonArray + objOffset + 0x1C) = s_itemPositions[itemIdx * 3 + 0];
        *(int *)(g_balloonArray + objOffset + 0x20) = s_itemPositions[itemIdx * 3 + 1];
        *(int *)(g_balloonArray + objOffset + 0x24) = s_itemPositions[itemIdx * 3 + 2];

        *(int *)(g_balloonArray + objOffset + 0x18) = 0x5F;  /* active */
        /* Store vertex buffer pointer in 64-bit-safe side table (see InitCollectibles_SP).
         * Original stored raw pointer at g_balloonArray+0x28 (worked in 32-bit,
         * truncates on 64-bit). Reader is AnimateBalloons in camera_per_track.c. */
        g_collectVertexPtrs[i] = &s_collectVertexBuf[i * 12];

        int cx = *(int *)(g_balloonArray + objOffset + 0);
        int cy = *(int *)(g_balloonArray + objOffset + 4) + 0x32;
        int cz = *(int *)(g_balloonArray + objOffset + 8);
        BuildGroundShadowQuad(cx, cy, cz, &s_collectVertexBuf[i * 12], 0x23);

        objOffset += 0x2C;
        srcIdx += 1;
    }
}
