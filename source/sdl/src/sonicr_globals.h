/**
 * sonicr_globals.h — All global variable declarations
 *
 * Generated from sonicr_annotated.c analysis.
 * Every global is declared here with its original address in a comment.
 * In the final build, these will be defined in sonicr_globals.c.
 */

#ifndef SONICR_GLOBALS_H
#define SONICR_GLOBALS_H

#include "sonicr_types.h"
#include "player_struct.h"
#include "vertex_struct.h"
#include "collect_effect.h"
#include "fileio.h"

extern FILE *g_fileHandle;                  /* 0x00625bfc */
extern unsigned char g_bgTintR;             /* 0x00625C9C */
extern unsigned char g_bgTintG;             /* 0x00625CA0 */
extern unsigned char g_bgTintB;             /* 0x00625CA4 */
extern unsigned char *g_tpageSurfacePtrs[]; /* 0x00625CC0 */


/* =====================================================================
 * DirectX Objects
 * ===================================================================== */
extern void        *g_lpD3DDevice;          /* 0x006300FC — IDirect3DDevice2* */
extern void        *g_lpDD;                 /* 0x00630104 — IDirectDraw* */
extern void        *g_lpFrontBuffer;        /* 0x0063010C — IDirectDrawSurface* */
extern void        *g_lpBackBuffer;         /* 0x00630114 — IDirectDrawSurface* */
extern void        *g_lpRenderSurface;      /* 0x00630110 — IDirectDrawSurface* */
extern float g_uvLUT256[];                  /* 0x0063fcdc */

/* Seam inset baked into g_uvLUT256 by InitUVLUT, in normalized UV.
 *
 * The table maps an inclusive texel index to a polygon edge: an even index is
 * a low edge addressing texel i, an odd index is a high edge addressing texel
 * i+1. The inset pulls each edge that much toward the polygon interior.
 *
 * The binary's 0.0005 is 0.128 texel of a 256-wide page — just enough to stop
 * float rounding landing on the integer boundary. Under nearest that floors to
 * the intended texel and nothing else about it matters.
 *
 * DC filters most pages bilinear, and PVR puts texel centres at k+0.5, so a
 * 0.128 inset taps the NEIGHBOURING texel at weight 0.5 - 0.128 = 0.372. The
 * pages are gutter-less atlases — RUIN00.RAW packs sandstone directly against
 * grey block against the wood door — so that neighbour is a foreign
 * sub-texture and every polygon edge picks up 37% of it. Insetting a full half
 * texel lands each edge exactly on a texel centre: zero bleed, and identical
 * under nearest, since i+0.5 still floors to i either way.
 *
 * Cost is an (n-1)/n content scale across an n-texel sub-rect — 3% on a 32.
 * The real fix is per-texture pages or a duplicated-edge gutter in the atlas;
 * this is the cheap stand-in until then.
 *
 * ---------------------------------------------------------------------------
 * UV_TEXEL_CENTRE puts EVERY LUT consumer on the texel-centre form — the same
 * math TRACK_UV_LEGACY already gives track surfaces. At a half-texel bias the
 * table's parity term collapses: even i/256 + b and odd (i+1)/256 - b both
 * become (i + 0.5)/256, so the table degenerates to a plain texel-centre
 * lookup and TRACK_UV_LEGACY becomes a no-op.
 *
 * Why that can fix a MENU SEAM: the parity form is index-parity sensitive.
 * Sites that address a span as base and base+N-1 (hud_full.c:395,
 * render_char_sprites.c:137 `g_uvLUT256[uvA + 47]`, weather.c) only get the
 * full span when the high index lands ODD. Flip the base's parity and the high
 * edge is treated as a LOW edge, so the sprite renders one texel short and the
 * neighbouring atlas content shows along that edge. The centre form has no
 * parity term, so every span comes out right regardless of base alignment.
 *
 * Cost is the same 3% shrink, now applied to characters, HUD and sprites too.
 *
 *   UV_TEXEL_CENTRE 0 + TRACK_UV_LEGACY 1 — track centre, rest binary (known good)
 *   UV_TEXEL_CENTRE 0 + TRACK_UV_LEGACY 0 — fully faithful to the binary
 *   UV_TEXEL_CENTRE 1                     — centre everywhere (this experiment)
 * ------------------------------------------------------------------------- */
#define UV_TEXEL_CENTRE  1

#if defined(BLURRY) || UV_TEXEL_CENTRE
#define UV_LUT_BIAS  (0.5f / 256.0f)        /* half texel — parity-free centre */
#else
#define UV_LUT_BIAS  0.0005f                /* 0x52C2xx — binary's 0.128 texel */
#endif

extern int          g_lightingRamp8bit[];   /* 0x006725CC */

extern unsigned char g_diKeyboardState[256];/* 0x006758CC */
extern unsigned short g_p1ButtonState;      /* 0x00675894 */
extern unsigned short g_p2ButtonState;      /* 0x00675896 */
extern unsigned short g_joySlotState[5];    /* 0x00675898..0x006758A0 — per-joystick button states */

/* =====================================================================
 * Input Mapping
 * ===================================================================== */
extern int          g_keyMappingData[20];   /* 0x00676084..0x006760D0 — 10 keys × 2 players.
                                              * Slot order is fixed by the binary's PollAllInputDevices
                                              * (0x004769B0): each slot index maps to one bit in the
                                              * 16-bit player input word. */
#define g_keyMap_P1_Start     g_keyMappingData[0]   /* 0x00676084 — bit 0x0800 */
#define g_keyMap_P1_Left      g_keyMappingData[1]   /* 0x00676088 — bit 0x4000 */
#define g_keyMap_P1_Right     g_keyMappingData[2]   /* 0x0067608C — bit 0x8000 */
#define g_keyMap_P1_Up        g_keyMappingData[3]   /* 0x00676090 — bit 0x1000 */
#define g_keyMap_P1_Down      g_keyMappingData[4]   /* 0x00676094 — bit 0x2000 */
#define g_keyMap_P1_Jump      g_keyMappingData[5]   /* 0x00676098 — bit 0x0400 */
#define g_keyMap_P1_Accel     g_keyMappingData[6]   /* 0x0067609C — bit 0x0100 */
#define g_keyMap_P1_DriftL    g_keyMappingData[7]   /* 0x006760A0 — bit 0x0008 */
#define g_keyMap_P1_DriftR    g_keyMappingData[8]   /* 0x006760A4 — bit 0x0080 */
#define g_keyMap_P1_LookBack  g_keyMappingData[9]   /* 0x006760A8 — bit 0x0040 */
#define g_keyMap_P2_Start     g_keyMappingData[10]  /* 0x006760AC — bit 0x0800 */
#define g_keyMap_P2_Left      g_keyMappingData[11]  /* 0x006760B0 — bit 0x4000 */
#define g_keyMap_P2_Right     g_keyMappingData[12]  /* 0x006760B4 — bit 0x8000 */
#define g_keyMap_P2_Up        g_keyMappingData[13]  /* 0x006760B8 — bit 0x1000 */
#define g_keyMap_P2_Down      g_keyMappingData[14]  /* 0x006760BC — bit 0x2000 */
#define g_keyMap_P2_Jump      g_keyMappingData[15]  /* 0x006760C0 — bit 0x0400 */
#define g_keyMap_P2_Accel     g_keyMappingData[16]  /* 0x006760C4 — bit 0x0100 */
#define g_keyMap_P2_DriftL    g_keyMappingData[17]  /* 0x006760C8 — bit 0x0008 */
#define g_keyMap_P2_DriftR    g_keyMappingData[18]  /* 0x006760CC — bit 0x0080 */
#define g_keyMap_P2_LookBack  g_keyMappingData[19]  /* 0x006760D0 — bit 0x0040 */

extern int g_factoryObjState[];             /* 0x0068158c */

/* =====================================================================
 * Network
 * ===================================================================== */
extern int g_netFrameCounter;               /* 0x00689b58 */
extern int g_netPlayerAlive[];              /* 0x00689B9C — per-player keepalive flags [4] */
extern int g_netDisconnectFlag;             /* 0x00689bb0 */
extern int g_netServiceProviders[];         /* 0x0068a4e0 */
extern int g_netFilteredProviders[];        /* 0x0068a6ec */
extern int g_portraitTextBuffer[];          /* 0x0068a6f0 */
/* Lobby config broadcast payload, 48 bytes (0x0068a89c..0x0068a8cb), sent whole
 * by UpdateNetworkSync(g_netGameInfoDest, 0x30). [11] carries the MODE A/B bit
 * in its byte 2 (0x0068a8ca) — reach it through net_lobby_mode_b(). */
