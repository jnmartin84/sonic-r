/**
 * screen_course.c — Course/track selection screen
 *
 * CourseSelectScreen — 0x0048DE34 — 3193 bytes
 * Track selection with rotating 3D course preview.
 * 5 tracks + Emerald (track 5 shown only if unlocked).
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "sonicr_paths.h"
#include "r_state.h"

extern void SetupMenuTexturesD3D(void);
extern void FinalizeMenuTexturesD3D(void);
extern void WaitForFrameCap(void);
extern void platform_pump_events(void);
extern void platform_poll_events(unsigned char *buf, int len);
extern void StopCD(void);
extern int g_lastCourseSelection;       /* 0x0068AF94 */

/* GP / multiplayer lobby globals */

/* Preview model object index per cursor position — 0x0050272C (from ROM) */
static const int s_previewObjIndex[6] = { 2, 3, 0, 1, 4, 0 };
/* Track ID per cursor position — 0x00502754 (from ROM) */
static const int __attribute__((unused)) s_previewTrackId[6] = { 1, 2, 4, 3, 5, -20 };

/* Track position Y table — 0x00502718 (from ROM) */
static const int s_trackPositionY[6] = { 12, 72, 132, 192, 252, 2 };

/* Course-preview camera orbit angle — 0x9252DC, block[20] of the 0x92528C state
 * block. The multiplayer join screen uses the same slot for its first seat's
 * device tag and the load/save screen for its confirm state; the three screens
 * are never live together and each writes before it reads. Initialized to
 * 0x100 (256), adjusted ±8 per frame via L/R input, clamped to [1, 0x2A0].
 * Used to compute camera tilt for the 3D preview. */
#define s_previewCamAngle g_stateBlock92528C[20]

/**
 * CourseSelectScreen — 0x0048DE34 — 3193 bytes
 * Returns: track index + 1, SCREEN_BACK, or SCREEN_TITLE
 */
