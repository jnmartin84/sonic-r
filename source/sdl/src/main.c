/**
 * main.c — SDL2 + OpenGL entry point
 *
 * Translation of WinMain (0x004CDFC4, 8254 bytes) from SONICR.EXE.
 *
 * Win32-specific calls are replaced with SDL2 equivalents.
 * Direct3D rendering is replaced with OpenGL fixed-function.
 * Game logic is translated as-is.
 */

#include <unistd.h>
#include <getopt.h>

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "vertex_struct.h"
#include "platform.h"
#include "net_transport.h"
#include "net/matchmaker.h"
#include "net/net_interp.h"

/* Shared keystate from the platform layer */
extern unsigned char s_keystate[256];

extern void UpdateWeatherCounters(void);

/* Timer — must share a clock base with timeGetTime() on every platform.
 * On Windows the real Win32 timeGetTime() (ms since boot) is in scope via
 * <mmsystem.h>; on macOS/Linux/DC the platform layer provides ms since
 * program start. Mixing wall-clock sources caused the client to free-run
 * because frame caps comparing the two always saw "huge elapsed" and
 * exited immediately. */
static uint32_t timeGetTime_ms(void)
{
#ifdef _WIN32
    return (uint32_t)timeGetTime();
#else
    return platform_get_time_ms();
#endif
}

/* Forward declarations — functions called from WinMain */
void InitFileIO(void);
void InitInputMappings(void);
void InitJoystick(void);
void InitDebugLog(void);
void InitSonicR_A(void);
void InitSaveData(void);
void InitPadTypes(void);
void LoadGameState(void);
void InitCDPlayer(void);
void InitDirectInput(void);
void InitCD(void);
void SetScreenDimensions(void);        /* 0x4CBEFC — sets g_dispCenterX/Y */
void ApplyViewportGeometry(void);      /* port addition — mode height + full rebuild */
extern int g_screenHeightBase;
void ComputeViewportBounds(void);      /* 0x4CBDC8 — sets g_dispClip bounds */
void SetupViewportConfig(void);        /* 0x4CBA28 — fills g_viewportArray */
void ResetInputState(void);
void InitTitleScreen(void);
void UpdatePalette(void);
int  SegaLogoScreen(void);
int  TravellersTalesLogoScreen(void);
int  TitleScreen(void);
int  MainMenuScreen(void);
int  AutoSelectDemo(void);
int  MultiPlayerScreen(void);
int  TimeAttackModeSelect(void);
int  MultiPlayerModeSelect(void);
int  NetworkScreen(void);
int  NetworkScreenReentry(void);                     /* 0x48A8AC */
void InitOptionStuff(void);
void Shutdown(void);
void LoadTrackSinglePlayer(void);
void InitLevel(void);
void InitWeather(void);
void UpdateFade(void);
void UpdateGameLogic(void);
void UpdatePerPlayerInput(void);
void CheckRaceCompletion(void);
void UpdateWeather(void);
void UpdateWeatherEffects(void);
/* Per-player functions — declared in sonicr_functions.h with player pointer params.
 * These that aren't in the header need explicit declarations: */
void SweepPlayerCollision(Player *pA, Player *pB);
void Update5PlayerRacePhysics(void);
void UpdatePlayerMovement(Player *player);
void SpawnPlayerParticleEffects(Player *player, int effectsEnabled);
void SpawnFootShadows(void);
void UpdateTrackObjects(CollectEffect *buf);
void UpdateMissiles(void);
void UpdateTrackWorld(void);
void AnimateTrackObjects(void);
void UpdatePlayfieldGridPositions(void);
void UpdateRaceObjects(void);
void UpdateRaceRings(void);
void BuildCameraViewForViewport(int vpIdx);
void UpdatePerViewportCamera(void);
void SpawnOtherParticle(int viewportIdx);
void UpdateLapCounter(int isGameActive);
void UpdateFrameTimers(void);
void RenderHUD(void);
void FlipD3D(void);
void UpdateNetworkHost(void);
void UpdateNetworkClient(void);
void SendNetworkHostData(void);
void SendNetworkClientData(void);
void UpdateNetworkSync(const void *data, int len);
void SetAllSoundVolumes(void);
void PlaySoundEffect(int soundCmd, int distance, int freqParam);
void SpawnTAParticles(void);
void ReadInput(void);
void DrawDebugOverlayText(const char *s, int x, int y, int pixSz, unsigned int color);  /* screen_misc.c — demo overlay */
void SelectCDTrack(void);
void InitRaceStart(void);
void SelectCDTrack(void);
void UpdateNetworkHost(void);
void UpdateNetworkClient(void);
extern void LoadKeyMappings(void);
extern void InitRandomTables(void);
extern void NetLevelSyncBarrier(void);

extern unsigned short g_randomRingBuffer[];
extern int g_netSavedButtonByte;        /* 0x68AFAC */
extern int g_raceState2ec;
extern int g_raceState2f8, g_raceState2fc;
extern int g_raceState300, g_raceState304, g_raceState308;
extern int g_raceState30c, g_raceState310, g_raceState314;
extern int g_sortListOffset;
extern int g_aiGridAngleOffset;
extern int g_introCountdownInit;
extern int g_screenshotFlag;
extern int g_frameSpeedAdjust;
extern int g_fpsHistoryTable[];
extern int g_fpsDisplay;
extern int g_raceTimerBase;
extern int g_raceStateCounter0, g_raceStateCounter1;
extern int g_savedDemoMode;
extern int g_raceElapsedSec;

/* Forward declarations for post-race functions */
void ShowEmeraldUnlockScreen(void);       /* 0x004C86D0 — 1625 bytes — unlock notification screen */
void AdvanceGrandPrixTrack(void);       /* 0x00471364 — 1695 bytes — GP progression */
void CalculateChampionshipPoints(void); /* 0x004C43C0 — 2395 bytes — GP scoring */
void CloseDirectPlaySession(void);      /* 0x004875CC */
void CloseDirectPlayLobby(void);        /* FUN_00486928 — DirectPlay lobby cleanup */
void SaveGhostData(void);               /* 0x0042E9A4 */
void SetupReplayData(void);             /* 0x004D06F8 */
extern int g_savedNumHumans;            /* 0x00925688 */
extern int g_specialRacePlacement;      /* 0x006D9AAC */
/* g_charUnlockState is a #define via sonicr_globals.h */
extern unsigned char g_taRecordFlags[];
extern int g_taBestTotalTime[];
extern int g_taBestLapTime[];
extern int g_triggerDepthRef;
extern int g_raceElapsedDisplay;
extern int g_raceTimerDisplayBase;
extern int g_interlaceMode;

extern int g_splashCountdown;

extern void net_deco_set_name(char *entry, const char *name);
extern void net_set_slot_name(int slot, const char *name);
extern void net_set_slot_platform(int slot, const char *platform, uint8_t region);

/* Command-line options */
static char s_hostIPArg[64];
const char *g_cmdHostIP = NULL;
/* Default-unlocked on both DC (no command line) and SDL (debug convenience).
 * SDL also accepts -u to set this explicitly. */
static int s_cmdUnlock = 0;
static int s_cmdFullscreen = 0;
static int s_cmdPort = -1;
static char s_cmdUsername[MM_MAX_USERNAME];

extern void UpdateFlyoverCamera(Player *player, CamStateEntry *cam, RenderCamera *outStruct,
                                 int *waypointTable, int param5, int vpIdx);

extern int LoadGameSettings(void);
extern void InitDirectSound(void);

extern void AdjustRubberBandAI(void);                        /* 0x00421270 */
extern void ResolvePlayerCollision(Player *pA, Player *pB);  /* 0x004D56B8 */
extern void SweepPlayerCollision(Player *pA, Player *pB);    /* 0x004201A0 */
extern void TrackSurfaceAI(Player *player);                  /* 0x0041E6DC */
extern void PlayerPhysicsMain(Player *player);               /* 0x004206B8 */
extern void UpdatePlayerMovement(Player *player);            /* 0x004D8AB8 */

/* On non-Windows platforms, stub out the Win32 entry points the original
 * code used to call. On Windows the real <windows.h> provides them, so this
 * whole block is suppressed to avoid redefinition errors. */
#ifndef _WIN32
/* timeGetTime — Win32 multimedia timer, returns ms since startup */
DWORD timeGetTime(void) {
    return timeGetTime_ms();
}
#endif

/* g_titleLogoAngle — title screen track rotation angle. 0x800 is the ROM value
 * at 0x507690. Race start derives mirror mode from it as 1 - (angle / 2048). */
int g_titleLogoAngle = 0x800;   /* 0x00507690 */

/* Platform init stubs — these set up Win32 subsystems we don't need */
void InitDebugLog(void) {
     /* Win32 debug log file — we use printf */
}
void InitFileIO(void) { 
    /* Win32 file system init — not needed on SDL port */
}
void InitJoystick(void) { 
    /* DirectInput joystick enum — not needed yet */
}

/* Network stubs — DirectPlay multiplayer, not implemented */

/* DirectPlay session management — replaced by UDP networking.
 * NetworkScreen calls these; they return success so the state machine
 * can proceed through its lobby flow. */
/* 0x486D78 — Host: bind UDP socket and begin accepting clients */
int CreateNetworkSession(const char *name, const char *password)
{
    (void)name; (void)password;
    if (net_host_start(NET_PORT_DEFAULT) == 0) {
        g_currentPlayerIdx = 0;          /* host is slot 0 */
        g_localPlayerIndex = 0;
        g_netPlayerCount = 1;            /* host counts as player 0 */
        g_netGameStarted = 1;            /* binary: DirectPlay sets to 1 (initial player count) */

        /* Populate host's decoration table entry + raw-name cache */
        char *entry = g_netPlayerDecorations + 0 * NET_DECO_STRIDE;
        *(int *)(entry + NET_DECO_DPID) = 0;
        *(unsigned short *)(entry + NET_DECO_SLOT) = 0;
        const char *uname = MatchmakerGetUsername();
        if (uname == NULL || uname[0] == '\0') {
            uname = "host";
        }
        net_deco_set_name(entry, uname);
        net_set_slot_name(0, uname);
        net_set_slot_platform(0, MM_PLATFORM, (uint8_t)platform_get_region());

        DebugLog("Network session created (host, port %d)\n", NET_PORT_DEFAULT);
        return 1;
    }
    DebugLog("CreateNetworkSession failed\n");
    return 0;
}