extern int g_netGameInfoDest[];             /* 0x0068a89c */
extern short g_netTrackIndex;               /* 0x0068a8a0 */
extern short g_netRaceSubModeIndex;         /* 0x0068a8a2 */
extern int g_netWeatherType;                /* 0x0068a8a4 */
extern int g_netPlayerMode;                 /* 0x0068a8a6 */
extern int g_netLobbyConfigBuf[];           /* 0x0068a8cc */
extern int g_netReceivedLobbyData;          /* 0x0068a8fc */
extern HANDLE       g_netRenderEvent;       /* 0x0068AC78 */
extern HANDLE       g_netRecvEvent;         /* 0x0068AC74 */
extern HANDLE       g_netSendEvent;         /* 0x0068AC7C */
extern int          g_currentPlayerIdx;     /* 0x0068ACD8 — local player DPID from IDirectPlay4::CreatePlayer */
extern int          g_localPlayerIndex;     /* 0x0068ACDC */
extern int          g_netGameStarted;       /* 0x0068ACE4 */
extern int          g_gameDataPacketHeader; /* 0x0068AEC8 */
extern unsigned short g_gameDataPacketFrame;/* 0x0068AECC */
extern unsigned short g_netRecvInput[];     /* 0x0068AECE */
extern int          g_netPlayerCount;       /* 0x0068AEE8 */
extern int          g_netSessionActive;        /* 0x0068AF18 — DP session live (transient); see block comment above */
extern int g_menuState;                     /* 0x0068afc8 */
extern int g_netCharSelectState;            /* 0x0068afd0 */
extern int g_resultsState;                  /* 0x0068afd4 */
extern int g_resultsPlayerCount;            /* 0x0068afdc */
extern int g_resultsTextLineCount;          /* 0x0068afd8 */

extern int g_floatMtxDest[];                /* 0x0068b280 */




/* =====================================================================
 * Window / Instance
 * ===================================================================== */
extern HACCEL       g_hAccel;                /* 0x006D987C */
extern HINSTANCE    g_hInstance;             /* 0x006D9880 */
extern HINSTANCE    g_hPrevInstance;         /* 0x006D9884 */
extern LPSTR        g_lpCmdLine;             /* 0x006D9888 */
extern int          g_nCmdShow;              /* 0x006D988C */
extern HWND         g_hWnd;                  /* 0x006D9890 */
extern HINSTANCE    g_hInstance_dinput;      /* 0x006D9900 */
extern int          g_surfaceLost;           /* 0x006D9908 */
extern HWND         g_hWndGame;              /* 0x006D9998 */
extern HINSTANCE    g_hInstance_dplay;       /* 0x006D99AC */
extern int          g_renderPass;            /* 0x006D9A4C */

/* =====================================================================
 * Render Mode & Display
 * ===================================================================== */
extern int          g_renderMode;           /* 0x006DD860 — RENDER_D3D(1) or RENDER_SOFT(2) */
extern int          g_bitsPerPixel;         /* 0x006E98B4 — 8, 15, or 16 */
extern int          g_screenWidth;          /* 0x006E9898 */
extern int          g_screenHeight;         /* 0x006E989C */
extern int          g_screenWidthFull;      /* 0x006E98DC */
extern int          g_screenHeightFull;     /* 0x006E98E0 */
extern int          g_screenCenterX;        /* 0x006E98D4 */
extern int          g_screenCenterY;        /* 0x006E98D8 */
extern int          g_projScaleX;           /* 0x006E98EC */
extern int          g_projScaleY;           /* 0x006E98F4 */
extern int          g_projScaleXCurrent;    /* 0x006E98F0 */
extern int          g_clipLeft;             /* 0x006E98BC */
extern int          g_clipRight;            /* 0x006E98C0 */
extern int          g_clipTop;              /* 0x006E98CC */
extern int          g_clipBottom;           /* 0x006E98D0 */
extern int          g_screenScale;          /* 0x006E98B8 — g_screenWidth/320 resolution scale */
extern int          g_renderEnabled;        /* 0x008FB81C */
extern int          g_surfaceStride;        /* 0x006E9890 — pixels per row incl. padding */


/* =====================================================================
 * Frame Timing
 * ===================================================================== */
extern DWORD        g_currentTime;          /* 0x006E9D00 */
extern int          g_currentFPS;           /* 0x006E9CDC */
extern int          g_totalFrames;          /* 0x008FB68C */
extern int          g_totalFrames2;         /* 0x008FB690 */
extern int          g_frameSkip;            /* 0x008FB80C */
extern int          g_skipThisFrame;        /* 0x006E9D08 */

/* =====================================================================
 * Polygon Sort & Software Framebuffer
 * ===================================================================== */
extern int          g_polyCount;            /* 0x008FB358 */
extern int          g_maxPolyCount;         /* 0x008F7350 */

/* =====================================================================
 * Game State
 * ===================================================================== */
extern int          g_demoMode;             /* 0x008FB8E4 — DEMO_NONE/DEMO_TITLE/DEMO_REPLAY */
extern int          g_trackId;              /* 0x008FB8EC — 1-5 */
enum {
    RACE_GP           = 0,
    RACE_MULTIPLAYER  = 1,
    RACE_TIMEATTACK   = 2,
    RACE_SPECIAL      = 3,
    RACE_CHAMPIONSHIP = 4,
};
extern int          g_raceType;             /* 0x008FB950 */
extern int          g_raceSubMode;          /* 0x008FB954 — SUBMODE_NORMAL..SUBMODE_BALLOON */
/* ============================================================================
 * NETWORK GAME FLAGS — READ THIS BEFORE USING EITHER FLAG
 * ============================================================================
 *
 * g_netSessionActive and g_isNetworkGame are BOTH network-game flags — neither
 * has anything to do with local 2P split-screen multi (that is gated by
 * g_raceType == RACE_TYPE_MULTIPLAYER / g_isMultiRace, NOT by these flags).
 *
 * The two flags exist because they track DIFFERENT STATES of the network
 * connection lifecycle. They are NOT aliases — do not substitute one for
 * the other based on "they're always equal" intuition, because they diverge
 * transiently at well-defined moments.
 *
 *   g_netSessionActive  ←  "a DirectPlay session is currently live"
 *     SET by DP session-established callbacks at 0x487c53 (host side) and
 *     0x4d9494 (client side) — these can fire DURING NetworkScreen's event
 *     loop, before the user has committed to a race. Also set alongside
 *     g_isNetworkGame at WinMain 0x4ce785 after NetworkScreen returns OK.
 *     CLEARED aggressively at nearly every path boundary: LoadGameState
 *     (0x4cdb94), top of WinMain loop (0x4ce4eb), after single-player course
 *     select (0x4ce95c), post-race teardown (0x4cf9b6), NetworkScreen entry
 *     (0x48a8d6), etc. Treat as a transient session-liveness flag.
 *
 *   g_isNetworkGame  ←  "we have committed to playing a network race"
 *     SET only at WinMain 0x4ce790, after NetworkScreen returns success and
 *     we are building the race. CLEARED only at explicit teardown points
 *     (0x4ceefd paired teardown, 0x4cfa76 post-race). Treat as a sticky
 *     mode flag that persists across the race.
 *
 * The two flags diverge in well-known windows, and these windows matter:
 *   (a) During NetworkScreen's event loop, after a DP callback fires but
 *       before NetworkScreen returns: g_netSessionActive=1, g_isNetworkGame=0.
 *   (b) During post-race teardown between 0x4cf9b6 and 0x4cfa76:
 *       g_netSessionActive=0, g_isNetworkGame=1 (briefly).
 *
 * Many sites OR both flags together (`g_netSessionActive || g_isNetworkGame`)
 * to catch "any form of network game, at any point in its lifecycle". That
 * pattern is correct — don't simplify it to a single flag.
 *
 * When TRANSLATING from binary: the original writes [0x68af18] for
 * g_netSessionActive and [0x6d9a44] for g_isNetworkGame. Several file-local
 * extern declarations were historically mis-labelled with swapped addresses
 * (e.g. a line reading "extern int g_isNetworkGame; // 0x68AF18") — the
 * name resolves correctly by linkage to the globals.c definition, but the
 * LOGIC in those sites tested the wrong variable. If you see a new site
 * testing only one of these flags, verify against the binary which address
 * is actually being read.
 * ============================================================================ */
