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

/* Cameras that follow, helix-legacy's CameraFollow2D, CameraFollow3D, and Orbit3D: a camera
 * actor (wgf_camera2d.h, wgf_camera3d.h) given a target actor is moved each frame, before
 * the frame callback, to follow where the target is drawn. At most 64 cameras follow.
 *
 * A 2D camera centers the view on a point that follows the target: the target moves freely
 * inside a dead zone around it, and when it leaves, the point catches up, smoothed; the
 * view is kept inside its bounds when it has them (the presentation's visible area, over
 * the camera's zoom, is what must fit).
 *
 * A 3D camera sits at its offset from a point that follows the target the same way (a dead
 * zone of width across and height up, and smoothing), and looks at the point: in CHASE the
 * offset is turned with the target's heading (its turn about y), swinging round to it
 * smoothed, so it stays behind a car; in FIXED it isn't turned; in ORBIT it is the orbit's
 * (a yaw, a pitch, and a distance the program sets, from a stick or the pointer).
 *
 * Either shakes: trauma (0 to 1) added by shake and lost over time, the view thrown by its
 * square so small shakes are gentle, as helix's. */

typedef enum wgf_follow_mode_t {
    WGF_FOLLOW_MODE_CHASE = 0, /* 3D: the offset turned with the target's heading (the default) */
    WGF_FOLLOW_MODE_FIXED = 1, /* 3D: the offset as it is, in the world */
    WGF_FOLLOW_MODE_ORBIT = 2  /* 3D: round the point at the orbit's yaw, pitch, and distance */
} wgf_follow_mode_t;

/* The camera follows `target` (0: stops following, staying where it is). False for a
 * camera that isn't a 2D or 3D camera, a target that isn't an actor, or 64 following. */
WGF_API bool wgf_follow_set_target(wgf_actor_t camera, wgf_actor_t target);
WGF_API wgf_actor_t wgf_follow_get_target(wgf_actor_t camera);

/* How quickly the point catches up, a rate a second (8 by default: about a fifth of a
 * second); 0 or less: at once. The setters below are false for a camera that follows
 * nothing. */
WGF_API bool wgf_follow_set_smoothing(wgf_actor_t camera, float rate);
WGF_API float wgf_follow_get_smoothing(wgf_actor_t camera);
/* The dead zone around the point the target moves in freely: width and height (0, 0 by
 * default: always catching up). Refuses a negative one. */
WGF_API bool wgf_follow_set_dead_zone(wgf_actor_t camera, float width, float height);
WGF_API wgf_vec2_t wgf_follow_get_dead_zone(wgf_actor_t camera);
/* 2D: the world the view stays inside, left, top, right, bottom (centered on one smaller than
 * the view); a right not above the left (the default) lets it go anywhere. */
WGF_API bool wgf_follow_set_bounds(wgf_actor_t camera, float left, float top, float right, float bottom);
WGF_API wgf_vec4_t wgf_follow_get_bounds(wgf_actor_t camera);

/* 3D: where the camera sits from the point (0, 3, 6 by default: behind a target facing -z,
 * and up), and how. */
WGF_API bool wgf_follow_set_offset(wgf_actor_t camera, float x, float y, float z);
WGF_API wgf_vec3_t wgf_follow_get_offset(wgf_actor_t camera);
WGF_API bool wgf_follow_set_mode(wgf_actor_t camera, wgf_follow_mode_t mode);
WGF_API wgf_follow_mode_t wgf_follow_get_mode(wgf_actor_t camera);
/* 3D, ORBIT: round the point, `yaw` radians about y (0 on its +z side), `pitch` radians up
 * (clamped to just short of straight up or down), `distance` out. Refuses a distance not
 * above 0. Read back as yaw, pitch, distance. */
WGF_API bool wgf_follow_set_orbit(wgf_actor_t camera, float yaw, float pitch, float distance);
WGF_API wgf_vec3_t wgf_follow_get_orbit(wgf_actor_t camera);

/* Trauma added (held to 1): 0.3 a bump, 1 an explosion; lost at 1.5 a second. The
 * getter is the trauma now. */
WGF_API bool wgf_follow_shake(wgf_actor_t camera, float amount);
WGF_API float wgf_follow_get_trauma(wgf_actor_t camera);

/* The point put on the target at once, and the camera with it (a respawn, a new level). */
WGF_API bool wgf_follow_snap(wgf_actor_t camera);

#ifdef __cplusplus
}
#endif

#endif /* WGF_FOLLOW_H */
