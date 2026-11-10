/**
 * gamepad_buttons.h — Button INDICES of the SDL_GameController layout, and the
 * per-pad binding storage those indices select.
 *
 * Distinct from pad_bits.h, and the two are easy to confuse: pad_bits.h names
 * the BITS of the 16-bit gameplay input word, this names the button-index space
 * those bits are looked up BY. Each pad has one binding per button index:
 *
 *     index <  JOY_SLOT_CFG_WORDS  g_joystickSlots[slot][0x104 + index * 2]
 *     index >= JOY_SLOT_CFG_WORDS  g_joystickSlotCfgExt[slot][index - JOY_SLOT_CFG_WORDS]
 *
 * The remap UI writes both; g_joystickConfigWords[index] holds the defaults
 * they are seeded from.
 *
 * Values 0..14 are the SDL_CONTROLLER_BUTTON_* ordinals, restated here as plain
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

/* From 15 on the indices are ours, fixed, and translated to SDL explicitly in
 * platform_sdl.c. SDL's own enum carries on past the d-pad too, but has grown
 * across releases, and saved bindings must not shift with the SDL version.
 *
 * SDL reports the triggers as analog AXES with no button index of their own;
 * the poller thresholds them into these indices so they bind like any button.
 * The rest are SDL buttons that only some pads have. */
#define GCBTN_TRIGGER_LEFT   15
#define GCBTN_TRIGGER_RIGHT  16
#define GCBTN_MISC1          17     /* Switch capture, Xbox Series share, PS5 mic */
#define GCBTN_PADDLE1        18     /* Xbox Elite back paddles */
#define GCBTN_PADDLE2        19
#define GCBTN_PADDLE3        20
#define GCBTN_PADDLE4        21
#define GCBTN_TOUCHPAD       22     /* PS4 / PS5 touchpad click */
#define GC_BUTTON_COUNT      23

/* Buttons per pad. This is the binary's stride for g_keyPressState (0x675B0C,
 * 80 bytes per slot — PollAllInputDevices 0x476BB1), so it is also the most a
 * raw device may report and the remap UI may scan. */
#define JOY_MAX_BUTTONS      80

/* Bindings held in the original slot. g_joystickSlots[slot] is 0x11A bytes, as
 * in the binary: device name through 0x103, TEN binding shorts at
 * 0x104..0x117, then the button count at 0x118. That block is what
 * JOYSTICK.INF has always stored, and the Dreamcast's VMU SONICR_PAD with it. */
#define JOY_SLOT_CFG_WORDS   10

/* Bindings for indices JOY_SLOT_CFG_WORDS and up, which the original slot has
 * no room for. A port addition: g_joystickSlotCfgExt[4][JOY_SLOT_EXT_WORDS].
 * On SDL builds it is saved after the four original slots in JOYSTICK.INF,
 * behind JOY_EXT_TAG, so a file without it (or the original game reading one
 * with it) still sees exactly the 4 x 0x11A bytes it expects. The Dreamcast
 * pad has 7 buttons, all inside the original slot, so its VMU save is
 * unchanged. */
#define JOY_SLOT_EXT_WORDS   (JOY_MAX_BUTTONS - JOY_SLOT_CFG_WORDS)
#define JOY_EXT_TAG          "SRX1"

#endif /* GAMEPAD_BUTTONS_H */
