#ifndef WGF_GAMEPAD_H
#define WGF_GAMEPAD_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_input.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Gamepads, read inside a tick or a frame.
 *
 * A read reports what changed since the previous tick, or the previous frame.
 *
 * - Up to 4 pads at once, numbered 0 to 3.
 * - A pad keeps its number while it is connected. A new pad takes the lowest free number, so
 *   pad 0 stays player one until it is unplugged.
 * - Pads are read once a frame, before its ticks. Reading starts with the first call that
 *   reaches the pads: any call here, a pad binding, or the UI's. A program that never makes
 *   one links none of the pads' code.
 *
 * Each platform reads pads its own way:
 * - Linux: evdev, the kernel's input devices, checked for new pads every couple of seconds.
 * - Windows: XInput.
 * - Web: the browser's Gamepad API, which lists a pad only once one of its buttons is
 *   pressed on the page.
 * - macOS: no pads yet. */

/* A pad's buttons, named by where they are on an Xbox-style pad, not by their labels. */
typedef enum wgf_gamepad_button_t {
    WGF_GAMEPAD_BUTTON_NONE = -1, /* no button (see wgf_gamepad_get_last_pressed) */
    WGF_GAMEPAD_BUTTON_SOUTH = 0, /* A on Xbox, cross on PlayStation, B on Switch */
    WGF_GAMEPAD_BUTTON_EAST = 1,
    WGF_GAMEPAD_BUTTON_WEST = 2,
    WGF_GAMEPAD_BUTTON_NORTH = 3,
    WGF_GAMEPAD_BUTTON_LEFT_BUMPER = 4,
    WGF_GAMEPAD_BUTTON_RIGHT_BUMPER = 5,
    WGF_GAMEPAD_BUTTON_LEFT_TRIGGER = 6, /* down when pulled past halfway */
    WGF_GAMEPAD_BUTTON_RIGHT_TRIGGER = 7,
    WGF_GAMEPAD_BUTTON_BACK = 8,   /* labeled view, select, share, or minus */
    WGF_GAMEPAD_BUTTON_START = 9,  /* labeled menu, options, or plus */
    WGF_GAMEPAD_BUTTON_GUIDE = 10, /* the logo button */
    WGF_GAMEPAD_BUTTON_LEFT_STICK = 11,
    WGF_GAMEPAD_BUTTON_RIGHT_STICK = 12,
    WGF_GAMEPAD_BUTTON_DPAD_UP = 13,
    WGF_GAMEPAD_BUTTON_DPAD_DOWN = 14,
    WGF_GAMEPAD_BUTTON_DPAD_LEFT = 15,
    WGF_GAMEPAD_BUTTON_DPAD_RIGHT = 16
} wgf_gamepad_button_t;

typedef enum wgf_gamepad_stick_t {
    WGF_GAMEPAD_STICK_LEFT = 0,
    WGF_GAMEPAD_STICK_RIGHT = 1
} wgf_gamepad_stick_t;

typedef enum wgf_gamepad_trigger_t {
    WGF_GAMEPAD_TRIGGER_LEFT = 0,
    WGF_GAMEPAD_TRIGGER_RIGHT = 1
} wgf_gamepad_trigger_t;

/* Every axis of a pad, each read as one number.
 *
 * - A stick's axes read as wgf_gamepad_get_stick gives them: -1 to 1, y down, past the dead
 *   zone.
 * - A trigger reads 0 to 1.
 *
 * Input actions bind these (wgf_action.h), and an autopilot's `pad axis` lines use their
 * names. */
typedef enum wgf_gamepad_axis_t {
    WGF_GAMEPAD_AXIS_LEFT_X = 0,
    WGF_GAMEPAD_AXIS_LEFT_Y = 1,
    WGF_GAMEPAD_AXIS_RIGHT_X = 2,
    WGF_GAMEPAD_AXIS_RIGHT_Y = 3,
    WGF_GAMEPAD_AXIS_LEFT_TRIGGER = 4,
    WGF_GAMEPAD_AXIS_RIGHT_TRIGGER = 5
} wgf_gamepad_axis_t;

/* Returns whether pad `pad` (0 to 3) is connected. */
WGF_API bool wgf_gamepad_is_connected(int pad);

/* Returns the platform's name for the pad, such as "Microsoft X-Box 360 pad".
 *
 * Returns "" if no pad is there. */
WGF_API const char *wgf_gamepad_get_name(int pad);

/* Returns the state of `button`: UP, PRESSED, DOWN, or RELEASED, as for keys.
 *
 * A pad unplugged while a button is held releases that button. Returns UP for a pad or button
 * that isn't one. */
