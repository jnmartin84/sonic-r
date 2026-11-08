/**
 * platform_sdl.c - SDL2 platform implementation
 *
 * SDL2 platform layer - pure C, no platform-specific dependencies.
 * Creates an SDL2 window with OpenGL context.
 *
 * Input: SDL_Event key events → DirectInput scan code mapping → g_diKeyboardState.
 * Timing: SDL_GetTicks → milliseconds.
 * Gamepad: SDL_GameController for any pad in SDL's mapping database, so button
 *   indices are the stable labeled ones (GCBTN_*) instead of whatever order a
 *   driver happens to report. Devices with no mapping — wheels, arcade sticks,
 *   flight sticks — fall back to raw SDL_Joystick. Either path hands the game a
 *   button INDEX which it looks up in the per-slot config, which is the
 *   DirectInput-shaped model the binary's remap UI was written against.
 */

#include <SDL.h>
#include <SDL_mixer.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "platform.h"
#ifdef SONICR_XBOX
#include <hal/video.h>   /* XVideoSetMode */
#include <pbgl.h>        /* OpenGL 1.x on the NV2A */
#include <pbkit/pbkit.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* =====================================================================
 * Globals
 * ===================================================================== */

SDL_Window *g_sdlWindow = NULL;
SDL_GLContext g_sdlGLContext = NULL;

static int s_fbWidth, s_fbHeight;
static int s_quitRequested;
unsigned char s_keystate[256];

#include "pad_bits.h"         /* PAD_LEFT / PAD_UP / ... — input-word bits */
#include "gamepad_buttons.h"  /* GCBTN_* / JOY_SLOT_CFG_WORDS — button indices */

/* The button indices we hand the game have to be SDL's, or every binding is
 * off by however far the enum has drifted. Assert the ends and both edges of
 * the d-pad run rather than trusting the copy in gamepad_buttons.h. */
_Static_assert(GCBTN_A == SDL_CONTROLLER_BUTTON_A, "GCBTN_A != SDL");
_Static_assert(GCBTN_START == SDL_CONTROLLER_BUTTON_START, "GCBTN_START != SDL");
_Static_assert(GCBTN_LEFTSHOULDER == SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
               "GCBTN_LEFTSHOULDER != SDL");
_Static_assert(GCBTN_DPAD_UP == SDL_CONTROLLER_BUTTON_DPAD_UP, "GCBTN_DPAD_UP != SDL");
_Static_assert(GCBTN_DPAD_RIGHT == SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
               "GCBTN_DPAD_RIGHT != SDL");
/* The synthetic trigger indices must sit past every button we actually read
 * through SDL_GameControllerGetButton, which is 0..GCBTN_DPAD_RIGHT. SDL's enum
 * carries on past there (MISC1, paddles, touchpad) and we deliberately stop
 * mirroring it — so this is NOT an equality check against
 * SDL_CONTROLLER_BUTTON_MAX, whose value has grown across SDL releases. */
_Static_assert(GCBTN_TRIGGER_LEFT > GCBTN_DPAD_RIGHT,
               "trigger indices overlap a mirrored SDL button");
_Static_assert(GC_BUTTON_COUNT == GCBTN_TRIGGER_RIGHT + 1,
               "GC_BUTTON_COUNT does not cover both triggers");

/* Gamepad state - up to 4 slots. */
#define MAX_GAMEPADS 4
#define JOY_BUTTONS_PER_SLOT 80

/* One connected pad. `ctrl` non-NULL means SDL had a mapping for the device and
 * its buttons arrive as GCBTN_* indices; otherwise `joy` is set and the indices
 * are whatever order the driver reports. Exactly one of the two is ever set. */
typedef struct {
    SDL_GameController *ctrl;
    SDL_Joystick       *joy;
    SDL_JoystickID      id;        /* instance id — survives index reshuffling */
    int                 nButtons;
} GamepadSlot;

static GamepadSlot s_pads[MAX_GAMEPADS];
static int s_padCount = 0;

/* Analog trigger travel that counts as a press. A quarter pull: far enough past
 * the resting position to ignore a noisy centre, close enough to feel digital. */
#define TRIGGER_THRESHOLD_I 8192

/* Per-joystick-button pressed state at 0x675B0C - defined in
 * globals_extra.c. Stride of 80 bytes per slot matches binary
 * (PollAllInputDevices loop at 0x00476BB1: ebx*5*16 = 80). */
extern unsigned char g_keyPressState[320];

/* Per-button bit-pattern config from the binary's joystick init.
 * g_joystickConfigWords[i] is the 16-bit pad-bit pattern OR'd into the
 * player input word when button index i is held. Defaults set by
 * InitJoystickConfig in init.c.
 *
 * Per-slot copy lives in g_joystickSlots[slot][0x104 + b*2] (10 shorts);
 * SyncJoystickSlots seeds it from g_joystickConfigWords on first run, the
 * in-game remap UI overwrites it. We read from the per-slot copy at poll
 * time so user remaps take effect. */
extern short g_joystickConfigWords[];
extern char g_joystickSlots[4][282];   /* 0x0067541A */
extern char g_joystickDeviceNames[4][260]; /* 0x00675C54 — detected device
                                            * names, stride 0x104. The binary's
                                            * DirectInput enum callback fills
                                            * this at 0x477E8E; SyncJoystickSlots
                                            * compares it against the slot's
                                            * stored name to decide whether the
                                            * saved mapping still belongs to the
                                            * pad now in that slot. Leaving it
                                            * empty made every launch look like
                                            * a new device and reset the map. */
extern int  g_initFeatureC;            /* detected pad count */
extern void SyncJoystickSlots(void);   /* 0x477D94 */

/* Publish a detected pad's name into g_joystickDeviceNames[slot]. The slot
 * name it is compared against is a 282-byte field, so truncate to something
 * that always round-trips through JOYSTICK.INF intact. */
static void platform_publish_joystick_name(int slot, const char *name)
{
    if (slot < 0 || slot >= 4) {
        return;
    }
    char *dst = g_joystickDeviceNames[slot];
    if (name == NULL) {
        name = "Gamepad";
    }
    size_t n = strlen(name);
    if (n > 258) {
        n = 258;
    }
    memcpy(dst, name, n);
    dst[n] = '\0';
}
extern short g_joystickDeviceFlags[8]; /* 0x00675C40 - per-device caps;
                                        * we write the remap scan bound here
                                        * so SyncJoystickSlots propagates
                                        * it to slot[0x118] (which is
                                        * what ScanKeyRemap reads). */

/* =====================================================================
 * Gamepad open / close
 *
 * Shared by startup enumeration and the hotplug events, so a pad arriving
 * either way lands in a slot identically.
 * ===================================================================== */

/* Take the device at `deviceIndex` into the next free slot. Returns 1 if a slot
 * was filled, 0 if it was a duplicate, unopenable, or we are already full. */