/* 0x4870A8 — Client: discover host via broadcast, then connect */
int JoinNetworkSession(const char *name, int enumIdx)
{
    (void)name; (void)enumIdx;
    char hostIp[64];

    /* Direct connect via --host IP */
    if (g_cmdHostIP != NULL) {
        strncpy(hostIp, g_cmdHostIP, sizeof(hostIp) - 1);
        hostIp[sizeof(hostIp) - 1] = '\0';
    }
    /* LAN discovery fallback */
    else {    
        if (net_discover_check(hostIp, sizeof(hostIp)) != 1) {
            DebugLog("JoinNetworkSession: no host found yet\n");
            return 0;
        }
    }

    if (net_client_connect(hostIp, NET_PORT_DEFAULT) == 0) {
        /* Send join request so host assigns us a slot.  Payload carries our
         * matchmaker username so the host (and via SLOT_ASSIGN/PEER_NAME, the
         * other clients) can populate the decoration table with real names. */
        struct {
            int     hdr;
            char    name[MM_MAX_USERNAME];
            char    platform[16];
            uint8_t region;
        } joinReq;
        memset(&joinReq, 0, sizeof(joinReq));
        joinReq.hdr = NET_MSG_JOIN_REQ;
        const char *uname = MatchmakerGetUsername();
        if (uname == NULL) {
            uname = "";
        }
        strncpy(joinReq.name, uname, MM_MAX_USERNAME - 1);
        strncpy(joinReq.platform, MM_PLATFORM, sizeof(joinReq.platform) - 1);
        joinReq.region = (uint8_t)platform_get_region();
        net_send_to_host(&joinReq, sizeof(joinReq));
        DebugLog("Joined session at %s:%d as '%s'\n", hostIp, NET_PORT_DEFAULT, joinReq.name);
        return 1;
    }
    DebugLog("JoinNetworkSession: connect failed\n");
    return 0;
}

/* 0x486C5C — Kick off LAN discovery broadcast */
void EnumDirectPlaySessions(void)
{
    net_discover_send(NET_PORT_DEFAULT);
    /* Binary: DirectPlay enumeration callback set g_resultsUnlockFlag when
     * sessions were found. SDL: set it unconditionally so the join path
     * in state 1 can call JoinNetworkSession. */
    g_resultsUnlockFlag = 1;
}

/**
 * BuildCameraView — 0x00423574 — 1569 bytes
 * Camera setup only. Computes camera world position, builds view matrix.
 * The actual D3D rendering (sky, track, objects, HUD) is in RenderHUD.
 */
extern void BuildCameraView(Player *player, CamStateEntry *cameraParams,
                            void *renderCam, void *smoothedCam);
extern void UpdateCameraConfig(CamStateEntry *camState, int flags);
extern void SetViewportClipRect(int *camStruct);

void SetupSpecialRace(void);            /* 0x00471AA0 */

/* g_viewportLayoutMode is g_numHumans at 0x6E9908 — use the canonical name */
#define g_viewportLayoutMode g_numHumans

 /*===========================================================
 * BuildCameraViewForViewport — calls BuildCameraView (0x423574) with
 * the correct per-viewport parameters from the binary's global arrays.
 *
 * Binary parameters (Watcom fastcall):
 *   EAX = player pointer (&g_playerBase[vp])
 *   EDX = camera state (g_camStateTable + vp * 40, at 0x902140)
 *   EBX = viewport config / render cam output (g_viewportConfigArray + vp * 0xC8, at 0x6E9924)
 *   ECX = same as EDX (smoothedCam = cameraState, binary never reads ECX separately)
 *
 * Also calls UpdateCameraConfig before BuildCameraView, and
 * SetViewportClipRect( after, matching the binary's call sequence.
 *=========================================================== */
void BuildCameraViewForViewport(int vpIdx)
{
    Player *player = &g_playerBase[vpIdx];
    if (player == NULL) {
        return;
    }

    /* g_viewportIndex (0x6E991C) is the split-screen ORIENTATION flag, not a
     * viewport counter: 1 = vertical split or single-player, 0 = horizontal
     * split. All three writes in the binary are in SetupViewportConfig
     * (0x4CBA42 entry = 1, 0x4CBABA vertical = 1, 0x4CBB4C horizontal = 0);
     * the other 36 references are reads. BuildCameraView reads it to pick the
     * camDist push-back (0x4235E4: +0x200 vertical, 0x4235F2: +0x300
     * horizontal) and the relY pitch tweak, both of which are per-orientation
     * and must be identical for every player in the same split. Assigning the
     * loop counter here gave player 2 the vertical-split camera in a
     * horizontal split. */

    CamStateEntry *camState = &g_camStateTable[vpIdx];
    int *vpConfig = (int *)((char *)g_viewportConfigArray + vpIdx * 0xC8);

    /* Init detail level on first call (binary: 0x902140[5] = 0x200000) */
    if (camState->fovDetail == 0) {
        camState->fovDetail = 0x20;
    }

    /* Binary calls UpdateCameraConfig SEPARATELY from BuildCameraView,
     * guarded by g_demoMode==0 && g_introCountdown==0, with per-player
     * input flags from g_perPlayerInput[]. See 0x4CF42D-0x4CF4A5.
     * Do NOT call UpdateCameraConfig here — it's handled in the main loop. */
    BuildCameraView(player, camState, (void *)vpConfig, (void *)camState);
    SetViewportClipRect(vpConfig);

    /* Make render camera globally accessible */
    g_currentRenderCam = vpConfig;
}

/* UpdatePerViewportCamera — binary 0x4CF638-0x4CF6A7.
 *
 * In the binary, this loop IS the main camera loop (inlined in WinMain).
 * The binary has ONE loop that either calls BuildCameraView (g_demoMode==0)
 * or UpdateFlyoverCamera (g_demoMode!=0). Our main loop at lines 1278-1284
 * already handles the g_demoMode==0 path (BuildCameraViewForViewport), so
 * this function only needs to handle the g_demoMode!=0 path.
 *
 * Previously this called BuildCameraViewForViewport when g_demoMode==0,
 * causing BuildCameraView to run TWICE per frame (double camera smoothing).
 */

 /*===========================================================
 * UpdatePerViewportCamera — binary 0x4CF638-0x4CF6A7.
 * Per-viewport camera loop. In the binary, this runs AFTER the main
 * BuildCameraView calls (lines 620-634). For viewports 1+:
 *   g_demoMode == DEMO_NONE: calls BuildCameraView (normal race)
 *   g_demoMode != DEMO_NONE: calls UpdateFlyoverCamera (pre-race flyover)
 *
 * For single-player (1 viewport), this loop body runs for viewport 0
 * and either re-runs BuildCameraView or runs the flyover camera.
 *=========================================================== */
void UpdatePerViewportCamera(void)
{
    if (g_demoMode == DEMO_NONE) {
        return;  /* already handled by main loop */
    }

    int numVP = g_viewportLayoutMode;

    for (int vp = 0; vp < numVP; vp++) {
        Player *player = &g_playerBase[vp];
        CamStateEntry *camState = &g_camStateTable[vp];
        RenderCamera *vpConfig = (RenderCamera *)((char *)g_viewportConfigArray + vp * 0xC8);

        UpdateFlyoverCamera(player, camState, vpConfig,
                            g_waypointTablePtr, 0, vp);
    }
}

/**
 * SpawnTAParticles — 0x00486714 — 438 bytes
 * TA sparkle particle spawner. Called only when g_raceType==2 && g_raceSubMode<2.
 * Spawns 3 sparkle particles per frame into g_collectEffectBuf at nearby
 * track waypoint positions with random scatter offsets.
 * Normal mode: sparkles appear AHEAD of player along track.
 * Reverse mode: sparkles appear BEHIND player (ahead in reverse direction).
 */
void SpawnTAParticles(void)
{
    Player *p0 = &g_playerBase[0];
    int wpCount = g_posDataCount4;                              /* 0x48671f */
    int searchZ = p0->posZ >> 12;                               /* 0x486725 */
    int searchY = -(p0->posY) >> 12;                            /* 0x48672b-42: neg ebx; sar 0xc */
    int searchX = p0->posX >> 12;                               /* 0x48673c-45 */

    int baseWpIdx = FindNearestWaypoint(                        /* 0x48674a */
        (int *)g_splineWaypoints, searchX, searchY, searchZ, wpCount);

    for (int i = 0; i < 3; i++) {                                  /* 0x486880-84 */
        /* Pick a random waypoint offset */
        int randVal = Random() / 1024;                          /* 0x486886-95 */
        int wpIdx;

        if (g_raceSubMode != 0) {                               /* 0x4868a0-a2: Reverse */
            wpIdx = baseWpIdx - randVal;                        /* 0x486756-58 */
            if (wpIdx < 0) wpIdx += wpCount;                   /* 0x48675e-60 */
        }
        else {                                                /* Normal */
            wpIdx = randVal + baseWpIdx;                        /* 0x4868ae */
            if (wpIdx >= wpCount) wpIdx -= wpCount;            /* 0x4868b1-b9 */
        }

        /* Get waypoint position */
        int *wp = (int *)((char *)g_splineWaypoints + wpIdx * 12); /* 0x48676d-76 */

        CollectEffect *part = &g_collectEffectBuf[g_raceCounterA0]; /* 0x486766 */

        /* X position: (wpX << 8) + Random()/2 - 0x2000 */
        int baseX = wp[0] << 8;                                 /* 0x486778-7a */
        int scatterX = Random() / 2 - 0x2000;                  /* 0x486780-97 */
        part->posX = baseX + scatterX;                          /* 0x4867a6 */

        /* Y position: ((-wpY) << 8) + Random()/2 - 0x2000 */
        int baseY = -(wp[1]) << 8;                              /* 0x4867a8-ae */
        int scatterY = Random() / 2 - 0x2000;                  /* 0x4867b4-c5 */
        part->posY = baseY + scatterY;                          /* 0x4867d1 */

        /* Z position: (wpZ << 8) + Random()/2 - 0x2000 */
        int baseZ = wp[2] << 8;                                 /* 0x4867d4 */
        int scatterZ = Random() / 2 - 0x2000;                  /* 0x4867d8-e4, 0x48682b */
        part->posZ = baseZ + scatterZ;                          /* 0x486838 */

        /* Zero velocity */
        part->velX = 0;
        part->velY = 0;
        part->velZ = 0;
        part->accelY = 0;

        /* Particle parameters */
        part->halfW = 0x20;                                     /* 0x486825: size = 32 */
        part->lifetime = 0x0A;                                  /* 0x486802: lifetime = 10 */
        part->timer = 0;
        part->animEnd = 0x40;
        part->animFrameW = 3;
        part->animFrameH = 3;
        part->billboardSize = 0x18;
        part->animDiv = 0x10;

        /* Render mode flag: D3D = 0x40 (binary: soft=0, d3d=0x40) */
        part->uvBaseX = 0x40;                                   /* 0x486852 */
        part->uvBaseY = 0x60;                                   /* 0x48686a */
        part->tpage = *(unsigned char *)&g_tpageCharBase;       /* 0x486856: tpage */
        part->uvSpan = 0x10;                                    /* 0x486863 */

        /* Advance write index (circular, 64 entries) */
        g_raceCounterA0++;                                      /* 0x486869 */
        if (g_raceCounterA0 == 0x40) g_raceCounterA0 = 0;      /* 0x486873-7a */
    }
}