WGF_API wgf_input_state_t wgf_gamepad_get_button_state(int pad, wgf_gamepad_button_t button);
WGF_API bool wgf_gamepad_is_down(int pad, wgf_gamepad_button_t button);
WGF_API bool wgf_gamepad_is_pressed(int pad, wgf_gamepad_button_t button);
WGF_API bool wgf_gamepad_is_released(int pad, wgf_gamepad_button_t button);
/* Returns the button of `pad` pressed in this frame (in a tick, in that tick).
 *
 * When several were pressed, returns the lowest-numbered. Returns WGF_GAMEPAD_BUTTON_NONE if
 * none was, or if `pad` is out of range. A rebinding screen's "press a button" uses it, beside
 * wgf_keyboard_get_last_pressed. */
WGF_API wgf_gamepad_button_t wgf_gamepad_get_last_pressed(int pad);

/* Returns where a stick points, -1 to 1 on each axis, with y down as on the screen.
 *
 * Within the dead zone it is (0, 0). Past it, the length is rescaled to reach 1 at the edge, so
 * a stick moves smoothly out of the dead zone in every direction. */
WGF_API wgf_vec2_t wgf_gamepad_get_stick(int pad, wgf_gamepad_stick_t stick);

/* Returns how far a trigger is pulled, 0 to 1. */
WGF_API float wgf_gamepad_get_trigger(int pad, wgf_gamepad_trigger_t trigger);

/* Returns one axis, as get_stick and get_trigger give it. Returns 0 for a pad or axis that
 * isn't one. */
WGF_API float wgf_gamepad_get_axis(int pad, wgf_gamepad_axis_t axis);

/* Sets or gets the dead zone: how far from the middle a stick still counts as the middle.
 *
 * It is the same for every pad, clamped to 0 to 0.9 (default: 0.15). */
WGF_API void wgf_gamepad_set_deadzone(float radius);
WGF_API float wgf_gamepad_get_deadzone(void);

/* Rumbles a pad's two motors for `seconds`, in place of whatever rumble it was doing.
 *
 * - `low` drives the low-frequency motor (the heavy, left one), 0 to 1 (clamped).
 * - `high` drives the high-frequency motor (the light, right one), 0 to 1 (clamped).
 * - `seconds` is clamped to at most 2.
 * - libwgf stops the rumble once the time is up. wgf_gamepad_stop_rumble stops it sooner.
 *
 * Each platform rumbles its own way:
 * - Linux: evdev's FF_RUMBLE, where the pad's device could be opened for writing.
 * - Windows: XInput.
 * - Web: the Gamepad API's vibration actuator ("dual-rumble"), or a haptic actuator's pulse,
 *   where the browser has one. Firefox has neither.
 *
 * Returns false for a pad that isn't connected or can't rumble, or if `seconds` isn't above 0.
 * The first such refusal in a run is warned about. */
WGF_API bool wgf_gamepad_rumble(int pad, float low, float high, float seconds);
/* Stops a pad's rumble. Returns false if it isn't rumbling. */
WGF_API bool wgf_gamepad_stop_rumble(int pad);
/* Returns whether a pad is rumbling.
 *
 * It is rumbling from wgf_gamepad_rumble until its time is up, it is stopped, or the pad is
 * unplugged. */
WGF_API bool wgf_gamepad_is_rumbling(int pad);
/* Returns whether a pad is connected and has motors libwgf can reach. Ask before offering a
 * rumble setting. */
WGF_API bool wgf_gamepad_can_rumble(int pad);

/* Adds mappings for pads libwgf doesn't know, in the format of SDL's game controller database.
 *
 * `text` holds one mapping a line, such as `GUID,name,a:b0,b:b1,leftx:a0,...,platform:Linux,`.
 * - Buttons, axes (`+a2`, `-a2`, `a2~`), and hats (`h0.4`) use SDL's numbering. A target's
 *   halves (`+leftx:b3`) are read too.
 * - A line for another platform and a comment (`#`) are left out.
 * - A target libwgf has no place for (misc1, the paddles, the touchpad) is left out.
 * - A line that isn't a mapping is skipped, with a warning giving its line number.
 * - A GUID given again replaces its mapping.
 *
 * From the next frame, a pad whose GUID has a mapping is read through it, in place of libwgf's
 * own reading. So SDL's community database (gamecontrollerdb.txt, read with wgf_fs) maps the
 * pads it knows.
 *
 * Returns how many of this call's mappings were kept. Only this platform's mappings are kept.
 *
 * Mappings apply on Linux, where pads are read raw. Windows' pads are XInput's, and the web's
 * use the browser's standard mapping, both already in libwgf's order. There a mapping is kept
 * but drives no pad. */
WGF_API int wgf_gamepad_add_mappings(const char *text);
/* Returns pad `pad`'s GUID as SDL writes it, 32 hex digits, for writing a mapping of its own.
 *
 * Returns "" if there is no pad, and where the platform makes no GUID (Windows, the web). */
WGF_API const char *wgf_gamepad_get_guid(int pad);
/* Returns whether a mapping drives pad `pad`. */
WGF_API bool wgf_gamepad_is_mapped(int pad);

#ifdef __cplusplus
}
#endif

#endif
