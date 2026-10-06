#ifndef WGF_MOTION_H
#define WGF_MOTION_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_entity.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Motion (WGF_COMPONENT_MOTION): each tick the entity moves by its velocity and turns by
 * its spin, its velocity first damped and capped at its top speed. Defaults: still, no
 * damping, no top speed. Every call is false (or 0) for an entity without motion. */

/* Units a second, in its parent's space. */
WGF_API bool wgf_motion_set_velocity(wgf_entity_t entity, float x, float y, float z);
WGF_API wgf_vec3_t wgf_motion_get_velocity(wgf_entity_t entity);

/* Radians a second about each axis (in 2D, z alone), added to its rotation's angles. */
WGF_API bool wgf_motion_set_spin(wgf_entity_t entity, float x, float y, float z);
WGF_API wgf_vec3_t wgf_motion_get_spin(wgf_entity_t entity);

/* The part of its velocity it loses each second: 0 none, 1 all of it; clamped to that. */
WGF_API bool wgf_motion_set_damping(wgf_entity_t entity, float damping);
WGF_API float wgf_motion_get_damping(wgf_entity_t entity);

/* The most its speed can be, in units a second; 0 for no limit. Clamped to 0 or more. */
WGF_API bool wgf_motion_set_max_speed(wgf_entity_t entity, float speed);
WGF_API float wgf_motion_get_max_speed(wgf_entity_t entity);

#ifdef __cplusplus
}
#endif

#endif
