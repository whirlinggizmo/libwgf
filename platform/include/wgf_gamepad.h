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
 * until it is unplugged. Pads are read once a frame, before its ticks.
 *
 * Linux: evdev, the kernel's input devices, checked for new pads every couple of
 * seconds. Windows: XInput. Web: the browser's Gamepad API, which lists a pad only
 * once one of its buttons is pressed on the page. macOS: none yet. */

/* Buttons by where they are on an Xbox-style pad, not by what they're labeled. */
typedef enum wgf_gamepad_button_t {
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

/* Where a stick points, -1 to 1 on each axis, y down as the screen's is. Within the
 * dead zone it is (0, 0); past it, the length is rescaled to reach 1 at the edge,
 * so a stick moves smoothly out of the dead zone in every direction. */
WGF_API wgf_vec2_t wgf_gamepad_get_stick(int pad, wgf_gamepad_stick_t stick);

/* How far a trigger is pulled, 0 to 1. */
WGF_API float wgf_gamepad_get_trigger(int pad, wgf_gamepad_trigger_t trigger);

/* How far from the middle a stick counts as the middle, the same for every pad:
 * default 0.15, clamped to 0 to 0.9. */
WGF_API void wgf_gamepad_set_deadzone(float radius);
WGF_API float wgf_gamepad_get_deadzone(void);

#ifdef __cplusplus
}
#endif

#endif
