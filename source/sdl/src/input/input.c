/**
 * input.c - Input polling, processing, ghost recording/playback, config I/O
 *
 * ReadInput, PollAllInputDevices, ReadLocalInput - device polling
 * UpdatePerPlayerInput - per-frame input poll + ghost record/playback
 * LoadKeyMappings, SaveKeyMappings, LoadJoystickConfig, SyncJoystickSlots - config
 * ScanTypedLetter, ScanInputForAction - button scanning
 * ResetInputState - screen transition reset
 */

#include <string.h>
#include "gamepad_buttons.h"
#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "platform.h"
#include "endian_util.h"
#include <stdio.h>
int NetRaceFrozen(void);   /* net/network.c */

/* Externs */

extern void StopAmbientSounds(void);

extern char g_joystickDeviceNames[][260]; /* 0x00675C54 - stride 0x104 */
extern short g_joystickDeviceFlags[];     /* 0x00675C40 */

/* DirectInput objects - see dinput.h for vtable layouts */
extern void *g_lpDirectInput;       /* DAT_006758A4 - IDirectInputA* */
extern void *g_lpDIKeyboardDev;     /* DAT_006758A8 - IDirectInputDeviceA* (keyboard) */

/* Ghost replay/save arrays - fixed .bss addresses in binary */

/* Joystick raw state pointers (NULL if joystick not connected) */

/* Pause menu */
int RunPauseMenu(void);                      /* 0x004CDD24 */

/* File I/O */

/* Joystick config */
extern char  g_joystickSlots[][282];     /* 0x0067541A - stride 0x11A */

/* ResetInputState externs */
extern void SetViewportFromConfig(int *config);
extern void SetCameraStructPtr(int *cam);
extern void InitFarClipAndFog(int farClip);
extern void SetViewportClipRect(int *cam);

/* Input mapping aliases */
extern int g_optSubPageCounter;          /* 0x0068B018 */
extern int g_optSubPageState;            /* 0x0068B014 */

/* Defines */

/* Input mask during intro countdown: strip most buttons */
#define INTRO_INPUT_MASK    0x3957

/* Ghost record mask: strip jump bit (0x0800) */
#define GHOST_RECORD_MASK   0xF7FF

#define g_joystickConfig ((char *)g_joystickSlots)

/* g_inputMappingCount is same memory as g_optSubPageCounter (0x0068B018) */
#define g_inputMappingCount g_optSubPageCounter
/* g_inputMappingMode is same memory as g_optSubPageState (0x0068B014) */
#define g_inputMappingMode g_optSubPageState

/* Per-viewport camera state at 0x006E9924 - 9 ints zeroed by ResetInputState.
 * Same memory as g_viewportConfigArray (0x006E9924, globals_extra.c) - first 9 entries. */
#define s_camState9924 g_viewportConfigArray

/* Static variables */

static void *g_fileHandle2;             /* 0x00625C00 - transient file handle (keys.bin write) */
static char g_joyShiftFlag1;            /* 0x006758F6 - shift/trigger 1 */
static char g_joyShiftFlag2;            /* 0x00675902 - shift/trigger 2 */

/* ROM data */

/* ROM table at 0x4FECBC - 83 entries + sentinel, 12-byte stride.
 * Used by ScanInputForAction to validate which DIK scancodes are
 * eligible to be bound during the keyboard remap UI, and to look
 * up each key's glyph position (col2/col3 = X/Y in the controls
 * tpage). Notable exclusions: ESC (DIK 0x01) and F1-F12 cannot be
 * remapped - the binary skips them on purpose. */
