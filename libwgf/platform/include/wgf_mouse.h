#ifndef WGF_MOUSE_H
#define WGF_MOUSE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_input.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The mouse, read inside a tick or a frame.
 *
 * A read reports what changed since the previous tick, or the previous frame. Positions are in
 * logical pixels from the window's top-left, with y down, as gfx draws. */

typedef enum wgf_mouse_button_t {
    WGF_MOUSE_BUTTON_LEFT = 0,
    WGF_MOUSE_BUTTON_RIGHT = 1,
    WGF_MOUSE_BUTTON_MIDDLE = 2
} wgf_mouse_button_t;

/* Returns where the pointer is: where it last moved over the window. */
WGF_API wgf_vec2_t wgf_mouse_get_position(void);

/* Returns whether the pointer is over the window.
 *
 * - A mouse is over the window from when it enters or moves over it until it leaves.
 * - A finger driving the pointer (wgf_touch_set_mouse_emulated) is over it while it is down.
 *
 * Returns false until the pointer has been seen. */
WGF_API bool wgf_mouse_is_over_window(void);

/* Returns how far the pointer moved since the previous tick or frame.
 *
 * While the mouse is locked, this is the only way to read its movement. */
WGF_API wgf_vec2_t wgf_mouse_get_delta(void);

/* Returns the scrolling since the previous tick or frame.
 *
 * y is vertical (up is positive) and x horizontal. A wheel notch is about 1, and trackpads give
 * fractions. */
WGF_API wgf_vec2_t wgf_mouse_get_wheel(void);

/* Returns the state of `button`: UP, PRESSED, DOWN, or RELEASED, as for keys. */
WGF_API wgf_input_state_t wgf_mouse_get_button_state(wgf_mouse_button_t button);
WGF_API bool wgf_mouse_is_down(wgf_mouse_button_t button);
WGF_API bool wgf_mouse_is_pressed(wgf_mouse_button_t button);
WGF_API bool wgf_mouse_is_released(wgf_mouse_button_t button);

/* Sets or gets whether the pointer is locked to the window and hidden, for mouse look
 * (default: off).
 *
 * While locked, the position stays put and the delta keeps coming. On the web the browser
 * grants the lock only during a click or key press, and the user can end it (Escape). */
WGF_API void wgf_mouse_set_locked(bool locked);
WGF_API bool wgf_mouse_is_locked(void);

/* Sets or gets whether the pointer shows over the window (default: true). */
WGF_API void wgf_mouse_set_cursor_visible(bool visible);
WGF_API bool wgf_mouse_is_cursor_visible(void);

/* The cursor's shape over the window, set and read with wgf_mouse_set_cursor and
 * wgf_mouse_get_cursor.
 *
 * DEFAULT is the system's cursor. The others include a hand over a button, an I-beam over
 * text, and the resizing arrows. Setting it does nothing on a phone. wgf_mouse_set_cursor
 * returns false for a value that isn't a cursor. */
typedef enum wgf_mouse_cursor_t {
    WGF_MOUSE_CURSOR_DEFAULT = 0,
    WGF_MOUSE_CURSOR_ARROW = 1,
    WGF_MOUSE_CURSOR_IBEAM = 2,
    WGF_MOUSE_CURSOR_CROSSHAIR = 3,
    WGF_MOUSE_CURSOR_HAND = 4,
    WGF_MOUSE_CURSOR_RESIZE_EW = 5,
    WGF_MOUSE_CURSOR_RESIZE_NS = 6,
    WGF_MOUSE_CURSOR_RESIZE_NWSE = 7,
    WGF_MOUSE_CURSOR_RESIZE_NESW = 8,
    WGF_MOUSE_CURSOR_RESIZE_ALL = 9,
    WGF_MOUSE_CURSOR_NOT_ALLOWED = 10
} wgf_mouse_cursor_t;
WGF_API bool wgf_mouse_set_cursor(wgf_mouse_cursor_t cursor);
WGF_API wgf_mouse_cursor_t wgf_mouse_get_cursor(void);

#ifdef __cplusplus
}
#endif

#endif
