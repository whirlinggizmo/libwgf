#ifndef WGF_BEHAVIOR_H
#define WGF_BEHAVIOR_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Behaviors: your game's own code on an actor, named here and written in your game's
 * language.
 *
 * An actor can have any number of behaviors, several with the same name too, each with its
 * own parameters. libwgf keeps each behavior's name and parameters, from a scene file or set
 * in code. It raises the behavior's lifecycle as events (wgf_world.h):
 * - CREATED when the behavior is added.
 * - DESTROYED when it is removed, or its actor is destroyed.
 * - The triggers of its actor's colliders.
 *
 * Your game's binding takes those events and calls its behaviors' create, destroy, trigger
 * enter, and trigger exit. It calls their tick and frame from its own. Nothing in C calls a
 * behavior.
 *
 * A behavior on a paused actor (wgf_actor_set_paused, as under a pause screen) gets no tick
 * and no frame until the actor runs again.
 *
 * A behavior is named by its actor and its id. The id is a number from 1 that the actor gives
 * it when it is added. The behavior keeps it until it is removed, and the actor never gives it
 * to another of its behaviors.
 *
 * Limits:
 * - A name is 1 to 63 bytes.
 * - A parameter's key is 1 to 63 bytes, and its value is text under 256 bytes.
 * - A behavior has at most 32 parameters.
 *
 * Every call returns false (or 0, or "") for an actor or id that isn't one. */

/* Adds a behavior named `name` to `actor`, after the ones it has, and returns its id.
 * Returns 0 if `actor` isn't an actor, the name breaks the limits above, or memory runs
 * out. */
WGF_API int wgf_actor_add_behavior(wgf_actor_t actor, const char *name);
/* Removes a behavior, raising DESTROYED for it. */
WGF_API bool wgf_actor_remove_behavior(wgf_actor_t actor, int behavior);

/* Lists the actor's behaviors, in the order they were added.
 *
 * - get_behavior_count returns how many there are.
 * - get_behavior returns the id of the one at `index`, or 0 past the end.
 * - find_behavior returns the id of the first one named `name`, or 0 for none. */
WGF_API int wgf_actor_get_behavior_count(wgf_actor_t actor);
WGF_API int wgf_actor_get_behavior(wgf_actor_t actor, int index);
WGF_API int wgf_actor_find_behavior(wgf_actor_t actor, const char *name);

/* Returns the behavior's name. The actor owns the string, valid while the behavior is. */
WGF_API const char *wgf_behavior_get_name(wgf_actor_t actor, int behavior);

/* Sets or gets a parameter, as text.
 *
 * - set_param sets `key` to `value`, adding the parameter the first time. A NULL `value`
 *   removes it. It also returns false for a key or value that breaks the limits, or a 33rd
 *   parameter.
 * - get_param returns the value. The actor owns the string, valid until it changes. It
 *   returns "" for a parameter the behavior doesn't have.
 * - get_param_number reads the value as a number. It returns 0 for a parameter the behavior
 *   doesn't have, or one that isn't a number.
 *
 * A value starting with '@' refers to another actor. The actor is found once, when the value
 * is set, and again when all of a scene's actors are made. It is then kept, so
 * get_param_actor returns it without a search.
 * - "@start_gate" is the one actor with that name on this actor's stage (wgf_stage2d_find).
 * - "@car/wheel_rl" is a path from that actor (wgf_actor_find).
 * - "@./flame" is a path from this actor, and "@../gun" a path from its parent.
 *
 * get_param_actor returns 0 for a parameter that isn't a reference, a reference that found no
 * actor, or an actor since destroyed. */
WGF_API bool wgf_behavior_set_param(wgf_actor_t actor, int behavior, const char *key, const char *value);
WGF_API const char *wgf_behavior_get_param(wgf_actor_t actor, int behavior, const char *key);
WGF_API double wgf_behavior_get_param_number(wgf_actor_t actor, int behavior, const char *key);
WGF_API wgf_actor_t wgf_behavior_get_param_actor(wgf_actor_t actor, int behavior, const char *key);
WGF_API bool wgf_behavior_has_param(wgf_actor_t actor, int behavior, const char *key);

/* Lists the behavior's parameters, in the order they were first set. get_param_count returns
 * how many there are, and get_param_key returns each one's key, or "" past the end. */
WGF_API int wgf_behavior_get_param_count(wgf_actor_t actor, int behavior);
WGF_API const char *wgf_behavior_get_param_key(wgf_actor_t actor, int behavior, int index);

/* Finds the actors with a behavior named `name`, oldest first.
 *
 * An actor's age is when its first component or behavior was added. find_with_behavior fills
 * `out` with as many as fit in `count`, and returns how many it filled. count_with_behavior
 * returns how many there are, to size `out`. An actor with two behaviors of that name counts
 * once. Each name is indexed, so neither call looks at every actor. */
WGF_API int wgf_actor_find_with_behavior(const char *name, wgf_actor_t *out, int count);
WGF_API int wgf_actor_count_with_behavior(const char *name);

#ifdef __cplusplus
}
#endif

#endif