const int s_remapValidKeys[][3] = {
    { 0x02,    0,  132 }, { 0x03,   32,  132 }, { 0x04,   64,  132 },
    { 0x05,   96,  132 }, { 0x06,  128,  132 }, { 0x07,  160,  132 },
    { 0x08,  192,  132 }, { 0x09,  224,  132 }, { 0x0A,    0,  144 },
    { 0x0B,   32,  144 }, { 0x0C,   64,  144 }, { 0x0D,   96,  144 },
    { 0x0E,  128,  144 }, { 0x0F,  160,  144 }, { 0x10,  192,  144 },
    { 0x11,  224,  144 }, { 0x12,    0,  156 }, { 0x13,   32,  156 },
    { 0x14,   64,  156 }, { 0x15,   96,  156 }, { 0x16,  128,  156 },
    { 0x17,  160,  156 }, { 0x18,  192,  156 }, { 0x19,  224,  156 },
    { 0x1A,    0,  168 }, { 0x1B,   32,  168 }, { 0x1C,   64,  168 },
    { 0x1E,   96,  168 }, { 0x1F,  128,  168 }, { 0x20,  160,  168 },
    { 0x21,  192,  168 }, { 0x22,  224,  168 }, { 0x23,    0,  180 },
    { 0x24,   32,  180 }, { 0x25,   64,  180 }, { 0x26,   96,  180 },
    { 0x27,  128,  180 }, { 0x28,  160,  180 }, { 0x29,  192,  180 },
    { 0x2A,  224,  180 }, { 0x2B,    0,  192 }, { 0x2C,   32,  192 },
    { 0x2D,   64,  192 }, { 0x2E,   96,  192 }, { 0x2F,  128,  192 },
    { 0x30,  160,  192 }, { 0x31,  192,  192 }, { 0x32,  224,  192 },
    { 0x33,    0,  204 }, { 0x34,   32,  204 }, { 0x35,   64,  204 },
    { 0x36,   96,  204 }, { 0x1D,  128,  204 }, { 0x38,  160,  204 },
    { 0x39,  192,  204 }, { 0xB8,  224,  204 }, { 0x9D,    0,  216 },
    { 0xD2,   32,  216 }, { 0xD3,   64,  216 }, { 0xC7,   96,  216 },
    { 0xCF,  128,  216 }, { 0xC9,  160,  216 }, { 0xD1,  192,  216 },
    { 0xC8,  224,  216 }, { 0xD0,    0,  228 }, { 0xCB,   32,  228 },
    { 0xCD,   64,  228 }, { 0xB5,   96,  228 }, { 0x37,  128,  228 },
    { 0x4A,  160,  228 }, { 0x4E,  192,  228 }, { 0x47,  224,  228 },
    { 0x48,    0,  240 }, { 0x49,   32,  240 }, { 0x4B,   64,  240 },
    { 0x4C,   96,  240 }, { 0x4D,  128,  240 }, { 0x4F,  160,  240 },
    { 0x50,  192,  240 }, { 0x51,  224,  240 }, { 0x52,  160,  120 },
    { 0x53,  192,  120 }, { 0x9C,  224,  120 },
    {   -1,    0,    0 },  /* sentinel */
};

/* ROM joystick button map at 0x501CA0 - 46 entries + sentinel, 16-byte stride */
const int s_joyMapRom[][4] = {
    {  30,    0,  224,    1}, {  48,    8,  224,    1},
    {  46,   16,  224,    1}, {  32,   24,  224,    1},
    {  18,   32,  224,    1}, {  33,   40,  224,    1},
    {  34,   48,  224,    1}, {  35,   56,  224,    1},
    {  23,   64,  224,    1}, {  36,   72,  224,    1},
    {  37,   80,  224,    1}, {  38,   88,  224,    1},
    {  50,   96,  224,    1}, {  49,  104,  224,    1},
    {  24,  112,  224,    1}, {  25,  120,  224,    1},
    {  16,  128,  224,    1}, {  19,  136,  224,    1},
    {  31,  144,  224,    1}, {  20,  152,  224,    1},
    {  22,  160,  224,    1}, {  47,  168,  224,    1},
    {  17,  176,  224,    1}, {  45,  184,  224,    1},
    {  21,  192,  224,    1}, {  44,  200,  224,    1},
    {  11,    0,  200,    1}, {   2,    8,  200,    1},
    {   3,   16,  200,    1}, {   4,   24,  200,    1},
    {   5,   32,  200,    1}, {   6,   40,  200,    1},
    {   7,   48,  200,    1}, {   8,   56,  200,    0},
    {   9,   64,  200,    1}, {  10,   72,  200,    1},
    {  57,   80,  200,    0}, {  51,   88,  200,    1},
    {  52,   96,  200,    1}, {  40,  104,  200,    0},
    {  12,  112,  200,    1}, {  13,  120,  200,    1},
    {  53,  128,  200,    1}, {  26,  136,  200,    1},
    {  27,  144,  200,    1}, {  39,  160,  200,    1},
    {  -1,    0,  218,    6}, /* sentinel */
};

/* Static functions */

/**
 * MirrorInputLR - swap left/right D-pad bits for mirror mode
 */
static unsigned short MirrorInputLR(unsigned short input)
{
    unsigned short lr = (input >> 8) & 0xC0;
    if (lr == 0x40 || lr == 0x80) {
        input ^= 0xC000;   /* swap D-pad left/right */
    }
    if ((input & 0x88) == 0x08 || (input & 0x88) == 0x80) {
        input ^= 0x88;     /* swap B/A? */
    }
    return input;
}

/* =====================================================================
 * Polling
 * ===================================================================== */