int CourseSelectScreen(void)
{
    DebugLog("CourseSelectScreen\n");

    /* Magenta tint — binary: R=0xFF, G=0, B=0xFF at 0x48DE5C */
    g_bgTintR = 0xFF;
    g_bgTintG = 0;
    g_bgTintB = 0xFF;
    g_menuState = 5;

    /* Load textures (from disasm 0x48DEC3-0x48DF1E) */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupMenuTexturesD3D();
    LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);
    TintBackgroundTPage(g_bgTintR, g_bgTintG, g_bgTintB);
    LoadTPageRGB(g_uiTexPage + 1, PATH_MENU_COURSE0);
    LoadTPageRGB(g_uiTexPage + 2, PATH_MENU_COURSE1);
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

    g_trackId = TRACK_RESORT_ISLAND;    /* 0x48DF74 */
    g_totalFrames = 0;
    g_modelRotation = 0;
    s_previewCamAngle = 0x100;      /* 0x48DFDF: initial camera angle = 256 */
    g_fadeState = FADE_IN;
    g_screenResult = SCREEN_BACK;
    g_renderEnabled = 1;

    /* 0x48DFB2-0x48DFC0: the remembered course is kept unless it is Radiant
     * Emerald and that course is not unlocked. */
    if (g_lastCourseSelection == 4 && g_gpAllTracksFlag == 0) {
        g_lastCourseSelection = 0;
    }
    g_menuCursor = g_lastCourseSelection;                   /* 0x48DFDA */
    g_menuCursorY = s_trackPositionY[g_menuCursor];         /* 0x48DFF1 */
    g_menuCursorTargetY = g_menuCursorY;

    DWORD idleStart = timeGetTime();
    unsigned int idleStartSec = idleStart / 1000;

    while (1) {
        platform_pump_events();
        g_currentTime = timeGetTime();
        DWORD now = timeGetTime();
        unsigned int elapsed = now / 1000 - idleStartSec;
        unsigned int sign = (int)elapsed >> 31;
        int idleElapsed = (int)((elapsed ^ sign) - sign);

        int cd = GetLogicalCDTrack();
        if (cd != 5) {
            UpdateCDPlayback(5);
        }
        if (g_fadeState != FADE_VISIBLE) {
            UpdateFade();
        }
        if (g_fadeLevel == -256) {
#ifdef SONICR_DC
            /* Stop music only when a course was actually chosen (forward to a
             * race) — the track load that follows hits the filesystem hard and
             * concurrent music streaming glitches on DC. On the back/title
             * exits the menu track must keep playing: charsel only Pause/
             * Resumes it on re-entry, it does not restart a stopped track. */
            if (g_screenResult != SCREEN_BACK && g_screenResult != SCREEN_TITLE) {
                StopCD();
            }
#endif
            return g_screenResult;
        }

        ReadInput();

        if (g_fadeState == FADE_VISIBLE) {
            /* Confirm — 0x48E0AC-0x48E118.
             * Binary swaps trackId 3↔4 here, but the C codebase uses
             * trackId 3=Ruin, 4=Factory consistently (opposite of binary).
             * No swap needed — cursor+1 already gives the right C trackId. */
            if ((g_inputBits & 6) != 0 && g_menuCursorTargetY == g_menuCursorY) {
                PlaySoundEffect(2, 0, 0);
                g_screenResult = g_menuCursor + 1;
                g_trackId = g_menuCursor + 1;
                g_fadeState = FADE_OUT;
                g_lastCourseSelection = g_menuCursor;           /* 0x48E113 */
            }
            /* Back */
            if ((g_inputBits & 1) != 0) {
                PlaySoundEffect(0, 0, 0);
                g_screenResult = SCREEN_BACK;
                g_fadeState = FADE_OUT;
            }
            /* Idle timeout */
            if (idleElapsed > 30) {
                PlaySoundEffect(0, 0, 0);
                g_screenResult = SCREEN_TITLE;
                g_fadeState = FADE_OUT;
            }
            /* Navigation */
            if (g_menuCursorTargetY == g_menuCursorY) {
                if ((g_inputBits & 0x40) && !(g_inputBits & 0x80) && g_menuCursor > 0) {
                    g_menuCursor--;
                    PlaySoundEffect(1, 0, 0);
                    idleStart = timeGetTime();
                    idleStartSec = idleStart / 1000;
                }
                if ((g_inputBits & 0x80) && !(g_inputBits & 0x40) && g_menuCursor < 4
                    && !(g_menuCursor >= 3 && g_gpAllTracksFlag == 0)) {
                    g_menuCursor++;
                    PlaySoundEffect(1, 0, 0);
                    idleStart = timeGetTime();
                    idleStartSec = idleStart / 1000;
                }
                g_menuCursorTargetY = s_trackPositionY[g_menuCursor];
            }
        }

        /* Course-preview camera orbit — 0x48E14B-0x48E194.
         * Input bits 0x10/0x20 adjust camera tilt angle ±8 per frame,
         * clamped to [1, 672]. */
        if (g_inputBits & 0x10) {
            s_previewCamAngle += 8;    /* 0x48E154 */
        }
        if (g_inputBits & 0x20) {
            s_previewCamAngle -= 8;    /* 0x48E164 */
        }
        if (s_previewCamAngle > 0x2A0) {
            s_previewCamAngle = 0x2A0;  /* 0x48E177 */
        }
        if (s_previewCamAngle < 1) {
            s_previewCamAngle = 1;          /* 0x48E18A */
        }

        /* Smooth cursor scroll */
        if (g_menuCursorY < g_menuCursorTargetY) {
            g_menuCursorY += 4;
            if (g_menuCursorY > g_menuCursorTargetY) {
                g_menuCursorY = g_menuCursorTargetY;
            }
        }
        else if (g_menuCursorTargetY < g_menuCursorY) {
            g_menuCursorY -= 4;
            if (g_menuCursorY < g_menuCursorTargetY) {
                g_menuCursorY = g_menuCursorTargetY;
            }
        }
        g_menuAnimY = g_menuCursorY;

        g_modelRotation = (g_modelRotation + 0x20) & 0xFFF;
        g_emeraldSineOffX = (g_emeraldSineOffX + 0x4D) & 0xFFF;
        g_emeraldSineOffY = (g_emeraldSineOffY + 0xB6) & 0xFFF;
        g_emeraldSineOffZ = (g_emeraldSineOffZ + 0x73) & 0xFFF;

        /* Rendering (D3D path from disasm 0x48E6A0-0x48E9E0) */
        ProcessTpageStates();
        BeginFrame();
        D3DAppClearBackBuffer();
        EndFrame();
        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }

        BeginFrame();

        /* #1-2: "SELECT COURSE" banner (left + right halves)
         * From disasm 0x48E725, 0x48E757 */
        DrawTexturedQuad(0, 0x20, 0x447A0000, 0x140, 0x40, g_uiTexPage + 1,
                         0, 0, 0xA0, 0x20, VERTEX_WHITE);
        DrawTexturedQuad(0x140, 0x20, 0x447A0000, 0x140, 0x40, g_uiTexPage + 1,
                         0, 0x20, 0xA0, 0x20, VERTEX_WHITE);

        /* #3: Selected track name — UV row from (0,64) area, 192x16 per name
         * From disasm 0x48E793. [0x9252B4] = cursor index. */
        int nameUvY = g_menuCursor * 0x10 + 0x40;
        DrawTexturedQuad(0x80, 0x120, 0x41C80000, 0x180, 0x20, g_uiTexPage + 1,
                         0, nameUvY, 0xC0, 0x10, VERTEX_WHITE);

        /* #4: Cursor highlight box — UV (0,192)-(64,256)
         * From disasm 0x48E7CD. Follows animated cursor position. */
        int cursorX = (g_menuCursorY - 4) * 2;
        DrawTexturedQuad(cursorX, 0x150, 0x43340000, 0x80, 0x80, g_uiTexPage + 1,
                         0, 0xC0, 0x40, 0x40, VERTEX_WHITE);

        /* Rotating 3D track preview model.
         * Translated from 0x48E9C0-0x48EA4D.
         * Sets g_trackId for correct tpage lookup, then calls
         * Draw3DModelD3D with camera orbit computed from s_previewCamAngle. */
        int objIndex = s_previewObjIndex[g_menuCursor];
        intptr_t objPtr = (intptr_t)((char *)g_objectStructArray + objIndex * 0x44);

        /* 0x48E9CC-0x48E9DA: set g_trackId for preview rendering.
         * Binary uses s_previewTrackId (binary mapping: 3=Factory,4=Ruin),
         * but C codebase uses 3=Ruin,4=Factory, so just use cursor+1. */
        g_trackId = g_menuCursor + 1;

        /* 0x48E9F2-0x48EA4D: compute camera params from s_previewCamAngle (V).
         *   EBX = cosTable[V & 0xFFF] >> 4       → zBase
         *   EDX = sinTable[(V*3) & 0xFFF] >> 7   → yOffset
         *   ECX = 0x1000 - V                      → angleA
         *   EAX = 0                                → xOffset */
        int V = s_previewCamAngle;
        int cosV = g_cosTable[V & 0xFFF];
        int zBase = ((cosV >> 2) << 10) >> 12;    /* cosTable[V] >> 4 */
        int tripleV = (V * 6) / 2;                /* signed div by 2 of V*6 = V*3 */
        int sinV3 = g_sinTable[tripleV & 0xFFF];
        int yOff = ((sinV3 >> 2) << 10) >> 15;   /* sinTable[V*3] >> 7 */
        int angleA = 0x1000 - V;

        Draw3DModelD3D(0, yOff, zBase, angleA,
                       g_modelRotation, 0, objPtr, 0);

        /* #5-6: Animated trophy icons — only if character is unlocked.
         * UV cycles through 8 frames at half-speed. From disasm 0x48E839. */
        /* g_charUnlockTable is a #define via sonicr_globals.h */
        static const int s_trackCharMap[] = { 0, 0, 1, 2, 3, 4 }; /* DAT_00502740 */
        int trackIdx = (g_trackId >= 1 && g_trackId <= 5) ? s_trackCharMap[g_trackId] : 0;
        if (g_charRaceState[trackIdx] == 2) {
            unsigned int frame = (g_totalFrames >> 1) & 0x1F;
            int uvX1 = ((g_totalFrames >> 1) & 7) << 5;
            int uvY1 = ((int)frame >> 3) * 0x28;
            /* Left trophy — 0x48e82f: EAX=0x60, EDX=0xc8. Bottom (0xc8+0x50
             * = 0x118) sits just above the course-name label at 0x120. */
            DrawTexturedQuad(0x60, 0xC8, 0x42C80000, 0x40, 0x50, g_uiTexPage + 2,
                             uvX1, uvY1, 0x20, 0x28, VERTEX_WHITE);
            unsigned int frame2 = 0x1F - frame;
            int uvX2 = (frame2 & 7) << 5;
            int uvY2 = ((int)(frame2 / 8)) * 0x28;
            /* Right trophy — 0x48e879: EAX=0x1e0, EDX=0xc8. Spins opposite
             * direction (frame2 = 0x1f - frame). */
            DrawTexturedQuad(0x1E0, 0xC8, 0x42C80000, 0x40, 0x50, g_uiTexPage + 2,
                             uvX2, uvY2, 0x20, 0x28, VERTEX_WHITE);
        }

        /* #7-11: Course preview thumbnails — UV from (200, i*48) area
         * From disasm 0x48E8B4-0x48E97E */
        int uvYOffsets[5] = { 0x00, 0x30, 0x60, 0x90, 0xC0 };
        for (int ti = 0; ti < 5; ti++) {
            DrawTexturedQuad(s_trackPositionY[ti] * 2, 0x168, 0x438C0000,
                             0x70, 0x60, g_uiTexPage + 1,
                             0xC8, uvYOffsets[ti], 0x38, 0x30, VERTEX_WHITE);
        }

        /* Locked overlay on Emerald thumbnail — SOFTWARE twin, 0x48E5AE.
         *
         * This screen forks on renderMode at 0x48E307 (`cmp [0x6DD860], 2 /
         * jne 0x48E6BB`) and draws the lock overlay on BOTH sides with a
         * different graphic each:
         *   D3D      0x48E983 — DrawTexturedQuad (0x450C38), UV (0x90, 0x90),
         *                       colour 0xFFE0E0E0, no blend
         *   software 0x48E5AE — Blit2DSprite   (0x44C3B4), UV (0x90, 0xC0),
         *                       flags 0x20 = ADDITIVE
         * We had the D3D one while declaring RENDER_SOFT. The software row sits
         * on opaque black in the same column, so it needs the additive blend to
         * drop the background — exactly how the charsel lock X is drawn
         * (0x48D7D5, also Blit2DSprite with flags 0x20). Blit2DSprite writes a
         * pipeline our backend never reads, so as in charsel the additive flag
         * becomes an explicit blend-mode change around DrawTexturedQuad —
         * see [[feedback_blit2d_d3d_path]]. */
        if (g_gpAllTracksFlag == 0) {
            R_PushState();
            R_SetBlendMode(R_BLEND_ADDITIVE);
            DrawTexturedQuad(s_trackPositionY[4] * 2, 0x168, 0x438C0000,
                             0x70, 0x60, g_uiTexPage + 1,
                             0x90, 0xC0, 0x38, 0x30, VERTEX_WHITE);
            R_PopState();
        }

        RenderWavingMenuBackground();                     /* 0x004C68B8 — software twin */
        EndFrame();
        FlipD3D();

        g_totalFrames2++;
        g_totalFrames++;
        WaitForFrameCap();
    }
}

