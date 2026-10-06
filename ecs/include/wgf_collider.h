#ifndef WGF_COLLIDER_H
#define WGF_COLLIDER_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A collider (WGF_COMPONENT_COLLIDER): a circle about the entity's position, in its
 * parent's space, scaled by the larger of its scale's x and y, that overlaps others as a
 * trigger: each tick, after motion and bounds, two colliders that start to overlap raise
 * TRIGGER_ENTER and two that stop raise TRIGGER_EXIT (wgf_ecs.h), told to each. They
 * overlap only where one's layer meets the other's mask: (a's layer & b's mask) or (b's
 * layer & a's mask). Nothing is pushed apart: that is physics, not a trigger. Only
 * colliders under the same parent node are compared. Defaults: a radius of 1, layer 1,
 * mask everything (-1). Every call is false (or 0) for an entity without a collider. */

/* False too for a radius below 0. */
WGF_API bool wgf_collider_set_radius(wgf_entity_t entity, float radius);
WGF_API float wgf_collider_get_radius(wgf_entity_t entity);

/* Bits: what it is, and what it meets. */
WGF_API bool wgf_collider_set_layer(wgf_entity_t entity, int layer);
WGF_API int wgf_collider_get_layer(wgf_entity_t entity);
WGF_API bool wgf_collider_set_mask(wgf_entity_t entity, int mask);
WGF_API int wgf_collider_get_mask(wgf_entity_t entity);

/* What it overlaps as of the last tick, into `out`, as many as fit in `count`, returning
 * how many it filled. */
WGF_API int wgf_collider_get_overlaps(wgf_entity_t entity, wgf_entity_t *out, int count);

#ifdef __cplusplus
}
#endif

#endif
