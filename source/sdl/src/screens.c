#ifdef SONICR_XBOX
#include <stdio.h>
#endif
/**
 * screens.c — Screen functions (game state machine screens)
 *
 * Each screen function runs its own frame loop and returns a result code.
 * See state_machine.md for the full flow diagram.
 */

#include <stdlib.h>
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "platform.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* Forward declarations for screen-specific rendering helpers */
void LoadTitleTextureD3D(void);
void SetupD3DTexturesBegin(void);      /* FUN_00438ca4 */
void FinalizeMenuTexturesD3D(void);
void ProcessTpageStates(void);
void RenderBackground(void);           /* FUN_00435868 */
void RenderFadeOverlay(void);          /* FUN_00461df4 */
void SoftwareRenderSortedPolygons(void);
void SetViewportFromConfig(int *config);
void ReadInput(void);
void FlipSoftware(void);
void platform_pump_events(void);

extern void LoadTPageRGB(int tpage, const char *filename);
extern void SetTitleTextureFile(const char *path);

/**
 * Helper: 30fps frame cap.
 * Waits until at least 1/30th second has passed since g_currentTime, while
 * keeping the platform event pump alive. On Windows, SDL_Delay() == Sleep()
 * which does NOT service the Win32 message queue — sleeping ~30ms straight
 * starves keyboard messages until something else (e.g. mouse motion) kicks
 * the queue, which is the "wiggle the mouse to advance menus" symptom.
 */
void WaitForFrameCap(void)
{
#ifdef SONICR_DC
    /* DC: block on vblank IRQ via genwait — no busy-poll on gettimeofday.
     * 60 Hz vblank × 2 = 30 Hz cap. */
    extern void DC_WaitForVBlank2(void);
    DC_WaitForVBlank2();
    g_currentFPS = 30;
    return;
#elif defined(__EMSCRIPTEN__)
    DWORD now = timeGetTime();
    int elapsed = (int)(now - g_currentTime);
    int remaining = 33 - elapsed;
    emscripten_sleep(remaining > 0 ? remaining : 0);
    g_currentFPS = 30;
#else
    while (1) {
        DWORD now = timeGetTime();
        int elapsed = (int)(now - g_currentTime);
        int remaining = 33 - elapsed;
        if (remaining <= 0) {
            break;
        }
        platform_pump_events();
        platform_sleep_ms(remaining > 5 ? 5 : remaining);
    }
    g_currentFPS = 30;
#endif
}

/**
 * SegaLogoScreen — 0x004DC1E0 — 605 bytes
 * Displays the SEGA logo with fade-in/fade-out.
 * Auto-advances after 3 seconds or on A+Start press.
 *
 * D3D path: two render passes per frame.
 *   Pass 1: RenderBackground (wallpaper) + RenderFadeOverlay (iris iris)
 *   Pass 2: RenderLogoQuads (6 textured quads for the logo image)
 */