extern int          g_isNetworkGame;        /* 0x006D9A44 — committed to network race (sticky); see block comment above */
extern int          g_numPlayers;           /* 0x006E990C — 1-5 */
extern int          g_numHumans;            /* 0x006E9908 — 1-4 */
extern int          g_numViewports;         /* 0x006E9910 — 1-4 */
extern int          g_mirrorMode;           /* 0x006E9920 */

/* =====================================================================
 * Save Data Block — contiguous 2216-byte (554 int) block at 0x8FBA4C.
 * The binary saves/loads this as a single blob. All named fields below
 * are #define accessors into g_saveBlock to preserve the binary's
 * intentional aliasing (e.g. charUnlockTable[4] == trackUnlockState).
 * ===================================================================== */
extern int          g_saveBlock[554];       /* 0x008FBA4C — canonical save data */
#define g_gpTrackStatus      g_saveBlock          /* 0x8FBA4C — GP track/char status, [20] */
#define g_aiBlock            g_saveBlock          /* 0x8FBA4C — AI state block alias */
#define g_gpAllTracksFlag    g_saveBlock[5]       /* 0x8FBA60 — all 4 tracks won */

/* Character/race availability (0x8FBA50-0x8FBA60) */
#define g_charRaceState      (&g_saveBlock[1])   /* 0x8FBA50, [4] per-track avail */
#define g_allCharsRaceUnlock g_saveBlock[5]       /* 0x8FBA60 */

/* Character-select ROM data (0x502648) — ONE region under two bases.
 * g_charSelViewportPos starts six ints inside g_aiCharAssignment, so
 * ai[6..9] and vp[0..3] are the same memory. The AI assignment loop really
 * does read that far when nothing is unlocked — see globals_extra.c. */
extern int g_charSelDataBlock[];                    /* 0x00502648, [26] */
#define g_aiCharAssignment   (&g_charSelDataBlock[0])  /* 0x502648, [10] */
#define g_charSelViewportPos (&g_charSelDataBlock[6])  /* 0x502660, [20] */

/* Character unlock table (0x8FBA64-0x8FBA88) — 10 entries, chars 0-9.
 * Entries [4]-[9] intentionally alias gpState/collect/titleCam below. */
#define g_charUnlockTable    (&g_saveBlock[6])    /* 0x8FBA64, [10] */

/* GP / collectible state (0x8FBA74-0x8FBA94) — aliases charUnlockTable[4+] */
#define g_gpStateBlock       (&g_saveBlock[10])   /* 0x8FBA74, [8] */

/* g_trackUnlockState and g_gpRelayFlag used to name g_saveBlock[10] here.
 * Both were WRITE-ONLY — nothing ever read the slot under either name, and
 * both sound like track/relay state when 0x8FBA74 is in fact
 * g_charUnlockTable[4]: Eggman's unlock flag. 1 = locked (fresh save),
 * 2 = unlocked, set on winning Radiant Emerald (race_setup.c, binary
 * 0x4c48ac). Its only reader is the char-select skip-locked cursor
 * (`g_charUnlockTable[n] != 2`, binary 0x48d302). Both writers now say
 * g_charUnlockTable[CHAR_EGGMAN]; do not reintroduce the old names. */
/* Gates the title-screen logo spin, and so mirror mode. Tested as
 * dword [0x8FBA4C] == 1 at 0x4dd5fe. Same slot as g_gpTrackStatus[0]. */
#define g_titleLogoEnabled    g_saveBlock[0]       /* 0x8FBA4C */
#define g_collectUnlock      (&g_saveBlock[12])   /* 0x8FBA7C = charUnlockTable[6], [5] */
#define g_allCharsUnlocked   g_saveBlock[15]      /* 0x8FBA88 = charUnlockTable[9] */

/* Track collect / char unlock state (0x8FBA8C) — aliased */
#define g_charUnlockState    (&g_saveBlock[16])   /* 0x8FBA8C, [7] */
#define g_trackCollectState  (&g_saveBlock[16])   /* 0x8FBA8C, [6] — alias */

/* Per-track time tables (0x8FBAA4-0x8FBB44) */
#define g_lapTimeA           (&g_saveBlock[22])   /* 0x8FBAA4, [6] */
#define g_lapTimeB           (&g_saveBlock[23])   /* 0x8FBAA8, [6] — overlaps lapTimeA */
#define g_lapTimeC           (&g_saveBlock[28])   /* 0x8FBABC, [6] */
#define g_lapTimeTA0         (&g_saveBlock[32])   /* 0x8FBACC, [6] */
#define g_lapTimeD           (&g_saveBlock[33])   /* 0x8FBAD0, [6] */
#define g_lapTimeTA1         (&g_saveBlock[38])   /* 0x8FBAE4, [6] */
#define g_lapTimeTAHalf0     (&g_saveBlock[43])   /* 0x8FBAF8, [6] */
#define g_lapTimeTAHalf1     (&g_saveBlock[48])   /* 0x8FBB0C, [6] */
#define g_lapTimeEmerald     (&g_saveBlock[53])   /* 0x8FBB20, [6] */
#define g_lapTime5Lap        (&g_saveBlock[58])   /* 0x8FBB34, [6] */

/* Same eight tables under the names CalculateChampionshipPoints uses, where
 * they are indexed by trackId-1 rather than trackId. Binary 0x8FBAA8-0x8FBB34
 * is inside the save block, so these must share storage with the g_lapTime*
 * views above — InitDefaultTimeTables seeds them and the scoring pass compares
 * against them. */
#define g_gpTrackBestA       (&g_saveBlock[23])   /* 0x8FBAA8 — = g_lapTimeB */
#define g_gpTrackBestB       (&g_saveBlock[28])   /* 0x8FBABC — = g_lapTimeC */
#define g_gpTrackBestC       (&g_saveBlock[33])   /* 0x8FBAD0 — = g_lapTimeD */
#define g_gpTrackBestD       (&g_saveBlock[38])   /* 0x8FBAE4 — = g_lapTimeTA1 */
#define g_gpTrackBestE       (&g_saveBlock[43])   /* 0x8FBAF8 — = g_lapTimeTAHalf0 */
#define g_gpTrackBestF       (&g_saveBlock[48])   /* 0x8FBB0C — = g_lapTimeTAHalf1 */
#define g_gpTrackBestG       (&g_saveBlock[53])   /* 0x8FBB20 — = g_lapTimeEmerald */
#define g_gpTrackBestH       (&g_saveBlock[58])   /* 0x8FBB34 — = g_lapTime5Lap */

/* Per-player championship standings (0x8FBB88), capped at 99. Binary users are
 * CalculateChampionshipPoints (0x4C455E/456C/462D/463A) and both
 * DrawResultChampPoints twins (0x4C55EB, 0x4C5F61) — not "best times".
 * Was also declared as a standalone int[4] in globals_extra.c, which split the
 * storage: the increments never reached the save block and the reset never
 * cleared what was read. */
#define g_gpPlayerStandings  (&g_saveBlock[79])   /* 0x8FBB88, [4] */

/* Per-character per-track GP win record at 0x8FBB94, [51]. Indexed
 * [charId * 5 + trackId] with 1-based track ids (Island 1 .. Emerald 5), so
 * charId 9 (Super Sonic) on track 5 reaches index 50 — the table needs 51
 * slots, not 50. Index 0 is never addressed: that int is g_gpPlayerStandings[3].
 *   0x4C486C — [edx + eax*4 + 0x8FBB94] with edx = trackId*4, eax = charId*5
 *   0x470EF2 — clear loop, 10 rows × 20 bytes, byte offsets 4..20 per row
 * The same 50 written ints appear in the binary under a second name whose
 * clear is translated in save.c; both #defined here so writes via either name
 * are visible to readers via the other. g_checkpointBestTimes is the clear-loop
 * view and starts one int later, at the first addressed slot. */
#define g_gpCharTrackWins     (&g_saveBlock[82])  /* 0x8FBB94, [51] */
#define g_checkpointBestTimes (&g_saveBlock[83])  /* 0x8FBB98, [50] — alias */