/* Multiplayer Lobby */

/* Multiplayer player position layout tables (pairs of x_center, y_center) — from ROM.
 * Rendering computes screen x = (center_x - 64) * 2, y = (center_y - 20) * 2.
 *
 * 0x00501C10: 1 player (centered)
 * 0x00501C30: 2 players, split vertical (side by side)
 * 0x00501C50: 2 players, split horizontal (stacked)
 * 0x00501C70: 3-4 players (2×2 grid)
 */
static const int s_gpLayout1P[8]  = { 160, 120, 0, 0, 0, 0, 0, 0 };
static const int s_gpLayout2PV[8] = { 100, 120, 220, 120, 0, 0, 0, 0 };
static const int s_gpLayout2PH[8] = { 160, 84, 160, 156, 0, 0, 0, 0 };
static const int s_gpLayout4P[8]  = { 100, 84, 220, 84, 100, 156, 220, 156 };

/* Maximum local humans the lobby will seat. The binary's guard is
 * `cmp edx, 4 / jne` at 0x47734D — four seats, one per joystick port or
 * keyboard half.
 *
 * Four on every platform, deliberately, including Dreamcast. Input is
 * faithful to four end to end: seating here, g_perPlayerInput[0..3] in
 * UpdatePerPlayerInput, and the per-viewport loops in character select.
 * Presentation is too — the 3/4P race-end overlay, minimap and timer
 * positions were re-verified against the binary 2026-08-11 and are correct;
 * earlier notes claiming otherwise were wrong.
 *
 * DC ran 2P/3P/4P playable on 2026-08-13 once the grid gained a viewport
 * reject and the PVR USERCLIP erratum was worked around. Draw distance is
 * tuned per mode via SPLIT_QUALITY_* in track_init.c. */
