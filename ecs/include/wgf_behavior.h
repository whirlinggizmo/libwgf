#ifndef WGF_BEHAVIOR_H
#define WGF_BEHAVIOR_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A behavior (WGF_COMPONENT_BEHAVIOR): the program's own code for an entity, named here
 * and written in the program's language. libwgf keeps its name and its parameters (from
 * a scene file, or set) and raises its lifecycle as events (wgf_ecs.h): CREATED when it
 * is added, DESTROYED when its entity goes, the trigger events for its collider. The
 * program's binding takes those and calls its behaviors' create, destroy, and trigger
 * enter and exit, and their tick and frame from its own. A name is 1 to 63 bytes; a
 * parameter's key 1 to 63 bytes and its value, text, under 256; at most 32 parameters.
 * Every call is false (or 0, or "") for an entity without a behavior. */

/* Default: "" (none: no code). Setting a name doesn't raise CREATED again. False too
 * for a name breaking the rule above. get_name's is the entity's, valid until it
 * changes. */
WGF_API bool wgf_behavior_set_name(wgf_entity_t entity, const char *name);
WGF_API const char *wgf_behavior_get_name(wgf_entity_t entity);

/* A parameter, set to `value` (text), added the first time; NULL removes it. False too
 * for a key or value breaking the rule, or a 33rd parameter. get_param's is the
 * entity's: "" for one it doesn't have, valid until it changes. get_param_number reads
 * it as a number: 0 for one it doesn't have or that isn't one. */
WGF_API bool wgf_behavior_set_param(wgf_entity_t entity, const char *key, const char *value);
WGF_API const char *wgf_behavior_get_param(wgf_entity_t entity, const char *key);
WGF_API double wgf_behavior_get_param_number(wgf_entity_t entity, const char *key);
WGF_API bool wgf_behavior_has_param(wgf_entity_t entity, const char *key);

/* Its parameters in the order first set: how many, and each one's key ("" past the
 * end). */
WGF_API int wgf_behavior_get_param_count(wgf_entity_t entity);
WGF_API const char *wgf_behavior_get_param_key(wgf_entity_t entity, int index);

#ifdef __cplusplus
}
#endif

#endif
