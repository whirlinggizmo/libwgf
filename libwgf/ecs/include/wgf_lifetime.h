#ifndef WGF_LIFETIME_H
#define WGF_LIFETIME_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A lifetime (WGF_COMPONENT_LIFETIME): seconds left, counted down each tick; at 0 the
 * actor is destroyed. Default: 1 second. */

/* Seconds left from now. False for an actor without a lifetime, or a time below 0 or
 * not finite. */
WGF_API bool wgf_lifetime_set_seconds(wgf_actor_t actor, float seconds);
/* Seconds left; 0 for an actor without a lifetime. */
WGF_API float wgf_lifetime_get_seconds(wgf_actor_t actor);

#ifdef __cplusplus
}
#endif

#endif
