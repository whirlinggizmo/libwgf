#ifndef WGF_CANVAS_H
#define WGF_CANVAS_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A canvas: the root of a 2D tree, in logical pixels from the top-left, y down.
 * It draws when asked, its nodes depth first, a parent before its children and
 * children in order, so later ones draw over earlier ones. The game composes a
 * frame by drawing its canvases in the order it wants: a backdrop, then the world,
 * then a HUD. A canvas is a node (wgf_node.h): its own transform moves
 * everything in it, and wgf_node_destroy ends it. */

/* A canvas, with no camera. 0 when there is no room for another node. */
WGF_API wgf_node_t wgf_canvas_create(void);

/* Draw it into this frame. Outside a frame, nothing. */
WGF_API void wgf_canvas_draw(wgf_node_t canvas);

/* With a camera (wgf_camera2d.h), the canvas point at the camera's position is
 * drawn at the frame's center, turned and zoomed as the camera is: a 2D world.
 * Without one (0, the default), canvas units are the frame's logical pixels: a HUD.
 * The camera can be anywhere -- in this canvas, parented to the player so it
 * follows, or in no tree at all. False when `camera` isn't a 2D camera. A camera
 * that is destroyed leaves the canvas with none. */
WGF_API bool wgf_canvas_set_camera(wgf_node_t canvas, wgf_node_t camera);
WGF_API wgf_node_t wgf_canvas_get_camera(wgf_node_t canvas);

#ifdef __cplusplus
}
#endif

#endif
