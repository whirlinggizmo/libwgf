#ifndef WGF_MOTION_H
#define WGF_MOTION_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Motion (WGF_COMPONENT_MOTION): moves the actor by its velocity and turns it by its spin,
 * each tick.
 *
 * Each tick its velocity is first damped, and capped at its top speed.
 *
 * Defaults: still, no damping, and no top speed.
 *
 * Every call returns false (or 0) for an actor without motion. */

/* Sets or gets the velocity, in units a second, in the actor's parent's space. */
WGF_API bool wgf_motion_set_velocity(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_motion_get_velocity(wgf_actor_t actor);

/* Sets or gets the spin, in radians a second about each axis (in 2D, about z alone).
 *
 * Each tick the actor turns by the tick's share, about its parent's axes. A spin about one
 * axis turns it steadily, past any half turn. */
WGF_API bool wgf_motion_set_spin(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_motion_get_spin(wgf_actor_t actor);

/* Sets or gets the damping: the part of its velocity the actor loses each second
 * (default: 0). 0 loses none and 1 loses all of it. It is clamped to 0 to 1. */
WGF_API bool wgf_motion_set_damping(wgf_actor_t actor, float damping);
WGF_API float wgf_motion_get_damping(wgf_actor_t actor);

/* Sets or gets the top speed, in units a second (default: 0, no limit). 0 means no limit.
 * It is clamped to 0 or more. */
WGF_API bool wgf_motion_set_max_speed(wgf_actor_t actor, float speed);
WGF_API float wgf_motion_get_max_speed(wgf_actor_t actor);

#ifdef __cplusplus
}
#endif

#endif