#define MP_MAX_LOCAL_PLAYERS 4

/* Per-seat join jingle — ROM table at 0x4FECAC, copied to the stack at
 * 0x4772C5 and indexed by the joining player's number (0x47739E reads
 * 0x92528C, which MultiPlayerScreen holds equal to the playerCount argument
 * across the call). The port previously played sound 2 for every seat. */
static const int s_joinSfx[4] = { 0x1C, 0x1B, 0x23, 0x22 };

/**
 * IsJoystickAssigned — FUN_00477278 — 60 bytes
 * Checks if a device pointer is already assigned to any player slot.
 * EAX = device pointer to check.
 * Returns 1 if already assigned, 0 if free.
 */
static int IsJoystickAssigned(unsigned short *ptr)
{
    if (ptr == g_p1JoystickPtr) {
        return 1;
    }
    if (ptr == g_p2JoystickPtr) {
        return 1;
    }
    if (ptr == g_p3JoystickPtr) {
        return 1;
    }
    if (ptr == g_p4JoystickPtr) {
        return 1;
    }
    return 0;
}

/**
 * PollForPlayerJoin — FUN_004772b4 — 492 bytes
 * Polls input devices, checks for new players pressing confirm.
 * EAX = current player count. Returns updated player count.
 *
 * Original checks 4 joystick ports (DAT_00675898+i*2, mask 0x600)
 * then keyboard P1 (g_p1ButtonState & 0x600) and P2 (g_p2ButtonState & 0x600).
 */