/**
 * ReadInput - 0x00477228 - 77 bytes
 * Polls input devices and builds g_combinedInputState.
 *
 * In the original binary, g_inputBits at 0x009020D9 is the HIGH BYTE
 * of g_combinedInputState at 0x009020D8 (they overlap in memory).
 * Writing g_combinedInputState automatically updates g_inputBits.
 * In our build they are separate variables, so we manually sync.
 */
void ReadInput(void)
{
    platform_poll_events(g_diKeyboardState, 256);

    PollAllInputDevices();
    g_combinedInputState =
        g_p1ButtonState | g_p2ButtonState
        | g_joySlotState[0] | g_joySlotState[1] | g_joySlotState[2]
        | g_joySlotState[3] | g_joySlotState[4];

    /* g_inputBits overlaps the high byte of g_combinedInputState
     * in the original binary (0x009020D9 = high byte of 0x009020D8).
     * Sync manually since our globals don't overlap. */
    g_inputBits = (unsigned char)(g_combinedInputState >> 8);
}

/**
 * PollAllInputDevices - 0x004769B0
 * Reads keyboard via DirectInput and joysticks.
 * Builds g_p1ButtonState and g_p2ButtonState from the keyboard
 * state using the key mapping table.
 */
void PollAllInputDevices(void)
{
    /* Clear button states */
    g_p1ButtonState = 0;
    g_p2ButtonState = 0;

    if (g_diDeviceReady == 0) {
        return;
    }

    /* On SDL, g_diKeyboardState is already filled by platform_poll_events
     * in ReadInput. Just map the keys below. */

    /* Map keyboard state to the 16-bit player input word.
     * Bit assignments (verified against PollAllInputDevices @ 0x004769B0):
     *   0x8000 Right    0x4000 Left      0x2000 Down     0x1000 Up
     *   0x0800 Start    0x0400 Jump      0x0100 Accel
     *   0x0080 DriftR   0x0040 LookBack  0x0008 DriftL
     * (bits 0x0200, 0x0020, 0x0010, 0x0004, 0x0002, 0x0001 come only from
     * joystick / gamepad, not the keyboard.) */
    if (g_diKeyboardState[g_keyMap_P1_Left] != 0)
        g_p1ButtonState |= 0x4000;
    if (g_diKeyboardState[g_keyMap_P1_Right] != 0)
        g_p1ButtonState |= 0x8000;
    if (g_diKeyboardState[g_keyMap_P1_Up] != 0)
        g_p1ButtonState |= 0x1000;
    if (g_diKeyboardState[g_keyMap_P1_Down] != 0)
        g_p1ButtonState |= 0x2000;
    if (g_diKeyboardState[g_keyMap_P1_Start] != 0)
        g_p1ButtonState |= 0x0800;
    if (g_diKeyboardState[g_keyMap_P1_Jump] != 0)
        g_p1ButtonState |= 0x0400;
    if (g_diKeyboardState[g_keyMap_P1_Accel] != 0)
        g_p1ButtonState |= 0x0100;
    if (g_diKeyboardState[g_keyMap_P1_DriftL] != 0)
        g_p1ButtonState |= 0x0008;
    if (g_diKeyboardState[g_keyMap_P1_DriftR] != 0)
        g_p1ButtonState |= 0x0080;
    if (g_diKeyboardState[g_keyMap_P1_LookBack] != 0)
        g_p1ButtonState |= 0x0040;

    /* Map keyboard state to player 2 buttons */
    if (g_diKeyboardState[g_keyMap_P2_Left] != 0)
        g_p2ButtonState |= 0x4000;
    if (g_diKeyboardState[g_keyMap_P2_Right] != 0)
        g_p2ButtonState |= 0x8000;
    if (g_diKeyboardState[g_keyMap_P2_Up] != 0)
        g_p2ButtonState |= 0x1000;
    if (g_diKeyboardState[g_keyMap_P2_Down] != 0)
        g_p2ButtonState |= 0x2000;
    if (g_diKeyboardState[g_keyMap_P2_Start] != 0)
        g_p2ButtonState |= 0x0800;
    if (g_diKeyboardState[g_keyMap_P2_Jump] != 0)
        g_p2ButtonState |= 0x0400;
    if (g_diKeyboardState[g_keyMap_P2_Accel] != 0)
        g_p2ButtonState |= 0x0100;
    if (g_diKeyboardState[g_keyMap_P2_DriftL] != 0)
        g_p2ButtonState |= 0x0008;
    if (g_diKeyboardState[g_keyMap_P2_DriftR] != 0)
        g_p2ButtonState |= 0x0080;
    if (g_diKeyboardState[g_keyMap_P2_LookBack] != 0)
        g_p2ButtonState |= 0x0040;

    /* Poll gamepads into g_joySlotState[] */
    g_initFeatureC = platform_poll_gamepads(g_joySlotState, 5);

    /* 0x675894/6 are keyboard-only in the binary — nothing folds pad state
     * into them. The port used to OR slots 0 and 1 back in so character
     * select, which read those two words directly, would see gamepads. That
     * made one pad look like two devices to PollForPlayerJoin: it would seat
     * the pad, then seat the keyboard-P1 word on the next frame off the same
     * physical press. Character select now reads g_perPlayerInput[], so the
     * OR-back is gone and every consumer sees the same words the binary
     * builds: ReadInput and UpdatePerPlayerInput combine all seven. */
}

