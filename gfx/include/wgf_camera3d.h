#ifndef WGF_CAMERA3D_H
#define WGF_CAMERA3D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 3D camera: a node the world is seen from (wgf_draw_begin_3d), looking down its -z
 * with +y up. Place and aim it with the node calls (wgf_node_look_at), or parent it to
 * what it should follow; its scale is ignored. It sees in perspective by default, or
 * orthographically, with no shrinking with distance, for a plan view or an editor. What
 * it sees fills the presentation's visible area (wgf_presentation.h), the horizontal
 * field following from that area's shape. Space is right-handed, y up. It draws nothing
 * itself. */

/* A camera with a 60 degree vertical field of view, seeing from 0.1 to 1000 units in
 * front of it. 0 when there is no room for another node. */
WGF_API wgf_node_t wgf_camera3d_create(void);

/* The vertical field of view, in radians, clamped to 0.01 to 3.13 (just short of a
 * half turn). */
WGF_API bool wgf_camera3d_set_fov(wgf_node_t camera, float radians);
WGF_API float wgf_camera3d_get_fov(wgf_node_t camera);

/* How near and far in front of the camera it sees. False unless 0 < near < far. */
WGF_API bool wgf_camera3d_set_clip(wgf_node_t camera, float near_z, float far_z);
WGF_API float wgf_camera3d_get_near(wgf_node_t camera);
WGF_API float wgf_camera3d_get_far(wgf_node_t camera);

/* Orthographic (true) or perspective (false, the default). */
WGF_API bool wgf_camera3d_set_orthographic(wgf_node_t camera, bool orthographic);
WGF_API bool wgf_camera3d_is_orthographic(wgf_node_t camera);

/* How tall a view an orthographic camera sees, in units (default 10). False for 0 or
 * less. */
WGF_API bool wgf_camera3d_set_ortho_height(wgf_node_t camera, float height);
WGF_API float wgf_camera3d_get_ortho_height(wgf_node_t camera);

#ifdef __cplusplus
}
#endif

#endif