static int PollForPlayerJoin(int playerCount)
{
    /* 0x4772C8: PollAllInputDevices, then 0x4772D3-0x477343 folds all seven
     * device words into g_combinedInputState — both keyboards and all five
     * joystick slots. MultiPlayerScreen's Back check reads g_inputBits off
     * this word (0x489283), so leaving the joystick slots out means a pad
     * can join the lobby but cannot back out of it. */
    PollAllInputDevices();
    g_combinedInputState = g_p1ButtonState | g_p2ButtonState
                         | g_joySlotState[0] | g_joySlotState[1]
                         | g_joySlotState[2] | g_joySlotState[3]
                         | g_joySlotState[4];

    if (playerCount >= MP_MAX_LOCAL_PLAYERS) return playerCount;

    /* 0x4772E8: Check joystick ports for join (original loop over DAT_00675898+i*2).
     * All four physical ports are still scanned, so any controller can take
     * a free seat; only the seat count is capped. */
    for (int i = 0; i < 4 && playerCount < MP_MAX_LOCAL_PLAYERS; i++) {
        if ((g_joySlotState[i] & 0x600) != 0) {
            if (!IsJoystickAssigned(&g_joySlotState[i])) {
                switch (playerCount) {
                    case 0:
                        g_p1JoystickPtr = &g_joySlotState[i];
                        break;
                    case 1:
                        g_p2JoystickPtr = &g_joySlotState[i];
                        break;
                    case 2:
                        g_p3JoystickPtr = &g_joySlotState[i];
                        break;
                    case 3:
                        g_p4JoystickPtr = &g_joySlotState[i];
                        break;
                }
                PlaySoundEffect(s_joinSfx[playerCount], 0, 0);   /* 0x4773B3 */
                g_playerDeviceType[playerCount] = 0x10000 + i;  /* joystick device */
                return playerCount + 1;
            }
        }
    }

    /* 0x477322: Check keyboard P1 confirm (bits 9-10 = 0x600).
     *
     * Live on DC as well as SDL. A maple keyboard reaches these words the same
     * way a PC one does: platform_pump_events reads it, hid_to_dik converts
     * HID usages to DIK scancodes, and PollAllInputDevices maps
     * g_diKeyboardState through the g_keyMap_P1 and g_keyMap_P2 entries —
     * every key in both default maps is covered by that table.
     *
     * This was #ifndef SONICR_DC on the grounds that "stale or default
     * button-state values claim a player slot the moment the joystick player
     * presses confirm". That was the g_p1ButtonState |= g_joySlotState[0]
     * fold-back in PollAllInputDevices, which is gone — the keyboard words are
     * keyboard-only now, as in the binary, so one pad can no longer seat two
     * players and the gate has nothing left to protect against.
     *
     * NOTE the confirm mask is 0x0600: P1 joins with Jump (Space), P2 with
     * Jump (O). Start — Enter and P — is 0x0800 and deliberately cannot join. */
    if ((g_p1ButtonState & 0x600) != 0) {
        if (!IsJoystickAssigned(&g_p1ButtonState)) {
            /* Assign keyboard P1 to player slot — 0x477332 */
            switch (playerCount) {
                case 0:
                    g_p1JoystickPtr = &g_p1ButtonState;
                    break;
                case 1:
                    g_p2JoystickPtr = &g_p1ButtonState;
                    break;
                case 2:
                    g_p3JoystickPtr = &g_p1ButtonState;
                    break;
                case 3:
                    g_p4JoystickPtr = &g_p1ButtonState;
                    break;
            }
            PlaySoundEffect(s_joinSfx[playerCount], 0, 0);   /* 0x477408 */
            g_playerDeviceType[playerCount] = 0;  /* 0 = keyboard P1 */
            return playerCount + 1;
        }
    }

    /* 0x477350: Check keyboard P2 confirm */
    if ((g_p2ButtonState & 0x600) != 0) {
        if (!IsJoystickAssigned(&g_p2ButtonState)) {
            switch (playerCount) {
                case 0:
                    g_p1JoystickPtr = &g_p2ButtonState;
                    break;
                case 1:
                    g_p2JoystickPtr = &g_p2ButtonState;
                    break;
                case 2:
                    g_p3JoystickPtr = &g_p2ButtonState;
                    break;
                case 3:
                    g_p4JoystickPtr = &g_p2ButtonState;
                    break;
            }
            PlaySoundEffect(s_joinSfx[playerCount], 0, 0);   /* 0x47746E */
            g_playerDeviceType[playerCount] = 1;  /* 1 = keyboard P2 */
            return playerCount + 1;
        }
    }

    return playerCount;
}

