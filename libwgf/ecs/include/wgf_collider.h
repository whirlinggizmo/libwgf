#ifndef WGF_COLLIDER_H
#define WGF_COLLIDER_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"
#include "wgf_vec2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A collider (WGF_COMPONENT_COLLIDER): a shape about the actor's position, in its parent's
 * space, that overlaps others as a trigger: each tick, after motion and bounds, two colliders
 * that start to overlap raise TRIGGER_ENTER and two that stop raise TRIGGER_EXIT
 * (wgf_world.h), told to each. They overlap only where one's layer meets the other's mask:
 * (a's layer & b's mask) or (b's layer & a's mask). Nothing is pushed apart by it: how far
 * apart would part a pair is read (get_push), and a body (wgf_body2d.h) is stopped by solid
 * ones. Only colliders under the same parent actor are compared, found by a sweep of their
 * bounds (a few thousand at once).
 *
 * Its shape: a circle (a sphere: its z counts against another circle's), its radius scaled
 * by the larger of the actor's scale's x and y; or, on a 2D stage's plane (x and y; z
 * ignored), a rectangle, a convex polygon, or a segment, scaled by the actor's scale and
 * turned by its turn about z. Each is about the actor's position moved by its offset
 * (scaled and turned with it). Defaults: a circle of radius 1, no offset, layer 1, mask
 * everything (-1), enabled. Every call is false (or 0) for an actor without a collider. */

typedef enum wgf_collider_shape_t {
    WGF_COLLIDER_SHAPE_CIRCLE = 0,
    WGF_COLLIDER_SHAPE_RECTANGLE = 1,
    WGF_COLLIDER_SHAPE_POLYGON = 2,
    WGF_COLLIDER_SHAPE_SEGMENT = 3
} wgf_collider_shape_t;

/* The most points a polygon has. */
#define WGF_COLLIDER_POLYGON_MAX 16

/* A circle of `radius`. False too for a radius below 0. get_radius is a circle's (0 for
 * another shape). */
WGF_API bool wgf_collider_set_radius(wgf_actor_t actor, float radius);
WGF_API float wgf_collider_get_radius(wgf_actor_t actor);

/* A rectangle `width` by `height`, centered. False too for a size below 0. Its size (0, 0
 * for another shape). */
WGF_API bool wgf_collider_set_rectangle(wgf_actor_t actor, float width, float height);
WGF_API wgf_vec2_t wgf_collider_get_size(wgf_actor_t actor);

/* A convex polygon of `points`, x and y in turn (`count` floats: 3 to
 * WGF_COLLIDER_POLYGON_MAX points), about the actor's position. False too for no points,
 * too few or too many, or a count that is odd. A polygon that isn't convex overlaps as its
 * convex hull would. */
WGF_API bool wgf_collider_set_polygon(wgf_actor_t actor, const float *points, int count);

/* A segment from x0, y0 to x1, y1, about the actor's position: a wall, a laser. */
WGF_API bool wgf_collider_set_segment(wgf_actor_t actor, float x0, float y0, float x1, float y1);

/* Its shape; and a polygon's or a segment's points, into `out` (x and y in turn, as many
 * as fit in `count` floats), returning how many floats it filled (0 for a circle or a
 * rectangle). */
WGF_API wgf_collider_shape_t wgf_collider_get_shape(wgf_actor_t actor);
WGF_API int wgf_collider_get_points(wgf_actor_t actor, float *out, int count);

/* Its shape moved from the actor's position, before the actor's scale and turn. */
WGF_API bool wgf_collider_set_offset(wgf_actor_t actor, float x, float y);
WGF_API wgf_vec2_t wgf_collider_get_offset(wgf_actor_t actor);

/* Bits: what it is, and what it meets. A pair meets when either side's mask has the
 * other's layer, so clearing one collider's mask doesn't stop another whose mask has its
 * layer from meeting it: to make one meet nothing, switch it off (below). */
WGF_API bool wgf_collider_set_layer(wgf_actor_t actor, int layer);
WGF_API int wgf_collider_get_layer(wgf_actor_t actor);
WGF_API bool wgf_collider_set_mask(wgf_actor_t actor, int mask);
WGF_API int wgf_collider_get_mask(wgf_actor_t actor);

/* Switched off, a collider meets nothing, from either side, and a pair it was in ends
 * (TRIGGER_EXIT at the next tick); its radius, layer, and mask are kept, so switched on
 * again it meets as they say: a ship that can't be hit while it blinks. */
WGF_API bool wgf_collider_set_enabled(wgf_actor_t actor, bool enabled);
WGF_API bool wgf_collider_is_enabled(wgf_actor_t actor);

/* What it overlaps as of the last tick, into `out`, as many as fit in `count`, returning
 * how many it filled. */
WGF_API int wgf_collider_get_overlaps(wgf_actor_t actor, wgf_actor_t *out, int count);

/* The least move of `actor` that would part it from `other`, as of the last tick: out of
 * the other along the way they meet, as deep as they overlap (a push out, for a game that
 * pushes). 0, 0 when they don't overlap. */
WGF_API wgf_vec2_t wgf_collider_get_push(wgf_actor_t actor, wgf_actor_t other);

#ifdef __cplusplus
}
#endif

#endif
