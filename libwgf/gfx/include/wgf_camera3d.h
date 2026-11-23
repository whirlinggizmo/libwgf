#ifndef WGF_CAMERA3D_H
#define WGF_CAMERA3D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 3D camera: an actor the world is seen from (see wgf_draw_begin_3d).
 *
 * - It looks down its -z, with +y up.
 * - Place and aim it with the actor calls (wgf_actor_look_at), or parent it to the actor it
 *   should follow. Its scale is ignored.
 * - It sees in perspective by default. It can instead see orthographically, with no shrinking
 *   with distance, for a plan view or an editor.
 * - What it sees fills the presentation's visible area (wgf_presentation.h). The horizontal
 *   field of view follows from that area's shape.
 * - Space is right-handed, with y up.
 * - It draws nothing itself. */

/* Creates a 3D camera with a 60 degree vertical field of view, seeing from 0.1 to 1000 units
 * in front of it.
 *
 * Returns 0 if there is no room for another actor. */
WGF_API wgf_actor_t wgf_camera3d_create(void);

/* Sets or gets the vertical field of view, in radians.
 *
 * It is clamped to 0.01 to 3.13 (just short of a half turn). */
WGF_API bool wgf_camera3d_set_fov(wgf_actor_t camera, float radians);
WGF_API float wgf_camera3d_get_fov(wgf_actor_t camera);

/* Sets or gets how near and how far in front of the camera it sees, in units.
 *
 * set_clip returns false unless 0 < `near_z` < `far_z`. */
WGF_API bool wgf_camera3d_set_clip(wgf_actor_t camera, float near_z, float far_z);
WGF_API float wgf_camera3d_get_near(wgf_actor_t camera);
WGF_API float wgf_camera3d_get_far(wgf_actor_t camera);

/* Sets or gets whether the camera is orthographic (true) or perspective (false) (default:
 * perspective). */
WGF_API bool wgf_camera3d_set_orthographic(wgf_actor_t camera, bool orthographic);
WGF_API bool wgf_camera3d_is_orthographic(wgf_actor_t camera);

/* Sets or gets how tall a view an orthographic camera sees, in units (default: 10).
 *
 * set_ortho_height returns false for a height of 0 or less. */
WGF_API bool wgf_camera3d_set_ortho_height(wgf_actor_t camera, float height);
WGF_API float wgf_camera3d_get_ortho_height(wgf_actor_t camera);

/* Converts between the world and the screen's logical pixels, through the camera as it is now.
 *
 * The world is the camera's tree's root's space. The view fills the visible area, as a stage
 * draws it.
 *
 * - world_to_screen returns where a point shows: x and y on the screen, and z its depth. Depth
 *   runs from 0 at the near plane to 1 at the far plane. Below 0 is behind the camera, where a
 *   name tag would be hidden.
 * - screen_to_origin and screen_to_direction return the ray under a place on the screen. Its
 *   origin is on the near plane, and its direction is a unit vector. Use it for picking, or
 *   for a click in the world.
 *
 * Each returns 0, 0, 0 if `camera` isn't a 3D camera. */
WGF_API wgf_vec3_t wgf_camera3d_world_to_screen(wgf_actor_t camera, float x, float y, float z);
WGF_API wgf_vec3_t wgf_camera3d_screen_to_origin(wgf_actor_t camera, float x, float y);
WGF_API wgf_vec3_t wgf_camera3d_screen_to_direction(wgf_actor_t camera, float x, float y);

#ifdef __cplusplus
}
#endif

#endif
