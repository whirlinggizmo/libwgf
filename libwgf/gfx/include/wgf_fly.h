#ifndef WGF_FLY_H
#define WGF_FLY_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cameras flown by input, raylib's CAMERA_FREE and CAMERA_FIRST_PERSON (UpdateCamera): a 3D
 * camera actor (wgf_camera3d.h) given a mode is turned and moved each frame, before the frame
 * callback, by the keyboard, the mouse, and the pads. At most 64 cameras have a mode or a
 * setting.
 *
 *   the mouse            looks: yaw and pitch, the pitch held just short of straight up or
 *                        down; the pointer is locked while it looks (wgf_mouse_set_locked),
 *                        and a click locks it again once the platform let it go (Escape in
 *                        a browser)
 *   the right stick      looks, at the turn speed
 *   W A S D, the arrows  move forward, left, back, and right; the left stick too
 *   Space or E           FREE: up (the world's +y); a pad's right bumper too
 *   Left Control or Q    FREE: down; a pad's left bumper too
 *
 * FREE moves where it looks; FIRST_PERSON moves on the ground plane, its height kept (what
 * the program sets it to: stairs, a jump). Every pad is read, the one pushed furthest
 * winning. Keys aren't read while the keyboard is captured, and the mouse doesn't look while
 * the pointer is (wgf_input.h: a UI has it). The camera's position is read each frame, so the
 * program may move it; it is turned only by the look below while it flies. Every flown
 * camera moves by the same input, so fly one at a time; a camera that also follows
 * (wgf_follow.h) is placed by both. */

typedef enum wgf_fly_mode_t {
    WGF_FLY_MODE_NONE = 0,        /* not flown: the program places it (the default) */
    WGF_FLY_MODE_FREE = 1,        /* free-fly: forward where it looks, up and down by keys */
    WGF_FLY_MODE_FIRST_PERSON = 2 /* on the ground plane, its height kept */
} wgf_fly_mode_t;

/* The camera flown in `mode` (NONE: no longer, staying where it is and its settings kept).
 * Starting, its look is taken from where it faces now. False for a camera that isn't a 3D
 * camera, a mode that isn't one, or 64 cameras with a mode or a setting. */
WGF_API bool wgf_fly_set_mode(wgf_actor_t camera, wgf_fly_mode_t mode);
WGF_API wgf_fly_mode_t wgf_fly_get_mode(wgf_actor_t camera);

/* How fast it moves, world units a second (5.4 by default, raylib's); a stick part way, part
 * of it. False for a negative one, and, as every setter here, a camera refused as above. */
WGF_API bool wgf_fly_set_speed(wgf_actor_t camera, float speed);
WGF_API float wgf_fly_get_speed(wgf_actor_t camera);

/* How far the mouse turns it, radians a logical pixel (0.003 by default, raylib's); 0: the
 * mouse doesn't look. False for a negative one. */
WGF_API bool wgf_fly_set_sensitivity(wgf_actor_t camera, float radians);
WGF_API float wgf_fly_get_sensitivity(wgf_actor_t camera);

/* How fast a stick pushed all the way turns it, radians a second (2 by default). False for
 * a negative one. */
WGF_API bool wgf_fly_set_turn_speed(wgf_actor_t camera, float radians);
WGF_API float wgf_fly_get_turn_speed(wgf_actor_t camera);

/* Where it looks, turned at once: `yaw` radians about y (0 facing -z, a quarter turn
 * facing -x), `pitch` radians up (clamped to just short of straight up or down). Read back
 * as where it looks now, yaw (-pi to pi) and pitch. False for a value not finite. */
WGF_API bool wgf_fly_set_look(wgf_actor_t camera, float yaw, float pitch);
WGF_API wgf_vec2_t wgf_fly_get_look(wgf_actor_t camera);

/* The mouse looking only while its right button is held, the pointer locked while it is (a
 * debug camera beside a game's own clicks); false (the default): always, the pointer locked
 * while it flies. */
WGF_API bool wgf_fly_set_look_held(wgf_actor_t camera, bool held);
WGF_API bool wgf_fly_is_look_held(wgf_actor_t camera);

#ifdef __cplusplus
}
#endif

#endif /* WGF_FLY_H */
