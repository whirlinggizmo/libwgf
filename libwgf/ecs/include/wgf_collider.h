#ifndef WGF_COLLIDER_H
#define WGF_COLLIDER_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A collider (WGF_COMPONENT_COLLIDER): a shape that overlaps other colliders as a trigger.
 *
 * The shape is placed about the actor's position, in its parent's space. Each tick, after
 * motion and bounds, colliders are compared (wgf_world.h):
 * - Two colliders that start to overlap raise TRIGGER_ENTER, told to each of them.
 * - Two that stop overlapping raise TRIGGER_EXIT, told to each of them.
 *
 * Two colliders overlap only where one's layer meets the other's mask: (a's layer & b's mask)
 * or (b's layer & a's mask).
 *
 * A collider pushes nothing apart. You can read how far apart would part a pair (see
 * wgf_collider_get_push), and the arcade body (wgf_arcade2d.h) is stopped by solid colliders.
 *
 * Only colliders under the same parent actor are compared. They are found by a sweep of their
 * bounds, which handles a few thousand at once.
 *
 * Shapes:
 * - A circle, which is a sphere: its z counts against another circle's. Its radius is scaled
 *   by the larger of the actor's scale's x and y.
 * - A rectangle, a convex polygon, or a segment, on a 2D stage's plane (x and y, with z
 *   ignored). Each is scaled by the actor's scale and turned by its turn about z.
 *
 * Each shape is about the actor's position moved by its offset. The offset is scaled and
 * turned with the shape.
 *
 * Defaults:
 * - a circle of radius 1
 * - no offset
 * - layer 1
 * - a mask of every layer (-1)
 * - enabled
 *
 * Every call returns false (or 0) for an actor without a collider. */

typedef enum wgf_collider_shape_t {
    WGF_COLLIDER_SHAPE_CIRCLE = 0,
    WGF_COLLIDER_SHAPE_RECTANGLE = 1,
    WGF_COLLIDER_SHAPE_POLYGON = 2,
    WGF_COLLIDER_SHAPE_SEGMENT = 3
} wgf_collider_shape_t;

/* The most points a polygon has. */
#define WGF_COLLIDER_POLYGON_MAX 16

/* Sets the shape to a circle of `radius`, or gets a circle's radius. The setter also returns
 * false for a radius below 0. get_radius returns 0 for another shape. */
WGF_API bool wgf_collider_set_radius(wgf_actor_t actor, float radius);
WGF_API float wgf_collider_get_radius(wgf_actor_t actor);

/* Sets the shape to a centered rectangle, `width` by `height`, or gets a rectangle's size.
 * The setter also returns false for a size below 0. get_size returns 0, 0 for another
 * shape. */
WGF_API bool wgf_collider_set_rectangle(wgf_actor_t actor, float width, float height);
WGF_API wgf_vec2_t wgf_collider_get_size(wgf_actor_t actor);

/* Sets the shape to a convex polygon of `points`, about the actor's position.
 *
 * `points` holds x and y in turn: `count` floats, for 3 to WGF_COLLIDER_POLYGON_MAX points.
 * A polygon that isn't convex overlaps as its convex hull would.
 *
 * Also returns false for no points, too few or too many, or an odd `count`. */
WGF_API bool wgf_collider_set_polygon(wgf_actor_t actor, const float *points, int count);

/* Sets the shape to a segment from `x0`, `y0` to `x1`, `y1`, about the actor's position, such
 * as a wall or a laser. */
WGF_API bool wgf_collider_set_segment(wgf_actor_t actor, float x0, float y0, float x1, float y1);

/* Returns the collider's shape, and a polygon's or a segment's points.
 *
 * get_points fills `out` with x and y in turn, as many as fit in `count` floats. It returns
 * how many floats it filled, or 0 for a circle or a rectangle. */
WGF_API wgf_collider_shape_t wgf_collider_get_shape(wgf_actor_t actor);
WGF_API int wgf_collider_get_points(wgf_actor_t actor, float *out, int count);

/* Sets or gets the offset: how far the shape is moved from the actor's position, before the
 * actor's scale and turn (default: no offset). */
WGF_API bool wgf_collider_set_offset(wgf_actor_t actor, float x, float y);
WGF_API wgf_vec2_t wgf_collider_get_offset(wgf_actor_t actor);

/* Sets or gets the layer and mask, as bits. The layer is what the collider is, and the mask is
 * what it meets (defaults: layer 1, mask -1).
 *
 * A pair meets when either side's mask has the other's layer. Clearing one collider's mask
 * doesn't stop another collider, whose mask has its layer, from meeting it. To make a collider
 * meet nothing, switch it off (see wgf_collider_set_enabled). */
WGF_API bool wgf_collider_set_layer(wgf_actor_t actor, int layer);
WGF_API int wgf_collider_get_layer(wgf_actor_t actor);
WGF_API bool wgf_collider_set_mask(wgf_actor_t actor, int mask);
WGF_API int wgf_collider_get_mask(wgf_actor_t actor);

/* Switches the collider on or off, or gets whether it is on (default: on).
 *
 * A collider switched off meets nothing, from either side. A pair it was in ends, raising
 * TRIGGER_EXIT at the next tick. Its radius, layer, and mask are kept, so when it is switched
 * on again it meets as they say. Use it for a ship that can't be hit while it blinks. */
WGF_API bool wgf_collider_set_enabled(wgf_actor_t actor, bool enabled);
WGF_API bool wgf_collider_is_enabled(wgf_actor_t actor);

/* Fills `out` with the actors whose colliders this one overlaps as of the last tick, as many
 * as fit in `count`. Returns how many it filled. */
WGF_API int wgf_collider_get_overlaps(wgf_actor_t actor, wgf_actor_t *out, int count);

/* Returns the smallest move of `actor` that would part it from `other`, as of the last tick.
 *
 * The move is out of the other collider, along the direction they meet, as deep as they
 * overlap. Use it as a push out, for a game that pushes. Returns 0, 0 when they don't
 * overlap. */
WGF_API wgf_vec2_t wgf_collider_get_push(wgf_actor_t actor, wgf_actor_t other);

#ifdef __cplusplus
}
#endif

#endif
