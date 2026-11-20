#ifndef WGF_TOUCH_H
#define WGF_TOUCH_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_input.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fingers on a touch screen, read inside a tick or a frame: what changed since the
 * previous tick, or the previous frame. List them by index, then read each by its
 * id, which stays the same while the finger is down:
 *
 *   for (i = 0; i < wgf_touch_get_count(); i++) {
 *       id = wgf_touch_get_id(i);
 *       if (wgf_touch_get_state(id) == WGF_INPUT_STATE_PRESSED) ...
 *   }
 *
 * At most 8 fingers at once; more are ignored. Positions are logical pixels, as the
 * mouse's are. */

/* Fingers down, plus those lifted since the previous tick or frame. */
WGF_API int wgf_touch_get_count(void);

/* The id of the finger at `index` (0 up to the count), oldest first; -1 past the
 * count. Ids are 0 to 7: one stays the same while its finger is down, and is
 * reused once it has lifted. */
WGF_API int wgf_touch_get_id(int index);

/* PRESSED (touched down since the previous tick or frame), DOWN, or RELEASED; UP
 * for an id with no finger. A tap within one read is PRESSED. */
WGF_API wgf_input_state_t wgf_touch_get_state(int id);

/* Where the finger is, or where it lifted; (0, 0) for an id with no finger. */
WGF_API wgf_vec2_t wgf_touch_get_position(int id);

/* How far it moved since the previous tick or frame. */
WGF_API wgf_vec2_t wgf_touch_get_delta(int id);

/* Whether the first finger also drives the mouse (default on), so mouse code works
 * by touch: it moves the pointer and holds the left button. A second finger cancels
 * that press -- the pointer lets go off the window, at (-1, -1), so nothing under it
 * is clicked -- and the mouse stays up until every finger has lifted. */
WGF_API void wgf_touch_set_mouse_emulated(bool emulated);
WGF_API bool wgf_touch_is_mouse_emulated(void);
/* Whether the mouse's left button is a finger (default off): pressed, a finger touches down
 * where the pointer is, follows it, and lifts with the button, so touch controls are tried
 * with a mouse (raylib's touch point 0 on a desktop). Its own id, as any finger's. The two
 * don't answer each other: the finger a press makes doesn't drive the mouse back. Turned
 * off while pressed, the finger lifts. */
WGF_API void wgf_touch_set_from_mouse(bool from_mouse);
WGF_API bool wgf_touch_is_from_mouse(void);

/* Two fingers, the first two down: how they panned, pinched, and twisted since the
 * previous tick, or the previous frame, for zooming, panning, and turning a view by
 * touch. */

/* Two or more fingers are down. */
WGF_API bool wgf_touch_is_gesture(void);

/* The point between the two, in logical pixels; (0, 0) with fewer than two. */
WGF_API wgf_vec2_t wgf_touch_get_gesture_center(void);

/* How far that point moved since the previous tick or frame. */
WGF_API wgf_vec2_t wgf_touch_get_gesture_pan(void);

/* The pinch since the previous tick or frame: how many times farther apart they
 * are. 1 is none, 2 twice as far, 0.5 half. */
WGF_API float wgf_touch_get_gesture_scale(void);

/* The twist since the previous tick or frame, in radians, clockwise on the
 * screen. */
WGF_API float wgf_touch_get_gesture_rotation(void);

/* One finger's gestures, each a finger that was alone while it was down: a tap (lifted
 * within 0.3 s, having moved under 12 logical pixels) in this frame (in a tick, the tick),
 * and a double tap (a second within 0.35 s of the last, near it), where it was; a swipe
 * (lifted within 0.5 s, having gone 50 or more): how far, 0, 0 for none; and a hold (down
 * for 0.5 s or more, still), true while it lasts. */
WGF_API bool wgf_touch_is_tapped(void);
WGF_API bool wgf_touch_is_double_tapped(void);
WGF_API wgf_vec2_t wgf_touch_get_tap_position(void);
WGF_API wgf_vec2_t wgf_touch_get_swipe(void);
WGF_API bool wgf_touch_is_holding(void);

#ifdef __cplusplus
}
#endif

#endif