static int gamepad_open(int deviceIndex)
{
    if (s_padCount >= MAX_GAMEPADS) {
        return 0;
    }

    /* Dedup on instance id. SDL queues an ADDED for every device already present
     * when the subsystem starts, and those arrive on the first pump AFTER
     * platform_init_gamepads has opened them. Without this the same pad occupies
     * two slots and — worse — has its mapping re-seeded from defaults, silently
     * discarding a remap the user just made. */
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(deviceIndex);
    if (id < 0) {
        return 0;
    }
    for (int i = 0; i < s_padCount; i++) {
        if (s_pads[i].id == id) {
            return 0;
        }
    }

    GamepadSlot pad = { NULL, NULL, id, 0 };
    const char *name = NULL;

    if (SDL_IsGameController(deviceIndex)) {
        pad.ctrl = SDL_GameControllerOpen(deviceIndex);
        if (pad.ctrl) {
            /* Fixed count: SDL synthesises every button in the layout whether
             * the hardware has it or not, so absent ones simply read as
             * released. The two analog triggers are appended on the end. */
            pad.nButtons = GC_BUTTON_COUNT;
            name = SDL_GameControllerName(pad.ctrl);
        }
    }
    if (!pad.ctrl) {
        pad.joy = SDL_JoystickOpen(deviceIndex);
        if (!pad.joy) {
            return 0;
        }
        pad.nButtons = SDL_JoystickNumButtons(pad.joy);
        if (pad.nButtons > JOY_BUTTONS_PER_SLOT) {
            pad.nButtons = JOY_BUTTONS_PER_SLOT;
        }
        name = SDL_JoystickName(pad.joy);
    }

    int s = s_padCount;
    s_pads[s] = pad;

    /* Publish name and scan bound, then let SyncJoystickSlots decide whether the
     * slot's stored mapping still belongs to the pad now sitting in it. The
     * bound is the per-slot config capacity, NOT the device's button count — see
     * JOY_SLOT_CFG_WORDS for why a button the commit cannot store must not be
     * capturable in the first place. */
    platform_publish_joystick_name(s, name);
    g_joystickDeviceFlags[s] = (short)(pad.nButtons < JOY_SLOT_CFG_WORDS
                                       ? pad.nButtons : JOY_SLOT_CFG_WORDS);

    s_padCount++;
    g_initFeatureC = s_padCount;

    fprintf(stderr, "Gamepad slot %d: %s (%s, %d buttons)\n",
            s, name ? name : "unnamed",
            pad.ctrl ? "mapped controller" : "raw joystick", pad.nButtons);
    return 1;
}

static void gamepad_close(int slot)
{
    if (s_pads[slot].ctrl) {
        SDL_GameControllerClose(s_pads[slot].ctrl);
    } else if (s_pads[slot].joy) {
        SDL_JoystickClose(s_pads[slot].joy);
    }
    s_pads[slot].ctrl     = NULL;
    s_pads[slot].joy      = NULL;
    s_pads[slot].id       = -1;
    s_pads[slot].nButtons = 0;
}

/* =====================================================================
 * SDL_Scancode → DirectInput scan code mapping
 *
 * SDL scancodes mapped to DIK_* values so the existing
 * PollAllInputDevices key mapping works unchanged.
 * ===================================================================== */

static unsigned char SDLScancodeToDIK(SDL_Scancode sc)
{
    switch (sc) {
        case SDL_SCANCODE_A:
            return 0x1E; /* DIK_A */
        case SDL_SCANCODE_B:
            return 0x30; /* DIK_B */
        case SDL_SCANCODE_C:
            return 0x2E; /* DIK_C */
        case SDL_SCANCODE_D:
            return 0x20; /* DIK_D */
        case SDL_SCANCODE_E:
            return 0x12; /* DIK_E */
        case SDL_SCANCODE_F:
            return 0x21; /* DIK_F */
        case SDL_SCANCODE_G:
            return 0x22; /* DIK_G */
        case SDL_SCANCODE_H:
            return 0x23; /* DIK_H */
        case SDL_SCANCODE_I:
            return 0x17; /* DIK_I */
        case SDL_SCANCODE_J:
            return 0x24; /* DIK_J */
        case SDL_SCANCODE_K:
            return 0x25; /* DIK_K */
        case SDL_SCANCODE_L:
            return 0x26; /* DIK_L */
        case SDL_SCANCODE_M:
            return 0x32; /* DIK_M */
        case SDL_SCANCODE_N:
            return 0x31; /* DIK_N */
        case SDL_SCANCODE_O:
            return 0x18; /* DIK_O */
        case SDL_SCANCODE_P:
            return 0x19; /* DIK_P */
        case SDL_SCANCODE_Q:
            return 0x10; /* DIK_Q */
        case SDL_SCANCODE_R:
            return 0x13; /* DIK_R */
        case SDL_SCANCODE_S:
            return 0x1F; /* DIK_S */
        case SDL_SCANCODE_T:
            return 0x14; /* DIK_T */
        case SDL_SCANCODE_U:
            return 0x16; /* DIK_U */
        case SDL_SCANCODE_V:
            return 0x2F; /* DIK_V */
        case SDL_SCANCODE_W:
            return 0x11; /* DIK_W */
        case SDL_SCANCODE_X:
            return 0x2D; /* DIK_X */
        case SDL_SCANCODE_Y:
            return 0x15; /* DIK_Y */
        case SDL_SCANCODE_Z:
            return 0x2C; /* DIK_Z */
        case SDL_SCANCODE_1:
            return 0x02; /* DIK_1 */
        case SDL_SCANCODE_2:
            return 0x03; /* DIK_2 */
        case SDL_SCANCODE_3:
            return 0x04; /* DIK_3 */
        case SDL_SCANCODE_4:
            return 0x05; /* DIK_4 */
        case SDL_SCANCODE_5:
            return 0x06; /* DIK_5 */
        case SDL_SCANCODE_6:
            return 0x07; /* DIK_6 */
        case SDL_SCANCODE_7:
            return 0x08; /* DIK_7 */
        case SDL_SCANCODE_8:
            return 0x09; /* DIK_8 */
        case SDL_SCANCODE_9:
            return 0x0A; /* DIK_9 */
        case SDL_SCANCODE_0:
            return 0x0B; /* DIK_0 */
        case SDL_SCANCODE_MINUS:
            return 0x0C; /* DIK_MINUS */
        case SDL_SCANCODE_EQUALS:
            return 0x0D; /* DIK_EQUALS */
        case SDL_SCANCODE_BACKSPACE:
            return 0x0E; /* DIK_BACK */
        case SDL_SCANCODE_LEFTBRACKET:
            return 0x1A; /* DIK_LBRACKET */
        case SDL_SCANCODE_RIGHTBRACKET:
            return 0x1B; /* DIK_RBRACKET */
        case SDL_SCANCODE_SEMICOLON:
            return 0x27; /* DIK_SEMICOLON */
        case SDL_SCANCODE_APOSTROPHE:
            return 0x28; /* DIK_APOSTROPHE */
        case SDL_SCANCODE_GRAVE:
            return 0x29; /* DIK_GRAVE */
        case SDL_SCANCODE_BACKSLASH:
            return 0x2B; /* DIK_BACKSLASH */
        case SDL_SCANCODE_COMMA:
            return 0x33; /* DIK_COMMA */
        case SDL_SCANCODE_PERIOD:
            return 0x34; /* DIK_PERIOD */
        case SDL_SCANCODE_SLASH:
            return 0x35; /* DIK_SLASH */
        case SDL_SCANCODE_RETURN:
            return 0x1C; /* DIK_RETURN */
        case SDL_SCANCODE_TAB:
            return 0x0F; /* DIK_TAB */
        case SDL_SCANCODE_SPACE:
            return 0x39; /* DIK_SPACE */
        case SDL_SCANCODE_ESCAPE:
            return 0x01; /* DIK_ESCAPE */
        case SDL_SCANCODE_LSHIFT:
            return 0x2A; /* DIK_LSHIFT */
        case SDL_SCANCODE_RSHIFT:
            return 0x36; /* DIK_RSHIFT */
        case SDL_SCANCODE_LALT:
            return 0x38; /* DIK_LALT */
        case SDL_SCANCODE_LCTRL:
            return 0x1D; /* DIK_LCONTROL */
        case SDL_SCANCODE_RALT:
            return 0xB8; /* DIK_RMENU */
        case SDL_SCANCODE_RCTRL:
            return 0x9D; /* DIK_RCONTROL */
        case SDL_SCANCODE_F1:
            return 0x3B; /* DIK_F1 */
        case SDL_SCANCODE_F2:
            return 0x3C; /* DIK_F2 */
        case SDL_SCANCODE_F3:
            return 0x3D; /* DIK_F3 */
        case SDL_SCANCODE_F4:
            return 0x3E; /* DIK_F4 */
        case SDL_SCANCODE_F5:
            return 0x3F; /* DIK_F5 */
        case SDL_SCANCODE_F6:
            return 0x40; /* DIK_F6 */
        case SDL_SCANCODE_F7:
            return 0x41; /* DIK_F7 */
        case SDL_SCANCODE_F8:
            return 0x42; /* DIK_F8 */
        case SDL_SCANCODE_LEFT:
            return 0xCB; /* DIK_LEFT */
        case SDL_SCANCODE_RIGHT:
            return 0xCD; /* DIK_RIGHT */
        case SDL_SCANCODE_DOWN:
            return 0xD0; /* DIK_DOWN */
        case SDL_SCANCODE_UP:
            return 0xC8; /* DIK_UP */
        case SDL_SCANCODE_INSERT:
            return 0xD2; /* DIK_INSERT */
        case SDL_SCANCODE_DELETE:
            return 0xD3; /* DIK_DELETE */
        case SDL_SCANCODE_HOME:
            return 0xC7; /* DIK_HOME */
        case SDL_SCANCODE_END:
            return 0xCF; /* DIK_END */
        case SDL_SCANCODE_PAGEUP:
            return 0xC9; /* DIK_PRIOR */
        case SDL_SCANCODE_PAGEDOWN:
            return 0xD1; /* DIK_NEXT */
        /* Numeric keypad. SDL reports these scancodes for the physical
         * keys regardless of NumLock, so no shift-state handling is
         * needed. All sixteen are in the remap-eligible ROM table at
         * 0x4FECBC, so the options screen accepts every one of them. */
        case SDL_SCANCODE_KP_DIVIDE:
            return 0xB5; /* DIK_DIVIDE */
        case SDL_SCANCODE_KP_MULTIPLY:
            return 0x37; /* DIK_MULTIPLY */
        case SDL_SCANCODE_KP_MINUS:
            return 0x4A; /* DIK_SUBTRACT */
        case SDL_SCANCODE_KP_PLUS:
            return 0x4E; /* DIK_ADD */
        case SDL_SCANCODE_KP_ENTER:
            return 0x9C; /* DIK_NUMPADENTER */
        case SDL_SCANCODE_KP_1:
            return 0x4F; /* DIK_NUMPAD1 */
        case SDL_SCANCODE_KP_2:
            return 0x50; /* DIK_NUMPAD2 */
        case SDL_SCANCODE_KP_3:
            return 0x51; /* DIK_NUMPAD3 */
        case SDL_SCANCODE_KP_4:
            return 0x4B; /* DIK_NUMPAD4 */
        case SDL_SCANCODE_KP_5:
            return 0x4C; /* DIK_NUMPAD5 */
        case SDL_SCANCODE_KP_6:
            return 0x4D; /* DIK_NUMPAD6 */
        case SDL_SCANCODE_KP_7:
            return 0x47; /* DIK_NUMPAD7 */
        case SDL_SCANCODE_KP_8:
            return 0x48; /* DIK_NUMPAD8 */
        case SDL_SCANCODE_KP_9:
            return 0x49; /* DIK_NUMPAD9 */
        case SDL_SCANCODE_KP_0:
            return 0x52; /* DIK_NUMPAD0 */
        case SDL_SCANCODE_KP_PERIOD:
            return 0x53; /* DIK_DECIMAL */
        default:
            return 0;
    }
}

