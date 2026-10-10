#ifndef WGF_COLLIDER_H
#define WGF_COLLIDER_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A collider (WGF_COMPONENT_COLLIDER): a sphere about the actor's position, in its
 * parent's space (a circle, on a 2D stage, where every z is 0), its radius scaled by the
 * larger of its scale's x and y, that overlaps others as a trigger: each tick, after motion and bounds, two colliders that start to overlap raise
 * TRIGGER_ENTER and two that stop raise TRIGGER_EXIT (wgf_world.h), told to each. They
 * overlap only where one's layer meets the other's mask: (a's layer & b's mask) or (b's
 * layer & a's mask). Nothing is pushed apart: that is physics, not a trigger. Only
 * colliders under the same parent actor are compared. Defaults: a radius of 1, layer 1,
 * mask everything (-1), enabled. Every call is false (or 0) for an actor without a
 * collider. */

/* False too for a radius below 0. */
WGF_API bool wgf_collider_set_radius(wgf_actor_t actor, float radius);
WGF_API float wgf_collider_get_radius(wgf_actor_t actor);

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

#ifdef __cplusplus
}
#endif

#endif