/**
 * ReadLocalInput - FUN_004774a0 - 172 bytes - VALIDATED
 *
 * Polls all input devices, ORs all 7 button state words (P1, P2, 5 joystick
 * slots) into a single combined value. If mirror mode is active, swaps
 * left/right (bits 14-15) and swaps L-trigger/ability (bits 3 and 7).
 * If g_introCountdown > 60, clears all input. If g_introCountdown > 0,
 * masks to only navigation buttons (0x3957). Always forces bits 1+2 on.
 *
 * EAX = combined input state (Watcom fastcall return).
 */
int ReadLocalInput(void)
{
    PollAllInputDevices();

    /* OR all input sources together */
    unsigned int combined = (unsigned int)g_p1ButtonState
                          | (unsigned int)g_p2ButtonState
                          | (unsigned int)g_joySlotState[0]
                          | (unsigned int)g_joySlotState[1]
                          | (unsigned int)g_joySlotState[2]
                          | (unsigned int)g_joySlotState[3]
                          | (unsigned int)g_joySlotState[4];

    /* Mirror mode: swap left/right and L-trigger/ability */
    if (g_mirrorMode != 0) {
        /* Check if exactly one of left (0x4000) or right (0x8000) is pressed */
        unsigned int lr = combined & 0xC000;
        if (lr == 0x4000 || lr == 0x8000) {
            combined ^= 0xC000;                          /* swap left/right */
        }
        /* Check if exactly one of bit 3 (0x08) or bit 7 (0x80) is pressed */
        unsigned int ab = combined & 0x0088;
        if (ab == 0x0008 || ab == 0x0080) {
            combined ^= 0x0088;                          /* swap bits 3 and 7 */
        }
    }

    /* Timer gate: suppress input during countdown/transition */
    if (g_introCountdown > 0x3C) {
        combined = 0;                                    /* block all input */
    }
    else if (g_introCountdown > 0) {
        combined &= 0x3957;                              /* navigation only */
    }

    combined |= 0x06;                                    /* always set bits 1+2 */

    return (int)combined;
}

/* =====================================================================
 * Input processing + ghost record/playback
 * ===================================================================== */

/* Start = pause, and the pause menu. Split out of UpdatePerPlayerInput so the
 * online race loop (which does not run UpdatePerPlayerInput: inputs come from
 * the network there) can pause too. Online, "Restart" would restart the race
 * on this machine only, so it just resumes; "Quit" leaves the session right
 * away (the others are told and keep racing). */
extern void CloseDirectPlaySession(void);
void HandlePauseInput(void)
{
    int online = (g_netSessionActive != 0 || g_isNetworkGame != 0);
    /* Pause handling */
    if ((g_combinedInputState & 0x800) != 0 && g_isPaused == 0 &&
        g_pauseLatch == 0 && g_fadeState == FADE_VISIBLE &&
        g_introCountdown == 0 && g_demoMode == DEMO_NONE && g_postRaceCameraMode == 0 &&
        !NetRaceFrozen())   /* someone else paused us, or the resume countdown */
    {
        PlaySoundEffect(0, 0, 0);
        StopAmbientSounds();
        g_isPaused = 1;
        g_pauseLatch = 1;
    }

    if ((g_combinedInputState & 0x800) == 0 && g_isPaused == 0 && g_pauseLatch == 1) {
        g_pauseLatch = 0;
    }

    if (g_isPaused != 0) {
        int pauseResult = RunPauseMenu();
        if (pauseResult == 1) {
            g_isPaused = 0;
            g_pauseLatch = 1;
        }
        else if (pauseResult == 2 && online) {               /* restart: not possible online */
            g_isPaused = 0;
            g_pauseLatch = 1;
        }
        else if (pauseResult == 2) {                         /* 0x477674: options/restart (selection=1) */
            g_fadeState = FADE_OUT;                          /* 0x477683: EAX=2 */
            g_fadeSpeed = 0x10;                              /* 0x477688 */
            g_exitRaceFlag = 1;                              /* 0x47768e */
            g_raceResult = RACE_RESULT_RESTART;               /* binary: aliased via 0x901C10 = selection */
        }
        else if (pauseResult == 3) {                         /* 0x477696: quit (selection=2) */
            g_fadeSpeed = 0x10;                              /* 0x4776aa */
            g_exitRaceFlag = 1;                              /* 0x4776b0 */
            g_fadeState = FADE_OUT;                          /* 0x4776b6: EDX=2 */
            g_raceResult = RACE_RESULT_QUIT;                 /* binary: aliased via 0x901C10 = selection */
            if (online) {
                g_isPaused = 0;                              /* our pause ends for the others */
                CloseDirectPlaySession();                    /* LEAVE / HOST_LEFT now */
#ifdef SONICR_MATCHMAKER
                {   /* host: take the session off the online list */
                    extern void MatchmakerClearSession(void);
                    MatchmakerClearSession();
                }
#endif
            }
        }
    }

}

