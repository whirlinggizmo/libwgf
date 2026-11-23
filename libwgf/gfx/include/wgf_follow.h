#ifndef WGF_FOLLOW_H
#define WGF_FOLLOW_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_vec2.h"
#include "wgf_vec3.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cameras that follow a target actor: 2D follow, 3D follow, and 3D orbit.
 *
 * A camera actor (wgf_camera2d.h, wgf_camera3d.h) given a target actor is moved each frame,
 * before the frame callback. It follows where the target is drawn. At most 64 cameras follow.
 *
 * A 2D camera centers the view on a point that follows the target.
 * - The target moves freely inside a dead zone around the point. When it leaves, the point
 *   catches up, smoothed.
 * - The view is kept inside the camera's bounds, when it has them. What must fit is the
 *   presentation's visible area, divided by the camera's zoom.
 *
 * A 3D camera sits at its offset from a point that follows the target the same way, and looks
 * at the point. Its dead zone is a width across and a height up, and it is smoothed too.
 * - CHASE: the offset is turned with the target's heading (its turn about y). It swings round
 *   to it, smoothed, so it stays behind a car.
 * - FIXED: the offset isn't turned.
 * - ORBIT: the offset is the orbit's: a yaw, a pitch, and a distance the program sets, from a
 *   stick or the pointer.
 *
 * Either kind of camera shakes. wgf_follow_shake adds trauma (0 to 1), which is lost over
 * time. The view is thrown by the trauma's square, so small shakes are gentle. */

typedef enum wgf_follow_mode_t {
    WGF_FOLLOW_MODE_CHASE = 0, /* 3D: the offset turns with the target's heading (the default) */
    WGF_FOLLOW_MODE_FIXED = 1, /* 3D: the offset stays as it is, in the world */
    WGF_FOLLOW_MODE_ORBIT = 2  /* 3D: round the point, at the orbit's yaw, pitch, and distance */
} wgf_follow_mode_t;

/* Sets or gets the actor the camera follows.
 *
 * A `target` of 0 stops following. The camera stays where it is.
 *
 * set_target returns false if `camera` isn't a 2D or 3D camera, if `target` isn't an actor,
 * or if 64 cameras already follow. */
WGF_API bool wgf_follow_set_target(wgf_actor_t camera, wgf_actor_t target);
WGF_API wgf_actor_t wgf_follow_get_target(wgf_actor_t camera);

/* Sets or gets how quickly the point catches up, as a rate a second (default: 8, about a
 * fifth of a second).
 *
 * At 0 or less, it catches up at once.
 *
 * This setter, and every setter below, returns false for a camera that follows no actor. */
WGF_API bool wgf_follow_set_smoothing(wgf_actor_t camera, float rate);
WGF_API float wgf_follow_get_smoothing(wgf_actor_t camera);
/* Sets or gets the dead zone around the point, which the target moves in freely: its width
 * and height (default: 0, 0, always catching up). A negative size is refused. */
WGF_API bool wgf_follow_set_dead_zone(wgf_actor_t camera, float width, float height);
WGF_API wgf_vec2_t wgf_follow_get_dead_zone(wgf_actor_t camera);
/* 2D: sets or gets the part of the world the view stays inside: left, top, right, bottom.
 *
 * - If the bounds are smaller than the view, the view is centered on them.
 * - A right that isn't above the left lets the view go anywhere (the default). */
WGF_API bool wgf_follow_set_bounds(wgf_actor_t camera, float left, float top, float right, float bottom);
WGF_API wgf_vec4_t wgf_follow_get_bounds(wgf_actor_t camera);

/* 3D: sets or gets where the camera sits from the point (default: 0, 3, 6, behind a target
 * facing -z, and up), and the mode that says how (see wgf_follow_mode_t). */
WGF_API bool wgf_follow_set_offset(wgf_actor_t camera, float x, float y, float z);
WGF_API wgf_vec3_t wgf_follow_get_offset(wgf_actor_t camera);
WGF_API bool wgf_follow_set_mode(wgf_actor_t camera, wgf_follow_mode_t mode);
WGF_API wgf_follow_mode_t wgf_follow_get_mode(wgf_actor_t camera);
/* 3D, ORBIT: sets or gets where the camera sits round the point.
 *
 * - `yaw` is in radians about y. 0 is on the point's +z side.
 * - `pitch` is in radians up, clamped to just short of straight up or down.
 * - `distance` is how far out. A distance that isn't above 0 is refused.
 *
 * get_orbit returns yaw, pitch, and distance. */
WGF_API bool wgf_follow_set_orbit(wgf_actor_t camera, float yaw, float pitch, float distance);
WGF_API wgf_vec3_t wgf_follow_get_orbit(wgf_actor_t camera);

/* Adds trauma to the camera's shake, held to 1. Gets the trauma now.
 *
 * For example, 0.3 for a bump, and 1 for an explosion. Trauma is lost at 1.5 a second. */
WGF_API bool wgf_follow_shake(wgf_actor_t camera, float amount);
WGF_API float wgf_follow_get_trauma(wgf_actor_t camera);

/* Puts the point on the target at once, and the camera with it, as for a respawn or a new
 * level. */
WGF_API bool wgf_follow_snap(wgf_actor_t camera);

#ifdef __cplusplus
}
#endif

#endif /* WGF_FOLLOW_H */