/**
 * DrawMPDeviceIcon — FUN_00488FAC — 173 bytes
 * Renders the device type icon for a confirmed player slot.
 * EAX = screen x, EDX = screen y, EBX = device type.
 *
 * Maps device type to UV Y offset in the MP00.RAW texture:
 *   0 (KB P1) → 0x40    1 (KB P2) → 0x52
 *   0x10000 (Joy0) → 0x64   0x10001 → 0x76
 *   0x10002 → 0x88          0x10003 → 0x9A
 */
static void DrawMPDeviceIcon(int x, int y, int deviceType)
{
    int uvY = 0;

    /* 0x488FAE-0x488FF4: device type → UV Y lookup */
    if ((unsigned int)deviceType < 0x10000) {
        if (deviceType == 0) {
            uvY = 0x40;
        }
        else if (deviceType == 1) {
            uvY = 0x52;
        }
    }
    else if ((unsigned int)deviceType == 0x10000) {
        uvY = 0x64;
    }
    else if ((unsigned int)deviceType == 0x10001) {
        uvY = 0x76;
    }
    else if ((unsigned int)deviceType == 0x10002) {
        uvY = 0x88;
    }
    else if ((unsigned int)deviceType == 0x10003) {
        uvY = 0x9A;
    }

    /* 0x489032-0x489058: D3D path DrawTexturedQuad
     * x, y from caller; z=0x43FA0000, w=0x100, h=0x24,
     * tpage=g_uiTexPage+1, uvX=0x80, uvY=computed, uvW=0x80, uvH=0x12 */
    DrawTexturedQuad(x, y, 0x43FA0000, 0x100, 0x24,
                     g_uiTexPage + 1, 0x80, uvY, 0x80, 0x12, VERTEX_WHITE);
}

/**
 * MultiPlayerScreen — 0x0048905C — 1752 bytes
 * Multiplayer lobby with countdown timer. Players join by pressing confirm.
 * Returns: player count (2-4) on success, 0 on back, or -1 on error.
 *
 * NOTE: g_modelRotation (0x92528C) is reused as the confirmed player count.
 */
