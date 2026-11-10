/**
 * screen_menu.c — Main menu screen
 *
 * MainMenuScreen — the mode selection menu after the title screen.
 * 5 items: Grand Prix, Time Attack, VS, Multiplayer, Options.
 * See state_machine.md for flow context.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"

/* Menu-specific globals — g_menuExtraY is a #define in sonicr_globals.h */
extern int g_lastMenuSelection;     /* 0x0068AF78 */
extern int g_networkAvailable;      /* 0x00689B00 */

/* Menu item positions lookup table */
extern int g_menuItemPositions[];   /* 0x00501BD4 */
/* Menu item 3D model indices */
extern int g_menuModelIndices[];    /* 0x00501BFC */

/* Forward declarations */
void SetupMenuTexturesD3D(void);
void LoadMenuBitmaps(void);
void LoadMenuBitmapsSoftware(void);
void FinalizeMenuTexturesD3D(void);
void ProcessTpageStates(void);
void D3DAppClearBackBuffer(void);
void RenderFadeOverlay(void);
void platform_pump_events(void);

/* 30fps cap helper (shared with screens.c) */
extern void WaitForFrameCap(void);

/**
 * MainMenuScreen — 0x004885BC — 2541 bytes
 * Main mode selection menu. Returns selection code (1-5) or 0/SCREEN_TITLE.
 *
 * Menu items:
 *   0 = Grand Prix    → returns 1
 *   1 = Time Attack   → returns 2
 *   2 = VS Mode       → returns 3
 *   3 = Multiplayer   → returns 4 (skipped if no network)
 *   4 = Options       → returns 5
 */
