#ifndef WGF_CAMERA2D_H
#define WGF_CAMERA2D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 2D camera: a node whose position is the point a canvas shows at the frame's
 * center (wgf_canvas_set_camera), turned by its rotation about z. Its world
 * position counts, so a camera parented to the player follows it. It draws nothing
 * itself. */

/* A 2D camera, zoom 1. 0 when there is no room for another node. */
WGF_API wgf_node_t wgf_camera2d_create(void);

/* How big things look: 2 draws everything twice as big. False for a zoom of 0 or
 * less. */
WGF_API bool wgf_camera2d_set_zoom(wgf_node_t camera, float zoom);
WGF_API float wgf_camera2d_get_zoom(wgf_node_t camera);

#ifdef __cplusplus
}
#endif

#endif
