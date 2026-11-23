#ifndef WGF_ARCADE2D_H
#define WGF_ARCADE2D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 2D body (WGF_COMPONENT_ARCADE2D), helix-legacy's Body2D: an arcade box that moves its
 * actor each tick by its velocity and gravity, sliding along what is solid and stopping its
 * velocity against it, and what it touches kept: the ground under it, a wall either side, a
 * ceiling. A platformer's player or enemy; a top-down character, with no gravity. On a 2D
 * stage, y down: gravity is + down, and the ground is under it.
 *
 * Its box is `size` about the actor's position, scaled by its scale (not turned). Each tick,
 * after motion, gravity speeds its fall (held to its fastest fall), and it moves across then
 * down (wgf_collide2d_move_rects), against two things under the same parent actor:
 *   - colliders (wgf_collider.h) whose layer is in its solid mask, by their bounds (a
 *     rectangle's own box; a circle's, a polygon's, a segment's box around it), switched on,
 *     on actors without a body: bodies don't push or stop each other (their colliders meet
 *     them as triggers);
 *   - the parent's grid of solid cells (set_grid below): a tilemap's walls and floors.
 * A body moves its actor itself: an actor with one should have no motion. Its collider, if it
 * has one, isn't solid to it.
 *
 * Defaults: 1 by 1, still, no gravity, no fastest fall, a solid mask of every layer (-1).
 * Every call is false (or 0) for an actor without a body. */

/* False too for a size below 0, or not finite. */
WGF_API bool wgf_arcade2d_set_size(wgf_actor_t actor, float width, float height);
WGF_API wgf_vec2_t wgf_arcade2d_get_size(wgf_actor_t actor);

/* Units a second, + right and down. False too for numbers that aren't finite. */
WGF_API bool wgf_arcade2d_set_velocity(wgf_actor_t actor, float x, float y);
WGF_API wgf_vec2_t wgf_arcade2d_get_velocity(wgf_actor_t actor);

/* Its fall speeding up, units a second each second (+ down; 0, none: top-down). False too for
 * a number that isn't finite. */
WGF_API bool wgf_arcade2d_set_gravity(wgf_actor_t actor, float gravity);
WGF_API float wgf_arcade2d_get_gravity(wgf_actor_t actor);

/* The fastest it falls, units a second; 0 or less for no limit. False too for a number that
 * isn't finite. */
WGF_API bool wgf_arcade2d_set_max_fall_speed(wgf_actor_t actor, float speed);
WGF_API float wgf_arcade2d_get_max_fall_speed(wgf_actor_t actor);

/* The collider layers that are solid to it. */
WGF_API bool wgf_arcade2d_set_solid_mask(wgf_actor_t actor, int mask);
WGF_API int wgf_arcade2d_get_solid_mask(wgf_actor_t actor);

/* What it touched at its last tick, moving into it: standing on something (grounded: gravity
 * presses it down each tick), stopped on its left, its right, its top (it hit its head). A
 * side reads blocked only while it moves that way: a body walking into a wall each tick, as
 * a key held does, reads blocked; one stopped there and left still doesn't. And seconds since it was last grounded, 0 while it is:
 * a jump a moment after walking off a ledge (coyote time) asks whether this is short. */
WGF_API bool wgf_arcade2d_is_grounded(wgf_actor_t actor);
WGF_API bool wgf_arcade2d_is_blocked_left(wgf_actor_t actor);
WGF_API bool wgf_arcade2d_is_blocked_right(wgf_actor_t actor);
WGF_API bool wgf_arcade2d_is_blocked_above(wgf_actor_t actor);
WGF_API float wgf_arcade2d_get_air_time(wgf_actor_t actor);

/* The grid of solid cells the bodies under `parent` slide against: `cells` row by row from
 * the top, `columns` a row, `count` of them (copied), nonzero solid; cell 0's top-left at
 * x, y in the parent's space, each `size` square; outside it is open. One a parent, set
 * again replacing it. False for a parent that isn't an actor, no cells, columns of 0 or less,
 * or a size that isn't above 0. clear: none again. A cell read (0 outside the grid, or with
 * none) and set (false outside it): a door opened, a block broken. */
WGF_API bool wgf_arcade2d_set_grid(wgf_actor_t parent, const int *cells, int count, int columns, float size, float x,
                                 float y);
/* The grid set from a placed Tiled map's tile layer (wgf_tilemap_layer.h): its cells with a
 * tile solid, the rest open, each the map's tile size, cell 0 where the layer's top-left cell
 * is in the parent's space (the layer's offset, for the map's root, and an infinite map's
 * origin; a layer scaled or turned under the parent is placed, not scaled or turned). Its
 * cells as they are now: a cell set on the layer later is set here too with set_grid_cell.
 * False too for a layer that isn't a placed tile layer, a map that isn't orthogonal, or one
 * whose tiles aren't square. */
WGF_API bool wgf_arcade2d_set_grid_from_layer(wgf_actor_t parent, wgf_actor_t layer);
WGF_API bool wgf_arcade2d_clear_grid(wgf_actor_t parent);
WGF_API int wgf_arcade2d_get_grid_cell(wgf_actor_t parent, int column, int row);
WGF_API bool wgf_arcade2d_set_grid_cell(wgf_actor_t parent, int column, int row, int value);

#ifdef __cplusplus
}
#endif

#endif