int SegaLogoScreen(void)
{
    DebugLog("SegaLogoScreen\n");

    g_uiTexPage = 1;
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupD3DTexturesBegin();
    LoadTPageRGB(0, DATA_DIR SEP "BIN" SEP "TITLES" SEP "TITLES00.RAW");
    SetTitleTextureFile(DATA_DIR SEP "BIN" SEP "TITLES" SEP "SEGALOGO.RAW");
    LoadTitleTextureD3D();
    ProcessTpageStates();
    FinalizeMenuTexturesD3D();
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) ResumeCD();
#endif

    g_fadeLevel = -0x100;
    g_fadeState = FADE_IN;
    g_fadeSpeed = 0xc;
    g_totalFrames = 0;
    g_renderEnabled = 1;

    DWORD startTime = timeGetTime();

    while (1) {
        platform_pump_events();  /* pump SDL events so window stays responsive */
        g_currentTime = timeGetTime();

        DWORD now = timeGetTime();
        uint32_t elapsed = now / 1000 - startTime / 1000;
        uint32_t sign = (int)elapsed >> 31;

        if (g_fadeState != FADE_VISIBLE) {
            UpdateFade();
        }
        if (g_fadeLevel == -256) {
            break;
        }

        ReadInput();

        if (g_fadeState == FADE_VISIBLE) {
            if (3 < (int)((elapsed ^ sign) - sign)) {   /* line 7432: >3 seconds elapsed */
                g_fadeState = FADE_OUT;
            }
            if (((g_inputBits & 6) != 0) && ((g_inputBits & 1) != 0)) { /* line 7435: A+Start */
                g_fadeState = FADE_OUT;
            }
        }

        g_renderPass = 1;

        ProcessTpageStates();
        BeginFrame();
        RenderBackground();
        EndFrame();
        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }
        BeginFrame();
        RenderLogoQuads();                           /* line 7467: FUN_0046154c */
        EndFrame();
        FlipD3D();

        g_totalFrames2 = g_totalFrames2 + 1;
        g_totalFrames = g_totalFrames + 1;

        WaitForFrameCap();
    }

    return SCREEN_OK;
}

/**
 * TravellersTalesLogoScreen — 0x004DC440 — 608 bytes
 * Identical structure to SegaLogoScreen. Loads TTLOGO.RAW instead of SEGALOGO.RAW.
 */
int TravellersTalesLogoScreen(void)
{
    DebugLog("TravellersTalesLogoScreen\n");

    g_uiTexPage = 1;
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupD3DTexturesBegin();
    LoadTPageRGB(0, DATA_DIR SEP "BIN" SEP "TITLES" SEP "TITLES00.RAW");
    SetTitleTextureFile(DATA_DIR SEP "BIN" SEP "TITLES" SEP "TTLOGO.RAW");
    LoadTitleTextureD3D();
#ifdef SONICR_XBOX
    fprintf(stderr, "  mark: TT LoadTitleTextureD3D returned\n");
#endif
    ProcessTpageStates();
#ifdef SONICR_XBOX
    fprintf(stderr, "  mark: TT ProcessTpageStates done\n");
#endif
    FinalizeMenuTexturesD3D();
#ifdef SONICR_XBOX
    fprintf(stderr, "  mark: TT FinalizeMenuTexturesD3D done\n");
#endif
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) ResumeCD();
#endif

    g_fadeLevel = -0x100;
    g_fadeState = FADE_IN;
    g_fadeSpeed = 0xc;
    g_totalFrames = 0;
    g_renderEnabled = 1;

    DWORD startTime = timeGetTime();

    while (1) {
        platform_pump_events();
        g_currentTime = timeGetTime();

        DWORD now = timeGetTime();
        uint32_t elapsed = now / 1000 - startTime / 1000;
        uint32_t sign = (int)elapsed >> 31;

        if (g_fadeState != FADE_VISIBLE) {
            UpdateFade();
        }
        if (g_fadeLevel == -256) break;

        ReadInput();

        if (g_fadeState == FADE_VISIBLE) {
            if (3 < (int)((elapsed ^ sign) - sign)) {
                g_fadeState = FADE_OUT;
            }
            if (((g_inputBits & 6) != 0) && ((g_inputBits & 1) != 0)) {
                g_fadeState = FADE_OUT;
            }
        }

        g_renderPass = 1;

        ProcessTpageStates();
        BeginFrame();
        RenderBackground();
        EndFrame();
        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }
        BeginFrame();
        RenderLogoQuads();
        EndFrame();
        FlipD3D();

        g_totalFrames2 = g_totalFrames2 + 1;
        g_totalFrames = g_totalFrames + 1;

        WaitForFrameCap();
    }

    return SCREEN_OK;
}

