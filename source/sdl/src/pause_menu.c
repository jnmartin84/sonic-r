/**
 * pause_menu.c — Pause menu input + rendering
 *
 * RunPauseMenu         — 0x004CDD24 — 306 bytes
 * DrawPauseOverlay  — 0x004D13E8 — 474 bytes
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"

/* Pause Menu State Variables */
/* Original binary addresses:
 *   0x901C10 = s_pauseMenuSelection
 *   0x901C14 = s_pauseMenuActive
 *   0x901C2C = s_pauseMenuAnimCounter
 *   0x901C34 = g_pauseLatch (declared in sonicr_globals.h)
 */

static int s_pauseMenuSelection = 0;    /* 0=resume, 1=options, 2=quit */
static int s_pauseMenuActive = 0;       /* D-pad debounce flag */
static int s_pauseMenuAnimCounter = 0;  /* frame counter for UI animation */

/**
 * RunPauseMenu — 0x004CDD24 — 306 bytes
 *
 * Called once per frame while g_isPaused != 0.
 * Handles D-pad navigation and Start confirmation.
 *
 * Returns:
 *   0 = stay paused
 *   1 = resume (selection 0 + 1)
 *   2 = options (selection 1 + 1)
 *   3 = quit   (selection 2 + 1)
 */
int RunPauseMenu(void)
{
    /* 0x4cdd27: early exit if exit-race already requested */
    if (g_exitRaceFlag != 0) { /* EAX=0 */
        return 0;
    }

    /* 0x4cdd37: check Start button (bit 3 = 0x08) */
    if ((g_inputBits & 0x08) != 0) {
        /* Start pressed — binary checks g_pauseLatch at 0x4cdd40 */
        if (g_pauseLatch == 0) {                                /* 0x4cdd40: [0x901C34] */
            PlaySoundEffect(2, 0, 0);                                 /* 0x4cdd4a: EAX=2 */
            return s_pauseMenuSelection + 1;                    /* 0x4cdd56 */
        }
        /* g_pauseLatch != 0 — skip confirm, fall through to D-pad */
    }
    else {
        /* Start not pressed — clear latch (0x4cdd60: edx=0 from exitRaceFlag check) */
        g_pauseLatch = 0;                                       /* 0x4cdd60 */

        unsigned char dp = g_inputBits;

        /* Left (0x10) without Right (0x20) */
        if ((dp & 0x10) != 0 && (dp & 0x20) == 0) {             /* 0x4cdd6c */
            if (s_pauseMenuActive == 0) {                       /* 0x4cdd76 */
                if (s_pauseMenuSelection > 0) {                 /* 0x4cdd83 */
                    PlaySoundEffect(1, 0, 0);                         /* 0x4cdd8c: EAX=1 */
                    s_pauseMenuSelection--;                     /* 0x4cdd9c */
                    /* 0x4cdda2: cmp [0x8fb950], 2 / 0x4cdda9: jle 0x4cddbc
                     * — option 1 (Retry) is skipped only above Time Attack:
                     * Special race (3) and the internal post-upgrade state
                     * (4). The signed jle spares RACE_TIMEATTACK == 2, so
                     * Retry stays selectable in every TA sub-mode. */
                    if (g_raceType > RACE_TIMEATTACK && s_pauseMenuSelection == 1) {
                        s_pauseMenuSelection = 0;               /* 0x4cddb4 */
                    }
                }
            }
            s_pauseMenuActive = 1;                              /* 0x4cddbc */
        }
        /* Right (0x20) without Left (0x10) */
        else if ((dp & 0x20) != 0 && (dp & 0x10) == 0) {        /* 0x4cddcb */
            if (s_pauseMenuActive == 0) {                       /* 0x4cdddb */
                if (s_pauseMenuSelection < 2) {                 /* 0x4cdde4 */
                    PlaySoundEffect(1, 0, 0);                         /* 0x4cdded: EAX=1 */
                    s_pauseMenuSelection++;                     /* 0x4cddfd */
                    /* 0x4cde03: cmp [0x8fb950], 2 / 0x4cde0a: jle 0x4cde1f
                     * — same guard as the left-press above. */
                    if (g_raceType > RACE_TIMEATTACK && s_pauseMenuSelection == 1) {
                        s_pauseMenuSelection = 2;               /* 0x4cde15 */
                    }
                }
            }
            s_pauseMenuActive = 1;                              /* 0x4cde1f */
        }
        else {
            /* No D-pad — clear debounce */
            s_pauseMenuActive = 0;                              /* 0x4cde3c */
        }
    }

    s_pauseMenuAnimCounter++;                                   /* 0x4cde43 */
    return 0;
}

/**
 * GetPauseMenuSelection — helper for rendering code
 */
int GetPauseMenuSelection(void)
{
    return s_pauseMenuSelection;
}

/**
 * GetPauseMenuAnimCounter — helper for rendering code
 */
int GetPauseMenuAnimCounter(void)
{
    return s_pauseMenuAnimCounter;
}

