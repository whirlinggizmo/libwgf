#ifndef WGF_CAMERA2D_H
#define WGF_CAMERA2D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 2D camera: an actor whose position is the point a 2D stage shows at the frame's center.
 *
 * - Give it to a stage with wgf_stage2d_set_camera.
 * - Its rotation about z turns the view.
 * - Its world position is what counts, so a camera parented to the player follows the player.
 * - It draws nothing itself. */

/* Creates a 2D camera, at zoom 1.
 *
 * Returns 0 if there is no room for another actor. */
WGF_API wgf_actor_t wgf_camera2d_create(void);

/* Sets or gets the camera's zoom: how big actors look. A zoom of 2 draws everything twice as
 * big (default: 1).
 *
 * set_zoom returns false for a zoom of 0 or less. */
WGF_API bool wgf_camera2d_set_zoom(wgf_actor_t camera, float zoom);
WGF_API float wgf_camera2d_get_zoom(wgf_actor_t camera);

#ifdef __cplusplus
}
#endif

#endif