/* Per-character race/time data at 0x8FBC60, [10]. Same memory region used
 * under two names in the binary:
 *   g_raceTimeTable  — cleared on save init (save.c)
 *   g_gpCharRaceCount — per-character race count, capped at 99 (race_setup.c)
 * Both #defined into g_saveBlock so writes via either name are visible to
 * readers via the other — previously disjoint standalone arrays. */
#define g_raceTimeTable      (&g_saveBlock[133])  /* 0x8FBC60, [10] */
#define g_gpCharRaceCount    (&g_saveBlock[133])  /* 0x8FBC60, [10] — alias */

/* Per-checkpoint / per-character best-time table — two aliases into the same
 * save block region. Binary address 0x8FBC84 (g_cpTableA) / 0x8FBC88
 * (g_gpCharDetail) sit one int apart in the same 411-int table; both names
 * are used in the binary for different access patterns (checkpoint column
 * math vs. 41-stride per-character row math). The port previously had
 * g_gpCharDetail as a separate standalone array — that was an address-alias
 * bug; writes via one name didn't show up in reads via the other. Now
 * unified: both names index into g_saveBlock. */
#define g_cpTableA           (&g_saveBlock[142])  /* 0x8FBC84, [411] */
#define g_gpCharDetail       (&g_saveBlock[143])  /* 0x8FBC88, [410] */

/* Save state flag (0x8FC2F0) — last entry in block */
#define g_saveStateFlag      g_saveBlock[553]      /* 0x8FC2F0 */

/* =====================================================================
 * Fade System
 * ===================================================================== */
extern int          g_fadeState;            /* 0x00901C48 */
extern int          g_fadeLevel;            /* 0x00901C44 */
extern int          g_fadeSpeed;            /* 0x00901C4C */

/* =====================================================================
 * Race State
 * ===================================================================== */
extern int          g_raceResult;           /* 0x00901C10 */
extern int          g_raceFinished;         /* 0x00901C88 */
extern int          g_raceCheckpoint;       /* 0x00901C78 */
extern int          g_postRaceCameraMode;   /* 0x00901C84 */
extern int          g_introCountdown;       /* 0x00901CC4 */
extern int          g_orbitAngle;           /* 0x00901C50 */
extern int          g_isMultiRace;          /* 0x008FB94C — true iff g_raceType == RACE_TYPE_MULTIPLAYER (1); gates 2P split-screen camera/physics/lap-distance */
extern int          g_playerCount;          /* 0x008FB99C — number of racers; single-race intro anim threshold is g_playerCount+4 */
extern int          g_viewportIndex;        /* 0x006E991C */
extern int          g_exitRaceFlag;         /* 0x00901C18 */
extern int          g_isPaused;             /* 0x00901C30 */
extern int          g_pauseLatch;           /* 0x00901C34 */

/* =====================================================================
 * Screen / Menu
 * ===================================================================== */
/* g_screenResult (0x925418) and g_modelRotation (0x92528C) live inside the
 * 0x92528C state block — declared with the rest of it further down. */
extern unsigned char g_inputBits;           /* 0x009020D9 */
extern unsigned short g_perPlayerInput[4];  /* 0x009020C8 — per-player input state words */
extern unsigned short g_combinedInputState; /* 0x009020D8 */

/* Bits of the pad word above (PAD_JUMP, PAD_LEFT, PAD_DIRECTIONS, ...).
 * Separate header so the platform layers can have them without pulling in
 * all of sonicr_globals.h. Note g_inputBits directly above is a DIFFERENT
 * bit set with colliding values — pad_bits.h explains. */
#include "pad_bits.h"

/* =====================================================================
 * Math Tables
 * ===================================================================== */
extern int          g_sinTable[5120];       /* 0x0092568C — extra 1024 for cosTable wrap */
extern int         *g_cosTable;             /* 0x0051E074 — &g_sinTable[1024] */

/* Texture loading state */
extern int          g_nextLoadTpage;
extern const char  *g_nextLoadFilename;

/* =====================================================================
 * Model / Track Data
 * ===================================================================== */
extern int          g_modelCount;           /* 0x00713060 */
extern int          g_modelLimbCount;       /* 0x00713064 */
extern int          g_modelVertexCount;     /* 0x00713068 */
extern int          g_modelPolygonCount;    /* 0x00713070 */
extern int          g_modelFrameCount;      /* 0x00713074 */
extern void        *g_objectStructArray;    /* 0x00712D44 */
extern SrcVertex   *g_vertexArrayBase;      /* 0x00712D48 */
extern void        *g_polygonArrayBase;     /* 0x00712D4C */
extern int          g_totalObjects;         /* 0x006EAD30 */
extern int          g_trackVertexCount;     /* 0x006EAD34 */
extern int          g_ringCount;            /* 0x006EAD3C */
extern int          g_weatherType;          /* 0x0094BCF4 — 0=clear, 1=rain, 2=snow */

/* Camera orientation vectors (from SetViewportClipRect camBlock[0]-[2]) */
extern int          g_camOrientX;           /* 0x006E9C84 */
extern int          g_camOrientY;           /* 0x006E9C88 */
extern int          g_camOrientZ;           /* 0x006E9C8C */

/* Software 3D renderer state */
extern int          g_visibleObjectCount;   /* 0x006EAD2C */
extern int          g_processedObjectCount; /* 0x006DA2E8 */
extern int          g_triggerObjectIndex;   /* 0x006DA318 */
extern int          g_triggerFlag;          /* 0x006DA2F0 */
extern int          g_triggeredObjectFound; /* 0x006E9D1C */
extern int          g_farClipDepth;         /* 0x008FB35C */
extern int          g_renderQuality;        /* 0x008FD474 */
extern int          g_fogDist0;             /* 0x006D7624 */
extern int          g_fogDist1;             /* 0x006D762C */
extern int          g_fogDist2;             /* 0x006D7634 */
extern int          g_colorTintEnable;      /* 0x0094BCFC */
extern int          g_colorTintR;           /* 0x0094BD0C */
extern int          g_colorTintG;           /* 0x0094BD10 */
extern int          g_colorTintB;           /* 0x0094BD14 */
extern int          g_renderFlags;          /* 0x008FB828 */
extern int          g_emeraldSineOffX;      /* 0x00712D68 */
extern int          g_emeraldSineOffY;      /* 0x00712D6C */
extern int          g_emeraldSineOffZ;      /* 0x00712D70 */

/* =====================================================================
 * Character Data
 * ===================================================================== */
extern int         *g_charStatsTable;       /* 0x005016B4 — stride 0x28 */
extern void        *g_charAnimTables;       /* 0x004FBDF8 */
/* g_charUnlockTable and g_allCharsUnlocked are now #defines into g_saveBlock */

/* =====================================================================
 * Ghost Replay
 * ===================================================================== */
extern int          g_ghostDataExists;      /* 0x008FB960 */
extern int          g_ghostCharId;          /* 0x008FB964 */
extern int          g_replayCharIds[];      /* 0x008FB980 — 5 sign-extended charIds per player slot */
extern int          g_ghostWriteIndex;      /* 0x00901CD8 */
extern int          g_ghostReadIndex;       /* 0x00901CDC */
extern int          g_ghostMaxFrames;       /* 0x00901CE0 */

/* =====================================================================
 * Sound
 * ===================================================================== */
extern void        *g_lpDirectSound;        /* 0x006D9AE8 — IDirectSound* */
extern int          g_masterVolume;         /* 0x006DA29C */
extern int          g_mciDeviceId;          /* 0x006DA28C */



/* =====================================================================
 * Player
 * ===================================================================== */
extern Player      *g_playerBase;          /* 0x008FD4F4 — first player struct */

/* -----------------------------------------------------------------
 * Player-struct aliased globals.
 *
 * The binary declares these as "global labels" that happen to be at
 * addresses INSIDE the player 0 struct. In the binary, writes via
 * `players[0].field = X` and writes via `*(int*)0x8fd550 = X` refer
 * to the same memory. The C port must reflect this aliasing or
 * reads from the global won't see struct updates (and vice versa).
 *
 *   g_racePlacement    @ 0x008FD550 = player[0] + 0x5C = racePosition
 *   g_raceLapFinished  @ 0x008FD552 = player[0] + 0x5E = lapsCompleted
 *
 * These are just g_playerBase[0].racePosition and
 * g_playerBase[0].lapsCompleted — use those directly.
 * ----------------------------------------------------------------- */

/* =====================================================================
 * Texture Pages
 * ===================================================================== */
