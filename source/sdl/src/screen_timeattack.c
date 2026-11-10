/**
 * screen_timeattack.c — Time Attack mode selection screen
 *
 * 4-item menu: Single Race, Free Practice, Time Trial Replay, 5-Player.
 * Same structural pattern as MainMenuScreen.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"

extern int g_lastTimeAttackSelection;   /* 0x0068AF7C */

/* Time attack menu item positions */
extern int g_timeAttackMenuPositions[]; /* 0x005025CC */
/* Time attack model indices */
extern int g_timeAttackModelIndices[];  /* 0x005025DC */

/* Forward declarations */
void SetupMenuTexturesD3D(void);
void LoadMenuBitmaps(void);
void LoadMenuBitmapsSoftware(void);
void FinalizeMenuTexturesD3D(void);
void ProcessTpageStates(void);
void D3DAppClearBackBuffer(void);
void RenderFadeOverlay(void);
void WaitForFrameCap(void);
void platform_pump_events(void);

/**
 * TimeAttackModeSelect — 0x0048BA80 — 2030 bytes
 * Time Attack sub-mode selection.
 *
 * Returns:
 *   1 = Single Race (g_raceSubMode=0)
 *   2 = Free Practice (g_raceSubMode=1)
 *   3 = Time Trial Replay (g_raceSubMode=3)
 *   4 = 5-Player Mode (g_raceSubMode=2, g_numPlayers=5)
 *   0 = back, SCREEN_TITLE = title
 */
