/**
 * platform.h — Platform abstraction interface
 *
 * Abstracts window creation, framebuffer display, input, timing, and audio.
 * SDL2 implementation: platform_sdl.c
 * A KOS/Dreamcast implementation would go in platform_dc.c.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

/* =====================================================================
 * Display
 * ===================================================================== */

/* Initialize the platform: window, GL context, audio.
 * Returns 0 on success, non-zero on failure (caller exits). */
int platform_init(int width, int height, int fullscreen, const char *title);

/* Shutdown: destroy window, free resources. */
void platform_shutdown(void);

/* Executable's own directory (desktop), or NULL where not applicable (DC/web).
 * Used to locate game data co-located with the binary. */
const char *platform_base_path(void);

/* =====================================================================
 * Input
 * ===================================================================== */

/* Poll input events. Updates the keystate array.
 * Returns 0 if the app should continue, 1 if quit was requested. */
int platform_poll_events(unsigned char *keystateOut, int keystateSize);

/* Key scan codes — mapped to DirectInput DIK_* values so the existing
 * key mapping table in PollAllInputDevices works unchanged.
 * See dinput.h for the full DIK_* list. Common ones.
 * On Windows we get these from <dinput.h> via sonicr_types.h. */
#ifndef _WIN32
#define DIK_ESCAPE      0x01
#define DIK_1           0x02
#define DIK_2           0x03
#define DIK_3           0x04
#define DIK_4           0x05
#define DIK_RETURN      0x1C
#define DIK_SPACE       0x39
#define DIK_UP          0xC8
#define DIK_DOWN        0xD0
#define DIK_LEFT        0xCB
#define DIK_RIGHT       0xCD
#define DIK_A           0x1E
#define DIK_B           0x30
#define DIK_C           0x2E
#define DIK_D           0x20
#define DIK_E           0x12
#define DIK_F           0x21
#define DIK_Q           0x10
#define DIK_R           0x13
#define DIK_S           0x1F
#define DIK_W           0x11
#define DIK_X           0x2D
#define DIK_Z           0x2C
#define DIK_LSHIFT      0x2A
#define DIK_RSHIFT      0x36
#define DIK_LCONTROL    0x1D
#define DIK_LALT        0x38
#define DIK_T           0x14
#define DIK_TAB         0x0F
#endif

/* =====================================================================
 * Gamepad
 * ===================================================================== */

/* Initialize gamepad subsystem. Call once at startup.
 * Kicks off wireless controller discovery and returns initial count. */
int platform_init_gamepads(void);

/* Poll all connected gamepads and write button state into joySlotState[].
 * Each slot is a 16-bit bitmask matching the Sonic R button layout.
 * Unused slots are zeroed. Returns number of gamepads found. */
int platform_poll_gamepads(unsigned short *joySlotState, int maxSlots);

/* =====================================================================
 * Timing
 * ===================================================================== */

/* Returns milliseconds since program start. Replaces timeGetTime(). */
uint32_t platform_get_time_ms(void);

/* Sleep for the given number of milliseconds. */
void platform_sleep_ms(int ms);

/* =====================================================================
 * Audio (stub for now)
 * ===================================================================== */

/* Initialize audio system. Returns 0 on success. */
int platform_audio_init(void);

/* Shutdown audio. */
void platform_audio_shutdown(void);

/* =====================================================================
 * GL helpers (SDL platform layer)
 * ===================================================================== */

/* Swap the GL double buffer (calls SDL_GL_SwapWindow). */
void platform_gl_swap(void);

/* Get the GL drawable size in pixels (for Retina/HiDPI). */
void platform_get_drawable_size(int *w, int *h);

/* Pump the platform event loop (keyboard/gamepad/window events).
 * Call from any frame loop so the window stays responsive. */
void platform_pump_events(void);

/* =====================================================================
 * Networking
 * ===================================================================== */

/* Bring up the platform network stack. On DC this defers BBA/modem
 * detection from boot to first use (avoids startup delay). On SDL
 * this is a no-op since the OS network stack is always available.
 * Safe to call multiple times — only the first call does work.
 * Returns 0 on success, -1 if no network device could be initialized. */
int platform_net_init(void);

/* Tear down the platform network stack. */
void platform_net_shutdown(void);

/* Returns 1 if the active network link is a modem (PPP), 0 for broadband. */
int platform_net_is_modem(void);

/* Returns the console region (DC: 1=JP, 2=US, 3=EU from flashrom; other: 0). */
int platform_get_region(void);

#if defined(SONICR_DC) || defined(SONICR_XBOX)
/* Raw controller buttons for the DC menu screens the keyboard-era UI never
 * mapped to a pad (the network lobby: F1/F2/F3/F6/F7/F8). Physical buttons,
 * bypassing the gameplay remap, OR'd across all ports. Bit set = held. */
#define MENUBTN_A      0x001u
#define MENUBTN_B      0x002u
#define MENUBTN_X      0x004u
#define MENUBTN_Y      0x008u
#define MENUBTN_START  0x010u
#define MENUBTN_L      0x020u
#define MENUBTN_R      0x040u
#define MENUBTN_UP     0x080u
#define MENUBTN_DOWN   0x100u
#define MENUBTN_LEFT   0x200u
#define MENUBTN_RIGHT  0x400u
unsigned int platform_menu_buttons(void);
#endif

#endif /* PLATFORM_H */