extern int          g_uiTexPage;            /* 0x008F6C48 */
extern int          g_tpagePlayfield1;      /* 0x008F6C34 */
extern int          g_tpagePlayfield2;      /* 0x008F6C2C */
extern int          g_tpageObjects;         /* 0x008F6C38 */
extern int          g_tpageCharacters;      /* 0x008F6C30 */
extern int          g_tpageCharBase;        /* 0x008F6C28 */
/* Binary reads 0x8F6C28 / 0x8F6C2C as bytes for particle tpages — same address as the ints */
#define g_tpageParticle1 ((unsigned char)(g_tpageCharBase & 0xFF))    /* 0x008F6C28 low byte */
#define g_tpageParticle2 ((unsigned char)(g_tpagePlayfield2 & 0xFF)) /* 0x008F6C2C low byte */
extern int          g_tpageParallax1;       /* 0x008F6C3C */
extern int          g_tpageExtra;           /* 0x008F6C44 */
extern int          g_tpageBase;            /* 0x008F6C50 */

/* Save Data — g_saveBlock and all field accessors defined above (search "Save Data Block") */
extern int          g_shutdownStarted;      /* 0x006D9ACC */

/* =====================================================================
 * AI / Track
 * ===================================================================== */
extern void        *g_trackSurfaceData;     /* 0x006DA564 */
extern void        *g_splineWaypoints;      /* 0x009024A0 */
extern void        *g_waypointDataBase;     /* 0x00540064 */
extern void        *g_aiGridGround;         /* 0x00540070 */
/* 0x540048/4A/4C are three packed 16-bit fields; every binary access to each
 * is 16-bit and readers zero-extend, so they are unsigned. 0x540050 above
 * them is a genuine 32-bit int. */
extern unsigned short g_baseSpeedFactor;    /* 0x00540048 — aliased as g_rbBaseSpeed */
extern int          g_aiTurnThreshold;      /* 0x00540060 — canonical (aliased as g_rbTrackLen) */
extern int          g_aiGridOriginX;        /* 0x006DA5E0 */
extern int          g_aiGridOriginZ;        /* 0x006DA5E4 */
extern int          g_aiGridCellWidth;      /* 0x006DA5E8 */
extern int          g_aiGridCellHeight;     /* 0x006DA5EC */

/* =====================================================================
 * Camera / float scales
 * ===================================================================== */
extern sr_double    g_fixedToFloat;          /* 0x0051FCA0 — double in original */
extern sr_double    g_angleToRadians;        /* 0x0051FCA8 — double in original */

/* View matrix — stride-4 layout matching binary (4 ints per row, 3 used + 1 padding).
 * Binary: 0x6E9C44/48/4C row0, 0x6E9C54/58/5C row1, 0x6E9C64/68/6C row2. */
extern int          g_viewMtx[16];          /* 0x006E9C44 */
#ifdef SONICR_DC
extern float        g_viewMtxF[16];         /* DC: float copy, /4096 baked in (BuildViewMatrix) */
#endif
#define g_viewMtx00 g_viewMtx[0]
#define g_viewMtx01 g_viewMtx[1]
#define g_viewMtx02 g_viewMtx[2]
#define g_viewMtx10 g_viewMtx[4]
#define g_viewMtx11 g_viewMtx[5]
#define g_viewMtx12 g_viewMtx[6]
#define g_viewMtx20 g_viewMtx[8]
#define g_viewMtx21 g_viewMtx[9]
#define g_viewMtx22 g_viewMtx[10]
extern int          g_camIntX;              /* 0x006E9C90 */
extern int          g_camIntY;              /* 0x006E9C94 */
extern int          g_camIntZ;              /* 0x006E9C98 */



/* =====================================================================
 * Title Screen State
 * ===================================================================== */
extern int          g_pressStartColorR[4];       /* 0x00507630 — interpolated R per corner */
extern int          g_pressStartColorG[4];       /* 0x00507640 — interpolated G per corner */
extern int          g_pressStartColorB[4];       /* 0x00507650 — interpolated B per corner */
extern int          g_pressStartTargetR[4];      /* 0x00507660 — target R per corner */
extern int          g_pressStartTargetG[4];      /* 0x00507670 — target G per corner */
extern int          g_pressStartTargetB[4];      /* 0x00507680 — target B per corner */
extern int          g_pressStartAngle;       /* title logo rotation seed */
extern int          g_titleLogoAngle;       /* 0x00507690 — current track angle */
extern int          g_titleLogoAngleTarget; /* 0x00507694 — target track angle */
extern int          g_titleInputFlag;       /* input latch for title screen */
extern int          g_titleLogoColorLatch;        /* camera preset button latch */
extern int          g_titleLogoColorPreset;       /* current camera preset index */
/* g_titleLogoEnabled is now a #define into g_saveBlock (0x8FBA78) */
extern unsigned char g_titleCharIndices[5]; /* 0x00507628 — {2,3,4,5,0} */
extern int          g_titleLogoColorPresets[7][6];/* 0x005076A8 — camera preset table */
extern int          g_titleLogoPosX;         /* 0x008F7078 */
extern int          g_titleLogoPosY;         /* 0x008F707C */
extern int          g_titleLogoPosZ;         /* 0x008F7080 */
extern int          g_titleLogoRotYaw;          /* 0x008F7084 */
extern int          g_titleLogoRotPitch;        /* 0x008F7088 */
extern int          g_titleLogoRotRoll;         /* 0x008F708C */
extern int          g_cdPlaybackActive;     /* 0x006DA294 */

/* =====================================================================
 * Unlock Screen State
 * ===================================================================== */
extern const short *g_animDataPtrs[10];     /* 64-bit side-storage for player anim cursors */
extern int          g_creditsTpageSlots[4];  /* 0x006DA61C — tpage indices {2,3,4,5} */
extern int          g_creditsStepCounter;    /* 0x006DA62C */
extern int          g_creditsStateFlag;      /* 0x006DA630 */
extern short        g_creditsFontGlyphs[3][256][3]; /* 0x006DA634 — font glyph tables */

/* =====================================================================
 * Additional globals (defined in globals_extra.c)
 *
 * These are the canonical definitions for globals that have shared-address
 * aliases (see alias block below).
 * ===================================================================== */
extern int g_difficultyConfig;             /* 0x008fd444 */
extern int g_ghostToggle;                  /* 0x008FD448 */
extern int g_weatherConfig;                /* 0x008fd44c */
extern int g_catchUpToggle;                /* 0x008fd450 */
extern int g_guideToggle;                  /* 0x008FD454 */
extern int g_minimapConfig;                /* 0x008FD458 */
extern int g_softDoubleBuf;                /* 0x008FD478 */
extern int g_emeraldRenderFlag;            /* 0x008FD488 */
extern int g_doubleWidthFlag;              /* 0x008FD490 */
extern int g_vocalsEnabled;                /* 0x008FD498 */
extern int g_optSfxVolume;                 /* 0x008FD49C — SFX volume, 0-8 (0 = off) */
extern int g_musicEnabled;                 /* 0x008FD4A0 — derived: g_optMusicVolume != 0 */
extern int g_optMusicVolume;               /* music volume 0-8 (0 = off) — port addition,
                                            * no VMA; the original option is a toggle */
extern int g_rubberBandLowerBound;         /* 0x00540050 */
extern void *g_terCollisionMesh;           /* 0x006DA568 */
extern void *g_itemStateTable;             /* 0x006DA578 */
extern int g_dispProjScaleX;              /* 0x006E9888 */
extern int g_dispProjScaleY;              /* 0x006E988C */
extern int g_dispCenterX;                  /* 0x006E98A0 */
extern int g_dispCenterY;                  /* 0x006E98A4 */
extern int g_introTimer;                   /* 0x00901CC8 */
extern unsigned short *g_ringSpawnReadPtr; /* 0x00901CD0 */
#define g_randomStream g_ringSpawnReadPtr  /* alias: same memory, same type */
extern CamStateEntry g_camStateTable[];    /* 0x00902140 — 4 viewports × 40 bytes */
extern ModelMeta g_modelMeta[];            /* 0x007130A4 — 26 entries, stride 0x50 */
/* =====================================================================
 * State block at 0x0092528C — 151 ints, 0x92528C..0x9254E4.
 *
 * A general scratch region, NOT character-select state. Menu screens,
 * track-object animation, device enumeration and screen plumbing all live
 * here, because only one of them is active at a time. Multiple names on one
 * slot are real reuse and are listed together rather than hidden behind a
 * rename. The extent is where the binary's references stop: 0x9254E4 is the
 * last address referenced, followed by a 65-int gap.
 *
 * ONLY THE FIRST 128 INTS ARE CLEARED AT TRACK INIT. The loop at 0x472480 —
 *     xor eax,eax / add eax,4 / mov [eax+0x925288],ecx / cmp eax,0x200 / jne
 * walks eax 4..0x200, so it covers 0x92528C..0x925488 and stops. Slots [128]
 * and above are deliberately outside it; each of their tenants initialises
 * its own slots on entry. Do not widen that loop to the array size.
 *
 * Every alias indexes the block directly — no name resolves through another
 * name. Block index = (address - 0x92528C) / 4.
 * ===================================================================== */