/* =====================================================================
 * Platform API implementation
 * ===================================================================== */

#ifdef SONICR_XBOX
int g_xbStretch;   /* 720p: stretch the 4:3 image to the full 16:9 frame */
int g_xbRunningMode;   /* video mode in use: 0 480p, 1 720p, 2 720p stretched */
int g_xb720Allowed;    /* dashboard has 720p enabled */
#endif

int platform_init(int width, int height, int fullscreen, const char *title)
{
    s_fbWidth = width;
    s_fbHeight = height;
    memset(s_keystate, 0, sizeof(s_keystate));
    s_quitRequested = 0;

#ifdef SONICR_XBOX
    /* Original Xbox: no window system. SDL provides input and audio only;
     * pbgl (pbkit) owns the framebuffer and the GPU. */
    (void)fullscreen;
    (void)title;
    {
        /* Default 640x480 (480p/480i per the dashboard). 720p (the 4:3 image
         * stretched to fill 16:9) is chosen in Options > Graphics > Resolution,
         * or, before a choice is saved, with a D:\VIDEO.TXT starting with 'W'
         * (or '7', the old pillarbox setting, which had display bugs). Both
         * need 720p enabled in the dashboard. */
        DWORD enc = XVideoGetEncoderSettings();
        int want720 = 0;
        extern int g_xbVideoMode;   /* Options > Graphics > Resolution (save.c) */
        if (g_xbVideoMode >= 0) {
            /* 1 (old pillarboxed 720p setting) now also means full-screen 720p. */
            want720 = (g_xbVideoMode >= 1);
            g_xbStretch = want720;
        }
        else {
            FILE *cfg = fopen("D:\\VIDEO.TXT", "r");
            if (cfg) {
                int c = fgetc(cfg);
                if (c == '7' || c == 'W' || c == 'w') { want720 = 1; g_xbStretch = 1; }
                fclose(cfg);
            }
        }
        g_xb720Allowed = (enc & VIDEO_MODE_720P) != 0;
        fprintf(stderr, "video: dashboard settings 0x%08lx (720p %s, 480p %s, widescreen %s)\n",
                (unsigned long)enc, (enc & VIDEO_MODE_720P) ? "on" : "off",
                (enc & VIDEO_MODE_480P) ? "on" : "off", (enc & VIDEO_WIDESCREEN) ? "on" : "off");
        if (want720 && XVideoSetMode(1280, 720, 32, REFRESH_DEFAULT)) {
            width = 1280;
            height = 720;
        } else if (XVideoSetMode(640, 480, 32, REFRESH_DEFAULT)) {
            width = 640;
            height = 480;
        } else {
            fprintf(stderr, "XVideoSetMode failed\n");
            return -1;
        }
        s_fbWidth = width;
        s_fbHeight = height;
        if (width != 1280) g_xbStretch = 0;
        g_xbRunningMode = (width == 1280) ? (g_xbStretch ? 2 : 1) : 0;
        fprintf(stderr, "video: running at %dx%d%s\n", width, height, g_xbStretch ? " (stretched to 16:9)" : "");
    }
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
        return -1;
    }
    pbgl_set_swap_interval(1);   /* vsync */
    /* pbkit's push buffer is only reset once per frame, and the game draws with
     * immediate-mode glVertex calls (~60 bytes each). The default 512 KB filled
     * up in busy frames (GPU error "DMA push buffer engine encountered invalid
     * data"), so use 4 MB. Must be set before pb_init (called by pbgl_init). */
    pb_size(4 * 1024 * 1024);
    if (pbgl_init(GL_TRUE) != 0) {
        fprintf(stderr, "pbgl_init failed\n");
        return -1;
    }
    {
        extern void xbox_start_watchdog(void);
        { extern int g_xbDebug; if (g_xbDebug) xbox_start_watchdog(); }
    }
    return 0;
