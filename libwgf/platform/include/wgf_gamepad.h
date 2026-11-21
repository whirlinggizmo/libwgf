#ifndef WGF_GAMEPAD_H
#define WGF_GAMEPAD_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_input.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Gamepads, read inside a tick or a frame: what changed since the previous tick, or
 * the previous frame. Up to 4 at once, pads 0 to 3: a pad keeps its number while it
 * is connected, and a new one takes the lowest free number, so pad 0 stays player one
 * until it is unplugged. Pads are read once a frame, before its ticks, from the first call
 * that reaches them (any here, a pad binding, the UI's): a program that never does links none
 * of the pads' code.
 *
 * Linux: evdev, the kernel's input devices, checked for new pads every couple of
 * seconds. Windows: XInput. Web: the browser's Gamepad API, which lists a pad only
 * once one of its buttons is pressed on the page. macOS: none yet. */

/* Buttons by where they are on an Xbox-style pad, not by what they're labeled. */
typedef enum wgf_gamepad_button_t {
    WGF_GAMEPAD_BUTTON_NONE = -1, /* none (wgf_gamepad_get_last_pressed) */
    WGF_GAMEPAD_BUTTON_SOUTH = 0, /* A on Xbox, cross on PlayStation, B on Switch */
    WGF_GAMEPAD_BUTTON_EAST = 1,
    WGF_GAMEPAD_BUTTON_WEST = 2,
    WGF_GAMEPAD_BUTTON_NORTH = 3,
    WGF_GAMEPAD_BUTTON_LEFT_BUMPER = 4,
    WGF_GAMEPAD_BUTTON_RIGHT_BUMPER = 5,
    WGF_GAMEPAD_BUTTON_LEFT_TRIGGER = 6, /* pulled past halfway */
    WGF_GAMEPAD_BUTTON_RIGHT_TRIGGER = 7,
    WGF_GAMEPAD_BUTTON_BACK = 8,   /* view, select, share, minus */
    WGF_GAMEPAD_BUTTON_START = 9,  /* menu, options, plus */
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

/* Every axis of a pad, one number each: the sticks' as get_stick gives them (-1 to 1, y
 * down, past the dead zone) and the triggers' (0 to 1). What input actions bind
 * (wgf_action.h), and the names an autopilot's `pad axis` lines use. */
typedef enum wgf_gamepad_axis_t {
    WGF_GAMEPAD_AXIS_LEFT_X = 0,
    WGF_GAMEPAD_AXIS_LEFT_Y = 1,
    WGF_GAMEPAD_AXIS_RIGHT_X = 2,
    WGF_GAMEPAD_AXIS_RIGHT_Y = 3,
    WGF_GAMEPAD_AXIS_LEFT_TRIGGER = 4,
    WGF_GAMEPAD_AXIS_RIGHT_TRIGGER = 5
} wgf_gamepad_axis_t;

/* Whether pad 0 to 3 is connected. */
WGF_API bool wgf_gamepad_is_connected(int pad);

/* What the platform calls the pad, such as "Microsoft X-Box 360 pad"; "" with no pad
 * there. */
WGF_API const char *wgf_gamepad_get_name(int pad);

/* Where `button` is: UP, PRESSED, DOWN, or RELEASED, as keys are. A pad unplugged
 * while a button is held releases it. UP for a pad or button that isn't one. */
WGF_API wgf_input_state_t wgf_gamepad_get_button_state(int pad, wgf_gamepad_button_t button);
WGF_API bool wgf_gamepad_is_down(int pad, wgf_gamepad_button_t button);
WGF_API bool wgf_gamepad_is_pressed(int pad, wgf_gamepad_button_t button);
WGF_API bool wgf_gamepad_is_released(int pad, wgf_gamepad_button_t button);
/* The button of `pad` pressed in this frame (in a tick, the tick), the lowest-numbered when
 * several were; WGF_GAMEPAD_BUTTON_NONE for none, or a pad out of range: a rebinding
 * screen's "press a button", beside wgf_keyboard_get_last_pressed. */
WGF_API wgf_gamepad_button_t wgf_gamepad_get_last_pressed(int pad);

/* Where a stick points, -1 to 1 on each axis, y down as the screen's is. Within the
 * dead zone it is (0, 0); past it, the length is rescaled to reach 1 at the edge,
 * so a stick moves smoothly out of the dead zone in every direction. */
WGF_API wgf_vec2_t wgf_gamepad_get_stick(int pad, wgf_gamepad_stick_t stick);

/* How far a trigger is pulled, 0 to 1. */
WGF_API float wgf_gamepad_get_trigger(int pad, wgf_gamepad_trigger_t trigger);

/* One axis, as get_stick and get_trigger give it; 0 for a pad or axis that isn't one. */
WGF_API float wgf_gamepad_get_axis(int pad, wgf_gamepad_axis_t axis);

/* How far from the middle a stick counts as the middle, the same for every pad:
 * default 0.15, clamped to 0 to 0.9. */
WGF_API void wgf_gamepad_set_deadzone(float radius);
WGF_API float wgf_gamepad_get_deadzone(void);

/* Rumble: a pad's two motors, the low-frequency one (the heavy, left one) and the
 * high-frequency one (the light, right one), each 0 to 1 (clamped), for `seconds`
 * (clamped to at most 2, raylib's limit), in place of whatever it was doing; stopped by
 * libwgf once the time is up, or by wgf_gamepad_stop_rumble. Linux: evdev's FF_RUMBLE,
 * where the pad's device could be opened for writing; Windows: XInput; web: the Gamepad
 * API's vibration actuator ("dual-rumble"), or a haptic actuator's pulse, where the
 * browser has one (Firefox has neither). False for a pad that isn't connected or can't
 * rumble (the first such refusal a run warns of), or `seconds` not above 0. */
WGF_API bool wgf_gamepad_rumble(int pad, float low, float high, float seconds);
/* Stops a pad's rumble. False where it isn't rumbling. */
WGF_API bool wgf_gamepad_stop_rumble(int pad);
/* Whether a pad is rumbling: from wgf_gamepad_rumble until its time is up, it is stopped,
 * or the pad is unplugged. */
WGF_API bool wgf_gamepad_is_rumbling(int pad);
/* Whether a pad is connected and has motors libwgf can reach: ask before offering a
 * rumble setting. */
WGF_API bool wgf_gamepad_can_rumble(int pad);

/* Mappings for pads libwgf doesn't know, as SDL's game controller database writes them
 * (raylib's SetGamepadMappings): one a line, `GUID,name,a:b0,b:b1,leftx:a0,...,platform:Linux,`,
 * its buttons, axes (`+a2`, `-a2`, `a2~`), and hats (`h0.4`) in SDL's numbering, its targets'
 * halves (`+leftx:b3`) too; a line for another platform, a comment (`#`), and a target libwgf
 * has no place for (misc1, the paddles, the touchpad) are left out, a line that isn't one is
 * skipped (warned with its number), and a GUID given again replaces its mapping. A pad whose
 * GUID a mapping has is read through it from the next frame, in place of libwgf's own reading:
 * so SDL's community database (gamecontrollerdb.txt, read with wgf_fs) maps the pads it knows.
 * The mappings kept, of this platform's. Applied on Linux, where pads are read raw; Windows'
 * pads are XInput's, and the web's the browser's standard mapping, already in libwgf's order,
 * so there a mapping is kept and drives none. */
WGF_API int wgf_gamepad_add_mappings(const char *text);
/* Pad `pad`'s GUID as SDL writes it, 32 hex digits, for a line of its own; "" for none, and
 * where the platform makes none (Windows, the web). */
WGF_API const char *wgf_gamepad_get_guid(int pad);
/* Whether a mapping drives pad `pad`. */
WGF_API bool wgf_gamepad_is_mapped(int pad);

#ifdef __cplusplus
}
#endif

#endif