/**
 * UpdatePerPlayerInput - 0x0047754C - 1750 bytes
 *
 * Per-frame input processing + ghost record/playback.
 * Called once per frame from the main game loop.
 */
void UpdatePerPlayerInput(void)
{
    /* Clear all input states */
    g_perPlayerInput[0] = 0;
    g_perPlayerInput[1] = 0;
    g_perPlayerInput[2] = 0;
    g_perPlayerInput[3] = 0;

    /* Poll all input devices (keyboard + joysticks) */
    PollAllInputDevices();

    /* Combine every device word — 0x47757A-0x4775B9 folds the two keyboard
     * words and all five joystick slots into ESI. This is the value the
     * one-viewport case assigns to g_perPlayerInput[0] (0x4776E6) and the
     * value the demo path masks for the Start button (0x477C07), so the
     * joystick slots have to be in it. */
    unsigned short combined = g_p1ButtonState | g_p2ButtonState
                            | g_joySlotState[0] | g_joySlotState[1]
                            | g_joySlotState[2] | g_joySlotState[3]
                            | g_joySlotState[4];

    HandlePauseInput();

    /* Input assignment (normal gameplay) */
    if (g_demoMode == DEMO_NONE) {
        if (g_introCountdown < 0x3D) {
            /* 0x4776DD: a single viewport takes the combined all-device word.
             * With more than one, only the seat pointer drives the slot, and
             * an unseated slot keeps the zero written at the top of the
             * function — 0x4776F7 skips the store when the pointer is null. */
            if (g_numViewports == 1) {
                g_perPlayerInput[0] = combined;                  /* 0x4776E6 */
            }
            else if (g_p1JoystickPtr != NULL) {
                g_perPlayerInput[0] = *g_p1JoystickPtr;          /* 0x4776F9 */
            }

            /* Mirror mode: swap left/right */
            if (g_mirrorMode != 0) {
                g_perPlayerInput[0] = MirrorInputLR(g_perPlayerInput[0]);
            }

            /* Grand Prix: read additional joysticks */
            if (g_raceType == RACE_MULTIPLAYER) {
                if (g_p2JoystickPtr != NULL) {
                    g_perPlayerInput[1] = *g_p2JoystickPtr;
                }
                if (g_mirrorMode != 0) {
                    g_perPlayerInput[1] = MirrorInputLR(g_perPlayerInput[1]);
                }

                if (g_numViewports > 2 && g_p3JoystickPtr != NULL) {
                    g_perPlayerInput[2] = *g_p3JoystickPtr;
                    if (g_mirrorMode != 0) {
                        g_perPlayerInput[2] = MirrorInputLR(g_perPlayerInput[2]);
                    }
                }
                if (g_numViewports > 3 && g_p4JoystickPtr != NULL) {
                    g_perPlayerInput[3] = *g_p4JoystickPtr;
                    if (g_mirrorMode != 0) {
                        g_perPlayerInput[3] = MirrorInputLR(g_perPlayerInput[3]);
                    }
                }
            }
            /* Time attack ghost playback for P2 — six conditions in binary
             * order, 0x4778A9-0x4778DE:
             *   0x4778A9  not paused
             *   0x4778B2  g_ghostToggle == 1        (was missing here)
             *   0x4778BB  g_raceType == 2
             *   0x4778C0  g_raceType > g_raceSubMode — plain TA, not Tag/Balloon
             *   0x4778C8  a ghost is loaded
             *   0x4778DC  the ghost has frames left */
            else if (g_isPaused == 0 && g_ghostToggle == 1 &&
                     g_raceType == RACE_TIMEATTACK &&
                     g_raceType > g_raceSubMode && g_ghostDataExists != 0 &&
                     g_ghostReadIndex < g_ghostTotalFrames)
            {
                g_perPlayerInput[1] = g_taGhostBuffer[g_ghostReadIndex];
                g_ghostReadIndex++;
            }
        }

        /* Mask inputs during intro countdown */
        if (g_introCountdown > 0) {
            g_perPlayerInput[0] &= INTRO_INPUT_MASK;
            g_perPlayerInput[1] &= INTRO_INPUT_MASK;
            g_perPlayerInput[2] &= INTRO_INPUT_MASK;
            g_perPlayerInput[3] &= INTRO_INPUT_MASK;
        }

        /* Ghost recording */
        if (g_isPaused == 0 && g_introCountdown < 0x3D &&
            g_ghostWriteIndex < g_ghostMaxFrames && g_postRaceCameraMode == 0)
        {
            g_taGhostSource[g_ghostWriteIndex] = g_perPlayerInput[0] & GHOST_RECORD_MASK;

            if (g_raceType == RACE_MULTIPLAYER) {
                g_taGhostSource[g_ghostMaxFrames + g_ghostWriteIndex] =
                    g_perPlayerInput[1] & GHOST_RECORD_MASK;
                if (g_numViewports > 2) {
                    g_taGhostSource[g_ghostMaxFrames * 2 + g_ghostWriteIndex] =
                        g_perPlayerInput[2] & GHOST_RECORD_MASK;
                }
                if (g_numViewports > 3) {
                    g_taGhostSource[g_ghostMaxFrames * 3 + g_ghostWriteIndex] =
                        g_perPlayerInput[3] & GHOST_RECORD_MASK;
                }
            }
            g_ghostWriteIndex++;
        }

        /* Combined input state for UI */
        g_combinedInputState = g_perPlayerInput[0];
        if (g_raceType != 2) {
            g_combinedInputState = g_perPlayerInput[0] | g_perPlayerInput[1] |
                                   g_perPlayerInput[2] | g_perPlayerInput[3];
        }
    }
    /* Ghost playback mode (g_demoMode != 0) */
    else {
        if (g_introCountdown < 0x3D && g_ghostReadIndex < g_ghostMaxFrames &&
            g_postRaceCameraMode == 0)
        {
            /* Read P1 from ghost, OR with live joystick start button */
            g_perPlayerInput[0] = 0;
            if (g_p1JoystickPtr != NULL) {
                g_perPlayerInput[0] = (*g_p1JoystickPtr >> 8 & 8) << 8;
            }
            g_perPlayerInput[0] |= g_taGhostSource[g_ghostReadIndex];
            g_combinedInputState = g_perPlayerInput[0];

            if (g_raceType == RACE_MULTIPLAYER) {
                g_perPlayerInput[1] = 0;
                if (g_p2JoystickPtr != NULL) {
                    g_perPlayerInput[1] = (*g_p2JoystickPtr >> 8 & 8) << 8;
                }
                g_perPlayerInput[1] |= g_taGhostSource[g_ghostMaxFrames + g_ghostReadIndex];
                g_combinedInputState |= g_perPlayerInput[1];

                if (g_numViewports > 2) {
                    g_perPlayerInput[2] = 0;
                    if (g_p3JoystickPtr != NULL) {
                        g_perPlayerInput[2] = (*g_p3JoystickPtr >> 8 & 8) << 8;
                    }
                    g_perPlayerInput[2] |= g_taGhostSource[g_ghostMaxFrames * 2 + g_ghostReadIndex];
                    g_combinedInputState |= g_perPlayerInput[2];

                    if (g_numViewports > 3) {
                        g_perPlayerInput[3] = 0;
                        if (g_p4JoystickPtr != NULL) {
                            g_perPlayerInput[3] = (*g_p4JoystickPtr >> 8 & 8) << 8;
                        }
                        g_perPlayerInput[3] |= g_taGhostSource[g_ghostMaxFrames * 3 + g_ghostReadIndex];
                        g_combinedInputState |= g_perPlayerInput[3];
                    }
                }
            }
            g_ghostReadIndex++;
        }
        g_combinedInputState |= combined & 0x800;  /* preserve start button */
    }
}