extern int          g_stateBlock92528C[151]; /* 0x0092528C */

/* [0] 0x92528C — course-select player count / DirectPlay provider choice */
#define g_modelRotation     g_stateBlock92528C[0]
#define g_netProviderChoice g_stateBlock92528C[0]

/* [1]-[12] 0x925290-0x9252BC — menu cursor/scroll state, the track-object
 * angle counters, and the particle-spawn countdowns, all reusing these slots. */
#define g_menuExtraY        g_stateBlock92528C[1]    /* 0x925290 */
#define g_inputCaptureGate  g_stateBlock92528C[1]    /* 0x925290 — key-capture debounce */
#define g_particleSpawnStep1 g_stateBlock92528C[2]   /* 0x925294 — countdown, source A */
#define g_menuScrollX       g_stateBlock92528C[2]    /* 0x925294 */
#define g_menuScrollTarget  g_stateBlock92528C[3]    /* 0x925298 */
#define g_particleSpawnStep2 g_stateBlock92528C[4]   /* 0x92529C — countdown, source B */
#define g_menuMaxScroll     g_stateBlock92528C[4]    /* 0x92529C */
#define g_animStateA0       g_stateBlock92528C[5]    /* 0x9252A0 */
#define g_particleSpawnStep3 g_stateBlock92528C[6]   /* 0x9252A4 — countdown, source C */
#define g_animStateA4       g_stateBlock92528C[6]    /* 0x9252A4 */
#define g_initTableOffset   g_stateBlock92528C[6]    /* 0x9252A4 */
#define g_specialUnlock     g_stateBlock92528C[6]    /* 0x9252A4 */
#define g_animStateA8       g_stateBlock92528C[7]    /* 0x9252A8 */
#define g_animStateAC       g_stateBlock92528C[8]    /* 0x9252AC */
#define g_animBobBaseY      g_stateBlock92528C[9]    /* 0x9252B0 */
#define g_titleModelOffset  g_stateBlock92528C[9]    /* 0x9252B0 */
#define g_titleModelAngle   g_stateBlock92528C[10]   /* 0x9252B4 */
#define g_menuCursor        g_stateBlock92528C[10]   /* 0x9252B4 */
#define g_animStateB4       g_stateBlock92528C[10]   /* 0x9252B4 */
#define g_animStateB8       g_stateBlock92528C[11]   /* 0x9252B8 */
#define g_menuCursorY       g_stateBlock92528C[11]   /* 0x9252B8 */
#define g_menuCursorTargetY g_stateBlock92528C[12]   /* 0x9252BC */

/* [14]-[19] 0x9252C4-0x9252D8 — menu-ready flag and track-object anim state */
#define g_menuReady         g_stateBlock92528C[14]   /* 0x9252C4 */
#define g_animStateC8       g_stateBlock92528C[15]   /* 0x9252C8 */
#define g_animStateCC       g_stateBlock92528C[16]   /* 0x9252CC */
#define g_animStateD0       g_stateBlock92528C[17]   /* 0x9252D0 */
#define g_animStateD4       g_stateBlock92528C[18]   /* 0x9252D4 */
#define g_animStateD8       g_stateBlock92528C[19]   /* 0x9252D8 */

/* [20]-[23] 0x9252DC-0x9252E8 — per-player device type, and the animation
 * state that shares the same four slots. */
#define g_playerDeviceType  (&g_stateBlock92528C[20]) /* 0x9252DC — int[4] */
#define g_animStateE0       g_stateBlock92528C[21]   /* 0x9252E0 */
#define g_animStateE4       g_stateBlock92528C[22]   /* 0x9252E4 */
#define g_animStateE8       g_stateBlock92528C[23]   /* 0x9252E8 */

/* [24]-[34] 0x9252EC-0x925314 — track-object animation state */
#define g_animStateEC       g_stateBlock92528C[24]   /* 0x9252EC */
#define g_animStateF0       g_stateBlock92528C[25]   /* 0x9252F0 */
#define g_animStateF4       g_stateBlock92528C[26]   /* 0x9252F4 */
#define g_animStateF8       g_stateBlock92528C[27]   /* 0x9252F8 */
#define g_animStateFC       g_stateBlock92528C[28]   /* 0x9252FC */
#define g_animState300      g_stateBlock92528C[29]   /* 0x925300 */
#define g_animState304      g_stateBlock92528C[30]   /* 0x925304 */
#define g_animState308      g_stateBlock92528C[31]   /* 0x925308 */
#define g_factoryDoorState  g_stateBlock92528C[32]   /* 0x92530C — silo-door proximity (0..3 hysteresis) */
#define g_animState310      g_stateBlock92528C[33]   /* 0x925310 */
#define g_animState314      g_stateBlock92528C[34]   /* 0x925314 */

/* [38]-[44] 0x925324-0x92533C — Factory emerald timers and more anim state */
#define g_factoryEmeraldBounceTimer1 g_stateBlock92528C[38]  /* 0x925324 — slot 0x40 */
#define g_animState330      g_stateBlock92528C[41]   /* 0x925330 */
#define g_factoryEmeraldBounceTimer2 g_stateBlock92528C[42]  /* 0x925334 — slot 0x41 */
#define g_animState338      g_stateBlock92528C[43]   /* 0x925338 */
#define g_animState33C      g_stateBlock92528C[44]   /* 0x92533C */

/* [59] and [99] — screen plumbing */
/* The menus' kept choices live outside the 0x92528C block (0x68AF74, 0x68AF94),
 * so they survive the track-init clear: */
extern int g_prevNumViewports;    /* 0x68AF74 — numViewports saved across screens, boot: 1 (0x4CD9AB) */
extern int g_lastCourseCursor;    /* 0x68AF94 — course chosen last, boot: 0 (0x4CD9B7) */
#define g_screenResult      g_stateBlock92528C[99]   /* 0x925418 — value every screen function returns */

/* [128]-[150] 0x92548C-0x9254E4 — above the track-init clear.
 *
 * Three subsystems reuse [128] across their own lifetimes: the DirectInput
 * device-enumeration counter at startup (callback 0x477EE4 increments it,
 * 0x47804D zeroes it first, 0x478067 reads the total), the City sign-physics
 * active flag during a race, and the results-screen button cursor. Each
 * initialises the slot itself, which is why nothing bulk-clears this range. */
#define g_animState48C      g_stateBlock92528C[128]  /* 0x92548C — sign block A active */
#define g_animState490      g_stateBlock92528C[129]  /* 0x925490 — block A velocity X */
#define g_animState494      g_stateBlock92528C[130]  /* 0x925494 — block A gravity */
#define g_animState498      g_stateBlock92528C[131]  /* 0x925498 — block A velocity Z */
#define g_animState49C      g_stateBlock92528C[132]  /* 0x92549C — block A scatter X */
#define g_animState4A0      g_stateBlock92528C[133]  /* 0x9254A0 — block A scatter Y */
#define g_animState4A4      g_stateBlock92528C[134]  /* 0x9254A4 — block A scatter Z */
#define g_animState4A8      g_stateBlock92528C[135]  /* 0x9254A8 — block A accum pos X */
#define g_animState4AC      g_stateBlock92528C[136]  /* 0x9254AC — block A accum pos Z */
#define g_animState4B0      g_stateBlock92528C[137]  /* 0x9254B0 — block A bounce count */
#define g_animState4B4      g_stateBlock92528C[138]  /* 0x9254B4 — block B active flag */
#define g_animState4B8      g_stateBlock92528C[139]  /* 0x9254B8 — block B velocity X */
#define g_animState4BC      g_stateBlock92528C[140]  /* 0x9254BC — block B gravity */
#define g_animState4C0      g_stateBlock92528C[141]  /* 0x9254C0 — block B velocity Z */
#define g_animState4C4      g_stateBlock92528C[142]  /* 0x9254C4 — block B scatter X */
#define g_animState4C8      g_stateBlock92528C[143]  /* 0x9254C8 — block B scatter Y */
#define g_animState4CC      g_stateBlock92528C[144]  /* 0x9254CC — block B scatter Z */
#define g_animState4D0      g_stateBlock92528C[145]  /* 0x9254D0 — block B accum pos X */
#define g_animState4D4      g_stateBlock92528C[146]  /* 0x9254D4 — block B accum pos Z */
#define g_animState4D8      g_stateBlock92528C[147]  /* 0x9254D8 — block B bounce count */