/**
 * PostRaceHandling2P — 0x004C92B4 — 103 bytes
 * Called from WinMain when g_raceOrder[0] == 1 (binary 0x4CF3B7).
 * Decrements g_autoSteerFlag, runs collision/AI/physics for player1.
 */
void PostRaceHandling2P(void)
{
    if (g_introCountdown != 0) {
        return;
    }
    if (g_autoSteerFlag != 0) {
        g_autoSteerFlag--;
    }

    Player *player0 = &g_playerBase[0];
    Player *player1 = &g_playerBase[1];

    SweepPlayerCollision(player0, player1);                      /* 0x4201a0 */
    ResolvePlayerCollision(player0, player1);                    /* 0x4d56b8 */
    AdjustRubberBandAI();                                        /* 0x421270 */
    TrackSurfaceAI(player1);                                     /* 0x41e6dc */
    PlayerPhysicsMain(player1);                                  /* 0x4206b8 */
    UpdatePlayerMovement(player1);                               /* 0x4d8ab8 */
}

/* WinMain @ 0x004CDFC4 — 8254 bytes */
int main(int argc, char *argv[])
{
    /* Parse command-line options */
    const char *dataDir = DATA_DIR;

    static struct option long_opts[] = {
        {"host", required_argument, NULL, 'h'},
        {"port", required_argument, NULL, 'p'},
        {"unlock", no_argument, NULL, 'u'},
        {"fullscreen", no_argument, NULL, 'f'},
        {"username", required_argument, NULL, 'n'},
        {NULL, 0, NULL, 0}};
    int opt;
    while ((opt = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
        switch (opt)
        {
            case 'h':
                strncpy(s_hostIPArg, optarg, sizeof(s_hostIPArg) - 1);
                s_hostIPArg[sizeof(s_hostIPArg) - 1] = '\0';
                g_cmdHostIP = s_hostIPArg;
                break;
            case 'p':
                s_cmdPort = atoi(optarg);
                break;
            case 'u':
                s_cmdUnlock = 1;
                break;
            case 'f':
                s_cmdFullscreen = 1;
                break;
            case 'n':
                strncpy(s_cmdUsername, optarg, sizeof(s_cmdUsername) - 1);
                s_cmdUsername[sizeof(s_cmdUsername) - 1] = '\0';
                MatchmakerSetFallbackUsername(s_cmdUsername);
                break;
        }
    }
    if (optind < argc) {
        dataDir = argv[optind];
    }

    /* Get EXE directory, set as working directory
     * Original: GetModuleFileNameA, strip trailing \\, SetCurrentDirectoryA.
     * Faithful to that, with a dev override: an explicit data-path argument
     * wins; otherwise chdir to the executable's own directory (so the binary
     * can sit in the data folder and launch from anywhere, including a
     * double-click). DC/web have no exe-dir concept, so platform_base_path()
     * returns NULL there and we fall back to DATA_DIR (/cd, /pc, .). */
    if (optind < argc) { /* explicit data path wins */
        if (chdir(dataDir) != 0) {
            fprintf(stderr, "Cannot chdir to data directory: %s\n", dataDir);
            return 1;
        }
    }
    else {
        const char *base = platform_base_path();
        const char *target = base ? base : dataDir;
        if (chdir(target) != 0) {
            fprintf(stderr, "Cannot chdir to data directory: %s\n", target);
            return 1;
        }
    }

    /* Existence test via fopen (access() is absent from the KOS/newlib
     * libc used by the Dreamcast build). fOpen is case-insensitivity-aware
     * on desktop, so a mixed/lowercase data folder is accepted too. */
    FILE *probe = fOpen(PATH_GENERAL_BIT, "rb");
    if (!probe) {
        fprintf(stderr,
                "Game data not found here. Place the binary in the data folder, "
                "or pass the data path as an argument, and try again.\n");
        return 1;
    }
    fclose(probe);

    /* Save menu settings on any exit (normal, window close, SCREEN_QUIT) */
    atexit(SaveGameSettings);

    /* InitTimerSystem();
     * Original inits the Win32 multimedia timer.
     * SDL: SDL_GetTicks provides ms since SDL_Init, no setup needed. */

    /* *(0x6e9cd8) = *(0x6e9cd4) / 0x1e;
     * Timer tick calibration. Not needed — we use SDL_GetTicks directly. */

    InitFileIO();

    InitInputMappings();

    /* loads SONICR.INF (saved options) */
    LoadGameSettings();

    InitJoystick();

    g_doubleWidthFlag = 0;                      /* 0x8fd490 */

    /* memset_zero — clears a block of globals */
    /* TODO: identify what region this clears and replicate */

    InitDebugLog();

    /* Win32 instance handles, command line
     * g_hInstance = param_1; etc.
     * Not applicable. The original stores hInstance for
     * DirectInput, DirectPlay, and window creation. We don't need these.
     * _DAT_006d99a0 = &LAB_004d0004 — WndProc callback pointer.
     * _DAT_006d999c = 0x203 — window class style (CS_HREDRAW|CS_VREDRAW|CS_OWNDC). */

    /* RegisterClassA, CreateWindowExA, ShowWindow
     * Creates fullscreen Win32 window sized to full screen.
     * Platform-specific window/GL/audio init lives in platform_*.c. */
    if (platform_init(640, 480, s_cmdFullscreen, "Sonic R") != 0) {
        return 1;
    }

#if 0
def SONICR_DC /* DC splash screen */
    /* DC splash screen — draw immediately after PVR init, stays visible
     * while the rest of the engine loads. */
    {
        #include <dc/pvr.h>
        #define SPLASH_W 512
        #define SPLASH_H 512
        #define SPLASH_HDR 16
        #define SPLASH_SIZE (SPLASH_W * SPLASH_H * 2)

        pvr_ptr_t tex = pvr_mem_malloc(SPLASH_SIZE);
        if (tex) {
            FILE *fp = fopen("/pc/splash.tex", "rb");
            if (fp) {
                void *buf = malloc(SPLASH_HDR + SPLASH_SIZE);
                if (buf) {
                    fread(buf, 1, SPLASH_HDR + SPLASH_SIZE, fp);
                    pvr_txr_load((uint8_t *)buf + SPLASH_HDR, tex, SPLASH_SIZE);
                    free(buf);
                }
                fclose(fp);

                pvr_poly_cxt_t cxt;
                pvr_poly_hdr_t hdr;
                pvr_vertex_t v;

                pvr_poly_cxt_txr(&cxt, PVR_LIST_PT_POLY,
                                 PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED,
                                 SPLASH_W, SPLASH_H, tex, PVR_FILTER_BILINEAR);
                pvr_poly_compile(&hdr, &cxt);

                float x0 = (640 - SPLASH_W) / 2.0f;
                float y0 = (480 - SPLASH_H) / 2.0f;
                float x1 = x0 + SPLASH_W;
                float y1 = y0 + SPLASH_H;

                for (int _f = 0; _f < 2; _f++) {
                    pvr_scene_begin();
                    pvr_list_begin(PVR_LIST_PT_POLY);
                    pvr_prim(&hdr, sizeof(hdr));

                    v.flags = PVR_CMD_VERTEX;
                    v.z = 1.0f; v.argb = 0xFFFFFFFF; v.oargb = 0;

                    v.x = x0; v.y = y0; v.u = 0.0f; v.v = 0.0f;
                    pvr_prim(&v, sizeof(v));
                    v.x = x1; v.y = y0; v.u = 1.0f; v.v = 0.0f;
                    pvr_prim(&v, sizeof(v));
                    v.x = x0; v.y = y1; v.u = 0.0f; v.v = 1.0f;
                    pvr_prim(&v, sizeof(v));
                    v.flags = PVR_CMD_VERTEX_EOL;
                    v.x = x1; v.y = y1; v.u = 1.0f; v.v = 1.0f;
                    pvr_prim(&v, sizeof(v));

                    pvr_list_finish();
                    pvr_scene_finish();
                }

                thd_sleep(3000);
            }
            pvr_mem_free(tex);
        }

        #undef SPLASH_W
        #undef SPLASH_H
        #undef SPLASH_HDR
        #undef SPLASH_SIZE
    }
#endif /* DC splash screen */

    if (net_transport_init() != 0) {
        fprintf(stderr, "net_transport_init failed\n");
#ifndef __EMSCRIPTEN__
        return 1;
#endif
    }
#ifdef SONICR_DC
    MatchmakerSetFallbackUsername("Dreamcast");
#endif

    g_diDeviceReady = 1;                                   /* enable keyboard polling */

    /* load SFX WAV files */
    InitDirectSound();

    InitSonicR_A();

    /* Load saved key mappings (KEYS.BIN) */
    LoadKeyMappings();

    InitSineTable();

    InitRandomTables();

    g_ringSpawnReadPtr = g_randomRingBuffer;

    g_randomRingIdx = 0;

    InitSaveData();

    InitPadTypes();

    /* ExamineMachine / D3D vs Software detection
     * Original probes the machine for D3D capability, releases COM objects.
     * We select mode 2, the native software rasteriser: its branches are the
     * ones this port translates, including the 13-bit additive colour path.
     * The OpenGL backend is how those branches reach the screen — it does not
     * make us the binary's D3D mode. */
    g_renderMode = RENDER_SOFT;

    /* InitD3DApp, CreateD3DApp
     * Original creates DirectDraw surfaces, D3D device, viewport.
     * Replaced by OpenGL context above. Set screen dimensions from it. */
    g_screenWidth = 640;
    g_screenHeight = 480;
    g_screenHeightBase = 480;   /* render height is derived from this per mode */
    g_bitsPerPixel = 16;
    g_surfaceStride = 640;

    LoadGameState();

    SetScreenDimensions();          /* 0x4CBEFC — sets g_dispCenterX/Y etc */

    InitCDPlayer();

    ApplyViewportGeometry();        /* was ComputeViewportBounds + SetupViewportConfig */

    /*
     * Original creates DirectInput device for keyboard/joystick.
     * SDL: keyboard input via SDL_PollEvent in the event loop.
     */
    InitDirectInput();

    /* Screen sequence */

    int screenResult;
    int subScreenResult;

    /* Multiplayer human-player count — the binary's [ebp-0x10], a slot kept
     * separate from every screen return code. Written from MultiPlayerScreen
     * (0x4CE693) and re-seeded on retire (0x4CFA57); read only where the three
     * count globals are set (0x4CE750). Screen return codes must never land
     * here: SCREEN_BACK is 0, so routing one through this would zero the
     * viewport count and skip every per-player init loop downstream. */
    int mpPlayerCount = 0;

title_sequence:
    InitCD();
    ResetInputState();

    screenResult = SegaLogoScreen();
    if (screenResult == SCREEN_QUIT) {
        return 0;
    }
    if (screenResult == SCREEN_EXIT) {
        return 0;
    }

    screenResult = TravellersTalesLogoScreen();
    if (screenResult == SCREEN_QUIT) {
        return 0;
    }
    if (screenResult == SCREEN_EXIT) {
        return 0;
    }

title_screen_entry:
    ResetInputState();
    InitTitleScreen();
    DebugLog("RenderMode=RENDER_SOFT\n");
    DebugLog("SWIDTH=%i SHEIGHT=%i SBPP=%i TW=%i\n",
             g_screenWidth, g_screenHeight, g_bitsPerPixel, g_surfaceStride);

    /* DEBUG: test CreditsScreen — remove when done */
#if 0
    {
        extern void InitAnimData(void);
        InitAnimData();                             /* populate g_charAnimTables */
        g_gpResultFlag = 1;                         /* show emeralds slide */
        for (int i = 0; i < 7; i++)
            g_charUnlockSource[i] = 2;              /* all chars unlocked for step 0x24 */
        /* Also copy to charUnlockTable so the loop sees them */
        for (int i = 0; i < 7; i++)
            g_charUnlockTable[i] = 2;
        g_playerBase->charId = 0;                  /* Sonic for the per-char slide */
        g_playerBase->animId = 0;                  /* default anim slot */
        g_numPlayers = 1;                          /* RunCreditsStep loops up to g_numPlayers */
        /* Copy viewport slot 0 into slots 4 and 5 (used by credits render) */
        for (int i = 0; i < 21; i++) {
            g_viewportArray[4*21 + i] = g_viewportArray[i];
            g_viewportArray[5*21 + i] = g_viewportArray[i];
        }
        /* Seed g_animDataPtrs[] for the credits 3D character renders.
         * Mirrors the per-viewport setup ResultsScreen does at 0x4c78e6
         * — without this, AdvancePlayerAnimation reads a NULL/stale frame
         * stream and limbs end up at uninitialized rotation angles. */
        {
            for (int i = 0; i < g_numPlayers; i++) {
                int charId = g_playerBase[i].charId;
                int animId = g_playerBase[i].animId;
                void **animTable = (void **)((void ***)g_charAnimTables)[charId * 2];
                if (animTable == NULL) continue;
                const short *fs = (const short *)animTable[animId];
                if (fs == NULL) continue;
                g_animDataPtrs[i] = fs;
                g_playerBase[i].animFrameIdx = (int)*fs - 1;
            }
        }
        CreditsScreen();
        return 0;
    }
#endif

    screenResult = TitleScreen();
    if (screenResult == SCREEN_QUIT) {
        return 0;
    }
    if (screenResult == SCREEN_EXIT) {
        Shutdown();
        return 0;
    }
    if (screenResult != 2) {
        goto main_menu_init;
    }
    g_demoMode = DEMO_TITLE;

    if (g_demoMode != DEMO_TITLE) {
        g_demoMode = DEMO_NONE;
        goto race_setup;
    }
    screenResult = AutoSelectDemo();
    if (screenResult != 0) {
        goto race_setup;
    }
    g_demoMode = DEMO_NONE;

main_menu_init:
    g_netSessionActive = 0;
    ResetInputState();
    InitOptionStuff();

main_menu_loop:
    if (s_cmdUnlock) {
        g_gpAllTracksFlag = 1;          /* unlock Radiant Emerald in course select */
        /* Debug unlock (-u): all characters selectable + Super Sonic toggle.
         * Deliberately NOT setting g_charUnlockState so the per-track emerald
         * tokens still spawn (those are hidden when state==2). */
        for (int ui = 0; ui < 10; ui++) {
            g_charUnlockTable[ui] = 2;
        }
        g_allCharsUnlocked = 2;
        g_superSonicSeed = 0x28;     /* seed char-select default to Super Sonic */
    }

    do {
        g_trackId = TRACK_NONE;
        g_netSessionActive = 0;
        g_isNetworkGame = 0;
        g_numHumans = 1;
        g_numViewports = 1;
        ApplyViewportGeometry(); /* restore full-screen viewport after split-screen */
        g_numPlayers = 5;

        screenResult = MainMenuScreen();
        if (screenResult == SCREEN_QUIT) {
            return 0;
        }
        if (screenResult == SCREEN_BACK) {
            StopCD();
            goto title_screen_entry;
        }
        if (screenResult == SCREEN_EXIT) {
            Shutdown();
            return 0;
        }
        if (screenResult == SCREEN_TITLE) {
            StopCD();
            goto title_sequence;
        }
        if (screenResult == SCREEN_OK) {
            /* Single-player Grand Prix — set mode, fall through to shared charsel/coursesel */
            /* Binary: screenResult==1 falls through all cmp/je checks to 0x4bf5a4 */
            g_raceSubMode = SUBMODE_NORMAL;     /* 0x4bf5a4: mov [0x8FB954], edi (edi=0) */
            g_raceType = RACE_GP;                     /* 0x4bf5aa: mov [0x8FB950], edi */
            g_numPlayers = 5;
            goto char_select;                  /* 0x4bf5b0: jmp 0x4bf66e */
        }
        if (screenResult == SCREEN_TA) {
            goto time_attack_select;
        }
        if (screenResult == SCREEN_MP) {
            g_raceType = RACE_MULTIPLAYER;
            subScreenResult = MultiPlayerScreen();
            if (subScreenResult == SCREEN_QUIT) {
                return 0;
            }
            if (subScreenResult == SCREEN_BACK) {
                continue;       /* 0x4ce6a9: je 0x4ce4fa (back → main menu) */
            }
            if (subScreenResult == SCREEN_EXIT) {
                Shutdown(); return 0;
            }
            if (SCREEN_OK < (int)subScreenResult) {
                mpPlayerCount = subScreenResult;   /* 0x4CE693 */
                goto multiplayer_mode_select;
            }
            /* 0x4ce6d5: cmp eax,2 / 0x4ce6d8: jl 0x4ce4fa — the lobby
             * countdown expired with fewer than two humans seated, so the
             * match never starts. 0x4ce4fa is the top of the main-menu loop,
             * the same target the BACK case above takes. Without this the
             * one-player return code falls out of the SCREEN_MP block into
             * `goto time_attack_done`, whose break lands on char_select. */
            continue;
        }
        else if (screenResult == SCREEN_OPTIONS) {
            /* OPTIONS LOOP — 0x493BDC loop */
            for (;;) {
                int optResult = OptionsMenuScreen();
                if (optResult == SCREEN_QUIT) {
                    return 0;
                }
                if (optResult == SCREEN_EXIT) {
                    Shutdown();
                    return 0;
                }
                if (optResult == SCREEN_TITLE) {
                    StopCD();
                    goto title_sequence;
                }
                if (optResult == SCREEN_LOADSAVE) {
                    /* Load/Save Data */
                    int lsdResult = LoadSaveScreen();
                    if (lsdResult == SCREEN_QUIT) {
                        return 0;
                    }
                    continue;  /* back to OptionsMenuScreen */
                }
                if (optResult == SCREEN_TIMES) {
                    /* TimeRankingScreen (Records) */
                    int lsResult = TimeRankingScreen();
                    if (lsResult == SCREEN_QUIT) {
                        return 0;
                    }
                    continue;  /* back to OptionsMenuScreen */
                }
                break;  /* optResult == 1 (back) return to main menu */
            }
            continue;  /* go back to main_menu_loop (main menu loop) */
        }
        else if (screenResult == SCREEN_NET) {
network_screen_entry:
            g_isNetworkGame = 0;
            screenResult = NetworkScreen();
            if (screenResult == SCREEN_TITLE) {
                StopCD();
                goto title_sequence;
            }
network_result_dispatch:                                   /* 0x4ce767: network result dispatch (also reached from post-race) */
            if (screenResult == SCREEN_QUIT) {
                return 0;
            }
            if (screenResult == SCREEN_BACK) {                   /* 0x4CE77A: test eax; je 0x4CE4FA */
                goto main_menu_loop;              /* back to main dispatch */
            }
            g_netSessionActive = 1;                /* 0x4CE785 */
            g_isNetworkGame = 1;                /* 0x4CE790 */
            g_raceType = RACE_MULTIPLAYER;                     /* 0x4CE796 */
            g_numHumans = 1;                    /* 0x4CE79C */
            g_numViewports = g_netPlayerCount;
            g_numPlayers = g_netPlayerCount;
            goto race_setup;
        }

        goto time_attack_done;

time_attack_select:
        g_raceType = RACE_TIMEATTACK;
        screenResult = TimeAttackModeSelect();
        if (screenResult == SCREEN_QUIT) {
            return 0;
        }
        if (screenResult == SCREEN_BACK) {
            goto main_menu_loop;
        }
        if (screenResult == SCREEN_EXIT) {
            Shutdown();
            return 0;
        }
        if (screenResult == SCREEN_TITLE) {
            StopCD();
            goto title_sequence;
        }
        g_numPlayers = 1;
        g_numViewports = 1;
        if (screenResult == SCREEN_TA_NORMAL) {
            g_raceSubMode = SUBMODE_NORMAL;
        }
        else if (screenResult == SCREEN_TA_REVERSE) {
            g_raceSubMode = SUBMODE_REVERSE;
        }
        else if (screenResult == SCREEN_TA_BALLOON) {
            g_raceSubMode = SUBMODE_BALLOON;
        }
        else if (screenResult == SCREEN_TA_TAG) {
            g_raceSubMode = SUBMODE_TAG;
            g_numPlayers = 5;
        }
time_attack_done:
    if (g_nextScreenId != SCREEN_MP_MODE_SELECT) {
        break;
    }

    goto multiplayer_mode_select;                          /* 0x4ce5b5: g_nextScreenId==7 mode select */
multiplayer_mode_select:
        screenResult = MultiPlayerModeSelect();
        if (screenResult == SCREEN_QUIT) {
            return 0;
        }
        if (screenResult == SCREEN_BACK) {
            continue;              /* 0x4ce6f6: je 0x4ce4fa (main menu) */
        }
        if (screenResult == SCREEN_EXIT) {
            Shutdown();
            return 0;
        }
        if (screenResult == SCREEN_TITLE) {
            StopCD();
            goto title_sequence;
        }
        if (screenResult == SCREEN_MP_NORMAL) {
            g_raceSubMode = SUBMODE_NORMAL;
        }
        else if (screenResult == SCREEN_MP_BALLOON) {
            g_raceSubMode = SUBMODE_BALLOON;
        }
        /* 0x4CE750-0x4CE75D: all three counts come from the dedicated slot,
         * not from whatever the last screen returned. */
        g_numHumans = mpPlayerCount;
        g_numPlayers = g_numHumans;
        g_numViewports = g_numHumans;
        goto char_select;                      /* 0x4ce762: jmp 0x4ce66e */
    } while (1);

    /* Shared CharacterSelect -> CourseSelect (0x4CE66E-0x4CE984) */

char_select:                                   /* 0x4ce66e */
    subScreenResult = CharacterSelectScreen();        /* 0x4ce66e: call 0x48c968 */
    if (subScreenResult == SCREEN_QUIT) {
        return 0;
    }
    if (subScreenResult == SCREEN_EXIT) {
        Shutdown();
        return 0;
    }
    if (subScreenResult == SCREEN_TITLE) {
        StopCD();
        goto title_sequence;
    }
    if (subScreenResult == SCREEN_BACK) {                       /* 0x4ce86c: back pressed */
        /* 0x4ce870: dispatch on g_nextScreenId */
        if (g_nextScreenId == SCREEN_TA_MODE_SELECT) {
            goto time_attack_select;
        }
        if (g_nextScreenId == SCREEN_MP_MODE_SELECT) {
            goto multiplayer_mode_select;
        }
        goto main_menu_loop;
    }
    subScreenResult = CourseSelectScreen();           /* 0x4ce88d: call 0x48de34 */
    if (subScreenResult == SCREEN_QUIT) {
        return 0;
    }
    if (subScreenResult == SCREEN_EXIT) {
        Shutdown();
        return 0;
    }
    if (subScreenResult == SCREEN_TITLE) {
        StopCD();
        goto title_sequence;
    }
    if (subScreenResult == SCREEN_BACK) {
        goto char_select;      /* 0x4ce8a5: back, charsel again */
    }

    /* 0x4ce8e2-0x4ce91e: TA ghost setup — load ghost file, bump to 2 players
     * if ghost data exists, record which character the ghost will be. */
    if (g_ghostToggle == 1 && g_raceType == RACE_TIMEATTACK               /* 0x4ce8e2-0x4ce8fc */
        && g_raceType > g_raceSubMode) {                                  /* cmp eax,[0x8FB954] */
        LoadGhostData();                                         /* 0x4ce8fe */
        if (g_ghostDataExists != 0) {                            /* 0x4ce903 */
            g_numPlayers = 2;                                    /* 0x4ce90c */
        }
        g_ghostCharId = g_playerBase[0].charId;                  /* 0x4ce912-0x4ce919 */
    }

    /* 0x4ce95c */
    g_netSessionActive = 0;

    /* Race setup */

race_setup:
    ApplyViewportGeometry();                       /* 0x4ce985: call 0x4cba28 */
    if (g_netSessionActive == 0) {
        LoadTrackSinglePlayer();
    }
    if (g_netSessionActive != 0) {
        if (g_netGameStarted == 0) {
            for (int i = 0; i < 12; i++) {
                g_netGameInfoDest[i] = g_netLobbyConfigBuf[i]; /* 0x68A8CC */
            }
        }
        g_trackId = g_trackIdTable[g_netTrackIndex];
        g_raceSubMode = g_netRaceSubModeIndex * 3;
        g_timeOfDay = (int)g_netPlayerMode;
        g_weatherType = (int)g_netWeatherType;
        /* Host runs state 1, client state 3 (client takes the
         * UpdateNetworkClient path). g_netGameStarted is the host/client
         * discriminator — 1 on the host, 0 on clients. */
        if (g_netGameStarted != 0) {
            g_netGameStartState = 1;
        }
        else {
            g_netGameStartState = 3;
        }
        g_netSavedButtonByte = g_netGameInfoDest[11] >> 0x10; /* 0x4CEA33/41 */
    }

    InitLevel();
    DebugLog("InitLevel OK\n");

    if (g_trackId != TRACK_RADIANT_EMERALD) {                        /* 0x4cecb5-0x4cecbc */
        InitWeather();                      /* 0x4cecbe: calls InitSplitScreenLayout + rain/snow particle init */
    }
    g_objectRenderEnable = 1;
    g_triggerFlag = 0;
    g_raceState2ec = 0;
    g_raceState2f8 = 0;
    g_raceState2fc = 0;
    g_raceState300 = 0;
    g_raceState304 = 0;
    g_raceState308 = 0;
    g_raceState30c = 0;
    g_raceState310 = 0;
    g_raceState314 = 0;
    g_ringAnimFrame = 0;
    g_emeraldAnimFrame1 = 0;
    if (g_emeraldRenderFlag == 1) {
        g_renderFlags = 0x40;
    }
    else {
        g_renderFlags = 0;
    }
    g_minimapToggle = g_minimapConfig;
    g_triggerObjectIndex = 0;
    g_totalFrames = 0;
    g_clipLeftDouble = 0;
    g_clipLeft = 0;
    g_clipTop = 0;
    g_clipRight = g_screenWidth - 1;
    g_sortListOffset = 0;
    g_clipBottom = g_screenHeight - 1;
    g_screenWidthFull = g_screenWidth;
    g_screenCenterX = g_screenWidth / 2;
    g_screenHeightFull = g_screenHeight;
    g_screenCenterY = g_screenHeight / 2;
    g_renderEnabled = 1;
    g_vpClipLeft10 = 0;
    g_vpClipRight10 = g_screenWidth * 0x400 - 1;
    g_vpClipLeft16 = 0;
    g_vpClipRight16 = g_screenWidth * 0x10000 - 1;
    g_projScaleX = (g_screenWidth * 0x100) / 0x140;
    g_projScaleY = (g_screenHeight * 0x100) / 0xf0;
    g_vpParam0F = 0;
    g_parallaxWidthDouble = g_parallaxWidth * 2;
    g_fadeState = FADE_IN;
    g_vpParam10 = 0;
    g_aiGridAngleOffset = 0;
    g_fadeLevel = -0x100;
    if (g_demoMode == DEMO_NONE) {
        g_ghostMaxFrames = (int)(0x8000 / (long long)(int)g_numViewports);
    }
    g_cdPlaybackState = 0;
    g_projScaleXCurrent = g_projScaleX;
    SelectCDTrack();
    g_netFrameCounter = 0;
    g_netDisconnectFlag = 0;
    g_netSyncEstablished = 0;
    g_netPlayerAlive[0] = 0;
    g_netPlayerAlive[1] = 0;
    g_netPlayerAlive[2] = 0;
    g_netPlayerAlive[3] = 0;
    /* Binary: client (state==3) waited for keepalive handshake before advancing
     * past SET. SDL: connection is already confirmed via NET_MSG_START_GAME
     * before the race loop begins, so all players are ready immediately. */
    g_netReadyFlag = 1;

    /* Initialize race — 0x471A04.
     * Sets intro countdown (0xD2), lap times, AI setup, player physics.
     * InitRaceStart -> InitAIConfig -> ComputeBaseSpeed properly computes
     * g_baseSpeedFactor from difficulty/character/track. */
    InitRaceStart();

    /* Player yaw is set per-track by player_init.c (s_trackConfigTable[trackId][1]).
     * Resort Island = 0xC00, Radical City = 0xE40, etc. (from ROM at 0x4FEB00).
     * Just need to also set player+0x70 (current facing yaw) to match player[4] (target yaw),
     * so physics starts moving in the correct direction immediately. */
    for (int i = 0; i < 5; i++) {
        int yaw = g_playerBase[i].angleYaw & 0xFFF;
        g_playerBase[i].moveMode = (short)yaw;
    }

    /* Network level-load sync: wait until all peers have finished loading
     * before entering the countdown iris. Prevents fast peers from racing
     * while slow peers (DC) are still touching the filesystem. */
    NetLevelSyncBarrier();

race_start:
    g_introCountdownInit = 0xffffffff;
    if (g_demoMode == DEMO_TITLE) {
        g_mirrorMode = 0;
    }
    else {
        /* binary 0x4cec96-0x4cecaf */
        g_mirrorMode = 1 - (g_titleLogoAngle / 2048);
    }
    if (g_trackId != TRACK_RADIANT_EMERALD) {
        InitWeather();
    }
    g_renderEnabled = 1;
    g_screenshotFlag = 1;
    g_frameSpeedAdjust = 0;
    g_currentTime = timeGetTime_ms();
    for (int i = 1; i <= 7; i++) {
        g_fpsHistoryTable[i] = 0xf;
    }
    g_fpsDisplay = g_currentFPS;
    g_raceTimerBase = timeGetTime_ms() / 1000;
    g_raceStateCounter0 = 0;
    g_raceStateCounter1 = 0;
    DebugLog("MAIN GAME LOOP\n");
    g_splashCountdown = 2;
    g_renderPass = 2;
    g_netWaitFlag = 0;
    /* Attenuate the effects for the duration of a demo/replay so the
     * commentary voice — which SetAllSoundVolumes exempts (0x4d07a6) — sits
     * above them. Deliberate divergence: the binary scales by 3/4, which is
     * only 2.5dB and inaudible in practice; halving gives a clear 6dB. */
    if (g_demoMode != DEMO_NONE && g_musicEnabled == 1) {
        g_optSfxVolumeSave = g_optSfxVolume;
        g_optSfxVolume = g_optSfxVolume >> 1;
        if (g_optSfxVolume == 0) {
            g_optSfxVolume = g_musicEnabled;
        }
        SetAllSoundVolumes();
    }
    if (g_demoMode == DEMO_NONE) {
        g_savedDemoMode = g_demoMode;
    }

    /* Race loop */

    while ((g_raceFinished == 0 || g_fadeState != FADE_VISIBLE) && (int)g_raceFinished < 5) {
        /* PeekMessageA — SDL: pump events.
         * platform_pump_events updates s_keystate via HandleSDLEvent.
         * Copy to g_diKeyboardState so the game's input system sees the keys. */
        platform_pump_events();
        memcpy(g_diKeyboardState, s_keystate, 256);

        g_netFrameCounter = g_netFrameCounter + 1;
        if (g_netSessionActive != 0) {
            if (g_netGameStartState == 1) {
                UpdateNetworkHost();
            }
            else {
                UpdateNetworkClient();
            }
        }
        g_currentTime = timeGetTime_ms();
        ReplayVoice_Tick();  /* start each piece of the replay commentary when due */
        SFX_DuckTick();      /* restore music once the replay commentary has finished */
        uint32_t elapsedRaw = timeGetTime_ms() / 1000 - g_raceTimerBase;
        uint32_t sign = (int)elapsedRaw >> 31;
        g_raceElapsedSec = (elapsedRaw ^ sign) - sign;
#if 0
        g_skipThisFrame = 0;
        if (g_frameSkip != 0 && 0 < g_totalFrames % (g_frameSkip + 1)) {
            g_skipThisFrame = 1;
        }
#endif
        if (g_fadeState != FADE_VISIBLE) {
            UpdateFade();
        }

        /* In network mode, UpdateNetworkHost/Client already populated
         * g_perPlayerInput[] from the network.  ReadInput() clears all
         * 4 slots then overwrites from local joystick pointers — which
         * are NULL for remote players, zeroing their input.
         * Save the network values, let ReadInput run (it handles the
         * pause menu and input polling), then restore all slots from
         * the network-provided values.  Re-apply intro countdown mask
         * since the restored values are unmasked. */
        if (g_netSessionActive != 0 && g_isNetworkGame != 0) {
            unsigned short savedInput[4];
            for (int k = 0; k < 4; k++) {
                savedInput[k] = g_perPlayerInput[k];
            }
            ReadInput();
            for (int k = 0; k < 4; k++) {
                g_perPlayerInput[k] = savedInput[k];
            }
            if (g_introCountdown > 0) {
                for (int k = 0; k < 4; k++) {
                    g_perPlayerInput[k] &= 0x3957; /* INTRO_INPUT_MASK */
                }
            }
        }
        else {
            ReadInput();
        }

        UpdateGameLogic();
        g_splashPrevState = g_interlaceMode;
        if (g_netSessionActive == 0 && g_isNetworkGame == 0) {
            UpdatePerPlayerInput();
        }
        /* [0x4CC9B4]: debug track/variant-select cheat — deliberately not
         * ported (debug keys unwired; the intro-countdown reset it was mistranslated as
         * is done for real in race_timing.c @ 0x4CB8C5). */
        if (g_netDisconnectFlag != 0 && g_fadeLevel == -256) {
            StopCD();
            StopAmbientSounds();
            InitOptionStuff();
            SelectCDTrack();
            g_netSessionActive = 0;
            goto network_screen_entry;
        }
        if ((g_inputBits & 8) != 0 && g_isPaused == 0 && g_pauseLatch == 0 &&
            g_demoMode != DEMO_NONE && g_fadeState == FADE_VISIBLE)
        {
            g_fadeSpeed = 0xc;
            g_raceResult = RACE_RESULT_ENDED;
            g_fadeState = FADE_OUT;
        }
        if (g_demoMode == DEMO_TITLE && g_ghostMaxFrames == g_ghostReadIndex &&
            g_fadeState == FADE_VISIBLE)
        {
            g_fadeSpeed = 0xc;
            g_raceResult = RACE_RESULT_ENDED;
            g_fadeState = FADE_OUT;
        }
        if (g_raceResult == RACE_RESULT_ENDED && g_fadeLevel == -256) {
            StopCD();
            StopAmbientSounds();
            if (g_demoMode != DEMO_NONE && g_musicEnabled == 1) {
                g_optSfxVolume = g_optSfxVolumeSave;
                SetAllSoundVolumes();
            }
            if (g_demoMode == DEMO_TITLE) {
                g_difficultyConfig = g_gpDifficultyLevel;
                g_demoMode = DEMO_NONE;
                /* Demo race may have been 2P split-screen; restore
                 * full-screen viewport before returning to title so
                 * menus don't render in cropped split bounds. Mirrors
                 * the later race-loop exit path. */
                g_numHumans = 1;
                g_numViewports = 1;
                ApplyViewportGeometry();
                goto title_sequence;
            }
            if (g_demoMode == DEMO_REPLAY) {
                goto post_race_dispatch;
            }
        }

        g_polyCount = 0;
        if (g_isPaused == 0) {
            if (g_introTimer == 0x1e && g_demoMode == DEMO_REPLAY) {
                /* 0x4cf0b8: mov eax,0x38 — replay commentary. The clip is
                 * split across consecutive slots from 0x38, so starting it
                 * hands off to the sequencer, which also ducks the music for
                 * the length of the whole line. */
                ReplayVoice_Start();
            }

            uint32_t triggerDepth = (uint32_t)*(unsigned short *)((char *)g_objectStructArray +
                0x32 + g_triggerObjectIndex * 0x44);
            if ((int)triggerDepth <= g_triggerDepthRef) {
                g_triggerDepthRef = triggerDepth - 1;
            }

            g_triggeredObjectFound = 0;
            g_visibleObjectCount = 0;
            g_processedObjectCount = 0;
            g_playerBase[4].renderEnabled = 1;
            g_playerBase[3].renderEnabled = 1;
            g_playerBase[2].renderEnabled = 1;
            g_playerBase[1].renderEnabled = 1;
            g_playerBase[0].renderEnabled = 1;

            g_aiGridAngleOffset = g_aiGridAngleOffset & 0xFFF;
            CheckRaceCompletion();
            if (g_trackId != TRACK_RADIANT_EMERALD) {
                UpdateWeatherEffects();
            }
            UpdateWeather();
            if (g_guideToggle == 1 && g_raceType == RACE_TIMEATTACK &&
                g_raceSubMode < SUBMODE_TAG && g_trackId != TRACK_RADIANT_EMERALD)
            {
                SpawnTAParticles();
            }
            g_emeraldSineOffX = (g_emeraldSineOffX + 0x4D) & 0xFFF;
            g_emeraldSineOffY = (g_emeraldSineOffY + 0xB6) & 0xFFF;
            g_emeraldSineOffZ = (g_emeraldSineOffZ + 0x73) & 0xFFF;
            if (g_netSessionActive != 0 && g_netGameStarted != 0 &&
                g_netGameStartState == 1 &&
                g_netCharSelectState != 4 && g_netCharSelectState != 3)
            {
                /* Pack game data — globals not contiguous on SDL */
                char gdPkt[16];
                memcpy(gdPkt, &g_gameDataPacketHeader, 4);
                memcpy(gdPkt + 4, &g_gameDataPacketFrame, 2);
                memcpy(gdPkt + 6, g_netRecvInput, 8);
                gdPkt[14] = 0;
                gdPkt[15] = 0;
                UpdateNetworkSync(gdPkt, 0x10);
            }
            /* Player pointer helper — original uses Watcom EAX register auto-advance */
            #define PLAYER_PTR(i) (&g_playerBase[(i)])

            if (g_postRaceCameraMode == 0) {
                for (int i = 0; i < (int)g_numPlayers; i++) {
                    if (!net_should_run_physics(i)) continue;
                    UpdatePlayerPhysicsA(PLAYER_PTR(i));
                    UpdatePlayerPhysicsB(PLAYER_PTR(i));
                    if (g_raceSubMode < 2) {
                        UpdatePlayerLapSector(PLAYER_PTR(i));
                    }
                    if (g_raceType != RACE_TIMEATTACK) {
                        UpdatePlayerAnimation(PLAYER_PTR(i));
                    }
                }
                if (g_raceType != RACE_TIMEATTACK && g_raceSubMode != SUBMODE_TAG) {
                    ComputeRacePositions((int)g_numPlayers);
                }
                if (net_should_run_physics(0)) {
                    UpdateHumanPlayerPhysics(PLAYER_PTR(0), g_perPlayerInput[0]);
                }
                if (g_raceType == RACE_MULTIPLAYER) {
                    if (net_should_run_physics(1)) {
                        UpdateHumanPlayerPhysics(PLAYER_PTR(1), g_perPlayerInput[1]);
                    }
                    if (net_should_run_physics(0) && net_should_run_physics(1)) {
                        SweepPlayerCollision(PLAYER_PTR(0), PLAYER_PTR(1));
                    }
                    if (2 < (int)g_numPlayers) {
                        if (net_should_run_physics(2)) {
                            UpdateHumanPlayerPhysics(PLAYER_PTR(2), g_perPlayerInput[2]);
                        }
                        if (net_should_run_physics(0) && net_should_run_physics(2)) {
                            SweepPlayerCollision(PLAYER_PTR(0), PLAYER_PTR(2));
                        }
                        if (net_should_run_physics(1) && net_should_run_physics(2)) {
                            SweepPlayerCollision(PLAYER_PTR(1), PLAYER_PTR(2));
                        }
                    }
                    if (3 < (int)g_numPlayers) {
                        if (net_should_run_physics(3)) {
                            UpdateHumanPlayerPhysics(PLAYER_PTR(3), g_perPlayerInput[3]);
                        }
                        if (net_should_run_physics(0) && net_should_run_physics(3)) {
                            SweepPlayerCollision(PLAYER_PTR(0), PLAYER_PTR(3));
                        }
                        if (net_should_run_physics(1) && net_should_run_physics(3)) {
                            SweepPlayerCollision(PLAYER_PTR(1), PLAYER_PTR(3));
                        }
                        if (net_should_run_physics(2) && net_should_run_physics(3)) {
                            SweepPlayerCollision(PLAYER_PTR(2), PLAYER_PTR(3));
                        }
                    }
                }
                else if (g_raceType == RACE_TIMEATTACK && g_raceSubMode < SUBMODE_TAG && g_ghostDataExists != 0) {
                    UpdateHumanPlayerPhysics(PLAYER_PTR(1), g_perPlayerInput[1]);
                }
                if (g_raceOrder[0] == 1) {
                    PostRaceHandling2P();                /* 0x4C92B4 - binary 0x4CF3B7 */
                }
                else if (g_numPlayers == 5) {
                    Update5PlayerRacePhysics();
                }
                if (g_netWaitFlag == 0 || g_postRaceCameraMode != 0) {
                    for (int i = 0; i < (int)g_numViewports; i++) {
                        if (!net_should_run_physics(i)) {
                            continue;
                        }
                        UpdatePlayerMovement(PLAYER_PTR(i));
                    }
                }
                if (g_raceType == RACE_TIMEATTACK && g_raceSubMode < SUBMODE_TAG && g_numPlayers == 2) {
                    UpdatePlayerMovement(PLAYER_PTR(1));
                }

                /* UpdateCameraConfig: binary 0x4CF42D-0x4CF4AA
                 * Inside postRaceCameraMode==0 block. Guarded by gameMode==0 && introCountdown==0.
                 * Passes per-player INPUT FLAGS (g_perPlayerInput), not hardcoded 0x40.
                 * Bit 6 (0x40) = camera active flag, tied to player input state. */
                if (g_demoMode == DEMO_NONE && g_introCountdown == 0) {   /* 0x4CF42D, 0x4CF43A */
                    /* Binary passes raw g_perPlayerInput — no forced 0x40.
                     * Bit 6 (0x40) comes from the input system naturally. */
                    UpdateCameraConfig(&g_camStateTable[0], (int)g_perPlayerInput[0]); /* VP 0 */
                    if (g_raceType == RACE_MULTIPLAYER) {          /* 0x4CF456: Multiplayer only */
                        UpdateCameraConfig(&g_camStateTable[1], (int)g_perPlayerInput[1]); /* VP 1 */
                    }
                    if ((int)g_numPlayers > 2) {                   /* 0x4CF472 */
                        UpdateCameraConfig(&g_camStateTable[2], (int)g_perPlayerInput[2]); /* VP 2 */
                    }
                    if ((int)g_numPlayers > 3) {                   /* 0x4CF48E */
                        UpdateCameraConfig(&g_camStateTable[3], (int)g_perPlayerInput[3]); /* VP 3 */
                    }
                }
            }
            if (g_netWaitFlag == 0 || g_postRaceCameraMode != 0) {
                for (int i = 0; i < (int)g_numPlayers; i++) {
                    TickPlayerAnimation(PLAYER_PTR(i));
                }
            }
            NetInterpApply();
            SpawnPlayerParticleEffects(PLAYER_PTR(0), 1);
            for (int i = 1; i < (int)g_numPlayers; i++) {
                SpawnPlayerParticleEffects(PLAYER_PTR(i), 0);
            }
            if (g_postRaceCameraMode == 0) {
                SpawnFootShadows();
            }
            UpdateTrackObjects(g_collectEffectBuf);      /* 0x4C970A: EAX=0x907F20 */
            UpdateMissiles();                            /* l0x480D74 */
            UpdateTrackWorld();
            AnimateTrackObjects();
            UpdatePlayfieldGridPositions();
            if (g_raceType != RACE_TIMEATTACK && g_postRaceCameraMode == 0) {
                UpdateRaceObjects();
                UpdateRaceRings();
            }
            if (g_netSessionActive != 0 && g_netGameStarted != 0 &&
                g_netGameStartState == 1 &&
                g_netCharSelectState != 4 && g_netCharSelectState != 3)
            {
                char gdPkt2[16];
                memcpy(gdPkt2, &g_gameDataPacketHeader, 4);
                memcpy(gdPkt2 + 4, &g_gameDataPacketFrame, 2);
                memcpy(gdPkt2 + 6, g_netRecvInput, 8);
                gdPkt2[14] = 0; gdPkt2[15] = 0;
                UpdateNetworkSync(gdPkt2, 0x10);
            }
            /* Camera update: binary 0x4CF5AB-0x4CF6A9 (inlined)
             * Binary has ONE loop at 0x4CF638 that handles all camera modes.
             * raceType==3: BuildCameraView for player 2, skip loop.
             * Network/multiplayer: BuildCameraView for local player, skip loop.
             * Otherwise: loop over viewports:
             *   g_demoMode==0 (normal race): BuildCameraView
             *   g_demoMode!=0 (time trial/replay): UpdateFlyoverCamera
             */
            if (g_raceType == RACE_SPECIAL) {                       /* 0x4CF5AB */
                /* Per binary 0x4CF5B4-0x4CF5C3: BuildCameraView is called
                 * with PLAYER 2's struct as the player arg, but viewport 0's
                 * camState (smoothedCam) and viewport 0's vpConfig (renderCam).
                 * The orbit-camera math uses player 2's spawn anchor while
                 * the result lands in viewport 0 — which is what the renderer
                 * actually reads via g_currentRenderCam. */
                Player *player = &g_playerBase[2];
                CamStateEntry *camState = &g_camStateTable[0];
                int *vpConfig = (int *)((char *)g_viewportConfigArray + 0 * 0xC8);
                BuildCameraView(player, camState, (void *)vpConfig, (void *)camState);
                SetViewportClipRect(vpConfig);
                g_currentRenderCam = vpConfig;
            }
            else if (g_isNetworkGame != 0 || g_netSessionActive != 0) { /* 0x4CF5CD: line 623 */
                /* Network: camera for the LOCAL player into viewport[local]'s
                 * config (0x4CF5E9-0x4CF5FF: 0x6E9924 + local*0xC8). Every
                 * viewport has full-screen bounds when g_numHumans <= 1
                 * (SetupViewportConfig 0x4CBA4C), so RenderHUD, rain, snow
                 * and SpawnOtherParticle all index by local too. */
                int localIdx = (int)(unsigned short)g_localPlayerIndex;
                Player *player = &g_playerBase[localIdx];
                CamStateEntry *camState = &g_camStateTable[localIdx];
                int *vpConfig = (int *)((char *)g_viewportConfigArray + localIdx * 0xC8);
                BuildCameraView(player, camState, (void *)vpConfig, (void *)camState);
                SetViewportClipRect(vpConfig);
                g_currentRenderCam = vpConfig;
            }
            else {
                /* 0x4CF638-0x4CF6A7: per-viewport camera loop */
                for (int vp = 0; vp < (int)g_numHumans; vp++) { /* 0x4CF639 */
                    if (g_demoMode == DEMO_NONE) {               /* 0x4CF68B */
                        BuildCameraViewForViewport(vp); /* 0x4CF68F: BuildCameraView */
                    } else {
                        /* 0x4CF696-0x4CF6A2: UpdateFlyoverCamera */
                        Player *player = &g_playerBase[vp];
                        CamStateEntry *camState = &g_camStateTable[vp];
                        RenderCamera *vpConfig = (RenderCamera *)((char *)g_viewportConfigArray + vp * 0xC8);
                        UpdateFlyoverCamera(player, camState, vpConfig,
                                            g_waypointTablePtr, 0, vp);
                    }
                }
            }
            if (g_trackId != TRACK_RADIANT_EMERALD) {
                UpdateWeatherCounters();                                     /* 0x4CF6B2 */
                if (g_netSessionActive == 0 && g_isNetworkGame == 0) {
                    for (int vp = 0; vp < (int)g_numHumans; vp++) {
                        SpawnOtherParticle(vp);
                    }
                } else {
                    SpawnOtherParticle((int)(unsigned short)g_localPlayerIndex); /* 0x4CF6CB */
                }
            }
            /* clamp per-player ringCount to 999 */
            for (int p = 0; p < 5; p++) {
                if (g_playerBase[p].ringCount > 999) {
                    g_playerBase[p].ringCount = 999;
                }
            }
            if (g_skipThisFrame == 0) {
                UpdateFrameTimers();
                RenderHUD();
            }
            g_raceElapsedDisplay = timeGetTime_ms() / 1000 - g_raceTimerDisplayBase;
            UpdateLapCounter(1);
            if (g_skipThisFrame == 0) {
                goto render_frame;
            }
        }
        else {
            if (g_fadeLevel == -256) {
                StopCD();
                if (g_raceResult == RACE_RESULT_RESTART) {
                    goto race_setup;
                }
                if (g_raceResult == RACE_RESULT_QUIT) {
                    if (g_raceType == RACE_MULTIPLAYER && g_netSessionActive == 0) {
                        mpPlayerCount = g_numPlayers;    /* 0x4CFA52-0x4CFA57 */
                        g_numHumans = 1;                 /* restore full-screen viewport */
                        ApplyViewportGeometry();
                        ResetInputState();
                        InitOptionStuff();
                        goto multiplayer_mode_select;
                    }
                    if (g_raceType != 4 || g_raceCheckpoint == 0 || g_trackId == TRACK_RADIANT_EMERALD) {
                        goto main_menu_init;
                    }
                    goto special_race_unlock;
                }
            }
            RenderHUD();
render_frame:
            ;
        }

        FlipD3D();                                       /* OpenGL: swapBuffers */

        g_totalFrames2 = g_totalFrames2 + 1;
        if (g_netSessionActive != 0) {
            if (g_netGameStartState == 1) {
                if (g_netGameStarted != 0) {
                    SendNetworkHostData();
                }
                /* Host also sends keepalive + player state (same as client).
                 * Binary host never called this, but SDL needs it:
                 * keepalive syncs intro countdown, state broadcasts positions. */
                SendNetworkClientData();
            } else {
                SendNetworkClientData();
            }
        }
        WaitForFrameCap();
    } /* end race loop */

    /* Race loop exit handler (0x4CF84D-0x4CF9A0) */
    /* Reached when the race loop exits. Handles replay mode, GP progression,
     * special race setup, and ghost termination before falling through to
     * the main post-race dispatcher. */
    StopAmbientSounds(); /* 0x4d0458 */
    StopCD();            /* 0x4d0264 */

    /* Replay mode -> title screen */
    if (g_demoMode == DEMO_TITLE) {                                             /* 0x4cf857 */
        g_difficultyConfig = g_gpDifficultyLevel; /* 0x8fd444 = 0x6dd834 */
        g_demoMode = DEMO_NONE;
        /* Demo race may have been 2P split-screen; restore full-screen
         * viewport before bouncing back to title so menus don't render
         * in the cropped split bounds. Mirrors the post-race exit path
         * at lines 1858-1859 and the main-menu loop entry at 962-964. */
        g_numHumans = 1;
        g_numViewports = 1;
        ApplyViewportGeometry();
        goto title_sequence;
    }

    /* Handle special raceFinished values */
    if (g_raceFinished == CHALLENGE_LOST) { /* 0x4cf877 */
        g_raceFinished = 1;
        g_playerBase->racePosition = 5; /* placement = 5 (last) */
    }
    if (g_raceFinished == CHALLENGE_WON) { /* 0x4cf897 */
        g_raceFinished = 1;
        g_playerBase->racePosition = 1; /* placement = 1 (won) */
    }

    /* Non-GP or special conditions - skip GP progression */
    if (g_demoMode != DEMO_NONE) {
        goto post_race_non_gp; /* 0x4cf8b3 */
    }
    if ((int)g_raceFinished > 2) {
        goto post_race_non_gp; /* 0x4cf8c0 */
    }
    if (g_raceType == RACE_TIMEATTACK) {
        goto post_race_non_gp; /* 0x4cf8cd */
    }
    if (g_raceType == RACE_MULTIPLAYER && g_raceSubMode == SUBMODE_BALLOON) {
        goto post_race_non_gp; /* 0x4cf8d8 */
    }

    /* GP progression decision */
    if (g_raceType != RACE_GP             /* 0x4cf8e6 */
        || g_p1CollectionCount != 5 /* 0x4cf8f0 — not all emeralds */
        || g_playerBase->racePosition >= 4)
    {                            /* 0x4cf8f9 — didn't podium */
        AdvanceGrandPrixTrack(); /* 0x471364 — 0x4cf930 */
        goto race_start;         /* race start loop */
    }

    /* Won top 3 with all 5 emeralds - special boss race */
    CalculateChampionshipPoints(); /* 0x4c43c0 — 0x4cf905 */
    g_specialRacePlacement = g_playerBase->racePosition;
    SetupSpecialRace();    /* 0x471aa0 — 0x4cf916 */
    g_cdPlaybackState = 0; /* 0x6d9a40 */
    g_numPlayers = 2;      /* 0x4cf926 */
    goto race_start;       /* race start loop */

post_race_non_gp: /* 0x4cf93a */
    /* Non-GP exit handler */
    if (g_raceFinished == CHALLENGE_LOST) { /* 0x4cf93a */
        /* reset from challenge race type to gp race type */
        if (g_raceType == 4) {
            g_raceType = RACE_GP;
        }
        goto race_setup; /* race setup */
    }
    if (g_raceFinished == CHALLENGE_WON) { /* 0x4cf95f */
        goto main_menu_init; /* main menu */
    }
    /* Terminate ghost recording */
    if (g_demoMode == DEMO_NONE) { /* 0x4cf968 */
        g_taGhostSource[g_ghostWriteIndex] = 0xFFFF; /* mark ghost end */
    }
    /* Undo the demo/replay SFX attenuation applied at loop entry.
     * 0x4cf989: cmp [0x8fd4a0], 1 — the guard reads g_musicEnabled. */
    if (g_demoMode != DEMO_NONE && g_musicEnabled == 1) { /* 0x4cf980 */
        g_optSfxVolume = g_optSfxVolumeSave; /* 0x4cf992/97: 0x8fd49c = 0x6da2a0 */
        SetAllSoundVolumes();               /* 0x4d0760 */
    }

    /* Post-race 0x004CF9A1 to 0x004CFFFF */

post_race_dispatch:

    if (g_netSessionActive != 0 || g_isNetworkGame != 0) {
        g_netSessionActive = 0;
        LoadTrackSinglePlayer();
        CloseDirectPlaySession();
        g_multiplayerWasActive = 1;
    }
    else
    {
        g_multiplayerWasActive = 0;
    }

    /* 0x4CFA02: g_raceType == RACE_MULTIPLAYER - Multiplayer */
    if (g_raceType == RACE_MULTIPLAYER) {
        /* Populate g_racePointsLaps from player structs for multiplayer.
         * Binary skips CalculateChampionshipPoints for multiplayer
         * (g_multiplayerWasActive != 0), leaving g_racePointsLaps at zero.
         * Network mode was dormant — this was never wired up. */
        for (int k = 0; k < (int)g_numViewports; k++) {
            g_racePointsLaps[k * 3 + 0] = g_playerBase[k].lap1Time & 0xFFFFFF;
            g_racePointsLaps[k * 3 + 1] = g_playerBase[k].lap2Time & 0xFFFFFF;
            g_racePointsLaps[k * 3 + 2] = g_playerBase[k].lap3Time & 0xFFFFFF;
        }
        g_savedNumHumans = g_numHumans;

        int screenType;
        if (g_raceSubMode == SUBMODE_NORMAL) {
            screenType = 1; /* single-lap TA */
        }
        else {
            screenType = 2; /* multi-lap TA */
        }
        screenResult = ResultsScreen(screenType);

        if (screenResult == SCREEN_OK) {
            g_numHumans = g_savedNumHumans;
            ApplyViewportGeometry();
            goto race_setup;
        }
        if (screenResult == SCREEN_TA || screenResult == SCREEN_TITLE) {
            mpPlayerCount = g_numPlayers;    /* same re-seed as 0x4CFA57 */
            g_numHumans = 1;
            ApplyViewportGeometry();
            InitOptionStuff();
            if (g_isNetworkGame != 0) {
                g_isNetworkGame = 0;
                screenResult = NetworkScreenReentry();
                if (screenResult == SCREEN_TITLE) {
                    StopCD();
                    goto title_sequence;
                }
                goto network_result_dispatch;
            }
            goto multiplayer_mode_select;
        }
        /* fall through if result is something else */
    }

    /* 0x4CFA95: g_raceType == RACE_TIMEATTACK — Time Attack */
    if (g_raceType == RACE_TIMEATTACK) {
        g_timeAttackResult = 0;

        /* Compute total race time from 3 laps (masking out high byte flags) */
        int totalTime = (g_playerBase[0].lap1Time & 0xFFFFFF) + (g_playerBase[0].lap2Time & 0xFFFFFF)
                      + (g_playerBase[0].lap3Time & 0xFFFFFF);

        /* Ghost record check (only if normal difficulty, short config, finished 3 laps) */
        if (g_ghostToggle == 1 && g_raceSubMode < 2
            && g_playerBase[0].lapsCompleted == 3)
        {
            /* Index into TA record arrays: charId + (trackId-1)*10 + lapConfig*50 */
            int charId0 = g_playerBase->charId;
            int taIdx = charId0 + (g_trackId - 1) * 10 + g_raceSubMode * 50;
            int recordFlag = g_taRecordFlags[taIdx];
            int bestTotal  = g_taBestTotalTime[taIdx];

            /* Check if same char as player 1 for record display */
            if (recordFlag == 1) {
                short charId1 = g_playerBase[1].charId;
                /* same character? */
                if (charId0 == charId1) {           
                    /* new record */               
                    if (totalTime < bestTotal) {
                        g_timeAttackResult = 2;
                    }
                    /* placed, not better */
                    else {
                        g_timeAttackResult = 1;
                    }
                }
            }

            /* Save new record if first time OR better time */
            if (recordFlag == 0 || (recordFlag == 1 && totalTime < bestTotal)) {
                /* Update best total time */
                g_taBestTotalTime[taIdx] = totalTime;

                /* Update best individual lap times */
                int lap1 = g_playerBase[0].lap1Time & 0xFFFFFF;
                if (lap1 < g_taBestLapTime[taIdx]) {
                    g_taBestLapTime[taIdx] = lap1;
                }

                int lap2 = g_playerBase[0].lap2Time & 0xFFFFFF;
                if (lap2 < g_taBestLapTime[taIdx]) {
                    g_taBestLapTime[taIdx] = lap2;
                }

                int lap3 = g_playerBase[0].lap3Time & 0xFFFFFF;
                if (lap3 < g_taBestLapTime[taIdx]) {
                    g_taBestLapTime[taIdx] = lap3;
                }

                /* Copy ghost replay buffer */
                for (int i = 0; i < 0x1600; i++) {
                    g_taGhostBuffer[i] = g_taGhostSource[i];
                }

                g_ghostDataExists = 1;
                g_ghostTotalFrames = g_ghostWriteIndex;
                SaveGhostData();

                /* Recompute index and set record flag */
                taIdx = charId0 + (g_trackId - 1) * 10 + g_raceSubMode * 50;
                g_taRecordFlags[taIdx] = 1;
            }
        }

        /* Call ResultsScreen per lap config (one call per player type) */

        int resultVal = 0; /* edx in original — accumulates last result */
        if (g_raceSubMode == SUBMODE_NORMAL) {
            resultVal = ResultsScreen(3);
        }
        if (g_raceSubMode == SUBMODE_REVERSE) {
            resultVal = ResultsScreen(4);
        }
        if (g_raceSubMode == SUBMODE_BALLOON) {
            resultVal = ResultsScreen(5);
        }
        if (g_raceSubMode == SUBMODE_TAG) {
            resultVal = ResultsScreen(6);
        }

        if (resultVal == 1) {
            if (g_raceSubMode < 2) {
                g_replayCharIds[1] = g_ghostCharId;
            }
            g_ghostWriteIndex = 0;
            g_ghostReadIndex = 0;
            if (g_numPlayers == 1 && g_raceSubMode < 2 && g_ghostDataExists != 0) {
                g_numPlayers = 2;
            }
            goto race_setup;
        }
        if (resultVal == 2 || resultVal == SCREEN_TITLE) {
            goto main_menu_init;
        }
    }

    /* 0x4CFDF4: g_raceType == RACE_GP — Grand Prix single race */
    if (g_raceType == RACE_GP) {
        /* Check for character unlocks (only if gameMode==0, checkpoint active, not special track) */
        if (g_demoMode == DEMO_NONE && g_raceCheckpoint != 0
            && g_trackId != TRACK_RADIANT_EMERALD)
        {
            ShowEmeraldUnlockScreen();
            g_savedDemoMode = DEMO_TITLE;
        }

        /* Show results (skip if gameMode == 1 = replay mode) */
        if (g_demoMode != DEMO_TITLE) {
            screenResult = ResultsScreen(0);
        } else {
            screenResult = 0;
        }
        g_demoMode = DEMO_NONE;

        if (screenResult == 0) {
            /* Save race data for replay */
            g_replayPointsLaps[0] = g_racePointsLaps[0];
            g_replayPointsLaps[1] = g_racePointsLaps[1];
            g_replayPointsLaps[2] = g_racePointsLaps[2];
            g_demoMode = DEMO_REPLAY;
            g_replayPointsTotal[0] = g_racePointsTotal[0];
            g_replayLapCount = g_playerBase[0].lapsCompleted;
            g_replayPlacement = g_playerBase[0].racePosition;
            SetupReplayData();
            goto race_setup; /* restart in replay mode */
        }

        /* Accumulate character unlocks if any were found */
        if (g_savedDemoMode != DEMO_NONE) {
            int unlockCount = 0;
            for (int i = 0; i < 7; i++) {
                g_charUnlockState[i] = g_charUnlockSource[i];
                if (g_charUnlockSource[i] == 2) unlockCount++;
            }
            if (unlockCount == 7 && g_allCharsUnlocked == 0) {
                g_superSonicSeed = 0x28; /* default to Super Sonic */
                g_allCharsUnlocked = 2;
            }
        }

        if (screenResult == 1) {
            goto race_setup;
        }
        if (screenResult == 2) {
            if (g_trackId == TRACK_RADIANT_EMERALD) {
                if (g_playerBase[0].racePosition == 1) {
                    ResetInputState();
                    CreditsScreen();
                    goto title_sequence;
                }
            }
            goto main_menu_init;
        }
    }

    /* 0x4CFF2E: g_raceType == RACE_SPECIAL — VS Challenge */
    if (g_raceType == RACE_SPECIAL) {
        if (g_raceFinished == 4) {
            g_raceType = 4;                                        /* upgrade to raceType==4 internal state */
            InitRaceStart();
            goto race_start;
        }
    }

    /* 0x4CFF52: g_raceType == 4 — internal post-upgrade state */
    if (g_raceType == 4 && g_raceCheckpoint != 0
        && g_trackId != TRACK_RADIANT_EMERALD)
    {

special_race_unlock:                                                      /* also reached from race loop exit */
        /* Apply special race placement to racePlacement field */
        g_playerBase->racePosition = (short)g_specialRacePlacement;

        ShowEmeraldUnlockScreen();

        /* Accumulate character unlocks */
        int unlockCount = 0;
        for (int i = 0; i < 7; i++) {
            g_charUnlockState[i] = g_charUnlockSource[i];
            if (g_charUnlockSource[i] == 2) {
                unlockCount++;
            }
            if (unlockCount == 7 && g_allCharsUnlocked == 0) {
                g_superSonicSeed = 0x28;
                g_allCharsUnlocked = 2;
            }
        }
    }

    /* raceType==4 post-race: call CalculateChampionshipPoints then reset to GP.
     * Function name suggests this internal state is championship-related, but
     * raceType==4 itself isn't in the authoritative Sonic Retro enum table. */
    if (g_raceType == 4) {
        CalculateChampionshipPoints();
        g_raceType = 0;                                            /* reset to GP mode */
    }
    goto main_menu_init;

    platform_shutdown();
    return 0;
}