/* =====================================================================
 * Config I/O
 * ===================================================================== */

/**
 * LoadKeyMappings - 0x0042e908 - 63 bytes
 * Load keys.bin - reads 80 bytes (20 ints × 4 bytes) into key mapping.
 */
void LoadKeyMappings(void)
{
    FILE *fp = fOpen("KEYS.BIN", "rb");
    g_fileHandle = fp;
    if (fp != NULL) {
        fRead(g_keyMappingData, 4, 20, fp);
        bswap32_arr(g_keyMappingData, 20);          /* canonical-LE file → host order */
        fClose(fp);
    }
}

/**
 * SaveKeyMappings - 0x0042e948 - 63 bytes
 * Save keys.bin - writes 80 bytes from key mapping.
 */
void SaveKeyMappings(void)
{
    FILE *fp = fOpen("KEYS.BIN", "wb");
    g_fileHandle2 = fp;
    if (fp != NULL) {
        bswap32_arr(g_keyMappingData, 20);          /* host order → canonical-LE file */
        fWrite(g_keyMappingData, 4, 20, fp);
        bswap32_arr(g_keyMappingData, 20);          /* restore host order */
        fClose(fp);
    }
}

/**
 * LoadJoystickConfig - 0x00477d14 - 63 bytes
 * Load joystick.inf - reads 1128 bytes into joystick config.
 */
