/**
 * screen_multimode.c — Multiplayer mode selection screen
 *
 * MultiPlayerModeSelect — 2 items: 2-lap race or extended race.
 * Same structural pattern as other menu screens.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"

/* Multiplayer menu positions and model indices */
extern int g_multiModePositions[];      /* 0x005025EC */
extern int g_multiModeModelIndices[];   /* 0x005025F4 */
extern int g_lastMultiSelection;        /* 0x0068AF80 */

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
 * MultiPlayerModeSelect — 0x0048C270 — 1783 bytes
 * Returns: 1 = normal (g_raceSubMode=SUBMODE_NORMAL), 2 = balloon (g_raceSubMode=SUBMODE_BALLOON)
 */
int MultiPlayerModeSelect(void)
{
    DebugLog("MultiPlayerModeSelectScreen\n");

    /* blue */
    g_bgTintR = 0;
    g_bgTintG = 0;
    g_bgTintB = 0xFF;
    g_menuState = 5;

#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupMenuTexturesD3D();
    LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);
    TintBackgroundTPage(g_bgTintR, g_bgTintG, g_bgTintB);
    LoadTPageRGB(g_uiTexPage + 1, PATH_MENU_MULTIMODE);
    ProcessTpageStates();
    FinalizeMenuTexturesD3D();
#ifdef SONICR_DC
    ResumeCD();
#endif

    g_totalFrames = 0;
    g_modelRotation = 0;
    g_fadeState = FADE_IN;
    g_menuExtraY = g_screenHeight + g_screenScale * -0x3E;
    g_screenResult = -1;
    g_renderEnabled = 1;

    g_menuCursor = g_lastMultiSelection;
    g_menuCursorY = g_multiModePositions[g_lastMultiSelection];
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
            return g_screenResult;
        }

        ReadInput();

        if (g_fadeState == FADE_VISIBLE) {
            if ((g_inputBits & 6) != 0 && g_menuCursorTargetY == g_menuCursorY) {
                PlaySoundEffect(2, 0, 0);
                g_screenResult = g_menuCursor + 1;
                g_lastMultiSelection = g_menuCursor;
                g_nextScreenId = 7;
                g_fadeState = FADE_OUT;
            }
            if ((g_inputBits & 1) != 0) {
                PlaySoundEffect(0, 0, 0);
                g_screenResult = SCREEN_BACK;
                g_fadeState = FADE_OUT;
            }
            if (elapsed > 30) {
                PlaySoundEffect(0, 0, 0);
                g_screenResult = SCREEN_TITLE;
                g_fadeState = FADE_OUT;
            }

            if (g_menuCursorTargetY == g_menuCursorY) {
                if ((g_inputBits & 0x40) && !(g_inputBits & 0x80) && g_menuCursor > 0) {
                    g_menuCursor--;
                    PlaySoundEffect(1, 0, 0);
                    idleStart = timeGetTime();
                    idleStartSec = idleStart / 1000;
                }
                if ((g_inputBits & 0x80) && !(g_inputBits & 0x40) && g_menuCursor < 1) {
                    g_menuCursor++;
                    PlaySoundEffect(1, 0, 0);
                    idleStart = timeGetTime();
                    idleStartSec = idleStart / 1000;
                }
                g_menuCursorTargetY = g_multiModePositions[g_menuCursor];
            }
        }

        if (g_menuCursorY < g_menuCursorTargetY) {
            g_menuCursorY += 4;
        }
        else if (g_menuCursorTargetY < g_menuCursorY) {
            g_menuCursorY -= 4;
        }
        g_menuAnimY = g_menuCursorY;
        g_modelRotation = (g_modelRotation + 0x20) & 0xFFF;

        /* Render */
        {
            ProcessTpageStates();
            BeginFrame(); 
            D3DAppClearBackBuffer(); 
            EndFrame();

            if (g_fadeLevel < 0) {
                RenderFadeOverlay();
            }

            BeginFrame();
            /* Positions verified from disassembly 0x48C79E-0x48C907.
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
            /* 3D character model */
            Draw3DModelD3D(0, 0, 0x400, 0, g_modelRotation, 0,
                           (intptr_t)((char *)g_objectStructArray + g_multiModeModelIndices[g_menuCursor] * 0x44),
                           0);
            /* Preview icon — xPos from g_menuAnimY */
            DrawTexturedQuad((g_menuAnimY - 4) * 2, 0x14C, 0x43340000, 0x90, 0x80, g_uiTexPage + 1,
                             0xA0, 0, 0x48, 0x40, VERTEX_WHITE);
            /* Mode label: 2-Lap Race */
            DrawTexturedQuad(g_multiModePositions[0] * 2, 0x164, 0x43480000, 0x80, 0x60, g_uiTexPage + 1,
                             0, 0x40, 0x40, 0x30, VERTEX_WHITE);
            /* Mode label: Extended Race */
            DrawTexturedQuad(g_multiModePositions[1] * 2, 0x164, 0x43480000, 0x80, 0x60, g_uiTexPage + 1,
                             0, 0x70, 0x40, 0x30, VERTEX_WHITE);
            RenderWavingMenuBackground();                     /* 0x004C68B8 — software twin */
            EndFrame();
            FlipD3D();
        }

        g_totalFrames2++;
        g_totalFrames++;
        WaitForFrameCap();
    }
}