/**
 * TitleScreen — 0x004DCFCC — 4069 bytes
 * Main title screen with rotating "SONIC R" logo, animated "S O N I C" models,
 * camera orbit.
 * Returns: 1 = Start pressed (go to main menu)
 *          2 = timeout/A+B (go to demo)
 */
int TitleScreen(void)
{
    DebugLog("TitleScreen\n");
    FindSonicRCD();                                      /* line 7738: InitScreenCommon = FUN_004d0f4c */

    /* Set view matrix to identity for sparkle particle rendering.
     * The race loop calls BuildViewMatrix, but the title screen doesn't.
     * Identity: forward=(0,0,4096), right=(4096,0,0), up=(0,4096,0). */
    g_viewMtx02 = 0;
    g_viewMtx12 = 0;
    g_viewMtx22 = 4096; /* forward = +Z */
    g_viewMtx00 = 4096;
    g_viewMtx10 = 0;
    g_viewMtx20 = 0; /* right = +X */
    g_viewMtx01 = 0;
    g_viewMtx11 = 4096;
    g_viewMtx21 = 0; /* up = +Y */

    int bVar4 = 1;
    g_titleLogoAngleTarget = g_titleLogoAngle;
    g_titleInputFlag = 0;
    UpdateCDPlayback(2);
    g_renderEnabled = 1;

    DWORD begin = timeGetTime();
    g_pressStartAngle = begin & 0xFFF;
    g_titleLogoColorLatch = 1;
    begin = timeGetTime();

    InitParticleDisplayParams();

    /* 12 random color targets — 4 corners × R/G/B  
     * Each: Random() / 0x80 (signed divide with Watcom rounding) */
    int randInt;
    #define SDIV128(v) ((v) / 128)

    randInt = Random();
    g_pressStartTargetR[0] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetG[0] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetB[0] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetR[1] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetG[1] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetB[1] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetR[2] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetG[2] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetB[2] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetR[3] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetG[3] = SDIV128(randInt);
    randInt = Random();
    g_pressStartTargetB[3] = SDIV128(randInt);

#undef SDIV128

    /* Position 5 character models (S O N I C) in the object struct array */
    {
        int *obj = (int *)g_objectStructArray;

        /* First character (index g_titleCharIndices[0]=2) initial position */
        g_titleLogoPosY = (int)0xFFF9C000;
        obj[100 / 4] = (int)0xFFFFFF06;
        obj[0x68 / 4] = 100;
        obj[0x6C / 4] = 0x380;
        g_titleLogoPosZ = 0x380000;
        obj[0x44 / 4] = obj[100 / 4];
        g_titleLogoPosX = (int)0xFFF06000;
        g_titleLogoRotYaw = 0;
        obj[0x48 / 4] = obj[0x68 / 4];
        g_titleLogoRotPitch = 0;
        g_titleLogoRotRoll = 0;
        obj[0x4C / 4] = obj[0x6C / 4];

        /* Set height (offset 0x24) = 300 for each of the 5 characters,
         * then copy height to offset 0x04 (current Y). Stride = 0x44 per object. */
        for (int i = 0; i < 5; i++) {                       /* lines 7787-7795 */
            unsigned char cIdx = g_titleCharIndices[i];
            int *charObj = (int *)((char *)g_objectStructArray + (unsigned int)cIdx * 0x44);
            charObj[0x24 / 4] = 300;
            charObj[0x04 / 4] = charObj[0x24 / 4];      /* line 7793-7794 */
        }

        /* Set rotation offset (0x20) per character and copy to position (0x00) */
        {
            int *base = (int *)g_objectStructArray;
            int *c;

            /* S Char 0 (g_titleCharIndices[0]=2): offset 0x20 = 0xFFFFFE0C, pos = same */
            c = (int *)((char *)base + (unsigned int)g_titleCharIndices[0] * 0x44);
            c[0x20 / 4] = (int)0xFFFFFE0C;
            c[0] = c[0x20 / 4];

            /* O Char 1 (g_titleCharIndices[1]=3): 0xFFFFFEA2 */
            c = (int *)((char *)base + (unsigned int)g_titleCharIndices[1] * 0x44);
            c[0x20 / 4] = (int)0xFFFFFEA2;
            c[0] = c[0x20 / 4];

            /* N Char 2 (g_titleCharIndices[2]=4): 0xFFFFFF38 */
            c = (int *)((char *)base + (unsigned int)g_titleCharIndices[2] * 0x44);
            c[0x20 / 4] = (int)0xFFFFFF38;
            c[0] = c[0x20 / 4];

            /* I Char 3 (g_titleCharIndices[3]=5): 0xFFFFFF9C */
            c = (int *)((char *)base + (unsigned int)g_titleCharIndices[3] * 0x44);
            c[0x20 / 4] = (int)0xFFFFFF9C;
            c[0] = c[0x20 / 4];

            /* C Char 4 (g_titleCharIndices[4]=0): 0x00000000 */
            c = (int *)((char *)base + (unsigned int)g_titleCharIndices[4] * 0x44);
            c[0x20 / 4] = 0;
            g_menuCursor = 0;
            c[0] = c[0x20 / 4];
        }
    }

    g_totalFrames = 0;

    /* Random initial camera orbit speed */
    g_modelRotation = 0x1C;
    {
        int r = Random();
        if (0x4000 < r) {
            g_modelRotation = -g_modelRotation;
        }
    }
    g_menuExtraY = 0x27;
    {
        int r = Random();
        if (0x4000 < r) {
            g_menuExtraY = -g_menuExtraY;
        }
    }
    g_menuScrollTarget = 0;
    g_menuScrollX = 0;

    int local_24 = 0;                                    /* return value */

    /* ================================================================
     * Main frame loop
     * ================================================================ */
    do {
        platform_pump_events();
        g_currentTime = timeGetTime();

        DWORD now = timeGetTime();
        uint32_t elapsed = now / 1000 - begin / 1000;
        uint32_t delta = (int)elapsed >> 31;

        if (g_fadeState != FADE_VISIBLE) {
            UpdateFade();
        }
        if (g_fadeLevel == -256) {
            StopCD();
            g_titleLogoAngle = g_titleLogoAngleTarget;
            return local_24;
        }

        /* Animate 5 character models with sine bobbing */
        {
            uint32_t uVar10 = (g_menuCursor + 0x40) & 0xFFF;
            int iVar6 = 4;
            uint32_t uVar12 = uVar10;
            g_menuCursor = uVar10;

            do {
                int *base = (int *)g_objectStructArray;
                unsigned char cIdx = g_titleCharIndices[iVar6];

                /* Set rotation (offset 0x1A, unsigned short) = -(sinTable[uVar10] >> 5) & 0xFFF */
                *(unsigned short *)((char *)base + (unsigned int)cIdx * 0x44 + 0x1A) =
                    (unsigned short)(-(g_sinTable[uVar10] >> 5)) & 0xFFF;

                /* Set Y position (offset 0x28) = (sinTable[uVar12] >> 8) + 0x400 */
                int *charObj = (int *)((char *)base + (unsigned int)cIdx * 0x44);
                charObj[0x28 / 4] = (g_sinTable[uVar12] >> 8) + 0x400;

                uVar10 = (uVar10 + 0x300) & 0xFFF;

                /* Copy Y position to offset 0x08 */
                charObj[0x08 / 4] = charObj[0x28 / 4];

                iVar6 = iVar6 - 1;
                uVar12 = (uVar10 + 0x200) & 0xFFF;
            } while (iVar6 >= 0);
        }

        ReadInput();

        if (g_fadeState == FADE_VISIBLE) {
            /* Input handling */
            if ((g_inputBits & 8) != 0) {                /* Start pressed */
                local_24 = 1;
                g_fadeState = FADE_OUT;
                PlaySoundEffect(2, 0, 0);
            }
            else if (((g_inputBits & 6) != 0) && ((g_inputBits & 1) != 0)) {
                /* A+B pressed — go to demo */
                g_fadeState = FADE_OUT;
                local_24 = 2;
            }
            else {
                /* Timeout check — 45s (0x2D). Debug: SONICR_DEMO_TRACE=1 shortens
                 * to 1s so attract-demo test cycles don't wait on the title. */
                static int s_attractTimeout = -1;
                if (s_attractTimeout < 0)
                    s_attractTimeout = getenv("SONICR_DEMO_TRACE") ? 1 : 0x2D;

                if ((int)((elapsed ^ delta) - delta) < s_attractTimeout) {
                    if ((g_cdPlaybackActive == 1) && (g_titleInputFlag == 0)) {
                        int cdStat = GetLogicalCDTrack();
                        if (cdStat != 2) {
                            StopCD();
                        }
                    }
                }
                else {
                    g_fadeState = FADE_OUT;
                    StopCD();
                    local_24 = 2;
                }
            }

            /* D-pad: adjust R logo rotation manually */
            if (((g_inputBits & 0x10) != 0) && (g_modelRotation > -0x31)) {
                g_modelRotation = g_modelRotation - 2;
                g_menuScrollTarget = 1;
            }
            if (((g_inputBits & 0x20) != 0) && (g_modelRotation < 0x31)) {
                g_modelRotation = g_modelRotation + 2;
                g_menuScrollTarget = 1;
            }
            if (((g_inputBits & 0x40) != 0) && (g_menuExtraY > -0x31)) {
                g_menuExtraY = g_menuExtraY - 2;
                g_menuScrollTarget = 1;
            }
            if (((g_inputBits & 0x80) != 0) && (g_menuExtraY < 0x31)) {
                g_menuExtraY = g_menuExtraY + 2;
                g_menuScrollTarget = 1;
            }

            /* R logo color preset cycling — 0x4dd588
             * test byte ptr [0x9020d8], 0x40
             *
             * Note the address: this is the LOW byte of
             * g_combinedInputState, not the 0x9020d9 high byte
             * (g_inputBits) the four rotation tests above read. Low-byte
             * bit 0x40 is LookBack / Camera Change (keyboard default
             * DIK_1, gamepad btn 5); high-byte bit 0x40 would be 0x4000,
             * D-pad Left. */
            if ((g_combinedInputState & 0x0040) == 0) {
                g_titleLogoColorLatch = 0;
            }
            else {
                if (g_titleLogoColorLatch == 0) {
                    g_titleLogoColorPreset = (g_titleLogoColorPreset + 1) % 7;
                    FillVertexColorsByDepth(
                        g_titleLogoColorPresets[g_titleLogoColorPreset][0],
                        g_titleLogoColorPresets[g_titleLogoColorPreset][1],
                        g_titleLogoColorPresets[g_titleLogoColorPreset][2],
                        g_titleLogoColorPresets[g_titleLogoColorPreset][3],
                        g_titleLogoColorPresets[g_titleLogoColorPreset][4],
                        g_titleLogoColorPresets[g_titleLogoColorPreset][5]
                    );
                }
                g_titleLogoColorLatch = 1;
            }

            /* A/B toggle: rotate track 180° and toggle mirror */
            if ((g_inputBits & 6) == 0) {
                bVar4 = 0;
            }
            else {
                if ((!bVar4) && (g_titleLogoEnabled == 1)) {
                    g_titleLogoAngleTarget = (g_titleLogoAngleTarget + 0x800) & 0xFFF;
                    g_mirrorMode = (g_mirrorMode + 1) & 1;
                }
                bVar4 = 1;
            }
        } /* end if FADE_VISIBLE */

        /* ================================================================
         * R logo rotation system
         * ================================================================ */
        {
            int iVar6 = g_menuScrollTarget + 1;
            if (g_menuScrollTarget >= 0x81) {
                /* Smooth deceleration mode */
                if ((int)g_titleLogoRotYaw < 0x801) {
                    g_titleLogoRotYaw = g_titleLogoRotYaw - (int)g_titleLogoRotYaw / 32;
                    g_modelRotation = (int)g_titleLogoRotYaw / 32;
                }
                else {
                    g_titleLogoRotYaw = g_titleLogoRotYaw + (int)(0x1000 - g_titleLogoRotYaw) / 32;
                    g_modelRotation = (int)-(0x1000 - g_titleLogoRotYaw) / 32;
                }

                if ((int)g_titleLogoRotPitch < 0x801) {
                    g_titleLogoRotPitch = g_titleLogoRotPitch - (int)g_titleLogoRotPitch / 32;
                    g_menuExtraY = (int)g_titleLogoRotPitch / 32;
                }
                else {
                    g_titleLogoRotPitch = g_titleLogoRotPitch + (int)(0x1000 - g_titleLogoRotPitch) / 32;
                    g_menuExtraY = (int)-(0x1000 - g_titleLogoRotPitch) / 32;
                }
                g_menuExtraY = g_menuExtraY >> 5;

                if ((int)g_titleLogoRotRoll < 0x801) {
                    g_titleLogoRotRoll = g_titleLogoRotRoll - (int)g_titleLogoRotRoll / 32;
                    g_menuScrollX = (int)g_titleLogoRotRoll / 32;
                }
                else {
                    g_titleLogoRotRoll = g_titleLogoRotRoll + (int)(0x1000 - g_titleLogoRotRoll) / 32;
                    g_menuScrollX = (int)-(0x1000 - g_titleLogoRotRoll) / 32;
                }
                g_menuScrollX = g_menuScrollX >> 5;

                g_menuScrollTarget = iVar6;

                /* Reset rotation after enough time */
                if (iVar6 > 0x180) {
                    g_modelRotation = 0x1C;
                    int r = Random();
                    if (0x4000 < r) {
                        g_modelRotation = -g_modelRotation;
                    }
                    g_menuExtraY = 0x27;
                    r = Random();
                    if (0x4000 < r) {
                        g_menuExtraY = -g_menuExtraY;
                    }
                    g_menuScrollTarget = 0;
                    g_menuScrollX = 0;
                }
            }
            else if (g_menuScrollTarget == 0) {
                /* Free rotation mode */
                iVar6 = g_modelRotation - 1;
                if ((int)g_titleLogoRotYaw < 0x801) {
                    g_modelRotation = g_modelRotation + 1;
                    iVar6 = g_modelRotation;
                }
                g_modelRotation = iVar6;

                if ((int)g_titleLogoRotPitch < 0x801) {
                    g_menuExtraY = g_menuExtraY + 1;
                }
                else {
                    g_menuExtraY = g_menuExtraY - 1;
                }

                if ((int)g_titleLogoRotRoll < 0x801) {
                    g_menuScrollX = g_menuScrollX + 1;
                }
                else {
                    g_menuScrollX = g_menuScrollX - 1;
                }
            }
            else {
                /* Decelerating from manual input */
                if (g_modelRotation > 0) {
                    g_modelRotation = g_modelRotation - 1;
                }
                if (g_modelRotation < 0) {
                    g_modelRotation = g_modelRotation + 1;
                }
                if (g_menuExtraY > 0) {
                    g_menuExtraY = g_menuExtraY - 1;
                }
                if (g_menuExtraY < 0) {
                    g_menuExtraY = g_menuExtraY + 1;
                }
                if (g_menuScrollX > 0) {
                    g_menuScrollX = g_menuScrollX - 1;
                }
                g_menuScrollTarget = iVar6;
                if (g_menuScrollX < 0) {
                    g_menuScrollX = g_menuScrollX + 1;
                }
            }

            /* Update rotation angles (only when menuScrollTarget < 0x81) */
            if (g_menuScrollTarget < 0x81) {
                g_titleLogoRotYaw = (g_titleLogoRotYaw - g_modelRotation) & 0xFFF;
                g_titleLogoRotPitch = (g_titleLogoRotPitch - g_menuExtraY) & 0xFFF;
                g_titleLogoRotRoll = (g_titleLogoRotRoll - g_menuScrollX) & 0xFFF;
            }
        }

        /* Write rotation to object struct */
        {
            int *obj = (int *)g_objectStructArray;
            *(unsigned short *)((char *)obj + 0x5C) = (unsigned short)g_titleLogoRotYaw;
            *(unsigned short *)((char *)obj + 0x5E) = (unsigned short)g_titleLogoRotPitch;
            *(unsigned short *)((char *)obj + 0x60) = (unsigned short)g_titleLogoRotRoll;
        }

        /* ================================================================
         * Animate vertex colors — chase targets at speed 5 per frame
         * ================================================================ */
        {
            int iVar7 = 0; /* byte offset into color arrays */
            do {
                /* R channel */
                int tgt = *(int *)((char *)g_pressStartTargetR + iVar7);
                int cur = *(int *)((char *)g_pressStartColorR + iVar7);
                if (cur < tgt) {
                    *(int *)((char *)g_pressStartColorR + iVar7) = cur + 5;
                    if (tgt < cur + 5) {
                        *(int *)((char *)g_pressStartColorR + iVar7) = tgt;
                    }
                }
                else if (tgt < cur) {
                    *(int *)((char *)g_pressStartColorR + iVar7) = cur - 5;
                    if (cur - 5 < tgt) {
                        *(int *)((char *)g_pressStartColorR + iVar7) = tgt;
                    }
                }

                /* G channel */
                tgt = *(int *)((char *)g_pressStartTargetG + iVar7);
                cur = *(int *)((char *)g_pressStartColorG + iVar7);
                if (cur < tgt) {
                    *(int *)((char *)g_pressStartColorG + iVar7) = cur + 5;
                    if (tgt < cur + 5) {
                        *(int *)((char *)g_pressStartColorG + iVar7) = tgt;
                    }
                }
                else if (tgt < cur) {
                    *(int *)((char *)g_pressStartColorG + iVar7) = cur - 5;
                    if (cur - 5 < tgt) {
                        *(int *)((char *)g_pressStartColorG + iVar7) = tgt;
                    }
                }

                /* B channel */
                tgt = *(int *)((char *)g_pressStartTargetB + iVar7);
                cur = *(int *)((char *)g_pressStartColorB + iVar7);
                if (cur < tgt) {
                    *(int *)((char *)g_pressStartColorB + iVar7) = cur + 5;
                    if (tgt < cur + 5) {
                        *(int *)((char *)g_pressStartColorB + iVar7) = tgt;
                    }
                }
                else if (tgt < cur) {
                    *(int *)((char *)g_pressStartColorB + iVar7) = cur - 5;
                    if (cur - 5 < tgt) {
                        *(int *)((char *)g_pressStartColorB + iVar7) = tgt;
                    }
                }

                /* When target reached, pick new random target */
                if (*(int *)((char *)g_pressStartTargetR + iVar7) ==
                    *(int *)((char *)g_pressStartColorR + iVar7)) {
                    int r = Random();
                    *(int *)((char *)g_pressStartTargetR + iVar7) = r / 0x80;
                }
                if (*(int *)((char *)g_pressStartTargetG + iVar7) ==
                    *(int *)((char *)g_pressStartColorG + iVar7)) {
                    int r = Random();
                    *(int *)((char *)g_pressStartTargetG + iVar7) = r / 0x80;
                }
                if (*(int *)((char *)g_pressStartTargetB + iVar7) ==
                    *(int *)((char *)g_pressStartColorB + iVar7)) {
                    int r = Random();
                    *(int *)((char *)g_pressStartTargetB + iVar7) = r / 0x80;
                }

                iVar7 = iVar7 + 4;
            } while (iVar7 != 0x10);
        }

        /* wobble */
        if (g_titleLogoEnabled == 1) {
            uint32_t uVar9_prev = g_titleLogoAngle - 0x40;
            if ((int)g_titleLogoAngle < (int)g_titleLogoAngleTarget) {
                int diff = g_titleLogoAngleTarget - g_titleLogoAngle;
                g_titleLogoAngle = g_titleLogoAngle + 0x40;
                if (diff > 0x800) {
                    g_titleLogoAngle = uVar9_prev;
                }
            }
            else if ((int)g_titleLogoAngleTarget < (int)g_titleLogoAngle) {
                int diff = g_titleLogoAngle - g_titleLogoAngleTarget;
                g_titleLogoAngle = g_titleLogoAngle + 0x40;
                if (diff < 0x800) {
                    g_titleLogoAngle = uVar9_prev;      /* line 8104 via goto */
                }
            }
            g_titleLogoAngle = g_titleLogoAngle & 0xFFF;
        }

        UpdateParticleSpawning();

        /* Periodic render pass change */
        if ((g_totalFrames & 0x1F) == 0x19) {
            g_renderPass = 2;
        }

        /* ================================================================
         * Rendering — D3D path (OpenGL replacement)
         * ================================================================ */

        /* Original D3D order — now matches exactly with GL depth test
         * handling the layering (Z values determine front-to-back). */
        ProcessTpageStates();
        BeginFrame();
        SetViewportFromConfig(g_viewportArray);      /* line 8165: FUN_004cc0e8 */
        RenderBackground();
        EndFrame();
        SetViewportFromConfig(g_viewportArray);      /* line 8168: FUN_004cc0e8 */
        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }
        BeginFrame();
        RenderTitleLogo();                           /* line 8173: FUN_004dcb14 */
        /* Actual register values at call site (from binary at VA 0x4DDE39):
         *   EAX=-225, EDX=100, EBX=0x3C0(960), ECX=g_titleLogoRotYaw
         *   Stack: g_titleLogoRotPitch, caller's EAX */
        /* Binary at 0x4dde39: stack params 3 and 4 (modelIdx, flag) are
         * both 0 — title renders model entry 0 ("R" letter). */
        RenderEnvMappedModel3D(-225, 100, 960, g_titleLogoRotYaw,
                               g_titleLogoRotPitch, 0, 0, 0); /* FUN_00468744 */
        /* Draw 5 character models — binary at 0x4DDE3E-0x4DDF43.
         * Each: EAX=xOffset, EDX=0x12C(300), EBX=obj[0x28](zBase),
         * ECX=0, stack: obj[0x18]>>16(rotation), 0, objPtr, 0,0,0. */
        {
            static const int s_titleCharXOff[5] = {
                -500, -350, -200, -100, 0  /* 0xFFFFFE0C..0 */
            };
            int *base = (int *)g_objectStructArray;
            int ci;
            for (ci = 0; ci < 5; ci++) {
                intptr_t objPtr = (intptr_t)((char *)base +
                                  (unsigned int)g_titleCharIndices[ci] * 0x44);
                int rot = *(short *)((char *)objPtr + 0x1A);
                int zBase = *(int *)((char *)objPtr + 0x28);
                Draw3DModelD3D(s_titleCharXOff[ci], 0x12C, zBase, 0,
                               rot, 0, objPtr, 0);
            }
        }
        RenderLogoQuads();
        /* Sparkles after background so additive blend has pixels to add to */
        DrawOtherParticles();
        EndFrame();
        FlipD3D();

        g_totalFrames2 = g_totalFrames2 + 1;
        g_totalFrames = g_totalFrames + 1;

        WaitForFrameCap();

    } while (1);
}