int MainMenuScreen(void)
{
    DebugLog("ModeSelectScreen\n");

    /* Init CD audio */
    int cdStatus = GetLogicalCDTrack();
    if (cdStatus != 5 || g_menuState != 5) {
        UpdateCDPlayback(5);
    }

    DWORD startTimeMs = timeGetTime();
    g_menuIdleStartTime = startTimeMs / 1000;
    g_menuState = 5;

    /* Fade background: green */
    g_bgTintR = 0;
    g_bgTintG = 0xFF;
    g_bgTintB = 0;

    /* Load textures */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupMenuTexturesD3D();
    LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);
    TintBackgroundTPage(g_bgTintR, g_bgTintG, g_bgTintB);
    LoadTPageRGB(g_uiTexPage + 1, PATH_MENU_MODESEL);
    /* SetupMenuTexturesD3D set all tpage states to 6. Restore state 4
     * for tpages that were already loaded (character textures, env map, etc.)
     * so the fade overlay and 3D models can reference them. */
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

    g_totalFrames = 0;
    g_modelRotation = 0;
    int minItem = 0;
    /* When network is available, use the 5-item position table (indices 5-9) */
    int posOff = g_networkAvailable ? 5 : 0;
    g_fadeState = FADE_IN;
    g_menuExtraY = g_screenHeight + g_screenScale * -0x3E;

    /* Skip multiplayer if no CD */
    if (g_cdAvailable == 0) {
        if (g_lastMenuSelection < 3) {
            g_lastMenuSelection = 4;
        }
        minItem = 4;
    }

    g_menuCursor = g_lastMenuSelection;
    g_screenResult = SCREEN_OK;
    g_menuReady = 1;
    g_menuCursorY = g_menuItemPositions[g_lastMenuSelection + posOff];
    g_renderEnabled = 1;
    g_menuCursorTargetY = g_menuCursorY;

    DWORD idleStart = timeGetTime();
    unsigned int idleStartSec = idleStart / 1000;

    /* Main frame loop */
    while (1) {
        platform_pump_events();
        g_currentTime = timeGetTime();
        DWORD now = timeGetTime();
        unsigned int elapsedSec = now / 1000 - idleStartSec;
        unsigned int sign = (int)elapsedSec >> 31;
        unsigned int elapsed = (elapsedSec ^ sign) - sign;

        cdStatus = GetLogicalCDTrack();
        if (cdStatus != 5) {
            UpdateCDPlayback(5);
        }

        if (g_fadeState != FADE_VISIBLE) {
            UpdateFade();
        }

        /* Exit when fade-out completes */
        if (g_fadeLevel == -256) {
            return g_screenResult;
        }

        ReadInput();

        if (g_fadeState == FADE_VISIBLE) {
            /* A/Start = confirm */
            if ((g_inputBits & 6) != 0 && g_menuCursorTargetY == g_menuCursorY) {
                PlaySoundEffect(2, 0, 0);
                g_screenResult = g_menuCursor + 1;
                g_lastMenuSelection = g_menuCursor;
                g_fadeState = FADE_OUT;
                g_nextScreenId = 3;
            }

            /* B = back */
            if ((g_inputBits & 1) != 0) {
                PlaySoundEffect(0, 0, 0);
                g_screenResult = SCREEN_BACK;
                g_fadeState = FADE_OUT;
            }

            /* 30-second idle timeout */
            if (elapsed > 30) {
                PlaySoundEffect(0, 0, 0);
                g_screenResult = SCREEN_TITLE;
                g_fadeState = FADE_OUT;
            }

            /* D-pad navigation (only when cursor animation complete) */
            if (g_menuCursorTargetY == g_menuCursorY) {
                /* Up */
                if ((g_inputBits & 0x40) != 0 && (g_inputBits & 0x80) == 0 &&
                    minItem < g_menuCursor)
                {
                    int newItem = g_menuCursor - 1;
                    /* Skip multiplayer if unavailable */
                    if (newItem == 3 && g_networkAvailable == 0) {
                        newItem = g_menuCursor - 2;
                    }
                    g_menuCursor = newItem;
                    PlaySoundEffect(1, 0, 0);
                    idleStart = timeGetTime();
                    idleStartSec = idleStart / 1000;
                }

                /* Down */
                if ((g_inputBits & 0x80) != 0 && (g_inputBits & 0x40) == 0 &&
                    g_menuCursor < 4)
                {
                    int newItem = g_menuCursor + 1;
                    if (newItem == 3 && g_networkAvailable == 0) {
                        newItem = g_menuCursor + 2;
                    }
                    g_menuCursor = newItem;
                    PlaySoundEffect(1, 0, 0);
                    idleStart = timeGetTime();
                    idleStartSec = idleStart / 1000;
                }

                g_menuCursorTargetY = g_menuItemPositions[g_menuCursor + posOff];
            }
        }

        /* Smooth cursor scroll (+/-4 per frame) */
        if (g_menuCursorY < g_menuCursorTargetY) {
            g_menuCursorY += 4;
        }
        else if (g_menuCursorTargetY < g_menuCursorY) {
            g_menuCursorY -= 4;
        }
        g_menuAnimY = g_menuCursorY;

        /* Rotate 3D character model */
        g_modelRotation = (g_modelRotation + 0x20) & 0xFFF;

        /* Render */
        ProcessTpageStates();
        BeginFrame();
        D3DAppClearBackBuffer();
        EndFrame();

        BeginFrame();

        /* Menu UI quads — xPos/yPos from x86 disasm of 0x4885BC.
         * EAX=xPos, EDX=yPos are Watcom register params. */
        /* "SELECT MODE" banner (two halves) */
        DrawTexturedQuad(0, 0x20, 0x447A0000, 0x140, 0x40, g_uiTexPage + 1,
                         0, 0, 0xA0, 0x20, VERTEX_WHITE);
        DrawTexturedQuad(0x140, 0x20, 0x447A0000, 0x140, 0x40, g_uiTexPage + 1,
                         0, 0x20, 0xA0, 0x20, VERTEX_WHITE);
        
        /* Cursor highlight */
        DrawTexturedQuad(0x80, 0x120, 0x41200000, 0x180, 0x20, g_uiTexPage + 1,
                         0, g_menuCursor * 0x10 + 0x40, 0xC0, 0x10, VERTEX_WHITE);
 
        /* Menu item icons — xPos = g_menuItemPositions[i] * 2 (from x86: add eax,eax).
         * Normal state icons (depth 0x43FA0000), then hover copies behind
         * (depth 0x43F78000) if network available. */
        DrawTexturedQuad(g_menuItemPositions[0 + posOff] * 2, 0x164, 0x43FA0000, 0x70, 0x60,
                         g_uiTexPage + 1, 0, 0xA0, 0x38, 0x30, VERTEX_WHITE);
        DrawTexturedQuad(g_menuItemPositions[1 + posOff] * 2, 0x164, 0x43FA0000, 0x70, 0x60,
                         g_uiTexPage + 1, 0x38, 0xA0, 0x38, 0x30, VERTEX_WHITE);
        DrawTexturedQuad(g_menuItemPositions[2 + posOff] * 2, 0x164, 0x43FA0000, 0x70, 0x60,
                         g_uiTexPage + 1, 0x70, 0xA0, 0x38, 0x30, VERTEX_WHITE);

        /* Network icon — UV (0x38, 0xD0) in SMODE00 */
        if (g_networkAvailable) {
            DrawTexturedQuad(g_menuItemPositions[3 + posOff] * 2, 0x164, 0x43FA0000, 0x70, 0x60,
                             g_uiTexPage + 1, 0x38, 0xD0, 0x38, 0x30, VERTEX_WHITE);
        }

        /* Options icon */
        DrawTexturedQuad(g_menuItemPositions[4 + posOff] * 2, 0x164, 0x43FA0000, 0x70, 0x60,
                         g_uiTexPage + 1, 0, 0xD0, 0x38, 0x30, VERTEX_WHITE);

        /* Character model display area — xPos tracks selected icon.
         * Binary 0x488C0B-0x488C1B: eax = [0x9252C0] - 4, eax *= 2 */
        DrawTexturedQuad((g_menuAnimY - 4) * 2, 0x14C, 0x43F50000,
                         0x80, 0x80, g_uiTexPage + 1,
                         0xC0, 0xC0, 0x40, 0x40, VERTEX_WHITE);

        /* 3D rotating character model — 0x488f33-0x488f41 (D3D path)
         * D3D path calls Draw3DModelD3D at 0x488f41. */
        intptr_t menuObjPtr = (intptr_t)((char *)g_objectStructArray +
                              g_menuModelIndices[g_menuCursor] * 0x44);
        Draw3DModelD3D(0, 0, 0x400, 0, g_modelRotation, 0, menuObjPtr, 0);

        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }
        RenderWavingMenuBackground();                     /* 0x004C68B8 — software twin */
        EndFrame();
        FlipD3D();
        
        g_totalFrames2++;
        g_totalFrames++;

        /* 30fps cap */
        WaitForFrameCap();
    }
}