/* g_rbBaseSpeed removed — alias for g_baseSpeedFactor (0x00540048), see #define below */
/* 0x009020EC — one of the input-state words the track-init clear treats as a
 * group with 0x9020E2..0x9020EA. Every one of its seven binary accesses is
 * 16-bit (all carry a 0x66 prefix), and 0x9020EE is never referenced. */
extern short        g_inputStateEC;         /* 0x009020EC */
extern void        *g_terLoopTable;        /* 0x006DA570 — loop/ride surface table: TerLoopEntry[] */
extern int          g_lightingDepthTable[]; /* 0x0050A294 */

/* =====================================================================
 * Shared-address aliases
 *
 * In the original binary, each pair below occupied the SAME memory address.
 * The "canonical" name is the real definition; the alias is a #define so
 * that code using either name compiles and resolves to one storage location.
 * ===================================================================== */

/* Same type, same address */
#define g_interlaceMode     g_softDoubleBuf         /* 0x008FD478 */
#define g_optCfg_488        g_emeraldRenderFlag     /* 0x008FD488 */
#define g_initFlag8fd490    g_doubleWidthFlag       /* 0x008FD490 */
#define g_rbThreshold       g_rubberBandLowerBound  /* 0x00540050 */
#define g_rbBaseSpeed       g_baseSpeedFactor       /* 0x00540048 — same variable */
#define g_rbTrackLen        g_aiTurnThreshold       /* 0x00540060 — same variable */

extern int          g_timeOfDay;            /* 0x0094BCF8 — TOD_SUNRISE..TOD_NIGHT */

/* Flyover camera state arrays (0x902008-0x902048) */
extern int g_flyoverNearest[];             /* 0x00902008 */
extern int g_flyoverMode[];                /* 0x00902018 */
extern int g_flyoverParam[];               /* 0x00902028 */
extern int g_flyoverTimer[];               /* 0x00902038 */
extern int g_flyoverWpIdx[];               /* 0x00902048 */
extern int *g_waypointTablePtr;            /* 0x00901EE4 */
extern int g_numWaypoints;                 /* 0x00901EE8 */

/* Type mismatches — int vs short at same address (little-endian overlap) */

/* Particle effect state buffer: base 0x907F20, 64 entries × 60 bytes. */
extern CollectEffect g_collectEffectBuf[];

extern float g_farClipFloat;

extern char g_tpageStateArray[];


extern int g_raceOrder[];   /* 0x902070 — [3]=doorTrigger, [4]=debrisGate, [5]=debrisPlayerIdx */
/* 0x902074-0x902090 = g_raceOrder[1..8] — cycling counters inside the race order array.
 * ComputeRacePositions reads g_raceOrder[numPlayers] to pick which player to update. */

/* ==== Graduated globals (automated extern pass) ==== */
extern intptr_t g_animRegTable[];          /* 0x00676ff0 */
extern void *g_charFaceBase;          /* 0x00713e68 */
extern int g_menuAnimY;          /* 0x9252c0 */
extern Player *g_trailSrcA;          /* 0x8fd4e4 */
extern int g_animFrameCounter;
extern int g_raceCounterA0;          /* 0x009020a0 */
extern int g_ringChaseArray[];          /* 0x907b20 */
extern int g_particleIdx;          /* 0x9020a4 */
extern void* g_ringChaseTarget[];
extern char g_balloonArray[];          /* 0x712d74 */
extern int g_p1CollectionCount;          /* 0x901c70 */
extern int *g_gateWaypointPtr;          /* 0x901ef0 */
extern char g_gateHistoryBuf[];          /* 0x90e3c0 */
extern void *g_introSplineBase;          /* 0x902498 */
extern CamStateEntry g_gpSmoothedCam;          /* 0x009020f0 */
extern short g_savedCamPitch;          /* 0x009021e0 */
extern short g_savedCamYaw;          /* 0x009021e2 */
extern int g_savedCamX;          /* 0x009021e4 */
extern int g_savedCamY;          /* 0x009021e8 */
extern int g_savedCamZ;          /* 0x009021ec */
extern int *g_currentRenderCam;
extern float g_camFloatX;
extern float g_camFloatY;
extern float g_camFloatZ;
extern int g_tpageParallax2;          /* 0x008f6c40 */
extern int g_tpageCount;          /* 0x008f6c4c */
extern int g_randomRingIdx;          /* 0x901cd4 */
extern int g_collectAnimTimer;
extern int g_raceSpeedMult;          /* 0x8fb998 */
extern int g_cdPlaybackState;          /* 0x006d9a40 */
extern int g_cdPlaybackTarget;
extern void* g_tpagePixelBuf[];
extern int g_tpageWidth[];
extern int g_tpageHeight[];
extern int g_cdAvailable;          /* 0x00504278 */


extern int g_ghostTotalFrames;          /* 0x008fb95c */
/* Capacity of g_taGhostBuffer. Recording is bounded by g_ghostMaxFrames
 * (0x8000 / numViewports) instead, which is larger, so any frame count reaching
 * the buffer has to be clamped to this. Must match the definition in
 * globals_extra.c. */