#endif

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
#ifdef _WIN32
    /* Win10+ blocks SetForegroundWindow from non-foreground processes; this
     * hint (SDL 2.0.22+) tells SDL to bypass the lock-out so SDL_RaiseWindow /
     * SDL_SetWindowInputFocus actually work. */
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO
                 | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_StopTextInput();  /* disable macOS IME composition overlay */

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
#else
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif

    Uint32 winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (fullscreen) {
        winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    g_sdlWindow = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        winFlags);
    if (!g_sdlWindow) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Force the SDL window to the foreground with input focus. Without this
     * on Windows, the launching shell keeps focus and key events miss the
     * SDL window until the user wiggles the mouse over it. */
    SDL_RaiseWindow(g_sdlWindow);
    SDL_SetWindowInputFocus(g_sdlWindow);
    SDL_ShowCursor(SDL_DISABLE);

    g_sdlGLContext = SDL_GL_CreateContext(g_sdlWindow);
    if (!g_sdlGLContext) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(g_sdlWindow, g_sdlGLContext);

#ifndef __EMSCRIPTEN__
    SDL_GL_SetSwapInterval(1);  /* vsync - browser handles this via rAF */
#endif

    return 0;
}

void platform_shutdown(void)
{
    for (int i = 0; i < s_padCount; i++) {
        gamepad_close(i);
    }
    s_padCount = 0;

#ifdef SONICR_XBOX
    Mix_CloseAudio();
    SDL_Quit();
    pbgl_shutdown();
    return;
#endif

    if (g_sdlGLContext) {
        SDL_GL_DeleteContext(g_sdlGLContext);
        g_sdlGLContext = NULL;
    }
    if (g_sdlWindow) {
        SDL_DestroyWindow(g_sdlWindow);
        g_sdlWindow = NULL;
    }

    Mix_CloseAudio();
    SDL_Quit();
}

/* Executable's own directory, cached so the caller never frees it.
 * Returns NULL under Emscripten (no meaningful base path; data lives in the
 * virtual filesystem rooted at DATA_DIR). */
const char *platform_base_path(void)
{
#if defined(__EMSCRIPTEN__)
    return NULL;
#elif defined(SONICR_XBOX)
    /* The XBE's own directory is mapped to D: when launched from the dashboard
     * or xemu; game data sits beside default.xbe. */
    return "D:\\";
#else
    static char s_base[1024];
    static int s_have = 0;
    if (!s_have) {
        char *b = SDL_GetBasePath();
        if (!b) {
            return NULL;
        }
        strncpy(s_base, b, sizeof(s_base) - 1);
        s_base[sizeof(s_base) - 1] = '\0';
        SDL_free(b);
        s_have = 1;
    }
    return s_have ? s_base : NULL;
#endif
}

/* =====================================================================
 * Event processing - shared by platform_poll_events and
 * platform_pump_events. Processes one SDL event.
 * ===================================================================== */
static void HandleSDLEvent(SDL_Event *event)
{
    switch (event->type) {
        case SDL_QUIT: {
    #ifdef __EMSCRIPTEN__
            s_quitRequested = 1;
    #else
            exit(0);
    #endif
            break;
        }

        case SDL_KEYDOWN: {
            unsigned char dik = SDLScancodeToDIK(event->key.keysym.scancode);
            if (dik) {
                s_keystate[dik] = 0x80;
            }
            break;
        }

        case SDL_KEYUP: {
            unsigned char dik = SDLScancodeToDIK(event->key.keysym.scancode);
            if (dik) {
                s_keystate[dik] = 0x00;
            }
            break;
        }

        /* Hotplug is handled on the JOY events, not the CONTROLLER ones, even
         * for mapped pads. SDL emits CONTROLLERDEVICEADDED *in addition to*
         * JOYDEVICEADDED for a device it has a mapping for — not instead of it —
         * so watching only the joystick pair covers every device through one
         * path and cannot double-open a controller. gamepad_open() decides which
         * API to drive it with. */
        case SDL_JOYDEVICEADDED: {
            if (gamepad_open(event->jdevice.which)) {
                SyncJoystickSlots();
            }
            break;
        }

        case SDL_JOYDEVICEREMOVED: {
            /* ADDED carries a device index, REMOVED an instance id. */
            SDL_JoystickID jid = event->jdevice.which;
            for (int i = 0; i < s_padCount; i++) {
                if (s_pads[i].id != jid) {
                    continue;
                }
                gamepad_close(i);
                /* Clear pressed state for removed slot */
                memset(&g_keyPressState[i * JOY_BUTTONS_PER_SLOT], 0,
                    JOY_BUTTONS_PER_SLOT);
                /* Shift remaining slots down */
                for (int j = i; j < s_padCount - 1; j++) {
                    s_pads[j] = s_pads[j + 1];
                }
                s_pads[s_padCount - 1].ctrl     = NULL;
                s_pads[s_padCount - 1].joy      = NULL;
                s_pads[s_padCount - 1].id       = -1;
                s_pads[s_padCount - 1].nButtons = 0;
                s_padCount--;
                break;
            }
            break;
        }

        default:
            break;
    }
}

#ifdef SONICR_XBOX
/* nxdk's SDL sends no key events: the key state is the USB keyboard's. */
extern void xbox_kbd_merge(unsigned char *keys);
static void xb_refresh_keys(void)
{
    memset(s_keystate, 0, sizeof(s_keystate));
    xbox_kbd_merge(s_keystate);
}
#else
static void xb_refresh_keys(void) { }
#endif

int platform_poll_events(unsigned char *keystateOut, int keystateSize)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        HandleSDLEvent(&event);
    }
    xb_refresh_keys();

    /* Copy keystate to caller's buffer */
    int copySize = keystateSize < 256 ? keystateSize : 256;
    memcpy(keystateOut, s_keystate, copySize);

    return s_quitRequested;
}

/**
 * platform_pump_events - pump SDL event loop.
 * Must be called from any frame loop so the window redraws and input works.
 */
void platform_pump_events(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        HandleSDLEvent(&event);
    }
    xb_refresh_keys();
}

