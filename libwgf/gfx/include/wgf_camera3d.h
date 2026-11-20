#ifndef WGF_CAMERA3D_H
#define WGF_CAMERA3D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 3D camera: an actor the world is seen from (wgf_draw_begin_3d), looking down its -z
 * with +y up. Place and aim it with the actor calls (wgf_actor_look_at), or parent it to
 * what it should follow; its scale is ignored. It sees in perspective by default, or
 * orthographically, with no shrinking with distance, for a plan view or an editor. What
 * it sees fills the presentation's visible area (wgf_presentation.h), the horizontal
 * field following from that area's shape. Space is right-handed, y up. It draws nothing
 * itself. */

/* A camera with a 60 degree vertical field of view, seeing from 0.1 to 1000 units in
 * front of it. 0 when there is no room for another actor. */
WGF_API wgf_actor_t wgf_camera3d_create(void);

/* The vertical field of view, in radians, clamped to 0.01 to 3.13 (just short of a
 * half turn). */
WGF_API bool wgf_camera3d_set_fov(wgf_actor_t camera, float radians);
WGF_API float wgf_camera3d_get_fov(wgf_actor_t camera);

/* How near and far in front of the camera it sees. False unless 0 < near < far. */
WGF_API bool wgf_camera3d_set_clip(wgf_actor_t camera, float near_z, float far_z);
WGF_API float wgf_camera3d_get_near(wgf_actor_t camera);
WGF_API float wgf_camera3d_get_far(wgf_actor_t camera);

/* Orthographic (true) or perspective (false, the default). */
WGF_API bool wgf_camera3d_set_orthographic(wgf_actor_t camera, bool orthographic);
WGF_API bool wgf_camera3d_is_orthographic(wgf_actor_t camera);

/* How tall a view an orthographic camera sees, in units (default 10). False for 0 or
 * less. */
WGF_API bool wgf_camera3d_set_ortho_height(wgf_actor_t camera, float height);
WGF_API float wgf_camera3d_get_ortho_height(wgf_actor_t camera);

/* Between the world (its tree's root's space) and the screen's logical pixels, through the
 * camera as it is now, filling the visible area as a stage draws it: where a point shows (x
 * and y; z its depth, 0 at the near plane to 1 at the far, below 0 behind the camera: a
 * name tag hidden), and the ray under a place on the screen, from the near plane (its origin)
 * along a unit direction: picking, a click in the world. 0, 0, 0 for a handle that isn't a 3D
 * camera. */
WGF_API wgf_vec3_t wgf_camera3d_world_to_screen(wgf_actor_t camera, float x, float y, float z);
WGF_API wgf_vec3_t wgf_camera3d_screen_to_origin(wgf_actor_t camera, float x, float y);
WGF_API wgf_vec3_t wgf_camera3d_screen_to_direction(wgf_actor_t camera, float x, float y);

#ifdef __cplusplus
}
#endif

#endif
