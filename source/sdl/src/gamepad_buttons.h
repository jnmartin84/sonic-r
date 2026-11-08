/**
 * gamepad_buttons.h — Button INDICES of the SDL_GameController layout.
 *
 * Distinct from pad_bits.h, and the two are easy to confuse: pad_bits.h names
 * the BITS of the 16-bit gameplay input word, this names the button-index space
 * those bits are looked up BY. The pairing is
 *
 *     pad bits = g_joystickConfigWords[button index]
 *
 * or, for indices below JOY_SLOT_CFG_WORDS, the per-slot copy at
 * g_joystickSlots[slot][0x104 + index * 2] that the remap UI writes.
 *
 * Values are the SDL_CONTROLLER_BUTTON_* ordinals, restated here as plain
 * integers so init.c can lay out the defaults without pulling in SDL.h.
 * platform_sdl.c static_asserts them against the real enum, so if SDL ever
 * renumbers, the build breaks instead of the mapping silently shifting.
 */

#ifndef GAMEPAD_BUTTONS_H
#define GAMEPAD_BUTTONS_H

#define GCBTN_A              0
#define GCBTN_B              1
#define GCBTN_X              2
#define GCBTN_Y              3
#define GCBTN_BACK           4
#define GCBTN_GUIDE          5
#define GCBTN_START          6
#define GCBTN_LEFTSTICK      7      /* stick CLICK, not deflection */
#define GCBTN_RIGHTSTICK     8
#define GCBTN_LEFTSHOULDER   9
#define GCBTN_RIGHTSHOULDER  10
#define GCBTN_DPAD_UP        11
#define GCBTN_DPAD_DOWN      12
#define GCBTN_DPAD_LEFT      13
#define GCBTN_DPAD_RIGHT     14

/* SDL's own enum continues past the d-pad with MISC1, four paddles and a
 * touchpad click. This port does not expose those — nothing in Sonic R wants a
 * paddle — so index 15 onward is ours to define, and the two rear triggers live
 * there. SDL reports triggers as analog AXES with no button index of their own;
 * the poller thresholds them into these indices so they carry ordinary bindings
 * like any button. Ending the borrowed range at the d-pad also keeps these
 * indices stable across SDL versions, which kept adding to the tail. */
#define GCBTN_TRIGGER_LEFT   15
#define GCBTN_TRIGGER_RIGHT  16
#define GC_BUTTON_COUNT      17

/* Per-slot binding capacity. g_joystickSlots[slot] is 0x11A bytes: device name
 * through 0x103, then TEN binding shorts at 0x104..0x117, then the button count
 * at 0x118. Ten is therefore a hard ceiling on per-pad bindings — indices at or
 * above it fall back to the shared g_joystickConfigWords[] and so are identical
 * for all four players. Raising it would change the on-disk JOYSTICK.INF layout
 * and the Dreamcast's VMU SONICR_PAD blob with it.
 *
 * It doubles as the remap UI's scan bound (published via
 * g_joystickDeviceFlags[] → slot[0x118] → ScanKeyRemap): a button the commit
 * cannot store must not be capturable, or it consumes one of the six action
 * prompts and binds nothing. Keeping the bound at ten also keeps ScanKeyRemap's
 * writes inside its 16-entry g_optKeyRemapResult / s_remapMappedFlag arrays,
 * which GC_BUTTON_COUNT on its own would overrun. */
#define JOY_SLOT_CFG_WORDS   10

#ifdef SONICR_XBOX
/* Xbox: buttons JOY_SLOT_CFG_WORDS..GC_BUTTON_COUNT-1 (Black, L/R triggers)
 * are remappable too; their bindings are kept in g_joyExtCfg (input.c). */
#define XB_EXT_BUTTONS       GC_BUTTON_COUNT
extern short g_joyExtCfg[4][XB_EXT_BUTTONS];
extern unsigned char g_joyExtValid[4];
#endif

#endif /* GAMEPAD_BUTTONS_H */