int TimeAttackModeSelect(void)
{
    DebugLog("TimeAttackModeSelectScreen\n");

    /* red */
    g_bgTintR = 0xFF;
    g_bgTintG = 0;
    g_bgTintB = 0; 
    g_menuState = 5;

    /* Load textures */
    {
#ifdef SONICR_DC
        if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
        SetupMenuTexturesD3D();
        LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);
        TintBackgroundTPage(g_bgTintR, g_bgTintG, g_bgTintB);
        LoadTPageRGB(g_uiTexPage + 1, PATH_MENU_TIMEATTACK);
        for (int tp = 0; tp < 52; tp++) {
            if (g_tpageStateArray[tp] == 6 && g_tpagePixelBuf[tp] != NULL) {
                g_tpageStateArray[tp] = 4;
            }
        }
        ProcessTpageStates();
        FinalizeMenuTexturesD3D();
#ifdef SONICR_DC
        ResumeCD();
#endif
    }

    g_totalFrames = 0;
    g_modelRotation = 0;
    g_fadeState = FADE_IN;
    g_screenResult = -1;
    g_menuExtraY = g_screenHeight + g_screenScale * -0x3E;
    g_renderEnabled = 1;
    g_trackId = TRACK_NONE;  /* reset track for selection */

    g_menuCursor = g_lastTimeAttackSelection;
    g_menuCursorY = g_timeAttackMenuPositions[g_lastTimeAttackSelection];
    g_menuCursorTargetY = g_menuCursorY;

    DWORD idleStart = timeGetTime();
    unsigned int idleStartSec = idleStart / 1000;

    while (1) {
        platform_pump_events();
        g_currentTime = timeGetTime();
        DWORD now = timeGetTime();
        unsigned int elapsed = now / 1000 - idleStartSec;
        unsigned int sign = (int)elapsed >> 31;
        elapsed = (elapsed ^ sign) - sign;

        int cdStatus = GetLogicalCDTrack();
        if (cdStatus != 5) {
            UpdateCDPlayback(5);
        }

        if (g_fadeState != FADE_VISIBLE) {
            UpdateFade();
        }
        if (g_fadeLevel == -256) {
            break;
        }

        ReadInput();

        if (g_fadeState == FADE_VISIBLE) {
            /* Confirm */
            if ((g_inputBits & 6) != 0 && g_menuCursorTargetY == g_menuCursorY) {
                PlaySoundEffect(2, 0, 0);
                g_screenResult = g_menuCursor + 1;
                g_lastTimeAttackSelection = g_menuCursor;
                g_fadeState = FADE_OUT;
                g_nextScreenId = 6;  /* "came from time attack" */
            }

            /* Back */
            if ((g_inputBits & 1) != 0) {
                PlaySoundEffect(0, 0, 0);
                g_screenResult = SCREEN_BACK;
                g_fadeState = FADE_OUT;
            }

            /* Idle timeout */
            if (elapsed > 30) {
                PlaySoundEffect(0, 0, 0);
                g_screenResult = SCREEN_TITLE;
                g_fadeState = FADE_OUT;
            }

            /* D-pad navigation */
            if (g_menuCursorTargetY == g_menuCursorY) {
                if ((g_inputBits & 0x40) != 0 && (g_inputBits & 0x80) == 0 &&
                    g_menuCursor > 0) {
                    g_menuCursor--;
                    PlaySoundEffect(1, 0, 0);
                    idleStart = timeGetTime();
                    idleStartSec = idleStart / 1000;
                }
                if ((g_inputBits & 0x80) != 0 && (g_inputBits & 0x40) == 0 &&
                    g_menuCursor < 3) {
                    g_menuCursor++;
                    PlaySoundEffect(1, 0, 0);
                    idleStart = timeGetTime();
                    idleStartSec = idleStart / 1000;
                }
                g_menuCursorTargetY = g_timeAttackMenuPositions[g_menuCursor];
            }
        }

        /* Smooth cursor scroll */
        if (g_menuCursorY < g_menuCursorTargetY) {
            g_menuCursorY += 4;
        }
        else if (g_menuCursorTargetY < g_menuCursorY) {
            g_menuCursorY -= 4;
        }
        g_menuAnimY = g_menuCursorY;

        /* Rotate model */
        g_modelRotation = (g_modelRotation + 0x20) & 0xFFF;

        /*Render*/
        {
            ProcessTpageStates();
            BeginFrame();
            D3DAppClearBackBuffer();
            EndFrame();
            if (g_fadeLevel < 0) {
                RenderFadeOverlay();
            }

            BeginFrame();
            /* Positions verified from disassembly 0x48C038-0x48C20E.
             * EAX=xPos, EDX=yPos, stack: depth,width,height,tpage,uvX,uvY,uvW,uvH,color */
            /* Header bar left half */
            DrawTexturedQuad(0, 0x20, 0x447A0000, 0x140, 0x40, g_uiTexPage + 1,
                             0, 0, 0xA0, 0x20, VERTEX_WHITE);
            /* Header bar right half */
            DrawTexturedQuad(0x140, 0x20, 0x447A0000, 0x140, 0x40, g_uiTexPage + 1,
                             0, 0x20, 0xA0, 0x20, VERTEX_WHITE);
            /* Cursor highlight */
            DrawTexturedQuad(0x80, 0x120, 0x41C80000, 0x180, 0x20, g_uiTexPage + 1,
                             0x40, g_menuCursor * 0x10 + 0x40, 0xC0, 0x10, VERTEX_WHITE);
            /* 3D model */
            Draw3DModelD3D(0, 0, 0x400, 0, g_modelRotation, 0,
                           (intptr_t)((char *)g_objectStructArray + g_timeAttackModelIndices[g_menuCursor] * 0x44),
                           0);
            /* Course preview icon — xPos from g_menuAnimY (0x9252C0), the per-frame
             * copy of the interpolated cursor Y set above. */
            DrawTexturedQuad((g_menuAnimY - 4) * 2, 0x14C, 0x43340000, 0x90, 0x80, g_uiTexPage + 1,
                             0xA0, 0, 0x48, 0x40, VERTEX_WHITE);
            /* Mode item labels — xPos from g_timeAttackMenuPositions[i] * 2 */
            DrawTexturedQuad(g_timeAttackMenuPositions[0] * 2, 0x164, 0x43480000, 0x80, 0x60, g_uiTexPage + 1,
                             0, 0x40, 0x40, 0x30, VERTEX_WHITE);
            DrawTexturedQuad(g_timeAttackMenuPositions[1] * 2, 0x164, 0x43480000, 0x80, 0x60, g_uiTexPage + 1,
                             0, 0x70, 0x40, 0x30, VERTEX_WHITE);
            DrawTexturedQuad(g_timeAttackMenuPositions[2] * 2, 0x164, 0x43480000, 0x80, 0x60, g_uiTexPage + 1,
                             0, 0xA0, 0x40, 0x30, VERTEX_WHITE);
            DrawTexturedQuad(g_timeAttackMenuPositions[3] * 2, 0x164, 0x43480000, 0x80, 0x60, g_uiTexPage + 1,
                             0, 0xD0, 0x40, 0x30, VERTEX_WHITE);

            RenderWavingMenuBackground();                     /* 0x004C68B8 — software twin */
            EndFrame();
            FlipD3D();
        }

        g_totalFrames2++;
        g_totalFrames++;
        WaitForFrameCap();
    }

    return g_screenResult;
}