/**
 * ResetPauseMenuState — called on pause entry (g_isPaused 0→1)
 */
void ResetPauseMenuState(void)
{
    s_pauseMenuSelection = 0;
    s_pauseMenuActive = 0;
    s_pauseMenuAnimCounter = 0;
}

/**
 * DrawPauseOverlay — 0x004D13E8 — 474 bytes
 *
 * Pause overlay renderer. Called from RenderHUD Pass 3.
 *
 * Two modes:
 *   g_demoMode != DEMO_NONE : blinking debug timing overlay
 *   g_isPaused != 0 : full pause menu (background + title + 3 options)
 *
 * Menu option highlight: selected item uses uvY=0x3d, unselected uses uvY=0x32.
 * All quads are sourced from g_tpageObjects.
 */
void DrawPauseOverlay(void)
{
    int tpage = g_tpageObjects;                                 /* 0x8f6c38 */

    /* Debug/replay mode overlay */
    if (g_demoMode != DEMO_NONE) {                              /* 0x4d13ed */
        int remainder = g_totalFrames % g_raceSpeedMult;        /* 0x4d1407: idiv */
        int half = g_raceSpeedMult / 2;                         /* 0x4d1417: sar 1 */
        if (remainder >= half)
            return;                                             /* 0x4d141b: jge exit */

        /* Small debug timing quad */
        DrawTexturedQuad(0xe2, 0xd6, 0x40a00000,                /* 0x4d1444: xPos=226, yPos=214, depth=5.0f */
                         0xbc, 0x34, tpage,                     /* width=188, height=52 */
                         0xa2, 0x18, 0x5e, 0x1a,                /* uvX=162, uvY=24, uvW=94, uvH=26 */
                         VERTEX_WHITE);                          /* 0x4d1421 */
        return;
    }

    /* Online pause messages / resume countdown (net/network.c). */
    { extern void NetPauseDraw(void); NetPauseDraw(); }

    /* Pause menu overlay */
    if (g_isPaused == 0)
        return;                                                 /* 0x4d1453 */

    /* Background box */
    DrawTexturedQuad(0xe0, 0xc0, 0x40a00000,                    /* 0x4d1486: xPos=224, yPos=192, depth=5.0f */
                     0xc0, 0x80, tpage,                         /* width=192, height=128 */
                     0xa0, 0x68, 0x60, 0x40,                    /* uvX=160, uvY=104, uvW=96, uvH=64 */
                     VERTEX_WHITE);                              /* 0x4d1460-0x4d1490 */

    /* "PAUSE" title */
    DrawTexturedQuad(0x10a, 0xb4, 0x40800000,                   /* 0x4d14b5: xPos=266, yPos=180, depth=4.0f */
                     0x6c, 0x1a, tpage,                         /* width=108, height=26 */
                     0x78, 0xc2, 0x36, 0xd,                     /* uvX=120, uvY=194, uvW=54, uvH=13 */
                     VERTEX_WHITE);                              /* 0x4d1495-0x4d14bf */

    int sel = GetPauseMenuSelection();
    int uvY;
    /* Option 0 — Resume:  yPos=0xdc(220), uvX=0x32(50) */
    uvY = (sel == 0) ? 0x3d : 0x32;                             /* 0x4d14d4: selected=61, unselected=50 */
    DrawTexturedQuad(0x140 - 0x42, 0xdc, 0x40800000,            /* xPos=254, yPos=220, depth=4.0f */
                     0x42 * 2, 0x16, tpage,                     /* width=132, height=22 */
                     0x32, uvY, 0x42, 0xb,                      /* uvX=50, uvW=66, uvH=11 */
                     VERTEX_WHITE);                              /* 0x4d14e1-0x4d150f */

    /* Option 1 — Options/Restart:  yPos=0xf4(244), uvX=0x74(116) */
    uvY = (sel == 1) ? 0x3d : 0x32;                             /* 0x4d1524 */
    DrawTexturedQuad(0x140 - 0x42, 0xf4, 0x40800000,            /* xPos=254, yPos=244, depth=4.0f */
                     0x42 * 2, 0x16, tpage,                     /* width=132, height=22 */
                     0x74, uvY, 0x42, 0xb,                      /* uvX=116, uvW=66, uvH=11 */
                     VERTEX_WHITE);                              /* 0x4d1535-0x4d1563 */
    

    /* Option 2 — Quit:  yPos=0x10c(268), uvX=0xb6(182) */
    uvY = (sel == 2) ? 0x3d : 0x32;                             /* 0x4d1578 */
    DrawTexturedQuad(0x140 - 0x42, 0x10c, 0x40800000,           /* xPos=254, yPos=268, depth=4.0f */
                     0x42 * 2, 0x16, tpage,                     /* width=132, height=22 */
                     0xb6, uvY, 0x42, 0xb,                      /* uvX=182, uvW=66, uvH=11 */
                     VERTEX_WHITE);                              /* 0x4d1589-0x4d15b7 */
    
}