/* =====================================================================
 * Gamepad polling
 *
 * The binary's remap UI binds individual button INDICES to 16-bit pad-bit
 * patterns (button index → action bits), so that is the shape the game gets
 * either way. What SDL_GameController changes is where the indices come from:
 * for a mapped pad they are the fixed GCBTN_* ordinals, so "button 0" is the
 * south face button on every device rather than whatever the driver enumerated
 * first. Unmapped devices keep the old raw indices.
 *
 * Directions: on the controller path the d-pad arrives as buttons 11-14 and
 * flows through the config table like anything else. On the raw path there are
 * no reliable d-pad indices, so the hat supplies directions and direction bits
 * are masked out of table lookups — otherwise a table written for GCBTN_*
 * ordinals would fire PAD_UP off some unrelated raw button. The left stick
 * produces directions on both paths.
 *
 * Defaults live in InitJoystickConfig (init.c) and are laid out for GCBTN_*.
 * An unmapped device will need a pass through Options → Controls → Joystick.
 *
 * Analog deadzone: 0.6 (digital threshold).
 * ===================================================================== */

#define STICK_DEADZONE_I 19660  /* 0.6 * 32767 */
#define JOY_CFG_MAX      32     /* g_joystickConfigWords array size */

int platform_init_gamepads(void)
{
    /* Open anything already connected at startup. Hotplug arrivals go through
     * the same gamepad_open() from the JOYDEVICEADDED handler. */
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n && s_padCount < MAX_GAMEPADS; i++) {
        gamepad_open(i);
    }
    return s_padCount;
}

/* Is button index `b` currently held? On the controller path the two indices
 * past the mirrored range (see gamepad_buttons.h) are the analog triggers,
 * thresholded into a digital press. */
static int gamepad_button_held(const GamepadSlot *pad, int b)
{
    if (pad->ctrl) {
        if (b == GCBTN_TRIGGER_LEFT) {
            return SDL_GameControllerGetAxis(pad->ctrl,
                       SDL_CONTROLLER_AXIS_TRIGGERLEFT) > TRIGGER_THRESHOLD_I;
        }
        if (b == GCBTN_TRIGGER_RIGHT) {
            return SDL_GameControllerGetAxis(pad->ctrl,
                       SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIGGER_THRESHOLD_I;
        }
        return SDL_GameControllerGetButton(pad->ctrl,
                   (SDL_GameControllerButton)b) != 0;
    }
    return SDL_JoystickGetButton(pad->joy, b) != 0;
}

int platform_poll_gamepads(unsigned short *joySlotState, int maxSlots)
{
    int count = s_padCount;
    if (count > maxSlots) {
        count = maxSlots;
    }

    for (int i = 0; i < count; i++) {
        const GamepadSlot *pad = &s_pads[i];
        unsigned char *pressBase = &g_keyPressState[i * JOY_BUTTONS_PER_SLOT];

        if (!pad->ctrl && !pad->joy) {
            joySlotState[i] = 0;
            memset(pressBase, 0, JOY_BUTTONS_PER_SLOT);
            continue;
        }

        unsigned short bits = 0;

        /* Hat (D-pad) — raw path only, first hat only. A mapped pad reports its
         * d-pad as buttons, and SDL exposes no hats for it. */
        if (pad->joy && SDL_JoystickNumHats(pad->joy) > 0) {
            Uint8 hat = SDL_JoystickGetHat(pad->joy, 0);
            if (hat & SDL_HAT_LEFT) {
                bits |= PAD_LEFT;
            }
            if (hat & SDL_HAT_RIGHT) {
                bits |= PAD_RIGHT;
            }
            if (hat & SDL_HAT_UP) {
                bits |= PAD_UP;
            }
            if (hat & SDL_HAT_DOWN) {
                bits |= PAD_DOWN;
            }
        }

        /* Left analog stick — digital threshold. Negative = left/up, positive =
         * right/down. On the controller path SDL guarantees that orientation; on
         * the raw path it is merely the common convention, and positive-up
         * outliers read inverted (one more reason to prefer a mapping). */
        int haveStick = 1;
        Sint16 lx = 0, ly = 0;
        if (pad->ctrl) {
            lx = SDL_GameControllerGetAxis(pad->ctrl, SDL_CONTROLLER_AXIS_LEFTX);
            ly = SDL_GameControllerGetAxis(pad->ctrl, SDL_CONTROLLER_AXIS_LEFTY);
        } else if (SDL_JoystickNumAxes(pad->joy) >= 2) {
            lx = SDL_JoystickGetAxis(pad->joy, 0);
            ly = SDL_JoystickGetAxis(pad->joy, 1);
        } else {
            haveStick = 0;
        }
        if (haveStick) {
            if (lx < -STICK_DEADZONE_I) {
                bits |= PAD_LEFT;
            }
            if (lx > STICK_DEADZONE_I) {
                bits |= PAD_RIGHT;
            }
            if (ly < -STICK_DEADZONE_I) {
                bits |= PAD_UP;
            }
            if (ly > STICK_DEADZONE_I) {
                bits |= PAD_DOWN;
            }
        }

        /* Buttons — populate g_keyPressState (so ScanKeyRemap can scan during
         * the remap UI), then OR in the bit pattern for each held button. The
         * pattern comes from the per-slot config at slot[0x104 + b*2], which is
         * what the remap UI commits to; indices past its capacity fall back to
         * the shared g_joystickConfigWords[b]. */
        const short *slotCfg = (const short *)&g_joystickSlots[i][0x104];
        for (int b = 0; b < pad->nButtons; b++) {
            int held = gamepad_button_held(pad, b);
            pressBase[b] = held ? 0x80 : 0x00;
            if (!held) {
                continue;
            }
            short cfg;
            if (b < JOY_SLOT_CFG_WORDS) {
                cfg = slotCfg[b];
            } else if (b < JOY_CFG_MAX) {
                cfg = g_joystickConfigWords[b];
#ifdef SONICR_XBOX
                if (i < 4 && g_joyExtValid[i] && b < XB_EXT_BUTTONS) {
                    cfg = g_joyExtCfg[i][b];   /* remapped Black / trigger */
                }
#endif
            } else {
                cfg = 0;
            }
            if (pad->joy) {
                /* Raw device: directions came from the hat/stick above, and this
                 * table is not laid out for this device's indices. */
                cfg &= (short)~PAD_DIRECTIONS;
            }
            bits |= (unsigned short)cfg;
        }
        /* Zero any trailing slots that this device doesn't have. */
        for (int b = pad->nButtons; b < JOY_BUTTONS_PER_SLOT; b++) {
            pressBase[b] = 0x00;
        }

        joySlotState[i] = bits;
    }

    /* Clear unused slot state and output. */
    for (int i = count; i < maxSlots; i++) {
        joySlotState[i] = 0;
    }
    for (int i = count; i < MAX_GAMEPADS; i++) {
        memset(&g_keyPressState[i * JOY_BUTTONS_PER_SLOT], 0,
               JOY_BUTTONS_PER_SLOT);
    }

    return count;
}

uint32_t platform_get_time_ms(void)
{
    return SDL_GetTicks();
}

void platform_sleep_ms(int ms)
{
#ifdef __EMSCRIPTEN__
    emscripten_sleep(ms);
#else
    SDL_Delay(ms);
#endif
}

int platform_audio_init(void)
{
    /* Audio is initialized via SDL_mixer in main() */
    return 0;
}

void platform_audio_shutdown(void)
{
    /* Audio shutdown handled by Mix_CloseAudio in main() */
}

/* =====================================================================
 * SDL platform helpers for render_gl.c
 * ===================================================================== */

#ifdef SONICR_XBOX
volatile const char *g_lastMark = "start";
volatile int g_xbFrames;
unsigned g_dbgTLoop, g_dbgTHud, g_dbgLogicMs, g_dbgRenderMs, g_dbgLastLogic, g_dbgLastRender;
int g_dbgRainActive, g_dbgRainDrawn, g_dbgRainRejected;
static unsigned *g_dbgSwapAcc;
extern int g_dbgMode;
static int xb_watchdog(void *arg)
{
    int last = -1, stalled = 0;
    (void)arg;
    for (;;) {
        SDL_Delay(1000);
        if (g_xbFrames == last) {
            stalled++;
            if (stalled == 4 || (stalled > 4 && stalled % 10 == 0)) {
                fprintf(stderr, "WATCHDOG: no frame for %d s, last mark: %s\n", stalled,
                        (const char *)g_lastMark);
                fflush(stderr);
            }
        } else {
            stalled = 0;
            last = g_xbFrames;
        }
    }
    return 0;
}
void xbox_start_watchdog(void) { SDL_CreateThread(xb_watchdog, "watchdog", NULL); }


/* Buffered diagnostics: unbuffered fprintf to the HDD stalls a frame by ~150 ms,
 * which showed up as periodic hitches. Collect lines in RAM and write them out
 * only at a natural pause (loading/menus) or when the buffer fills. */
#include <stdarg.h>
static char s_xbLog[12288];
static int s_xbLogLen;
static void xb_log_flush(void)
{
    if (s_xbLogLen) { s_xbLog[s_xbLogLen] = 0; fputs(s_xbLog, stderr); s_xbLogLen = 0; }
    fflush(stderr);   /* stderr is fully buffered on Xbox: this is the write point */
}
/* Move the line buffer into stdio's (RAM) buffer without touching the disk. */
static void xb_log_spill(void)
{
    if (s_xbLogLen) { s_xbLog[s_xbLogLen] = 0; fputs(s_xbLog, stderr); s_xbLogLen = 0; }
}
static void xb_logf(const char *fmt, ...)
{
    va_list ap;
    int n;
    if (s_xbLogLen > (int)sizeof(s_xbLog) - 600) xb_log_spill();
    va_start(ap, fmt);
    n = vsnprintf(s_xbLog + s_xbLogLen, sizeof(s_xbLog) - 1 - s_xbLogLen, fmt, ap);
    va_end(ap);
    if (n > 0) s_xbLogLen += n;
    if (s_xbLogLen > (int)sizeof(s_xbLog) - 1) s_xbLogLen = (int)sizeof(s_xbLog) - 1;
}

static unsigned s_dhist[64];
/* Early lost-flip repair (see platform_gl_swap). After each swap we expect the
 * frame just finished to reach the screen within two VBlanks. Checked while the
 * next frame is being drawn (xb_draw_fan calls this every 128 fans): if the
 * display is still on the older buffer, point it at the finished frame now, so
 * that by the next swap no wait is needed. */
#define XB_PCRTC_START (*(volatile unsigned *)0xFD600800u)
static volatile int s_fwArmed;
static unsigned s_fwExpect, s_fwVbl;
int g_flipEarly, g_flipLate;
void xb_flip_watch(void)
{
    if (!s_fwArmed) return;
    if ((unsigned)pb_get_vbl_counter() - s_fwVbl < 2u) return;
    s_fwArmed = 0;
    if ((XB_PCRTC_START & 0x03FFFFFFu) != s_fwExpect) {
        XB_PCRTC_START = s_fwExpect;
        g_flipEarly++;
    }
}

int s_xbStatePrev[20], s_xbStatePrevOk;
static void xb_capture_state(int *v)
{
    extern int g_processedObjectCount, g_totalObjects, g_farClipDepth;
    extern int g_camIntX, g_camIntY, g_camIntZ, g_viewMtx[16];
    extern int g_clipLeft, g_clipRight, g_clipTop, g_clipBottom;
    extern int g_projScaleXCurrent, g_projScaleY, g_screenCenterX, g_screenCenterY;
    int k = 0;
    v[k++] = g_processedObjectCount; v[k++] = g_totalObjects; v[k++] = g_farClipDepth;
    v[k++] = g_camIntX; v[k++] = g_camIntY; v[k++] = g_camIntZ;
    v[k++] = g_viewMtx[2]; v[k++] = g_viewMtx[6]; v[k++] = g_viewMtx[10];
    v[k++] = g_viewMtx[0]; v[k++] = g_viewMtx[4]; v[k++] = g_viewMtx[8];
    v[k++] = g_clipLeft; v[k++] = g_clipRight; v[k++] = g_clipTop; v[k++] = g_clipBottom;
    v[k++] = g_projScaleXCurrent; v[k++] = g_projScaleY; v[k++] = g_screenCenterX; v[k++] = g_screenCenterY;
}
static void xb_logf(const char *fmt, ...);
static void xb_log_state(const char *tag, const int *v)
{
    xb_logf("%sobjs %d/%d far %d cam %d,%d,%d fwd %d,%d,%d right %d,%d,%d clip %d,%d,%d,%d proj %d,%d ctr %d,%d\n",
            tag, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], v[14],
            v[15], v[16], v[17], v[18], v[19]);
}
   /* draws per frame, indexed by frame & 63 */