#ifdef SONICR_XBOX
/* Xbox pad buttons past the ten the JOYSTICK.INF slot layout can hold (Black,
 * the L/R triggers): their bindings live here, per slot, and are appended to
 * JOYSTICK.INF after the four original slots. valid=0 means "never remapped:
 * use the shared defaults" (g_joystickConfigWords). */
short g_joyExtCfg[4][XB_EXT_BUTTONS];
unsigned char g_joyExtValid[4];
#endif

void LoadJoystickConfig(void)
{
    FILE *fp = fOpen("JOYSTICK.INF", "rb");
    g_fileHandle = fp;
    if (fp != NULL) {
        fRead(g_joystickConfig, 0x11a, 4, fp);
        /* Each 0x11a slot: name bytes [0..0x103] + 11 config shorts [0x104..0x119] */
        for (int s = 0; s < 4; s++) {
            bswap16_arr(g_joystickConfig + s * 0x11a + 0x104, 11);
        }
#ifdef SONICR_XBOX
        {
            char magic[4];
            unsigned char valid[4];
            short cfg[4][XB_EXT_BUTTONS];
            if (fRead(magic, 1, 4, fp) == 4 && memcmp(magic, "XEXT", 4) == 0
                && fRead(valid, 1, 4, fp) == 4
                && fRead(cfg, sizeof(cfg), 1, fp) == 1) {
                memcpy(g_joyExtValid, valid, sizeof(valid));
                memcpy(g_joyExtCfg, cfg, sizeof(cfg));
            }
        }
#endif
        fClose(fp);
    }
}

/**
 * SyncJoystickSlots - FUN_00477d94 - 199 bytes - VALIDATED
 *
 * Compares detected gamepad device names (g_joystickDeviceNames) against
 * configured player slot names (g_joystickSlots). When a slot's name
 * differs from the detected device, copies the new name and default
 * config into the slot.
 */
void SyncJoystickSlots(void)
{
    int count = g_initFeatureC;
    if (count <= 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        char *detectedName = g_joystickDeviceNames[i];    /* 0x675C54 + i*0x104 */
        char *slotBase = g_joystickSlots[i];              /* 0x67541A + i*0x11A */

        if (strcmp(detectedName, slotBase) != 0) {
            char *src = detectedName;
            char *dst = slotBase;
            while (1) {
                dst[0] = src[0];
                if (src[0] == 0) {
                    break;
                }
                dst[1] = src[1];
                if (src[1] == 0) {
                    break;
                }
                src += 2;
                dst += 2;
            }

            /* Per-slot button-bit-pattern table at slot+0x104. The binary's
             * loop pre-increments by 2 before each write (verified at
             * 0x00477DFC-0x00477E13), so the first slot lands at +0x104,
             * not +0x102. ScanKeyRemap commits at the same offset. */
            short *cfgDst = (short *)(slotBase + 0x104);
            for (int w = 0; w < 10; w++) {
                cfgDst[w] = g_joystickConfigWords[w];
            }

            *(short *)(slotBase + 0x118) = g_joystickDeviceFlags[i];
#ifdef SONICR_XBOX
            g_joyExtValid[i] = 0;          /* new device: extra buttons back to defaults */
#endif
        }
    }
}

/* =====================================================================
 * Button scanning
 * ===================================================================== */

/**
 * ScanTypedLetter - 0x0048a554 - 116 bytes
 * Searches for an active joystick button in a linked list of button mappings.
 * Returns: list position (OR'd with 0x10000 if shift held), or -1 if not found.
 */
