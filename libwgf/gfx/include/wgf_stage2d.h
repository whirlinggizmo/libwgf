#ifndef WGF_STAGE2D_H
#define WGF_STAGE2D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_vec2.h"
#include "wgf_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 2D stage: the root of a 2D tree, in logical pixels from the top-left, with y down.
 *
 * It draws when asked. It draws its actors depth first: a parent before its children, and
 * children in order, so later ones draw over earlier ones. The game composes a frame by
 * drawing its stages in the order it wants: a backdrop, then the world, then a HUD.
 *
 * A stage is an actor (wgf_actor.h). Its own transform moves every actor on it, and
 * wgf_actor_destroy ends it. */

/* Creates a 2D stage, with no camera.
 *
 * Returns 0 if there is no room for another actor. */
WGF_API wgf_actor_t wgf_stage2d_create(void);

/* Draws the stage into this frame. Outside a frame, it does nothing. */
WGF_API void wgf_stage2d_draw(wgf_actor_t stage);

/* Sets or gets the stage's camera (wgf_camera2d.h).
 *
 * - With a camera, the stage's point at the camera's position is drawn at the frame's center,
 *   turned and zoomed as the camera is. Use it for a 2D world.
 * - Without one (0, the default), stage units are the frame's logical pixels. Use it for a HUD.
 *
 * The camera can be anywhere: on this stage, parented to the player so it follows, or in no
 * tree at all. A camera that is destroyed leaves the stage with none.
 *
 * set_camera returns false if `camera` isn't a 2D camera. */
WGF_API bool wgf_stage2d_set_camera(wgf_actor_t stage, wgf_actor_t camera);
WGF_API wgf_actor_t wgf_stage2d_get_camera(wgf_actor_t stage);

/* Converts between the stage's units and the screen's logical pixels, through its camera as it
 * is now.
 *
 * Logical pixels are what the pointer reads (wgf_mouse.h).
 *
 * - world_to_screen returns where a point of the stage shows on the screen.
 * - screen_to_world returns the stage's point under a place on the screen, as for a click in
 *   the world.
 *
 * Each returns 0, 0 if `stage` isn't a 2D stage. */
WGF_API wgf_vec2_t wgf_stage2d_world_to_screen(wgf_actor_t stage, float x, float y);
WGF_API wgf_vec2_t wgf_stage2d_screen_to_world(wgf_actor_t stage, float x, float y);

/* Returns the topmost actor on this stage at the screen's point (x, y).
 *
 * The point is in logical pixels, where the pointer is (wgf_mouse.h), seen through the stage's
 * camera as it is now. The topmost actor is the one drawn last.
 *
 * What can be hit:
 * - A sprite, by its quad, turned and scaled as it is drawn. Or by its texels' alpha, if set
 *   (see wgf_sprite_set_pick_alpha).
 * - A 2D shape, by its area: a rectangle (with its corners rounded), circle, or polygon,
 *   filled or outlined. A line isn't hit.
 * - A text, by its block.
 *
 * An actor that isn't enabled isn't hit, and neither is any actor under it. An actor that
 * isn't visible, or isn't pickable (wgf_actor_set_pickable), lets the point through. Its
 * children can still be hit.
 *
 * Where it hit goes into `hit`, your array, as many values as fit in `hit_count`. Pass NULL
 * and 0 for none.
 * - 0, 1: the point, in the stage's units.
 * - 2, 3: the point, in the hit actor's own units. Its origin is at the actor's position, so a
 *   sprite's top-left corner is at minus its pivot times its size.
 *
 * All values are 0 after a miss.
 *
 * Returns the actor hit. Returns 0 if nothing is hit, or if `stage` isn't a 2D stage. It works
 * outside a frame, with the stage as it is placed now. */
WGF_API wgf_actor_t wgf_stage2d_pick(wgf_actor_t stage, float x, float y, float *hit, int hit_count);

/* Returns the actor on this stage named `name` (wgf_actor_set_name), at any depth.
 *
 * It looks the name up in an index, rather than walking the tree.
 *
 * Returns 0 if:
 * - No actor has that name.
 * - `stage` isn't a 2D stage.
 * - Two of its actors share the name (warned). Find it by its path from an actor above it
 *   instead (see wgf_actor_find). */
WGF_API wgf_actor_t wgf_stage2d_find(wgf_actor_t stage, const char *name);

#ifdef __cplusplus
}
#endif

#endif
