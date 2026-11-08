/**
 * screen_misc.c — Miscellaneous screen functions
 *
 * CreditsScreen (fully implemented), ResultsScreen,
 * OptionsMenuScreen, LoadSaveScreen, TimeRankingScreen, NetworkScreen.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "endian_util.h"
#include "platform.h"
#include "player_struct.h"
#include "r_types.h"
#include "r_state.h"
#include "net/matchmaker.h"
#include "qrcodegen.h"
#include "net_transport.h"
#include "r_draw.h"
#include <stdarg.h>
#ifdef SONICR_XBOX
#include "xbox_menu_patch.h"
/* Controls page is pad-only: first item (Set Joystick) sits at offset 0, and
 * the pad's Back button replaces ESC as "cancel". */
#define XB_PAD_ITEM_OFS 0
#define XB_CANCEL_PRESSED() ((g_diKeyboardState[0x01] != 0) || (g_keyPressState[4] & 0x80))
#else
#define XB_PAD_ITEM_OFS 3
#define XB_CANCEL_PRESSED() (g_diKeyboardState[0x01] != 0)
#endif

void SoftwareRenderSortedPolygons(void);
void FlipSoftware(void);
void SoftwareSortAndDraw(void);
void ApplySoftwareFade(void);
void platform_pump_events(void);
void platform_sleep_ms(int ms);
void SetupMenuTexturesD3D(void); /* 0x438CD8 */
extern void R_SetNoColorKey(int tpage);
extern void SelectCDTrack(void);
extern void UpdateCDPlayback(int trackNum);
extern int DrawGlyphString(int startX, int y, int *glyphIds, int maxCount);
extern void SetAllSoundVolumes(void);
extern void StopCD(void);
void SetupMenuTexturesD3D(void);
void ProcessTpageStates(void);
void RenderFadeOverlay(void);
void FlipD3D(void);
extern void WaitForFrameCap(void);
void LoadTPageRGB(int tpage, const char *filename);
extern void DrawTexturedQuad(int xPos, int yPos, int depth, int width, int height,
                             int tpage, int uvX, int uvY, int uvW, int uvH,
                             unsigned int color);
void BeginFrame(void);                   /* 0x42299C */
void UpdateFrameTimers(void);            /* 0x47FDE8 */
void AnimateVehicleGlow(Player *p);      /* 0x4217F8 */
void SetViewportFromConfig(int *config); /* 0x4CC0E8 */
extern void RenderBackground(void);
extern void RenderLogoQuads(void);
extern void UpdateFrameTimers(void);
extern void AdvancePlayerAnimation(int playerIndex);
extern void RenderCharacterCredits(int, int, int, int, int, int, Player *);
extern void LoadTPageRGB(int tpage, const char *filename);
extern void SetupD3DTexturesBegin(void);               /* 0x438CA4 */
extern void SetTitleTextureFile(const char *path);
extern void LoadTitleTextureD3D(void);                      /* 0x42C3EC */
extern void RemapCharacterTpages(void);                /* 0x470460 */
extern void LoadCharacterGouraudTables(void);          /* 0x476750 */
extern void TintCharacterGouraudTables(int, int, int); /* 0x430BDC */
extern void R_ClearNoColorKey(int tpage);
void EnumNetworkSessions(int flag); /* 0x487180 */
extern void R_FreezeTexture(int tpage);
/* Forward declarations for render/update functions */
void RenderBackground(void);                    /* 0x435868 */
void ColorizeTpageHiColor(int r, int g, int b); /* 0x488310 */
void SetupMenuTexturesD3D(void);                /* 0x438CD8 */
void FinalizeMenuTexturesD3D(void);             /* 0x438D10 */
void UpdateFrameTimers(void);                   /* 0x47FDE8 */
void UpdateVertexLighting(Player *player);      /* 0x430638 */
void PollAllInputDevices(void);                 /* 0x479248 */
void RenderCharacterOnPodium(int xOff, int yOff, int zOffset,
                             int angleC, int angleD, int angleE,
                             Player *player);               /* 0x4437FC */
void RenderResultsScreen(int screenMode, int playerOffset); /* 0x489D18 */
void EnumNetworkSessions(int flag);                         /* 0x487180 */
void OpenNetworkSession(int sessionIdx);                    /* 0x4871B4 */
void InitNetworkGame(void);                                 /* 0x487A38 */
void UpdateNetworkSync(const void *data, int len);          /* 0x487644 */
void StartNetworkThread(void);                              /* 0x487674 */
void CloseDirectPlaySession(void);                          /* 0x4875CC */
int IsDirectPlayAvailable(void);                            /* 0x487CD8 */
void AnimateVehicleGlow(Player *player);                    /* 0x4217F8 */
extern void RenderBalloonModelForResultsScreen(int xOff, int yOff, int zBase,
                                               int angleA, int angleB, int angleC,
                                               int scaleDivisor,
                                               int colorR, int colorG, int colorB);
void UpdateIslandAnimations(void);  /* 0x47df20 — trackId 1 */
void UpdateCityAnimations(void);    /* 0x47c384 — trackId 2 */
void UpdateFactoryAnimations(void); /* 0x479b80 — trackId 4 */
void UpdateRuinAnimations(void);    /* 0x47a5f8 — trackId 3 */

/* ROM position scale template — 10 ints at 0x503B40, indexed by charId */
static const int s_resultObjTemplate[10] = {
    64, 64, 64, 80, 96, 64, 64, 64, 72, 80
};

extern int g_optCurrentPage;          /* 0x0068AFCC */

/* =====================================================================
 * Options Menu — globals and ROM data
 * ===================================================================== */

/* Options-menu tenancy of the shared block at 0x925290. Each name indexes
 * g_stateBlock92528C[] directly, so the slot behind an alias is visible here
 * without following it through another name. The block is reused by other
 * screens at the same indices — the full tenant list is in sonicr_globals.h.
 *
 *   [0] 0x925290  input/capture debounce gate
 *   [1] 0x925294  selected item
 *   [2] 0x925298  last item index on the page
 *   [3] 0x92529C  doubles as scroll-dirty flag and ESC debounce, see below
 *   [4] 0x9252A0  current smooth-scroll Y
 *   [5] 0x9252A4  target scroll Y
 *   [6] 0x9252A8  centering offset for the item list
 *   [8] 0x9252B0  item count on the page
 *  [10] 0x9252B8  exit-confirmation flag
 *
 * g_optMenuCursor is NOT in the array: 0x92528C sits one int below the base. */
#define g_optMenuInputGateG   g_stateBlock92528C[1]   /* 0x925290 — capture-debounce
                                                       *   used by ScanInputForAction */
#define g_optEscGate          g_stateBlock92528C[4]   /* 0x92529c — ESC-debounce gate
                                                       *   used by the sub-page dispatcher
                                                       *   (binary 0x494c6e/0x494d0f) */
#define g_optMenuCursor       g_modelRotation        /* 0x92528C — below the array base */
#define g_optMenuSelected     g_stateBlock92528C[2]   /* 0x925294 */
#define g_optMenuMaxItem      g_stateBlock92528C[3]   /* 0x925298 */
#define g_optMenuScrollFlag   g_stateBlock92528C[4]   /* 0x92529C */
#define g_optMenuScrollCur    g_stateBlock92528C[5]   /* 0x9252A0 */
#define g_optMenuScrollTgt    g_stateBlock92528C[6]   /* 0x9252A4 */
#define g_optMenuCenter       g_stateBlock92528C[7]   /* 0x9252A8 */
#define g_optMenuItemCount    g_stateBlock92528C[9]   /* 0x9252B0 */
#define g_optMenuExitConfirm  g_stateBlock92528C[11]  /* 0x9252B8 */
#define g_optMenuReturnCode   g_screenResult         /* 0x925418 */

/* g_optEscGate and g_optMenuScrollFlag are one press-debounce gate serving two
 * input contexts that never run in the same frame: the nav/scroll block is
 * guarded by g_optSubPageState == 0 and the sub-page dispatcher by
 * g_optSubPageState != 0. Sharing the slot is safe in both directions. */

/* Existing globals referenced */
/* g_interlaceMode — #define alias for g_softDoubleBuf in sonicr_globals.h */

/* Additional option menu state */
int g_optSubPageState;                    /* 0x0068B014 — controller/key remap sub-page; extern'd by leaf_small.c */
int g_optSubPageCounter;                  /* 0x0068B018 — also used as g_inputMappingCount in leaf_small.c */
static int g_optKeyRemapState;            /* 0x0068B01C — key remapping active */
static int g_optKeyRemapProgress;         /* 0x0068B020 */
static int g_optKeyRemapPlayer;           /* 0x0068B024 */
static int g_optKeyRemapFlag;             /* 0x0068B028 */
static int g_optSfxTestSel;                /* 0x0068AF34 — Sound Test selector (0-0x2A) */
static int g_optMusicTestSel;              /* 0x0068AF38 — Music Test selector (0-0x13) */
/* Music Test's currently-requested track, compared each frame against
 * GetLogicalCDTrack() so the CD is stopped once the test track ends (binary
 * [0x68afc8], set at 0x49480a). Kept as its own var — the binary reuses that
 * address for g_menuState, but the two never overlap in time. */
static int s_musicTestTrack = -1;
static int __attribute__((unused)) g_optSplashState;              /* 0x0068AF70 */

static short g_optKeyRemapResult[16];     /* 0x0068B040 — remap output key IDs */
static int   s_optKeyRemapTemp[10];       /* stack temp [ebp-0x50] in binary —
                                           * snapshot of current keymap for
                                           * cancel/restore via ESC */

/* Per-joystick-button pressed state at 0x675B0C — written by
 * platform_poll_gamepads (SDL_Joystick) each frame, read by ScanKeyRemap
 * during joystick remap. Stride is 80 bytes per joystick slot. */

/* Forward decls — implemented in input/input.c */
extern int  ScanInputForAction(void);     /* 0x00493744 */
extern void SaveKeyMappings(void);        /* 0x0042e948 */

/* Joystick slot config — declared in globals_extra.c */
extern char g_joystickSlots[4][282];      /* 0x0067541A */
/* g_keyPressState[] now defined in globals_extra.c — extern decl above. */

/* ROM table at 0x00502D08: action bit-patterns indexed by
 * [(btnCount - 3) * 6 + actionCounter]. Used by ScanKeyRemap to map
 * the current action slot (0..5) and the joystick's button count
 * (3..6) to the 16-bit pad-bit pattern that should be assigned to
 * the button the user just pressed.
 *
 * Row layout (6 shorts per row, btnCount=3..6):
 *   3:  0x0600 0x0800 0x0100 0xFFFF 0xFFFF 0xFFFF
 *   4:  0x0600 0x0800 0x0100 0x0004 0xFFFF 0xFFFF
 *   5:  0x0600 0x0800 0x0100 0x1100 0x0004 0xFFFF
 *   6:  0x0600 0x0800 0x0100 0x0008 0x0080 0x0040
 *
 * 0xFFFF = no action for that slot. The original was previously
 * mislabeled as `g_romKeySfxTable` — it has nothing to do with SFX. */
/* PAD_DIRECTIONS (sonicr_globals.h) is deliberately absent from the table
 * below — the remap UI binds actions only, never directions — which is why
 * the commit in the dispatcher has to leave those entries alone.
 *
 * The literals here stay raw: this is ROM-derived data, and two of the entries
 * (0x0600 = PAD_JUMP | 0x0200, and 0x1100 = PAD_UP | PAD_ACCEL) are composites
 * whose intent is not obvious enough to name confidently. 0x0004 has no name
 * at all — it is one of the bits no keyboard slot writes. */
static const short g_remapActionBits[4][6] = {
    { 0x0600, 0x0800, 0x0100, (short)0xFFFF, (short)0xFFFF, (short)0xFFFF },
    { 0x0600, 0x0800, 0x0100, 0x0004,        (short)0xFFFF, (short)0xFFFF },
    { 0x0600, 0x0800, 0x0100, 0x1100,        0x0004,        (short)0xFFFF },
    { 0x0600, 0x0800, 0x0100, 0x0008,        0x0080,        0x0040 },
};

/* Per-button "already-mapped this session" flag at 0x68b02c.
 * ScanKeyRemap checks/sets this so the user can't bind the same button
 * to two different actions in one remap pass. Cleared on entry. */
static short s_remapMappedFlag[16];

/* ROM SFX test table at 0x50421A — sound IDs indexed by g_optSfxTestSel.
 * Binary reads dword at [vol*2 + 0x50421A], then sar 16 → table[vol+1]. */
static const short s_sfxTestTable[] = {  /* 0x50421A, 44 entries */
    0x53, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 24, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 39, 45, 50, 51, 52, 53, 54, 55, 56,
};

/* SetPlayerCharacter — 0x004D9CF0 — 85 bytes
 * Set player character and lookup model data.
 * EAX=playerIndex, EDX=charId, EBX=raceType */
void SetPlayerCharacter(int playerIndex, int charId, int raceType)
{
    Player *p = &g_playerBase[playerIndex];

    p->charId = (short)charId;                               /* 0xF2 */
    *(short *)&p->selectCharId = (short)charId;              /* match char-select setup */
    p->modelCharId = 0;                                       /* clear gameplay leftover */
    p->_unk_0x1E0 = (short)charId;                          /* 0x1E0 */
    p->animId = (short)raceType;                             /* 0x98 */

    short sCharId = p->charId;
    uintptr_t *table = (uintptr_t *)g_charAnimTables;
    uintptr_t *perChar = (uintptr_t *)table[sCharId * 2];

    int animId = (int)p->animId;                             /* 0x98 */

    const short *entry = (const short *)perChar[animId];

    g_animDataPtrs[playerIndex] = entry;

    int frameCount = (int)(short)entry[0];
    p->animFrameIdx = frameCount - 1;                        /* 0x1EC */
}

/**
 * ScanKeyRemap — 0x00492DF4 — 244 bytes
 *
 * Joystick-remap button scanner. Called each frame from OptionsMenuScreen
 * while the joystick remap sub-page is active. Checks the active player's
 * joystick buttons (g_keyPressState[playerSlot*80..+nButtons]) for any
 * press; on the first newly-pressed button that hasn't already been bound
 * this session, assigns the current action's bit pattern (looked up in
 * g_remapActionBits[btnCount-3][progress]) to that button's slot in
 * g_optKeyRemapResult[], increments the progress counter, plays SFX, and
 * returns. Once progress reaches btnCount the dispatcher commits the
 * result buffer to the slot's per-button config.
 *
 * Args: slotPtr = &g_joystickSlots[g_optKeyRemapPlayer][0] (282-byte slot).
 *   slot[0x118] (short) = button count (filled by SyncJoystickSlots from
 *   g_joystickDeviceFlags, which platform_init_gamepads writes from
 *   SDL_JoystickNumButtons).
 */
static void ScanKeyRemap(char *slotPtr)
{
    int btnCount   = *(short *)(slotPtr + 0x118);
    int playerSlot = g_optKeyRemapPlayer;
    int pressBase  = playerSlot * 80;            /* g_keyPressState stride */

    /* Two modes:
     *   flag != 0: a button was just captured; wait until ALL buttons
     *              are released, then clear flag (binary 0x492EBA-0x492EE2).
     *   flag == 0: scan for first press, capture it, set flag.
     * This forces the user to release between captures so a held-down
     * button can't accidentally bind to multiple actions in one frame. */
    if (g_optKeyRemapFlag != 0) {
        for (int btn = 0; btn < btnCount; btn++) {
            if (g_keyPressState[pressBase + btn] != 0) {
                return;  /* still held */
            }
        }
        g_optKeyRemapFlag = 0;
        return;
    }

    for (int btn = 0; btn < btnCount; btn++) {
        if (g_keyPressState[pressBase + btn] == 0) {
            continue;
        }

        g_optKeyRemapFlag = 1;

        if (s_remapMappedFlag[btn] != 0) {
            continue; /* already bound this session — try next */
        }
        s_remapMappedFlag[btn] = 1;

        int cappedCount = btnCount > 6 ? 6 : btnCount;
        int row = cappedCount - 3;
        if (row < 0) {
            row = 0;
        }
        if (row > 3) {
            row = 3;
        }
        short bits = g_remapActionBits[row][g_optKeyRemapProgress];

        g_optKeyRemapResult[btn] = bits;

        g_optKeyRemapProgress++;
        PlaySoundEffect(2, 0, 0);
        return;
    }
}

/* ROM data: option menu page definitions — [startItem, endItem] per page.
 * Extracted from 0x0050286C in SONICR.EXE DGROUP. */
static const int __attribute__((unused)) s_optPageTable[][2] = {
    {0, 7},     /* page 0: main options */
    {8, 15},    /* page 1: game settings */
    {16, 22},   /* page 2: sound settings */
    {23, 30},   /* page 3: display settings */
    {99, 99},   /* page 4: unused */
    {31, 31},   /* page 5: single item (exit confirm) */
    {32, 36},   /* page 6: controller remap */
};

/* ROM data: per-item rendering info.
 * 5 ints per item: {tpageOffset, srcX, srcY, width, hasValue}
 * Extracted from 0x005028A4 in SONICR.EXE DGROUP. */
static const int s_optItemInfo[][5] = {
    /* Page 0: main menu items (items 0-7) — hasValue=0 */
    {1, 0, 64, 64, 0},     /* 0: Times */
    {1, 0, 80, 64, 0},     /* 1: Load/Save Data */
    {1, 0, 96, 64, 0},     /* 2: Graphics */
    {1, 0, 112, 64, 0},    /* 3: Sound */
    {1, 0, 128, 64, 0},    /* 4: Controls */
    {1, 0, 144, 64, 0},    /* 5: Game */
    {1, 0, 160, 64, 0},    /* 6: Exit to Windows */
    {1, 0, 176, 64, 0},    /* 7: Back */
    /* Page 1: game settings (items 8-15) — hasValue=1 */
    {1, 128, 64, 136, 1},  /* 8: g_difficultyConfig (0-2) */
    {1, 128, 80, 136, 1},  /* 9: g_ghostToggle (0-1) */
    {1, 128, 96, 136, 1},  /* 10: g_weatherConfig (0-3) */
    {1, 128, 112, 136, 1}, /* 11: g_catchUpToggle (0-1) */
    {1, 128, 128, 136, 1}, /* 12: g_guideToggle (0-1) */
    {1, 128, 144, 136, 1}, /* 13: g_minimapConfig (0-1) */
    {1, 128, 160, 136, 1}, /* 14: g_splitScreenMode (0-1) */
    {1, 128, 176, 136, 0}, /* 15: back (no value) */
    /* Page 2: sound settings (items 16-22) */
    {2, 128, 0, 136, 1},   /* 16: g_stereoEnabled (0-1) */
    {2, 128, 16, 136, 3},  /* 17: Sound Test selector (0-0x2A) */
    {2, 128, 32, 136, 3},  /* 18: Music Test selector (0-0x13) */
    {2, 128, 48, 136, 1},  /* 19: g_vocalsEnabled (0-1) */
    {2, 128, 64, 136, 2},  /* 20: SFX volume (0-8) */
    {2, 128, 80, 136, 2},  /* 21: music volume (0-8) — see g_optMusicVolume.
                            * DELIBERATE DIVERGENCE: the 1997 build draws this
                            * as an ON/OFF toggle (hasValue 1). Type 2 puts it
                            * on the same 0-8 readout as SFX Volume above, which
                            * is what the 2004 re-release shipped. */
    {1, 128, 176, 136, 0}, /* 22: back (no value) */
    /* Page 3: display settings (items 23-30) */
    {2, 128, 96, 136, 4},  /* 23: resolution (packed) */
    {2, 128, 240, 136, 1}, /* 24: color depth */
    {2, 128, 112, 136, 1}, /* 25: g_interlaceMode (0-1) */
    {2, 128, 128, 136, 1}, /* 26: g_qualityLevel (0-4) */
    {2, 128, 144, 136, 1}, /* 27: g_resolutionLevel (0-4) */
    {2, 128, 160, 136, 1}, /* 28: g_emeraldRenderFlag */
#ifdef SONICR_XBOX
    {2, 128, 128, 136, 1}, /* 29: Xbox — Draw Distance, moved here so it sits right above Back */
#else
    {1, 0, 208, 136, 1},   /* 29: g_optCfg_470 */
#endif
    {1, 128, 176, 136, 0}, /* 30: back (no value) */
    /* Items 31-36 */
    {1, 0, 192, 136, 1},   /* 31: exit confirm */
    {2, 128, 176, 64, 0},  /* 32: remap P1 KB1 */
    {2, 128, 192, 64, 0},  /* 33: remap P1 KB2 */
    {2, 128, 208, 64, 0},  /* 34: remap P2 */
    {2, 128, 224, 64, 0},  /* 35: detect pad */
    {1, 0, 176, 64, 0},    /* 36: back */
};

/* Forward declarations for option menu helpers */
/* =====================================================================
 * FindCurrentDisplayMode — 0x00492144 — 129 bytes
 * Searches the display mode list for one matching current width, height,
 * and color depth. Stores matched index to g_displayModeIndex (0x68b008).
 * ===================================================================== */
static int g_displayModeIndex;          /* 0x0068B008 */
static void *g_renderStateBase;         /* 0x0063ED90 */

static void FindCurrentDisplayMode(void)
{
    int width = g_screenWidth;
    int height = g_screenHeight;

    g_displayModeIndex = 0;
    int depth = (g_bitsPerPixel == 8) ? 8 : 16;

    char *obj = (char *)g_renderStateBase;  /* 0x0063ED90 */
    if (obj == NULL) {
        return;  /* no DirectDraw on SDL */
    }
    int *entry = (int *)(obj + 0x234);

    for (int i = 0; i < 128; i++) {
        if (entry[0] == width && entry[1] == height && depth == entry[2]) {
            g_displayModeIndex = i;
            break;
        }
        entry = (int *)((char *)entry + 0xc);
    }
    g_screenWidth = width;
    g_screenHeight = height;
}

/* Options page descriptor table — ROM 0x50286C, 8-byte stride.
 * Each entry is {firstItem, lastItem} into the 37-entry s_optItemInfo list;
 * switching pages just repoints the cursor at a different span.
 * Page 4 is {99,99} in ROM and is never referenced by any caller. */
static const int s_optPageItems[7][2] = {
    {  0,  7 },   /* 0: main options list */
    {  8, 15 },   /* 1: Game */
    { 16, 22 },   /* 2: Sound */
#ifdef SONICR_XBOX
    { 29, 30 },   /* 3: Graphics — Draw Distance (item 29 here) + Back */
#else
#ifdef SONICR_XBOX
    { 29, 30 },   /* 3: Graphics — Draw Distance (shown as item 29) + Back */
#else
    { 23, 30 },   /* 3: Graphics */
#endif
#endif
    { 99, 99 },   /* 4: unused */
    { 31, 31 },   /* 5: Exit to Windows confirm (YES/NO) */
#ifdef SONICR_XBOX
    { 35, 36 },   /* 6: Controls — pad only (Set Joystick, Back) */
#else
    { 32, 36 },   /* 6: Controls */
#endif
};

/* =====================================================================
 * InitOptionsMenuPage — 0x00493820 — 140 bytes
 * Initializes an options menu page from a ROM page descriptor table.
 * EAX = page index, EDX = base scroll position
 * ROM table at 0x50286C: 8-byte stride {firstItem, lastItem} per page.
 * Computes item count, scroll range, and centering offset.
 * ===================================================================== */
static int IsDisabledGraphicsItem(int itemIndex);

void InitOptionsMenuPage(int pageIndex, int basePos)  /* EAX, EDX */
{
    g_optCurrentPage = pageIndex;
    g_optMenuInputGateG = 1;
    g_optMenuScrollFlag = 1;

    int start = s_optPageItems[pageIndex][0];
    int length = s_optPageItems[pageIndex][1];

    g_optMenuCursor = basePos + start;
    /* Land on the first selectable row (Graphics opens with blank rows). */
    while (g_optMenuCursor < length && IsDisabledGraphicsItem(g_optMenuCursor)) {
        g_optMenuCursor++;
    }
    g_optMenuMaxItem = length;

    int diff = length - start;                              /* eax after sub */
    int count = diff + 1;                                   /* edx = eax + ecx(1) */
    g_optMenuItemCount = count;

    /* Binary centering: shl eax,2 + shl edx,4 then sar 1, sub from 0x8E */
    int totalWidth = diff * 4 + count * 16;                 /* 0x493860-0x493866 */
    int center = 0x8E - (totalWidth + (totalWidth >> 31)) / 2; /* abs + sar 1 */
    g_optMenuCenter = center;

    /* Compute scroll position: center + (position - start) * 20 */
    int scrollPos = center + (g_optMenuCursor - start) * 20;
    g_optMenuSelected = start;                              /* 0x493899: [0x925294] = ebx (= start) */
    g_optMenuScrollTgt = scrollPos;
    g_optMenuScrollCur = scrollPos;
}

/* =====================================================================
 * OptionsMenuScrollUp — 0x004938AC — 269 bytes
 * Scrolls the option menu cursor UP, skipping disabled items.
 * Checks various conditions to determine if the previous item
 * should be skipped (e.g., items that depend on hardware mode).
 * ===================================================================== */
static void OptionsMenuScrollUp(void)
{
    int driverMode = g_renderMode;            /* 0x6DD860 */
    int cdFlag     = g_cdAvailable;           /* 0x504278 */
    int soundFlag  = (g_lpDirectSound != NULL); /* 0x6D9AE8 — sound system active */
    int screenH    = g_screenHeight;          /* 0x6E989C */
    int joyFlag    = g_initFeatureC;          /* 0x675C1C */
    int cursor;

    for (;;) {
        int skip = 0;
        cursor = g_optMenuCursor;

        /* Item 3: skip if no CD AND no sound system */
        if (cursor == 3 && cdFlag == 0 && soundFlag == 0) {
            skip = 1;
        }

        /* Blanked Graphics rows are not selectable (the callers undo the move
         * when nothing selectable lies in that direction). */
        if (IsDisabledGraphicsItem(cursor)) {
            skip = 1;
        }

        /* Item 0x19 (25): available only in software mode with double-width or
         * high-res — 0x4938f3: cmp edx, 2 */
        if (cursor == 0x19) {
            if (driverMode == RENDER_SOFT && (g_doubleWidthFlag != 0 || screenH > 0xF0)) {
                ;  /* don't skip — item available */
            }
            else {
                skip = 1;
            }
        }

        /* Item 0x23 (35): skip if no joystick */
        if (cursor == 0x23 && joyFlag == 0) {
            skip = 1;
        }

        /* Items 0x11 (17) or 0x14 (20): skip if no sound system */
        if ((cursor == 0x11 || cursor == 0x14) && soundFlag == 0) {
            skip = 1;
        }

        /* Items 0x12 (18), 0x13 (19), or 0x15 (21): skip if no CD */
        if ((cursor == 0x12 || cursor == 0x13 || cursor == 0x15) && cdFlag == 0) {
            skip = 1;
        }

        /* The binary (0x493960 / 0x493977: cmp edx, 1) also skips items 0x18 /
         * 0x1c / 0x1d when g_renderMode == RENDER_D3D — those are the
         * DirectDraw-era software toggles, hidden when hardware is driving.
         * We run RENDER_SOFT, so the gate is never true and the rows stay. */

        if (skip) {
            g_optMenuCursor--;
            g_optMenuScrollTgt -= 0x14;
        }
        else {
            break;
        }
    }
}

/* =====================================================================
 * OptionsMenuScrollDown — 0x004939BC — 269 bytes
 * Mirror of ScrollUp — scrolls cursor DOWN, skipping disabled items.
 * ===================================================================== */
static void OptionsMenuScrollDown(void)
{
    int driverMode = g_renderMode;            /* 0x6DD860 */
    int cdFlag     = g_cdAvailable;           /* 0x504278 */
    int soundFlag  = (g_lpDirectSound != NULL); /* 0x6D9AE8 — sound system active */
    int screenH    = g_screenHeight;          /* 0x6E989C */
    int joyFlag    = g_initFeatureC;          /* 0x675C1C */
    int cursor;

    for (;;) {
        int skip = 0;
        cursor = g_optMenuCursor;

        /* Item 3: skip if no CD AND no sound system */
        if (cursor == 3 && cdFlag == 0 && soundFlag == 0) {
            skip = 1;
        }

        /* Blanked Graphics rows are not selectable (the callers undo the move
         * when nothing selectable lies in that direction). */
        if (IsDisabledGraphicsItem(cursor)) {
            skip = 1;
        }

        /* Item 0x19 (25): available only in software mode with double-width or
         * high-res — 0x4938f3: cmp edx, 2 */
        if (cursor == 0x19) {
            if (driverMode == RENDER_SOFT && (g_doubleWidthFlag != 0 || screenH > 0xF0)) {
                ;  /* don't skip — item available */
            }
            else {
                skip = 1;
            }
        }

        /* Item 0x23 (35): skip if no joystick */
        if (cursor == 0x23 && joyFlag == 0) {
            skip = 1;
        }

        /* Items 0x11 (17) or 0x14 (20): skip if no sound system */
        if ((cursor == 0x11 || cursor == 0x14) && soundFlag == 0) {
            skip = 1;
        }

        /* Items 0x12 (18), 0x13 (19), or 0x15 (21): skip if no CD */
        if ((cursor == 0x12 || cursor == 0x13 || cursor == 0x15) && cdFlag == 0) {
            skip = 1;
        }

        /* The binary (0x493960 / 0x493977: cmp edx, 1) also skips items 0x18 /
         * 0x1c / 0x1d when g_renderMode == RENDER_D3D — those are the
         * DirectDraw-era software toggles, hidden when hardware is driving.
         * We run RENDER_SOFT, so the gate is never true and the rows stay. */

        if (skip) {
            g_optMenuCursor++;
            g_optMenuScrollTgt += 0x14;
        }
        else {
            break;
        }
    }
}

/* Graphics page (page 3, items 23-30) rows this port does not implement:
 * Resolution (23), Color (24), Interlace (25), Window (27), Track Shading
 * (28) and Alpha Blending (29). The window and pixel format are settled
 * outside the game, and the last two are unconditionally on in the D3D/GL/PVR
 * path. These rows draw empty and ignore left/right; the cursor still stops
 * on them so the page keeps its original eight-row spacing.
 * DELIBERATE DIVERGENCE — all platforms. */
static int IsDisabledGraphicsItem(int itemIndex)
{
    switch (itemIndex) {
        case 23:
        case 24:
        case 25:
        case 27:
        case 28:
#ifdef SONICR_XBOX
        case 26:   /* Draw Distance lives in slot 29 on this build */
#else
        case 29:
#endif
            return 1;
        default:
            return 0;
    }
}

/* =====================================================================
 * DrawOptionItem — 0x0049257C — 2166 bytes
 * Draws one option menu item: label sprite + value indicator.
 *
 * Parameters (Watcom fastcall):
 *   EAX = X position (in virtual 320-wide coords)
 *   EDX = item index (0-36)
 * ===================================================================== */
static void DrawOptionItem(int xPos, int itemIndex)
{
    if (IsDisabledGraphicsItem(itemIndex)) {
        return;
    }

#ifdef SONICR_DC
    /* Item 6 (page 0) is "Exit to Windows". On DC there is no host desktop to
     * return to, so the row is drawn empty — the layout and cursor spacing are
     * kept intact and the cursor still stops here. Selecting it is a no-op
     * (see the OptionsMenuScreen select handler). */
    if (itemIndex == 6) {
        return;
    }
#endif

    const int *info = s_optItemInfo[itemIndex]; /* 5 ints: tpage, srcX, srcY, width, hasValue */
    int tpageOff = info[0];
    int srcX = info[1];
    int srcY = info[2];
    int itemW = info[3];
    int hasValue = info[4];

    /* Draw the item label sprite */
    int dstX = 0x140 - itemW * 2;  /* 320 - width*2 — right-justified */

    /* Quad path — the binary's D3D branch, kept for the GL backend */
    DrawTexturedQuad(dstX, xPos * 2, 0x43FA0000,  /* depth = 500.0f */
                     0x100, 0x20,                   /* dstW=256, dstH=32 */
                     g_uiTexPage + tpageOff,
                     srcX, srcY, 0x80, 0x10,        /* src: 128x16 */
                     VERTEX_WHITE);

    /* Value rendering — only for items with hasValue != 0 */
    if (hasValue == 0) {
        return;
    }

    /* For the top-level page (items 0-7), hasValue is always 0,
     * so this code is not reached. Value rendering for sub-pages
     * (items 8-36) will be added when those pages are implemented.
     *
     * hasValue types:
     *   1 = binary toggle (ON/OFF icon from tpage)
     *   2 = numeric slider (0-8 range, draws bar + digits)
     *   3 = numeric display (2-digit number from digit sprites)
     *   4 = resolution string (WxH from display mode table)
     */

    /* Determine value and display position based on item */
    int value = -1;
    int uvBaseX = 0;      /* UV X for value icon */
    int uvBaseY = 0;      /* UV Y for value icon */
    int valueTpage = 1;   /* default tpage offset for value icons */

    if (itemIndex >= 8 && itemIndex <= 31) {
        int idx = itemIndex - 8;
#ifdef SONICR_XBOX
        if (itemIndex == 29) {
            idx = 26 - 8;   /* Draw Distance value */
        }
#endif
        switch (idx) {
            case 0: /* item 8: g_difficultyConfig — 3 states */
                value = g_difficultyConfig;
                valueTpage = 2;
                uvBaseY = 0;
                break;
            case 1: /* item 9: g_ghostToggle — 2 states */
                value = g_ghostToggle;
                uvBaseX = 0xC0;
                uvBaseY = 0xE0;
                break;
            case 2: /* item 10: g_weatherConfig — 4 states */
                value = g_weatherConfig;
                uvBaseX = 0xC0;
                uvBaseY = 0;
                break;
            case 3: /* item 11: g_catchUpToggle */
                value = g_catchUpToggle;
                uvBaseX = 0xC0;
                uvBaseY = 0xE0;
                break;
            case 4: /* item 12: g_guideToggle */
                value = g_guideToggle;
                uvBaseX = 0xC0;
                uvBaseY = 0xE0;
                break;
            case 5: /* item 13: g_minimapConfig */
                value = g_minimapConfig;
                uvBaseX = 0xC0;
                uvBaseY = 0xE0;
                break;
            case 6: /* item 14: g_splitScreenMode */
                value = g_splitScreenMode;
                uvBaseX = 0xC0;
                uvBaseY = 0xC0;
                break;
            case 7: /* item 15: no value */
                break;
            case 8: /* item 16: g_stereoEnabled */
                value = g_stereoEnabled;
                uvBaseX = 0xC0;
                uvBaseY = 0xE0;
                break;
            case 9: /* item 17: SFX volume */
                value = g_optSfxTestSel;
                break;
            case 10: /* item 18: music volume */
                value = g_optMusicTestSel;
                break;
            case 11: /* item 19: g_vocalsEnabled */
                value = g_vocalsEnabled;
                uvBaseX = 0xC0;
                uvBaseY = 0xE0;
                break;
            case 12: /* item 20: SFX volume (0-8) */
                value = g_optSfxVolume;
                break;
            case 13: /* item 21: music volume (0-8) */
                value = g_optMusicVolume;
                break;
            case 15: /* item 23: resolution (packed) */
                value = g_optCfg_468;
                break;
            case 16: /* item 24: color depth */
                value = (g_bitsPerPixel != 8) ? 1 : 0;
                uvBaseY = 0xD0;
                valueTpage = 2;
                break;
            case 17: /* item 25: interlace — disabled in D3D */
                value = 0;
                uvBaseX = 0xC0;
                uvBaseY = 0xE0;
                break;
            case 18: /* item 26: quality level (0-4) */
                value = g_qualityLevel;
                uvBaseY = 0x30;
                valueTpage = 2;
                break;
            case 19: /* item 27: resolution level (0-4) */
                value = g_resolutionLevel;
                uvBaseY = 0x80;
                valueTpage = 2;
                break;
            case 20: /* item 28: DirectDraw-era toggle (Track Shading) */
                /* Vestigial in our GL/PVR path — the effect is always on.
                * Show ON (value 1) and keep it non-editable. The binary fed
                * g_renderMode here (=2 in software mode), which indexed off the
                * end of the ON/OFF strip and drew the stray "Random" glyph. */
                if (g_renderMode == RENDER_SOFT) {
                    value = 1;
                }
                else {
                    value = g_emeraldRenderFlag;
                }
                uvBaseX = 0xC0;
                uvBaseY = 0xE0;
                break;
            case 21: /* item 29: DirectDraw-era toggle (Alpha Blending) */
                /* Vestigial in our GL/PVR path — always on. Show ON (value 1),
                * non-editable. (Binary fed g_renderMode=2 → "Random" glyph.) */
                if (g_renderMode == RENDER_SOFT) {
                    value = 1;
                }
                else {
                    value = g_optCfg_470;
                }
                uvBaseX = 0xC0;
                uvBaseY = 0xE0;
                break;
            case 23: /* item 31: exit confirm flag */
                value = g_optMenuExitConfirm;
                uvBaseX = 0x80;
                uvBaseY = 0xC0;
                break;
            default:
                break;
        }
    }

    if (value == -1) {
        return;
    }

    /* Draw value indicator based on hasValue type */
    if (hasValue == 1) {
        /* Binary toggle: draw ON/OFF icon */
        int uvY = (value << 4) + uvBaseY;  /* value * 16 + base */
        DrawTexturedQuad(0x148, xPos * 2, 0x43FA0000,
                         0x80, 0x20,
                         g_uiTexPage + valueTpage,
                         uvBaseX, uvY, 0x40, 0x10,
                         VERTEX_WHITE);
    }
    else if (hasValue == 2) {
        /* Volume/slider: draw slider bar with position */
        int tens = value / 10;
        int ones = value % 10;

        /* Draw tens digit */
        DrawTexturedQuad(0x148, xPos * 2, 0x43FA0000,
                         0x18, 0x20,
                         g_uiTexPage + 2,
                         tens * 12, 0xF0, 0x0C, 0x10,
                         VERTEX_WHITE);
        /* Draw ones digit */
        DrawTexturedQuad(0x164, xPos * 2, 0x43FA0000,
                         0x18, 0x20,
                         g_uiTexPage + 2,
                         ones * 12, 0xF0, 0x0C, 0x10,
                         VERTEX_WHITE);
    }
    else if (hasValue == 3) {
        /* 2-digit numeric display (volume) */
        int displayVal = value + 1;
        int tens = displayVal / 10;
        int ones = displayVal % 10;

        DrawTexturedQuad(0x148, xPos * 2, 0x43FA0000,
                         0x18, 0x20,
                         g_uiTexPage + 2,
                         tens * 12, 0xF0, 0x0C, 0x10,
                         VERTEX_WHITE);
        DrawTexturedQuad(0x164, xPos * 2, 0x43FA0000,
                         0x18, 0x20,
                         g_uiTexPage + 2,
                         ones * 12, 0xF0, 0x0C, 0x10,
                         VERTEX_WHITE);
    }
    else if (hasValue == 4) {
        /* Resolution display: WxH from packed value */
        /* TODO: full digit rendering for resolution display */
    }
}

/* =====================================================================
 * DrawControlsRemapLabels — FUN_00493298 — 847 bytes
 *
 * Draws the keyboard remap page overlay: two column headers ("PLAYER 1",
 * "PLAYER 2"), 10 action label sprites per column, and a pulsing ESC
 * hint at the bottom. All sprites from tpage+3 (controls texture).
 * Called when g_optSubPageState != 0 (keyboard remap active).
 * ===================================================================== */
static void DrawControlsRemapLabels(void)
{
    int tpage3 = g_uiTexPage + 3;

    /* Column headers — "PLAYER 1" left, "PLAYER 2" right (UV row 0) */
    DrawTexturedQuad(0xBC, 0x6E, 0x43FA0000,
                     0x80, 0x18, tpage3,
                     0, 0, 0x40, 0x0C,
                     VERTEX_WHITE);
    DrawTexturedQuad(0x144, 0x6E, 0x43FA0000,
                     0x80, 0x18, tpage3,
                     0x40, 0, 0x40, 0x0C,
                     VERTEX_WHITE);

    /* Player number sub-headers (UV at 0xC0,0x38 / 0xC0,0x4A) */
    DrawTexturedQuad(0x10, 0x68, 0x43FA0000,
                     0x80, 0x24, tpage3,
                     0xC0, 0x38, 0x40, 0x12,
                     VERTEX_WHITE);
    DrawTexturedQuad(0x1F0, 0x68, 0x43FA0000,
                     0x80, 0x24, tpage3,
                     0xC0, 0x4A, 0x40, 0x12,
                     VERTEX_WHITE);

    /* Pulsing ESC hint — grayscale pulse from sine table */
    int sineVal = g_sinTable[((g_totalFrames & 0x3F) << 8) / 4];
    int val = sineVal / 269 + 0xC0;
    unsigned int pulseColor = 0xFF000000u
        | ((unsigned)val << 16)
        | ((unsigned)val << 8)
        | (unsigned)val;
#ifndef SONICR_XBOX   /* no ESC key on the console */
    DrawTexturedQuad(0x10, 0x1BA, 0x43FA0000,
                     0x48, 0x1A, tpage3,
                     0xDC, 0x6B, 0x24, 0x0D,
                     pulseColor);
#else
    (void)pulseColor;
#endif

    /* 10 action label rows — left (P1) and right (P2) columns */
    int yPos = 0x96;
    int uvY  = 0x0C;
    for (int i = 0; i < 10; i++) {
        DrawTexturedQuad(0xBC, yPos, 0x43FA0000,
                         0x80, 0x18, tpage3,
                         0, uvY, 0x40, 0x0C,
                         VERTEX_WHITE);
        DrawTexturedQuad(0x144, yPos, 0x43FA0000,
                         0x80, 0x18, tpage3,
                         0x40, uvY, 0x40, 0x0C,
                         VERTEX_WHITE);
        yPos += 0x1C;
        uvY  += 0x0C;
    }
}

/* =====================================================================
 * DrawJoystickRemapProgress — FUN_00492ee8 — 954 bytes
 *
 * Draws the joystick remap page overlay: player header, two column
 * headers, action sprites with blink for the current capture position,
 * and a pulsing ESC hint. Action UVs come from ROM table at 0x502B88.
 * Called when g_optKeyRemapState != 0 (joystick remap active).
 *
 * EAX = slotPtr (address of g_joystickSlots[player][0]).
 * ===================================================================== */

/* ROM table at 0x502B88 — joystick remap action UV sprites.
 * 4 configs (btnCount 3..6), each padded to 6 entries.
 * Each entry: [leftUvX, leftUvY, rightUvX, rightUvY]. */
static const int s_joyRemapUVs[4][6][4] = {
    /* btnCount=3 */
    { {0x00,0x48, 0x40,0x48}, {0x00,0x0C, 0x40,0x0C}, {0x00,0x54, 0x40,0x54},
      {0,0,0,0}, {0,0,0,0}, {0,0,0,0} },
    /* btnCount=4 */
    { {0x00,0x48, 0x40,0x48}, {0x00,0x0C, 0x40,0x0C}, {0x00,0x54, 0x40,0x54},
      {0x80,0x18, 0x40,0x60}, {0,0,0,0}, {0,0,0,0} },
    /* btnCount=5 */
    { {0x00,0x48, 0x40,0x48}, {0x00,0x0C, 0x40,0x0C}, {0x00,0x54, 0x40,0x54},
      {0x00,0x60, 0x40,0x60}, {0x80,0x18, 0x40,0x6C}, {0,0,0,0} },
    /* btnCount=6 */
    { {0x00,0x48, 0x40,0x48}, {0x00,0x0C, 0x40,0x0C}, {0x00,0x54, 0x40,0x54},
      {0x00,0x60, 0x40,0x60}, {0x00,0x6C, 0x40,0x6C}, {0x00,0x78, 0x40,0x78} },
};

static void DrawJoystickRemapProgress(char *slotPtr)
{
    int tpage3 = g_uiTexPage + 3;

    /* Read button count from slot[0x118] — binary reads dword at 0x116,
     * shifts right 16 to get the high word. */
    int btnCount = *(short *)(slotPtr + 0x118);
    int cappedCount = btnCount > 6 ? 6 : btnCount;
    if (cappedCount <= 0) {
        return;
    }

    /* Centering: baseY depends on button count */
    int baseY = 0xF8 - cappedCount * 16;
    int labelY = baseY + 0x30;
    int itemY = baseY + 0x58;

    /* Player header — UV Y depends on which player */
    int playerUvY = g_optKeyRemapPlayer * 16 + 0x38;
    DrawTexturedQuad(0x100, baseY, 0x43FA0000,
                     0x80, 0x20, tpage3,
                     0x80, playerUvY, 0x40, 0x10,
                     VERTEX_WHITE);

    /* Column headers — same as keyboard remap but at different X */
    DrawTexturedQuad(0xB0, labelY, 0x43FA0000,
                     0x80, 0x18, tpage3,
                     0, 0, 0x40, 0x0C,
                     VERTEX_WHITE);
    DrawTexturedQuad(0x150, labelY, 0x43FA0000,
                     0x80, 0x18, tpage3,
                     0x40, 0, 0x40, 0x0C,
                     VERTEX_WHITE);

    /* Pulsing ESC hint */
    int sineVal = g_sinTable[((g_totalFrames & 0x3F) << 8) / 4];
    int val = sineVal / 269 + 0xC0;
    unsigned int pulseColor = 0xFF000000u
        | ((unsigned)val << 16)
        | ((unsigned)val << 8)
        | (unsigned)val;
#ifndef SONICR_XBOX   /* no ESC key on the console */
    DrawTexturedQuad(0x10, 0x1BA, 0x43FA0000,
                     0x48, 0x1A, tpage3,
                     0xDC, 0x6B, 0x24, 0x0D,
                     pulseColor);
#else
    (void)pulseColor;
#endif

    /* Action label rows — ROM table indexed by (cappedCount-3) */
    int cfgIdx = cappedCount - 3;
    if (cfgIdx < 0) {
        cfgIdx = 0;
    }
    if (cfgIdx > 3) {
        cfgIdx = 3;
    }
    const int (*uvRow)[4] = s_joyRemapUVs[cfgIdx];

    int y = itemY;
    for (int i = 0; i < cappedCount; i++) {
        /* Blink: skip drawing the current capture position every other half-second */
        if (i == g_optKeyRemapProgress && (g_totalFrames & 0xF) < 8) {
            y += 0x20;
            continue;
        }
        DrawTexturedQuad(0xB0, y, 0x43FA0000,
                         0x80, 0x18, tpage3,
                         uvRow[i][0], uvRow[i][1], 0x40, 0x0C,
                         VERTEX_WHITE);
        DrawTexturedQuad(0x150, y, 0x43FA0000,
                         0x80, 0x18, tpage3,
                         uvRow[i][2], uvRow[i][3], 0x40, 0x0C,
                         VERTEX_WHITE);
        y += 0x20;
    }
}

/* LookupKeycapSpriteUV — 0x004935E8 — 50 bytes
 * Searches ROM table for a DirectInput scancode, returns the UV coordinates
 * of that key's cap sprite in the key glyph tpage. */
typedef struct { int scancode; int uvX; int uvY; } KeycapUVEntry;
static KeycapUVEntry s_scancodeToKeycapUV[] = {
    {0x02, 0x00, 0x84}, {0x03, 0x20, 0x84}, {0x04, 0x40, 0x84}, {0x05, 0x60, 0x84},
    {0x06, 0x80, 0x84}, {0x07, 0xA0, 0x84}, {0x08, 0xC0, 0x84}, {0x09, 0xE0, 0x84},
    {0x0A, 0x00, 0x90}, {0x0B, 0x20, 0x90}, {0x0C, 0x40, 0x90}, {0x0D, 0x60, 0x90},
    {0x0E, 0x80, 0x90}, {0x0F, 0xA0, 0x90}, {0x10, 0xC0, 0x90}, {0x11, 0xE0, 0x90},
    {0x12, 0x00, 0x9C}, {0x13, 0x20, 0x9C}, {0x14, 0x40, 0x9C}, {0x15, 0x60, 0x9C},
    {0x16, 0x80, 0x9C}, {0x17, 0xA0, 0x9C}, {0x18, 0xC0, 0x9C}, {0x19, 0xE0, 0x9C},
    {0x1A, 0x00, 0xA8}, {0x1B, 0x20, 0xA8}, {0x1C, 0x40, 0xA8}, {0x1E, 0x60, 0xA8},
    {0x1F, 0x80, 0xA8}, {0x20, 0xA0, 0xA8}, {0x21, 0xC0, 0xA8}, {0x22, 0xE0, 0xA8},
    {0x23, 0x00, 0xB4}, {0x24, 0x20, 0xB4}, {0x25, 0x40, 0xB4}, {0x26, 0x60, 0xB4},
    {0x27, 0x80, 0xB4}, {0x28, 0xA0, 0xB4}, {0x29, 0xC0, 0xB4}, {0x2A, 0xE0, 0xB4},
    {0x2B, 0x00, 0xC0}, {0x2C, 0x20, 0xC0}, {0x2D, 0x40, 0xC0}, {0x2E, 0x60, 0xC0},
    {0x2F, 0x80, 0xC0}, {0x30, 0xA0, 0xC0}, {0x31, 0xC0, 0xC0}, {0x32, 0xE0, 0xC0},
    {0x33, 0x00, 0xCC}, {0x34, 0x20, 0xCC}, {0x35, 0x40, 0xCC}, {0x36, 0x60, 0xCC},
    {0x1D, 0x80, 0xCC}, {0x38, 0xA0, 0xCC}, {0x39, 0xC0, 0xCC}, {0xB8, 0xE0, 0xCC},
    {0x9D, 0x00, 0xD8}, {0xD2, 0x20, 0xD8}, {0xD3, 0x40, 0xD8}, {0xC7, 0x60, 0xD8},
    {0xCF, 0x80, 0xD8}, {0xC9, 0xA0, 0xD8}, {0xD1, 0xC0, 0xD8}, {0xC8, 0xE0, 0xD8},
    {0xD0, 0x00, 0xE4}, {0xCB, 0x20, 0xE4}, {0xCD, 0x40, 0xE4}, {0xB5, 0x60, 0xE4},
    {0x37, 0x80, 0xE4}, {0x4A, 0xA0, 0xE4}, {0x4E, 0xC0, 0xE4}, {0x47, 0xE0, 0xE4},
    {0x48, 0x00, 0xF0}, {0x49, 0x20, 0xF0}, {0x4B, 0x40, 0xF0}, {0x4C, 0x60, 0xF0},
    {0x4D, 0x80, 0xF0}, {0x4F, 0xA0, 0xF0}, {0x50, 0xC0, 0xF0}, {0x51, 0xE0, 0xF0},
    {0x52, 0xA0, 0x78}, {0x53, 0xC0, 0x78}, {0x9C, 0xE0, 0x78},
    {  -1,    0,    0},
};

static int LookupKeycapSpriteUV(int scancode, int *outUvX, int *outUvY)
{
    KeycapUVEntry *entry = s_scancodeToKeycapUV;
    while (entry->scancode != -1) {
        if (entry->scancode == scancode) { *outUvX = entry->uvX; *outUvY = entry->uvY; return 1; }
        entry++;
    }
    return 0;
}

/* =====================================================================
 * DrawKeyBindingItems — 0x49361c — 293 bytes
 *
 * Draws 10 key binding sprites for one page of the key config screen.
 * For each item, looks up the bound scancode in the key map array
 * (0x676084), finds display UV coordinates via ROM table (0x4FECBC),
 * then draws a textured quad (D3D) or software sprite.
 *
 * The currently selected item blinks: visible when (g_totalFrames & 0xF) >= 8,
 * with overridden UVs (0x80, 0x78) when (g_totalFrames & 0x1F) < 0x10.
 *
 * EAX = page (0=P1, 1=P2). Watcom fastcall.
 * ===================================================================== */
void DrawKeyBindingItems(int page)
{
    int xPos = (page == 0) ? 0x30 : 0x210;              /* 0x4d5a02..0x4d5a3a */
    int yPos = 0x96;                                     /* esi = 0x96 */
    int tableOffset = page * 40;                         /* page*5*8 = page*40 */

    for (int i = 0; i < 10; i++) {                          /* loop 0..9 */
        /* Look up bound key's sprite UVs from ROM table */
        int uvX, uvY;
        int key = g_keyMappingData[tableOffset / 4];                  /* [eax + 0x676084] */
        int found = LookupKeycapSpriteUV(key, &uvX, &uvY);      /* EDX=&uvX, EBX=&uvY */

        if (found) {
            /* Check if this is the selected item on the active page */
            if ((g_optSubPageState - 1 == page) &&      /* [0x68b014]-1 == page */
                (i == g_optSubPageCounter))              /* i == [0x68b018] */
            {
                /* Selected item: blink effect */
                int frameLow = g_totalFrames & 0xF;     /* 0x8fb68c & 0xF */
                if (frameLow < 8) {
                    /* Blink off phase — skip drawing */
                    yPos += 0x1C;
                    tableOffset += 4;
                    continue;
                }
                /* Blink on phase — check for UV override */
                int frameWide = g_totalFrames & 0x1F;   /* 0x8fb68c & 0x1F */
                if (frameWide < 0x10) {
                    uvY = 0x78;                          /* override UVs for highlight */
                    uvX = 0x80;
                }
            }

            /* Draw the key name sprite */
            int tpage = g_uiTexPage + 3;                 /* [0x8f6c48] + 3 */

            DrawTexturedQuad(xPos, yPos,
                0x43fa0000,                              /* depth (float 500.0 as int bits) */
                0x40, 0x18,                              /* width=64, height=24 */
                tpage, uvX, uvY,
                0x20, 0x0C,                              /* uvW=32, uvH=12 */
                VERTEX_WHITE);                      /* color */
        }

        yPos += 0x1C;                                    /* advance Y by 28 per item */
        tableOffset += 4;                                /* next key map entry */
    }
}

/* =====================================================================
 * OptionsMenuScreen — 0x00493BDC — 6100 bytes
 * The options/settings screen. Largest menu function.
 * Handles sound settings, controller config, display settings.
 *
 * Return codes: 10000=SwitchDriver, 10001=TimeRanking, others=standard
 * ===================================================================== */
int OptionsMenuScreen(void)
{
    DebugLog("\nOptionsScreen\n\n");

    /* Initialization */

    /* Background tint: R=0x40, G=0x60, B=0x80 — from binary 0x493bec-0x493c15 */
    g_bgTintR = 0x40;
    g_bgTintG = 0x60;
    g_bgTintB = 0x80;

    /* Load option menu textures — binary 0x493c20-0x493ce2 */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupMenuTexturesD3D();                                          /* 0x493c84 */
    LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);                     /* 0x493c8e */
    TintBackgroundTPage(g_bgTintR, g_bgTintG, g_bgTintB);           /* 0x493ca4 */
    LoadTPageRGB(g_uiTexPage + 1, PATH_MENU_OPTIONS0);
#ifdef SONICR_XBOX
    /* "Exit to Windows" makes no sense on the console: relabel it. */
    xb_patch_exit_to_dashboard((unsigned short *)g_tpagePixelBuf[g_uiTexPage + 1],
                               g_bitsPerPixel == 0x10);
#endif
    LoadTPageRGB(g_uiTexPage + 2, PATH_MENU_OPTIONS1);
    LoadTPageRGB(g_uiTexPage + 3, PATH_MENU_CONTROLS);
    /* Restore state 4 for loaded tpages (same pattern as MainMenuScreen) */
    for (int tp = 0; tp < 52; tp++) {
        if (g_tpageStateArray[tp] == 6 && g_tpagePixelBuf[tp] != NULL) {
            g_tpageStateArray[tp] = 4;
        }
    }
    ProcessTpageStates();                                                  /* 0x493cdd */
    FinalizeMenuTexturesD3D();                                        /* 0x493ce2 */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) ResumeCD();
#endif

    /* Initialize menu state — page 0, cursor at first item */
    InitOptionsMenuPage(0, 0);  /* 0x493ceb */

    /* Sync local state from the shared globals set by InitOptionsMenuPage */
    /* InitOptionsMenuPage writes to the 0x9252xx block which we mirror locally */

    g_optSubPageState = 0;                                      /* 0x68B014 = 0 */
    g_optKeyRemapState = 0;                                     /* 0x68B01C = 0 */
    g_optMenuInputGateG = 1;                                    /* 0x925290 = 1 */
    g_fadeState = FADE_IN;                                      /* 0x901C48 = 1 */
    g_optMenuReturnCode = 1;                                    /* 0x925418 = 1 */
    g_renderEnabled = 1;                                        /* 0x8FB81C = 1 */

    /* Audio init. 0x493D27 stores -1 to [0x68AFC8] = s_musicTestTrack — the
     * "no track selected" sentinel for the music test item, re-armed on every
     * entry. It is NOT [0x68AFCC] = g_optCurrentPage: writing there clobbered
     * the page InitOptionsMenuPage() just set, and since the back handler is
     * a switch on g_optCurrentPage, -1 fell through to default and the Back
     * button did nothing until a sub-page visit restored a real page. */
    s_musicTestTrack = -1;
    FindCurrentDisplayMode();  /* 0x492144 */

    /* Frame timing */
    unsigned int lastTime = timeGetTime() / 1000;
    int frameElapsed;

    /* Main loop */
    for (;;) {
        platform_pump_events();

        /* Frame timing — compute elapsed seconds for idle timeout */
        unsigned int now = timeGetTime() / 1000;
        int delta = (int)(now - lastTime);
        if (delta < 0) {
            delta = -delta;
        }
        frameElapsed = delta;

        /* Per-frame Music-Test poll — binary 0x493d80. When the track that was
           playing for the Music Test has ended or changed (GetLogicalCDTrack()
           no longer matches the requested track), clear it and stop the CD.
           The Sound menu has no background music — it is silent unless a test
           track is playing. A prior version mis-read this as a track-5 keep-
           alive, which forced track 5 every frame and clobbered every Music
           Test audition back to 5. */
        if (GetLogicalCDTrack() != s_musicTestTrack) {              /* 0x493d85 */
            s_musicTestTrack = -1;                                  /* 0x493d8d: [0x68afc8] = -1 */
            StopCD();                                               /* 0x493d97: 0x4d0264 */
        }

        /* Update fade if active */
        if (g_fadeState != 0) {
            UpdateFade();
        }

        /* Check for fade-out complete — 0x901c44 == -256 means fully black */
        if (g_fadeLevel <= -256) {
            /* DELIBERATE DIVERGENCE: settings persist on every options exit.
             * The binary's fade-out return (0x493db6) only loads the return
             * code and rets; its lone SaveGameSettings call is in the
             * Exit-to-Windows confirm handler at 0x494730. */
            SaveGameSettings();
            return g_optMenuReturnCode;
        }

        /* Store previous resolution level for change detection */
        g_splashPrevState = g_interlaceMode;

        /* Read input state */
        ReadInput();                                            /* 0x493dd5 */

        /* 0x493dda-0x493df4: while a CD track is playing the idle timer is
         * re-baselined every frame, so a Music Test audition is never cut
         * short by the 60-second timeout below.
         *
         * The binary spells "nothing playing" as -1 (0x493ddf: cmp eax,-1).
         * Both of our GetLogicalCDTrack implementations return 0 instead —
         * music_sdl.c on `s_logicalTrack == 0`, music_dc.c on
         * `s_currentTrack == 0`, and StopCD zeroes it on either platform. The
         * test has to use OUR sentinel, or it is true every frame and the
         * screen never times out at all. */
        if (GetLogicalCDTrack() != 0) {                         /* 0x493ddf */
            lastTime = timeGetTime() / 1000;                    /* 0x493df4 */
        }

        /* Input handling (only when fully visible / not fading) */
        if (g_fadeState == FADE_VISIBLE && g_optSubPageState == 0 && g_optKeyRemapState == 0) {

            /* UP button */
            if ((g_inputBits & 0x10) && !(g_inputBits & 0x20)) {  /* up pressed, not down */
                if (g_optMenuScrollTgt == g_optMenuScrollCur) {
                    if (g_optMenuCursor > g_optMenuSelected) {
                        int oldCur = g_optMenuCursor, oldTgt = g_optMenuScrollTgt;
                        g_optMenuCursor--;
                        g_optMenuScrollTgt -= 0x14;
                        OptionsMenuScrollUp();
                        if (g_optMenuCursor < g_optMenuSelected ||
                            IsDisabledGraphicsItem(g_optMenuCursor)) {
                            /* nothing selectable above — stay put */
                            g_optMenuCursor = oldCur;
                            g_optMenuScrollTgt = oldTgt;
                        }
                        else {
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                        }
                    }
                }
            }

            /* DOWN button */
            else if ((g_inputBits & 0x20) && !(g_inputBits & 0x10)) {
                if (g_optMenuScrollTgt == g_optMenuScrollCur) {
                    if (g_optMenuCursor < g_optMenuMaxItem) {
                        int oldCur = g_optMenuCursor, oldTgt = g_optMenuScrollTgt;
                        g_optMenuCursor++;
                        g_optMenuScrollTgt += 0x14;
                        OptionsMenuScrollDown();
                        if (g_optMenuCursor > g_optMenuMaxItem ||
                            IsDisabledGraphicsItem(g_optMenuCursor)) {
                            g_optMenuCursor = oldCur;
                            g_optMenuScrollTgt = oldTgt;
                        }
                        else {
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                        }
                    }
                }
            }

            /* LEFT/RIGHT buttons — determine direction */
            int direction = 0;
            if ((g_inputBits & 0x40) || (g_inputBits & 0x80)) {
                if (g_optMenuInputGateG == 0) {
                    if (g_optMenuScrollTgt == g_optMenuScrollCur) {
                        if ((g_inputBits & 0x40) && !(g_inputBits & 0x80)) {
                            direction = -1;
                        }
                        else if ((g_inputBits & 0x80) && !(g_inputBits & 0x40)) {
                            direction = 1;
                        }
                    }
                }
                g_optMenuInputGateG = 1;
            }
            else {
                g_optMenuInputGateG = 0;
            }

            /* Apply LEFT/RIGHT to current item value */
            if (direction != 0) {
                int item = g_optMenuCursor;

                /* Blanked Graphics rows take no input — item 27 in particular
                 * still has a live handler below (case 19, g_resolutionLevel). */
                if (IsDisabledGraphicsItem(item)) {
                    item = -1;
                }
#ifdef SONICR_XBOX
                else if (item == 29) {
                    item = 26;   /* slot 29 carries Draw Distance */
                }
#endif

                /* Only items 8-31 have adjustable values */
                if (item >= 8 && item <= 31) {
                    int idx = item - 8;
                    switch (idx) {
                        case 0: { /* item 8: g_difficultyConfig (0-2) */
                            int v = g_difficultyConfig + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 3) {
                                v = 2;
                            }
                            g_difficultyConfig = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        case 1: { /* item 9: g_ghostToggle (0-1) */
                            int v = g_ghostToggle + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 2) {
                                v = 1;
                            }
                            g_ghostToggle = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        case 2: { /* item 10: g_weatherConfig (0-3) */
                            int v = g_weatherConfig + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 4) {
                                v = 3;
                            }
                            g_weatherConfig = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        case 3: { /* item 11: g_catchUpToggle (0-1) */
                            int v = g_catchUpToggle + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 2) {
                                v = 1;
                            }
                            g_catchUpToggle = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        case 4: { /* item 12: g_guideToggle (0-1) */
                            int v = g_guideToggle + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 2) {
                                v = 1;
                            }
                            g_guideToggle = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        case 5: { /* item 13: g_minimapConfig (0-1) */
                            int v = g_minimapConfig + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 2) {
                                v = 1;
                            }
                            g_minimapConfig = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        case 6: { /* item 14: g_splitScreenMode (0-1) */
                            int v = g_splitScreenMode + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 2) {
                                v = 1;
                            }
                            g_splitScreenMode = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        /* case 7: item 15 — no value, skip */
                        case 8: { /* item 16: g_stereoEnabled (0-1) */
                            int v = g_stereoEnabled + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 2) {
                                v = 1;
                            }
                            g_stereoEnabled = v;
                            break;
                        }
                        case 9: { /* item 17: Sound Test selector (0-0x2A) */
                            int v = g_optSfxTestSel + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 0x2B) {
                                v = 0x2A;
                            }
                            g_optSfxTestSel = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        case 10: { /* item 18: Music Test selector (0-0x13) */
                            int v = g_optMusicTestSel + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 0x14) {
                                v = 0x13;
                            }
                            g_optMusicTestSel = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        case 11: { /* item 19: g_vocalsEnabled (0-1) */
                            int v = g_vocalsEnabled + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 2) {
                                v = 1;
                            }
                            g_vocalsEnabled = v;
                            break;
                        }
                        case 12: { /* item 20: SFX volume (0-8) */
                            int v = g_optSfxVolume + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 9) {
                                v = 8;
                            }
                            else {
                                PlaySoundEffect(1, 0, 0);
                                lastTime = timeGetTime() / 1000;
                            }
                            g_optSfxVolume = v;
                            SetAllSoundVolumes();            /* 0x4D0760 */
                            break;
                        }
                        case 13: { /* item 21: music volume (0-8) */
                            int v = g_optMusicVolume + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 9) {
                                v = 8;
                            }
                            else {
                                PlaySoundEffect(1, 0, 0);
                                lastTime = timeGetTime() / 1000;
                            }
                            g_optMusicVolume = v;
                            /* Every music gate in the tree reads g_musicEnabled,
                             * so keep it in step with the slider. */
                            g_musicEnabled = (v != 0);
                            Music_SetVolume(v);
                            if (v == 0) {
                                StopCD();
                            }
                            break;
                        }
                        /* case 14: item 22 — no left/right handler */
                        /* case 15: item 23 — display resolution change (stub for SDL) */
                        /* case 16: item 24 — color depth change (stub for SDL) */
                        case 17: /* item 25: interlace — software-only, no-op in D3D */
                            break;
                        case 18: { /* item 26: g_qualityLevel (0-4) */
                            int v = g_qualityLevel + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 5) {
                                v = 4;
                            }
                            g_qualityLevel = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        case 19: { /* item 27: g_resolutionLevel (0-4) */
                            int v = g_resolutionLevel + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 5) {
                                v = 4;
                            }
                            g_resolutionLevel = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        case 20: /* item 28: emerald render — software-only, no-op in D3D */
                            break;
                        case 21: /* item 29: render option — software-only, no-op in D3D */
                            break;
                        case 23: { /* item 31: g_optMenuExitConfirm (0-1) */
                            int v = g_optMenuExitConfirm + direction;
                            if (v < 0) {
                                v = 0;
                            }
                            if (v >= 2) {
                                v = 1;
                            }
                            g_optMenuExitConfirm = v;
                            PlaySoundEffect(1, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        }
                        default: break;
                    }
                }
            }

            /* CONFIRM / BACK button handling with debounce.
             * g_optMenuScrollFlag acts as a press gate:
             *   - When button first pressed (flag==0): fire action, set flag=1
             *   - While button held (flag!=0): block action
             *   - When released: clear flag to 0 */
            if (g_inputBits & 0x06) {  /* confirm (Space/B button) */
                if (g_optMenuScrollTgt == g_optMenuScrollCur && g_optMenuScrollFlag == 0) {
                    /* First frame of confirm press — handle action per item */
                    int item = g_optMenuCursor;
                    switch (item) {
                        case 0: /* Times → return 10001 (TimeRankingScreen) */
#if SONICR_DC
                            StopCD();
#endif
                            PlaySoundEffect(2, 0, 0);
                            g_optMenuReturnCode = 10001;
                            g_fadeState = FADE_OUT;
                            break;
                        case 1: /* Load/Save Data → return 10000 (LoadSaveScreen) */
#if SONICR_DC
                            StopCD();
#endif
                            PlaySoundEffect(2, 0, 0);
                            g_optMenuReturnCode = 10000;
                            g_fadeState = FADE_OUT;
                            break;
                        case 2: /* Graphics → page 3 */
                            PlaySoundEffect(2, 0, 0);
                            InitOptionsMenuPage(3, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        case 3: /* Sound → page 2 */
                            PlaySoundEffect(2, 0, 0);
                            InitOptionsMenuPage(2, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        case 4: /* Controls → page 6 — 0x4945f5 */
                            PlaySoundEffect(2, 0, 0);
                            InitOptionsMenuPage(6, 0);          /* 0x49461d */
                            lastTime = timeGetTime() / 1000;
                            break;
                        case 5: /* Game → page 1 */
                            PlaySoundEffect(2, 0, 0);
                            InitOptionsMenuPage(1, 0);
                            lastTime = timeGetTime() / 1000;
                            break;
                        case 6: /* Exit to Windows → confirm page — 0x49466d */
#ifdef SONICR_DC
                            /* No host desktop on DC; the row is blank and inert
                             * (see DrawOptionItem). Cursor still lands on it. */
                            break;
#else
                            PlaySoundEffect(2, 0, 0);           /* 0x494676 */
                            g_optMenuExitConfirm = 0;           /* 0x494684 */
                            /* Page 5 in the ROM page table is {31,31} — a
                             * single row holding the YES/NO toggle. */
                            InitOptionsMenuPage(5, 0);          /* 0x49468a */
                            lastTime = timeGetTime() / 1000;
                            break;
#endif
                        case 7: /* Back → return 1 (back to main menu) */
                            PlaySoundEffect(2, 0, 0);
                            g_optMenuReturnCode = 1;
                            g_fadeState = FADE_OUT;
                            break;
                        case 15: /* Back from Game page → main, cursor on Game */
                            PlaySoundEffect(0, 0, 0);
                            InitOptionsMenuPage(0, 5);
                            lastTime = timeGetTime() / 1000;
                            break;
                        case 17: { /* SFX Test — play test sound */
                            extern void SetupReplayData(void);
                            lastTime = timeGetTime() / 1000;
                            if (g_optSfxTestSel == 0x2A) {
                                SetupReplayData();           /* 0x4D06F8 — stop CD stream */
                            }
                            int sfxId = (int)s_sfxTestTable[g_optSfxTestSel + 1];
                            PlaySoundEffect(sfxId, 0, 0);
                            break;
                        }
                        case 18: { /* Music Test — play CD track */
                            lastTime = timeGetTime() / 1000;
                            int track = g_optMusicTestSel + 2;
                            s_musicTestTrack = track;        /* 0x49480a: [0x68afc8] = track */
                            UpdateCDPlayback(track);         /* 0x4D01AC */
                            break;
                        }
                        case 22: /* Back from Sound page → main, cursor on Sound */
                            PlaySoundEffect(0, 0, 0);
                            InitOptionsMenuPage(0, 3);
                            lastTime = timeGetTime() / 1000;
                            break;
                        case 30: /* Back from Graphics page → main, cursor on Graphics */
                            PlaySoundEffect(0, 0, 0);
                            InitOptionsMenuPage(0, 2);
                            lastTime = timeGetTime() / 1000;
                            break;
                        case 31: /* Exit confirm YES/NO — 0x4946f3 */
                            if (g_optMenuExitConfirm == 1) {    /* 0x4946f3 */
                                StopCD();                       /* 0x4946fc */
                                PlaySoundEffect(0x0E, 0, 0);    /* 0x494701 */
                                g_optMenuReturnCode = 99999;    /* 0x49470f: 0x1869F */
                                g_fadeSpeed = 3;                /* 0x494714 → 0x901C4C */
                                g_fadeState = FADE_OUT;         /* 0x494719 → 0x901C48 */
                                SaveGameSettings();             /* 0x494730 */
                            }
                            else {
                                PlaySoundEffect(0, 0, 0);       /* 0x49474a */
                                /* Main page, cursor back on Exit to Windows. */
                                InitOptionsMenuPage(0, 6);      /* 0x494756 */
                                lastTime = timeGetTime() / 1000;
                            }
                            break;
                        case 32: { /* Define controls — view current bindings (state 3, no scan) */
                            PlaySoundEffect(2, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            g_optSubPageState = 3;           /* binary 0x4948b3-0x4948d1 */
                            g_optMenuInputGateG = 1;
                            g_optSubPageCounter = -1;        /* sentinel — no key being captured */
                            break;
                        }
                        case 33: { /* Redefine Player 1 keys → state 1, save current to temp */
                            PlaySoundEffect(2, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            for (int k = 0; k < 10; k++) {
                                s_optKeyRemapTemp[k] = g_keyMappingData[k];
                            }
                            g_optSubPageState = 1;           /* binary 0x4948e6-0x494935 */
                            g_optMenuInputGateG = 1;          /* capture gate */
                            g_optEscGate = 1;                 /* ESC gate (binary 0x49493a) */
                            g_optSubPageCounter = 0;
                            break;
                        }
                        case 34: { /* Redefine Player 2 keys → state 2, save current to temp */
                            PlaySoundEffect(2, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            for (int k = 0; k < 10; k++) {
                                s_optKeyRemapTemp[k] = g_keyMappingData[10 + k];
                            }
                            g_optSubPageState = 2;           /* binary 0x494949-0x49499d */
                            g_optMenuInputGateG = 1;
                            g_optEscGate = 1;
                            g_optSubPageCounter = 0;
                            break;
                        }
                        case 35: { /* Detect / configure pad — enter joystick remap.
                                    * Translates binary 0x4949B2-0x494A9A.
                                    * Picks the joystick slot whose action bit
                                    * (high-byte bit 0x06 = pad bits 0x0600,
                                    * = Jump+altJump) is currently held — i.e.
                                    * the joystick the user just confirmed with.
                                    * If no joystick has action held, do nothing. */
                            int slot = -1;
                            if ((g_joySlotState[0] >> 8) & 6) {
                                slot = 0;
                            }
                            else if ((g_joySlotState[1] >> 8) & 6) {
                                slot = 1;
                            }
                            else if ((g_joySlotState[2] >> 8) & 6) {
                                slot = 2;
                            }
                            else if ((g_joySlotState[3] >> 8) & 6) {
                                slot = 3;
                            }
                            if (slot < 0) {
                                break;
                            }

                            PlaySoundEffect(2, 0, 0);
                            lastTime = timeGetTime() / 1000;
                            g_optKeyRemapProgress = 0;        /* 0x68b020 */
                            g_optMenuInputGateG   = 1;        /* gate1 */
                            g_optKeyRemapState    = 1;        /* 0x68b01c — active */
                            g_optKeyRemapFlag     = 1;        /* wait-for-release */
                            g_optKeyRemapPlayer   = slot;     /* 0x68b024 */
                            g_optEscGate          = 1;        /* gate2 */
                            for (int i = 0; i < 10; i++) {
                                g_optKeyRemapResult[i] = 0;
                                s_remapMappedFlag[i]   = 0;
                            }
                            break;
                        }
                        case 36: /* Back from Controls → main, cursor on Controls */
                            PlaySoundEffect(0, 0, 0);
                            InitOptionsMenuPage(0, 4);
                            lastTime = timeGetTime() / 1000;
                            break;
                        default: break;
                    }
                }
                g_optMenuScrollFlag = 1;
            }
            else if (g_inputBits & 0x01) {  /* back (A key / LTrigger) */
                if (g_optMenuScrollFlag == 0) {
                    /* First frame of back press — handle per page */
                    switch (g_optCurrentPage) {
                        case 0: /* back from main → exit options */
                            PlaySoundEffect(0, 0, 0);
                            g_optMenuReturnCode = 1;
                            g_fadeState = FADE_OUT;
                            break;
                        case 1: /* back from game settings → main, cursor on Game */
                            PlaySoundEffect(0, 0, 0);
                            InitOptionsMenuPage(0, 5);
                            break;
                        case 2: /* back from sound → main, cursor on Sound */
                            PlaySoundEffect(0, 0, 0);
                            InitOptionsMenuPage(0, 3);
                            break;
                        case 3: /* back from display → main, cursor on Graphics */
                            PlaySoundEffect(0, 0, 0);
                            InitOptionsMenuPage(0, 2);
                            break;
                        case 5: /* back from exit confirm → main, cursor on Exit */
                            PlaySoundEffect(0, 0, 0);
                            InitOptionsMenuPage(0, 6);
                            break;
                        case 6: /* back from controls → main, cursor on Controls */
                            PlaySoundEffect(0, 0, 0);
                            InitOptionsMenuPage(0, 4);
                            break;
                        default: break;
                    }
                }
                g_optMenuScrollFlag = 1;
            }
            else {
                /* No confirm/back pressed — clear debounce gate */
                g_optMenuScrollFlag = 0;
            }
        }

        /* =============================================================
         * Sub-page dispatcher — keyboard remap (states 1, 2) and the
         * "view bindings" mode (state 3). Translates binary
         * 0x494c54-0x494dec.
         *
         * State 1: P1 keyboard remap — capture 10 keys into slots 0..9
         * State 2: P2 keyboard remap — capture 10 keys into slots 10..19
         * State 3: read-only display of current bindings (no key scan)
         *
         * ESC at any time cancels and restores from s_optKeyRemapTemp.
         * After 10 keys captured, SaveKeyMappings() persists to KEYS.BIN
         * and we drop back to the controls page.
         * ============================================================= */
        if (g_fadeState == FADE_VISIBLE && g_optSubPageState != 0) {
            /* ESC = cancel — restore temp, return to controls page.
             * Two separate gates here:
             *   g_optEscGate (0x92529c): debounces the ESC press itself
             *   g_optMenuInputGateG (0x925290): debounces key capture inside
             *     ScanInputForAction. We must NOT touch the capture gate from
             *     the ESC path — case 33/34 set it to 1 on entry so that the
             *     SPACE/RETURN press used to enter the sub-page doesn't get
             *     captured as the first slot. */
            if (XB_CANCEL_PRESSED()) {
                if (g_optEscGate == 0) {
                    PlaySoundEffect(0, 0, 0);
                }
                if (g_optSubPageState == 1) {
                    for (int k = 0; k < 10; k++) {
                        g_keyMappingData[k] = s_optKeyRemapTemp[k];
                    }
                    InitOptionsMenuPage(6, 1);
                }
                else if (g_optSubPageState == 2) {
                    for (int k = 0; k < 10; k++) {
                        g_keyMappingData[10 + k] = s_optKeyRemapTemp[k];
                    }
                    InitOptionsMenuPage(6, 2);
                }
                else {
                    /* State 3: no temp to restore */
                    InitOptionsMenuPage(6, 0);
                }
                g_optSubPageState = 0;
                g_optEscGate = 1;
            }
            else {
                g_optEscGate = 0;
            }

            /* Key capture (only states 1 and 2 — state 3 is read-only) */
            if (g_optSubPageState != 0 && g_optSubPageState != 3) {
#if 0
                /* Slot order matches g_keyMappingData[] indices 0..9:
                 * 0=Start 1=Left 2=Right 3=Up 4=Down 5=Jump 6=Accel
                 * 7=DriftL 8=DriftR 9=LookBack — see reference_input_keymap.md */
                static const char *const s_actionName[10] = {
                    "Start", "Left", "Right", "Up", "Down",
                    "Jump",  "Accel","DriftL","DriftR","LookBack"
                };
#endif
                static int s_lastReportedSlot = -1;
                static int s_lastReportedState = -1;
                int slot = g_optSubPageCounter;
                if (s_lastReportedState != g_optSubPageState ||
                    s_lastReportedSlot  != slot)
                {
#if 0
                    fprintf(stderr, "[remap] P%d %s [%d/10] — press a key (ESC cancels)\n",
                            g_optSubPageState, s_actionName[slot], slot + 1);
#endif
                    s_lastReportedState = g_optSubPageState;
                    s_lastReportedSlot  = slot;
                }

                int scanned = ScanInputForAction();
                if (scanned != -1) {
                    PlaySoundEffect(2, 0, 0);
                    int slotBase = (g_optSubPageState - 1) * 10;
                    g_keyMappingData[slotBase + g_optSubPageCounter] = scanned;
#if 0
                    fprintf(stderr, "[remap] captured DIK 0x%02X for P%d %s\n",
                            scanned, g_optSubPageState, s_actionName[slot]);
#endif
                    g_optSubPageCounter++;
                    if (g_optSubPageCounter >= 10) {
                        SaveKeyMappings();
#if 0
                        fprintf(stderr, "[remap] P%d done — saved KEYS.BIN\n",
                                g_optSubPageState);
#endif
                        InitOptionsMenuPage(6, g_optSubPageState);
                        g_optSubPageState = 0;
                        s_lastReportedState = -1;
                        s_lastReportedSlot  = -1;
                    }
                }
            }

            /* Idle timeout in remap — also restores and exits options.
             * Binary writes the LAST captured key into the next slot but
             * doesn't increment counter — effectively a no-op partial
             * write. We just skip that and exit cleanly. */
            if (frameElapsed > 60) {
                PlaySoundEffect(0, 0, 0);
                g_optMenuReturnCode = 1;
                g_fadeState = FADE_OUT;
            }
        }

        /* =============================================================
         * Sub-page dispatcher — joystick remap. Translates binary
         * 0x494dec-0x494eca. Active while g_optKeyRemapState != 0.
         *
         * Each frame: call ScanKeyRemap to capture one button press;
         * once g_optKeyRemapProgress reaches the (capped) button count,
         * commit g_optKeyRemapResult[] into the per-slot config at
         * slot[0x104..0x116] and exit the sub-page. ESC cancels without
         * committing. Slot config wasn't modified during capture, so no
         * temp restore is needed (unlike the keyboard remap path).
         * ============================================================= */
        if (g_fadeState == FADE_VISIBLE && g_optKeyRemapState != 0) {
            int playerSlot = g_optKeyRemapPlayer;
            char *slotPtr = (char *)&g_joystickSlots[playerSlot][0];
            int btnCount = *(short *)(slotPtr + 0x118);

            if (XB_CANCEL_PRESSED()) {
                /* ESC = cancel — return to controls page, no commit */
                if (g_optEscGate == 0) {
                    PlaySoundEffect(0, 0, 0);
                    InitOptionsMenuPage(6, XB_PAD_ITEM_OFS);
                    g_optKeyRemapState = 0;
#if 0
                    fprintf(stderr, "[joy-remap] cancelled\n");
#endif
                }
                g_optEscGate = 1;
            } else {
                g_optEscGate = 0;
#if 0
                /* Progress reporting (parallels the keyboard path) */
                static const char *const s_jrAction[6] = {
                    "ACTION (Jump)", "PAUSE (Start)", "ACCEL",
                    "L.BRAKE",       "R.BRAKE",       "CAMERA"
                };
#endif
                static int s_jrLastReported = -1;
                int progress = g_optKeyRemapProgress;
                int cappedCount = btnCount > 6 ? 6 : btnCount;
                if (progress < cappedCount && s_jrLastReported != progress) {
#if 0
                    fprintf(stderr,
                        "[joy-remap] slot %d, %s [%d/%d] — press a button (ESC cancels)\n",
                        playerSlot, s_jrAction[progress], progress + 1, cappedCount);
#endif
                    s_jrLastReported = progress;
                }

                ScanKeyRemap(slotPtr);

                /* Commit when all actions captured.
                 *
                 * Zeroing the untouched entries is deliberate — it is what
                 * stops a stale binding lingering when an action is moved from
                 * one button to another. But the remap only ever captures the
                 * six ACTIONS in g_remapActionBits (jump / start / accel /
                 * driftL / driftR / camera); no direction is ever assigned. So
                 * a button holding a DIRECTION is not something this pass can
                 * have rebound, and wiping it just destroys it.
                 *
                 * That is exactly what killed the D-pad: on a Logitech Dual
                 * Action the D-pad reports as buttons 6-9, all inside the ten
                 * entries committed here and none of them among the six
                 * prompts, so every remap zeroed all four. (It survived when
                 * the pad's mode switch had the D-pad reporting as a hat
                 * instead — platform_sdl.c reads that separately with
                 * hardcoded bits, bypassing this table.)
                 *
                 * So: overwrite whatever the user actually bound, and leave a
                 * direction entry alone when they did not touch that button. */
                if (g_optKeyRemapProgress >= cappedCount) {
                    short *dst = (short *)(slotPtr + 0x104);
                    for (int i = 0; i < 10; i++) {
                        if (g_optKeyRemapResult[i] == 0 &&
                            (dst[i] & PAD_DIRECTIONS) != 0)
                        {
                            continue;   /* untouched direction — keep it */
                        }
                        dst[i] = g_optKeyRemapResult[i];
                    }
#if 0
                    fprintf(stderr,
                        "[joy-remap] complete — committed %d button(s) to slot %d\n",
                        cappedCount, playerSlot);
#endif
                    SavePadTypesImpl();
                    InitOptionsMenuPage(6, XB_PAD_ITEM_OFS);
                    g_optKeyRemapState = 0;
                    g_optEscGate       = 1;
                    s_jrLastReported   = -1;
                }
            }

            if (frameElapsed > 60) {
                PlaySoundEffect(0, 0, 0);
                g_optMenuReturnCode = 1;
                g_fadeState = FADE_OUT;
            }
        }

        /* Idle timeout — 0x494c1e. This is the tail of the gated input block
         * above: the three guards at 0x493df7 / 0x493e04 / 0x493e11 all jump
         * to 0x494c46, which is past this check. Repeating the gate here fires
         * the timeout exactly once, on the frame it expires — the fade-out it
         * starts then suppresses it while lastTime keeps advancing. */
        if (g_fadeState == FADE_VISIBLE && g_optSubPageState == 0 &&
            g_optKeyRemapState == 0 && frameElapsed > 60) {
            PlaySoundEffect(0, 0, 0);                           /* 0x494c2c */
            g_optMenuReturnCode = 1;                            /* 0x494c3b */
            g_fadeState = FADE_OUT;                             /* 0x494c40 */
        }

        /* Smooth scroll interpolation */
        if (g_optSubPageState == 0) {
            int cur = g_optMenuScrollCur;
            int tgt = g_optMenuScrollTgt;
            if (cur < tgt) {
                cur += 4;
                if (cur > tgt) {
                    cur = tgt;
                }
                g_optMenuScrollCur = cur;
            }
            else if (cur > tgt) {
                cur -= 4;
                if (cur < tgt) {
                    cur = tgt;
                }
                g_optMenuScrollCur = cur;
            }
        }

        /* Rendering */

        /* Quad path — the binary's D3D branch, kept for the GL backend */
        ProcessTpageStates();
        BeginFrame();
        RenderBackground();
        EndFrame();

        BeginFrame();
        RenderWavingMenuBackground();                         /* 0x004C68B8 — software twin */

        int tpage = g_uiTexPage + 1;

        /* Background texture (two halves spanning 640 virtual pixels) */
        DrawTexturedQuad(0, 0x18, 0x447A0000,
                         0x140, 0x40, tpage,
                         0, 0, 0xA0, 0x20,
                         VERTEX_WHITE);
        DrawTexturedQuad(0x140, 0x18, 0x447A0000,
                         0x140, 0x40, tpage,
                         0, 0x20, 0xA0, 0x20,
                         VERTEX_WHITE);

        if (g_optSubPageState != 0) {
            /* Keyboard remap — action labels + key binding glyphs */
            DrawControlsRemapLabels();
            DrawKeyBindingItems(0);
            DrawKeyBindingItems(1);
        }
        else if (g_optKeyRemapState != 0) {
            /* Joystick remap — action progress with blink */
            char *slotPtr = (char *)&g_joystickSlots[g_optKeyRemapPlayer][0];
            DrawJoystickRemapProgress(slotPtr);
        }
        else {
            /* Normal menu — items + cursor */
            int count = g_optMenuItemCount;
            int xOff = 0;
            for (int i = 0; i < count; i++) {
                int itemIdx = g_optMenuSelected + i;
                int itemX = g_optMenuCenter + xOff;
                DrawOptionItem(itemX, itemIdx);
                xOff += 0x14;
            }

            int selItem = g_optMenuSelected;
            int itemWidth = s_optItemInfo[selItem][3];
            int scrollPos = g_optMenuScrollCur;
            int cursorX = 0x140 - itemWidth * 2 - 4;
            int cursorY = (scrollPos - 2) * 2;

            DrawTexturedQuad(cursorX, cursorY, 0x433E0000,
                             0x108, 0x28, tpage,
                             0, 0xEC, 0x84, 0x14,
                             VERTEX_WHITE);
        }

        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }
        EndFrame();
        FlipD3D();

        g_totalFrames2++;
        g_totalFrames++;

        /* FPS cap — wait for 30fps.
         * WaitForFrameCap uses g_currentTime as the frame start reference. */
        g_currentTime = timeGetTime();
        WaitForFrameCap();
    }
}

/* =====================================================================
 * LoadSaveScreen — 0x0048F984 — 2945 bytes
 *
 * Load/Save data screen. Shows 10 save slots with character unlock
 * icons and file status. Three bottom buttons: SAVE (col 0), LOAD
 * (col 1), NEW (col 2). Supports scrolling through slots vertically
 * and selecting action horizontally.
 *
 * Helpers:
 *   LoadSaveScreenInit  — 0x0048F724 — 424 bytes  (texture load, file I/O)
 *   RenderSlotD3D       — 0x0048EBD4 — 1351 bytes (per-slot D3D render)
 *   LoadConfigSlot        — 0x0048F954 — 48 bytes   (load slot → g_saveBlock)
 *   SaveConfigSlot      — 0x0048F8CC — 136 bytes  (save g_saveBlock → file)
 * ===================================================================== */

static void *g_fileHandle2;             /* 0x00625C00 — transient file handle (keys.bin write) */

/* Save slot data — defined in leaf_small.c */
uint32_t __attribute__((aligned(32))) g_configSlotStorage[16][0x8a8 / 4 + 1]; /* 16 slots × 0x8a8 bytes */
void *g_configPtrTable[16];                   /* 0x0068AF44 */
extern const char *s_saveFilenames[10];           /* ROM at 0x502818 */

extern void InitDefaultTimeTables(void);          /* 0x00470DC8 — reset save data to defaults */
extern void LoadAllGhostTimes(void);              /* 0x0042ED7C */

const char *s_saveFilenames[10] = {  /* ROM at 0x502818 */
    "SAVE/R01.SAV", "SAVE/R02.SAV", "SAVE/R03.SAV", "SAVE/R04.SAV",
    "SAVE/R05.SAV", "SAVE/R06.SAV", "SAVE/R07.SAV", "SAVE/R08.SAV",
    "SAVE/R09.SAV", "SAVE/R10.SAV",
};

/* ---- Port addition: automatic save / load ---------------------------
 * The original only saves when the player picks SAVE on the Load/Save
 * screen. The slot last loaded or saved there is remembered (SONICR.INF);
 * at start-up its progress is loaded, and after every race the progress is
 * written back to it if it changed. */
extern int g_lastSaveSlot;
extern void SaveGameSettings(void);

int SaveConfigSlot(int slot);

static void SetLastSaveSlot(int slot)
{
    if (g_lastSaveSlot != slot) {
        g_lastSaveSlot = slot;
        SaveGameSettings();
    }
}

void AutoLoadLastSave(void)
{
    if (g_lastSaveSlot < 0 || g_lastSaveSlot >= 10) return;
    FILE *fp = fOpen(s_saveFilenames[g_lastSaveSlot], "rb");
    if (fp == NULL) return;
    int buf[554];
    int ok = fRead(buf, 554 * 4, 1, fp) == 1;
    fClose(fp);
    if (!ok) return;
    bswap32_arr(buf, 554);
    memcpy(g_saveBlock, buf, sizeof(buf));
    g_configPtrTable[g_lastSaveSlot] = g_configSlotStorage[g_lastSaveSlot];
    memcpy(g_configPtrTable[g_lastSaveSlot], buf, sizeof(buf));
    LoadAllGhostTimes();
    DebugLog("Save: loaded slot %d automatically\n", g_lastSaveSlot + 1);
}

void AutoSaveProgress(void)
{
    if (g_lastSaveSlot < 0 || g_lastSaveSlot >= 10) return;
    void *prev = g_configPtrTable[g_lastSaveSlot];
    if (prev != NULL && memcmp(prev, g_saveBlock, 554 * 4) == 0) return;   /* unchanged */
    if (SaveConfigSlot(g_lastSaveSlot)) {
        DebugLog("Save: progress saved to slot %d\n", g_lastSaveSlot + 1);
    }
}

/* LoadConfigSlot -- 0x0048F954 -- 48 bytes */
/* Load config block from pointer table into g_saveBlock. */
int LoadConfigSlot(int slot)
{
    void *src = g_configPtrTable[slot];
    if (src == NULL) {
        return 0;
    }
    memcpy(g_saveBlock, src, 554 * 4);
    return 1;
}

/* =====================================================================
 * SaveConfigSlot — 0x0048f8cc — 134 bytes
 * Saves 2216 bytes of config data to a numbered save file (SAVE/R0N.SAV).
 * Allocates persistent copy in g_configPtrTable if not already done.
 * EAX = slot index (0-9). Returns 1 on success, 0 on failure.
 * ===================================================================== */
int SaveConfigSlot(int slot)  /* EAX */
{
    /* g_saveBlock declared via sonicr_globals.h */

    FILE *fp = fOpen(s_saveFilenames[slot], "wb");
    g_fileHandle2 = fp;
    if (fp == NULL) {
        return 0;
    }

    int saveBuf[554];
    memcpy(saveBuf, g_saveBlock, 554 * 4);
    bswap32_arr(saveBuf, 554);
    fWrite(saveBuf, 554 * 4, 1, fp);
    fClose(fp);

    /* Allocate persistent slot if needed */
    if (g_configPtrTable[slot] == NULL) {
        g_configPtrTable[slot] = g_configSlotStorage[slot];
    }
    if (g_configPtrTable[slot] != NULL) {
        memcpy(g_configPtrTable[slot], g_saveBlock, 554 * 4);
    }
    return 1;
}

/* LoadSaveScreen state — aliases into shared global block */
#define s_lsColumn g_modelRotation             /* 0x92528C — 0=SAVE, 1=LOAD, 2=NEW */
#define s_lsScrollXCur g_stateBlock92528C[1]    /* 0x925290 */
#define s_lsScrollXTgt g_stateBlock92528C[2]    /* 0x925294 */
#define s_lsSlotIndex g_stateBlock92528C[10]     /* 0x9252B4 */
#define s_lsScrollYCur g_stateBlock92528C[11]   /* 0x9252B8 */
#define s_lsScrollYTgt g_stateBlock92528C[12]   /* 0x9252BC */
#define s_lsConfirmState g_stateBlock92528C[20] /* 0x9252DC */
#define s_lsActionState g_stateBlock92528C[21]  /* 0x9252E0 */
#define s_lsButtonFlag g_stateBlock92528C[22]   /* 0x9252E4 */
#define s_lsHighlight g_stateBlock92528C[23]    /* 0x9252E8 */

/* ROM table: x-positions for the 3 bottom buttons (SAVE/LOAD/NEW) */
static const int s_lsColumnX[3] = { 0x2F, 0x7D, 0xCB };  /* 0x502808 */
#define LS_ROW_HEIGHT 0x30  /* 0x502814 = 48 pixels per slot row */
#define LS_NUM_SLOTS 10

/* Packed cursor state persisted across screen entries — 0x68afa0 */
static int s_lsPackedCursor;  /* low word = column, high word = slot index */

/**
 * LoadSaveScreenInit — 0x0048F724 — 424 bytes
 * Loads textures, opens all 10 save files into memory, initializes cursor.
 */
static void LoadSaveScreenInit(void)
{
    /* Background tint: R=0x80, G=0x40, B=0xFF (purple) */
    g_bgTintR = 0x80;                                       /* [0x625C9C] */
    g_bgTintG = 0x40;                                       /* [0x625CA0] */
    g_bgTintB = 0xFF;                                       /* [0x625CA4] */

    /* Load textures — unified path for SDL/GL */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupMenuTexturesD3D();                                  /* 0x438CD8 */
    LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);
    TintBackgroundTPage(g_bgTintR, g_bgTintG, g_bgTintB);
    LoadTPageRGB(g_uiTexPage + 1, PATH_MENU_LOADSAVE);
    for (int tp = 0; tp < 52; tp++) {
        if (g_tpageStateArray[tp] == 6 && g_tpagePixelBuf[tp] != NULL) {
            g_tpageStateArray[tp] = 4;
        }
    }
    ProcessTpageStates();                                         /* 0x4323CC */
    FinalizeMenuTexturesD3D();                               /* 0x438D10 */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) ResumeCD();
#endif

    /* Initialize fade and cursor state */
    g_fadeState = FADE_IN;                                   /* [0x901C48] = 1 */

    /* Restore cursor from packed state; force column to 1 (LOAD) */
    s_lsPackedCursor = (s_lsPackedCursor & (int)0xFFFF0000) | 1;  /* 0x48F7E4-E9 */
    s_lsColumn = s_lsPackedCursor & 0xFFFF;                 /* = 1 */
    s_lsScrollXCur = s_lsColumnX[s_lsColumn];               /* 0x925290 */
    s_lsScrollXTgt = s_lsColumnX[s_lsColumn];               /* 0x925294 */

    s_lsSlotIndex = (s_lsPackedCursor >> 16) & 0xFFFF;      /* 0x9252B4 */
    int scrollY = LS_ROW_HEIGHT * s_lsSlotIndex;
    g_screenResult = 0;                                      /* [0x925418] */
    s_lsActionState = 0;                                     /* [0x9252E0] */
    s_lsButtonFlag = 1;                                      /* [0x9252E4] */
    g_totalFrames = 0;                                       /* [0x8FB68C] */
    s_lsScrollYCur = scrollY;                                /* [0x9252B8] */
    s_lsScrollYTgt = scrollY;                                /* [0x9252BC] */

#ifdef SONICR_DC
    /* Force the music fully off before the VMU access below. VMU reads are
     * slow maple-bus I/O, and the first fOpen pulls the whole save bundle
     * (bundle_ensure -> vmu_read) while io_lock is held — long enough that a
     * still-playing song (e.g. one started from Music Test) can't be refilled
     * and the AICA replays its last buffer (repeats a chunk of the OLD song).
     * vmu_read's internal PauseCD/ResumeCD can't fix that here: it races the
     * held lock. LoadSaveScreen starts its own track (5) right after init, so
     * stopping now only costs a brief silence, and a stopped stream has
     * nothing to underrun. */
    StopCD();
#endif

    /* Load all 10 save files into memory */
    for (int i = 0; i < LS_NUM_SLOTS; i++) {
        FILE *fp = fOpen(s_saveFilenames[i], "rb");      /* 0x48F862-6D */
        if (fp == NULL) {
            g_configPtrTable[i] = NULL;                  /* 0x48F854 */
        }
        else {
            g_configPtrTable[i] = g_configSlotStorage[i];
            fRead(g_configPtrTable[i], 0x8A8, 1, fp);   /* 0x48F882-895 */
            bswap32_arr(g_configPtrTable[i], 554);
            fClose(fp);                                  /* 0x48F89F */
        }
    }

    g_renderEnabled = 1;                                     /* [0x8FB81C] */
}

/**
 * RenderSlotD3D — 0x0048EBD4 — 1351 bytes
 * Renders one save slot row using DrawTexturedQuad (D3D/GL path).
 * EAX = slot index (0-9), EDX = y-position.
 */
static void RenderSlotD3D(int slotIdx, int yPos)
{
    int tpage = g_uiTexPage + 1;
    unsigned int color;
    int baseX = 0x58;

    /* Pick base color: dimmed if highlight bit 0x10 set, else bright */
    if (s_lsHighlight & 0x10) {                                /* 0x48EBE3 */
        color = 0xFF808080u;
    }
    else {
        color = VERTEX_WHITE;
    }

    /* Slot background bar */
    
    int barY = yPos + 0x24;
    if (barY >= 0x60) {
        if (barY <= 0x180) {
            /* Full bar visible */
            DrawTexturedQuad(baseX, yPos, 0x447C8000u,
                             0x1D0, 0x48, tpage,
                             0, 0xDC, 0xE8, 0x24,
                             color);                     /* 0x48EC50-70 */
        }
        else if (barY < 0x1C0) {
            /* Partially visible at bottom */
            DrawTexturedQuad(baseX, yPos, 0x447C8000u,
                             0x1D0, 0x24, tpage,
                             0, 0xDC, 0xE8, 0x12,
                             color);                     /* 0x48EC3C-4E */
        }
    }
    else if (barY > 0x20) {
        /* Partially visible at top */
        DrawTexturedQuad(baseX, barY, 0x447C8000u,
                         0x1D0, 0x24, tpage,
                         0, 0xEE, 0xE8, 0x12,
                         color);                         /* 0x48EC11-2C */
    }
    /* else: clipped off-screen */

    /* Row 1: character unlock icons (offsets +0x40..+0x50) */
    int row1Y = yPos + 6;
    int vis = yPos + 0x24;
    if (vis >= 0x60 && row1Y < 0x180) {
        void *slotData = g_configPtrTable[slotIdx];
        int depth = 0x447C4000u;

        /* Icon at +0x40 */
        if (slotData != NULL && *(int *)((char *)slotData + 0x40) != 0) {
            DrawTexturedQuad(baseX + 0x98, row1Y, depth,
                             0x26, 0x1E, tpage,
                             0x8F, 0xA5, 0x13, 0x0F, color);
        }
        else {
            DrawTexturedQuad(baseX + 0x98, row1Y, depth,
                             0x26, 0x1E, tpage,
                             0xDB, 0xA5, 0x13, 0x0F, color);
        }

        /* Icon at +0x44 */
        if (slotData != NULL && *(int *)((char *)slotData + 0x44) != 0) {
            DrawTexturedQuad(baseX + 0x08, row1Y, depth,
                             0x26, 0x1E, tpage,
                             0x56, 0xA5, 0x13, 0x0F, color);
        }
        else {
            DrawTexturedQuad(baseX + 0x08, row1Y, depth,
                             0x26, 0x1E, tpage,
                             0xDB, 0xA5, 0x13, 0x0F, color);
        }

        /* Icon at +0x48 */
        if (slotData != NULL && *(int *)((char *)slotData + 0x48) != 0) {
            DrawTexturedQuad(baseX + 0x68, row1Y, depth,
                             0x26, 0x1E, tpage,
                             0x7C, 0xA5, 0x13, 0x0F, color);
        }
        else {
            DrawTexturedQuad(baseX + 0x68, row1Y, depth,
                             0x26, 0x1E, tpage,
                             0xDB, 0xA5, 0x13, 0x0F, color);
        }

        /* Icon at +0x50 */
        if (slotData != NULL && *(int *)((char *)slotData + 0x50) != 0) {
            DrawTexturedQuad(baseX + 0x38, row1Y, depth,
                             0x26, 0x1E, tpage,
                             0x69, 0xA5, 0x13, 0x0F, color);
        }
        else {
            DrawTexturedQuad(baseX + 0x38, row1Y, depth,
                             0x26, 0x1E, tpage,
                             0xDB, 0xA5, 0x13, 0x0F, color);
        }
    }

    /* Row 2: more unlock icons (offsets +0x4C, +0x54, +0x58) */
    int row2Y = yPos + 0x26;
    vis = yPos + 0x44;
    if (vis >= 0x60 && row2Y < 0x180) {
        void *slotData = g_configPtrTable[slotIdx];
        int depth = 0x447C4000u;

        /* Icon at +0x4C */
        if (slotData != NULL && *(int *)((char *)slotData + 0x4C) != 0) {
            DrawTexturedQuad(baseX + 0x20, row2Y, depth,
                             0x26, 0x1E, tpage,
                             0xA2, 0xA5, 0x13, 0x0F, color);
        }
        else {
            DrawTexturedQuad(baseX + 0x20, row2Y, depth,
                             0x26, 0x1E, tpage,
                             0xDB, 0xA5, 0x13, 0x0F, color);
        }

        /* Icon at +0x54 */
        if (slotData != NULL && *(int *)((char *)slotData + 0x54) != 0) {
            DrawTexturedQuad(baseX + 0x50, row2Y, depth,
                             0x26, 0x1E, tpage,
                             0xB5, 0xA5, 0x13, 0x0F, color);
        }
        else {
            DrawTexturedQuad(baseX + 0x50, row2Y, depth,
                             0x26, 0x1E, tpage,
                             0xDB, 0xA5, 0x13, 0x0F, color);
        }

        /* Icon at +0x58 */
        if (slotData != NULL && *(int *)((char *)slotData + 0x58) != 0){
            DrawTexturedQuad(baseX + 0x80, row2Y, depth,
                             0x26, 0x1E, tpage,
                             0xC8, 0xA5, 0x13, 0x0F, color);
        }
        else {
            DrawTexturedQuad(baseX + 0x80, row2Y, depth,
                             0x26, 0x1E, tpage,
                             0xDB, 0xA5, 0x13, 0x0F, color);
        }
    }

    /* Row 3: track unlock icons (offsets +0x28..+0x38, value==2) */
    int row3Y = yPos + 0x0C;
    vis = yPos + 0x3C;
    if (vis >= 0x60 && row3Y < 0x180) {
        void *slotData = g_configPtrTable[slotIdx];
        int depth = 0x447C4000u;

        /* Track 0 (+0x28) */
        if (slotData != NULL && *(int *)((char *)slotData + 0x28) == 2) {
            DrawTexturedQuad(baseX + 0xC4, row3Y, depth,
                             0x30, 0x30, tpage,
                             0x56, 0x8D, 0x18, 0x18, color);
        }
        else {
            DrawTexturedQuad(baseX + 0xC4, row3Y, depth,
                             0x30, 0x30, tpage,
                             0xCE, 0x8D, 0x18, 0x18, color);
        }

        /* Track 1 (+0x2C) */
        if (slotData != NULL && *(int *)((char *)slotData + 0x2C) == 2) {
            DrawTexturedQuad(baseX + 0xF8, row3Y, depth,
                             0x30, 0x30, tpage,
                             0x6E, 0x8D, 0x18, 0x18, color);
        }
        else {
            DrawTexturedQuad(baseX + 0xF8, row3Y, depth,
                             0x30, 0x30, tpage,
                             0xCE, 0x8D, 0x18, 0x18, color);
        }

        /* Track 2 (+0x30) */
        if (slotData != NULL && *(int *)((char *)slotData + 0x30) == 2) {
            DrawTexturedQuad(baseX + 0x12C, row3Y, depth,
                             0x30, 0x30, tpage,
                             0x86, 0x8D, 0x18, 0x18, color);
        }
        else {
            DrawTexturedQuad(baseX + 0x12C, row3Y, depth,
                             0x30, 0x30, tpage,
                             0xCE, 0x8D, 0x18, 0x18, color);
        }

        /* Track 3 (+0x34) */
        if (slotData != NULL && *(int *)((char *)slotData + 0x34) == 2) {
            DrawTexturedQuad(baseX + 0x160, row3Y, depth,
                             0x30, 0x30, tpage,
                             0x9E, 0x8D, 0x18, 0x18, color);
        }
        else {
            DrawTexturedQuad(baseX + 0x160, row3Y, depth,
                             0x30, 0x30, tpage,
                             0xCE, 0x8D, 0x18, 0x18, color);
        }

        /* Track 4 (+0x38) */
        if (slotData != NULL && *(int *)((char *)slotData + 0x38) == 2) {
            DrawTexturedQuad(baseX + 0x194, row3Y, depth,
                             0x30, 0x30, tpage,
                             0xB6, 0x8D, 0x18, 0x18, color);
        }
        else {
            DrawTexturedQuad(baseX + 0x194, row3Y, depth,
                             0x30, 0x30, tpage,
                             0xCE, 0x8D, 0x18, 0x18, color);
        }
    }

    /* Empty-slot icon (red X) — shown if slot has no data */
    void *slotData = g_configPtrTable[slotIdx];
    if (slotData == NULL)
    {
        int emptyY = yPos + 0x14;
        vis = yPos + 0x34;
        if (vis >= 0x60 && emptyY < 0x180) {
            DrawTexturedQuad(baseX + 0xD8, emptyY, 0x447C0000u,
                             0x20, 0x20, tpage,
                             0xB0, 0x00, 0x10, 0x10, color);
        }
    }
}

/**
 * LoadSaveScreen — 0x0048F984 — 2945 bytes
 * Main loop for the load/save data screen.
 */
int LoadSaveScreen(void)
{
    DebugLog("\nLoadSaveScreen\n\n");                         /* 0x5307A0 */

    LoadSaveScreenInit();                                    /* 0x48F724 */

    /* CD music — play track 5 if not already */
    if (GetLogicalCDTrack() != 5) {                              /* 0x48F9DA */
        UpdateCDPlayback(5);                                 /* 0x48F9E9 */
    }

    /* Frame timing */
    unsigned int lastTime = timeGetTime() / 1000;            /* 0x48F8C1 */
    int frameElapsed;

    /* Main loop */
    for (;;) {
        platform_pump_events();

        /* Frame timing — compute elapsed seconds for idle timeout */
        unsigned int now = timeGetTime() / 1000;
        int delta = (int)(now - lastTime);
        if (delta < 0) {
            delta = -delta;
        }
        frameElapsed = delta;                            /* [0x68AF40] */

        /* Update fade if active */
        if (g_fadeState != 0) {                              /* 0x48F9EE */
            UpdateFade();                                    /* 0x4305D4 */
        }

        /* Check for fade-out complete */
        if (g_fadeLevel <= (int)0xFFFFFF00) {                /* 0x48F9FB */
            /* Free slot data and return */
            for (int i = 0; i < LS_NUM_SLOTS; i++) {            /* 0x48FA0D-2C */
                if (g_configPtrTable[i] != NULL) {
                    g_configPtrTable[i] = NULL;
                }
            }
            return g_screenResult;                           /* 0x48FA2E */
        }

        /* Read input */
        ReadInput();                                         /* 0x477228 */

        /* Input handling (only when not fading) */
        if (g_fadeState == 0) {                              /* 0x48FA44 */

            if (s_lsActionState == 0) {                      /* 0x48FA4C */
                /* Normal navigation (no confirm dialog) */

                /* Horizontal (left/right = column select) */
                if (s_lsScrollXTgt == s_lsScrollXCur) {      /* 0x48FA5E */
                    unsigned char keys = g_inputBits;
                    /* Left arrow */
                    if ((keys & 0x40) && !(keys & 0x80)) {   /* 0x48FA6A */
                        if (s_lsColumn > 0) {                /* 0x48FA7F */
                            s_lsColumn--;
                            PlaySoundEffect(1, 0, 0);            /* 0x48FA97 */
                            lastTime = timeGetTime() / 1000;
                        }
                    }
                    /* Right arrow */
                    if ((keys & 0x80) && !(keys & 0x40)) {   /* 0x48FAB1 */
                        if (s_lsColumn < 2) {                /* 0x48FAC7 */
                            s_lsColumn++;
                            PlaySoundEffect(1, 0, 0);            /* 0x48FAE0 */
                            lastTime = timeGetTime() / 1000;
                        }
                    }
                    s_lsScrollXTgt = s_lsColumnX[s_lsColumn]; /* 0x48FAFF */
                }

                /* Vertical (up/down = slot select) */
                if (s_lsScrollYTgt == s_lsScrollYCur) {      /* 0x48FB11 */
                    unsigned char keys = g_inputBits;
                    /* Up arrow */
                    if ((keys & 0x10) && !(keys & 0x20)) {   /* 0x48FB1D */
                        if (s_lsSlotIndex > 0) {             /* 0x48FB32 */
                            s_lsSlotIndex--;
                            PlaySoundEffect(1, 0, 0);            /* 0x48FB4A */
                            lastTime = timeGetTime() / 1000;
                        }
                    }
                    /* Down arrow */
                    if ((keys & 0x20) && !(keys & 0x10)) {   /* 0x48FB64 */
                        if (s_lsSlotIndex < 9) {             /* 0x48FB7A */
                            s_lsSlotIndex++;
                            PlaySoundEffect(1, 0, 0);            /* 0x48FB93 */
                            lastTime = timeGetTime() / 1000;
                        }
                    }
                    s_lsScrollYTgt = LS_ROW_HEIGHT * s_lsSlotIndex; /* 0x48FBBB */
                }
            }
            else {
                /* Confirm dialog active (actionState != 0) */

                /* Left arrow → select Yes (confirmState = 0) */
                if (s_lsConfirmState == 1) {                 /* 0x48FBC5 */
                    unsigned char keys = g_inputBits;
                    if ((keys & 0x40) && !(keys & 0x80)) {   /* 0x48FBCE */
                        s_lsConfirmState = 0;
                        PlaySoundEffect(1, 0, 0);
                        lastTime = timeGetTime() / 1000;
                    }
                }
                /* Right arrow → select No (confirmState = 1) */
                if (s_lsConfirmState == 0) {                 /* 0x48FC09 */
                    unsigned char keys = g_inputBits;
                    if ((keys & 0x80) && !(keys & 0x40)) {   /* 0x48FC13 */
                        s_lsConfirmState = 1;
                        PlaySoundEffect(1, 0, 0);
                        lastTime = timeGetTime() / 1000;
                    }
                }
            }

            /* Action button (A/Start) */
            {
                unsigned char keys = g_inputBits;
                if (keys & 0x06) {                           /* 0x48FC51 — A or Start */
                    if (s_lsButtonFlag == 0) {               /* 0x48FC63 */
                        if (s_lsActionState == 1) {          /* 0x48FC71 */
                            /* Confirm dialog showing — A pressed */
                            if (s_lsConfirmState == 1) {     /* 0x48FC76 */
                                /* "No" selected → cancel confirm */
                                PlaySoundEffect(0, 0, 0);        /* 0x48FC89 */
                                s_lsActionState = 0;         /* 0x48FC84 */
                            }
                            else {
                                /* "Yes" selected → execute */
                                s_lsActionState = 2;         /* 0x48FCCF */
                            }
                            s_lsButtonFlag = 1;              /* 0x48FD2F */
                        }
                        else {
                            /* No confirm dialog — first press */
                            if (s_lsScrollXTgt == s_lsScrollXCur &&
                                s_lsScrollYTgt == s_lsScrollYCur) {
                                /* Not scrolling — check slot validity */
                                if (s_lsColumn == 0 &&       /* 0x48FCBF */
                                    g_configPtrTable[s_lsSlotIndex] == NULL) {
                                    /* SAVE to empty slot → execute directly */
                                    s_lsActionState = 2;     /* 0x48FCCF */
                                }
                                else if (s_lsColumn == 1 &&  /* 0x48FCE5 */
                                           g_configPtrTable[s_lsSlotIndex] == NULL) {
                                    /* LOAD from empty slot → no-op */
                                    s_lsActionState = 0;     /* 0x48FCFE */
                                }
                                else {
                                    /* Show confirm dialog */
                                    PlaySoundEffect(0x1A, 0, 0); /* 0x48FD1B */
                                    s_lsActionState = 1;     /* 0x48FD25 */
                                    s_lsConfirmState = 1;    /* 0x48FD2A — default: No */
                                }
                            }
                            s_lsButtonFlag = 1;              /* 0x48FD2F */
                        }
                    }
                }
                else {
                    s_lsButtonFlag = 0;                      /* 0x48FD3B */
                }
            }

            /* Execute action (state 2 = confirmed) */
            if (s_lsActionState == 2) {                      /* 0x48FD41 */
                if (s_lsColumn == 1) {
                    /* LOAD: copy slot data into g_saveBlock */
                    LoadConfigSlot(s_lsSlotIndex);             /* 0x48FD5D */
                    LoadAllGhostTimes();                     /* 0x48FD79 */
                    SetLastSaveSlot(s_lsSlotIndex);
                }
                else if (s_lsColumn == 0) {
                    /* SAVE: save g_saveBlock to slot */
                    SaveConfigSlot(s_lsSlotIndex);           /* 0x48FD6D */
                    SetLastSaveSlot(s_lsSlotIndex);
                }
                else {
                    /* NEW: reset to defaults */
                    InitDefaultTimeTables();                  /* 0x48FD74 */
                    LoadAllGhostTimes();                     /* 0x48FD79 */
                }

                /* Confirm sound + fade out */
                PlaySoundEffect(2, 0, 0);                        /* 0x48FD89 */
                g_screenResult = 1;                          /* 0x48FDA6 */
                g_fadeState = 2;                             /* FADE_OUT */

                /* Store cursor for next entry */
                s_lsPackedCursor = s_lsColumn |
                                   (s_lsSlotIndex << 16);    /* 0x48FDB4 */
            }

            /* Cancel button (B/Escape) */
            if (g_inputBits & 0x01) {  /* 0x48FDBB */
                PlaySoundEffect(0, 0, 0);                        /* 0x48FDCC */
                g_screenResult = 0;
                g_fadeState = 2;                             /* FADE_OUT */
            }

            /* Idle timeout (30 seconds) */
            if (frameElapsed > 0x1E) {                       /* 0x48FDE2 */
                PlaySoundEffect(0, 0, 0);
                g_fadeState = 2;
                g_screenResult = SCREEN_TITLE;               /* 0x3039 */
            }
        }

        /* Scroll animation */
        /* Horizontal scroll (column indicator) */
        {
            int cur = s_lsScrollXCur;
            int tgt = s_lsScrollXTgt;
            if (cur < tgt) {                                 /* 0x48FE1B */
                cur += 4;
                if (cur > tgt) {
                    cur = tgt;
                }
                s_lsScrollXCur = cur;
            }
            else if (cur > tgt) {                          /* 0x48FE38 */
                cur -= 4;
                if (cur < tgt) {
                    cur = tgt;
                }
                s_lsScrollXCur = cur;
            }
        }
        /* Vertical scroll (slot list) */
        {
            int cur = s_lsScrollYCur;
            int tgt = s_lsScrollYTgt;
            if (cur < tgt) {                                 /* 0x48FE5A */
                cur += 5;
                if (cur > tgt) {
                    cur = tgt;
                }
                s_lsScrollYCur = cur;
            }
            else if (cur > tgt) {                          /* 0x48FE77 */
                cur -= 5;
                if (cur < tgt) {
                    cur = tgt;
                }
                s_lsScrollYCur = cur;
            }
        }


        /* Set highlight mode */
        if (s_lsActionState != 0) {                          /* 0x48FE99 */
            s_lsHighlight = 0x10;                            /* 0x48FEA2 */
        }
        else {
            s_lsHighlight = s_lsActionState;                 /* = 0 */
        }

        /* D3D/GL Rendering */
        ProcessTpageStates();                                     /* 0x4901B0 */
        BeginFrame();                                        /* 0x4901B5 */
        RenderBackground();                                  /* 0x435868 */
        EndFrame();

        /* 0x4901DD `cmp esi, [0x901c44]` / `jle` — esi is 0 for the whole
         * function (xor esi,esi at 0x48f99b; every later reference is a push
         * passing 0, and the only pop is the epilogue at 0x48fa34). So the
         * test is 0 > g_fadeLevel, i.e. g_fadeLevel < 0 — the same gate every
         * other screen uses.
         *
         * This was translated as `g_fadeLevel > 0`, operands reversed.
         * g_fadeLevel only ever runs -256..0, so the overlay never drew and
         * this screen had no fade in either direction. Note `cmp reg, mem`
         * reverses the operand order relative to the `cmp mem, imm` form the
         * other screens use (compare 0x4DB23A). */
        if (g_fadeLevel < 0) {                               /* 0x4901DD */
            RenderFadeOverlay();                             /* 0x461DF4 */
        }

        BeginFrame();

        {
            int tpage = g_uiTexPage + 1;

            /* Background header bars (top) */
            DrawTexturedQuad(0, 0x20, 0x447A0000u,
                             0x140, 0x40, tpage,
                             0, 0, 0xA0, 0x20,
                             VERTEX_WHITE);                   /* 0x490212 */
            DrawTexturedQuad(0x140, 0x20, 0x447A0000u,
                             0x140, 0x40, tpage,
                             0, 0x20, 0xA0, 0x20,
                             VERTEX_WHITE);                   /* 0x490248 */

            /* Side panel / scrollbar */
            DrawTexturedQuad(0, 0x180, 0x447BE000u,
                             0x504, 0x42, tpage,
                             0xFF, 0, 0x01, 0x21,
                             VERTEX_WHITE);                   /* 0x490276 */

            /* Bottom buttons — SAVE (col 0) */
            DrawTexturedQuad(s_lsColumnX[0] * 2, 0x18E, 0x447BC000u,
                             0x8C, 0x28, tpage,
                             0, 0x60, 0x46, 0x14,
                             VERTEX_WHITE);                   /* 0x4902A6 */

            /* Bottom buttons — LOAD (col 1) */
            DrawTexturedQuad(s_lsColumnX[1] * 2, 0x18E, 0x447BC000u,
                             0x8C, 0x28, tpage,
                             0x46, 0x60, 0x46, 0x14,
                             VERTEX_WHITE);                   /* 0x4902D7 */

            /* Bottom buttons — NEW (col 2) */
            DrawTexturedQuad(s_lsColumnX[2] * 2, 0x18E, 0x447BC000u,
                             0x8C, 0x28, tpage,
                             0x8C, 0x60, 0x46, 0x14,
                             VERTEX_WHITE);                   /* 0x49030B */

            /* Cursor arrow (column indicator) */
            DrawTexturedQuad((s_lsScrollXCur - 1) * 2, 0x18C, 0x447B8000u,
                             0x90, 0x2C, tpage,
                             0xB4, 0x43, 0x48, 0x16,
                             VERTEX_WHITE);                   /* 0x490340 */

            /* Slot list background panel */
            DrawTexturedQuad(0x52, 0xC8, 0x447B8000u,
                             0x1DC, 0x50, tpage,
                             0, 0xB4, 0xEE, 0x28,
                             VERTEX_WHITE);                   /* 0x490374 */

            /* Confirm dialog (when s_lsActionState == 1) */
            if (s_lsActionState == 1) {                      /* 0x490379 */
                /* Selection highlight */
                int hlUvX, hlUvY, hlUvH;
                if (s_lsColumn == 0) {                       /* 0x490386 */
                    hlUvX = 0x4C; hlUvY = 0x74; hlUvH = 0x19;
                }
                else {
                    hlUvX = 0; hlUvY = 0x48; hlUvH = 0x18;
                }
                DrawTexturedQuad(0x8C, 0xBE, 0x447B4000u,
                                 0x168, 0x32, tpage,
                                 hlUvX, hlUvY, 0xB4, hlUvH,
                                 VERTEX_WHITE);               /* 0x4903D5 */

                /* Yes/No buttons */
                DrawTexturedQuad(0xB0, 0xFA, 0x447B4000u,
                                 0x8C, 0x28, tpage,
                                 0xA0, 0x1B, 0x46, 0x14,
                                 VERTEX_WHITE);               /* 0x490407 */

                DrawTexturedQuad(0x144, 0xFA, 0x447B4000u,
                                 0x8C, 0x28, tpage,
                                 0xA0, 0x2F, 0x46, 0x14,
                                 VERTEX_WHITE);               /* 0x490439 */

                /* Confirm cursor arrow */
                {
                    int arrowX;
                    if (s_lsConfirmState == 0) {              /* 0x49043E */
                        arrowX = 0xAE;
                    }
                    else {
                        arrowX = 0x142;
                    }
                    DrawTexturedQuad(arrowX, 0xF8, 0x447B0000u,
                                     0x90, 0x2C, tpage,
                                     0xB4, 0x43, 0x48, 0x16,
                                     VERTEX_WHITE);           /* 0x49047B */
                }
            }

            /* Draw 10 save slot rows */
            for (int slot = 0; slot < LS_NUM_SLOTS; slot++) { /* 0x490480 */
                int rowY = s_lsScrollYCur * 2;
                int slotY = LS_ROW_HEIGHT * slot * 2;
                int yPos = 0xCC - (rowY - slotY);
                RenderSlotD3D(slot, yPos);               /* 0x48EBD4 */
            }
        }

        /* Final render pass */
        RenderWavingMenuBackground();                             /* 0x004C68B8 — software twin */
        EndFrame();
        FlipD3D();                                           /* 0x4356BC */

        g_totalFrames2++;                                    /* [0x8FB690] */
        g_totalFrames++;                                     /* [0x8FB68C] */

        /* FPS cap */
        g_currentTime = timeGetTime();
        WaitForFrameCap();
    }
}

/* =====================================================================
 * ROM data and helpers for the time-ranking ("Records") screen.
 * Shows best times per character per track, with a rotating 3D model.
 * ===================================================================== */

/* ROM tables extracted from PE */

/* Screen layout: [0]=base X offset, [1..8]=Y positions for 8 time rows.
 * Binary accesses Y values as [i]*2, so the doubled values become pixel Y. */
static const int s_rankLayout[9] = {                     /* 0x00502840 */
    8, 12, 32, 58, 78, 103, 123, 158, 202
};

/* Character page → save-data index lookup (10 entries). */
static const int s_rankCharLookup[10] = {                /* 0x00502740 */
    0, 1, 3, 2, 4, 1, 2, 4, 3, 5
};

/* Character page → model struct offset multiplier (5 entries). */
static const int s_rankModelIndex[5] = {                 /* 0x0050272C */
    2, 3, 0, 1, 4
};

/* Per-character Y offset for name/text rendering (10 entries). */
static const int s_rankNameYOffset[10] = {               /* 0x005018C0 */
    120, 120, 120, 120, 65, 120, 120, 120, 115, 120
};

/* Save-data byte offsets from g_saveBlock base for each of 8 time rows.
 * Binary addresses: 0x8fbc9c, 0x8fbc88, 0x8fbcc4, 0x8fbcb0,
 *                   0x8fbcec, 0x8fbcd8, 0x8fbd00, 0x8fbd14.
 * Subtract g_saveBlock base 0x8fba4c to get offsets. */
static const int s_rankSaveOffset[8] = {
    0x250, 0x23C, 0x278, 0x264, 0x2A0, 0x28C, 0x2B4, 0x2C8
};

/* Externs needed by this screen */
extern int g_rankViewH;                 /* 0x0068AFFC */
extern int g_rankViewW;                 /* 0x0068B000 */
extern int g_rankPagePersist;           /* 0x0068AFA4 */
extern int g_rankCursorPersist;         /* 0x0068AFA8 */
extern int g_menuDepthBucket;           /* 0x006D763C */

/* Forward declarations for functions defined later in this file */
static void RenderTimeDigits(int screenX, int screenY, int depthBucket,
                             int timeValue, int highlightStyle);
void RenderCharacterOnPodium(int xOff, int yOff, int zOffset,
                             int angleC, int angleD, int angleE,
                             Player *player);
void RenderCharacterCredits(int angleA, int angleB, int zOffset,
                            int angleC, int angleD, int angleE,
                            Player *player);

/* Aliases for 0x9252xx state reused by this screen */
#define s_rankCursor        g_modelRotation        /* 0x92528C — current cursor slot */
#define s_rankNavPressed    g_stateBlock92528C[1]   /* 0x925290 — 1=direction key latched */
#define s_rankPage          g_stateBlock92528C[10]   /* 0x9252B4 — current character page */
#define s_rankLRPressed     g_stateBlock92528C[11]  /* 0x9252B8 — 1=left/right key latched */
#define s_rankResult        g_screenResult         /* 0x925418 — return code */

/**
 * RenderRankingEmblems — FUN_00490BEC — 309 bytes
 * D3D/GL path: renders 5 rotating collectible models via
 * Draw3DModelD3D (tpage vertex batching, 0x454C70).
 */
static void RenderRankingEmblems(void)                   /* 0x00490BEC */
{
    int phase = (g_totalFrames & 0x7F) << 5;
    char *objBase = (char *)g_objectStructArray;

    /* Binary: EAX=xOff, EDX=yOff, EBX=0x500(zBase), ECX=0(angleA),
     * stack: [rotation, angleC(0), objPtr, 0(unused), 0(unused), 2(scale)] */
    Draw3DModelD3D(-125, 450, 0x500, 0, phase, 0,
                   (intptr_t)(objBase + 0x2A8), 2);

    Draw3DModelD3D(-125, 225, 0x500, 0, (phase + 0x320) & 0xFFF, 0,
                   (intptr_t)(objBase + 0x198), 2);

    Draw3DModelD3D(-125, 0, 0x500, 0, (phase + 0x640) & 0xFFF, 0,
                   (intptr_t)(objBase + 0x1DC), 2);

    Draw3DModelD3D(-125, -225, 0x500, 0, (phase + 0x960) & 0xFFF, 0,
                   (intptr_t)(objBase + 0x220), 2);

    Draw3DModelD3D(-125, -450, 0x500, 0, (phase + 0xC80) & 0xFFF, 0,
                   (intptr_t)(objBase + 0x264), 2);
}

/**
 * RenderRankingNavArrows — FUN_00490508 — 654 bytes
 * Draws up/down/left/right navigation arrows for the ranking screen.
 * Arrows are dimmed (0x60808080) when no more slots/pages exist in
 * that direction, bright (0xC0E0E0E0) when navigation is possible.
 */
static void RenderRankingNavArrows(void)                 /* 0x00490508 */
{
    int tpage = g_uiTexPage + 1;

    /* DrawTexturedQuad signature:
     * (xPos, yPos, depth, width, height, tpage, uvX, uvY, uvW, uvH, color)
     * UV coords from binary software path (0x490595-0x49078b). */

    /* UP arrow */
    {
        int canUp = 0;
        int slot = s_rankCursor - 1;
        while (slot >= 0) {
            if (g_charUnlockTable[slot] == 2) {
                canUp = 1;
                break;
            }
            slot--;
        }
        unsigned int color = canUp ? 0xC0FFFFFFu : 0x60808080u;
        //canUp ? 0xC0E0E0E0u : 0x60808080u;
        DrawTexturedQuad(0x1D0, 0x94, 0x443B8000u, 0x30, 0x30,
                         tpage, 0xB8, 0xE8, 0x18, 0x18, color);
    }

    /* DOWN arrow */
    {
        int canDown = 0;
        int slot = s_rankCursor + 1;
        while (slot <= 9) {
            if (g_charUnlockTable[slot] == 2) {
                canDown = 1;
                break;
            }
            slot++;
        }
        unsigned int color = canDown ? 0xC0FFFFFFu : 0x60808080u;
        //canDown ? 0xC0E0E0E0u : 0x60808080u;
        DrawTexturedQuad(0x228, 0x94, 0x443B8000u, 0x30, 0x30,
                         tpage, 0x88, 0xE8, 0x18, 0x18, color);
    }

    /* LEFT arrow */
    {
        int canLeft = (s_rankPage > 0);
        unsigned int color = canLeft ? 0xC0FFFFFFu : 0x60808080u;
        //canLeft ? 0xC0E0E0E0u : 0x60808080u;
        DrawTexturedQuad(0x1FC, 0x190, 0x443B8000u, 0x30, 0x30,
                         tpage, 0xA0, 0xE8, 0x18, 0x18, color);
    }

    /* RIGHT arrow */
    {
        int maxPage = (g_gpAllTracksFlag != 0) ? 4 : 3;
        int canRight = (maxPage > s_rankPage);
        unsigned int color = canRight ? 0xC0FFFFFFu : 0x60A0A0A0u;
        //canRight ? 0xC0E0E0E0u : 0x60A0A0A0u;
        DrawTexturedQuad(0x1FC, 0xE8, 0x443B8000u, 0x30, 0x30,
                         tpage, 0x70, 0xE8, 0x18, 0x18, color);
    }
}

/**
 * TimeRankingScreen — 0x00490D24 — 3681 bytes
 * Displays best-time rankings per character/track with rotating 3D model.
 * Accessed from OptionsMenuScreen via return code 10001 ("Records").
 */
int TimeRankingScreen(void)
{
    Player *p = g_playerBase;

    DebugLog("\nTimeRankingScreen\n\n");

    /* CD music check (play track 5) */
    if (GetLogicalCDTrack() != 5) {
        UpdateCDPlayback(5);
    }

    /* Tpage loading */
    /* 0x490d50: timeGetTime / 1000 */
    g_menuIdleStartTime = timeGetTime() / 1000;               /* 0x68AFB0 */

    /* Fade background: R=0x80, G=0x40, B=0x20 */
    g_bgTintB = 0x20;                                    /* 0x625CA4 */
    g_bgTintR = 0x80;                                    /* 0x625C9C */
    g_bgTintG = 0x40;                                    /* 0x625CA0 */

    /* Load tpages — use same working pattern as OptionsMenuScreen.
     * LoadTPageRGB + TintBackgroundTPage works correctly on GL. */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupMenuTexturesD3D();
    LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);
    TintBackgroundTPage(g_bgTintR, g_bgTintG, g_bgTintB);
    LoadTPageRGB(g_uiTexPage + 1, PATH_MENU_RANKING);
    for (int tp = 0; tp < 52; tp++) {
        if (g_tpageStateArray[tp] == 6 && g_tpagePixelBuf[tp] != NULL)
            g_tpageStateArray[tp] = 4;
    }
    ProcessTpageStates();
    FinalizeMenuTexturesD3D();
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) ResumeCD();
#endif

    /* Initialize screen state */
    g_rankViewH = 0x7B;                                  /* 0x68AFFC */
    g_rankViewW = 0x384;                                 /* 0x68B000 */
    g_fadeState = 1;                                     /* FADE_IN */
    g_totalFrames = 0;                                   /* 0x8FB68C */

    /* If persistent page is out of range, reset */
    if (g_rankPagePersist == 4 && g_gpAllTracksFlag == 0) {
        g_rankPagePersist = 0;
    }

    /* If persistent cursor slot is no longer unlocked, reset */
    if (g_charUnlockTable[g_rankCursorPersist] != 2) {
        g_rankCursorPersist = 0;
    }

    /* Copy persistent state to working state */
    s_rankCursor = g_rankCursorPersist;                  /* 0x92528C */
    s_rankNavPressed = 1;                                /* 0x925290 */
    s_rankLRPressed = 1;                                 /* 0x9252B8 */
    s_rankResult = 1;                                    /* 0x925418 — default: SCREEN_BACK */
    g_renderEnabled = 1;                                 /* 0x8FB81C */
    s_rankPage = g_rankPagePersist;                      /* 0x9252B4 */

    /* Initialize animation: set charId to cursor, look up anim data */
    p->animId = 0;                                       /* 0x8FD58C */
    p->charId = (short)s_rankCursor;                     /* 0x8FD5E6 */
    p->_unk_0x1E0 = (short)s_rankCursor;                 /* 0x8FD6D4 */

    int cid = (int)p->charId;
    void **animTable = ((void ***)g_charAnimTables)[cid * 2];
    int aid = (int)p->animId;
    const short *fs = (animTable != NULL) ? (const short *)animTable[aid] : NULL;
    if (fs != NULL) {
        g_animDataPtrs[0] = fs;
        p->animFrameIdx = (int)fs[0] - 1;           /* 0x8FD6E0 */
    }

    /* Main loop */
    DWORD startTick = timeGetTime() / 1000;              /* ebp-0x18 */

    platform_pump_events();

    g_currentTime = timeGetTime();                       /* 0x6E9D00 */
    DWORD nowSec = timeGetTime() / 1000;
    int elapsed = (int)nowSec - (int)startTick;
    if (elapsed < 0) {
        elapsed = -elapsed;
    }

    while (1) {
        /* CD music check */
        if (GetLogicalCDTrack() != 5) {
            UpdateCDPlayback(5);
        }

        /* Update fade */
        if (g_fadeState != 0) {
            UpdateFade();
        }

        /* Exit: fully faded out → save persistent state and return */
        if (g_fadeLevel == (int)0xFFFFFF00) {
            g_rankPagePersist = s_rankPage;
            g_rankCursorPersist = s_rankCursor;
            return s_rankResult;
        }

        /* Input processing */
        g_visibleObjectCount = 0;                        /* 0x6EAD2C */
        g_processedObjectCount = 0;                      /* 0x6DA2E8 */
        ReadInput();

        if (g_fadeState != 0) {
            /* Fading — skip input, go to render */
            goto render;
        }

        /* ESC/Back button (bit 0x01) → begin fade out */
        if (g_inputBits & 0x01) {
            PlaySoundEffect(0, 0, 0);
            s_rankResult = 0;                            /* back */
            g_fadeState = 2;                             /* FADE_OUT */
        }

        /* Timeout: 180 seconds → auto-exit */
        if (elapsed > 0xB4) {
            elapsed = 0x3039;                            /* sentinel */
            PlaySoundEffect(0, 0, 0);
            s_rankResult = elapsed;
            g_fadeState = 2;
        }

        /* UP arrow (bit 0x40) */
        if (g_inputBits & 0x40) {
            if (s_rankNavPressed == 0 && !(g_inputBits & 0x80)) {
                int slot = s_rankCursor;
                if (slot > 0) {
                    slot--;
                    while (slot >= 0) {
                        if (g_charUnlockTable[slot] == 2) {
                            PlaySoundEffect(1, 0, 0);
                            s_rankCursor = slot;
                            /* Update animation charId */
                            p->charId = (short)s_rankCursor;
                            p->_unk_0x1E0 = (short)s_rankCursor;
                            int cid = (int)p->charId;
                            void **animTable = ((void ***)g_charAnimTables)[cid * 2];
                            int aid = (int)p->animId;
                            const short *fs = (animTable != NULL) ? (const short *)animTable[aid] : NULL;
                            if (fs != NULL) {
                                g_animDataPtrs[0] = fs;
                                p->animFrameIdx = (int)fs[0] - 1;
                            }
                            startTick = timeGetTime() / 1000;
                            break;
                        }
                        slot--;
                    }
                }
            }
            s_rankNavPressed = 1;
        }
        /* DOWN arrow (bit 0x80) */
        else if (g_inputBits & 0x80) {
            if (s_rankNavPressed == 0 && !(g_inputBits & 0x40)) {
                int slot = s_rankCursor;
                if (slot < 9) {
                    slot++;
                    while (slot <= 9) {
                        if (g_charUnlockTable[slot] == 2) {
                            PlaySoundEffect(1, 0, 0);
                            s_rankCursor = slot;
                            p->charId = (short)s_rankCursor;
                            p->_unk_0x1E0 = (short)s_rankCursor;
                            int cid = (int)p->charId;
                            void **animTable = ((void ***)g_charAnimTables)[cid * 2];
                            int aid = (int)p->animId;
                            const short *fs = (animTable != NULL) ? (const short *)animTable[aid] : NULL;
                            if (fs != NULL) {
                                g_animDataPtrs[0] = fs;
                                p->animFrameIdx = (int)fs[0] - 1;
                            }
                            startTick = timeGetTime() / 1000;
                            break;
                        }
                        slot++;
                    }
                }
            }
            s_rankNavPressed = 1;
        }
        /* Neither UP nor DOWN */
        else {
            s_rankNavPressed = 0;
        }

        /* LEFT (bit 0x20) — previous character page */
        if (g_inputBits & 0x20) {
            if (s_rankLRPressed == 0 && !(g_inputBits & 0x10)) {
                if (s_rankPage > 0) {
                    s_rankPage--;
                    s_rankLRPressed = 1;
                    PlaySoundEffect(1, 0, 0);
                    startTick = timeGetTime() / 1000;
                }
            }
            goto nav_done;
        }
        /* RIGHT (bit 0x10) — next character page */
        else if (g_inputBits & 0x10) {
            int maxPage = (g_gpAllTracksFlag != 0) ? 4 : 3;
            if (s_rankLRPressed == 0 && !(g_inputBits & 0x20)) {
                if (maxPage > s_rankPage) {
                    s_rankLRPressed = 1;
                    s_rankPage++;
                    PlaySoundEffect(1, 0, 0);
                    startTick = timeGetTime() / 1000;
                }
            }
            goto nav_done;
        }
        /* Neither LEFT nor RIGHT */
        else {
            s_rankLRPressed = 0;
        }
nav_done:

        /* Animation update */
render:
        UpdateFrameTimers();
        AnimateVehicleGlow(p);

        /* Advance animation frame stream */
        const short *fs = g_animDataPtrs[0];
        fs++;
        g_animDataPtrs[0] = fs;
        int frame = (int)*fs;
        p->_unk_0x1C = frame;
        if (frame == -1) {
            /* Loop: rewind by next word's value */
            const short *cur = g_animDataPtrs[0];
            int rewind = (int)cur[1];
            cur -= rewind;
            g_animDataPtrs[0] = cur;
            cur++;
            g_animDataPtrs[0] = cur;
            frame = (int)*cur;
            p->_unk_0x1C = frame;
        }
        p->_unk_0x1C = frame & 0xFFF;
        p->animFrameIdx = (frame & 0xFFF) - 1;

        /* Emerald sine phase advance */
        g_emeraldSineOffX = (g_emeraldSineOffX + 0x4D) & 0xFFF;
        g_emeraldSineOffY = (g_emeraldSineOffY + 0xB6) & 0xFFF;
        g_emeraldSineOffZ = (g_emeraldSineOffZ + 0x73) & 0xFFF;

        /* RENDERING */
        int charId = (int)p->charId;
        int pageLookup = s_rankCharLookup[s_rankPage];
        int fadePhase = (g_totalFrames & 0xFF) << 4;

        /* D3D rendering path (matches OptionsMenuScreen pattern) */

        /*  background */
        ProcessTpageStates();
        BeginFrame();
        RenderBackground();
        EndFrame();

        /* main content */
        BeginFrame();
        RenderWavingMenuBackground();

        /* Render emblems (GP trophy, balloon, tag figures, etc.) */
        RenderRankingEmblems();

        /* Render 8 row backgrounds via DrawTexturedQuad */
        {
            static const int s_spriteUvY[8] = {
                0x00, 0x10, 0x00, 0x10, 0x20, 0x30, 0x40, 0x50
            };
            int sprX = s_rankLayout[0];
            for (int row = 0; row < 8; row++) {
                int sprY = s_rankLayout[row + 1] * 2;
                DrawTexturedQuad(sprX, sprY, 0x43FA0000u,
                                 0xE0, 0x20, g_uiTexPage + 1,
                                 0, s_spriteUvY[row], 0x70, 0x10,
                                 VERTEX_WHITE);
            }
        }

        /* Render 8 rows of time digits */
        for (int row = 0; row < 8; row++) {
            int yPos = s_rankLayout[row + 1] * 2;
            int digX = s_rankLayout[0] + 0x12C;
            int timeValue = *(int *)((char *)g_saveBlock +
                s_rankSaveOffset[row] + pageLookup * 4 + charId * 0xA4);
            RenderTimeDigits(digX, yPos, 0, timeValue, 1);
        }

        /* Render navigation arrows */
        RenderRankingNavArrows();

        /* Render track preview model.
         * On GL, use Draw3DModelD3D (0x454C70, tpage vertex batches).
         * Binary params: EAX=0xD7, EDX=-100, EBX=0x200(zBase), ECX=0,
         * stack: [rotation, 0, objPtr, 0, 0, 4(scale)] */
        {
            int savedTrackId = g_trackId;
            if (s_rankPage == 4) {
                g_trackId = 5;
            }
            int modelOff = s_rankModelIndex[s_rankPage] * 0x44;
            char *objBase = (char *)g_objectStructArray;
            int rotation = 0xFFF - fadePhase;
            Draw3DModelD3D(0xD7, (int)0xFFFFFF9C, 0x200, 0, rotation, 0,
                           (intptr_t)(objBase + modelOff), 4);
            if (s_rankPage == 4) {
                g_trackId = savedTrackId;
            }
        }

        /* Render name/podium text */
        {
            int nameY = s_rankNameYOffset[charId];
            RenderCharacterOnPodium(0xD7, nameY, 0x200,
                                    0xF40, fadePhase, 0, p);
        }

        /* fade overlay and present */
        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }
        EndFrame();
        FlipD3D();

        /* Frame counters and FPS limiter */
        g_totalFrames2++;
        g_totalFrames++;

        WaitForFrameCap();

        /* Loop restart: pump events, update timing */
        platform_pump_events();

        g_currentTime = timeGetTime();
        nowSec = timeGetTime() / 1000;
        elapsed = (int)nowSec - (int)startTick;
        if (elapsed < 0) {
            elapsed = -elapsed;
        }
    }
}

/* =====================================================================
 * InitCreditsFontData — 0x004D9DD0 — 3410 bytes
 *
 * Initializes 3 font glyph lookup tables for the unlock screen text.
 * Each block is 256 entries indexed by ASCII code; each entry is
 * {uvX, uvY, pixelWidth} in the texture atlas. Sentinel uvX = -1
 * means unused entry.
 *
 * Block 0 (0x6DA634): standard font — A-Z uvY=0, a-z uvY=0x0C
 * Block 1 (0x6DAC34): bold font    — A-Z uvY=0x56, a-z uvY=0x48
 * Block 2 (0x6DB234): minimal (space only)
 *
 * Binary embeds all data as 336 literal mov-word stores. We represent
 * the same data as static const arrays for readability.
 * ===================================================================== */
static void InitCreditsFontData(void)                          /* 0x4D9DD0 */
{
    /* Step 1: sentinel-clear field 0 of all entries in all 3 blocks */
    for (int blk = 0; blk < 3; blk++) {
        for (int i = 0; i < 256; i++) {
            g_creditsFontGlyphs[blk][i][0] = -1;
        }
    }

    /* Step 2: block 0 — standard font */
    /* Uppercase A-Z (entries 65-90), uvY = 0x00 */
    static const short b0_upper[][2] = { /* {uvX, width} */
        {0x00,6},{0x06,6},{0x0C,6},{0x12,6},{0x18,5},{0x1C,5},  /* A-F */
        {0x22,6},{0x28,6},{0x2E,5},{0x33,5},{0x38,6},{0x3E,5},  /* G-L */
        {0x43,8},{0x4B,7},{0x52,6},{0x58,6},{0x5E,6},{0x64,6},  /* M-R */
        {0x6A,6},{0x70,5},{0x75,6},{0x7B,6},{0x81,9},{0x8A,6},  /* S-X */
        {0x90,6},{0x96,6}                                        /* Y-Z */
    };
    for (int i = 0; i < 26; i++) {
        g_creditsFontGlyphs[0][65+i][0] = b0_upper[i][0];
        g_creditsFontGlyphs[0][65+i][1] = 0x00;
        g_creditsFontGlyphs[0][65+i][2] = b0_upper[i][1];
    }

    /* Lowercase a-z (entries 97-122), uvY = 0x0C */
    static const short b0_lower[][2] = {
        {0x00,6},{0x06,6},{0x0C,6},{0x12,6},{0x18,6},{0x1E,5},  /* a-f */
        {0x23,6},{0x29,6},{0x2F,3},{0x32,4},{0x36,6},{0x3C,4},  /* g-l */
        {0x40,9},{0x49,6},{0x4F,6},{0x55,6},{0x5B,6},{0x61,5},  /* m-r */
        {0x66,6},{0x6C,5},{0x71,6},{0x77,6},{0x7D,9},{0x86,6},  /* s-x */
        {0x8C,6},{0x92,6}                                        /* y-z */
    };
    for (int i = 0; i < 26; i++) {
        g_creditsFontGlyphs[0][97+i][0] = b0_lower[i][0];
        g_creditsFontGlyphs[0][97+i][1] = 0x0C;
        g_creditsFontGlyphs[0][97+i][2] = b0_lower[i][1];
    }

    /* Block 0 special characters */
    /* ' (39) — ROM byte at 0x5050E2 = 0x27 = 39, indexes entry 39 */
    g_creditsFontGlyphs[0][39][0] = 0x9B; g_creditsFontGlyphs[0][39][1] = 0x00; g_creditsFontGlyphs[0][39][2] = 4;
    g_creditsFontGlyphs[0][40][0] = 0xA8; g_creditsFontGlyphs[0][40][1] = 0x00; g_creditsFontGlyphs[0][40][2] = 4; /* ( */
    g_creditsFontGlyphs[0][41][0] = 0xAC; g_creditsFontGlyphs[0][41][1] = 0x00; g_creditsFontGlyphs[0][41][2] = 4; /* ) */
    g_creditsFontGlyphs[0][46][0] = 0x9F; g_creditsFontGlyphs[0][46][1] = 0x00; g_creditsFontGlyphs[0][46][2] = 3; /* . */
    g_creditsFontGlyphs[0][51][0] = 0xA2; g_creditsFontGlyphs[0][51][1] = 0x00; g_creditsFontGlyphs[0][51][2] = 6; /* 3 */
    g_creditsFontGlyphs[0][32][0] = 0xFD; g_creditsFontGlyphs[0][32][1] = 0x00; g_creditsFontGlyphs[0][32][2] = 3; /* space */

    /* Step 3: block 1 — bold font */
    /* Uppercase A-Z (entries 65-90), uvY = 0x56 */
    static const short b1_upper[][2] = {
        {0x00,8},{0x08,8},{0x10,8},{0x18,8},{0x20,7},{0x27,7},   /* A-F */
        {0x2E,8},{0x36,8},{0x3E,6},{0x45,6},{0x4B,8},{0x53,6},   /* G-L */
        {0x59,12},{0x65,9},{0x6E,8},{0x76,8},{0x7E,8},{0x86,8},  /* M-R */
        {0x8E,8},{0x96,8},{0x9E,8},{0xA6,8},{0xAE,12},{0xBA,8},  /* S-X */
        {0xC2,8},{0xCA,8}                                         /* Y-Z */
    };
    for (int i = 0; i < 26; i++) {
        g_creditsFontGlyphs[1][65+i][0] = b1_upper[i][0];
        g_creditsFontGlyphs[1][65+i][1] = 0x56;
        g_creditsFontGlyphs[1][65+i][2] = b1_upper[i][1];
    }

    /* Lowercase a-z (entries 97-122), uvY = 0x48 */
    static const short b1_lower[][2] = {
        {0x00,8},{0x08,8},{0x10,8},{0x18,8},{0x20,8},{0x28,7},   /* a-f */
        {0x2F,8},{0x37,8},{0x3F,4},{0x44,5},{0x49,8},{0x51,5},   /* g-l */
        {0x56,12},{0x62,8},{0x6A,8},{0x72,8},{0x7A,8},{0x82,7},  /* m-r */
        {0x89,8},{0x91,7},{0x98,8},{0xA0,8},{0xA8,12},{0xB4,8},  /* s-x */
        {0xBC,8},{0xC4,8}                                         /* y-z */
    };
    for (int i = 0; i < 26; i++) {
        g_creditsFontGlyphs[1][97+i][0] = b1_lower[i][0];
        g_creditsFontGlyphs[1][97+i][1] = 0x48;
        g_creditsFontGlyphs[1][97+i][2] = b1_lower[i][1];
    }

    /* Block 1 special characters */
    g_creditsFontGlyphs[1][32][0] = 0xFC; g_creditsFontGlyphs[1][32][1] = 0x00; g_creditsFontGlyphs[1][32][2] = 4; /* space */
    g_creditsFontGlyphs[1][39][0] = 0xD2; g_creditsFontGlyphs[1][39][1] = 0x56; g_creditsFontGlyphs[1][39][2] = 5; /* ' */
    g_creditsFontGlyphs[1][46][0] = 0xD7; g_creditsFontGlyphs[1][46][1] = 0x56; g_creditsFontGlyphs[1][46][2] = 4; /* . */

    /* Step 4: block 2 — minimal (only space defined) */
    g_creditsFontGlyphs[2][32][0] = 0xFC; g_creditsFontGlyphs[2][32][1] = 0x00; g_creditsFontGlyphs[2][32][2] = 4;

    /* Step 5: final globals — 0x6DA6F4 region
     * Binary writes {0xFD, 0, 3} and {0xFC, 0, 3} at the end.
     * These correspond to block 0/1/2 entry indices that map to specific
     * screen layout parameters, stored after the main glyph init. */
}

/* =====================================================================
 * Unlock screen ROM data — text strings, portrait types, line heights
 * ===================================================================== */

/* ROM 0x50501C — text string pointers (38 entries).
 * '@0' = standard font line, '@1' = bold font line, '@x' = end. */
static const char *s_creditsTextStrings[] = {                    /* 0x50501C */
    "@0Decomp and port@0jnmartin@0@1Original staff@x",
    "@0Program design and@0implementation@1Jon Burton@x",
    "@0Head artist@1James Cunliffe@x",
    "@0Lead artist@1Dave Burton@x",
    "@0Game design director@1Takashi Iizuka@0SEGA ENTERPRISES LTD.@x",
    "@0Map design director@1Hirokazu Yasuhara@0SEGA OF AMERICA INC.@x",
    "@0Additional artwork@1Kazuyuki Hoshino@0SEGA ENTERPRISES LTD.@x",
    "@0Additional artwork and@0visual advisor@1Shigeru Okada@0SEGA EUROPE LTD.@x",
    "@0Character designer@1Yuji Uekawa@0SEGA ENTERPRISES LTD.@x",
    "@0Music and sound@0producer@1Richard Jaques@0SEGA EUROPE LTD.@x",
    "@0Project director@1Kats Sato@0SEGA EUROPE LTD.@x",
    "@0General producer@1Yuji Naka@0SEGA ENTERPRISES LTD.@x",
    "@0Polygon model design@0and implementation@1Neil Allen@1Dave Burton@1James Cunliffe@x",
    "@0Texture map design@0and application@1Neil Allen@1James Cunliffe@0@0Character animations@1Dave Burton@x",
    "@0Model and animation@0data conversion@1Andy Holdroyd@0@0Terrain system programming@1John Hodskinson@x",
    "@0Artificial intelligence@1Stephen Harding@1Gary Vine@0@0Texture application software@1Andy Holdroyd@x",
    "@0Additional programming@1Andy Holdroyd@1John Hodskinson@1Stephen Harding@1Gary Vine@x",
    "@1SEGA ENTERPRISES LTD.@0@0Producer@1Yuji Naka@0@0Game design director@1Takashi Iizuka@0@0Map design director@1Hirokazu Yasuhara@x",
    "@1SEGA ENTERPRISES LTD.@0@0Game designer@1Syun Nakamura@0@0Game advisors@1Takao Miyoshi@1Katsuhiro Hasegawa@x",
    "@1SEGA ENTERPRISES LTD.@0@0Additional artwork@1Kazuyuki Hoshino@0@0Additional artwork and visual advisor@1Shigeru Okada@0@0Character designers@1Yuji Uekawa@1Yoshitaki Miura@x",
    "@1SEGA ENTERPRISES LTD.@0@0Graphic advisors@1Naoto Oshima@1Hiroshi Nishiyama@0@0Sound advisor@1Naofumi Hataya@0@0Music and sound effects@1Richard Jacques@x",
    "@0Vocals@1T.J.Davis@1(courtesy of Freedom Management)@0Engineered and mixed by@1Matt Howe@0Digital editing by@1Neil Tucker@0Recorded and mixed at@1Metropolis Studios and@1Sega Digital Studio@x",
    "@1PC staff@x",
    "@0Program implementation@1John Hodskinson@x",
    "@0Artwork@1Bev Bush@1Carleen Smith@x",
    "@0Additional artwork@1Leon Warren@1Sean Naden@1Jon Rashid@1Will Thompson@x",
    "@0Network programming@1Jeremy Pardon@0@0Direct3D programming@1Paul Houbart@0@0Additional programming@1Neil Harding@0@x",
    "@0Product manager@1Toshinori Asai@x",
    "@0Producer@1Tetsuo Shinyu@x",
    "@0Director@1Masamitsu Shiino@x",
    "@0Sega Europe Ltd. director@1Richard Lloyd@x",
    "@0European marketing manager@1Hitendra Naik@0@0Assistant european@0product manager@1Steve Wombwell@x",
    "@0Localisation@1Roberto Parraga@1Dave Thompson@1Michael Wiessmuller@x",
    "@0Packaging and manual@0@0Japan@1Kaoru Ichigozaki@1Osamu Nakazato@1Hayato Takebayashi@0Europe@1Paul Jerem@0America@1France Tantiado@x",
    "@0Supervisor@1Yuji Naka@x",
    "@0Special thanks to@1Takashi Iizuka@1Jin Shimazaki@1Kazutoshi Miyake@1Katsuhisa Sato@1Scott Hawkins@0and@1Sonic Team@x",
    "@1developed@1by@x",
    "@0Game developed@0by@1Traveller's Tales@x",
};

/* ROM 0x504300 — portrait type per step (0=none, 1=small, 2=medium, 3=large) */
static const unsigned char s_creditsPortraitType[] = {           /* 0x504300 */
    3,1,1,1, 2,2,2,2, 2,2,2,2, 1,1,1,1,
    1,2,2,2, 2,2,3,1, 1,1,1,2, 2,2,2,2,
    2,2,2,2, 0,0,
};

/* Line height per font block — from ROM 0x5050DC/DE/E0 (upper words) */
static const int s_creditsLineHeight[] = { 12, 14, 10 };        /* blocks 0, 1, 2 */

/* ROM 0x5050E2 — screen X position per step (upper word of dword, used by text centering) */
static const short s_creditsScreenX[] = {                        /* 0x5050E2, stride 4, >>16 */
    0x140, 0x1E0, 0xA0, 0xA0, 0x1E0, 0x1E0, 0xA0, 0xA0,      /* 0-7 */
    0xA0, 0x1E0, 0xA0, 0x1E0, 0x140, 0x140, 0x140, 0x140,     /* 8-15 */
    0x140, 0x140, 0x140, 0x140, 0x140, 0x140, 0x140, 0xA0,     /* 16-23 */
    0x1E0, 0xA0, 0x1E0, 0x140, 0x140, 0x140, 0x140, 0x140,    /* 24-31 */
    0x140, 0x140, 0x140, 0x140, 0x1E0, 0x140,                  /* 32-37 */
};

/* Glyph output buffer — 0x6DB834, max 1024 entries × 8 bytes */
static unsigned char s_glyphBuf[1024 * 8];                      /* 0x6DB834 */

/* =====================================================================
 * RenderCreditsText — 0x004DAB24 — 1263 bytes
 *
 * Renders credits text and optional character portrait for one step
 * of the unlock/credits sequence. Called once per frame from
 * FUN_004DB2C8's render section.
 *
 * Parses the text string for this step, builds a glyph list from
 * the font tables (g_creditsFontGlyphs), then draws glyphs via
 * DrawTexturedQuad (D3D) and optionally a portrait quad.
 * ===================================================================== */
static void RenderCreditsText(int step)                          /* 0x4DAB24 */
{
    unsigned char *buf = s_glyphBuf;                            /* ebx = 0x6DB834 */
    int lineWidthAccum = 0;                                     /* [ebp-0x40] */
    int fontBlock = 0;                                          /* [ebp-0x38] */
    int yPos = 0;                                               /* [ebp-0x28] */
    int glyphCount = 0;                                         /* esi */
    int firstLineProcessed = 0;                                 /* [ebp-0x64] */

    const char *text = s_creditsTextStrings[step];               /* [ebp-0x1C], also ecx */
    const char *lineStart = text;
    int lineHeight = s_creditsLineHeight[fontBlock];             /* [ebp-0x2C] */
    int charWidthAccum = 0;                                     /* [ebp-0x24] — total pixel width */
    int numCharsInLine = 0;                                     /* edi */

    /* Text parsing loop (0x4DAB6B-0x4DACBA) */
    while (1) {
        unsigned char ch = (unsigned char)*text;

        if (ch == '@') {
            /* 0x4DAB78: '@' = line break/control */
            int screenX = s_creditsScreenX[step];                /* [ebp-0x60] */
            int xStart = screenX - (charWidthAccum / 2);       /* centered, [ebp-0x20] */

            /* Render glyphs for this line if we have chars and this isn't the first '@' */
            if (firstLineProcessed && numCharsInLine > 0) {
                for (int i = 0; i < numCharsInLine && glyphCount < 0x400; i++) {
                    unsigned char ascii = (unsigned char)*lineStart;
                    short *entry = g_creditsFontGlyphs[fontBlock][(int)ascii];

                    if (entry[0] != -1) {
                        /* Write 8-byte glyph record */
                        *(short *)(buf + 0) = (short)xStart;
                        *(short *)(buf + 2) = (short)yPos;
                        buf[4] = (unsigned char)entry[0];       /* uvX */
                        buf[5] = (unsigned char)entry[1];       /* uvY */
                        buf[6] = (unsigned char)entry[2];       /* width */
                        buf[7] = (unsigned char)lineHeight;
                        xStart += (int)(short)entry[2] * 2;    /* advance by width*2 */
                        buf += 8;
                        glyphCount++;
                    }
                    lineStart++;
                }
            }

            /* 0x4DAC30: check character after '@' */
            unsigned char nextCh = (unsigned char)*(text + 1);
            if (nextCh == 'x') {
                /* '@x' = end of text */
                lineWidthAccum += lineHeight * 2;
                break;
            }

            if (!firstLineProcessed) {
                firstLineProcessed = 1;
            }
            else {
                /* Add line spacing */
                int lineSpace = lineHeight * 2 + 6;
                lineWidthAccum += lineSpace;
                yPos += lineSpace;
            }

            /* Next char after '@' is font block digit: '0', '1', '2' */
            if (nextCh == '0') {
                fontBlock = 0;
            }
            else {
                fontBlock = nextCh - '0';
            }
            text += 2;                                          /* skip '@' + digit */

            /* Re-read line height for new block, reset line state */
            lineHeight = s_creditsLineHeight[fontBlock];
            lineStart = text;                                   /* next line starts here */
            charWidthAccum = 0;
            numCharsInLine = 0;
        } else {
            /* 0x4DAC84: regular character — measure width */
            short *entry = g_creditsFontGlyphs[fontBlock][(int)ch];
            int glyphWidth = (int)(short)entry[2];              /* [eax+4] >> 16 in binary */
            charWidthAccum += glyphWidth * 2;
            text++;
            numCharsInLine++;
        }
    }

    /* Portrait type setup (0x4DACBC-0x4DAD69) */
    int portraitTpage = -1;                                     /* [ebp-0x4C] = 0xFFFFFFFF */
    int portraitW = 0, portraitH = 0;                           /* [ebp-0x54], [ebp-0x50] */
    int portraitSrcW = 0;                                       /* [ebp-0x68] */
    int portraitType = s_creditsPortraitType[step];

    if (portraitType == 1) {
        portraitSrcW = 0x80; portraitH = 0x69; portraitW = 0x50;
        portraitTpage = 0;
        lineWidthAccum += 0xC0;
    }
    else if (portraitType == 2) {
        portraitSrcW = 0x80; portraitH = 0x82; portraitW = 0x28;
        portraitTpage = 0x69;
        lineWidthAccum += 0x70;
    }
    else if (portraitType == 3) {
        portraitSrcW = 0xA8; portraitH = 0x70; portraitW = 0x48;
        portraitTpage = 0x69;
        lineWidthAccum += 0xB0;
    }

    /* Position computation (0x4DAD6A-0x4DADAF) */
    int posX = s_creditsScreenX[step];                           /* [ebp-0x5C] */
    int posY = 0xF0 - (lineWidthAccum / 2);                    /* [ebp-0x58], Y=0xF0 constant */

    int portraitYStart = posY;                                  /* [ebp-0x28] adjusted */
    if (portraitTpage != -1) {
        portraitYStart += portraitW * 2 + 0x20;
    }
    else {
        portraitYStart += portraitW * 2;
    }
    yPos = portraitYStart;

    /* Adjust glyph Y positions: add yPos offset to all stored glyphs */
    for (int i = 0; i < glyphCount; i++) {
        short *gy = (short *)(s_glyphBuf + i * 8 + 2);
        *gy = (short)(*gy + yPos);
    }

    /* D3D glyph render (0x4DAEC3-0x4DAF0D) */
    {
        unsigned char *gp = s_glyphBuf;
        for (int i = 0; i < glyphCount; i++) {
            short gx = *(short *)(gp + 0);
            short gy = *(short *)(gp + 2);
            int uvX = gp[4];
            int uvY = gp[5];
            int gw  = gp[6];
            int gh  = gp[7];
            gp += 8;

            DrawTexturedQuad(gx, gy, 0x44FA0000,               /* depth = 2000.0f */
                             gw * 2, gh * 2,
                             g_creditsTpageSlots[0],             /* 0x6DA61C */
                             uvX, uvY, gw, gh,
                             VERTEX_WHITE);                       /* light gray tint */
        }
    }

    /* Step 0x24 special: "developed by" decorative quads (0x4DAF17-0x4DAF8B) */
    if (step == 0x24) {
        int specialX = s_creditsScreenX[step];
        /* Top quad: logo area — binary push: color,0x48,0x70,0xA8,0x69,tp,0x90,0xE0,depth */
        DrawTexturedQuad(specialX - 0x70, 0x30,
                         0x44FA0000, 0xE0, 0x90,
                         g_creditsTpageSlots[0],
                         0x69, 0xA8, 0x70, 0x48,
                         VERTEX_WHITE);
        /* Bottom quad: label — binary push: color,0x50,0x69,0x80,0,tp,0xA0,0xD2,depth */
        DrawTexturedQuad(specialX - 0x69, 0x118,
                         0x44FA0000, 0xD2, 0xA0,
                         g_creditsTpageSlots[0],
                         0, 0x80, 0x69, 0x50,
                         VERTEX_WHITE);
    }

    /* Portrait render (0x4DAFDB-0x4DB009) */
    /* Binary push order: color, portraitW, portraitH, portraitSrcW,
     * portraitTpage, tpage, portraitW*2, portraitH*2, depth
     * DrawTexturedQuad reads: width=[ebp+0xC]=portraitH*2,
     * height=[ebp+0x10]=portraitW*2, uvX=portraitTpage,
     * uvY=portraitSrcW, uvW=portraitH, uvH=portraitW */
    if (portraitTpage != -1) {
        int px = posX - portraitH;                              /* [ebp-0x5C] */
        int py = posY;                                          /* [ebp-0x58] */
        DrawTexturedQuad(px, py, 0x44FA0000,
                         portraitH * 2, portraitW * 2,          /* width=H*2, height=W*2 */
                         g_creditsTpageSlots[0],
                         portraitTpage, portraitSrcW,            /* uvX, uvY */
                         portraitH, portraitW,                   /* uvW, uvH */
                         VERTEX_WHITE);
    }
}

/* ROM table 0x5075D4 — 12 texture filenames for unlock/ending slides */
static const char *s_creditsSlideFiles[] = {                     /* 0x5075D4 */
    PATH_END_EMERALDS,   /* [0]  emeralds.raw  */
    PATH_END_SONIC,      /* [1]  sonic.raw     */
    PATH_END_TAILS,      /* [2]  tails.raw     */
    PATH_END_KNUCKLES,   /* [3]  knuckles.raw  */
    PATH_END_AMY,        /* [4]  amy.raw       */
    PATH_END_ROBOTNIK,   /* [5]  robotnik.raw  */
    PATH_END_MSONIC,     /* [6]  msonic.raw    */
    PATH_END_DTAILS,     /* [7]  dtails.raw    */
    PATH_END_MKNUCK,     /* [8]  mknuck.raw    */
    PATH_END_MROBOT,     /* [9]  mrobot.raw    */
    PATH_END_SSONIC,     /* [10] ssonic.raw    */
    PATH_END_THE_END,    /* [11] the_end.raw   */
};

/* =====================================================================
 * ShowCreditsSlide — 0x004DB014 — 674 bytes
 *
 * Generic "show one fullscreen texture page" loop for unlock/ending.
 * EAX = page index into ROM filename tables at 0x5075D4.
 * Returns 1 if user pressed Start to advance, 0 if cancelled (A+Start).
 *
 * Loads the texture, fades in, waits for input or 4-second timeout,
 * fades out, returns.
 * ===================================================================== */
static int ShowCreditsSlide(int pageIndex)                       /* 0x4DB014 */
{
    int result = 1;                                             /* esi = 1 — default return */

    /* 0x4DB05A: D3D texture load path (software path excluded) */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupMenuTexturesD3D();                                     /* 0x438CD8 */
    SetTitleTextureFile(s_creditsSlideFiles[pageIndex]);         /* 0x4DB064: EAX = [esi + 0x5075D4] */
    /* Disable color key on wallpaper tiles BEFORE the load so the eager
     * upload inside LoadTitleTextureD3D writes RGB565 (matching the header
     * compile_tpage_header will produce). If we set noKey after the load,
     * the upload has already laid ARGB1555 bits in VRAM but the header
     * expects RGB565 — and only tpage 0 is keep-pixels so a follow-up
     * re-upload silently bails on the freed buffers for tpages 1..5.
     * Result was rainbow noise on credit-slide character art. */
    for (int t = g_uiTexPage; t < g_uiTexPage + 6; t++) {
        R_SetNoColorKey(t);
    }
    LoadTitleTextureD3D();                                      /* 0x42C3EC */
    ProcessTpageStates();                                            /* 0x4323CC */
    FinalizeMenuTexturesD3D();                                  /* 0x438D10 */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) ResumeCD();
#endif

    /* 0x4DB079: state init */
    g_fadeLevel = -0x100;                                       /* 0x901C44 = 0xFFFFFF00 */
    g_fadeState = FADE_IN;                                      /* 0x901C48 = 1 */
    g_fadeSpeed = 0x0C;                                         /* 0x901C4C = 12 */
    g_totalFrames = 0;                                          /* 0x8FB68C = 0 */
    g_renderEnabled = 1;                                        /* 0x8FB81C = 1 */

    /* 0x4DB0AC: timing base */
    DWORD startTick = timeGetTime() / 1000;                     /* ebp-0x18 */

    while (1) {                                                 /* 0x4DB0BC */
        platform_pump_events();

        /* 0x4DB0D5-0x4DB0FA: elapsed time (absolute seconds) */
        g_currentTime = timeGetTime();                          /* 0x6E9D00 */
        DWORD nowSec = timeGetTime() / 1000;
        int elapsed = (int)nowSec - (int)startTick;
        if (elapsed < 0) {
            elapsed = -elapsed;                    /* cdq; xor; sub = abs() */
        }

        /* 0x4DB0F4: CD music check */
        if (g_creditsStepCounter != 0) {                         /* [0x6DA62C] */
            int cdStat = GetLogicalCDTrack();                       /* 0x4D0100 */
            if (cdStat != 0x12) {
                UpdateCDPlayback(0x12);                         /* 0x4D01AC — play track 18 */
            }
        }

        /* 0x4DB114: fade update */
        if (g_fadeState != 0) {                                 /* [0x901C48] */
            UpdateFade();                                       /* 0x4305D4 */
        }

        /* 0x4DB122: early exit when fully faded out */
        if (g_fadeLevel == -0x100) {                            /* 0xFFFFFF00 */
            return result;
        }

        /* 0x4DB13A: input */
        ReadInput();                                            /* 0x477228 */

        /* 0x4DB13F: input handling (only when fade visible) */
        if (g_fadeState == 0) {                                 /* FADE_VISIBLE */
            /* 0x4DB14D: auto-advance after 4 seconds */
            if (elapsed > 4) {
                g_fadeState = FADE_OUT;                         /* 0x901C48 = 2 */
            }

            /* 0x4DB15C: A+Start = cancel */
            if ((g_inputBits & 8) && (g_inputBits & 6)) {      /* 0x4DB162-0x4DB16A */
                g_fadeState = FADE_OUT;                         /* 0x4DB173 */
                result = 0;                                     /* 0x4DB171: xor esi,esi */
            }
            /* 0x4DB17B: Start alone = advance */
            else if (g_inputBits & 8) {                         /* 0x4DB17B-0x4DB182 */
                g_fadeState = FADE_OUT;                         /* 0x4DB18E */
                result = 1;                                     /* 0x4DB189 */
            }
        }

        /* 0x4DB193: render (D3D path) */
        g_renderPass = 1;                                       /* 0x6D9A4C = 1 */

        ProcessTpageStates();                                        /* 0x4DB210 */
        BeginFrame();                                           /* 0x4DB215: 0x42299C */
        SetViewportFromConfig(g_viewportArray + 5 * 21);        /* 0x4DB21A: EAX = 0x8FB50C */
        RenderBackground();                                     /* 0x4DB226: EAX = 1 (ebx) */
        EndFrame();                                             /* 0x4DB22B: 0x422BA0 */

        SetViewportFromConfig(g_viewportArray + 4 * 21);        /* 0x4DB230: EAX = 0x8FB4B8 */
        if (g_fadeLevel < 0) {                                  /* 0x4DB23A */
            RenderFadeOverlay();                                /* 0x4DB243: 0x461DF4 */
        }

        BeginFrame();                                           /* 0x4DB248 */
        RenderLogoQuads();                                      /* 0x4DB24D: 0x46154C */
        EndFrame();                                             /* 0x4DB252 */
        FlipD3D();                                              /* 0x4DB257: 0x4356BC */

        /* 0x4DB26A: frame counters */
        g_totalFrames2++;                                       /* 0x8FB690 */
        g_totalFrames++;                                        /* 0x8FB68C */

        WaitForFrameCap();
    }
}

/* =====================================================================
 * RunCreditsStep — 0x004DB2C8 — 1842 bytes
 *
 * Runs one step of the unlock/credits sequence. Displays text + optional
 * character model with fade in/out. Returns 1 to advance, 0 to cancel.
 *
 * EAX = step index (0-0x24). Steps 0x17-0x1A show specific characters.
 * Step 0x24 shows all unlocked characters in a circle.
 * ===================================================================== */
static int RunCreditsStep(int step)                              /* 0x4DB2C8 */
{
    int result = 1;                                             /* [ebp-0x38] */
    int showTexture;                                            /* [ebp-0x34] */
    int texPosY;                                                /* [ebp-0x48] = ROM Y */
    int texPosX;                                                /* [ebp-0x44] = ROM X (possibly swapped) */
    int tpageMode;                                              /* [ebp-0x40] */
    int texOffX;                                                /* [ebp-0x30] */
    int texOffY;                                                /* [ebp-0x3C] */

    /* 0x4DB2D9: read screen X from ROM position table */
    int screenX = s_creditsScreenX[step];                        /* [ebp-0x44] */

    if (screenX == 0x140) {
        /* 0x4DB37F: center = no texture quad, text only */
        showTexture = 0;
    }
    else {
        /* 0x4DB2F1: read Y position (always 0xF0 from our table) */
        texPosY = 0xF0;                                        /* [ebp-0x48] */
        showTexture = 1;

        /* 0x4DB305: swap X position (left↔right for alternation) */
        if (screenX == 0xA0) {
            texPosX = 0x1E0;
        }
        else if (screenX == 0x1E0) {
            texPosX = 0xA0;
        }
        else {
            texPosX = screenX;
        }

        /* 0x4DB325: layout params from counter */
        if (step == 0x24) {
            /* 0x4DB32B: celebration screen — fixed center layout */
            texOffX = 0;
            texOffY = 0x80;
            tpageMode = 2;
        }
        else {
            /* 0x4DB342: compute from g_creditsStateFlag cycling counter */
            int f = g_creditsStateFlag;                          /* [0x6DA630] */
            tpageMode = (f + (f < 0 ? 3 : 0)) >> 2;           /* signed div by 4 (rounding) */
            texOffX = (f & 1) << 7;                             /* bit0 * 128 */
            texOffY = (((f & 3) + ((f & 3) < 0 ? 1 : 0)) >> 1) << 7; /* (f&3)/2 * 128 */
        }
    }

    /* 0x4DB384: advance cycling counter mod 10 */
    g_creditsStateFlag = (g_creditsStateFlag + 1) % 10;

    /* 0x4DB3A1: Y position override for specific steps */
    if (step == 0x18 || step == 0x1A || step == 0x24) {
        texPosY = 0xA0;
    }
    else if (step == 0x17 || step == 0x19) {
        texPosY = 0x140;
    }

    /* 0x4DB3CD: fade + timing init */
    g_fadeLevel = -0x100;
    g_fadeState = FADE_IN;
    g_fadeSpeed = 0x0C;
    g_renderEnabled = 1;
    g_totalFrames = 0;

    unsigned int sinePhase = timeGetTime() & 0xFFF;            /* esi = low 12 bits */
    DWORD startTick = timeGetTime() / 1000;

    /* 0x4DB428: character model setup */
    if (step >= 0x17 && step <= 0x1A) {
        /* 0x4DB551: dispatch table for steps 23-26 */
        Player *pb = g_playerBase;
        switch (step) {
            /* animId 6 = ANIM_WIN_POSE, faithful to the binary (EBX=6). A prior
            * workaround forced 2 (ANIM_STILL) to hide a broken-limb glitch on
            * this screen — under test now that animation.c is a full translation. */
            case 0x17:                                              /* 0x4DB455 */
                SetPlayerCharacter(0, 1, 6);                              /* EAX=0, EDX=1, EBX=6 */
                pb->posX = 0xA0;                                   /* 0x8FD4F4 */
                pb->posY = 0x82;                                   /* 0x8FD4F8 */
                pb->posZ = 0x200;                                  /* 0x8FD4FC */
                g_numPlayers = 1;                                   /* 0x6E990C */
                break;
            case 0x18:                                              /* 0x4DB496 */
                SetPlayerCharacter(0, 3, 6);
                pb->posX = -160;                                   /* 0xFFFFFF60 */
                pb->posY = -150;                                   /* 0xFFFFFF6A */
                pb->posZ = 0x200;
                g_numPlayers = 1;
                break;
            case 0x19:                                              /* 0x4DB4D6 */
                SetPlayerCharacter(0, 2, 6);
                pb->posX = 0xA0;
                pb->posY = 0x82;
                pb->posZ = 0x200;
                g_numPlayers = 1;
                break;
            case 0x1A:                                              /* 0x4DB517 */
                SetPlayerCharacter(0, 0, 6);
                pb->posX = -160;
                pb->posY = -150;
                pb->posZ = 0x200;
                g_numPlayers = 1;
                break;
        }
    }
    else if (step == 0x24) {
        /* 0x4DB444: all-unlocked celebration — init each unlocked char */
        g_numPlayers = 0;
        for (int ci = 0; ci < 10; ci++) {
            if (g_charUnlockTable[ci] == 2) {
                SetPlayerCharacter(g_numPlayers, ci, 0);              /* EAX=slot, EDX=charId, EBX=0 */
                g_numPlayers++;
            }
        }
    }
    else {
        /* 0x4DB58D: no character models for this step */
        g_numPlayers = 0;
    }

    /* 0x4DB595: texture quad position offsets */
    int quadOffY = texPosY - 0x80;                              /* [ebp-0x2C] */
    int quadOffX = texPosX - 0x80;                              /* [ebp-0x28] */

    /* Main loop (0x4DB5AB-0x4DB9F5) */
    while (1) {
        platform_pump_events();

        g_currentTime = timeGetTime();
        DWORD nowSec = timeGetTime() / 1000;
        int elapsed = (int)nowSec - (int)startTick;
        if (elapsed < 0) {
            elapsed = -elapsed;
        }

        /* CD check */
        int cdStat = GetLogicalCDTrack();
        if (cdStat != 0x12) {
            UpdateCDPlayback(0x12);
        }

        /* Fade */
        if (g_fadeState != 0) {
            UpdateFade();
        }

        /* Early exit */
        if (g_fadeLevel == -0x100) {
            return result;
        }

        /* Input */
        ReadInput();

        if (g_fadeState == 0) {
            /* Timeout: 20s for step 0x24, 4s for others */
            int timeout = (step == 0x24) ? 0x14 : 4;
            if (elapsed > timeout) {
                g_fadeState = FADE_OUT;
            }

            /* A+Start = cancel */
            if ((g_inputBits & 8) && (g_inputBits & 6)) {
                g_fadeState = FADE_OUT;
                result = 0;
            }
            /* Start alone = advance */
            else if (g_inputBits & 8) {
                g_fadeState = FADE_OUT;
                result = 1;
            }
        }

        /* Character animation (0x4DB693-0x4DB7A5) */
        g_renderPass = 1;

        if (g_numPlayers > 0) {
            UpdateFrameTimers();                                /* 0x47FDE8 */

            /* Per-model anim + lighting update */
            for (int mi = 0; mi < g_numPlayers; mi++) {
                AdvancePlayerAnimation(mi);                     /* 0x4D9D48 */
                UpdateVertexLighting(&g_playerBase[mi]);       /* 0x430638 */
            }

            if (g_numPlayers == 1) {
                /* 0x4DB6EB: single model — simple rotation */
                int rot = (g_totalFrames2 & 0x7F) << 5;        /* 0x8FD504 */
                g_playerBase[0].angleYaw = rot;               /* player[0].rotation */
                if (step & 1) {
                    g_playerBase[0].angleYaw = 0xFFF - rot;   /* mirror for odd steps */
                }
            }
            else if (g_numPlayers > 1) {
                /* 0x4DB71E: multiple models — distribute in circle */
                int angleStep = 0x1000 / g_numPlayers;         /* [ebp-0x20] */
                sinePhase = (sinePhase + 0x20) & 0xFFF;

                int phase = sinePhase;
                for (int mi = 0; mi < g_numPlayers; mi++) {
                    int rotation = (phase + 0x800) & 0xFFF;
                    int posX = ((g_sinTable[phase] * 0x100) >> 14) - 0x180;
                    int posY = -350;                            /* 0xFFFFFEA2 */
                    int posZ = ((g_cosTable[phase] * 0x100) >> 14) + 0x500;

                    Player *pm = &g_playerBase[mi];
                    pm->angleYaw = rotation;                    /* player.rotation */
                    pm->posX = posX;
                    pm->posY = posY;
                    pm->posZ = posZ;

                    phase = (phase + angleStep) & 0xFFF;
                }
            }
        }

        /* D3D render (0x4DB8A8-0x4DB9AD) */
        ProcessTpageStates();

        /* Pass 1: background */
        BeginFrame();
        SetViewportFromConfig(g_viewportArray + 5 * 21);
        RenderBackground();
        EndFrame();

        /* Pass 2: fade + content */
        SetViewportFromConfig(g_viewportArray + 4 * 21);
        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }

        BeginFrame();

        /* Texture quad (if visible) — binary uses [0x6DA620] = slots[1] */
        if (showTexture) {
            DrawTexturedQuad(quadOffX, quadOffY, 0x44FA0000,    /* depth=2000.0f */
                             0x100, 0x100,
                             g_creditsTpageSlots[1] + tpageMode, /* views00-02 */
                             texOffX, texOffY,
                             0x80, 0x80,
                             VERTEX_WHITE);
        }

        /* Text overlay */
        RenderCreditsText(step);

        /* Character models — binary: EAX=posX, EDX=posY, EBX=posZ, ECX=0,
         * stack: rotation, 0, playerPtr */
        for (int mi = 0; mi < g_numPlayers; mi++) {
            Player *pm = &g_playerBase[mi];
            RenderCharacterCredits(pm->posX, pm->posY, pm->posZ, 0,
                                   pm->angleYaw, 0, pm);
        }

        EndFrame();
        FlipD3D();

        /* Frame counters + busy wait */
        g_totalFrames2++;
        g_totalFrames++;

        WaitForFrameCap();
    }
}

/**
 * CreditsScreen — 0x004DB9FC — 542 bytes
 * Shows end credits after winning on Radiant Emerald or completing
 * all character unlocks.
 *
 * Calls InitCreditsFontData, loads textures, then runs through up to
 * 37 credits steps via RunCreditsStep. Finishes with ShowCreditsSlide(11)
 * for the "THE END" screen.
 */
int CreditsScreen(void)
{
    /* Ensure viewport slots 4 and 5 are valid — ShowCreditsSlide and
     * RunCreditsStep use SetViewportFromConfig with these slots.
     * The binary relies on D3D surface init populating them; our GL port
     * must copy from slot 0 (always valid after InitTitleScreen). */
    for (int i = 0; i < 21; i++) {
        g_viewportArray[4*21 + i] = g_viewportArray[i];
        g_viewportArray[5*21 + i] = g_viewportArray[i];
    }

    /* 0x4DBA01: init font glyph tables */
    InitCreditsFontData();

    /* 0x4DBA21: set tpage config */
    g_creditsStateFlag = 0;                                      /* 0x6DA630 */
    g_tpageCharacters = 0;                                      /* 0x8F6C30 */
    g_tpagePlayfield1 = 1;                                     /* 0x8F6C34 */
    g_creditsTpageSlots[0] = 2;                                  /* 0x6DA61C */
    g_creditsTpageSlots[1] = 3;                                  /* 0x6DA620 */
    g_creditsTpageSlots[2] = 4;                                  /* 0x6DA624 */
    g_creditsTpageSlots[3] = 5;                                  /* 0x6DA628 */
    g_uiTexPage = 6;                                            /* 0x8F6C48 */
    g_creditsStepCounter = 0;                                    /* 0x6DA62C */

    /* 0x4DBABF: D3D texture loading — 6 tpages from binary strings */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupD3DTexturesBegin();                                /* 0x438CA4 */
    LoadTPageRGB(0, PATH_GENERAL "PLAYER00.RAW");           /* 0x532EDB */
    LoadTPageRGB(1, PATH_GENERAL "PLAYER01.RAW");           /* 0x532EF0 */
    LoadTPageRGB(2, DATA_DIR SEP "BIN" SEP "CREDITS" SEP "CREDIT00.RAW"); /* 0x532F05 */
    LoadTPageRGB(3, DATA_DIR SEP "BIN" SEP "CREDITS" SEP "VIEWS00.RAW");  /* 0x532F1E */
    LoadTPageRGB(4, DATA_DIR SEP "BIN" SEP "CREDITS" SEP "VIEWS01.RAW");  /* 0x532F36 */
    LoadTPageRGB(5, DATA_DIR SEP "BIN" SEP "CREDITS" SEP "VIEWS02.RAW");  /* 0x532F4E */
    ProcessTpageStates();                                        /* 0x4323CC / 0x4DBB15 */
    FinalizeMenuTexturesD3D();                              /* 0x438D10 / 0x4DBB1A */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) ResumeCD();
#endif

    /* 0x4DBB1F: init track collision + character lighting */
    RemapCharacterTpages();
    LoadCharacterGouraudTables();
    TintCharacterGouraudTables(0, 0, 0);                   /* EAX=0, EDX=0, EBX=0 */

    R_SetBlendMode(R_BLEND_ALPHA);

    /* 0x4DBB36: check if we should show initial slide */
    if (g_gpResultFlag != 0) {
        /* 0x4DBB45: show emeralds slide first */
        if (ShowCreditsSlide(0) == 0) {
            goto done;
        }
    }

    /* 0x4DBB54: play CD track 18, set step counter */
    UpdateCDPlayback(0x12);                                     /* 0x4D01AC */
    g_creditsStepCounter = 1;                                    /* 0x6DA62C */

    /* 0x4DBB69: ALWAYS show character slide (charId + 1) */
    int slideIdx = (short)g_playerBase->charId + 1;        /* 0x8FD5E6 */
    if (ShowCreditsSlide(slideIdx) == 0) {
        goto done;
    }

    /* 0x4DBB7F: 8-bit palette reload — skipped on D3D/GL path */
    /* (binary jumps to 0x4DBBEE when not 8-bit) */

    /* 0x4DBBEE: main credits loop — steps 0 through 0x24 */
    int step = 0;
    while (step < 0x25) {
        int ret = RunCreditsStep(step);
        if (ret == 0) {
            goto done;
        }
        step++;
    }

    /* 0x4DBC01: final "THE END" slide */
    ShowCreditsSlide(11);

done:
    /* 0x4DBC0B: cleanup */
    StopCD();  /* 0x4D0264 */

    /* Restore color keying on tpages used for credits slides.
     * ShowCreditsSlide disables it (R_SetNoColorKey) so green wallpaper
     * pixels aren't keyed out, but the flag persists after credits and
     * corrupts gameplay textures when those tpage indices are reused. */
    for (int t = g_uiTexPage; t < g_uiTexPage + 6; t++) {
        R_ClearNoColorKey(t);
    }

    R_SetBlendMode(R_BLEND_NONE);

    return 1;                                                   /* 0x4DBC10 */
}

/* =====================================================================
 * NetworkScreen helper functions
 * ===================================================================== */

/* Shared global aliases for NetworkScreen — these binary addresses are
 * reused by ResultsScreen under different variable names. */
/* g_netGameStarted at 0x68ACE4 — declared in sonicr_globals.h */
/* g_currentPlayerIdx at 0x68ACD8 — declared in sonicr_globals.h */
extern int g_palArraySrc1[];            /* 0x68A5E4 — palette copy source 1 */
extern int g_palArrayDst[];             /* 0x689CC0 — palette copy destination */
/* g_playerSlotIds/g_playerCharIds removed — fields inside g_netPlayerDecorations.
 * NET_DECO_* layout defines in net_transport.h. */

/* New globals from globals_extra.c */
extern int g_netMenuState;              /* 0x689AFC */
extern int g_netJoinedFlag;             /* 0x68A6E4 */
extern int g_netLobbyCounter;           /* 0x68A898 */
extern int g_netSavedCharId;            /* 0x68AFE0 */
extern int g_netSavedTrackIdx;          /* 0x68AFE4 */
extern int g_netSavedModeIdx;          /* 0x68AFE8 */
extern int g_netBroadcastCharId;        /* 0x68ACE0 */
extern int g_netSavedButtonByte;        /* 0x68AFAC */
extern int g_netLobbyPortrait[];        /* 0x68A730 — stride 0x48, 5 slots */
extern int g_netLobbyCharData[];        /* 0x68A854 — DPID + 16 ints */
extern int g_netSoundIndexBuf[];        /* 0x68A3D8 */
/* g_menuPlayer.animId, g_menuPlayer._unk_0x1E0, g_menuPlayer.animFrameIdx, g_menuPlayer._unk_0x1C,
 * g_menuPlayer._unk_0x9C and g_menuPlayer.angleYaw are field aliases on g_menuPlayer —
 * declared in sonicr_globals.h. */
extern int g_screenBaseTime;            /* 0x6E9D00 */
extern int g_screenFPS;                 /* 0x6E9CDC */
/* g_netProviderChoice is g_stateBlock92528C[0] — aliased in sonicr_globals.h */

static void DrawPixText(const char *s, int x, int y, int pixSz, uint32_t color);

/* On-screen matchmaker debug overlay */
#define NET_DBG_LINES 6
#define NET_DBG_LEN 64
static char s_netDbg[NET_DBG_LINES][NET_DBG_LEN];
static int s_netDbgCount = 0;

static void NetDbgPush(const char *fmt, ...)
{
    if (s_netDbgCount < NET_DBG_LINES) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(s_netDbg[s_netDbgCount], NET_DBG_LEN, fmt, ap);
        va_end(ap);
        s_netDbgCount++;
    }
    else {
        for (int i = 0; i < NET_DBG_LINES - 1; i++) {
            memcpy(s_netDbg[i], s_netDbg[i + 1], NET_DBG_LEN);
        }
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(s_netDbg[NET_DBG_LINES - 1], NET_DBG_LEN, fmt, ap);
        va_end(ap);
    }
}

static void NetDbgClear(void)
{
    s_netDbgCount = 0;
}

static void NetDbgDraw(void)
{
    (void)s_netDbgCount;
}

/* Wall-clock second of the client's last CHAR_CHANGE broadcast; the lobby
 * repeats the pick once a second because the packet is unacknowledged. */
static unsigned int s_charResendSec;

/* NetworkScreen state aliases */
#define ns_lobbyState      g_resultsState          /* 0x68AFD4 */
#define ns_setupMode       g_netCharSelectState    /* 0x68AFD0 */
#define ns_lobbyEntryCount g_resultsPlayerCount    /* 0x68AFDC */
#define ns_slotQueueIdx    g_resultsTextLineCount  /* 0x68AFD8 */
#define ns_connectionMode  g_stateBlock92528C[4]    /* 0x92529C — shared block, also the
                                                    * options-menu scroll/ESC slot */

/* ROM strings referenced by NetworkScreen (DGROUP addresses).
 * In the binary these are string pointers pushed before calls. On SDL
 * we pass string literals matching the original content. */
#define NS_STR_SESSION_NAME   "SonicR"            /* 0x530431 */
#define NS_STR_SESSION_PW     "SonicR"            /* 0x53042C */
#define NS_STR_MODEM_SESSION  "ModemSession"      /* 0x53043F */
#define NS_STR_JOIN_SESSION   "JoinSession"       /* 0x530438 */

/**
 * ShiftLobbyHistory — 0x0048A630 — 112 bytes
 * Shifts entries in the lobby buffer (0x689BB8) upward by 0x104 bytes
 * per entry, making room for new entries at the front.
 */
static void ShiftLobbyHistory(void)                         /* 0x48A630 */
{
    int count = ns_lobbyEntryCount;                         /* [0x68AFDC] */
    if (count < 5) {                                         /* cmp edx, 5 */
        ns_lobbyEntryCount = count + 1;
    }

    count = ns_lobbyEntryCount;
    if (count <= 1) {
        return;                                 /* jle 0x48a699 */
    }

    /* Shift each entry up by 0x104 bytes (65 ints) */
    for (int i = count; i > 1; i--) {                       /* 0x48a664 */
        int dstOff = i * (0x104 / 4);                       /* shl eax,6; add eax,esi; shl eax,2 */
        int srcOff = (i - 1) * (0x104 / 4);
        for (int j = 1; j < (0x104 / 4); j++) {             /* 0x48a671 */
            g_resultsPlayerData[dstOff + j] = g_resultsPlayerData[srcOff + j];
        }
    }
}

/**
 * CopyPaletteSrcToDst — 0x0048A6A0 — 27 bytes
 * Copies 0x100 bytes from g_palArraySrc1 to g_palArrayDst.
 */
static void CopyPaletteSrcToDst(void)                       /* 0x48A6A0 */
{
    for (int i = 1; i <= 64; i++) {                         /* 0x48a6a3 loop */
        g_palArrayDst[i] = g_palArraySrc1[i];               /* [0x689CC0+i*4] = [0x68A5E4+i*4] */
    }
}
/**
 * CopyResultsToDst — 0x0048A6BC — 27 bytes
 * Copies 0x100 bytes from g_resultsPlayerData to g_palArrayDst.
 */
static void CopyResultsToDst(void)                           /* 0x48A6BC */
{
    for (int i = 1; i <= 64; i++) {                          /* 0x48a6bf loop */
        g_palArrayDst[i] = g_resultsPlayerData[i];           /* [0x689CC0+i*4] = [0x689BBC+i*4] */
    }
}

/**
 * PrependToResultsBuffer — 0x0048A750 — 105 bytes
 * Counts entries in g_portraitTextBuffer (sentinel-terminated),
 * shifts g_resultsPlayerData right to make room, then copies
 * entries from g_netFilteredProviders to the front.
 */
static void PrependToResultsBuffer(void)                    /* 0x48A750 */
{
    /* Count sentinel-terminated entries in g_portraitTextBuffer */
    int count = 0;                                           /* ecx */
    for (int off = 0; ; off++) {                             /* 0x48a758 */
        if (g_portraitTextBuffer[off] == -1) break;
        count++;
    }

    /* Shift g_resultsPlayerData right by count entries */
    int shiftBytes = (64 - (count + 1));                     /* 0x48a76d-0x48a774 */
    for (int i = shiftBytes; i >= 0; i--) {                  /* 0x48a780 */
        g_resultsPlayerData[i + 1] = g_resultsPlayerData[i - count * 1 + 1];
    }

    /* FIXME: the shift logic above is an approximation of the binary's
     * pointer arithmetic. The binary shifts 64-count DWORDs downward by
     * count positions, then copies count entries from g_netFilteredProviders
     * into the vacated front. For the lobby this is non-critical since
     * we'll rework the text buffer system anyway. */

    /* Copy g_netFilteredProviders entries to front */
    for (int i = 1; i <= count; i++) {                       /* 0x48a797 */
        g_resultsPlayerData[i] = g_netFilteredProviders[i];
    }
    g_resultsTextBuffer[count] = 0x10026;                    /* sentinel */
}

/**
 * FilterNetworkProviders — 0x0048A7BC — 239 bytes
 * Processes ROM table at 0x502598, filters provider entries into
 * g_netFilteredProviders, optionally calls EnumNetworkSessions.
 * Returns 1 if providers found, 0 if table exhausted.
 */
static int FilterNetworkProviders(void)                     /* 0x48A7BC */
{
    /* Binary walks ROM table at 0x502598 matching DirectPlay service
     * provider GUIDs against g_netServiceProviders[].  The table stores
     * 32-bit pointers as ints — dereferencing them on 64-bit would
     * crash.  On SDL there are no DirectPlay providers, so
     * g_netServiceProviders[] is always empty/sentinel.  Short-circuit
     * and return 0 (no providers found).
     *
     * When we build a real UDP provider list, this function should
     * populate g_netFilteredProviders[] from that list instead. */

    /* If in lobby state 2, re-enumerate sessions (the one useful side
     * effect the binary version has). */
    if (ns_lobbyState == 2) {                                /* 0x48a85d */
        EnumNetworkSessions(0);
    }

    return 0;                                                /* no providers */
}

extern int ScanTypedLetter(void);

/* ROM data at 0x5024c4 — default sound indices (21 entries + sentinel) */
const int s_defaultSoundIndices[] = {
    36, 7, 0, 18, 36, 9, 14, 8, 13, 4, 3, 36, 19, 7, 4, 36, 6, 0, 12, 4, 38, -1
};

/* ROM default sound indices (21 entries + sentinel) — leaf_small.c */
extern const int s_defaultSoundIndices[];               /* 0x5024C4 — leaf_small.c */

/**
 * BuildSoundIndexList — 0x0048A5C8 — 102 bytes
 * Copies sentinel-terminated data from g_netLobbyCharData (0x68A858)
 * into g_netSoundIndexBuf (0x68A3DC), then appends ROM defaults
 * from 0x5024C4. Both lists terminated by -1.
 */
static void BuildSoundIndexList(void)                       /* 0x48A5C8 */
{
    /* Stack buffer for ROM copy (22 ints) */
    int romBuf[22];
    for (int i = 0; i < 22; i++) {                            /* rep movsd */
        romBuf[i] = s_defaultSoundIndices[i];
    }

    /* Copy from g_netLobbyCharData (0x68A858) to g_netSoundIndexBuf (0x68A3DC) */
    int *src = &g_netLobbyCharData[1];                       /* 0x68A858 = netLobbyCharData[1] */
    int *dst = &g_netSoundIndexBuf[1];                       /* 0x68A3DC = netSoundIndexBuf[1] */
    if (g_netLobbyCharData[1] != -1) {                       /* 0x48a5ee */
        while (*src != -1) {
            *dst++ = *src++;
        }
    }

    /* Append ROM defaults */
    int *romSrc = romBuf;
    if (*romSrc != -1) {                                     /* 0x48a60c */
        while (*romSrc != -1) {
            *dst++ = *romSrc++;
        }
    }
    *dst = -1;                                               /* terminate */
}

/* Shared 5×7 pixel font
 * Used by both RenderSignInScreen and the NetworkScreen state-0 prompt.
 * Letters: 'a-z' and 'A-Z' both map to the same uppercase glyphs (idx 0-25).
 * Digits '0'-'9' → idx 26-35.  '.' → idx 36.  '/' → idx 37. */
static const uint8_t s_pixFont[40][7] = {
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
        {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
        {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
        {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, /* G */
        {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
        {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
        {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
        {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, /* S */
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
        {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}, /* V */
        {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
        {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
        {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
        {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, /* 2 */
        {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 3 */
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
        {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
        {0x1F,0x01,0x02,0x04,0x04,0x04,0x04}, /* 7 */
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
        {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* . */
        {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, /* / */
        {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /*  */
        {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* ? */
};

static int PixFontIndex(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    if (c == '.') return 36;
    if (c == '/') return 37;
    if (c == '-') return 38;
    if (c == '?') return 39;
    return -1;
}

/* Matchmaker session picker (state 3) state — file-scope so both the
 * state-machine block and the render block can read/write them. */
static int s_pickerCursor = 0;
static int s_pickerRefreshFrame = 0;
static unsigned char s_pickerLastInput = 0xFF;
static int s_pickerPrevState = -1;

/* Convert a C string to the in-game-font glyph-ID array.
 * Mapping: a-z/A-Z → 0..25, 0-9 → 26..35.  Other chars are skipped.
 * Writes a -1 sentinel after the last glyph.  Returns glyph count. */
/* On-screen key hints of the network screens. The lobby's actions are PC
 * F-keys; on the Xbox the pad drives them (see NetSynthPadKeys). */
#ifdef SONICR_XBOX
#define NETTXT_LAN_PROMPT   "PRESS Y FOR LAN PLAY"
#define NETTXT_HOST         "PRESS A TO HOST"
#define NETTXT_JOIN         "PRESS RT TO JOIN"
#define NETTXT_PICKER_HINT  "UP/DOWN SELECT - A JOIN - Y LAN - B BACK"
#define NETTXT_LANHOST      "PRESS Y TO HOST LAN ONLY"
#else
#define NETTXT_LAN_PROMPT   "PRESS F3 FOR LAN PLAY"
#define NETTXT_HOST         "PRESS F1 TO HOST"
#define NETTXT_JOIN         "PRESS F2 TO JOIN"
#define NETTXT_PICKER_HINT  "UP/DOWN SELECT - ENTER JOIN - F3 LAN - ESC BACK"
#define NETTXT_LANHOST      "PRESS F3 TO HOST LAN ONLY"
#endif

static int StringToGlyphIds(const char *s, int *out, int maxOut)
{
    int n = 0;
    for (int i = 0; s[i] && n < maxOut - 1; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z')      out[n++] = c - 'a';
        else if (c >= 'A' && c <= 'Z') out[n++] = c - 'A';
        else if (c >= '0' && c <= '9') out[n++] = 26 + (c - '0');
    }
    out[n] = -1;
    return n;
}

/* Mirror of DrawGlyphString's per-glyph advance, without drawing.
 * Returns the pixel width the glyph string would occupy. */
static int MeasureGlyphString(const int *glyphIds, int maxCount)
{
    int x = 0;
    for (int i = 0; i < maxCount; i++) {
        int id = glyphIds[i];
        if (id == -1) break;
        int lowId = id & 0xFFFF;
        int uvW;
        if ((id & 0xFFFF0000) != 0) {
            int *mapEntry = (int *)((char *)g_romCharMap + lowId * 12);
            if (mapEntry[0] == -1) continue;
            uvW = mapEntry[2];
        } else {
            int *glyphEntry = (int *)((char *)g_romGlyphTable + lowId * 16);
            uvW = glyphEntry[2];
        }
        x += uvW * 2;
    }
    return x;
}

/* Set while a network screen fades out. The pixel-font text is drawn at
 * z=0.01, in front of the iris fade (z=1.1), so it used to stay on the black
 * screen until the next menu had loaded. */
static int s_pixTextHidden;

static void DrawPixText(const char *s, int x, int y, int pixSz, uint32_t color)
{
    if (s_pixTextHidden) return;
    int charW = 5 * pixSz + pixSz;
    float z = 0.01f;
    for (int k = 0; s[k]; k++) {
        int idx = PixFontIndex(s[k]);
        if (idx < 0) { x += charW; continue; }
        const uint8_t *glyph = s_pixFont[idx];
        for (int row = 0; row < 7; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (0x10 >> col)) {
                    R_DrawQuad2DSolid(
                        x + col * pixSz,
                        y + row * pixSz,
                        x + (col + 1) * pixSz,
                        y + (row + 1) * pixSz,
                        z, color);
                }
            }
        }
        x += charW;
    }
}

/* Public wrapper so the demo/debug overlay in main.c can draw pixel text. */
void DrawDebugOverlayText(const char *s, int x, int y, int pixSz, uint32_t color)
{
    DrawPixText(s, x, y, pixSz, color);
}

static int PixTextWidth(const char *s, int pixSz)
{
    int charW = 5 * pixSz + pixSz;
    int len = 0;
    while (s[len]) len++;
    return len * charW;
}

/* ─ Sign-in QR rendering helper ─ */
static void RenderSignInScreen(const uint8_t *qrBuf, int qrSize,
                               const char *code, int authState)
{
    int moduleSize = 4;
    int qrPixels = qrSize * moduleSize;
    int originX = (640 - qrPixels) / 2;
    int originY = (480 - qrPixels) / 2;

    int pad = moduleSize * 2;
    float z = 0.01f;

    R_DrawQuad2DSolid(originX - pad, originY - pad,
                      originX + qrPixels + pad, originY + qrPixels + pad,
                      z, 0xFFFFFFFF);

    for (int y = 0; y < qrSize; y++) {
        for (int x = 0; x < qrSize; x++) {
            if (qrcodegen_getModule(qrBuf, x, y)) {
                R_DrawQuad2DSolid(originX + x * moduleSize,
                                  originY + y * moduleSize,
                                  originX + (x + 1) * moduleSize,
                                  originY + (y + 1) * moduleSize,
                                  z, 0xFF000000);
            }
        }
    }

    /*  Sign-in URL above the QR 
     * QR encodes "https://signin.realityjump.co.uk/<code>", but for
     * human use we drop the protocol — phone keyboards/browsers fill
     * it in. URL pixSz shrinks to pixSz=2 if the line would overflow
     * 640 (max-length code case). */
    if (code && code[0]) {
        char url[MM_MAX_CODE + 32];
        snprintf(url, sizeof(url), "SIGNIN.SONICR.ONLINE/%s", code);
        /* Uppercase the code portion so it renders cleanly through the
         * uppercase-only pixel font. */
        for (int i = 0; url[i]; i++) {
            if (url[i] >= 'a' && url[i] <= 'z') url[i] = (char)(url[i] - 32);
        }

        int urlLen = 0;
        while (url[urlLen]) urlLen++;

        int urlPixSz = 3;
        int urlCharW = 5 * urlPixSz + urlPixSz;
        if (urlLen * urlCharW > 600) {
            urlPixSz = 2;
            urlCharW = 5 * urlPixSz + urlPixSz;
        }
        int urlX = (640 - urlLen * urlCharW) / 2;
        int urlY = 75;

        for (int k = 0; k < urlLen; k++) {
            char c = url[k];
            int idx;
            if (c >= 'A' && c <= 'Z') idx = c - 'A';
            else if (c >= '0' && c <= '9') idx = 26 + (c - '0');
            else if (c == '.') idx = 36;
            else if (c == '/') idx = 37;
            else { urlX += urlCharW; continue; }
            const uint8_t *glyph = s_pixFont[idx];
            for (int row = 0; row < 7; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < 5; col++) {
                    if (bits & (0x10 >> col)) {
                        R_DrawQuad2DSolid(
                            urlX + col * urlPixSz,
                            urlY + row * urlPixSz,
                            urlX + (col + 1) * urlPixSz,
                            urlY + (row + 1) * urlPixSz,
                            z, 0xFFFFFFFF);
                    }
                }
            }
            urlX += urlCharW;
        }

        /* "SIGN IN AT" header above the URL. */
        const char *hdr = "SIGN IN AT";
        int hdrPixSz = 2;
        int hdrCharW = 5 * hdrPixSz + hdrPixSz;
        int hdrLen = 0;
        while (hdr[hdrLen]) hdrLen++;
        int hdrX = (640 - hdrLen * hdrCharW) / 2;
        int hdrY = 35;
        for (int k = 0; k < hdrLen; k++) {
            char c = hdr[k];
            int idx;
            if (c >= 'A' && c <= 'Z') idx = c - 'A';
            else { hdrX += hdrCharW; continue; }
            const uint8_t *glyph = s_pixFont[idx];
            for (int row = 0; row < 7; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < 5; col++) {
                    if (bits & (0x10 >> col)) {
                        R_DrawQuad2DSolid(
                            hdrX + col * hdrPixSz,
                            hdrY + row * hdrPixSz,
                            hdrX + (col + 1) * hdrPixSz,
                            hdrY + (row + 1) * hdrPixSz,
                            z, 0xC0C0C0C0);
                    }
                }
            }
            hdrX += hdrCharW;
        }
    }

    {
        const char *prompt = NETTXT_LAN_PROMPT;
        int pixSz = 3;
        int charW = 5 * pixSz + pixSz;
        int pLen = 0;
        while (prompt[pLen]) pLen++;
        int px = (640 - pLen * charW) / 2;
        int py = 480 - 50;
        for (int k = 0; k < pLen; k++) {
            char c = prompt[k];
            int idx;
            if (c >= 'A' && c <= 'Z') idx = c - 'A';
            else if (c >= '0' && c <= '9') idx = 26 + (c - '0');
            else if (c == '.') idx = 36;
            else if (c == '/') idx = 37;
            else { px += charW; continue; }
            const uint8_t *glyph = s_pixFont[idx];
            for (int row = 0; row < 7; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < 5; col++) {
                    if (bits & (0x10 >> col)) {
                        R_DrawQuad2DSolid(
                            px + col * pixSz, py + row * pixSz,
                            px + (col + 1) * pixSz, py + (row + 1) * pixSz,
                            z, 0xC0C0C0C0);
                    }
                }
            }
            px += charW;
        }
    }

    (void)authState;
}

/**
 * NetworkScreen — 0x0048A8AC — 4564 bytes
 * Network lobby screen: provider selection, session create/join,
 * character negotiation, ready-up handshake.
 *
 * Adapted from binary with DirectPlay calls replaced by cross-platform
 * stubs. Modem states (0xA–0xC) stripped — not relevant for LAN/UDP.
 */
#if defined(SONICR_DC) || defined(SONICR_XBOX)
/* The network lobby was keyboard-only — F1 Host/Start, F2 Join, F3 LAN-only,
 * F6 character, F8 track, F7 mode. A stock DC has no keyboard, so map raw
 * controller buttons onto those F-key slots each frame, right after ReadInput
 * refreshes g_diKeyboardState and before the F-key state machine reads it.
 * Back is already on the pad via g_inputBits & 1 (= B or X), so it's left
 * alone. Join is therefore on R, NOT X — mapping Join to X made the back
 * button fire a join and a leave-to-menu together.
 *
 *   A / Start -> F1 (Host / Start race)      L trigger -> F7 (toggle mode)
 *   R trigger -> F2 (Join)                   D-pad L/R -> F6 (cycle character)
 *   Y         -> F3 (LAN-only fallback)      D-pad U/D -> F8 (cycle track)
 *
 * Rising-edge only: you enter this screen with the confirm button still held,
 * and state 0 (host/join) has no press latch of its own, so a level check would
 * host instantly on frame 1. NetSynthPadReset() captures the buttons held at
 * entry so they don't fire until released and pressed again.
 */
static unsigned int s_netPadPrev;

static void NetSynthPadReset(void)
{
    s_netPadPrev = platform_menu_buttons();   /* held-on-entry -> not an edge */
}

static void NetSynthPadKeys(void)
{
    unsigned int mb = platform_menu_buttons();
    unsigned int edge = mb & ~s_netPadPrev;   /* buttons newly pressed this frame */
    s_netPadPrev = mb;
    if (edge & (MENUBTN_A | MENUBTN_START))    g_diKeyboardState[0x3B] = 1; /* F1 */
    if (edge & MENUBTN_R)                       g_diKeyboardState[0x3C] = 1; /* F2 */
    if (edge & MENUBTN_Y)                       g_diKeyboardState[0x3D] = 1; /* F3 */
    if (edge & (MENUBTN_LEFT | MENUBTN_RIGHT))  g_diKeyboardState[0x40] = 1; /* F6 */
    if (edge & (MENUBTN_UP | MENUBTN_DOWN))     g_diKeyboardState[0x42] = 1; /* F8 */
    if (edge & MENUBTN_L)                        g_diKeyboardState[0x41] = 1; /* F7 */
}
#else
static void NetSynthPadReset(void) { }
static void NetSynthPadKeys(void) { }
#endif

static int s_lanOnlyHost;   /* set by F3/Y in state 0: host without matchmaker */

int NetworkScreen(void)
{
    /* Bring up the network stack (deferred from boot on DC). */
    if (platform_net_init() < 0) {
        DebugLog("NetworkScreen: platform_net_init failed\n");
        return SCREEN_BACK;
    }

    /* Load platform icon tpage (persists into race for nameplates). */
    LoadTPageRGB(TPAGE_PLATFORM_ICONS, "BIN/OPTION/NET01.RAW");
    R_FreezeTexture(TPAGE_PLATFORM_ICONS);

    NetSynthPadReset();   /* ignore pad buttons held on entry (see NetSynthPadKeys) */

    /*  Sign-in pre-phase 
     * If we don't have a valid auth token, run a sign-in loop first.
     * Once authenticated (or if already authenticated), fall through
     * to the existing lobby state machine. */
    {
        /* If MatchmakerInit() fails (e.g. compiled-out stub or curl init
         * error), needSignIn starts at 0 so we skip the QR loop and fall
         * straight through to LAN-only lobby flow. */
        int mmInitOk = MatchmakerInit();
        int needSignIn = (mmInitOk == 1);
        NetDbgClear();
        NetDbgPush("MM INIT %s", mmInitOk ? "OK" : "FAIL");
        if (needSignIn && MatchmakerLoadToken()) {
            NetDbgPush("TOKEN LOADED");
            if (MatchmakerRefreshToken()) {
                needSignIn = 0;
                NetDbgPush("TOKEN OK %.20s...", MatchmakerGetToken());
            }
            else {
                NetDbgPush("TOKEN REFRESH FAIL");
            }
        }

        if (needSignIn) {
            /* Load background + glyph textures for sign-in screen */
#ifdef SONICR_DC
            if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
            LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);
            TintBackgroundTPage(0x20, 0x48, 0xB0);
            LoadTPageRGB(g_uiTexPage + 1, "BIN/OPTION/NET00.RAW");
            ProcessTpageStates();
#ifdef SONICR_DC
            ResumeCD();
#endif

            /* Request sign-in code — if it fails, skip to LAN-only */
            if (!MatchmakerRequestCode()) {
                needSignIn = 0;
            }

            if (needSignIn) {
                /* Generate QR code */
                uint8_t qrBuf[qrcodegen_BUFFER_LEN_MAX];
                int qrSize = 0;
                MatchmakerGenerateQR(qrBuf, &qrSize);

                /* Sign-in render loop */
                g_fadeState = FADE_IN;
                g_screenResult = 0;
                int pollCounter = 0;
                int signInDone = 0;

                UpdateCDPlayback(5);

                while (!signInDone) {
                    platform_pump_events();
                    g_currentTime = timeGetTime();

                    if (g_fadeState != 0) {
                        UpdateFade();
                    }
                    if (g_fadeLevel == (int)0xFFFFFF00) {
                        s_pixTextHidden = 0;
                        return g_screenResult;
                    }

                    ReadInput();
                    NetSynthPadKeys();

                    if (g_fadeState == 0) {
                        /* Back button */
                        if (g_inputBits & 1) {
                            PlaySoundEffect(0, 0, 0);
                            g_screenResult = 0;
                            g_fadeState = FADE_OUT;
                            continue;
                        }

                        /* F3 = skip sign-in, LAN only */
                        if (g_diKeyboardState[0x3D]) {
                            PlaySoundEffect(2, 0, 0);
                            signInDone = 1;
                            continue;
                        }

                        /* Poll for token every ~300 frames (~10 sec at 30fps) */
                        pollCounter++;
                        if (pollCounter >= 300) {
                            pollCounter = 0;
                            int result = MatchmakerPollToken();
                            if (result == 1) {
                                PlaySoundEffect(2, 0, 0);
                                signInDone = 1;
                            }
                            else if (result == -1) {
                                /* Code expired — request a new one */
                                if (MatchmakerRequestCode()) {
                                    MatchmakerGenerateQR(qrBuf, &qrSize);
                                }
                            }
                        }
                    }


                    /* Render */
                    ProcessTpageStates();
                    BeginFrame();
                    s_pixTextHidden = (g_fadeState == 2);   /* fading out: drop the pixel-font text (it sits in front of the iris) */
                    RenderBackground();
                    EndFrame();

                    if (g_fadeLevel < 0) {
                        RenderFadeOverlay();
                    }

                    BeginFrame();
                    if (qrSize > 0) {
                        RenderSignInScreen(qrBuf, qrSize,
                                           MatchmakerGetCode(), 0);
                    }
                    NetDbgDraw();
                    RenderWavingMenuBackground();
                    EndFrame();
                    FlipD3D();

                    g_totalFrames++;
                    WaitForFrameCap();
                }
            }
        }
    }

    /* ROM table: per-character Y offset for nameplate rendering.
     * 10 entries indexed by charId. DGROUP at 0x501898. */
    static const int s_netCharModelY[10] = {
        80, 80, 80, 80, 25, 80, 80, 80, 75, 80              /* extracted from PE */
    };

    /* ROM table: per-track object index into g_objectStructArray.
     * 5 entries indexed by localTrackIdx. DGROUP at 0x50272C. */
    static const int s_netTrackModelIdx[5] = {
        2, 3, 0, 1, 4                                        /* extracted from PE */
    };

    /* ROM table: 2-int game mode model pair (from 0x5025C4).
     * movsd x2 copies 8 bytes from [0x5025c4] to [ebp-0x34]. */
    static const int s_modeModelPair[2] = { 22, 8 };        /* extracted from PE */

    /* Local variables (stack frame at ebp-0x34) */
    int modeModelLocal[2];                                   /* [ebp-0x34] */
    int inputThrottle;                                       /* [ebp-0x2C] */
    int localTrackIdx;                                       /* [ebp-0x28] */
    int firstFrame;                                          /* [ebp-0x24] */
    int inputLock;                                           /* [ebp-0x20] */
    int localModeIdx;                                       /* [ebp-0x1C] */
    DWORD lastTickSec;                                       /* [ebp-0x18] */
    MatchmakerSessionList g_mmSessionList;
    memset(&g_mmSessionList, 0, sizeof(g_mmSessionList));

    /* Copy ROM mode model pair to local */
    modeModelLocal[0] = s_modeModelPair[0];                  /* 0x48A8C4-C5: movsd x2 */
    modeModelLocal[1] = s_modeModelPair[1];

    /* Set background */
    /* push 0x5303cb; call 0x431140 — SetMenuBackground("TITLES07")
     * 0x431140 is a 13-byte no-op in binary (DebugLog). Background is
     * set by the tpage loading below. */

    /* Clear lobby globals */
    ns_setupMode = 0;                                        /* [0x68AFD0] = 0 */
    g_netSessionActive = 0;                                     /* [0x68AF18] = 0 */
    g_netPlayerCount = 0;                                    /* [0x68AEE8] = 0 */

    /* Init network */
    IsDirectPlayAvailable();                                 /* 0x487CD8 */
    if (g_dpLobbyObject == 0) {                              /* 0x48A8E7 */
        StartNetworkThread();                                /* 0x487674 */
    }

    /* Initialize config */
    firstFrame = 1;                                          /* [ebp-0x24] = 1 */
    g_netGameStartState = 1;                                 /* [0x501844] = 1 */
    g_netJoinedFlag = 0;                                     /* [0x68A6E4] = 0 */
    g_netLobbyCounter = 0;                                   /* [0x68A898] = 0 */
    g_netReceivedLobbyData = 0;                              /* [0x68A8FC] = 0 */
    g_netLobbyPlayerSlot = 0xFF;                             /* [0x689BB4] = 0xFF */
    g_netLobbyCharData[1] = -1;                              /* sentinel for BuildSoundIndexList */
    ns_lobbyEntryCount = 0;                                  /* [0x68AFDC] = 0 */
    g_netGameInfoDest[0] = (int)0xFFF02000;                  /* [0x68A89C] = 0xFFF02000 */
    g_resultsPlayerData[0] = (int)0xFFF01000;                /* [0x689BB8] = 0xFFF01000 */
    /* [0x68AFC8] = 5 — this is g_resultsPlayerData offset, not a separate global */
    g_bgTintR = 0x20;                                        /* brighter blue-saturated */
    g_bgTintG = 0x48;                                        /* brighter blue-saturated */
    g_bgTintB = 0xB0;                                        /* brighter blue-saturated */

    /* D3D vs Software setup */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
//    if (g_renderMode == RENDER_SOFT) {                       /* 0x48A96D: cmp [0x6DD860], 2 */
        /* D3D path — 0x5303DC: "general\sonicr.raw", 0x5303EF: "bin\option\net00.raw" */
        LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);         /* 0x42B044 */
        ColorizeTpageHiColor(g_bgTintR, g_bgTintG, g_bgTintB); /* 0x488310 */
        LoadTPageRGB(g_uiTexPage + 1, "BIN/OPTION/NET00.RAW"); /* 0x42B044 */
        /* Revive tpages suspended to state 6 during screen transition */
        for (int tp = 0; tp < 52; tp++) {
            if (g_tpageStateArray[tp] == 6 && g_tpagePixelBuf[tp] != NULL) {
                g_tpageStateArray[tp] = 4;
            }
        }
 //   } else { .. }
#ifdef SONICR_DC
    ResumeCD();
#endif

    /* Initialize state machine */
    ns_lobbyState = 0;                                       /* [0x68AFD4] = 0 */
    g_netMenuState = 0;                                      /* [0x689AFC] = 0 */
    inputLock = 1;                                           /* [ebp-0x20] = 1 */
    extern int g_netEnumActive;                              /* 0x689B54 */
    g_netEnumActive = 0;                                     /* [0x689B54] = 0 */
    g_menuPlayer.animId = 0;                                     /* [0x8FF918] = 0 */

    /* Character selection init */
    /* If saved charId is locked, reset to 0 */
    if (g_charUnlockTable[g_netSavedCharId] != 2) {        /* 0x48AA1C */
        g_netSavedCharId = 0;
    }

    g_menuPlayer.charId = (short)g_netSavedCharId;              /* [0x8FF972] */
    g_menuPlayer._unk_0x1E0 = g_menuPlayer.charId;                      /* [0x8FFA60] */
    g_menuPlayer._unk_0x1E0 = g_menuPlayer.charId; /* model index for lobby render */

    /* If saved char slot has zero status, reset to 0 */
    if (g_gpTrackStatus[1 + g_netSavedTrackIdx] == 0) {      /* 0x48AA3D */
        g_netSavedTrackIdx = 0;
    }

    localTrackIdx = g_netSavedTrackIdx;                      /* [ebp-0x28] */
    localModeIdx = g_netSavedModeIdx;                      /* [ebp-0x1C] */

    /* Copy char selection to lobby mirror */
    g_netFilteredProviders[0] = (int)(signed short)g_menuPlayer.charId; /* [0x68A6EC] */
    /* Binary copies 16 dwords from 0x8FD4A4 (player config area within
     * the player struct) to g_portraitTextBuffer.  0x8FD4A4 = player1Base + 0x4B0.
     * In the original, DirectPlay populates the name at +0x4B0 during session
     * creation. With DirectPlay stubbed, the field is zero-filled, producing
     * glyph 0 ('a') x16. Sentinel-terminate so DrawGlyphString renders nothing. */
    {
        int *src = (int *)((char *)&g_playerBase[0] + 0x4B0); /* DirectPlay name area */
        for (int i = 0; i < 16; i++) {
            g_portraitTextBuffer[i] = src[i];
        }
        if (g_portraitTextBuffer[0] == 0) {
            g_portraitTextBuffer[0] = -1;                     /* sentinel — no name */
        }
    }

    /* Animation init — same system as CharacterSelectScreen (0x48cd50).
     * ROM table at 0x4FBDF8 = g_charAnimTables.
     * 0x48AA10 seeds animId to 0, and both lookups (0x48AA95, 0x48B3B7) index
     * animTable by that field:
     *     movsx eax, word [0x8ff972]     ; menuPlayer.charId
     *     mov   edx, [eax*8 + 0x4fbdf8]  ; g_charAnimTables[charId*2]
     *     movsx eax, word [0x8ff918]     ; menuPlayer.animId
     *     mov   eax, [edx + eax*4]       ; animTable[animId]
     * g_animDataPtrs[0] is the 64-bit side store for menuPlayer._unk_0x9C. */
    {
        int charId = (int)(signed short)g_menuPlayer.charId;
        void **animTables = (void **)g_charAnimTables;
        g_menuPlayer.animId = 0;                            /* 0x48AA10 */
        if (animTables) {
            uintptr_t *animPtrs = (uintptr_t *)animTables[charId * 2];
            if (animPtrs) {
                const short *frameStream =
                    (const short *)(uintptr_t)animPtrs[g_menuPlayer.animId];
                if (frameStream) {
                    g_animDataPtrs[0] = frameStream;
                    g_menuPlayer.animFrameIdx = (int)*frameStream - 1;
                }
            }
        }
    }

    /* Timing and loop setup */
    inputThrottle = 1;                                        /* [ebp-0x2C] = 1 */
    ns_slotQueueIdx = 0;                                      /* [0x68AFD8] = 0 */
    g_totalFrames = 0;                                        /* [0x8FB68C] = 0 */
    g_fadeState = FADE_IN;                                    /* [0x901C48] = 1 */
    g_screenResult = 0;                                       /* [0x925418] = 0 — will be overwritten below */
    ns_connectionMode = 0;                                    /* [0x92529C] = 0 — no choice yet */
    g_netProviderChoice = -1;                                 /* [0x92528C] = -1 — no selection */

    /* Character lobby config bytes — 0x48AAF3-0x48AB37 */
    /* These are 16-bit writes to the lobby descriptor buffer */
    g_netGameInfoDest[1] = (localTrackIdx & 0xFFFF) | ((localModeIdx & 0xFFFF) << 16);
    g_netGameInfoDest[2] = (g_weatherType & 0xFFFF) | ((g_timeOfDay & 0xFFFF) << 16);
    net_lobby_set_mode_byte((unsigned char)(g_netSavedButtonByte & 0xFF)); /* [0x68A8CA] */
    g_optMenuMaxItem = 3;                                        /* [0x925298] = 3 */
    g_renderEnabled = 1;                                      /* [0x8FB81C] = 1 */

    /* Controller init + timing */
    PollAllInputDevices();                                    /* 0x479248 */
    lastTickSec = timeGetTime() / 1000;                       /* 0x48AB42-4D */

    /* Store initial time */
    g_screenBaseTime = (int)timeGetTime();                    /* [0x6E9D00] */

    /* Music: ensure track 5 */
    /* The binary checks current track and sets to 5 if needed.
     * GetCDTrack (0x4D0100) is not translated — just set directly. */
    UpdateCDPlayback(5);                                      /* 0x48AB99: track 5 */

    /* Fade setup */
    if (g_fadeState != 0) {                                   /* 0x48AB9E */
        UpdateFade();                                         /* 0x4305D4 */
    }

    /* ================================================================
     * MAIN LOOP — frame-rate limited to ~30fps
     * Binary: 0x48AB50 → 0x48BA7B, loops back via jmp 0x48AB50
     * ================================================================ */
    int elapsed;                                              /* esi — abs(now/1000 - lastTickSec) */

    for (;;) {
        platform_pump_events();

        /* Frame timing */
        DWORD nowMs = timeGetTime();
        g_currentTime = nowMs;
        elapsed = (int)(nowMs / 1000 - lastTickSec);
        if (elapsed < 0) {
            elapsed = -elapsed;                  /* 0x48AB83-88: cdq; xor; sub */
        }

        /* Music check */
        UpdateCDPlayback(5);                                  /* keep track 5 playing */

        /* Fade update */
        if (g_fadeState != 0) {                               /* 0x48AB9E */
            UpdateFade();                                     /* 0x4305D4 */
        }

        /* Check for exit (fade to black complete) */
        if (g_fadeLevel == (int)0xFFFFFF00) {                  /* 0x48ABAC: cmp [0x901C44], 0xFFFFFF00 */
            ns_lobbyState = 0;                                /* [0x68AFD4] = 0 */
            /* Save selections for next visit */
            g_netSavedCharId = (int)(signed short)g_menuPlayer.charId; /* 0x48ABDD */
            g_netSavedTrackIdx = localTrackIdx;               /* 0x48ABEF */
            g_netSavedModeIdx = localModeIdx;

            /* Copy lobby text buffer back to player config area */
            {
                int *dst = (int *)((char *)&g_playerBase[0] + 0x4B0);
                for (int i = 0; i < 16; i++)
                    dst[i] = g_portraitTextBuffer[i];
            }

            UpnpClosePort(NET_PORT_DEFAULT);
            MatchmakerClearSession();
            s_pixTextHidden = 0;
            return g_screenResult;                            /* 0x48AC1F: eax = [0x925418] */
        }

        /* Read input */
        ReadInput();                                          /* 0x477228 */
        NetSynthPadKeys();

        /* SDL: drain receive queue so join requests, keepalives, etc.
         * are processed during the lobby. Binary's recv thread handled
         * this; our thread enqueues but the main thread must dispatch. */
        ApplyNetworkPlayerState();

        /* If fade active (g_fadeState != 0), skip to render */
        if (g_fadeState != 0) {                               /* 0x48AC33 */
            goto render_frame;
        }

        /* "Back" key check (A button / LTrigger / ESC)-
         * g_inputBits & 1 covers the controller A button and the
         * Accel-mapped keyboard key (default 'A').  The on-screen
         * "Esc..." sprite implies ESC should also work, but the
         * original binary never wired ESC to the back action — added
         * here as an explicit g_diKeyboardState[DIK_ESCAPE] check so
         * the UI matches behaviour. */
        if ((g_inputBits & 1) != 0 || g_diKeyboardState[0x01] != 0) {
            if (firstFrame == 0) {
                CloseDirectPlaySession();                      /* 0x4875CC */
                PlaySoundEffect(0, 0, 0);
                g_fadeState = 2;                               /* fade out */
                g_screenResult = 0;                            /* SCREEN_BACK */
            }
            firstFrame = 0;
        }

        /* Timeout check (elapsed > 180 seconds) */
        if (elapsed > 0xB4) {                                  /* 0x48AC7A: cmp esi, 0xB4 */
            PlaySoundEffect(0, 0, 0);
            g_screenResult = 0x3039;                           /* SCREEN_TITLE */
            g_fadeState = 2;                                   /* fade out */
        }

        /* ================================================================
         * STATE MACHINE — driven by ns_lobbyState (0x68AFD4)
         * ================================================================ */

        /* STATE 0: Provider / Host-Join selection */
        if (g_diKeyboardState[0x3B] && (g_totalFrames & 0x1F) == 0) {
            DebugLog("F1 pressed: lobbyState=%d setupMode=%d connMode=%d gameStarted=%d playerCount=%d\n",
                     ns_lobbyState, ns_setupMode, ns_connectionMode, g_netGameStarted, g_netPlayerCount);
        }
        if (ns_lobbyState == 0) {                              /* 0x48ACA4 */
            g_netProviderChoice = -1;                          /* [0x92528C] = -1 */

            if (ns_connectionMode == 0) {                      /* 0x48ACC3: not yet selected */
                /* Provider selection — original: F1(0x3B)=IPX, F2(0x3C)=TCP,
                 * F3=Modem, F4=Serial. Simplified to Host/Join. */
                if (g_diKeyboardState[0x3B]) {                  /* F1 = Host — 0x675907 */
                    g_netProviderChoice = 0;
                    lastTickSec = timeGetTime() / 1000;
                    PlaySoundEffect(2, 0, 0);
                }
                if (g_diKeyboardState[0x3C]) {                  /* F2 = Join — 0x675908 */
                    g_netProviderChoice = 1;
                    lastTickSec = timeGetTime() / 1000;
                    PlaySoundEffect(2, 0, 0);
                }
                /* F3 (Y on a pad) = host on the local network only, without
                 * listing the session on the matchmaker. LAN players find
                 * any host by broadcast, so this just skips the online
                 * registration. */
                if (g_diKeyboardState[0x3D] && g_netProviderChoice == -1) {
                    g_netProviderChoice = 0;
                    s_lanOnlyHost = 1;
                    lastTickSec = timeGetTime() / 1000;
                    PlaySoundEffect(2, 0, 0);
                }
            }

            /* Process provider choice */
            ns_connectionMode = (g_netProviderChoice != -1) ? 1 : 0; /* 0x48ADB1 */

            if (g_netProviderChoice == 0) {                    /* Host selected */
                /* Single F1 press: create the session, register with the
                 * matchmaker, and skip straight to state 2 (the lobby).
                 * Original DirectPlay flow needed F1 again in state 1 to
                 * advance — that was a fossil of the protocol-picker UI. */
                if (CreateNetworkSession(NS_STR_SESSION_PW, NS_STR_SESSION_NAME)) {
                    ns_setupMode = 1;                          /* [0x68AFD0] = 1 */
                    ns_lobbyState = 2;                         /* skip state 1 */
                    if (s_lanOnlyHost) {
                        s_lanOnlyHost = 0;
                        NetDbgPush("LAN ONLY HOST");
                        DebugLog("Host: LAN only (not listed on the matchmaker)\n");
                    }
                    else if (MatchmakerHasToken()) {
                        UpnpOpenPort(NET_PORT_DEFAULT);
                        int csOk = MatchmakerCreateSession(MatchmakerGetUsername(), NET_PORT_DEFAULT);
                        NetDbgPush("CREATE %s ID %lld USER %s",
                                   csOk ? "OK" : "FAIL",
                                   (long long)MatchmakerGetSessionId(),
                                   MatchmakerGetUsername());
                    }
                    else {
                        NetDbgPush("NO TOKEN - LAN ONLY HOST");
                    }
                    EnumNetworkSessions(1);                    /* 0x487180 */
                    g_netEnumActive = 1;                       /* [0x689B54] */
                }
            }
            if (g_netProviderChoice == 1) {                    /* Join selected */
                ns_setupMode = 2;                              /* [0x68AFD0] = 2 — join mode */
                if (MatchmakerHasToken()) {
                    int lsOk = MatchmakerListSessions(&g_mmSessionList);
                    DebugLog("Matchmaker: hasToken=1 sessions=%d\n", g_mmSessionList.count);
                    NetDbgPush("LIST %s SESSIONS %d",
                               lsOk ? "OK" : "FAIL", g_mmSessionList.count);
                    ns_lobbyState = 3;                         /* picker */
                }
                else {
                    DebugLog("Matchmaker: hasToken=0, falling back to LAN\n");
                    NetDbgPush("NO TOKEN - LAN DISCOVERY");
                    ns_lobbyState = 1;
                }
            }
            /* Provider choices 2,3 (modem/serial) stripped */
        }

        /* STATE 1: Join discovery (host now skips this state)-
         * Reached only via F2 from state 0.  We poll for hosts and
         * automatically advance to state 2 once one is found, with no
         * second key press required.  The original DirectPlay flow
         * re-checked F1/F2 here to "confirm" the choice — that was the
         * source of the double-key-press bug and has been removed. */
        if (ns_lobbyState == 1) {                              /* 0x48AE90 */
            if (ns_setupMode != 3) {                           /* not modem: enum periodically */
                if ((g_totalFrames & 0x3F) == 0) {             /* 0x48AEA6 */
                    EnumDirectPlaySessions();                  /* 0x486C5C */
                }
            }

            /* Join path: try matchmaker sessions first, then LAN. */
            if (ns_setupMode == 2 && g_resultsUnlockFlag != 0) {
                int joined = 0;
                if (MatchmakerHasToken() && g_mmSessionList.count > 0) {
                    extern const char *g_cmdHostIP;
                    g_cmdHostIP = g_mmSessionList.sessions[0].ip_address;
                    joined = JoinNetworkSession(NS_STR_JOIN_SESSION, 0);
                    if (!joined) {
                        g_cmdHostIP = NULL;
                    }
                }
                if (!joined) {
                    joined = JoinNetworkSession(NS_STR_JOIN_SESSION, 0);
                }
                if (joined) {
                    ns_lobbyState = 2;
                }
            }

            /* If we reached state 2, start enumerating */
            if (ns_lobbyState == 2) {                          /* 0x48B029 */
                EnumNetworkSessions(1);                        /* 0x487180 */
                g_netEnumActive = 1;                           /* [0x689B54] = edx(1) */
            }
        }

        /* STATE 3: Matchmaker session picker-
         * Reached when F2 is pressed in state 0 and a matchmaker token is
         * present.  Shows the live session list, lets the user navigate
         * with Up/Down and join with Enter.  F3 falls back to LAN search.
         * ESC is handled by the back-key check at the top of the loop. */
        {
            if (ns_lobbyState != s_pickerPrevState) {
                if (ns_lobbyState == 3) {
                    s_pickerCursor = 0;
                    s_pickerRefreshFrame = 0;
                    s_pickerLastInput = 0xFF;  /* mask input on entry */
                }
                s_pickerPrevState = ns_lobbyState;
            }

            if (ns_lobbyState == 3) {
                /* Periodic refresh — every ~10 seconds at 30fps.  We
                 * keep a temp buffer so that a failed/empty response
                 * doesn't wipe the previously-known session list (the
                 * server has been observed returning empty arrays under
                 * load — see issue #16).  Only commit on a successful
                 * non-empty parse. */
                s_pickerRefreshFrame++;
                if (s_pickerRefreshFrame >= 300) {
                    MatchmakerSessionList tmp;
                    s_pickerRefreshFrame = 0;
                    int rfOk = MatchmakerListSessions(&tmp);
                    NetDbgPush("REFRESH %s SESSIONS %d",
                               rfOk ? "OK" : "FAIL", tmp.count);
                    if (rfOk && tmp.count > 0) {
                        g_mmSessionList = tmp;
                        if (s_pickerCursor >= g_mmSessionList.count) {
                            s_pickerCursor = g_mmSessionList.count - 1;
                        }
                    }
                }

                /* Edge-detect: react only to newly-pressed bits. */
                unsigned char newPress = (unsigned char)(g_inputBits & ~s_pickerLastInput);
                s_pickerLastInput = g_inputBits;

                if (g_mmSessionList.count > 0) {
                    if (newPress & 0x10) {                     /* Up */
                        if (s_pickerCursor > 0) {
                            s_pickerCursor--;
                            PlaySoundEffect(1, 0, 0);
                        }
                    }
                    if (newPress & 0x20) {                     /* Down */
                        if (s_pickerCursor < g_mmSessionList.count - 1) {
                            s_pickerCursor++;
                            PlaySoundEffect(1, 0, 0);
                        }
                    }
#ifdef SONICR_XBOX
                    if (newPress & 0x0A) {                     /* Start or A = Enter */
#else
                    if (newPress & 0x08) {                     /* Start = Enter */
#endif
                        extern const char *g_cmdHostIP;
                        MatchmakerSession *ms = &g_mmSessionList.sessions[s_pickerCursor];
                        g_cmdHostIP = ms->ip_address;
                        NetDbgPush("JOIN %s:%d", ms->ip_address, ms->port);
                        DebugLog("Picker: joining session[%d] ip=%s port=%d\n",
                                 s_pickerCursor, ms->ip_address, ms->port);
                        if (JoinNetworkSession(NS_STR_JOIN_SESSION, 0)) {
                            PlaySoundEffect(2, 0, 0);
                            NetDbgPush("CONNECTED TO %s:%d", ms->ip_address, NET_PORT_DEFAULT);
                            ns_lobbyState = 2;
                            EnumNetworkSessions(1);
                            g_netEnumActive = 1;
                        }
                        else {
                            g_cmdHostIP = NULL;
                            NetDbgPush("JOIN FAILED");
                            DebugLog("Picker: join failed, staying in picker\n");
                        }
                    }
                }

                /* F3: fall back to LAN-only discovery. */
                if (g_diKeyboardState[0x3D]) {
                    PlaySoundEffect(2, 0, 0);
                    ns_lobbyState = 1;
                }
            }
        }

        /* STATES 0xA-0xC (modem) — STRIPPED */
        /* Modem dialing logic removed. These states set up phone numbers
         * and dial via DirectPlay modem service provider. Not relevant
         * for cross-platform networking. */

        /* Client still waiting for the host's SLOT_ASSIGN: resend the join
         * request, fall back to a LAN host, or give up and go back. */
        if (ns_lobbyState == 2 && ns_setupMode == 2) {
            extern int NetClientJoinTick(void);
            if (NetClientJoinTick() < 0) {
                extern const char *g_cmdHostIP;
                CloseDirectPlaySession();
                g_cmdHostIP = NULL;
                NetDbgPush("HOST DID NOT ANSWER");
                PlaySoundEffect(0, 0, 0);
                ns_lobbyState = MatchmakerHasToken() ? 3 : 1;
            }
        }

        /* STATE 2: In lobby — character selection & sync */
        if (ns_lobbyState == 2) {                              /* 0x48B118 */
            /* Re-announce our character every ~2 s (UDP, a lost CHAR_CHANGE
             * would otherwise leave the others with the wrong character). */
            if ((g_totalFrames & 0x3F) == 0x20) {
                EnumNetworkSessions(1);
            }
            /* Periodically re-enumerate sessions */
            if (ns_setupMode == 3) {                           /* modem: specific timing */
                if ((int)(g_totalFrames * 0x3F) == 0x19) {     /* 0x48B12E */
                    g_netEnumActive = 0;
                }
            }
            else if (g_netEnumActive != 0) {                 /* 0x48B149 */
                g_netEnumActive = 0;
            }

            /* Check if host has enough players to start */
            if (g_netGameStarted == 1) {                             /* 0x48B15F: [0x68ACE4] == 1 */
                if (g_diKeyboardState[0x3B]) {                  /* F1 — 0x675907 */
                    if (ns_connectionMode == 0) {              /* 0x48B17A */
                        DebugLog("F1 in lobby: gameStarted=%d playerCount=%d\n", g_netGameStarted, g_netPlayerCount);
                        if (g_netPlayerCount >= 2) {           /* host + at least one client */
                            InitNetworkGame();                 /* 0x487A38 */
                        }
                        lastTickSec = timeGetTime() / 1000;
                    }
                    ns_connectionMode = 1;                     /* 0x48B19C */
                }
                else {
                    ns_connectionMode = 0;
                }
            }

            /* Check if multiplayer session is now live */
            if (g_netSessionActive != 0) {                        /* 0x48B1AF */
                PlaySoundEffect(2, 0, 0);
                g_screenResult = 1;                            /* success */
                g_fadeState = 2;                               /* fade out */
            }
        }

        /* Player joined notification */
        if (g_netJoinedFlag != 0) {                            /* 0x48B1DD: [0x68A6E4] */
            ShiftLobbyHistory();                               /* 0x48A630 */
            CopyPaletteSrcToDst();                             /* 0x48A6A0 */
            PlaySoundEffect(0x1A, 0, 0);                           /* join sound */
            g_netJoinedFlag = 0;
        }

        /* Text entry: only active in lobby states that need it */
        /* Binary: always ran because F1-F4 provider keys didn't overlap
         * with text entry keys. SDL: Up/Down overlap, so gate on state. */
        if (ns_lobbyState < 2) {
            goto skip_text_entry;
        }
#ifdef SONICR_XBOX
        /* No keyboard: the lobby's typed-message line would only collect the
         * pad's A presses as empty messages (rows of 'a' glyphs). */
        goto skip_text_entry;
#endif

        /* Confirm: submit selection */
        if (g_inputBits & 0x02) {                               /* A/confirm — was RETURN(0x1C) at 0x6758E8 */
            if (inputThrottle == 0) {                          /* 0x48B215 */
                /* Store current entry in session buffer */
                g_resultsTextBuffer[ns_slotQueueIdx] = -1;     /* 0x48B238 */

                /* Copy lobby config to snapshot slot 9 */
                memcpy((char *)g_resultsPlayerData + 9 * 0x104,
                       g_resultsPlayerData, 0x104);             /* 0x48B238: rep movsd */

                if (ns_lobbyState == 0xA) {
                    ns_lobbyState = 0xB;                       /* modem advance (stripped) */
                }
                else {
                    PrependToResultsBuffer();                  /* 0x48A750 */
                    if (ns_lobbyState > 1) {
                        UpdateNetworkSync(g_resultsPlayerData, 0x104); /* 0x48B281 */
                    }
                    FilterNetworkProviders();                  /* 0x48A7BC */
                    ShiftLobbyHistory();                       /* 0x48A630 */
                    CopyResultsToDst();                        /* 0x48A6BC */
                }

                ns_slotQueueIdx = 0;
                inputThrottle = 1;
                lastTickSec = timeGetTime() / 1000;
                PlaySoundEffect(0x1A, 0, 0);
            }
        }
        else if (g_inputBits & 0x01) {                       /* B/back — was BACKSPACE(0x0E) at 0x6758DA */
            if (inputThrottle == 0) {
                if (ns_slotQueueIdx > 0) {
                    ns_slotQueueIdx--;
                }
                inputThrottle = 1;
                lastTickSec = timeGetTime() / 1000;
            }
        }
        else {
            /* Poll for new input */
            int btn = ScanTypedLetter();                        /* 0x48A554 */
            if (btn != -1) {
                if (inputThrottle == 0) {
                    g_resultsTextBuffer[ns_slotQueueIdx] = btn;
                    if (ns_slotQueueIdx < 0x30) {
                        ns_slotQueueIdx++;
                    }
                    inputThrottle = 1;
                    lastTickSec = timeGetTime() / 1000;
                }
            }
            else {
                inputThrottle = 0;                             /* 0x48B356 */
            }
        }

skip_text_entry:
        /* Character selection input */
        if (g_diKeyboardState[0x40]) {                             /* F6 — 0x67590C */
            if (inputLock == 0) {                              /* 0x48B368 */
                /* Cycle to next unlocked character */
                int nextChar = (int)(signed short)g_menuPlayer.charId + 1;
                if (nextChar > 9) {
                    nextChar = 0;
                }
                while (g_charUnlockTable[nextChar] != 2) {   /* skip locked */
                    nextChar++;
                    if (nextChar > 9) {
                        nextChar = 0;
                    }
                }
                PlaySoundEffect(1, 0, 0);
                g_menuPlayer.charId = (short)nextChar;
                g_menuPlayer._unk_0x1E0 = (short)nextChar;
                g_menuPlayer._unk_0x1E0 = (short)nextChar; /* model index for RenderCharacterOnPodium */

                /* Re-init animation for new character — same as charsel */
                {
                    void **animTables = (void **)g_charAnimTables;
                    if (animTables) {
                        uintptr_t *animPtrs = (uintptr_t *)animTables[nextChar * 2];
                        if (animPtrs) {
                            const short *fs =
                                (const short *)(uintptr_t)animPtrs[g_menuPlayer.animId];
                            if (fs) {
                                g_animDataPtrs[0] = fs;
                                g_menuPlayer.animFrameIdx = (int)*fs - 1;
                            }
                        }
                    }
                }

                lastTickSec = timeGetTime() / 1000;
                inputLock = 1;
            }
        }
        else if (g_diKeyboardState[0x42]) {                     /* F8 — 0x67590E */
            if (g_netGameStarted == 1 && inputLock == 0) {           /* 0x48B3FC */
                /* Cycle track selection */
                int nextSlot = localTrackIdx + 1;
                if (nextSlot > 4) {
                    nextSlot = 0;
                }
                /* Skip locked tracks */
                while (g_gpTrackStatus[1 + nextSlot] == 0) {
                    nextSlot++;
                    if (nextSlot > 4) {
                        nextSlot = 0;
                    }
                }
                PlaySoundEffect(1, 0, 0);
                lastTickSec = timeGetTime() / 1000;
                localTrackIdx = nextSlot;
            }
            inputLock = 1;
        }
        else if (g_diKeyboardState[0x41]) {                     /* F7 — 0x67590D */
            if (g_netGameStarted == 1 && inputLock == 0) {           /* 0x48B462 */
                PlaySoundEffect(1, 0, 0);
                localModeIdx = (localModeIdx + 1) & 1;      /* toggle game mode */
                lastTickSec = timeGetTime() / 1000;
            }
            inputLock = 1;
        }
        else if (g_diKeyboardState[0x3F]) {                     /* F5 — 0x67590B */
            /* MODE A/B toggle disabled — main.c forces the dispatch by role,
             * so the setting has no effect. Key still swallowed here so it
             * does not fall through to the input-unlock branch. */
            inputLock = 1;
        }
        else {
            inputLock = 0;                                     /* 0x48B514 */
        }

        /* Character change broadcast */
        if ((int)(signed short)g_menuPlayer.charId != g_netFilteredProviders[0]) { /* 0x48B519 */
            /* Write char selection to player struct */
            g_playerBase[(int)(unsigned short)g_localPlayerIndex].charId =
                g_menuPlayer.charId;                               /* 0x48B53C: charId at +0xF2 */
            g_netFilteredProviders[0] = (int)(signed short)g_menuPlayer.charId;
            g_netBroadcastCharId = (int)(signed short)g_menuPlayer.charId;

            if (ns_lobbyState > 1) {
                EnumNetworkSessions(0);                        /* broadcast change */
            }
        }

        /* Incoming player slot handling */
        if (g_netLobbyPlayerSlot < 4) {                        /* 0x48B56B: cmp [0x689BB4], 4 */
            int slot = g_netLobbyPlayerSlot;
            DebugLog("Player %d joined\n", slot);     /* 0x48B583: push+call 0x4868D0 */

            /* Open session for this slot */
            OpenNetworkSession(*(int *)(g_netPlayerDecorations + slot * NET_DECO_STRIDE + NET_DECO_DPID)); /* 0x48B598 */

            /* Copy player character data */
            *(int *)(g_netPlayerDecorations + slot * NET_DECO_STRIDE + NET_DECO_LOBBYCHAR) = g_netLobbyCharData[0]; /* 0x48B5AB */
            for (int i = 1; i <= 16; i++) {                    /* 0x48B5B1 */
                /* Copy 16 ints from 0x68A858 to per-slot offset */
            }

            /* Copy to portrait data */
            {
                int *portraitDst = &g_netLobbyPortrait[slot * (0x48/4) + 2];
                for (int i = 0; i < 16; i++) {                  /* rep movsd 0x10 */
                    portraitDst[i] = g_netLobbyCharData[i + 1]; /* from 0x68A858 */
                }
                g_netLobbyPortrait[slot * (0x48/4) + 1] = g_netLobbyCharData[0];
            }

            /* Write char selection to player struct */
            g_playerBase[slot].charId =
                *(short *)(g_netPlayerDecorations + slot * NET_DECO_STRIDE + NET_DECO_LOBBYCHAR);

            g_netLobbyPlayerSlot = 0xFF;                       /* 0x48B61B: reset sentinel */

            /* Broadcast if connected */
            if (g_netGameStarted != 0) {                             /* 0x48B625 */
                BuildSoundIndexList();                         /* 0x48A5C8 */
                UpdateNetworkSync(g_netSoundIndexBuf, 0x104); /* 0x48B642 */
            }
        }

        /* Broadcast track/mode selection periodically */
        if (ns_lobbyState > 1 && g_netGameStarted != 0) {           /* 0x48B647 */
            g_netGameInfoDest[1] = (localTrackIdx & 0xFFFF) | ((localModeIdx & 0xFFFF) << 16);
            if ((g_totalFrames & 0x3F) == 0x20) {              /* 0x48B670: every 32 frames */
                UpdateNetworkSync(g_netGameInfoDest, 0x30);   /* 0x48B687 */
            }
            if ((g_totalFrames % 900) == 0 && MatchmakerGetSessionId() >= 0) {
                int kaStatus = MatchmakerKeepAlive(g_netPlayerCount, "LOBBY");
                NetDbgPush("KEEPALIVE HTTP %d SID %lld",
                           kaStatus, (long long)MatchmakerGetSessionId());
            }
        }

        /* Process received lobby data */
        if (g_netReceivedLobbyData != 0) {                     /* 0x48B68C */
            /* Copy received config to local */
            for (int i = 0; i < (0x30/4); i++) {
                g_netGameInfoDest[i] = g_netLobbyConfigBuf[i];
            }
 
            localTrackIdx = (int)(short)(g_netGameInfoDest[1] & 0xFFFF);         /* [1] low word */
            localModeIdx  = (int)(short)(g_netGameInfoDest[1] >> 16);          /* [1] high word */
            g_weatherType = (int)(short)(g_netGameInfoDest[2] & 0xFFFF);       /* [2] low word */
            g_timeOfDay   = (int)(short)(g_netGameInfoDest[2] >> 16);          /* [2] high word */

            g_netReceivedLobbyData = 0;
        }

        /* Multiplayer ready → exit */
        if (g_netSessionActive != 0) {                            /* 0x48B708 */
            PlaySoundEffect(2, 0, 0);
            g_fadeState = 2;                                   /* fade out */
            g_screenResult = 1;                                /* success */
        }

render_frame:
        /* ================================================================
         * RENDER FRAME
         * ================================================================ */

        /* Frame counter → timing value for renderers */
        g_menuPlayer.angleYaw = (g_totalFrames & 0xFF) << 4;       /* 0x48B737 */

        /* Radiant Emerald iridescent shader phase advance */
        g_emeraldSineOffX = (g_emeraldSineOffX + 0x4D) & 0xFFF;
        g_emeraldSineOffY = (g_emeraldSineOffY + 0xB6) & 0xFFF;
        g_emeraldSineOffZ = (g_emeraldSineOffZ + 0x73) & 0xFFF;


        /* Per-character blink/expression timer */
        UpdateFrameTimers();                                   /* 0x47FDE8 */

        /* Update vertex lighting for player model — 0x48B7A0 */
        UpdateVertexLighting((Player *)g_playerBase);         /* 0x430638 */

        /* Animation frame advance — same as ResultsScreen (0x4c7d12).
         * Walks g_animDataPtrs[0] by one short per frame, handles
         * the -1 loop marker.  Identical to screen_charsel.c:466. */
        {
            const short *fs = g_animDataPtrs[0];
            if (fs) {
                int frame = (int)*fs;
                fs++;
                if (frame == -1) {                               /* loop marker */
                    int k = (int)*fs;                            /* rewind count */
                    fs -= k;
                    frame = (int)*fs;
                    fs++;
                }
                g_animDataPtrs[0] = fs;
                g_menuPlayer.animFrameIdx = (frame & 0xFFF) - 1;
                g_menuPlayer._unk_0x1C = frame & 0xFFF;
            }
        }

        /* Animate vehicle glow per player — 0x48B800 */
        AnimateVehicleGlow(&g_menuPlayer);                    /* 0x4217F8 — 0x48B7FB passes 0x8FF880 */

        /* Render frame-
         * D3D path (0x48B812): RenderCharacterOnPodium, Draw3DModelD3D x2,
         * RenderResultsScreen (lobby info), then flip.
         * Binary calls 0x442464 (10-param model renderer); GL port uses
         * Draw3DModelD3D (0x454C70, 8 params) — same substitution as
         * ResultsScreen. */
        {
            g_polyCount = 0;
            ProcessTpageStates();                                   /* 0x4323CC */
            BeginFrame();                                      /* 0x42299C */
            s_pixTextHidden = (g_fadeState == 2);   /* fading out: drop the pixel-font text (it sits in front of the iris) */
            RenderBackground();                                /* 0x435868 */

            /* Lobby contents (character podium, track/mode models, lobby
             * HUD) only render in the actual lobby (state 2).  States 0,
             * 1, and 3 are the mode-select / search / picker overlays
             * which should be uncluttered. */
            if (ns_lobbyState == 2) {
                /* Character nameplate — 0x48B82E-0x48B856
                 * EAX=-190, EDX=ROM[charId]+0x32, EBX=0x200, ECX=0xE80
                 * Stack: g_menuPlayer.angleYaw, 0, player */
                {
                    int charId = (int)(signed short)g_menuPlayer.charId;
                    int nameY = s_netCharModelY[charId] + 0x32;
                    /* 0x48B82F pushes 0x8FF880 — the screen's own player, not
                     * the racing one. */
                    RenderCharacterOnPodium(-190, nameY, 0x200,
                                            0xE80, g_menuPlayer.angleYaw, 0,
                                            &g_menuPlayer);
                }

                /* Track model — 0x48B86B-0x48B89B
                 * Object from s_netTrackModelIdx[localTrackIdx].
                 * If localTrackIdx==4 (Emerald), temporarily set g_trackId=5
                 * for tpage lookup (0x48B85B-0x48B861), then reset (0x48B8A0). */
                {
                    int savedTrackId = g_trackId;
                    if (localTrackIdx == 4) {
                        g_trackId = 5;                             /* 0x48B861 */
                    }

                    intptr_t objPtr1 = (intptr_t)((char *)g_objectStructArray
                        + s_netTrackModelIdx[localTrackIdx] * 0x44);
                    Draw3DModelD3D(0x190, 0x12C, 0x400, 0xE80,
                                   g_menuPlayer.angleYaw, 0, objPtr1, 2);

                    if (localTrackIdx == 4) {
                        g_trackId = savedTrackId;                  /* 0x48B8A8 */
                    }
                }

                /* Game mode model — 0x48B8AE-0x48B8D8
                 * Object from modeModelLocal[localModeIdx]. */
                {
                    intptr_t objPtr2 = (intptr_t)((char *)g_objectStructArray
                        + modeModelLocal[localModeIdx] * 0x44);
                    Draw3DModelD3D(0, 0x113, 0x400, 0xE80,
                                   g_menuPlayer.angleYaw, 0, objPtr2, 2);
                }

                /* Lobby info HUD — 0x48B8DD-0x48B8E5
                 * EAX=ns_lobbyState, EDX=localModeIdx */
                RenderResultsScreen(ns_lobbyState, localModeIdx);

                {
                    int iconH = 24;
                    int textH = 0x14;
                    int iconYOff = (textH - iconH) / 2;
                    int standY = 0x1c2 - 0x1a;
                    for (int pi = 0; pi < g_resultsPlayerCount && pi < NET_MAX_PLAYERS; pi++) {
                        int iconIdx = net_platform_icon(
                            net_get_slot_platform(pi), net_get_slot_region(pi));
                        int icoUvX, icoUvY;
                        net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
                        DrawTexturedQuad(
                            0xc - iconH - 2, standY + iconYOff,
                            0x40000000,
                            iconH, iconH,
                            TPAGE_PLATFORM_ICONS,
                            icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
                            0xFFFFFFFFu);
                        standY -= 0x16;
                    }
                }
            }

            /*  Mode-select prompt (state 0), join-search (state 1),
             * matchmaker session picker (state 3), and in-lobby username
             * (state 2) overlays.
             *
             * Replaces the original protocol-picker UI (F1=IPX, F2=TCP,
             * ...) which was a fossil after the matchmaker refactor. */
            if (ns_lobbyState == 0 || ns_lobbyState == 1) {
                /* Identity line: "LOGGED IN AS <username>" with the
                 * prefix in pixel font and the username in the original
                 * game font.  Measured then placed for a centered combo. */
                if (MatchmakerHasToken() && MatchmakerGetUsername()[0]) {
                    const char *prefix = "LOGGED IN AS ";
                    int prefixW = PixTextWidth(prefix, 2);
                    int glyphs[MM_MAX_USERNAME];
                    StringToGlyphIds(MatchmakerGetUsername(), glyphs, MM_MAX_USERNAME);
                    int glyphW = MeasureGlyphString(glyphs, MM_MAX_USERNAME);
                    int totalW = prefixW + glyphW;
                    int x = (640 - totalW) / 2;
                    DrawPixText(prefix, x, 30 + 4, 2, 0xC0C0C0C0);
                    int endX = DrawGlyphString(x + prefixW, 30 + 2, glyphs, MM_MAX_USERNAME);
                    {
                        int textH = 0x14;
                        int iconH = 24;
                        int iconYOff = (textH - iconH) / 2;
                        int iconIdx = net_platform_icon(MM_PLATFORM,
                                                        (uint8_t)platform_get_region());
                        int icoUvX, icoUvY;
                        net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
                        DrawTexturedQuad(
                            endX + 4, 30 + 2 + iconYOff,
                            0x40000000,
                            iconH, iconH,
                            TPAGE_PLATFORM_ICONS,
                            icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
                            0xFFFFFFFFu);
                    }
                }
                else {
                    const char *idLine = "LAN MODE";
                    int idW = PixTextWidth(idLine, 2);
                    DrawPixText(idLine, (640 - idW) / 2, 30, 2, 0xC0C0C0C0);
                }

                if (ns_lobbyState == 0) {
                    const char *l1 = NETTXT_HOST;
                    const char *l2 = NETTXT_JOIN;
                    int w1 = PixTextWidth(l1, 4);
                    int w2 = PixTextWidth(l2, 4);
                    DrawPixText(l1, (640 - w1) / 2, 200, 4, 0xFFFFFFFF);
                    DrawPixText(l2, (640 - w2) / 2, 250, 4, 0xFFFFFFFF);
                    if (MatchmakerHasToken()) {
                        const char *l3 = NETTXT_LANHOST;
                        int w3 = PixTextWidth(l3, 3);
                        DrawPixText(l3, (640 - w3) / 2, 310, 3, 0xC0C0C0C0);
                    }
                }
                else /* ns_lobbyState == 1 */ {
                    const char *msg = "SEARCHING FOR SESSIONS...";
                    int w = PixTextWidth(msg, 3);
                    DrawPixText(msg, (640 - w) / 2, 220, 3, 0xFFFFFFFF);
                }
            }
            else if (ns_lobbyState == 3) {
                /*  Matchmaker session picker ─
                 * Header in pixel font; host names in the original game
                 * font (DrawGlyphString) for visual consistency with the
                 * lobby; player counts in pixel font next to each name. */
                if (MatchmakerGetUsername()[0]) {
                    const char *prefix = "LOGGED IN AS ";
                    int prefixW = PixTextWidth(prefix, 2);
                    int glyphs[MM_MAX_USERNAME];
                    StringToGlyphIds(MatchmakerGetUsername(), glyphs, MM_MAX_USERNAME);
                    int glyphW = MeasureGlyphString(glyphs, MM_MAX_USERNAME);
                    int x = (640 - (prefixW + glyphW)) / 2;
                    DrawPixText(prefix, x, 30 + 4, 2, 0xC0C0C0C0);
                    int endX = DrawGlyphString(x + prefixW, 30 + 2, glyphs, MM_MAX_USERNAME);
                    {
                        int textH = 0x14;
                        int iconH = 24;
                        int iconYOff = (textH - iconH) / 2;
                        int iconIdx = net_platform_icon(MM_PLATFORM,
                                                        (uint8_t)platform_get_region());
                        int icoUvX, icoUvY;
                        net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
                        DrawTexturedQuad(
                            endX + 4, 30 + 2 + iconYOff,
                            0x40000000,
                            iconH, iconH,
                            TPAGE_PLATFORM_ICONS,
                            icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
                            0xFFFFFFFFu);
                    }
                }
                else {
                    const char *idLine = "MATCHMAKER";
                    int idW = PixTextWidth(idLine, 2);
                    DrawPixText(idLine, (640 - idW) / 2, 30, 2, 0xC0C0C0C0);
                }

                const char *hdr = "SELECT A SESSION";
                int hdrW = PixTextWidth(hdr, 4);
                DrawPixText(hdr, (640 - hdrW) / 2, 60, 4, 0xFFFFFFFF);

                int picker_count = g_mmSessionList.count;
                int rowY = 130;
                int rowH = 28;
#ifdef SONICR_XBOX
                /* Keep the row inside the TV-safe area (the "1/4" column
                 * at x=580 was cut by overscan). */
                int rowX = 70;
                int countryX = 380;
                int pingX = 440;
                int countX = 536;
#else
                int rowX = 80;
                int countryX = 420;
                int pingX = 470;
                int countX = 580;
#endif

                int cursor = s_pickerCursor;

                if (picker_count == 0) {
                    const char *waitMsg = "NO SESSIONS YET - SEARCHING";
                    int w = PixTextWidth(waitMsg, 3);
                    DrawPixText(waitMsg, (640 - w) / 2, 240, 3, 0xC0C0C0C0);
                }
                else {
                    int maxRows = picker_count;
                    if (maxRows > 9) {
                        maxRows = 9;
                    }
                    for (int i = 0; i < maxRows; i++) {
                        int y = rowY + i * rowH;
                        if (i == cursor) {
                            /* z=3.0 puts the highlight bar behind the
                             * glyph text (DrawGlyphString uses z=2.0)
                             * so the username renders on top of it. */
#ifdef SONICR_XBOX
                            /* Dark bar: the white pixel-font text (country,
                             * ping, player count) vanished on the light one. */
                            R_DrawQuad2DSolid(rowX - 10, y - 4,
                                              countX + 64, y + 24,
                                              3.0f, 0xC0102870);
#else
                            R_DrawQuad2DSolid(rowX - 10, y - 4,
                                              countX + 60, y + 24,
                                              3.0f, 0x80FFFFFF);
#endif
                        }
                        int glyphs[MM_MAX_USERNAME];
                        StringToGlyphIds(g_mmSessionList.sessions[i].host_name,
                                         glyphs, MM_MAX_USERNAME);
                        int nameEndX = DrawGlyphString(rowX, y, glyphs, MM_MAX_USERNAME);
                        {
                            int textH = 0x14;
                            int iconH = 24;
                            int iconYOff = (textH - iconH) / 2;
                            uint8_t sessionRegion = 0;
                            const char *cc = g_mmSessionList.sessions[i].country_code;
                            if (cc[0] == 'J' && cc[1] == 'P') {
                                sessionRegion = NET_REGION_JAPAN;
                            }
                            else if ((cc[0] == 'G' && cc[1] == 'B') ||
                                     (cc[0] == 'D' && cc[1] == 'E') ||
                                     (cc[0] == 'F' && cc[1] == 'R') ||
                                     (cc[0] == 'E' && cc[1] == 'S') ||
                                     (cc[0] == 'I' && cc[1] == 'T') ||
                                     (cc[0] == 'N' && cc[1] == 'L') ||
                                     (cc[0] == 'S' && cc[1] == 'E') ||
                                     (cc[0] == 'P' && cc[1] == 'T') ||
                                     (cc[0] == 'A' && cc[1] == 'T') ||
                                     (cc[0] == 'B' && cc[1] == 'E') ||
                                     (cc[0] == 'C' && cc[1] == 'H') ||
                                     (cc[0] == 'N' && cc[1] == 'O') ||
                                     (cc[0] == 'D' && cc[1] == 'K') ||
                                     (cc[0] == 'F' && cc[1] == 'I') ||
                                     (cc[0] == 'I' && cc[1] == 'E') ||
                                     (cc[0] == 'P' && cc[1] == 'L'))
                            {
                                sessionRegion = NET_REGION_EUROPE;
                            }
                            else {
                                sessionRegion = NET_REGION_US;
                            }
                            int iconIdx = net_platform_icon(
                                g_mmSessionList.sessions[i].platform, sessionRegion);
                            int icoUvX, icoUvY;
                            net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
                            DrawTexturedQuad(
                                nameEndX + 4, y + iconYOff,
                                0x40000000,
                                iconH, iconH,
                                TPAGE_PLATFORM_ICONS,
                                icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
                                0xFFFFFFFFu);
                        }

                        const char *cc = g_mmSessionList.sessions[i].country_code;
                        if (cc[0]) {
                            DrawPixText(cc, countryX, y + 1, 3, 0xFFFFFFFF);
                        }

                        char pingStr[8];
                        int pms = g_mmSessionList.sessions[i].ping_ms;
                        if (pms < 0) {
                            snprintf(pingStr, sizeof(pingStr), "?");
                        }
                        else if (pms > 999) {
                            snprintf(pingStr, sizeof(pingStr), "999MS");
                        }
                        else {
                            snprintf(pingStr, sizeof(pingStr), "%dMS", pms);
                        }
                        DrawPixText(pingStr, pingX, y + 1, 3, 0xFFFFFFFF);

                        char countStr[8];
                        snprintf(countStr, sizeof(countStr), "%d/4",
                                 g_mmSessionList.sessions[i].player_count);
                        DrawPixText(countStr, countX, y + 1, 3, 0xFFFFFFFF);
                    }
                }

                const char *hint = NETTXT_PICKER_HINT;
                int hintW = PixTextWidth(hint, 2);
                DrawPixText(hint, (640 - hintW) / 2, 440, 2, 0xC0C0C0C0);
            }
            else if (MatchmakerHasToken() && MatchmakerGetUsername()[0]) {
                /* In-lobby (state 2) username with platform icon. */
                {
                    int textH = 0x14;
                    int iconH = 24;
                    int iconYOff = (textH - iconH) / 2;
                    int iconIdx = net_platform_icon(MM_PLATFORM,
                                                    (uint8_t)platform_get_region());
                    int icoUvX, icoUvY;
                    net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
                    DrawTexturedQuad(
                        20, 60 + iconYOff,
                        0x40000000,
                        iconH, iconH,
                        TPAGE_PLATFORM_ICONS,
                        icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
                        0xFFFFFFFFu);
                }
                int glyphs[MM_MAX_USERNAME];
                StringToGlyphIds(MatchmakerGetUsername(), glyphs, MM_MAX_USERNAME);
                DrawGlyphString(20 + 26, 60, glyphs, MM_MAX_USERNAME);
            }

            NetDbgDraw();

            if (g_fadeLevel < 0) {
                RenderFadeOverlay();
            }

            RenderWavingMenuBackground();
            EndFrame();
            FlipD3D();                                         /* 0x4356BC */
        }

        /* Frame counter increment */
        g_totalFrames2++;                                      /* [0x8FB690]++ */
        g_totalFrames++;                                       /* [0x8FB68C]++ */

        /* Frame rate limiting (~30fps) */
        WaitForFrameCap();
        g_screenFPS = 30;
        g_screenBaseTime = (int)timeGetTime();
    }
    /* unreachable */
}

/**
 * NetworkScreenReentry — 0x0048A8AC — ~4200 bytes
 *
 * Re-entry into the network lobby after a race. Binary calls this instead
 * of NetworkScreen (0x4885BC) when returning from a completed network race.
 * Duplicated from NetworkScreen as a starting point — needs modification
 * to match binary 0x48A8AC init sequence (tpage reload, state cleanup).
 *
 * Key differences from NetworkScreen:
 *   - Calls CloseDirectPlaySession at lobby exit (0x48AC51)
 *   - Has full network session lifecycle inline (create/join/enum/init)
 *   - 6 helper functions at 0x48A554-0x48A7BC
 *   - Reloads lobby tpages from scratch
 */
static int s_reentryAutoJoin;   /* client rejoining its last host after a race */

int NetworkScreenReentry(void)
{
    /* ROM table: per-character Y offset for nameplate rendering.
     * 10 entries indexed by charId. DGROUP at 0x501898. */
    static const int s_netCharModelY[10] = {
        80, 80, 80, 80, 25, 80, 80, 80, 75, 80              /* extracted from PE */
    };

    /* ROM table: per-track object index into g_objectStructArray.
     * 5 entries indexed by localTrackIdx. DGROUP at 0x50272C. */
    static const int s_netTrackModelIdx[5] = {
        2, 3, 0, 1, 4                                        /* extracted from PE */
    };

    /* ROM table: 2-int game mode model pair (from 0x5025C4).
     * movsd x2 copies 8 bytes from [0x5025c4] to [ebp-0x34]. */
    static const int s_modeModelPair[2] = { 22, 8 };        /* extracted from PE */

    /* Local variables (stack frame at ebp-0x34) */
    int modeModelLocal[2];                                   /* [ebp-0x34] */
    int inputThrottle;                                       /* [ebp-0x2C] */
    int localTrackIdx;                                       /* [ebp-0x28] */
    int firstFrame;                                          /* [ebp-0x24] */
    int inputLock;                                           /* [ebp-0x20] */
    int localModeIdx;                                       /* [ebp-0x1C] */
    DWORD lastTickSec;                                       /* [ebp-0x18] */

    /* Copy ROM mode model pair to local */
    modeModelLocal[0] = s_modeModelPair[0];                  /* 0x48A8C4-C5: movsd x2 */
    modeModelLocal[1] = s_modeModelPair[1];

    /* Debug log */
    DebugLog("NetworkScreenReentry\n");                      /* 0x48A8BF: push 0x5303CB */

    /* Clear lobby globals */
    ns_setupMode = 0;                                        /* [0x68AFD0] = 0 */
    g_netSessionActive = 0;                                     /* [0x68AF18] = 0 */
    g_netPlayerCount = 0;                                    /* [0x68AEE8] = 0 */

    /* Init network */
    IsDirectPlayAvailable();                                 /* 0x487CD8 */
    if (g_dpLobbyObject == 0) {                              /* 0x48A8E7 */
        StartNetworkThread();                                /* 0x487674 */
    }

    /* Initialize config */
    firstFrame = 1;                                          /* [ebp-0x24] = 1 */
    g_netGameStartState = 1;                                 /* [0x501844] = 1 */
    g_netJoinedFlag = 0;                                     /* [0x68A6E4] = 0 */
    g_netLobbyCounter = 0;                                   /* [0x68A898] = 0 */
    g_netReceivedLobbyData = 0;                              /* [0x68A8FC] = 0 */
    g_netLobbyPlayerSlot = 0xFF;                             /* [0x689BB4] = 0xFF */
    g_netLobbyCharData[1] = -1;                              /* sentinel for BuildSoundIndexList */
    ns_lobbyEntryCount = 0;                                  /* [0x68AFDC] = 0 */
    g_netGameInfoDest[0] = (int)0xFFF02000;                  /* [0x68A89C] = 0xFFF02000 */
    g_resultsPlayerData[0] = (int)0xFFF01000;                /* [0x689BB8] = 0xFFF01000 */
    g_bgTintR = 0x20;                                        /* brighter blue-saturated */
    g_bgTintG = 0x48;                                        /* brighter blue-saturated */
    g_bgTintB = 0xB0;                                        /* brighter blue-saturated */

    /* D3D vs Software setup */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    if (g_renderMode == RENDER_SOFT) {                       /* 0x48A96D: cmp [0x6DD860], 2 */
        LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);         /* 0x42B044 */
        ColorizeTpageHiColor(g_bgTintR, g_bgTintG, g_bgTintB); /* 0x488310 */
        LoadTPageRGB(g_uiTexPage + 1, "BIN/OPTION/NET00.RAW"); /* 0x42B044 */
        /* Revive tpages suspended to state 6 during screen transition */
        for (int tp = 0; tp < 52; tp++) {
            if (g_tpageStateArray[tp] == 6 && g_tpagePixelBuf[tp] != NULL) {
                g_tpageStateArray[tp] = 4;
            }
        }
    }
    else {
        SetupMenuTexturesD3D();                              /* 0x438CD8 */
        LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);         /* 0x42AD10 */
        ColorizeTpageHiColor(g_bgTintR, g_bgTintG, g_bgTintB); /* 0x488310 */
        LoadTPageRGB(g_uiTexPage + 1, "BIN/OPTION/NET00.RAW");
        ProcessTpageStates();                                     /* 0x4323CC */
        FinalizeMenuTexturesD3D();                           /* 0x438D10 */
    }
#ifdef SONICR_DC
    ResumeCD();
#endif

    /* Initialize state machine */
    ns_lobbyState = 0;                                       /* [0x68AFD4] = 0 */
    g_netMenuState = 0;                                      /* [0x689AFC] = 0 */
    inputLock = 1;                                           /* [ebp-0x20] = 1 */
    extern int g_netEnumActive;                              /* 0x689B54 */
    g_netEnumActive = 0;                                     /* [0x689B54] = 0 */
    g_menuPlayer.animId = 0;                                     /* [0x8FF918] = 0 */

    /* Character selection init */
    if (g_charUnlockTable[g_netSavedCharId] != 2) {        /* 0x48AA1C */
        g_netSavedCharId = 0;
    }

    g_menuPlayer.charId = (short)g_netSavedCharId;              /* [0x8FF972] */
    g_menuPlayer._unk_0x1E0 = g_menuPlayer.charId;                      /* [0x8FFA60] */
    g_menuPlayer._unk_0x1E0 = g_menuPlayer.charId;

    if (g_gpTrackStatus[1 + g_netSavedTrackIdx] == 0) {      /* 0x48AA3D */
        g_netSavedTrackIdx = 0;
    }

    localTrackIdx = g_netSavedTrackIdx;                      /* [ebp-0x28] */
    localModeIdx = g_netSavedModeIdx;                      /* [ebp-0x1C] */

    g_netFilteredProviders[0] = (int)(signed short)g_menuPlayer.charId;
    {
        int *src = (int *)((char *)&g_playerBase[0] + 0x4B0); /* DirectPlay name area */
        for (int i = 0; i < 16; i++) {
            g_portraitTextBuffer[i] = src[i];
        }
        if (g_portraitTextBuffer[0] == 0) {
            g_portraitTextBuffer[0] = -1;
        }
    }

    {
        int charId = (int)(signed short)g_menuPlayer.charId;
        void **animTables = (void **)g_charAnimTables;
        g_menuPlayer.animId = 0;                            /* 0x48AA10 */
        if (animTables) {
            uintptr_t *animPtrs = (uintptr_t *)animTables[charId * 2];
            if (animPtrs) {
                const short *frameStream =
                    (const short *)(uintptr_t)animPtrs[g_menuPlayer.animId];
                if (frameStream) {
                    g_animDataPtrs[0] = frameStream;
                    g_menuPlayer.animFrameIdx = (int)*frameStream - 1;
                }
            }
        }
    }

    /* Timing and loop setup */
    NetSynthPadReset();   /* ignore pad buttons held on entry (see NetSynthPadKeys) */
    inputThrottle = 1;
    ns_slotQueueIdx = 0;                                      /* [0x68AFD8] = 0 */
    g_totalFrames = 0;                                        /* [0x8FB68C] = 0 */
    g_fadeState = FADE_IN;                                    /* [0x901C48] = 1 */
    g_screenResult = 0;
    ns_connectionMode = 0;
    g_netProviderChoice = -1;

    g_netGameInfoDest[1] = (localTrackIdx & 0xFFFF) | ((localModeIdx & 0xFFFF) << 16);
    g_netGameInfoDest[2] = (g_weatherType & 0xFFFF) | ((g_timeOfDay & 0xFFFF) << 16);
    net_lobby_set_mode_byte((unsigned char)(g_netSavedButtonByte & 0xFF));
    g_optMenuMaxItem = 3;
    g_renderEnabled = 1;

    PollAllInputDevices();                                    /* 0x479248 */
    lastTickSec = timeGetTime() / 1000;
    g_screenBaseTime = (int)timeGetTime();
    UpdateCDPlayback(5);

    if (g_fadeState != 0) {
        UpdateFade();
    }

    /* ================================================================
     * MAIN LOOP — same as NetworkScreen
     * ================================================================ */
    int elapsed;

    for (;;) {
        platform_pump_events();

        DWORD nowMs = timeGetTime();
        g_currentTime = nowMs;
        elapsed = (int)(nowMs / 1000 - lastTickSec);
        if (elapsed < 0) {
            elapsed = -elapsed;
        }

        UpdateCDPlayback(5);

        if (g_fadeState != 0) {
            UpdateFade();
        }

        if (g_fadeLevel == (int)0xFFFFFF00) {                  /* 0x48ABAC */
            ns_lobbyState = 0;                                 /* 0x48ABC0 */
            /* Save selections for next visit */
            g_netSavedCharId = (int)(signed short)g_menuPlayer.charId; /* 0x48ABDD */
            g_netSavedTrackIdx = localTrackIdx;                /* 0x48ABEF */
            g_netSavedModeIdx = localModeIdx;

            /* Set race globals from lobby selections — 0x48ABF4-0x48AC18.
             * Re-entry function maps track/mode here so main.c doesn't
             * need the multiplayer init block to do it. */
            g_trackId = g_trackIdTable[localTrackIdx];         /* 0x48AC00 */
            g_raceSubMode = localModeIdx * 3;                  /* 0x48AC18: lea eax,[edx*4]; sub eax,edx */

            /* Copy player config back — 0x48AC1D: rep movsd 0x10 */
            {
                int *dst = (int *)((char *)&g_playerBase[0] + 0x4B0);
                for (int i = 0; i < 16; i++) {
                    dst[i] = g_portraitTextBuffer[i];
                }
            }

            s_pixTextHidden = 0;
            return g_screenResult;                             /* 0x48AC1F: eax = [0x925418] */
        }

        ReadInput();
        NetSynthPadKeys();
        ApplyNetworkPlayerState();

        if (g_fadeState != 0) {
            goto render_frame_re;
        }

        if ((g_inputBits & 1) != 0 || g_diKeyboardState[0x01] != 0) {
            if (firstFrame == 0) {
                CloseDirectPlaySession();
                PlaySoundEffect(0, 0, 0);
                g_fadeState = 2;
                g_screenResult = 0;
            }
            firstFrame = 0;
        }

        if (elapsed > 0xB4) {
            PlaySoundEffect(0, 0, 0);
            g_screenResult = 0x3039;
            g_fadeState = 2;
        }

        if (ns_lobbyState == 0) {
            g_netProviderChoice = -1;
            if (ns_connectionMode == 0) {
                if (g_diKeyboardState[0x3B]) {
                    g_netProviderChoice = 0;
                    lastTickSec = timeGetTime() / 1000;
                    PlaySoundEffect(2, 0, 0);
                }
                if (g_diKeyboardState[0x3C]) {
                    g_netProviderChoice = 1;
                    lastTickSec = timeGetTime() / 1000;
                    PlaySoundEffect(2, 0, 0);
                }
                /* Back from a network race: the host re-hosts and the client
                 * rejoins the same host on its own, so both land in the lobby
                 * again instead of on the host/join prompt. */
                {
                    extern int g_netLastRole;
                    extern const char *NetLastJoinedHost(void);
                    if (g_netLastRole == 1 && g_netProviderChoice == -1) {
                        g_netProviderChoice = 0;
                        DebugLog("Reentry: re-hosting automatically\n");
                    }
                    else if (g_netLastRole == 2 && NetLastJoinedHost() != NULL && g_netProviderChoice == -1) {
                        g_netProviderChoice = 1;
                        s_reentryAutoJoin = 1;
                        DebugLog("Reentry: rejoining %s automatically\n", NetLastJoinedHost());
                    }
                    g_netLastRole = 0;
                }
            }
            ns_connectionMode = (g_netProviderChoice != -1) ? 1 : 0;

            if (g_netProviderChoice == 0) {
                /* Single F1 press: create session and skip straight to
                 * state 2.  Re-entry path doesn't re-register with the
                 * matchmaker (the session was created before the race). */
                if (CreateNetworkSession(NS_STR_SESSION_PW, NS_STR_SESSION_NAME)) {
                    ns_setupMode = 1;
                    ns_lobbyState = 2;
                    /* List the room on the matchmaker again, so new players
                     * can find it between races (the listing from before the
                     * race is gone by now). */
                    if (MatchmakerHasToken() && MatchmakerGetUsername()[0]) {
                        UpnpOpenPort(NET_PORT_DEFAULT);
                        int csOk = MatchmakerCreateSession(MatchmakerGetUsername(), NET_PORT_DEFAULT);
                        DebugLog("Reentry: matchmaker listing %s\n", csOk ? "created" : "failed");
                    }
                    EnumNetworkSessions(1);
                    g_netEnumActive = 1;
                }
            }
            if (g_netProviderChoice == 1) {
                ns_lobbyState = 1;
                ns_setupMode = 2;
            }
        }

        /* State 1: join discovery only (host now skips this state). */
        if (ns_lobbyState == 1) {
            if (ns_setupMode != 3) {
                if ((g_totalFrames & 0x3F) == 0) {
                    EnumDirectPlaySessions();
                }
            }

            if (ns_setupMode == 2 && (g_resultsUnlockFlag != 0 || s_reentryAutoJoin)) {
                extern const char *g_cmdHostIP;
                extern const char *NetLastJoinedHost(void);
                const char *savedHost = g_cmdHostIP;
                char rejoinIp[64];
                int joinedNow;
                if (s_reentryAutoJoin) {
                    snprintf(rejoinIp, sizeof(rejoinIp), "%s", NetLastJoinedHost());
                    g_cmdHostIP = rejoinIp;
                }
                joinedNow = JoinNetworkSession(NS_STR_JOIN_SESSION, 0);
                g_cmdHostIP = savedHost;
                if (joinedNow == 1) {
                    if (s_reentryAutoJoin) {
                        /* The host may still be on its results screen. */
                        extern void NetClientJoinSetTimeout(int ms);
                        NetClientJoinSetTimeout(90000);
                        s_reentryAutoJoin = 0;
                    }
                    ns_lobbyState = 2;
                }
            }

            if (ns_lobbyState == 2) {
                EnumNetworkSessions(1);
                g_netEnumActive = 1;
            }
        }

        if (ns_lobbyState == 2 && ns_setupMode == 2) {
            extern int NetClientJoinTick(void);
            if (NetClientJoinTick() < 0) {
                CloseDirectPlaySession();
                NetDbgPush("HOST DID NOT ANSWER");
                PlaySoundEffect(0, 0, 0);
                ns_lobbyState = 0;
                ns_connectionMode = 0;
            }
        }

        if (ns_lobbyState == 2 && (g_totalFrames & 0x3F) == 0x20) {
            EnumNetworkSessions(1);   /* re-announce our character (see NetworkScreen) */
        }

        if (ns_lobbyState == 2) {
            if (ns_setupMode == 3) {
                if ((int)(g_totalFrames * 0x3F) == 0x19) {
                    g_netEnumActive = 0;
                }
            }
            else if (g_netEnumActive != 0) {
                g_netEnumActive = 0;
            }

            if (g_netGameStarted == 1) {
                if (g_diKeyboardState[0x3B]) {
                    if (ns_connectionMode == 0) {
                        if (g_netPlayerCount >= 2) {           /* host + at least one client */
                            InitNetworkGame();
                        }
                        lastTickSec = timeGetTime() / 1000;
                    }
                    ns_connectionMode = 1;
                }
                else {
                    ns_connectionMode = 0;
                }
            }

            if (g_netSessionActive != 0) {
                PlaySoundEffect(2, 0, 0);
                g_screenResult = 1;
                g_fadeState = 2;
            }
        }

        if (g_netJoinedFlag != 0) {
            ShiftLobbyHistory();
            CopyPaletteSrcToDst();
            PlaySoundEffect(0x1A, 0, 0);
            g_netJoinedFlag = 0;
        }

        if (ns_lobbyState < 2) {
            goto skip_text_entry_re;
        }
#ifdef SONICR_XBOX
        goto skip_text_entry_re;   /* no keyboard: see the main lobby */
#endif

        if (g_inputBits & 0x02) {
            if (inputThrottle == 0) {
                g_resultsTextBuffer[ns_slotQueueIdx] = -1;
                memcpy((char *)g_resultsPlayerData + 9 * 0x104,
                       g_resultsPlayerData, 0x104);
                if (ns_lobbyState == 0xA) {
                    ns_lobbyState = 0xB;
                }
                else {
                    PrependToResultsBuffer();
                    if (ns_lobbyState > 1) {
                        UpdateNetworkSync(g_resultsPlayerData, 0x104);
                    }
                    FilterNetworkProviders();
                    ShiftLobbyHistory();
                    CopyResultsToDst();
                }
                ns_slotQueueIdx = 0;
                inputThrottle = 1;
                lastTickSec = timeGetTime() / 1000;
                PlaySoundEffect(0x1A, 0, 0);
            }
        }
        else if (g_inputBits & 0x01) {
            if (inputThrottle == 0) {
                if (ns_slotQueueIdx > 0) {
                    ns_slotQueueIdx--;
                }
                inputThrottle = 1;
                lastTickSec = timeGetTime() / 1000;
            }
        }
        else {
            int btn = ScanTypedLetter();
            if (btn != -1) {
                if (inputThrottle == 0) {
                    g_resultsTextBuffer[ns_slotQueueIdx] = btn;
                    if (ns_slotQueueIdx < 0x30) {
                        ns_slotQueueIdx++;
                    }
                    inputThrottle = 1;
                    lastTickSec = timeGetTime() / 1000;
                }
            }
            else {
                inputThrottle = 0;
            }
        }

skip_text_entry_re:
        if (g_diKeyboardState[0x40]) {
            if (inputLock == 0) {
                int nextChar = (int)(signed short)g_menuPlayer.charId + 1;
                if (nextChar > 9) {
                    nextChar = 0;
                }
                while (g_charUnlockTable[nextChar] != 2) {
                    nextChar++;
                    if (nextChar > 9) {
                        nextChar = 0;
                    }
                }
                PlaySoundEffect(1, 0, 0);
                g_menuPlayer.charId = (short)nextChar;
                g_menuPlayer._unk_0x1E0 = (short)nextChar;
                g_menuPlayer._unk_0x1E0 = (short)nextChar;
                {
                    void **animTables = (void **)g_charAnimTables;
                    if (animTables) {
                        uintptr_t *animPtrs = (uintptr_t *)animTables[nextChar * 2];
                        if (animPtrs) {
                            const short *fs =
                                (const short *)(uintptr_t)animPtrs[g_menuPlayer.animId];
                            if (fs) {
                                g_animDataPtrs[0] = fs;
                                g_menuPlayer.animFrameIdx = (int)*fs - 1;
                            }
                        }
                    }
                }
                lastTickSec = timeGetTime() / 1000;
                inputLock = 1;
            }
        } else if (g_diKeyboardState[0x42]) {
            if (g_netGameStarted == 1 && inputLock == 0) {
                int nextSlot = localTrackIdx + 1;
                if (nextSlot > 4) {
                    nextSlot = 0;
                }
                while (g_gpTrackStatus[1 + nextSlot] == 0) {
                    nextSlot++;
                    if (nextSlot > 4) {
                        nextSlot = 0;
                    }
                }
                PlaySoundEffect(1, 0, 0);
                lastTickSec = timeGetTime() / 1000;
                localTrackIdx = nextSlot;
            }
            inputLock = 1;
        }
        else if (g_diKeyboardState[0x41]) {
            if (g_netGameStarted == 1 && inputLock == 0) {
                PlaySoundEffect(1, 0, 0);
                localModeIdx = (localModeIdx + 1) & 1;
                lastTickSec = timeGetTime() / 1000;
            }
            inputLock = 1;
        }
        else if (g_diKeyboardState[0x3F]) {
            /* MODE A/B toggle disabled — see NetworkScreen. */
            inputLock = 1;
        }
        else {
            inputLock = 0;
        }

        if ((int)(signed short)g_menuPlayer.charId != g_netFilteredProviders[0]) {
            g_playerBase[(int)(unsigned short)g_localPlayerIndex].charId =
                g_menuPlayer.charId;
            g_netFilteredProviders[0] = (int)(signed short)g_menuPlayer.charId;
            g_netBroadcastCharId = (int)(signed short)g_menuPlayer.charId;
            if (ns_lobbyState > 1) {
                EnumNetworkSessions(0);
                s_charResendSec = timeGetTime() / 1000;
            }
        }
        /* CHAR_CHANGE is one unacknowledged UDP packet. If the host misses
         * it, the host races us as the default character (issue #13), so
         * keep repeating the current pick once a second while in the lobby. */
        else if (ns_lobbyState > 1 && timeGetTime() / 1000 != s_charResendSec) {
            EnumNetworkSessions(0);
            s_charResendSec = timeGetTime() / 1000;
        }

        if (g_netLobbyPlayerSlot < 4) {
            int slot = g_netLobbyPlayerSlot;
            OpenNetworkSession(*(int *)(g_netPlayerDecorations + slot * NET_DECO_STRIDE + NET_DECO_DPID));
            *(int *)(g_netPlayerDecorations + slot * NET_DECO_STRIDE + NET_DECO_LOBBYCHAR) = g_netLobbyCharData[0];
            for (int i = 1; i <= 16; i++) {
                /* ? */;
            }
            {
                int *portraitDst = &g_netLobbyPortrait[slot * (0x48/4) + 2];
                for (int i = 0; i < 16; i++) {
                    portraitDst[i] = g_netLobbyCharData[i + 1];
                }
                g_netLobbyPortrait[slot * (0x48/4) + 1] = g_netLobbyCharData[0];
            }
            g_playerBase[slot].charId =
                *(short *)(g_netPlayerDecorations + slot * NET_DECO_STRIDE + NET_DECO_LOBBYCHAR);
            g_netLobbyPlayerSlot = 0xFF;
            if (g_netGameStarted != 0) {
                BuildSoundIndexList();
                UpdateNetworkSync(g_netSoundIndexBuf, 0x104);
            }
        }

        if (ns_lobbyState > 1 && g_netGameStarted != 0) {
            g_netGameInfoDest[1] = (localTrackIdx & 0xFFFF) | ((localModeIdx & 0xFFFF) << 16);
            if ((g_totalFrames & 0x3F) == 0x20) {
                UpdateNetworkSync(g_netGameInfoDest, 0x30);
            }
        }

        if (g_netReceivedLobbyData != 0) {
            for (int i = 0; i < (0x30/4); i++) {
                g_netGameInfoDest[i] = g_netLobbyConfigBuf[i];
            }
            localTrackIdx = (int)(short)(g_netGameInfoDest[1] & 0xFFFF);         /* [1] low word */
            localModeIdx  = (int)(short)(g_netGameInfoDest[1] >> 16);            /* [1] high word */
            g_weatherType = (int)(short)(g_netGameInfoDest[2] & 0xFFFF);         /* [2] low word */
            g_timeOfDay   = (int)(short)(g_netGameInfoDest[2] >> 16);            /* [2] high word */
            g_netReceivedLobbyData = 0;
        }

        if (g_netSessionActive != 0) {
            PlaySoundEffect(2, 0, 0);
            g_fadeState = 2;
            g_screenResult = 1;
        }

render_frame_re:
        g_menuPlayer.angleYaw = (g_totalFrames & 0xFF) << 4;
        g_emeraldSineOffX = (g_emeraldSineOffX + 0x4D) & 0xFFF;
        g_emeraldSineOffY = (g_emeraldSineOffY + 0xB6) & 0xFFF;
        g_emeraldSineOffZ = (g_emeraldSineOffZ + 0x73) & 0xFFF;
        UpdateFrameTimers();
        UpdateVertexLighting((Player *)g_playerBase);

        {
            const short *fs = g_animDataPtrs[0];
            if (fs) {
                int frame = (int)*fs;
                fs++;
                if (frame == -1) {
                    int k = (int)*fs;
                    fs -= k;
                    frame = (int)*fs;
                    fs++;
                }
                g_animDataPtrs[0] = fs;
                g_menuPlayer.animFrameIdx = (frame & 0xFFF) - 1;
                g_menuPlayer._unk_0x1C = frame & 0xFFF;
            }
        }

        AnimateVehicleGlow(&g_menuPlayer);

        {
            g_polyCount = 0;
            ProcessTpageStates();
            BeginFrame();
            s_pixTextHidden = (g_fadeState == 2);   /* fading out: drop the pixel-font text (it sits in front of the iris) */
            RenderBackground();
            if (ns_lobbyState >= 2) {
                int charId = (int)(signed short)g_menuPlayer.charId;
                int nameY = s_netCharModelY[charId] + 0x32;
                /* 0x48B82F pushes 0x8FF880 — see NetworkScreen. */
                RenderCharacterOnPodium(-190, nameY, 0x200,
                                        0xE80, g_menuPlayer.angleYaw, 0,
                                        &g_menuPlayer);
                {
                    int savedTrackId = g_trackId;
                    if (localTrackIdx == 4) {
                        g_trackId = 5;
                    }
                    intptr_t objPtr1 = (intptr_t)((char *)g_objectStructArray
                        + s_netTrackModelIdx[localTrackIdx] * 0x44);
                    Draw3DModelD3D(0x190, 0x12C, 0x400, 0xE80,
                                   g_menuPlayer.angleYaw, 0, objPtr1, 2);
                    if (localTrackIdx == 4) {
                        g_trackId = savedTrackId;
                    }
                }
                {
                    intptr_t objPtr2 = (intptr_t)((char *)g_objectStructArray
                        + modeModelLocal[localModeIdx] * 0x44);
                    Draw3DModelD3D(0, 0x113, 0x400, 0xE80,
                                   g_menuPlayer.angleYaw, 0, objPtr2, 2);
                }
                RenderResultsScreen(ns_lobbyState, localModeIdx);

                {
                    int iconH = 24;
                    int textH = 0x14;
                    int iconYOff = (textH - iconH) / 2;
                    int standY = 0x1c2 - 0x1a;
                    for (int pi = 0; pi < g_resultsPlayerCount && pi < NET_MAX_PLAYERS; pi++) {
                        int iconIdx = net_platform_icon(
                            net_get_slot_platform(pi), net_get_slot_region(pi));
                        int icoUvX, icoUvY;
                        net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
                        DrawTexturedQuad(
                            0xc - iconH - 2, standY + iconYOff,
                            0x40000000,
                            iconH, iconH,
                            TPAGE_PLATFORM_ICONS,
                            icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
                            0xFFFFFFFFu);
                        standY -= 0x16;
                    }
                }
            }

            /* Mode-select / search prompt for states 0 and 1; in-lobby
             * username via the original game font for state 2+. Mirrors
             * the corresponding block in NetworkScreen. */
            if (ns_lobbyState < 2) {
                if (MatchmakerHasToken() && MatchmakerGetUsername()[0]) {
                    const char *prefix = "LOGGED IN AS ";
                    int prefixW = PixTextWidth(prefix, 2);
                    int glyphs[MM_MAX_USERNAME];
                    StringToGlyphIds(MatchmakerGetUsername(), glyphs, MM_MAX_USERNAME);
                    int glyphW = MeasureGlyphString(glyphs, MM_MAX_USERNAME);
                    int x = (640 - (prefixW + glyphW)) / 2;
                    DrawPixText(prefix, x, 30 + 4, 2, 0xC0C0C0C0);
                    int endX = DrawGlyphString(x + prefixW, 30 + 2, glyphs, MM_MAX_USERNAME);
                    {
                        int textH = 0x14;
                        int iconH = 24;
                        int iconYOff = (textH - iconH) / 2;
                        int iconIdx = net_platform_icon(MM_PLATFORM,
                                                        (uint8_t)platform_get_region());
                        int icoUvX, icoUvY;
                        net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
                        DrawTexturedQuad(
                            endX + 4, 30 + 2 + iconYOff,
                            0x40000000,
                            iconH, iconH,
                            TPAGE_PLATFORM_ICONS,
                            icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
                            0xFFFFFFFFu);
                    }
                }
                else {
                    const char *idLine = "LAN MODE";
                    int idW = PixTextWidth(idLine, 2);
                    DrawPixText(idLine, (640 - idW) / 2, 30, 2, 0xC0C0C0C0);
                }

                extern int g_netLastRole;
                if (ns_lobbyState == 0 && g_netLastRole != 0) {
                    /* Rejoining automatically after a race (see state 0). */
                    const char *msg = "RETURNING TO LOBBY...";
                    int w = PixTextWidth(msg, 3);
                    DrawPixText(msg, (640 - w) / 2, 220, 3, 0xFFFFFFFF);
                }
                else if (ns_lobbyState == 0) {
                    const char *l1 = NETTXT_HOST;
                    const char *l2 = NETTXT_JOIN;
                    int w1 = PixTextWidth(l1, 4);
                    int w2 = PixTextWidth(l2, 4);
                    DrawPixText(l1, (640 - w1) / 2, 200, 4, 0xFFFFFFFF);
                    DrawPixText(l2, (640 - w2) / 2, 250, 4, 0xFFFFFFFF);
                }
                else /* ns_lobbyState == 1 */ {
                    const char *msg = "SEARCHING FOR SESSIONS...";
                    int w = PixTextWidth(msg, 3);
                    DrawPixText(msg, (640 - w) / 2, 220, 3, 0xFFFFFFFF);
                }
            }
            else if (MatchmakerHasToken() && MatchmakerGetUsername()[0]) {
                {
                    int textH = 0x14;
                    int iconH = 24;
                    int iconYOff = (textH - iconH) / 2;
                    int iconIdx = net_platform_icon(MM_PLATFORM,
                                                    (uint8_t)platform_get_region());
                    int icoUvX, icoUvY;
                    net_platform_icon_uv(iconIdx, &icoUvX, &icoUvY);
                    DrawTexturedQuad(
                        20, 60 + iconYOff,
                        0x40000000,
                        iconH, iconH,
                        TPAGE_PLATFORM_ICONS,
                        icoUvX, icoUvY, PLATFORM_ICON_SIZE, PLATFORM_ICON_SIZE,
                        0xFFFFFFFFu);
                }
                int glyphs[MM_MAX_USERNAME];
                StringToGlyphIds(MatchmakerGetUsername(), glyphs, MM_MAX_USERNAME);
                DrawGlyphString(20 + 26, 60, glyphs, MM_MAX_USERNAME);
            }

            if (g_fadeLevel < 0) {
                RenderFadeOverlay();
            }
            RenderWavingMenuBackground();
            EndFrame();
            FlipD3D();
        }

        g_totalFrames2++;
        g_totalFrames++;

        WaitForFrameCap();
        g_screenFPS = 30;
        g_screenBaseTime = (int)timeGetTime();
    }
    /* unreachable */
}

/* =====================================================================
 * ResultsScreen helper functions
 * ===================================================================== */

/* PLAYER_STRIDE_RS replaced by sizeof(Player) */

/* FUN_0047fde8 (per-character random blink/expression timer) is translated
 * canonically as UpdateFrameTimers in game_loop.c — same function is called
 * from screen_charsel.c and the main race loop. ResultsScreen previously had
 * a local broken duplicate here that never reset renderState to 0 and had
 * the wrong random-range splits, leaving characters stuck in mid-blink
 * expression states (the "one eye open, one eye closed" Sonic bug). Removed
 * 2026-04-13. ResultsScreen now calls UpdateFrameTimers directly. */

/**
 * RenderTimeDigits — FUN_004d15c4 — 937 bytes
 * Renders a formatted race time as MM'SS"CC using digit sprites.
 *
 * EAX=screenX, EDX=screenY, EBX=depthBucket, ECX=timeValue (game ticks)
 * Stack: highlightStyle (0=dim, 2=bright)
 *
 * NOTE: Original binary calls Blit2DSprite (0x44c3b4), which writes to the
 * SOFTWARE depth-sort list. On GL we use DrawTexturedQuad which goes through
 * the tpage vertex batching system that our GL renderer processes.
 */
static void RenderTimeDigits(int screenX, int screenY, int depthBucket,
                             int timeValue, int highlightStyle)
{
    (void)depthBucket;
    int tpage = g_tpageParallax1;                                    /* 0x008F6C3C */

    /* Style determines texture row offset */
    int baseU = 0;
    if (highlightStyle == 2) {
        baseU = 0x56;
    }

    /* Decompose time: total_seconds = timeValue / 60, frames = timeValue % 60 */
    int totalSeconds = timeValue / 60;
    int frames = timeValue % 60;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    /* Convert frames to centiseconds: (frames * 100) / 60 */
    int centiseconds = (frames * 100) / 60;

    /* Macro: adapt Blit2DSprite calls to DrawTexturedQuad.
     * 0x447A0000 = 1000.0f depth (standard 2D overlay). */
#define SPRITE(sx, sy, db, w, h, tp, uX, uY, uW, uH, fl) \
    DrawTexturedQuad((sx), (sy), 0x447A0000, (w), (h), (tp), (uX), (uY), (uW), (uH), VERTEX_WHITE)

    if (highlightStyle != 0) {
        /* Highlighted style: larger digits */
        int mTens = minutes / 10;
        SPRITE(screenX, screenY, depthBucket, 0x10, 0x20, tpage, mTens*8+baseU, 0x88, 8, 0x10, 0);
        int mOnes = minutes % 10;
        SPRITE(screenX+0x10, screenY, depthBucket, 0x10, 0x20, tpage, mOnes*8+baseU, 0x88, 8, 0x10, 0);

        /* Separator ' */
        SPRITE(screenX+0x22, screenY, depthBucket, 6, 0x20, tpage, 0x50+baseU, 0x88, 3, 0x10, 0);

        int sTens = seconds / 10;
        SPRITE(screenX+0x28, screenY, depthBucket, 0x10, 0x20, tpage, sTens*8+baseU, 0x88, 8, 0x10, 0);
        int sOnes = seconds % 10;
        SPRITE(screenX+0x38, screenY, depthBucket, 0x10, 0x20, tpage, sOnes*8+baseU, 0x88, 8, 0x10, 0);

        /* Separator " */
        SPRITE(screenX+0x4a, screenY, depthBucket, 6, 0x20, tpage, 0x50+baseU, 0x88, 3, 0x10, 0);

        int cTens = centiseconds / 10;
        SPRITE(screenX+0x50, screenY, depthBucket, 0x10, 0x20, tpage, cTens*8+baseU, 0x88, 8, 0x10, 0);
        int cOnes = centiseconds % 10;
        SPRITE(screenX+0x60, screenY, depthBucket, 0x10, 0x20, tpage, cOnes*8+baseU, 0x88, 8, 0x10, 0);
    }
    else {
        /* Dim style: fixed separator glyph at all digit positions-
         * Binary uses uvX=0xB1, uvY=0x92 (the colon/separator glyph in the
         * small-digit row), NOT actual digit values. Confirmed from disasm
         * of 0x4d169a, 0x4d17c2, 0x4d1909 push sequences. */
        SPRITE(screenX, screenY+0xE, depthBucket, 0x10, 6, tpage, 0xB1, 0x92, 8, 3, 0);
        SPRITE(screenX+0x10, screenY+0xE, depthBucket, 0x10, 6, tpage, 0xB1, 0x92, 8, 3, 0);

        SPRITE(screenX+0x22, screenY, depthBucket, 6, 0x20, tpage, 0x50+baseU, 0x88, 3, 0x10, 0);

        SPRITE(screenX+0x28, screenY+0xE, depthBucket, 0x10, 6, tpage, 0xB1, 0x92, 8, 3, 0);
        SPRITE(screenX+0x38, screenY+0xE, depthBucket, 0x10, 6, tpage, 0xB1, 0x92, 8, 3, 0);

        SPRITE(screenX+0x4a, screenY, depthBucket, 6, 0x20, tpage, 0x50+baseU, 0x88, 3, 0x10, 0);

        SPRITE(screenX+0x50, screenY+0xE, depthBucket, 0x10, 6, tpage, 0xB1, 0x92, 8, 3, 0);
        SPRITE(screenX+0x60, screenY+0xE, depthBucket, 0x10, 6, tpage, 0xB1, 0x92, 8, 3, 0);
    }
#undef SPRITE
}

/**
 * DrawResultTimeEntry — FUN_004c4d1c — 380 bytes
 *
 * Renders one player's lap time with highlight detection for new records.
 * Called 18× by the software-path results overlay dispatcher (0x4c6028).
 * The binary uses Blit2DSprite (0x44c3b4) to write into the software
 * framebuffer; the D3D-path sibling at 0x4c5698 uses DrawTexturedQuad
 * (0x450c38) instead.
 *
 * PORT NOTE — "CHEAT TRANSLATION":
 * This C code takes the software-path function's *logic* (player struct
 * indexing, highlight lookup from the char-detail table, label/time layout)
 * but substitutes DrawTexturedQuad and RenderTimeDigits for the binary's
 * software blits. That's a deliberate cheat: the port doesn't run the
 * software renderer at all, so we borrowed the software helper's structure
 * and replaced its draw primitives with GL-compatible ones.
 *
 * The properly-faithful D3D version lives in hud_full.c:DrawLapCheckpoint,
 * translated from 0x4c5698. That function is currently DEAD in the port —
 * nothing calls it. This function is what the port's ResultsScreen
 * reimplementation (screen_misc.c) uses for every per-player per-lap time
 * row on the results screen (GP, 2P, time attack, multi-player).
 *
 * Keep this function. Renaming it to _Soft makes the origin explicit so
 * future sessions don't confuse it with a binary-faithful D3D translation.
 *
 * EAX=screenX, EDX=screenY, EBX=playerIdx, ECX=lapIdx
 * Stack: style (0=normal, 4=championship)
 */
/* g_gpCharDetail is a #define into g_saveBlock via sonicr_globals.h */

static void DrawResultTimeEntry(int screenX, int screenY, int playerIdx,
                                     int lapIdx, int style)
{
    Player *pb = (Player *)g_playerBase;

    /* Access player struct directly */
    Player *pp = &pb[playerIdx];
    int placement = (short)pp->lapsCompleted;                          /* P_INT(0x5C)>>16 = lapsCompleted */

    int highlight = 0;  /* 0=no highlight, 1=normal, 2=best match */

    if (placement > lapIdx) {
        /* Player finished this lap — check for record */
        int charId = (short)pp->charId;
        int charBase = charId * 41;
        /* Lap time for this entry */
        int *lapBase = g_racePointsLaps;
        int lapTime = lapBase[playerIdx * 3 + lapIdx];

        /* Best lap time from charDetail — binary uses g_trackId directly (not -1)
         * Offsets verified from binary base addresses vs g_gpCharDetail (0x8fbc88) */
        int bestTime;
        if (style == 4) {
            bestTime = g_gpCharDetail[charBase + g_trackId + 4];   /* 0x8fbc98 */
        }
        else if (style == 0) {
            bestTime = g_gpCharDetail[charBase + g_trackId + 14];  /* 0x8fbcc0 */
        }
        else {
            bestTime = g_gpCharDetail[charBase + g_trackId + 24];  /* 0x8fbce8 */
        }

        if (lapTime == bestTime) {
            highlight = 2;  /* matches best → gold highlight */
        }
        else {
            highlight = 1;  /* finished but not best */
        }
    }

    /* Label sprite: Blit2DSprite at screenX*2-0x58
     * Binary 0x4c4e29: width=0x50(80), tpage=g_tpageBase+1, uvX=0, uvY=lapIdx*16+0x40 */
    int labelV = lapIdx * 0x10 + 0x40;
    int drawX = screenX * 2;
    int drawY = screenY * 2;
    DrawTexturedQuad(drawX - 0x58, drawY, 0x447A0000, 0x50, 0x20,
                     g_uiTexPage + 1, 0, labelV, 0x28, 0x10, VERTEX_WHITE);

    /* Time digits: binary passes screenX*2 (NOT the label-adjusted X)
     * Binary 0x4c4e87: EAX = [ebp-0x1c] = screenX*2 */
    int *lapBase = g_racePointsLaps;
    int timeValue = lapBase[playerIdx * 3 + lapIdx];
    RenderTimeDigits(drawX, drawY, 0x20, timeValue, highlight);
}

/**
 * DrawResultBestLap — FUN_004c4e98 — 326 bytes
 * Draws the best single-lap time for a player. Checks each lap against
 * the per-character best time table to determine highlight.
 *
 * NOTE: Blit2DSprite→SubmitSpriteQuad adaptation (see RenderTimeDigits).
 */
static void DrawResultBestLap(int screenX, int screenY, int playerIdx, int style)
{
    Player *pp = &((Player *)g_playerBase)[playerIdx];

    /* Look up best time from per-char detail table */
    int charId = (short)pp->charId;
    int charBase = charId * 41;
    int bestTime;
    /* Best lap time from charDetail — same offsets as DrawResultTimeEntry
     * Binary uses g_trackId directly (not -1) */
    if (style == 4) {
        bestTime = g_gpCharDetail[charBase + g_trackId + 4];        /* 0x8fbc98 */
    }
    else if (style == 0) {
        bestTime = g_gpCharDetail[charBase + g_trackId + 14];       /* 0x8fbcc0 */
    }
    else {
        bestTime = g_gpCharDetail[charBase + g_trackId + 24];       /* 0x8fbce8 */
    }

    int placement = (short)pp->lapsCompleted;                            /* P_INT(0x5C)>>16 = lapsCompleted */
    int highlight = 1;  /* default: finished but not best */

    if (placement == 3) {
        /* Check if any individual lap matches the best time */
        int *lapBase = g_racePointsLaps;
        for (int lap = 0; lap < 3; lap++) {
            if (placement > lap && bestTime == lapBase[playerIdx * 3 + lap]) {
                highlight = 2;  /* gold highlight */
                break;
            }
        }
    }

    /* Label sprite: "LAP RECORD" at uvY=0x80 in RES00.RAW texture
     * Binary has 0x90 but texture shows LAP RECORD at 0x80, COURSE RECORD at 0x90 */
    int drawX = screenX * 2;
    int drawY = screenY * 2;
    DrawTexturedQuad(drawX - 0xD0, drawY, 0x447A0000, 0xC8, 0x20,
                     g_uiTexPage + 1, 0, 0x80, 0x64, 0x10, VERTEX_WHITE);

    /* Time digits: binary passes screenX*2, NOT label-adjusted X
     * Binary 0x4c4fc5: EAX = [ebp-0x14] = screenX*2 */
    RenderTimeDigits(drawX, drawY, 0x20, bestTime, highlight);
}

/**
 * DrawResultTotalTime — FUN_004c4ff4 — 517 bytes
 * Draws the total race time for a player, with record highlight.
 *
 * NOTE: Blit2DSprite→SubmitSpriteQuad adaptation.
 */
static void DrawResultTotalTime(int screenX, int screenY, int playerIdx, int style)
{
    Player *pp = &((Player *)g_playerBase)[playerIdx];
    int placement = (short)pp->lapsCompleted;

    /* Highlight check: only if all 3 laps completed — binary 0x4c5020 */
    int highlight = 0;
    if (placement == 3) {
        /* Style-dependent charDetail comparison — binary jump table at 0x4c4fe0 */
        int charId = (short)pp->charId;
        int charBase = charId * 41;
        /* Best total time from charDetail — binary jump table at 0x4c4fe0
         * Uses g_trackId directly, offsets verified from binary base addresses */
        int bestTotal;
        switch (style) {
            case 0: 
                bestTotal = g_gpCharDetail[charBase + g_trackId + 9]; 
                break;   /* 0x8fbcac */
            case 1: 
                bestTotal = g_gpCharDetail[charBase + g_trackId + 19];
                break;  /* 0x8fbcd4 */
            case 2:
                bestTotal = g_gpCharDetail[charBase + g_trackId + 29];
                break;  /* 0x8fbcfc */
            case 3:
                bestTotal = g_gpCharDetail[charBase + g_trackId + 34];
                break;  /* 0x8fbd10 */
            case 4:
                bestTotal = g_gpCharDetail[charBase + g_trackId - 1];
                break;   /* 0x8fbc84 */
            default:
                bestTotal = 0;
                break;
        }

        int totalTime = g_racePointsTotal[playerIdx];
        if (totalTime == bestTotal) {
            highlight = 2;
        }
        else {
            highlight = 1;
        }
    }

    /* Binary 0x4c502b: always renders (no early return for unfinished) */
    int tpage = g_uiTexPage + 1;  /* results texture loaded to g_uiTexPage+1 */
    int drawX = screenX * 2;
    int drawY = screenY * 2;

    /* Label sprite: two variants based on numViewports
     * Binary 0x4c5042: cmp [0x6e9910], 1; jle large_label */
    if (g_numViewports > 1) {
        /* Small label: width=0x50, uvX=0, uvY=0xa0, x offset=0x58
         * Binary 0x4c504b */
        DrawTexturedQuad(drawX - 0x58, drawY, 0x447A0000, 0x50, 0x20,
                         tpage, 0, 0xa0, 0x28, 0x10, VERTEX_WHITE);
    }
    else {
        /* Large label: width=0xC8, uvX=0, uvY=0x70, x offset=0xD0
         * Binary 0x4c51b5 */
        DrawTexturedQuad(drawX - 0xD0, drawY, 0x447A0000, 0xC8, 0x20,
                         tpage, 0, 0x70, 0x64, 0x10, VERTEX_WHITE);
    }

    /* Time digits: binary passes screenX*2 (NOT label-adjusted)
     * Binary 0x4c51d9-0x4c51ed: reads total from g_racePointsTotal */
    RenderTimeDigits(drawX, drawY, 0x20, g_racePointsTotal[playerIdx], highlight);
}

/**
 * DrawResultChampPoints — FUN_004c5210 — 456 bytes
 * Draws championship point standings for a player.
 *
 * NOTE: Blit2DSprite→SubmitSpriteQuad adaptation.
 */
static void DrawResultChampPoints(int screenX, int screenY, int playerIdx, int style)
{

    Player *pp = &((Player *)g_playerBase)[playerIdx];

    /* Style-dependent charDetail lookup — binary jump table at 0x4c51fc
     * Each case reads a "best" value from a different charDetail sub-table.
     * Binary: EBX=playerIdx at entry, used for struct offset. */
    int charId = (short)pp->charId;
    int charBase = charId * 41;
    int value = 0;  /* esi �� value to render AND compare */

    if (style <= 4) {
        /* Offsets verified from binary: charId*164 + g_trackId*4 + base_address
         * Converted to g_gpCharDetail[charBase + g_trackId + int_offset] */
        switch (style) {
            case 0:
                value = g_gpCharDetail[charBase + g_trackId + 9];
                break; /* 0x8fbcac */
            case 1:
                value = g_gpCharDetail[charBase + g_trackId + 19];
                break; /* 0x8fbcd4 */
            case 2:
                value = g_gpCharDetail[charBase + g_trackId + 29];
                break; /* 0x8fbcfc */
            case 3:
                value = g_gpCharDetail[charBase + g_trackId + 34];
                break; /* 0x8fbd10 */
            case 4:
                value = g_gpCharDetail[charBase + g_trackId - 1];
                break; /* 0x8fbc84 */
        }
    }

    /* Highlight: compare charDetail value with actual total time
     * Binary 0x4c534d: no early return — always renders
     * highlight=2 if lapsCompleted==3 AND value==racePointsTotal, else highlight=1 */
    int placement = (short)pp->lapsCompleted;
    int highlight = 1;
    if (placement == 3 && value == g_racePointsTotal[playerIdx]) {
        highlight = 2;
    }

    /* Label sprite: binary 0x4c5386: width=0xC8, tpage=g_tpageBase+1
     * uvX=0, uvY=0x90 (binary push order: 0 then 0x90) */
    int drawX = screenX * 2;
    int drawY = screenY * 2;
    DrawTexturedQuad(drawX - 0xD0, drawY, 0x447A0000, 0xC8, 0x20,
                     g_uiTexPage + 1, 0, 0x90, 0x64, 0x10, VERTEX_WHITE);

    /* Time digits: binary renders esi (charDetail value), NOT standings
     * Binary 0x4c53c4: mov ecx, esi */
    RenderTimeDigits(drawX, drawY, 0x20, value, highlight);
}

/**
 * DrawResultWinnerTime — FUN_004c53ec — 451 bytes
 * Finds the first finished player and renders their total time with a label.
 * Used by case 2 (special results screen type).
 *
 * EAX=screenX, EDX=screenY, EBX=style
 *
 * Default time if no finished player: g_raceSpeedMult * 480.
 * Label uvY=0xC0 (different from "TOTAL" labels at 0x70/0xa0).
 */
static void DrawResultWinnerTime(int screenX, int screenY, int style)
{

    /* Default time = g_raceSpeedMult * 480 — binary 0x4c53fe-0x4c5416 */
    int timeValue = g_raceSpeedMult * 480;
    int highlight = 0;

    /* Search for first finished player — binary 0x4c5425-0x4c5456 */
    int playerIdx;
    for (playerIdx = 0; playerIdx < g_numViewports; playerIdx++) {
        Player *pp = &((Player *)g_playerBase)[playerIdx];
        int laps = (short)pp->lapsCompleted;
        if (laps == 3) {
            /* Found finished player — style-dependent charDetail lookup
             * Binary jump table at 0x4c53d8 */
            if (style <= 4) {
                int charId = (short)pp->charId;
                int charBase = charId * 41;
                int bestTime;
                /* Offsets verified from binary: base addresses relative to g_gpCharDetail */
                switch (style) {
                    case 0:
                        bestTime = g_gpCharDetail[charBase + g_trackId + 9];
                        break; /* 0x8fbcac */
                    case 1:
                        bestTime = g_gpCharDetail[charBase + g_trackId + 19];
                        break; /* 0x8fbcd4 */
                    case 2:
                        bestTime = g_gpCharDetail[charBase + g_trackId + 29];
                        break; /* 0x8fbcfc */
                    case 3:
                        bestTime = g_gpCharDetail[charBase + g_trackId + 34];
                        break; /* 0x8fbd10 */
                    case 4:
                        bestTime = g_gpCharDetail[charBase + g_trackId - 1];
                        break; /* 0x8fbc84 */
                    default:
                        bestTime = 0;
                        break;
                }

                timeValue = g_racePointsTotal[playerIdx];
                if (bestTime == timeValue) {
                    highlight = 2;
                }
                else {
                    highlight = 1;
                }
            }
            else {
                /* style > 4: read total, highlight=1 — binary 0x4c5544 */
                timeValue = g_racePointsTotal[playerIdx];
                highlight = 1;
            }
            break;
        }
    }

    /* Label sprite: width=0xC8, uvX=0, uvY=0xC0
     * Binary 0x4c555b-0x4c558d */
    int drawX = screenX * 2;
    int drawY = screenY * 2;
    DrawTexturedQuad(drawX - 0xD0, drawY, 0x447A0000, 0xC8, 0x20,
                     g_uiTexPage + 1, 0, 0xC0, 0x64, 0x10, VERTEX_WHITE);

    /* Time digits: screenX*2, NOT label-adjusted — binary 0x4c559b */
    RenderTimeDigits(drawX, drawY, 0x20, timeValue, highlight);
}

/**
 * DrawResultsOverlay — FUN_004c6028 — 936 bytes
 * Dispatches text rendering per screenType via jump table.
 * Each case renders lap times, best times, totals, and/or standings.
 *
 * EAX = screenType (0-6)
 *
 * NOTE: Blit2DSprite→SubmitSpriteQuad adaptation for all sprite calls.
 * Original uses per-screenType position data from tables at 0x503994+.
 * Position values are hardcoded from the binary data tables here.
 */
static void DrawResultsOverlay(int screenType)
{
    /* ROM position tables from PE .data section at 0x503994+.
     * Each pair is {screenX, screenY} passed to the Draw* helpers.
     * Helpers multiply by 2 and apply their own offsets internally. */

    /* Case 0 (GP) and cases 3/4 (TA) share the same layout positions.
     * Case 0: 0x503994, Cases 3/4: 0x5039c4 — same values. */
    static const int s_gpPos[][2] = {
        { 56, 108}, /* lap 0:  [0x503994] */
        { 56, 128}, /* lap 1:  [0x50399c] */
        { 56, 148}, /* lap 2:  [0x5039a4] */
        {244, 108}, /* total:  [0x5039ac] */
        {244, 128}, /* best:   [0x5039b4] */
        {244, 148}, /* champ:  [0x5039bc] */
    };

    /* Case 1 with numViewports==2: dual-column, 0x5039f4+ */
    static const int s_2pDualPos[][2] = {
        /* Player 0 */
        { 68,  96}, /* P0 lap 0: [0x5039f4] */
        { 68, 116}, /* P0 lap 1: [0x5039fc] */
        { 68, 136}, /* P0 lap 2: [0x503a04] */
        { 68, 156}, /* P0 total: [0x503a0c] */
        /* Player 1 */
        {232,  96}, /* P1 lap 0: [0x503a14] */
        {232, 116}, /* P1 lap 1: [0x503a1c] */
        {232, 136}, /* P1 lap 2: [0x503a24] */
        {232, 156}, /* P1 total: [0x503a2c] */
    };

    /* Case 1 with numViewports!=2: per-viewport loop, 0x503a34+, stride 0x20 */
    static const int s_multiPos[4][4][2] = {
        /* Player 0 */  {{ 60,  8}, { 60, 28}, { 60, 48}, { 60, 68}},
        /* Player 1 */  {{240,  8}, {240, 28}, {240, 48}, {240, 68}},
        /* Player 2 */  {{ 60,100}, { 60,120}, { 60,140}, { 60,160}},
        /* Player 3 */  {{240,100}, {240,120}, {240,140}, {240,160}},
    };

    /* Cases 5/6: total + champ only, 0x503ab4+ */
    static const int s_summaryPos[][2] = {
        {164, 108}, /* total: [0x503ab4] */
        {164, 140}, /* champ: [0x503abc] */
    };

    switch (screenType) {
        case 0: /* GP single race — 0x4c603e: style=4 */
            DrawResultTimeEntry(s_gpPos[0][0], s_gpPos[0][1], 0, 0, 4);
            DrawResultTimeEntry(s_gpPos[1][0], s_gpPos[1][1], 0, 1, 4);
            DrawResultTimeEntry(s_gpPos[2][0], s_gpPos[2][1], 0, 2, 4);
            DrawResultTotalTime(s_gpPos[3][0], s_gpPos[3][1], 0, 4);
            DrawResultBestLap(s_gpPos[4][0], s_gpPos[4][1], 0, 4);
            DrawResultChampPoints(s_gpPos[5][0], s_gpPos[5][1], 0, 4);
            break;

        case 1: /* 2P/multi — 0x4c60d1: checks numViewports */
            if (g_numViewports == 2) {
                /* Dual-column: P0 left, P1 right — 0x4c60e0 */
                DrawResultTimeEntry(s_2pDualPos[0][0], s_2pDualPos[0][1], 0, 0, 4);
                DrawResultTimeEntry(s_2pDualPos[1][0], s_2pDualPos[1][1], 0, 1, 4);
                DrawResultTimeEntry(s_2pDualPos[2][0], s_2pDualPos[2][1], 0, 2, 4);
                DrawResultTotalTime(s_2pDualPos[3][0], s_2pDualPos[3][1], 0, 4);
                DrawResultTimeEntry(s_2pDualPos[4][0], s_2pDualPos[4][1], 1, 0, 4);
                DrawResultTimeEntry(s_2pDualPos[5][0], s_2pDualPos[5][1], 1, 1, 4);
                DrawResultTimeEntry(s_2pDualPos[6][0], s_2pDualPos[6][1], 1, 2, 4);
                DrawResultTotalTime(s_2pDualPos[7][0], s_2pDualPos[7][1], 1, 4);
            }
            else {
                /* Per-viewport loop — 0x4c61ad */
                for (int i = 0; i < g_numViewports; i++) {
                    DrawResultTimeEntry(s_multiPos[i][0][0], s_multiPos[i][0][1], i, 0, 4);
                    DrawResultTimeEntry(s_multiPos[i][1][0], s_multiPos[i][1][1], i, 1, 4);
                    DrawResultTimeEntry(s_multiPos[i][2][0], s_multiPos[i][2][1], i, 2, 4);
                    DrawResultTotalTime(s_multiPos[i][3][0], s_multiPos[i][3][1], i, 4);
                }
            }
            break;

        case 2: /* Special — 0x4c6230: calls FUN_004c53ec */
            DrawResultWinnerTime(164, 152, 2);  /* [0x503ac4]=164, [0x503ac8]=152, style=2 */
            break;

        case 3: /* TA single-lap — 0x4c624b: style=0 */
            DrawResultTimeEntry(s_gpPos[0][0], s_gpPos[0][1], 0, 0, 0);
            DrawResultTimeEntry(s_gpPos[1][0], s_gpPos[1][1], 0, 1, 0);
            DrawResultTimeEntry(s_gpPos[2][0], s_gpPos[2][1], 0, 2, 0);
            DrawResultTotalTime(s_gpPos[3][0], s_gpPos[3][1], 0, 0);
            DrawResultBestLap(s_gpPos[4][0], s_gpPos[4][1], 0, 0);
            DrawResultChampPoints(s_gpPos[5][0], s_gpPos[5][1], 0, 0);
            break;

        case 4: /* TA variant — 0x4c62d5: style=1 */
            DrawResultTimeEntry(s_gpPos[0][0], s_gpPos[0][1], 0, 0, 1);
            DrawResultTimeEntry(s_gpPos[1][0], s_gpPos[1][1], 0, 1, 1);
            DrawResultTimeEntry(s_gpPos[2][0], s_gpPos[2][1], 0, 2, 1);
            DrawResultTotalTime(s_gpPos[3][0], s_gpPos[3][1], 0, 1);
            DrawResultBestLap(s_gpPos[4][0], s_gpPos[4][1], 0, 1);
            DrawResultChampPoints(s_gpPos[5][0], s_gpPos[5][1], 0, 1);
            break;

        case 5: /* Championship summary — 0x4c6368: style=2 */
            DrawResultTotalTime(s_summaryPos[0][0], s_summaryPos[0][1], 0, 2);
            DrawResultChampPoints(s_summaryPos[1][0], s_summaryPos[1][1], 0, 2);
            break;

        case 6: /* Championship summary — 0x4c639c: style=3 */
            DrawResultTotalTime(s_summaryPos[0][0], s_summaryPos[0][1], 0, 3);
            DrawResultChampPoints(s_summaryPos[1][0], s_summaryPos[1][1], 0, 3);
            break;
    }
}

/* DrawResultsBalloonsForPlayer — FUN_004c7088 — 359 bytes
 * Renders 3D rotating objects around a position on the results
 * screen. Loops over player's collisionCount items, placing each at an
 * angle around (xPos, zBase) with per-model RGB coloring.
 * EAX=xPos, EDX=yPos, EBX=zBase, ECX=playerIdx */
static void DrawResultsBalloonsForPlayer(int xPos, int yPos, int zBase, int playerIdx)
{
    /* Base angle from frame counter — 0x4c709b */
    int baseAngle = ((g_totalFrames & 0xFF) << 4);  /* EAX=0x4c70ad */

    /* Copy 10-int position template to local — 0x4c70bc-0x4c70c3 */
    int localTable[10];
    for (int i = 0; i < 10; i++) {
        localTable[i] = s_resultObjTemplate[i];
    }

    /* Player struct access — 0x4c70b5-0x4c70cf */
    Player *pl = &g_playerBase[playerIdx];

    /* Loop count = player collisionCount (offset 0x1F4) — 0x4c70cf */
    int loopCount = pl->collisionCount;

    if (loopCount <= 0) {
        return;
    }

    /* Angle step = 0x1000 / loopCount — 0x4c70d8-0x4c70ea */
    int angleStep = 0x1000 / loopCount;

    /* Position scale from charId index into local table — 0x4c70ed-0x4c710b */
    int charId = (int)pl->charId;  /* short at offset 0xF2 — 0x4c7100 */
    int posScale = localTable[charId];

    /* Model index table start: playerIdx * 5 — 0x4c710e-0x4c7125 */
    int tableIdx = playerIdx * 5;

    int angle = baseAngle;
    for (int vi = 0; vi < loopCount; vi++) {
        /* Position from angle — 0x4c717a-0x4c71aa */
        int cosVal = g_cosTable[angle & 0xFFF];      /* 0x4c7186 */
        int sinVal = g_sinTable[angle & 0xFFF];       /* 0x4c718e */
        int xOff = xPos + ((posScale * cosVal) >> 14);   /* 0x4c71ad */
        int zOff = zBase + ((-posScale * sinVal) >> 14);  /* 0x4c71b7 */

        /* Model color lookup — 0x4c719a-0x4c71bf */
        int modelIdx = g_resultModelIndices[tableIdx + vi];  /* 0x4c719d */
        int *colorPtr = (int *)((char *)g_balloonArray + modelIdx * 0x2C + 0x1C);

        /* 0x4c7132-0x4c714c */
        RenderBalloonModelForResultsScreen(xOff, yPos, zOff,
                                    0, 0, 0, 3,
                                    colorPtr[0], colorPtr[1], colorPtr[2]);

        /* Advance angle — 0x4c7151-0x4c715f */
        angle = (angle + angleStep) & 0xFFF;
    }
}

/* Results screen state — slots in the 0x92528C state block, above the
 * track-init clear. The same slots serve the DirectInput enumeration counter
 * at startup and the City sign physics during a race; the results screen
 * writes them on entry. */
#define s_resultChoice     g_screenResult             /* 0x925418 */
#define s_resultMenuPos    g_stateBlock92528C[128]    /* 0x92548C — current button slot (0..2) */
#define s_resultScrollPos  g_stateBlock92528C[129]    /* 0x925490 — animated highlight X (slides toward Cur) */
#define s_resultScrollCur  g_stateBlock92528C[131]    /* 0x925498 — slide target X */
#define s_resultNumItems   g_stateBlock92528C[133]    /* 0x9254A0 — button-row index (= starting slot, 0..2) */
#define s_resultMaxItem    g_stateBlock92528C[134]    /* 0x9254A4 — min slot clamp for up-nav (= start slot) */

/* Camera/animation state for the podium scene — the same block slots the
 * City sign-physics block B uses during a race. */
#define s_podiumXOff       g_stateBlock92528C[136]   /* 0x9254AC — camera X offset */
#define s_podiumYOff       g_stateBlock92528C[137]   /* 0x9254B0 — camera Y offset */
#define s_podiumZBase      g_stateBlock92528C[138]   /* 0x9254B4 — camera Z base */
#define s_podiumRotA       g_stateBlock92528C[139]   /* 0x9254B8 — character rotation axis A */
#define s_podiumRotB       g_stateBlock92528C[140]   /* 0x9254BC — character rotation axis B */
#define s_podiumRotC       g_stateBlock92528C[141]   /* 0x9254C0 — character rotation axis C */
#define s_podiumBounceA    g_stateBlock92528C[142]   /* 0x9254C4 — bounce velocity A */
#define s_podiumBounceB    g_stateBlock92528C[143]   /* 0x9254C8 — bounce velocity B */
#define s_podiumBounceC    g_stateBlock92528C[144]   /* 0x9254CC — bounce velocity C */

extern unsigned char g_diKeyboardState[];
extern void SetupViewportConfig(void);
extern void ApplyViewportGeometry(void);   /* mode render height + full rebuild */
extern int g_glowPrevChar5, g_glowPrevChar7, g_glowPrevChar8; /* 0x4FBE48/4C/50 */
void platform_pump_events(void);
void RenderBackground(void);                                     /* 0x435868 */
void RelocateModelVertices(int *arr, int count, int newBase,
                           int oldBase, int fileBase);           /* 0x421790 */
void LoadCharacterGouraudTables(void);                           /* 0x476750 */
void TintCharacterGouraudTables(int r, int g, int b);            /* 0x430bdc */
void FindSonicRCD(void);                                         /* 0x4d0f4c */
void CalculateChampionshipPoints(void);                          /* 0x4c43c0 */
void ColorizeTpageHiColor(int r, int g, int b);                  /* 0x488310 */

/* Result button X coordinates — ROM table at 0x503970 (3 rows × 3 dwords).
 * Indexed by [startRow][slot]; startRow = s_resultNumItems = binary
 * [0x9254a0]. Selected-button X is the diagonal: row r → column r.
 * Extracted from PE DGROUP. */
static const int s_resultButtonX[3][3] = {
    {  32, 128, 224 },  /* row 0: REPLAY / RETRY / EXIT — normal results */
    {   0,  80, 176 },  /* row 1: RETRY / EXIT         — non-results screen */
    {   0,   0, 128 },  /* row 2: EXIT only            — multiplayer */
};

/**
 * ResultsScreen — 0x004C71F0 — 4624 bytes
 * Post-race results, podium, championship standings.
 *
 * screenType: 0=GP, 1=TA single, 2=TA multi-lap, 3-6=2P variants
 * Returns: 0=replay, 1=next race, 2=quit, SCREEN_TITLE=back to title
 */
int ResultsScreen(int screenType)
{
    Player *pb = g_playerBase;

    /*
     * Force full-screen viewport
     *
     * Split-screen gameplay leaves g_clipBottom + g_projScaleY at half-screen
     * values, which would cause DrawTexturedQuad to clip away the bottom half
     * of the results screen (LAP 3, total time, REPLAY/RETRY/EXIT buttons).
     * Mirror the post-race exit pattern at main.c:1796-1798 / 1910-1911:
     * temporarily force g_numHumans=1 and re-run SetupViewportConfig so the
     * results UI draws full-screen. Restored on return; every code path
     * leaving here re-runs SetupViewportConfig anyway (LAB_004ce985:1133,
     * main.c:1798/1905/1911) so the original split-screen state is rebuilt
     * for restart / quit-to-title transitions. */
    int savedNumHumans = g_numHumans;
    g_numHumans = 1;
    ApplyViewportGeometry();   /* also restores full render height — results is 480 */

    /* Setup */

    /* Snap the Metal Sonic / Metal Knuckles / Eggman glow polygons back to
     * UV frame base 0xA00000 on every results-screen entry — binary
     * 0x4c72e8-0x4c7378. oldBase==fileBase (both = the prev glow state, init
     * 0x600000) so RelocateModelVertices leaves the pointer fields untouched;
     * the only effect is resetting the animated glow UV frame, which the replay
     * re-sim otherwise leaves on an arbitrary value. Same models / poly offsets
     * / counts as AnimateVehicleGlow (animation.c). */
    {
        RelocateModelVertices((int *)((char *)g_charFaceBase + (g_modelMeta[5].polyStart + 0x30) * 0x30),
                              4, 0xA00000, g_glowPrevChar5, g_glowPrevChar5);  /* 0x4c72e8 */
        RelocateModelVertices((int *)((char *)g_charFaceBase + (g_modelMeta[7].polyStart + 0x1C) * 0x30),
                              4, 0xA00000, g_glowPrevChar7, g_glowPrevChar7);  /* 0x4c7319 */
        RelocateModelVertices((int *)((char *)g_charFaceBase + (g_modelMeta[8].polyStart + 0x38) * 0x30),
                              8, 0xA00000, g_glowPrevChar8, g_glowPrevChar8);  /* 0x4c734b */
    }

    /* Normal finish recomputes championship points; replay-results display
     * restores the real results saved before the replay re-sim clobbered the
     * live arrays — binary 0x4c737d-0x4c73cd. The gate is g_demoMode, NOT
     * g_multiplayerWasActive (prior mistranslation), and the restore branch
     * was previously missing entirely. */
    if (g_demoMode == DEMO_NONE) {                                   /* 0x4c737d cmp [g_demoMode],0 */
        CalculateChampionshipPoints();                               /* 0x4c7386 */
    }
    else {                                                         /* 0x4c7384 jne → restore */
        g_racePointsLaps[0]  = g_replayPointsLaps[0];                /* 0x4c738d */
        g_racePointsLaps[1]  = g_replayPointsLaps[1];                /* 0x4c7397 */
        g_racePointsLaps[2]  = g_replayPointsLaps[2];                /* 0x4c73a1 */
        g_racePointsTotal[0] = g_replayPointsTotal[0];               /* 0x4c73ab */
        pb[0].lapsCompleted = (short)g_replayLapCount;               /* 0x4c73b5 → word 0x8FD552 */
        pb[0].racePosition  = (short)g_replayPlacement;              /* 0x4c73c1 → word 0x8FD550 */
    }

    ResetInputState();                                               /* 0x47059c — 0x4c73cd */

    /* Set fade background colors based on race type — 0x4c73d2-0x4c74be
     * R=0x625c9c, G=0x625ca0, B=0x625ca4 */
    if (g_raceType == RACE_GP) {                                     /* 0x4c73d8: GP mode */
        g_bgTintR = 0xC0;
        g_bgTintG = 0x00;
        g_bgTintB = 0xC0;      /* purple */
    }
    else if (g_raceType == RACE_MULTIPLAYER) {                     /* 0x4c73f8: Multiplayer */
        if (g_raceSubMode == SUBMODE_NORMAL) {                    /* 0x4c73fd, 0x4c7403 */
            g_bgTintR = 0xC0;
            g_bgTintG = 0x00;
            g_bgTintB = 0xC0;  /* purple — 0x4c7407 */
        }
        else {
            g_bgTintR = 0x00;
            g_bgTintG = 0x00;
            g_bgTintB = 0xC0;  /* blue — 0x4c7423 */
        }
    }
    else if (g_raceType == RACE_TIMEATTACK) {                       /* 0x4c7441: Time Attack */
        if (g_raceSubMode == SUBMODE_NORMAL) {                    /* 0x4c744a, 0x4c7450 */
            g_bgTintR = 0x00;
            g_bgTintG = 0xC0;
            g_bgTintB = 0x00;  /* green — 0x4c7454 */
        }
        else if (g_raceSubMode == SUBMODE_REVERSE) {              /* 0x4c746d */
            g_bgTintR = 0xC0;
            g_bgTintG = 0x00;
            g_bgTintB = 0x00;  /* red — 0x4c7472 */
        }
        else if (g_raceSubMode == SUBMODE_TAG) {                  /* 0x4c7487 — cmp edx, esi
                                                                   * where esi still holds
                                                                   * g_raceType == 2 */
            g_bgTintR = 0xC0;
            g_bgTintG = 0xC0;
            g_bgTintB = 0x00;  /* yellow — 0x4c748b */
        }
        else {                                                    /* SUBMODE_BALLOON */
            g_bgTintR = 0x00;
            g_bgTintG = 0x00;
            g_bgTintB = 0xC0;  /* blue — 0x4c74a6 */
        }
    }
    else {
        g_bgTintR = 0xC0;
        g_bgTintG = 0xC0;
        g_bgTintB = 0xC0;      /* grey fallback */
    }

    /* Load textures (D3D path) — 0x4C74BE */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);                 /* 0x4C74D5 */
    TintBackgroundTPage(g_bgTintR, g_bgTintG, g_bgTintB);       /* 0x4C74EB */
    LoadTPageRGB(g_uiTexPage + 1, "BIN/RESULT/RES00.RAW");      /* 0x4C74FB */

    /* Step 14 (D3D path) — per-finish-position env-map overlay
     * (binary 0x4c750e-0x4c75fb). GP-mode only; in non-GP races the
     * binary skips this entire block (jne 0x4c7764 at 0x4c7508). For
     * the human player's racePosition (1..5), patch BIN/EMAP/NO<n>.RAW
     * (128×128) into g_tpageParallax2 at (0,0). NO1=gold, NO2=silver,
     * NO3=bronze, NO4=dim, NO5=darkest — overrides the default
     * SONICR.RAW env-map loaded once at startup
     * (game_loop.c:517 / binary 0x470805) so the trophy and
     * env-mapped character surfaces shimmer in the right medal color
     * for the player's finish position. */
    if (g_raceType == RACE_GP) {                              /* 0x4c7500: GP only */
        int pos = pb[0].racePosition;                    /* binary [0x8fd54e]>>16 → player[0].racePosition */
        const char *emapFile = NULL;
        switch (pos) {
            case 1: emapFile = PATH_EMAP_NO1; break;    /* 0x4c7521: 0x53145c "no1.raw" */
            case 2: emapFile = PATH_EMAP_NO2; break;    /* 0x4c754f: 0x53146d "no2.raw" */
            case 3: emapFile = PATH_EMAP_NO3; break;    /* 0x4c757e: 0x53147e "no3.raw" */
            case 4: emapFile = PATH_EMAP_NO4; break;    /* 0x4c75ad: 0x53148f "no4.raw" */
            case 5: emapFile = PATH_EMAP_NO5; break;    /* 0x4c75e0: 0x5314a0 "no5.raw" */
        }
        if (emapFile != NULL) {
            extern void R_ThawTexture(int tpage);
            R_ThawTexture(g_tpageParallax2);
            LoadTextureSubRect(emapFile, g_tpageParallax2, 0x80, 0x80, 0, 0);
        }
    }

    /* Revive any tpages that CleanupD3DTPages suspended to state 6
     * during the post-race → results transition. Without this, the
     * track scenery tpages used by the track-decoration model (palm
     * tree / sphinx / airplane / etc.) stay suspended, and the
     * per-poly tpage-state check in Draw3DModelD3D silently drops
     * every polygon. Same pattern as screen_charsel.c:115 and
     * screen_timeattack.c:65. */
    for (int tp = 0; tp < 52; tp++) {
        if (g_tpageStateArray[tp] == 6 && g_tpagePixelBuf[tp] != NULL) {
            g_tpageStateArray[tp] = 4;
        }
    }

#ifdef SONICR_DC
    ResumeCD();
#endif

    /* Load character lighting for podium */
    LoadCharacterGouraudTables();                                    /* 0x476750 */
    TintCharacterGouraudTables(0, 0, 0);                             /* 0x430bdc */

    /* Initialize podium camera */
    s_podiumXOff = 0;
    s_podiumYOff = 0x50;
    s_podiumZBase = 0x12C;
    s_podiumRotA = 0;
    s_podiumRotB = 0;
    s_podiumRotC = 0;
    s_podiumBounceA = 0x1C;
    s_podiumBounceB = 0x27;
    s_podiumBounceC = 0x11;

    /* Step 14 — button-row state init (binary 0x4c7963-0x4c79c6).
     * Three-way dispatch on multiplayer / CD availability / screenType.
     * Decides start row (= min visible slot) and the initial highlight X.
     * Independent of the podium pose-setup at 0x4c77e4 (step 10), translated
     * just below as Step 10. */
    {
        int startRow;
        if (g_multiplayerWasActive != 0 || g_cdAvailable == 0) {
            startRow = 2;                             /* EXIT only */
        }
        else if (screenType == 0) {
            startRow = 0;                             /* REPLAY / RETRY / EXIT */
        }
        else {
            startRow = 1;                             /* RETRY / EXIT */
        }
        s_resultNumItems  = startRow;                 /* [0x9254a0] */
        s_resultMaxItem   = startRow;                 /* [0x9254a4] — min-slot clamp */
        s_resultMenuPos   = startRow;                 /* [0x92548c] — selected slot */
        s_resultScrollPos = s_resultButtonX[startRow][startRow];  /* [0x925490] */
        s_resultScrollCur = s_resultButtonX[startRow][startRow];  /* [0x925498] */
        /* NOTE: binary also writes [0x925494] and [0x92549c] here, but those
         * addresses are aliased to g_animState494/49C (city-sign physics) in
         * the port. Omitted to avoid clobbering track anim state. */
    }

    /* Step 10 — per-player podium pose from race result (binary 0x4c77e4-0x4c78e6).
     * Sets animId BEFORE Step 11 seeds the frame stream. Previously dropped
     * (mislabeled "menu-pos dispatch") — without it Step 11 inherits whatever
     * animId the race/replay left, so characters show the wrong pose after a
     * replay early-exit. 6=ANIM_WIN_POSE 7=ANIM_LOSE_POSE 2=ANIM_STILL. */
    {
        Player *p0 = pb;
        if (g_raceType == RACE_TIMEATTACK) {                                       /* 0x4c77e4 Time Attack */
            int tar = g_timeAttackResult;                            /* 0x4c77e9 */
            if (tar != 0) {
                p0->animId = (tar == 2) ? 6 : 7; /* 0x4c77f6/0x4c7804 */
            }
            else if (g_gpBestChanged[0] != 0) {
                p0->animId = 6; /* 0x4c781b */
            }
            else {
                p0->animId = 2; /* 0x4c7829 */
            }
        }
        else if (g_raceType == RACE_GP && g_gpBeatAllFlag != 0) { /* 0x4c783b → 0x4c7856 */
            p0->animId = 6;
        }
        else if (g_raceType != RACE_MULTIPLAYER) {                               /* 0x4c7864 jne → 0x4c7804 */
            p0->animId = 7;             /* raceType 0(beatAll==0) or 3 */
            /* (binary 0x4c784d gpBestChanged/raceType==2 branch is dead here) */
        }
        else
        {                                                            /* raceType==1 multiplayer */
            if (g_raceSubMode == SUBMODE_NORMAL) {                                 /* 0x4c7890 */
                for (int i = 0; i < g_numViewports; i++) {
                    pb[i].animId = (pb[i].racePosition != 1) ? 7 : 6;
                }
            }
            else {                                                    /* 0x4c78c2 */
                for (int i = 0; i < g_numViewports; i++) {
                    pb[i].animId = (pb[i].lapsCompleted != 3) ? 7 : 6;
                }
            }
        }
    }

    /* Step 11 — per-player podium anim setup (binary 0x4c78e6-0x4c7936).
     * For each viewport's character, look up the celebration animation in
     * g_charAnimTables[charId*2][animId] and seed the per-slot frame stream
     * pointer + the initial animFrameIdx countdown. Without this, characters
     * render frozen in whatever pose they had at race end.
     *
     * Same lookup pattern as screen_charsel.c:196-203. Uses g_animDataPtrs[]
     * (animation.c:36) instead of player+0x9C because pointers are 8 bytes on
     * the host and don't fit in the 4-byte _unk_0x9C struct field.
     *
     * Loop bound is g_numViewports ([0x6e9910]) — the audit calls it
     * g_numRacers, but adjacent globals (g_numHumans @ 0x6e9908, g_numPlayers
     * @ 0x6e990c) make g_numViewports the canonical name and the podium loop
     * directly below already uses it. */
    {
        Player *pArr = pb;
        for (int i = 0; i < g_numViewports; i++) {
            int charId = pArr[i].charId;
            int animId = pArr[i].animId;
            void **animTable = (void **)((void ***)g_charAnimTables)[charId * 2];
            if (animTable == NULL) {
                continue;
            }
            const short *fs = (const short *)animTable[animId];
            if (fs == NULL) {
                continue;
            }
            g_animDataPtrs[i]  = fs;
            pArr[i].animFrameIdx = (int)*fs - 1;   /* binary [eax + 0x8fcfc4]
                                                    * after add eax,0x71c =
                                                    * player+0x1EC */
        }
    }

    s_resultChoice = SCREEN_TITLE;  /* binary [0x925418] = 0x3039 sentinel */

    /* Initialize fade */
    g_fadeLevel = (int)0xFFFFFF00;                                   /* fully dark */
    g_fadeState = 1;                                                 /* FADE_IN */
    g_fadeSpeed = 8;
    g_totalFrames = 0;
    g_renderEnabled = 1;

    /* Start results music */
    FindSonicRCD();                                                  /* 0x4d0f4c */

    /* Music track selection */
    UpdateCDPlayback(0x15);                                          /* 0x4d01ac — track 21 */

    DWORD baseTimeSec = timeGetTime() / 1000;

    /* Phase 2: Main loop */
    for (;;) {
        platform_pump_events();

        /* Timer / elapsed */
        DWORD now = timeGetTime();
        g_currentTime = now;       /* WaitForFrameCap on SDL keys off this */
        int elapsed = (int)(now / 1000 - baseTimeSec);
        if (elapsed < 0) {
            elapsed = -elapsed;
        }

        /* Fade update */
        if (g_fadeState != 0) {
            UpdateFade();                                            /* 0x4305d4 */
        }

        /* If fade completed to black → return result */
        if (g_fadeLevel == (int)0xFFFFFF00 && g_totalFrames > 8) {
            StopCD();                                               /* 0x4d0264 */
            g_numHumans = savedNumHumans;       /* see Phase 0 */
            return s_resultChoice;
        }

        /* Read input */
        ReadInput();                                                 /* 0x477228 */

        /* Input handling — binary 0x4c7ad5-0x4c7bd1.
         * All input gated on g_fadeState == 0 (not fading). The confirm
         * and nav paths are additionally gated on the slide being settled
         * (s_resultScrollPos == s_resultScrollCur) so the user can't fire
         * mid-animation between slots. All reads go through g_inputBits
         * (high byte of g_combinedInputState at 0x9020D9). */
        if (g_fadeState == 0) {
            /* Confirm — binary 0x4c7ae3-0x4c7b1d.
             *   test byte [g_inputBits], 6   ; bits 0x0400 (Jump) | 0x0200 (alt)
             * Returns whatever slot is currently highlighted. */
            if ((g_inputBits & 0x06) && s_resultScrollPos == s_resultScrollCur) {
                PlaySoundEffect(2, 0, 0);                          /* 0x482280(2) */
                g_fadeState = 2;                         /* start fade out */
                s_resultChoice = s_resultMenuPos;        /* [0x925418]=[0x92548c] */
            }

            /* Auto-advance timeout — binary 0x4c7b1d-0x4c7b41.
             * esi holds elapsed *seconds* (GetTickCount/1000 - base); the
             * cmp esi, 0xB4 is 180 seconds = 3 minutes, not frames. */
            if (elapsed > 0xB4) {
                PlaySoundEffect(0, 0, 0);                          /* 0x482280(0) */
                g_fadeState = 2;
                s_resultChoice = 2;                      /* force EXIT on timeout */
            }

            /* Left/Right navigation — binary 0x4c7b41-0x4c7bbe.
             * Slots are laid out horizontally; binary tests g_inputBits bits
             * 0x40 (Left) and 0x80 (Right), with a mutual-exclusion test so
             * holding both does nothing. Min clamp is s_resultMaxItem
             * (= startRow); max is always slot 2. */
            if (s_resultScrollPos == s_resultScrollCur) {
                int leftPressed  = (g_inputBits & 0x40) && !(g_inputBits & 0x80);
                int rightPressed = (g_inputBits & 0x80) && !(g_inputBits & 0x40);
                if (leftPressed && s_resultMenuPos > s_resultMaxItem) {
                    s_resultMenuPos--;
                    PlaySoundEffect(1, 0, 0);
                }
                if (rightPressed && s_resultMenuPos < 2) {
                    s_resultMenuPos++;
                    PlaySoundEffect(1, 0, 0);
                }
                /* Update slide target — binary 0x4c7bbe-0x4c7bd1
                 *   scrollTgt = ROM[startRow*12 + slot*4 + 0x503970]    */
                s_resultScrollCur =
                    s_resultButtonX[s_resultNumItems][s_resultMenuPos];
            }
        }

        /* Highlight slide animation — binary 0x4c7bd6-0x4c7c05.
         * Runs every frame regardless of fade state; ±4 toward target. */
        if (s_resultScrollPos < s_resultScrollCur) {
            s_resultScrollPos += 4;
        }
        else if (s_resultScrollPos > s_resultScrollCur) { 
            s_resultScrollPos -= 4;
        }

        /* Per-track animation tick */
        switch (g_trackId) {
            case 1:
                UpdateIslandAnimations();
                break;
            case 2:
                UpdateCityAnimations();
                break;
            case 3:
                UpdateRuinAnimations();
                break;
            case 4:
                UpdateFactoryAnimations();
                break;
            case 5:
                break; /* 0x479700: bare ret (no-op) */
        }

        /* Step 23 — Radiant Emerald iridescent shader phase advance
         * (binary 0x4c7c44-0x4c7c7c). Three vertex-color phase counters
         * used by Draw3DModelD3D's iridescent path (0x00455224 reads
         * [0x712d68], etc.) for the trippy cycling vertex colors on the
         * Radiant Emerald track surface.
         *
         * Audit step 23 originally called these "podium camera angles" —
         * that was wrong. They have NO visible effect on the results
         * screen itself: the Draw3DModelD3D iridescent block is gated on
         * `[eax+0x30] > 0` (per-vertex flag) which is only set on the
         * Emerald track surface, not the results-screen track decoration.
         *
         * The increment is here as a continuity hack: when you bounce
         * race → results → race on Radiant Emerald, the iridescent
         * pattern stays in continuous time instead of snapping back by
         * however many seconds you spent on the results screen. Same
         * constants as main.c:1244-1246. */
        g_emeraldSineOffX = (g_emeraldSineOffX + 0x4D) & 0xFFF;
        g_emeraldSineOffY = (g_emeraldSineOffY + 0xB6) & 0xFFF;
        g_emeraldSineOffZ = (g_emeraldSineOffZ + 0x73) & 0xFFF;

        /* Update podium character rotations (3 oscillating axes) */
        s_podiumBounceA += (s_podiumRotA > 0x800) ? 1 : -1;
        s_podiumRotA = (s_podiumRotA + s_podiumBounceA) & 0xFFF;
        s_podiumBounceB += (s_podiumRotB > 0x800) ? 1 : -1;
        s_podiumRotB = (s_podiumRotB + s_podiumBounceB) & 0xFFF;
        s_podiumBounceC += (s_podiumRotC > 0x800) ? 1 : -1;
        s_podiumRotC = (s_podiumRotC + s_podiumBounceC) & 0xFFF;

        /* Per-character random blink/expression timer (binary 0x47fde8).
         * Canonical translation lives in game_loop.c as UpdateFrameTimers.
         * Used to be a local broken duplicate that left characters stuck
         * in mid-blink expression states — see comment at top of file. */
        UpdateFrameTimers();

        /* Step 26 — per-player anim frame advance (binary 0x4c7d12-0x4c7d91).
         * Walks each viewport's frame stream by one short, handles the -1
         * wrap marker (read loop length K, rewind K shorts, advance by 1).
         * Stores frame & 0xFFF in player+0x1C and (frame & 0xFFF)-1 in
         * player+0x1EC (animFrameIdx).
         *
         * Inlined here because the binary inlines it (no call). The
         * post-state-machine portion of TickPlayerAnimation (animation.c:
         * 244-280) is the same advance/wrap logic minus the trigger-byte and
         * -2 end-marker handling, which the binary's results-screen path also
         * omits.
         *
         * Audit's "0xFC4" claim was wrong: the compiler folds add eax,0x71c
         * into the displacement, so [eax + 0x8fcfc4] post-increment writes
         * to player+0x1EC (animFrameIdx), not player+0xFC4. */
        {
            Player *pArr = pb;
            for (int i = 0; i < g_numViewports; i++) {
                const short *p = g_animDataPtrs[i];
                if (p == NULL) {
                    continue;
                }
                int frame = (int)*p++;                           /* 0x4c7d2d-0x4c7d3c */
                if (frame == -1) {                               /* 0x4c7d45 wrap */
                    int k = (int)*p;                             /* 0x4c7d4a-0x4c7d50 */
                    p -= k;                                      /* 0x4c7d53-0x4c7d57: sub by 2*k bytes = k shorts */
                    frame = (int)*p++;                           /* 0x4c7d5d-0x4c7d69 */
                }
                g_animDataPtrs[i]   = p;
                pArr[i]._unk_0x1C     = frame & 0xFFF;           /* 0x4c7d80 (post-inc → player+0x1C) */
                pArr[i].animFrameIdx  = (frame & 0xFFF) - 1;     /* 0x4c7d87 (post-inc → player+0x1EC) */
            }
        }

        /* Render frame (D3D path) */
        //if (g_renderMode == RENDER_D3D) 
        {
            /* Clear sprite state and begin */
            g_polyCount = 0;
            ProcessTpageStates();
            BeginFrame();

            /* Wavy tinted background — binary composites via software renderer;
             * GL port: use RenderBackground which draws the ColorizeTpageHiColor-
             * tinted g_uiTexPage with sine wobble, matching other menu screens. */
            RenderBackground();

            /* NOTE: the binary's Path-A "3D podium scene" call at 0x4c7e3b
             * (FUN_004671b8) is the *software* renderer's env-map model function.
             * Path B calls the D3D version at 0x4c8167 (FUN_00468744 =
             * RenderEnvMappedModel3D in title_render.c), which is already what
             * we invoke below for the trophy. Don't re-translate 0x4671b8. */

            /* Character models on podium.
             * Binary at 0x4c7e79-0x4c7f3a: per-viewport position from ROM
             * table at 0x503b68, rotation = (g_totalFrames & 0x1FF) << 3,
             * alternating direction for odd/even viewports.
             * Per-char Y adjust: charID 9+model6 → -0x4B, charID 4 → -0x5A. */
            {
                /* ROM table: 3 ints per viewport (xOff, yOff, zOffset),
                 * indexed by (numViewports-1)*4 + viewportIdx.
                 * From DGROUP at 0x503b68. */
                static const int s_podiumPos[][3] = {
                    /* numViewports=1 */
                    {-144, 96, 384},
                    {0,0,0}, {0,0,0}, {0,0,0},
                    /* numViewports=2 */
                    {-144, 96, 384}, {144, 96, 384},
                    {0,0,0}, {0,0,0},
                    /* numViewports=3 */
                    {-200, 150, 512}, {200, 150, 512},
                    {-200, -30, 512}, {0,0,0},
                    /* numViewports=4 */
                    {-200, 150, 512}, {200, 150, 512},
                    {-200, -30, 512}, {200, -30, 512},
                };

                int charRot = (g_totalFrames & 0x1FF) << 3;  /* 0x4c7d9f-0x4c7da9 */

                Player *pArr = pb;
                for (int p = 0; p < g_numViewports; p++) {
                    Player *player = &pArr[p];

                    int posIdx = (g_numViewports - 1) * 4 + p;
                    int pxOff = s_podiumPos[posIdx][0];
                    int pyOff = s_podiumPos[posIdx][1];
                    int pzOff = s_podiumPos[posIdx][2];

                    /* Per-character Y adjustment (0x4c7eea-0x4c7f1d) */
                    short charId = player->charId;
                    if (charId == CHAR_SUPER_SONIC) {
                        int modelVal = player->animId;           /* P_INT(0x96)>>16 = animId at 0x98 */
                        if (modelVal == 6) {
                            pyOff -= 0x4B;
                        }
                    }
                    else if (charId == CHAR_EGGMAN) {
                        pyOff -= 0x5A;
                    }

                    /* Odd viewports face opposite direction (0x4c7ea3-0x4c7eb2) */
                    int rot = (p & 1) ? (0xFFF - charRot) : charRot;

                    UpdateVertexLighting(player);              /* 0x430638 — binary calls before each character */
                    RenderCharacterOnPodium(pxOff, pyOff, pzOff,
                                            0xF00, rot, 0, player);

                    /* 3D collectible objects around character — 0x4c7f3f-0x4c7f53 */
                    if (g_raceSubMode == SUBMODE_BALLOON) {
                        DrawResultsBalloonsForPlayer(pxOff, pyOff, pzOff, p);
                    }
                }
            }

            /* Track decoration model — binary 0x4c8354..0x4c83ad.
             * Renders one Draw3DModelD3D call with a per-track object taken
             * from g_objectStructArray, positioned per viewport count,
             * scaled per track. Three ROM tables drive this:
             *
             *   0x503c28 (5 shorts, tracks 1..5) — scale
             *   0x503c32 (5 shorts, tracks 1..5) — object index into
             *                                      g_objectStructArray
             *   0x503c3c (12 shorts, 4 × 3)     — (x, y, z) per viewport
             *                                      count (nv=1..4)
             *
             * Binary's indexing via `[ebp + nv*6 - 0x8a]` reads DWORDs and
             * extracts high shorts — effectively `position[nv-1]` for nv>=2.
             * For nv=1 the binary reads slightly out-of-bounds; we collapse
             * to using `position[0]` directly (= the first ROM triplet),
             * which is clearly the intended value.
             *
             * Rotation is [ebp-0x40], written once at 0x4c8251 as
             *   0xFFF - [ebp-0x44]
             * where [ebp-0x44] is the char podium rotation
             *   (g_totalFrames & 0x1ff) << 3 (0x4c7d9f-0x4c7db2).
             * Decoration rotates opposite direction to the character at
             * identical angular speed. */
            if (g_trackId >= TRACK_RESORT_ISLAND && g_trackId <= TRACK_RADIANT_EMERALD) {
                /* Audit step 2 mislabeled these. Binary decode at
                 * 0x4c8354-0x4c836a proves 0x503c28's values are used as
                 * object-array indices (×0x44) and 0x503c32's values are the
                 * scale divisors. The audit's names were swapped; here we
                 * correct the usage but keep the existing variable names so
                 * the rest of the block reads naturally.
                 *
                 * Ruin/Factory swap applied: both tables are in the same
                 * "0x4FEB00 family" as s_trackConfigTable (player_init.c) —
                 * ROM positions [2] and [3] are reversed vs natural trackId
                 * ordering. Without the swap, Factory reads Ruin's decoration
                 * model (864) and Ruin reads Factory's (946). See
                 * project_trackid_swap.md. */
                static const int s_trackDecoObjIdx[5] =
                    { 596, 1006, 864, 946, 848 };            /* ROM 0x503c28, [2]↔[3] swap */
                static const int s_trackDecoScale[5] =
                    { 7, 12, 4, 5, 10 };                      /* ROM 0x503c32, [2]↔[3] swap */
                static const int s_trackDecoPos[4][3] = {    /* ROM 0x503c3c */
                    { 128,  50, 352 },                        /* nv=1 */
                    {   0,   0, 352 },                        /* nv=2 */
                    { 128, -65, 352 },                        /* nv=3 */
                    {   0,   0, 352 },                        /* nv=4 */
                };
                int nvIdx = g_numViewports - 1;
                if (nvIdx < 0) {
                    nvIdx = 0;
                }
                if (nvIdx > 3) {
                    nvIdx = 3;
                }
                const int trackIdx0 = g_trackId - 1;          /* tracks are 1-indexed */
                const int xOff = s_trackDecoPos[nvIdx][0];
                const int yOff = s_trackDecoPos[nvIdx][1];
                const int zBase = s_trackDecoPos[nvIdx][2];
                const int objByteOff = s_trackDecoObjIdx[trackIdx0] * 0x44;
                intptr_t objPtr = (intptr_t)((char *)g_objectStructArray + objByteOff);
                const int rotation = 0xFFF - ((g_totalFrames & 0x1ff) << 3);  /* 0x4c8251: 0xFFF - charRot */
                const int scale = s_trackDecoScale[trackIdx0];
                Draw3DModelD3D(xOff, yOff, zBase, 0xf00,
                               rotation, 0, objPtr, scale);
            }

            /* All geometry now drawn immediately — no batch flush needed. */

            /* Ghost/record indicator — binary 0x4c8103-0x4c8162
             * Only for raceType==2 AND raceType > raceSubMode AND timeAttackResult != 0
             * Blinking: (g_totalFrames & 0xf) < 8 */
            if (g_raceType == RACE_TIMEATTACK && g_raceSubMode < SUBMODE_TAG && g_timeAttackResult != 0) {
                if ((g_totalFrames & 0xf) < 8) {
                    int ghostTexV = (g_timeAttackResult - 1) * 14 + 0x18;
                    DrawTexturedQuad(0xb8, 8, 0x43FA0000, 0x110, 0x1c,
                                     g_uiTexPage + 1, 0x78, ghostTexV, 0x88, 0xe,
                                     VERTEX_WHITE);
                }
            }

            /* Env-mapped podium trophy — binary 0x4c8167-0x4c81a0, GP only.
             * RenderEnvMappedModel3D (0x468744) is the generic env-mapped model
             * renderer; the title screen's "R" is model 0 down the same path. */
            if (g_raceType == RACE_GP) {
                /* Call site maps:
                 *   EAX..ECX = s_podiumXOff/YOff/ZBase/RotA     (0x9254ac..0x9254b8)
                 *   stack1   = s_podiumRotB                     (0x9254bc)
                 *   stack2   = s_podiumRotC                     (0x9254c0)
                 *   stack3   = [0x8fd54e] >> 16 = (short)g_racePlacement,
                 *              a macro alias for player[0].racePosition
                 *   stack4   = 0 (binary reuses ebx which holds g_raceType,
                 *                 known to be 0 because of the guard above) */
                const int modelIdx = g_playerBase[0].racePosition;
                RenderEnvMappedModel3D(s_podiumXOff, s_podiumYOff, s_podiumZBase,
                                       s_podiumRotA, s_podiumRotB, s_podiumRotC,
                                       modelIdx, 0);
            }

            /* Selected button highlight — binary 0x4c81a5-0x4c81d4
             * Position from s_resultScrollPos, always rendered */
            {
                int highlightX = (s_resultScrollPos - 4) * 2;
                DrawTexturedQuad(highlightX, 0x170, 0x433E0000, 0x90, 0x50,
                                 g_uiTexPage + 1, 0, 0x18, 0x48, 0x28,
                                 VERTEX_WHITE);
            }

            /* Button menu items loop — binary 0x4c8009-0x4c8082.
             * Draws REPLAY/RETRY/EXIT sprites from RES00.RAW. Walks slots
             * [startRow..2]; X comes from s_resultButtonX[startRow][slot]
             * (ROM 0x503970), texture U starts at startRow*0x40 and advances
             * by 0x40 per slot. */
            {
                const int startRow = s_resultNumItems;
                int texU = startRow * 0x40;
                for (int slot = startRow; slot < 3; slot++) {
                    int btnX = s_resultButtonX[startRow][slot] * 2;
                    DrawTexturedQuad(btnX, 0x188, 0x43480000, 0x80, 0x30,
                                     g_uiTexPage + 1, texU, 0, 0x40, 0x18,
                                     VERTEX_WHITE);
                    texU += 0x40;
                }
            }

            /* Text overlays */
            DrawResultsOverlay(screenType);

            /* Iris/iris fade overlay — binary 0x4c80f0-0x4c80f9
             * (cmp g_fadeLevel,0 / jge / call 0x461df4). Dropped in
             * translation, so the results iris never drew: the fade state
             * advanced but invisibly (screen cut instead of iris closing). */
            if (g_fadeLevel < 0) {
                RenderFadeOverlay();
            }

            /* End scene + flip */
            RenderWavingMenuBackground();                     /* 0x004C68B8 — software twin */
            EndFrame();
            FlipD3D();
        }

        /* Frame counters */
        g_totalFrames2++;
        g_totalFrames++;

        /* Frame rate limiter — see WaitForFrameCap. */
        WaitForFrameCap();
    }
}