#define GHOST_BUFFER_FRAMES 0x1600
extern unsigned short g_taGhostBuffer[];          /* 0x00911d8c */
extern int s_glTextureDirty[];
extern void *g_terVertexTable;          /* 0x6da55c */
extern void *g_terFaceTable;          /* 0x6da560 */
extern void *g_terEdgeList;          /* 0x6da56c */
extern void *g_terUnknown74;          /* 0x6da574 */
extern void *g_terGridIndex;          /* 0x6da57c */
extern void *g_terGridData;          /* 0x6da580 */
extern int g_terLoopCount;          /* 0x6da5d0 */
extern float g_worldBoundsB;          /* 0x6d75cc */
extern float g_worldBoundsE;          /* 0x6d75d8 */
extern float g_worldBoundsD;          /* 0x6d75d4 */
extern float g_worldBoundsC;          /* 0x6d75d0 */
extern float g_worldBoundsA;          /* 0x6d75c0 */
extern int g_tpageUIAlt;          /* 0x8f6c54 */
extern int g_farClipTimes8;          /* 0x8fb360 */
extern int g_viewportArray[];          /* 0x8fb368 */
extern int g_viewportConfigArray[];          /* 0x6e9924 */
extern int g_objectRenderEnable;
extern int g_glViewportOffsetX;
extern int g_glViewportOffsetY;
extern int g_splitScreenMode;          /* 0x8fd45c */
extern int g_ringAnimFrame;          /* 0x901c6c */
extern int g_netGameStartState;          /* 0x501844 */
extern int g_netReadyFlag;          /* 0x689bac */
extern int g_minimapToggle;          /* 0x8fb818 */
extern int g_qtSurfIdx;          /* 0x6da590 */
extern int g_qtEdgeIdx;          /* 0x6da594 */
extern int g_racePointsTotal[];          /* 0x8fb67c */
extern int g_racePointsLaps[];          /* 0x8fb64c */
extern int g_initFeatureC;          /* 0x675c1c */
extern int g_clipFar;          /* 0x8fb608 */
extern int g_clipNear;          /* 0x8fb610 */
extern int g_qualityLevel;          /* 0x8fd480 */
extern int *g_ringSpawnArray;          /* 0x712d50 */
extern uint32_t g_objectStructStorage[];          /* 0x00712d44 */
extern int g_optResLow;
extern int g_optResHigh;
extern int g_optCfg_46c;
extern int g_optCfg_470;          /* 0x008fd470 */
extern int g_optCfg_47c;
extern int g_resolutionLevel;          /* 0x8fd484 */
extern int g_optCfg_48c;
extern int g_stereoEnabled;       /* 0x008fd494 — Sound page stereo/mono toggle, 1 = stereo */
extern int g_optCfgRomData[];
extern int g_d3dSurfaceMode;          /* 0x4fc23c */
extern int g_tpageGroupShift;          /* 0x8f6c50 */
extern int g_dispClipLeft;          /* 0x6e9870 */
extern int g_dispClipTop;          /* 0x6e9874 */
extern int g_dispHalfWidth;          /* 0x6e9880 */
extern int g_dispHalfHeight;          /* 0x6e9884 */
extern int g_viewportWidth;          /* 0x8fb38c */
extern int g_viewportHeight;          /* 0x8fb390 */
extern int g_vpClipLeft10;
extern int g_vpClipRight10;
extern int g_vpClipLeft16;
extern int g_vpClipRight16;
extern short g_joystickConfigWords[];          /* 0x675404 */
extern int g_diDeviceReady;          /* 0x675c2c */
extern unsigned short g_taGhostSource[];          /* 0x91498c */
extern unsigned short *g_p1JoystickPtr;          /* 0x8fd6f0 */
extern unsigned short *g_p2JoystickPtr;          /* 0x8fde0c */
extern unsigned short *g_p3JoystickPtr;          /* 0x8fe528 */
extern unsigned short *g_p4JoystickPtr;          /* 0x8fec44 */
extern int s_cameraStruct[];
extern void* g_soundBuffers[64];          /* 0x006d9aec */
extern int g_limbMetaTable[];          /* 0x007133a0 */
extern unsigned char g_polyTypeTable[];          /* 0x008f6428 */
extern int g_charLightingTable[];          /* 0x007bc4a8 */
extern int g_vertexIndexRunning;          /* 0x006ead34 */
extern int g_footShadowCtrl[];          /* 0x00908e20 */
extern int g_trackIdTable[];          /* 0x502754 */
extern int g_emeraldAnimFrame1;          /* 0x00901c64 */
extern int g_clipLeftDouble;
extern int g_vpParam0F;
extern int g_vpParam10;
extern int g_parallaxWidthDouble;
extern int g_netWaitFlag;          /* 0x006d9ab8 */
extern int g_optSfxVolumeSave;          /* 0x006da2a0 — g_optSfxVolume saved across a demo/replay */
extern int g_multiplayerWasActive;          /* 0x006d97e8 */
extern int g_timeAttackResult;          /* 0x008fb958 */
extern int g_replayPointsLaps[];          /* 0x008fb8c4 */
extern int g_replayPointsTotal[];          /* 0x008fb8d0 */
extern int g_replayLapCount;          /* 0x008fb8d4 */
extern int g_replayPlacement;          /* 0x008fb8d8 */
extern int g_charUnlockSource[];          /* 0x006d981c */
extern int g_superSonicSeed;          /* 0x0068af98 */
extern int g_dpLobbyObject;          /* 0x0068ac6c */
extern int g_splashPrevState;          /* 0x0068af6c */
extern int g_nextScreenId;          /* 0x0068af9c */
extern int g_parallaxWidth;          /* 0x8fb620 */
extern int g_netSyncEstablished;          /* 0x0068af1c */
extern int g_gpDifficultyLevel;          /* 0x006dd834 */
extern char g_netPlayerDecorations[];          /* 0x0068acf8 */
extern int g_resultsUnlockFlag;          /* 0x689a70 */
extern int g_posDataCount4;          /* 0x00901ee0 */
extern int g_gpResultFlag;          /* 0x006da618 */
extern int g_finishOrderCounter;          /* 0x901c80 */
extern int g_netLobbyPlayerSlot;          /* 0x689bb4 */
/* NetworkScreen's own podium Player at 0x8FF880 (see globals_extra.c). Its
 * fields used to be seven separate globals with unrelated names —
 * g_menuTimingValue was really angleYaw (+0x010), g_menuCursorPos was animId
 * (+0x098), g_netLocalCharId was charId (+0x0F2), and so on. Offsets are
 * checked by the _Static_asserts in player_struct.h. */
extern Player g_menuPlayer;             /* 0x8ff880 */
extern int g_rubberBandThreshold;          /* 0x00540058 */
extern unsigned short g_autoSteerFlag;          /* 0x0054004c */
extern int g_sfxCooldownTimer;          /* 0x00901cc0 */
extern int *g_trackBoundaryWaypoints;          /* 0x90249c */
extern unsigned char g_keyPressState[320];
extern int g_prevPrevPosX;
extern int g_prevPrevPosY;
extern int g_prevPrevPosZ;
extern int g_bouncePosition;          /* 0x901c54 */
extern int g_bounceVelocity;          /* 0x901c58 */
extern void *g_segmentOffsetTable;
extern void *g_aiGridSurface;
extern void *g_charRampSpeedTable;
extern int g_rubberBandDivisor;          /* 0x0054005c */
extern void *g_playerPtrTable;
extern int g_avoidanceBias;
extern int g_avoidanceCounter;
extern void *g_podiumCenter;          /* 0x9024a8 */
extern short g_configLapCount;          /* 0x901de4 */
extern int g_terCollectibleCount;
extern unsigned char __attribute__((aligned(32))) g_tileMap[];          /* 0x0068b2c0 */
extern intptr_t g_bounceStateArray[];          /* 0x907af0 */
extern int g_ringCollectPos[];          /* 0x901c8c */
extern int g_raceTimerB[];          /* 0x901c98 */
extern int g_ringRespawnPos[];          /* 0x901ca4 */
extern int g_raceCounter98;          /* 0x902098 */
extern int g_raceCounterA8;          /* 0x9020a8 */
extern int g_trackEventTimer;          /* 0x9118e8 */
extern int g_effectPosItemBurst[];          /* 0x00901cb0 */
extern int g_gpBestChanged[];          /* 0x006d97f0 */
extern int g_gpBeatAllFlag;          /* 0x006d9810 */
extern PrecipParticle g_precipParticles[];          /* 0x94bd24 */
extern int g_precipVpRange[5];       /* 0x0094D924 — [v]..[v+1] is viewport v's particle range */
extern int g_gridTintR;          /* 0x0094bd00 */
extern int g_gridTintG;          /* 0x0094bd04 */
extern int g_gridTintB;          /* 0x0094bd08 */
extern int g_playerFinishFlag[];          /* 0x00901ff8 */
extern int g_trailWriteA;          /* 0x9020ac */
extern int g_trailWriteB;          /* 0x9020b4 */
extern int g_trailCountA;          /* 0x9020b0 */
extern int g_trailCountB;          /* 0x9020b8 */
extern Player *g_trailSrcB;          /* 0x8fd4e8 */
extern int g_trailBufA[];          /* 0x90e320 */
extern int g_trailBufB[];          /* 0x90e370 */
extern int g_romGlyphTable[];          /* 0x501f84 */
extern int g_romCharMap[];          /* 0x502268 */
extern int g_itemEffectAnimPhase;          /* 0x901c68 */
extern int g_animFrameData[];          /* 0x75eb18 */
/* Balloon mesh (Balloon submode only) — see collectible_model_data.c */
extern int s_balloonModelVtx[];          /* 0x508940 — 52 verts x 16 ints */
extern int s_balloonModelPoly[];          /* 0x509640 — 34 quads x 12 ints */
extern const int s_balloonModelNormal[];          /* 0x4ff788 — 52 shading factors */
extern int g_lightingPhaseGlobal;          /* 0x94d938 */
extern unsigned char g_rgb555Remap[];
extern int g_parallaxState[];
extern int g_optCfg_468;          /* 0x008fd468 */
extern int g_menuIdleStartTime;          /* 0x0068afb0 */
extern int g_resultsPlayerData[];          /* 0x689bb8 */
extern int g_resultsTextBuffer[];          /* 0x689bbc */
extern int g_resultModelIndices[];          /* 0x0068153c */
extern int g_parallaxExtraX;          /* 0x8fb628 */
extern int g_initFeatureB;          /* 0x006d9ae8 */
extern int g_volumeBase;          /* 0x005041a8 */
extern int g_sceneryPolygonCount;
extern int g_objectPolygonCount;
extern int g_polygonIndexRunning;          /* 0x6ead38 */
extern int g_weatherR;          /* 0x94d940 */
extern int g_weatherG;          /* 0x94d944 */
extern int g_weatherB;          /* 0x94d948 */

#endif /* SONICR_GLOBALS_H */
