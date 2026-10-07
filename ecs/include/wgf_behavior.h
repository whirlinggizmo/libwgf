#ifndef WGF_BEHAVIOR_H
#define WGF_BEHAVIOR_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Behaviors: the program's own code on an actor, named here and written in the program's
 * language. An actor has any number, several of one name too, each its own parameters.
 * libwgf keeps each behavior's name and parameters (from a scene file, or set) and raises
 * its lifecycle as events (wgf_ecs.h): CREATED when it is added, DESTROYED when it is
 * removed or its actor goes, and its actor's colliders' triggers. The program's binding
 * takes those and calls its behaviors' create, destroy, and trigger enter and exit, and
 * their tick and frame from its own: nothing in C calls a behavior.
 *
 * A behavior is named by its actor and its id: a number from 1 the actor gives it as it is
 * added, kept until it is removed, never given to another of the actor's behaviors. A name
 * is 1 to 63 bytes; a parameter's key 1 to 63 bytes and its value, text, under 256; at
 * most 32 parameters. Every call is false (or 0, or "") for an actor or id that isn't one. */

/* A behavior named `name` added to `actor`, after the ones it has: its id, or 0 when
 * `actor` isn't an actor, the name breaks the rule above, or it is out of memory. */
WGF_API int wgf_actor_add_behavior(wgf_actor_t actor, const char *name);
/* The behavior removed (DESTROYED raised for it). */
WGF_API bool wgf_actor_remove_behavior(wgf_actor_t actor, int behavior);

/* The actor's behaviors in the order they were added: how many, and the id of the one at
 * `index` (0 past the end); and the id of its first named `name` (0 for none). */
WGF_API int wgf_actor_get_behavior_count(wgf_actor_t actor);
WGF_API int wgf_actor_get_behavior(wgf_actor_t actor, int index);
WGF_API int wgf_actor_find_behavior(wgf_actor_t actor, const char *name);

/* Its name: the actor's, valid while the behavior is. */
WGF_API const char *wgf_behavior_get_name(wgf_actor_t actor, int behavior);

/* A parameter, set to `value` (text), added the first time; NULL removes it. False too
 * for a key or value breaking the rule, or a 33rd parameter. get_param's is the actor's:
 * "" for one it doesn't have, valid until it changes. get_param_number reads it as a
 * number: 0 for one it doesn't have or that isn't one.
 *
 * A value starting with '@' refers to another actor, found once -- as it is set, and again
 * when a scene's actors are all made -- and kept: get_param_actor is it, never a search.
 * "@start_gate" is the one actor so named on this actor's stage (wgf_stage2d_find);
 * "@car/wheel_rl" a path from it (wgf_actor_find); "@./flame" a path from this actor and
 * "@../gun" from its parent. 0 for a parameter that isn't a reference, one that found
 * none, and an actor since destroyed. */
WGF_API bool wgf_behavior_set_param(wgf_actor_t actor, int behavior, const char *key, const char *value);
WGF_API const char *wgf_behavior_get_param(wgf_actor_t actor, int behavior, const char *key);
WGF_API double wgf_behavior_get_param_number(wgf_actor_t actor, int behavior, const char *key);
WGF_API wgf_actor_t wgf_behavior_get_param_actor(wgf_actor_t actor, int behavior, const char *key);
WGF_API bool wgf_behavior_has_param(wgf_actor_t actor, int behavior, const char *key);

/* Its parameters in the order first set: how many, and each one's key ("" past the
 * end). */
WGF_API int wgf_behavior_get_param_count(wgf_actor_t actor, int behavior);
WGF_API const char *wgf_behavior_get_param_key(wgf_actor_t actor, int behavior, int index);

#ifdef __cplusplus
}
#endif

#endif
