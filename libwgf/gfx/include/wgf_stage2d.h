#ifndef WGF_STAGE2D_H
#define WGF_STAGE2D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 2D stage: the root of a 2D tree, in logical pixels from the top-left, y down.
 * It draws when asked, its actors depth first, a parent before its children and
 * children in order, so later ones draw over earlier ones. The game composes a
 * frame by drawing its stages in the order it wants: a backdrop, then the world,
 * then a HUD. A stage is an actor (wgf_actor.h): its own transform moves
 * everything on it, and wgf_actor_destroy ends it. */

/* A 2D stage, with no camera. 0 when there is no room for another actor. */
WGF_API wgf_actor_t wgf_stage2d_create(void);

/* Draw it into this frame. Outside a frame, nothing. */
WGF_API void wgf_stage2d_draw(wgf_actor_t stage);

/* With a camera (wgf_camera2d.h), the stage's point at the camera's position is
 * drawn at the frame's center, turned and zoomed as the camera is: a 2D world.
 * Without one (0, the default), stage units are the frame's logical pixels: a HUD.
 * The camera can be anywhere -- on this stage, parented to the player so it
 * follows, or in no tree at all. False when `camera` isn't a 2D camera. A camera
 * that is destroyed leaves the stage with none. */
WGF_API bool wgf_stage2d_set_camera(wgf_actor_t stage, wgf_actor_t camera);
WGF_API wgf_actor_t wgf_stage2d_get_camera(wgf_actor_t stage);

/* The actor on this stage named `name` (wgf_actor_set_name), at any depth, found by an
 * index of names rather than a walk of the tree. 0 for none, for a stage that isn't a
 * 2D stage, and for a name two of its actors share (warned: find it by its path from an
 * actor above it, wgf_actor_find, instead). */
WGF_API wgf_actor_t wgf_stage2d_find(wgf_actor_t stage, const char *name);

#ifdef __cplusplus
}
#endif

#endif