/* (still inside the SONICR_XBOX block opened above) */
/* Timestamped event note for the log (first 80), used to line events up with
 * what is seen on screen. */
void xb_note(const char *what)
{
    static int s_n;
    if (s_n < 80) {
        s_n++;
        xb_logf("event %s at frame %d, %u ms\n", what, g_xbFrames, (unsigned)SDL_GetTicks());
    }
}
#endif

void platform_gl_swap(void)
{
#ifdef SONICR_XBOX
    { extern void xb_batch_end(void); xb_batch_end(); }
    { extern void xb_audio_watch(void); xb_audio_watch(); }
    {
        static int s_frames;
        s_frames++;
        g_xbFrames = s_frames;
        {
            /* IGR (in-game reset), the homebrew convention: hold L + R triggers
             * + Back + Black on any pad for half a second -> back to the
             * dashboard, same path as Options > Exit to Dashboard. */
            static int s_igrHeld;
            int combo = 0;
            for (int i = 0; i < s_padCount && i < MAX_GAMEPADS; i++) {
                SDL_GameController *c = s_pads[i].ctrl;
                if (c &&
                    SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > TRIGGER_THRESHOLD_I &&
                    SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TRIGGER_THRESHOLD_I &&
                    SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK) &&
                    SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {   /* Black */
                    combo = 1;
                    break;
                }
            }
            s_igrHeld = combo ? s_igrHeld + 1 : 0;
            if (s_igrHeld >= 15) {
                xb_log_flush();   /* write out the buffered diagnostics first */
                fprintf(stderr, "IGR: reset requested\n");
                fflush(stderr);
                exit(0);   /* atexit handler idles the audio and does the full reboot */
            }
        }
        extern int g_xbDebug;
        if (g_xbDebug) {   /* debug-only pad combos (DEBUG.TXT) */
        {
            /* Back button cycles debug render modes (0 = normal). */
            extern int g_dbgMode;
            static int s_backPrev;
            /* Back alone is the menu "cancel"; the debug cycle needs LB + Back. */
            int back = (s_pads[0].ctrl &&
                        SDL_GameControllerGetButton(s_pads[0].ctrl, SDL_CONTROLLER_BUTTON_BACK) &&
                        SDL_GameControllerGetButton(s_pads[0].ctrl, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) ? 1 : 0;
            if (back && !s_backPrev) {
                g_dbgMode = (g_dbgMode + 1) % 9;   /* 1=magenta blocks 2=full texture 3=no tests 4=hide playfield grid (tpages 16/17) 5-8=viewer */
                {
                    extern int g_screenWidth, g_screenHeight, g_screenHeightFull, g_clipLeft, g_clipRight,
                               g_clipTop, g_clipBottom, g_projScaleXCurrent, g_projScaleY;
                    fprintf(stderr, "DEBUG MODE -> %d (frame %d) scr=%dx%d full_h=%d clip=%d,%d,%d,%d proj=%d,%d\n",
                            g_dbgMode, s_frames, g_screenWidth, g_screenHeight, g_screenHeightFull,
                            g_clipLeft, g_clipTop, g_clipRight, g_clipBottom, g_projScaleXCurrent, g_projScaleY);
                }
            }
            s_backPrev = back;
        }
        {
            /* R3 (right stick click) + Back: toggle bilinear texture filtering. */
            extern int g_xbBilinear;
            extern void R_InvalidateTextureFilters(void);
            static int s_bfPrev;
            int bf = (s_pads[0].ctrl &&
                      SDL_GameControllerGetButton(s_pads[0].ctrl, SDL_CONTROLLER_BUTTON_BACK) &&
                      SDL_GameControllerGetButton(s_pads[0].ctrl, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) ? 1 : 0;
            if (bf && !s_bfPrev) {
                g_xbBilinear = !g_xbBilinear;
                R_InvalidateTextureFilters();
                xb_logf("FILTER -> %s (frame %d)\n", g_xbBilinear ? "bilinear" : "nearest", s_frames);
            }
            s_bfPrev = bf;
        }
        {
            /* Y + Back: mark "I just saw a flash" in the log (frame + time). */
            static int s_mkPrev, s_mkN;
            int mk = (s_pads[0].ctrl &&
                      SDL_GameControllerGetButton(s_pads[0].ctrl, SDL_CONTROLLER_BUTTON_BACK) &&
                      SDL_GameControllerGetButton(s_pads[0].ctrl, SDL_CONTROLLER_BUTTON_Y)) ? 1 : 0;
            if (mk && !s_mkPrev && s_mkN < 200) {
                s_mkN++;
                xb_logf("FLASHMARK #%d at frame %d, %u ms\n", s_mkN, s_frames, (unsigned)SDL_GetTicks());
                {
                    int k;
                    xb_logf("  draws/frame, last 40 frames (oldest first):");
                    for (k = 40; k >= 1; k--) xb_logf(" %u", s_dhist[(s_frames - k) & 63]);
                    xb_logf("\n");
                }
            }
            s_mkPrev = mk;
        }
        {
            /* RB + Back: pause/resume the music (black-flash test: is it the audio?). */
            static int s_muPrev, s_muOff;
            int mu = (s_pads[0].ctrl &&
                      SDL_GameControllerGetButton(s_pads[0].ctrl, SDL_CONTROLLER_BUTTON_BACK) &&
                      SDL_GameControllerGetButton(s_pads[0].ctrl, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) ? 1 : 0;
            if (mu && !s_muPrev) {
                s_muOff = !s_muOff;
                { extern int xb_musicPaused; xb_musicPaused = s_muOff; }
                xb_logf("MUSIC %s (frame %d)\n", s_muOff ? "PAUSED" : "RESUMED", s_frames);
            }
            s_muPrev = mu;
        }
        }   /* g_xbDebug */
        {
            /* Log frames that took unusually long (stutter hunting). */
            static unsigned s_lastTick;
            unsigned now = (unsigned)SDL_GetTicks();
            if (s_lastTick && now - s_lastTick > 150 && s_frames > 30)
                xb_logf("slow frame %d: %u ms (mark: %s)\n", s_frames, now - s_lastTick,
                        (const char *)g_lastMark);
            s_lastTick = now;
        }
        {
            /* Hitch log: which phase of the frame was slow. */
            static unsigned s_hTick, s_hLogged;
            static int s_hUp;
            extern int g_dbgUploadCount;
            unsigned nowH = (unsigned)SDL_GetTicks();
            if (s_hTick && nowH - s_hTick > 70 && s_frames > 60 && s_hLogged < 60) {
                s_hLogged++;
                xb_logf("hitch f%d: %u ms (logic %u, render-hud %u, uploads %d, mark %s)\n",
                        s_frames, nowH - s_hTick, g_dbgLastLogic, g_dbgLastRender,
                        g_dbgUploadCount - s_hUp, (const char *)g_lastMark);
            }
            if (s_hTick && nowH - s_hTick > 400) xb_log_flush();
            s_hUp = g_dbgUploadCount;
            s_hTick = nowH;
        }
        {
            /* A frame with far fewer draws than usual is a likely "black flash". */
            extern int g_dbgDraws[53];
            static unsigned s_prevSum, s_avg, s_lastLow;
            static unsigned s_prevT[53], s_goodT[53];
            unsigned curT[53];
            unsigned sum = 0;
            for (int t = 0; t < 53; t++) { sum += (unsigned)g_dbgDraws[t]; curT[t] = (unsigned)g_dbgDraws[t] - s_prevT[t]; }
            if (s_frames % 600 != 1 && sum >= s_prevSum) {
                unsigned d = sum - s_prevSum;
                s_dhist[s_frames & 63] = d;
                if (0) {   /* LOWDRAW logging retired: the black flashes were lost flips */
                }
                else {
                    for (int t = 0; t < 53; t++) s_goodT[t] = curT[t];
                    xb_capture_state(s_xbStatePrev);
                    s_xbStatePrevOk = 1;
                }
                s_avg = s_avg ? (s_avg * 15 + d) / 16 : d;
            }
            for (int t = 0; t < 53; t++) s_prevT[t] = (unsigned)g_dbgDraws[t];
            s_prevSum = sum;
            (void)s_lastLow;
        }
        if (s_frames <= 3 || s_frames % 600 == 0)
            xb_logf("frame %d\n", s_frames);
        {
            /* Frame-time statistics, printed together once per 600 frames. */
            static unsigned s_t0, s_tPrev, s_n50, s_n80, s_n120, s_max, s_swapMs, s_cnt;
            static unsigned s_logMs;
            unsigned nowT = (unsigned)SDL_GetTicks();
            if (s_tPrev) {
                unsigned dt = nowT - s_tPrev;
                if (dt > 50) s_n50++;
                if (dt > 80) s_n80++;
                if (dt > 120) s_n120++;
                if (dt > s_max && !(s_frames % 600 == 1)) s_max = dt;
                s_cnt++;
            }
            s_tPrev = nowT;
            if (!s_t0) s_t0 = nowT;
            if (s_frames % 600 == 0) {
                unsigned span = nowT - s_t0;
                xb_logf("timing: %u frames in %u ms (avg %u.%u ms), >50ms:%u >80ms:%u >120ms:%u max:%u ms, swap-wait avg %u us\n",
                        s_cnt, span, s_cnt ? span / s_cnt : 0, s_cnt ? (span * 10 / s_cnt) % 10 : 0,
                        s_n50, s_n80, s_n120, s_max, s_cnt ? s_swapMs * 1000u / s_cnt : 0);
                {
                    extern int g_dbgUploadCount, g_dbgUploadMs, g_dbgUploadMaxMs, g_dbgBigQuads, g_dbgClipped, g_dbgWideW, g_dbgDraws[53];
                    unsigned nd = 0;
                    for (int t = 0; t < 53; t++) nd += (unsigned)g_dbgDraws[t];
                    xb_logf("timing2: logic %u ms, render %u ms (race frames, sum); uploads %d (%d ms, max %d ms); draws/frame %u; big-quads %d; wide-w %d; lost flips fixed early %d, late %d\n",
                            g_dbgLogicMs, g_dbgRenderMs, g_dbgUploadCount, g_dbgUploadMs, g_dbgUploadMaxMs,
                            nd / 600u, g_dbgBigQuads, g_dbgWideW, g_flipEarly, g_flipLate);
                    g_flipEarly = g_flipLate = 0;
                    g_dbgUploadCount = g_dbgUploadMs = g_dbgUploadMaxMs = g_dbgBigQuads = g_dbgClipped = g_dbgWideW = 0;
                    g_dbgLogicMs = g_dbgRenderMs = 0;
                    xb_logf("rain: active-slots/frame %d, drawn/frame %d, rejected/frame %d\n",
                            g_dbgRainActive / 600, g_dbgRainDrawn / 600, g_dbgRainRejected / 600);
                    g_dbgRainActive = g_dbgRainDrawn = g_dbgRainRejected = 0;
                }
                s_t0 = nowT; s_n50 = s_n80 = s_n120 = s_max = s_swapMs = s_cnt = 0;
                (void)s_logMs;
            }
            g_dbgSwapAcc = &s_swapMs;
        }
        if ((s_frames % 600 == 0 && g_dbgMode >= 5) || s_frames < 4) {
            extern int g_dbgDraws[53]; extern float g_dbgVert[53][6];
            for (int t = 0; t < 53; t++) if (g_dbgDraws[t]) {
                fprintf(stderr, "  draws tpage %d: %d  v0=(%d,%d,z*1e6=%d,rhw*1e6=%d,a%d) st=%d\n", t,
                        g_dbgDraws[t], (int)g_dbgVert[t][0], (int)g_dbgVert[t][1],
                        (int)(g_dbgVert[t][2] * 1e6f), (int)(g_dbgVert[t][3] * 1e6f),
                        (int)g_dbgVert[t][4], (int)g_dbgVert[t][5]);
                g_dbgDraws[t] = 0;
            }
        }
        if (s_frames % 600 == 0) {
            extern int g_dbgDraws[53];
            for (int t = 0; t < 53; t++) g_dbgDraws[t] = 0;
        }
        {
            /* Report the first few GL errors: they point at unsupported state. */
            static int s_glErrors;
            GLenum e;
            while (s_glErrors < 30 && (e = glGetError()) != GL_NO_ERROR) {
                fprintf(stderr, "GL error 0x%x (frame %d)\n", (unsigned)e, s_frames);
                s_glErrors++;
            }
        }
    }
    {
        /* Track how much push buffer a frame uses (dwords); log new maxima. */
        extern uint32_t *xb_pb_head;
        static uint32_t *s_pbStart;
        static unsigned s_pbMax;
        uint32_t *cur = pb_begin();
        pb_end(cur);
        if (s_pbStart && cur > s_pbStart) {
            unsigned used = (unsigned)(cur - s_pbStart) * 4u;
            if (used > s_pbMax + 65536u) {
                s_pbMax = used;
                fprintf(stderr, "pushbuffer: frame used %u KB\n", used / 1024u);
            }
        }
        {
            unsigned ts = (unsigned)SDL_GetTicks();
            /* pbkit triple buffering can lose a flip: if the frame-ready
             * interrupt and a VBlank are handled in the same DPC pass, the
             * VBlank handler marks the buffer as shown although the ISR never
             * pointed the display at it. The display then stays one buffer
             * behind, and the next frame starts clearing and drawing into the
             * buffer that is still on screen -> a black/blue area below the
             * beam for one refresh (the "black flashes"). Before drawing into
             * the new back buffer, make sure it is not the one being shown. */
            unsigned prevBack = (unsigned)(uintptr_t)pb_back_buffer() & 0x03FFFFFFu;
            s_fwArmed = 0;
            pbgl_swap_buffers();
            {
                static int s_fixLog;
                unsigned back = (unsigned)(uintptr_t)pb_back_buffer() & 0x03FFFFFFu;
                if ((XB_PCRTC_START & 0x03FFFFFFu) == back) {
                    /* Not caught early (short frame): wait for the pending flip. */
                    int n = 0, forced = 0;
                    while ((XB_PCRTC_START & 0x03FFFFFFu) == back && n < 3) {
                        pb_wait_for_vbl();
                        n++;
                    }
                    if ((XB_PCRTC_START & 0x03FFFFFFu) == back) {
                        XB_PCRTC_START = prevBack;   /* show the frame we just finished */
                        forced = 1;
                    }
                    g_flipLate++;
                    if (s_fixLog < 10) {
                        s_fixLog++;
                        xb_logf("FLIPFIX frame %d: back buffer was on screen (waited %d vbl%s)\n", (int)g_xbFrames, n,
                                forced ? ", forced flip" : "");
                    }
                }
                s_fwExpect = prevBack;
                s_fwVbl = (unsigned)pb_get_vbl_counter();
                s_fwArmed = 1;
            }
            if (g_dbgSwapAcc) *g_dbgSwapAcc += (unsigned)SDL_GetTicks() - ts;
        }
        s_pbStart = pb_begin();
        pb_end(s_pbStart);
        xb_pb_head = s_pbStart;
    }
    return;
#endif
    if (g_sdlWindow) {
        SDL_GL_SwapWindow(g_sdlWindow);
    }
}

void platform_get_drawable_size(int *w, int *h)
{
#ifdef SONICR_XBOX
    *w = s_fbWidth;
    *h = s_fbHeight;
    return;
#endif
    if (g_sdlWindow) {
        SDL_GL_GetDrawableSize(g_sdlWindow, w, h);
    }
    else {
        *w = 640;
        *h = 480;
    }
}

/* =====================================================================
 * Networking - no-ops on SDL (OS stack is always available)
 * ===================================================================== */

#ifdef SONICR_XBOX
/* Raw controller buttons for the network lobby, OR'd across all pads (the
 * lobby's actions are keyboard F-keys on PC; see NetSynthPadKeys). */
unsigned int platform_menu_buttons(void)
{
    unsigned int m = 0;
    SDL_GameControllerUpdate();
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        SDL_GameController *c = s_pads[i].ctrl;
        if (!c) continue;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A))          m |= MENUBTN_A;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B))          m |= MENUBTN_B;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X))          m |= MENUBTN_X;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y))          m |= MENUBTN_Y;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START))      m |= MENUBTN_START;
        if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > 16000) m |= MENUBTN_L;
        if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) m |= MENUBTN_R;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP))    m |= MENUBTN_UP;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  m |= MENUBTN_DOWN;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  m |= MENUBTN_LEFT;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) m |= MENUBTN_RIGHT;
    }
    return m;
}
#endif

int platform_net_init(void)
{
    return 0;
}

void platform_net_shutdown(void)
{
    /* */ ;
}

int platform_net_is_modem(void)
{
    return 0;
}

int platform_get_region(void)
{
    return 0;
}