int ScanTypedLetter(void)
{
    int shiftMask = 0;
    /* 0x6758F6 / 0x675902 are g_diKeyboardState[0x2A] / [0x36] (left/right
     * shift) in the binary's layout; the separate statics are never set. */
    if (g_joyShiftFlag1 != 0 || g_joyShiftFlag2 != 0
        || g_diKeyboardState[0x2A] != 0 || g_diKeyboardState[0x36] != 0) {
        shiftMask = 0x10000;
    }

    for (int btn = 0; btn < 256; btn++) {
        if (g_diKeyboardState[btn] == 0) {
            continue;
        }

        const int (*entry)[4] = s_joyMapRom;
        int idx = 0;

        while ((*entry)[0] != -1) {
            if (btn == (*entry)[0]) {
                int result = idx;
                if ((*entry)[3] == 1) {
                    result |= shiftMask;
                }
                return result;
            }
            idx++;
            entry++;
        }
    }
    return -1;
}

/**
 * ScanInputForAction - 0x00493744 - 217 bytes
 * Scans active joystick buttons and maps them to game actions.
 * Returns: matched button index, or -1 if none found.
 */
int ScanInputForAction(void)
{
    int count = g_inputMappingCount;
    int modeFlag = (g_inputMappingMode == 1) ? 1 : 0;
    int found = 0;

    int setA = modeFlag * 5 * 8;
    int altMode = (modeFlag + 1) & 1;
    int setB = altMode * 5 * 8;

    for (int btn = 0; btn < 256; btn++) {
        if (g_diKeyboardState[btn] == 0) {
            continue;
        }
        found++;

        if (count > 0) {
            int matched = 0;
            for (int k = 0; k < count; k++) {
                if (btn == g_keyMappingData[setB / 4 + k]) {
                    matched = 1;
                    break;
                }
            }
            if (matched) {
                continue;
            }
        }

        {
            int matched = 0;
            for (int k = 0; k < 10; k++) {
                if (btn == g_keyMappingData[setA / 4 + k]) {
                    matched = 1;
                    break;
                }
            }
            if (matched) {
                continue;
            }
        }

        {
            /* Look up against the valid-remap-keys table (binary 0x4937D0).
             * `g_inputCaptureGate` here is an alias for g_optMenuInputGateG at
             * 0x925290 - the press-debounce gate. Holding the key past the
             * first frame is blocked by the gate; the gate clears below
             * (after the loop) on any frame where no key is pressed. */
            const int (*entry)[3] = s_remapValidKeys;
            int valid = 0;
            while ((*entry)[0] != -1) {
                if ((*entry)[0] == btn) {
                    valid = 1;
                    break;
                }
                entry++;
            }
            if (!valid) {
                continue;
            }
            if (g_inputCaptureGate != 0) {
                continue;   /* gate held - same key */
            }
            g_inputCaptureGate = 1;
            g_inputMappingCount = count;
            return btn;
        }
    }

    g_inputCaptureGate = (found > 0) ? 1 : 0;
    g_inputMappingCount = count;
    return -1;
}

/* =====================================================================
 * State reset
 * ===================================================================== */

/**
 * ResetInputState - 0x0047059C - 183 bytes
 * Resets rendering state, camera, and input for a new screen context.
 * Called at each major screen transition (logos, title, menu, race).
 *
 * Despite the name, this does far more than reset input - it reinitializes
 * the viewport, far clip, camera matrix, and several game state flags.
 */
void ResetInputState(void)
{
    g_mirrorMode = 0;
    BeginFrame();
    SetViewportFromConfig(g_viewportArray);
    EndFrame();

    /* FUN_004704f0 - InitFarClipAndFog. In the original, EAX comes from
     * the preceding EndFrame/SetViewportFromConfig return value via Watcom
     * register state - not an explicit parameter. The value just needs to be
     * non-zero so g_farClipFloat = (float)(farClip << 3) doesn't produce 0.
     * Default 0x2B00 matches the Island track far clip. */
    InitFarClipAndFog(0x2B00);

    /* Zero camera state at 0x006e9924..0x006e9944 (9 ints) */
    for (int i = 0; i < 9; i++) {
        s_camState9924[i] = 0;
    }

    SetCameraStructPtr(s_cameraStruct);
    BuildViewMatrix();
    SetViewportClipRect(s_cameraStruct);
    g_introCountdown = 0xFFFFFFFF;
    g_demoMode = DEMO_NONE;
    g_colorTintEnable = 0;
    g_renderEnabled = 1;
    g_postRaceCameraMode = 1;
}
