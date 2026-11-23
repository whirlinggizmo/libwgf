#ifndef WGF_ARCADE2D_H
#define WGF_ARCADE2D_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The arcade body (WGF_COMPONENT_ARCADE2D): an arcade box that moves its actor each tick by
 * its velocity and gravity.
 *
 * It slides along what is solid, and its velocity stops against it. It keeps what it touches:
 * the ground under it, a wall on either side, and a ceiling. Use it for a platformer's player
 * or enemy, or, with no gravity, for a top-down character. It works on a 2D stage, where y
 * points down: positive gravity pulls down, and the ground is under it.
 *
 * Its box is `size`, centered on the actor's position and scaled by the actor's scale. The
 * box is never turned.
 *
 * Each tick, after motion, gravity speeds up its fall, up to its fastest fall. It then moves
 * across, then down (see wgf_collide2d_move_rects). It is stopped by two kinds of obstacle
 * under the same parent actor:
 * - Colliders (wgf_collider.h) that are switched on, whose layer is in its solid mask, on
 *   actors without an arcade body. Each is solid by its bounds: a rectangle's own box, or the
 *   box around a circle, polygon, or segment. Arcade bodies don't push or stop each other.
 *   Their colliders meet them as triggers.
 * - The parent's grid of solid cells (see wgf_arcade2d_set_grid), such as a tilemap's walls
 *   and floors.
 *
 * The arcade body moves its actor itself, so an actor with one should have no motion. The
 * actor's own collider, if it has one, isn't solid to it.
 *
 * Defaults:
 * - size 1 by 1
 * - still (no velocity)
 * - no gravity, and no fastest fall
 * - a solid mask of every layer (-1)
 *
 * Every call returns false (or 0) for an actor without an arcade body. */

/* Sets or gets the box's width and height (default: 1 by 1). The setter also returns false
 * for a size below 0, or one that isn't finite. */
WGF_API bool wgf_arcade2d_set_size(wgf_actor_t actor, float width, float height);
WGF_API wgf_vec2_t wgf_arcade2d_get_size(wgf_actor_t actor);

/* Sets or gets the velocity, in units a second, positive right and down. The setter also
 * returns false for numbers that aren't finite. */
WGF_API bool wgf_arcade2d_set_velocity(wgf_actor_t actor, float x, float y);
WGF_API wgf_vec2_t wgf_arcade2d_get_velocity(wgf_actor_t actor);

/* Sets or gets gravity: how fast the fall speeds up, in units a second each second. Positive
 * is down, and 0 is none, for a top-down game. The setter also returns false for a number
 * that isn't finite. */
WGF_API bool wgf_arcade2d_set_gravity(wgf_actor_t actor, float gravity);
WGF_API float wgf_arcade2d_get_gravity(wgf_actor_t actor);

/* Sets or gets the fastest fall, in units a second. 0 or less means no limit. The setter also
 * returns false for a number that isn't finite. */
WGF_API bool wgf_arcade2d_set_max_fall_speed(wgf_actor_t actor, float speed);
WGF_API float wgf_arcade2d_get_max_fall_speed(wgf_actor_t actor);

/* Sets or gets the collider layers that are solid to the arcade body (default: every layer,
 * -1). */
WGF_API bool wgf_arcade2d_set_solid_mask(wgf_actor_t actor, int mask);
WGF_API int wgf_arcade2d_get_solid_mask(wgf_actor_t actor);

/* Returns what the arcade body touched at its last tick, moving into it.
 *
 * - is_grounded: it is standing on a collider or a solid cell. Gravity presses it down each
 *   tick, so it reads grounded while it stands.
 * - is_blocked_left, is_blocked_right: it was stopped on that side.
 * - is_blocked_above: it was stopped on its top (it hit its head).
 * - get_air_time: the seconds since it was last grounded, or 0 while it is grounded.
 *
 * A side reads blocked only while the arcade body moves that way. A body walking into a wall
 * each tick, as when a key is held, reads blocked. One stopped there and left still doesn't.
 *
 * To allow a jump a moment after walking off a ledge (coyote time), check that the air time
 * is short. */
WGF_API bool wgf_arcade2d_is_grounded(wgf_actor_t actor);
WGF_API bool wgf_arcade2d_is_blocked_left(wgf_actor_t actor);
WGF_API bool wgf_arcade2d_is_blocked_right(wgf_actor_t actor);
WGF_API bool wgf_arcade2d_is_blocked_above(wgf_actor_t actor);
WGF_API float wgf_arcade2d_get_air_time(wgf_actor_t actor);

/* Sets the grid of solid cells that the arcade bodies under `parent` slide against.
 *
 * - `cells` holds `count` cells, row by row from the top, `columns` to a row. They are
 *   copied. A nonzero cell is solid.
 * - Cell 0's top-left corner is at `x`, `y` in the parent's space. Each cell is a square
 *   `size` wide.
 * - Outside the grid is open.
 * - A parent has one grid. Setting it again replaces it.
 *
 * Returns false for a parent that isn't an actor, no cells, `columns` of 0 or less, or a
 * `size` that isn't above 0.
 *
 * clear_grid removes the grid. get_grid_cell reads a cell, and returns 0 outside the grid or
 * when there is none. set_grid_cell sets one, such as a door opened or a block broken, and
 * returns false outside the grid. */
WGF_API bool wgf_arcade2d_set_grid(wgf_actor_t parent, const int *cells, int count, int columns, float size, float x,
                                 float y);
/* Sets the grid from a placed Tiled map's tile layer (wgf_tilemap_layer.h).
 *
 * - A cell with a tile is solid, and an empty cell is open.
 * - Each cell is the map's tile size.
 * - Cell 0 is where the layer's top-left cell is in the parent's space. That includes the
 *   layer's offset, the placement of the map's root, and an infinite map's origin.
 * - A layer scaled or turned under the parent is placed, but the grid isn't scaled or turned.
 * - The grid copies the layer's cells as they are now. A cell set on the layer later must also
 *   be set here, with set_grid_cell.
 *
 * Also returns false for a layer that isn't a placed tile layer, a map that isn't orthogonal,
 * or a map whose tiles aren't square. */
WGF_API bool wgf_arcade2d_set_grid_from_layer(wgf_actor_t parent, wgf_actor_t layer);
WGF_API bool wgf_arcade2d_clear_grid(wgf_actor_t parent);
WGF_API int wgf_arcade2d_get_grid_cell(wgf_actor_t parent, int column, int row);
WGF_API bool wgf_arcade2d_set_grid_cell(wgf_actor_t parent, int column, int row, int value);

#ifdef __cplusplus
}
#endif

#endif
