#ifndef WGF_MOUSE_H
#define WGF_MOUSE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_input.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The mouse, read inside a tick or a frame: what changed since the previous tick,
 * or the previous frame. Positions are logical pixels from the window's top-left,
 * y down, as gfx draws. */

typedef enum wgf_mouse_button_t {
    WGF_MOUSE_BUTTON_LEFT = 0,
    WGF_MOUSE_BUTTON_RIGHT = 1,
    WGF_MOUSE_BUTTON_MIDDLE = 2
} wgf_mouse_button_t;

/* Where the pointer is: where it last moved over the window. */
WGF_API wgf_vec2_t wgf_mouse_get_position(void);

/* How far it moved since the previous tick or frame. While the mouse is locked,
 * the only way to read its movement. */
WGF_API wgf_vec2_t wgf_mouse_get_delta(void);

/* Scrolling since the previous tick or frame: y vertical (up is positive), x
 * horizontal; about 1 a wheel notch, fractional on trackpads. */
WGF_API wgf_vec2_t wgf_mouse_get_wheel(void);

/* Where `button` is: UP, PRESSED, DOWN, or RELEASED, as keys are. */
WGF_API wgf_input_state_t wgf_mouse_get_button_state(wgf_mouse_button_t button);
WGF_API bool wgf_mouse_is_down(wgf_mouse_button_t button);
WGF_API bool wgf_mouse_is_pressed(wgf_mouse_button_t button);
WGF_API bool wgf_mouse_is_released(wgf_mouse_button_t button);

/* Lock the pointer to the window and hide it, for mouse look: the position stays
 * put and the delta keeps coming (default off). On the web the browser grants it
 * only during a click or key press, and the user can end it (Escape). */
WGF_API void wgf_mouse_set_locked(bool locked);
WGF_API bool wgf_mouse_is_locked(void);

/* Show the pointer over the window (default true). */
WGF_API void wgf_mouse_set_cursor_visible(bool visible);
WGF_API bool wgf_mouse_is_cursor_visible(void);

#ifdef __cplusplus
}
#endif

#endif
