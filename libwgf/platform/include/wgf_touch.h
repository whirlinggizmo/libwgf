#ifndef WGF_TOUCH_H
#define WGF_TOUCH_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_input.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fingers on a touch screen, read inside a tick or a frame.
 *
 * A read reports what changed since the previous tick, or the previous frame. List the fingers
 * by index, then read each by its id, which stays the same while the finger is down:
 *
 *   for (i = 0; i < wgf_touch_get_count(); i++) {
 *       id = wgf_touch_get_id(i);
 *       if (wgf_touch_get_state(id) == WGF_INPUT_STATE_PRESSED) ...
 *   }
 *
 * At most 8 fingers are read at once, and more are ignored. Positions are in logical pixels,
 * as the mouse's are. */

/* Returns the number of fingers down, plus those lifted since the previous tick or frame. */
WGF_API int wgf_touch_get_count(void);

/* Returns the id of the finger at `index` (0 up to the count), oldest first.
 *
 * Ids are 0 to 7. An id stays the same while its finger is down, and is reused once the finger
 * has lifted. Returns -1 past the count. */
WGF_API int wgf_touch_get_id(int index);

/* Returns the state of finger `id`: PRESSED (touched down since the previous tick or frame),
 * DOWN, or RELEASED.
 *
 * A tap within one read is PRESSED. Returns UP for an id with no finger. */
WGF_API wgf_input_state_t wgf_touch_get_state(int id);

/* Returns where the finger is, or where it lifted. Returns (0, 0) for an id with no finger. */
WGF_API wgf_vec2_t wgf_touch_get_position(int id);

/* Returns how far the finger moved since the previous tick or frame. */
WGF_API wgf_vec2_t wgf_touch_get_delta(int id);

/* Sets or gets whether the first finger also drives the mouse, so mouse code works by touch
 * (default: on).
 *
 * The finger moves the pointer and holds the left button. A second finger cancels that press:
 * the pointer lets go off the window, at (-1, -1), so nothing under it is clicked. The mouse
 * then stays up until every finger has lifted. */
WGF_API void wgf_touch_set_mouse_emulated(bool emulated);
WGF_API bool wgf_touch_is_mouse_emulated(void);
/* Sets or gets whether the mouse's left button acts as a finger, so touch controls can be tried
 * with a mouse on a desktop (default: off).
 *
 * - While the button is pressed, a finger touches down where the pointer is, follows it, and
 *   lifts with the button.
 * - That finger has its own id, as any finger does.
 * - The two settings don't feed each other: the finger a press makes doesn't drive the mouse
 *   back.
 * - Turned off while the button is pressed, the finger lifts. */
WGF_API void wgf_touch_set_from_mouse(bool from_mouse);
WGF_API bool wgf_touch_is_from_mouse(void);

/* Two-finger gestures: how the first two fingers down panned, pinched, and twisted since the
 * previous tick, or the previous frame.
 *
 * Use them for zooming, panning, and turning a view by touch. */

/* Returns whether two or more fingers are down. */
WGF_API bool wgf_touch_is_gesture(void);

/* Returns the point between the two fingers, in logical pixels. Returns (0, 0) with fewer than
 * two. */
WGF_API wgf_vec2_t wgf_touch_get_gesture_center(void);

/* Returns how far that point moved since the previous tick or frame. */
WGF_API wgf_vec2_t wgf_touch_get_gesture_pan(void);

/* Returns the pinch since the previous tick or frame: how many times farther apart the fingers
 * are. 1 is no pinch, 2 twice as far apart, and 0.5 half as far. */
WGF_API float wgf_touch_get_gesture_scale(void);

/* Returns the twist since the previous tick or frame, in radians, clockwise on the
 * screen. */
WGF_API float wgf_touch_get_gesture_rotation(void);

/* Returns one finger's gestures. Each is made by a finger that was alone while it was down.
 *
 * - A tap: lifted within 0.3 seconds, having moved under 12 logical pixels. is_tapped says
 *   whether one happened in this frame (in a tick, in that tick).
 * - A double tap: a second tap within 0.35 seconds of the last, near it. is_double_tapped
 *   says whether one happened.
 * - get_tap_position returns where the tap or double tap was.
 * - A swipe: lifted within 0.5 seconds, having gone 50 logical pixels or more. get_swipe
 *   returns how far it went, or 0, 0 for none.
 * - A hold: down for 0.5 seconds or more, still. is_holding is true while it lasts. */
WGF_API bool wgf_touch_is_tapped(void);
WGF_API bool wgf_touch_is_double_tapped(void);
WGF_API wgf_vec2_t wgf_touch_get_tap_position(void);
WGF_API wgf_vec2_t wgf_touch_get_swipe(void);
WGF_API bool wgf_touch_is_holding(void);

#ifdef __cplusplus
}
#endif

#endif