int MultiPlayerScreen(void)
{
    DebugLog("MultiPlayerScreen\n");

    /* Orange tint — EBX=0xFF(R), ECX=0x7F(G), ESI=0x3F(B) at 0x48906C */
    g_bgTintR = 0xFF;
    g_bgTintG = 0x7F;
    g_bgTintB = 0x3F;
    g_menuState = 5;

    /* Load textures (D3D path: 0x4890E2-0x48912B)
     * g_uiTexPage ← GENERAL/SONICR.RAW (tinted orange)
     * g_uiTexPage+1 ← BIN/OPTION/MP00.RAW (multiplayer menu sprites) */
#ifdef SONICR_DC
    if (RunningFromSlowMedia()) PauseCD();   /* slow media: hold music so the load can't starve the stream */
#endif
    SetupMenuTexturesD3D();
    LoadTPageRGB(g_uiTexPage, PATH_GENERAL_RAW);
    TintBackgroundTPage(g_bgTintR, g_bgTintG, g_bgTintB);
    LoadTPageRGB(g_uiTexPage + 1, PATH_MENU_MULTIPLAYER);
    ProcessTpageStates();
    FinalizeMenuTexturesD3D();
#ifdef SONICR_DC
    ResumeCD();
#endif

    /* Init state — 0x48912B-0x48916B */
    g_fadeState = FADE_IN;
    g_totalFrames = 0;
    g_modelRotation = 0;        /* reused as player count */
    g_screenResult = SCREEN_OK;
    g_p1JoystickPtr = NULL;
    g_p2JoystickPtr = NULL;
    g_p3JoystickPtr = NULL;
    g_p4JoystickPtr = NULL;
    g_renderEnabled = 1;

    /* Timing — 0x489170-0x48917B */
    DWORD startTime = timeGetTime();
    unsigned int startSec = startTime / 1000;
    unsigned int prevSec = 0;
    int mpElapsed = 0;

    while (1) {
        /* 0x48917E-0x489197 */
        platform_pump_events();
        g_currentTime = timeGetTime();

        /* Compute elapsed seconds — 0x489197-0x4891C8 */ 
        DWORD now = timeGetTime();
        unsigned int currentSec = now / 1000;
        unsigned int prevSecSaved = prevSec;
        prevSec = currentSec;

        int rawElapsed = (int)(currentSec - startSec);
        int sign = rawElapsed >> 31;
        mpElapsed = (rawElapsed ^ sign) - sign; /* abs() */
        if (mpElapsed > 12) {
            mpElapsed = 12;
        }

        /* Countdown beep — plays once per second change (0x4891D2-0x4891F3) */
        if (mpElapsed < 12 && g_fadeState == FADE_VISIBLE &&
            prevSecSaved != currentSec)
        {
            PlaySoundEffect(1, 0, 0);
        }
        
        /* 0x4891F8-0x48920C */
        int cd = GetLogicalCDTrack();
        if (cd != 5) {
            UpdateCDPlayback(5);
        }
        if (g_fadeState != FADE_VISIBLE) {
            UpdateFade();
        }
        if (g_fadeLevel == -256) {
#ifdef SONICR_DC
            /* Stop music before exiting — track loading hits the
             * filesystem hard and concurrent music streaming over
             * the same fileserver causes audible glitches on DC. */
            StopCD();
#endif
            return g_screenResult;
        }

        /* Poll for new players joining — 0x489231-0x48923B
         * PollForPlayerJoin calls PollAllInputDevices internally.
         * We must pump SDL events first so g_diKeyboardState is fresh. */
#ifdef SONICR_SDL
        platform_poll_events(g_diKeyboardState, 256);
#endif
        g_modelRotation = PollForPlayerJoin(g_modelRotation);

        /* Compute g_inputBits from combined state for back button check */
        g_inputBits = (unsigned char)(g_combinedInputState >> 8);

        if (g_fadeState == FADE_VISIBLE) {
            /* Countdown reached 12 → finish — 0x48924A-0x48927D */
            if (mpElapsed == 12) {
                if (g_modelRotation > 1) {
                    /* Enough players — success SFX */
                    PlaySoundEffect(2, 0, 0);
                }
                else {
                    /* Not enough players — cancel SFX */
                    PlaySoundEffect(0, 0, 0);
                }
                g_screenResult = g_modelRotation;
                g_fadeState = FADE_OUT;
            }

            /* Back button — 0x489283-0x4892A6
             * test byte [0x9020D9], 1 = g_inputBits & 1 */
            if ((g_inputBits & 1) != 0) {
                PlaySoundEffect(0, 0, 0);
                g_screenResult = 0;
                g_fadeState = FADE_OUT;
            }
        }

        /* 0x4892AC-0x4892BA */

        /* Rendering (D3D path: 0x4894D6-0x4896D9) */
        ProcessTpageStates();
        BeginFrame();
        D3DAppClearBackBuffer();
        EndFrame();
        if (g_fadeLevel < 0) {
            RenderFadeOverlay();
        }

        BeginFrame();

        /* Banner: "GRAND PRIX" (2 halves) — 0x489516-0x489572
         * Same UV layout as CourseSelectScreen banner. */
        DrawTexturedQuad(0, 0x20, 0x447A0000, 0x140, 0x40, g_uiTexPage + 1,
                         0, 0, 0xA0, 0x20, VERTEX_WHITE);
        DrawTexturedQuad(0x140, 0x20, 0x447A0000, 0x140, 0x40, g_uiTexPage + 1,
                         0, 0x20, 0xA0, 0x20, VERTEX_WHITE);

        /* Player slots — 0x489577-0x48962F
         * Select layout table based on player count and split screen mode.
         * Each entry is (x_center, y_center). */
        if (g_modelRotation > 0) {
            const int *layout;
            int playerCount = g_modelRotation;

            /* Layout table selection — 0x489577-0x4895B2 */
            if (playerCount == 1) {
                layout = s_gpLayout1P;
            }
            else if (playerCount == 2) {
                if (g_splitScreenMode == 1) {
                    layout = s_gpLayout2PV;
                }
                else {
                    layout = s_gpLayout2PH;
                }
            }
            else {
                layout = s_gpLayout4P;
            }

            /* Render each player slot — 0x4895C1-0x48962F */
            
            int uvY = 0x88;     /* UV row starts at 0x88, +0x12 per slot */
            for (int i = 0; i < playerCount; i++) {
                int cx = layout[i * 2];
                int cy = layout[i * 2 + 1];

                /* Slot background quad — 0x4895C8-0x4895FC
                 * x = (cx - 0x40) * 2, y = (cy - 0x14) * 2 */
                int slotX = (cx - 0x40) * 2;
                int slotY = (cy - 0x14) * 2;
                DrawTexturedQuad(slotX, slotY, 0x43FA0000, 0x100, 0x24,
                                 g_uiTexPage + 1, 0, uvY, 0x80, 0x12,
                                 VERTEX_WHITE);

                /* Device icon — FUN_00488FAC at 0x489618
                 * x = (cx - 0x40) * 2, y = (cy + 1) * 2 */
                int iconX = (cx - 0x40) * 2;
                int iconY = (cy + 1) * 2;
                DrawMPDeviceIcon(iconX, iconY,
                                 g_playerDeviceType[i]);

                uvY += 0x12;
            }
        }

        /* Countdown timer (2 digits) — 0x489631-0x4896CA
         * remaining = 11 - mpElapsed, clamped [0..10]
         * Digits from g_tpageParallax1 at uvY=0x88, each 8px wide, uvX = digit*8 + 0x56 */
        int remaining = 11 - mpElapsed;
        if (remaining < 0) {
            remaining = 0;
        }
        if (remaining > 10) {
            remaining = 10;
        }

        int tens = remaining / 10;
        int ones = remaining % 10;

        /* Tens digit — x=0x120, y=0x180 at 0x48964C-0x489689 */
        DrawTexturedQuad(0x120, 0x180, 0x43FA0000, 0x20, 0x40,
                         g_tpageParallax1, tens * 8 + 0x56, 0x88,
                         8, 0x10, VERTEX_WHITE);

        /* Ones digit — x=0x140, y=0x180 at 0x48968E-0x4896CA */
        DrawTexturedQuad(0x140, 0x180, 0x43FA0000, 0x20, 0x40,
                         g_tpageParallax1, ones * 8 + 0x56, 0x88,
                         8, 0x10, VERTEX_WHITE);

        RenderWavingMenuBackground();                     /* 0x004C68B8 — software twin */
        EndFrame();
        FlipD3D();

        /* Frame count + frame cap — 0x4896DE-0x489723 */
        g_totalFrames2++;
        g_totalFrames++;
        WaitForFrameCap();
    }
}
