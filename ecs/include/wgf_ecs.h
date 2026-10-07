#ifndef WGF_ECS_H
#define WGF_ECS_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_component.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The simulation as a whole: libwgf's systems over the actors' components
 * (wgf_component.h), the events they raise, finding actors by behavior or component, and the simulated
 * actors written out as text.
 *
 * Each tick, as it begins, every simulated actor's transform is kept as it is; after the
 * program's tick the systems run in this order: lifetimes count down (an actor whose time is
 * up destroyed), motion moves each actor by its velocity and spin (damped, its speed
 * capped), bounds wrap, clamp, or destroy what has left its rectangle, and colliders find
 * the overlaps. Each frame a simulated actor is drawn between its last two ticks'
 * transforms.
 *
 * Events are what the program's behaviors are told (wgf_behavior.h), queued as they
 * happen and taken by the program, in order, four ints each (the event and three more):
 *
 *   CREATED          a behavior added: its actor, its id, 0
 *   DESTROYED        a behavior removed, or its actor gone: its actor (stale by then, a name
 *                    for what it was), its id, 0
 *   TRIGGER_ENTER    two colliders starting to overlap, told to each: the actor, the other,
 *                    and the other's collider layer as they met
 *   TRIGGER_EXIT     and ceasing to
 *
 * An actor destroyed while it overlaps another raises no TRIGGER_EXIT. Events taken in a
 * batch can name an actor the program destroyed while handling an earlier one of the
 * batch: a binding's runtime drops a trigger whose actor or other is gone by then. Nothing is called
 * back: a binding takes them at its tick's start and calls its behaviors itself. At most
 * 65536 wait; past that the oldest are dropped, warned once. */
typedef enum wgf_ecs_event_t {
    WGF_ECS_EVENT_NONE = 0,
    WGF_ECS_EVENT_CREATED = 1,
    WGF_ECS_EVENT_DESTROYED = 2,
    WGF_ECS_EVENT_TRIGGER_ENTER = 3,
    WGF_ECS_EVENT_TRIGGER_EXIT = 4
} wgf_ecs_event_t;

/* Events waiting, and the oldest taken off the queue into `out`, four ints each (the
 * event, then the three above), as many whole events as fit in `count` ints, returning how
 * many ints it filled. */
WGF_API int wgf_ecs_get_event_count(void);
WGF_API int wgf_ecs_take_events(int *out, int count);

/* How many actors have a component or a behavior. */
WGF_API int wgf_ecs_get_count(void);

/* The actors with a behavior named `name`, oldest first (by when their first component or
 * behavior came), into `out`, as many as fit in `count`, returning how many it filled;
 * and how many there are, to size `out`. An actor with two of the name is one actor.
 * Found by an index of each name and component, never by a look at every actor. */
WGF_API int wgf_ecs_find_behavior(const char *name, wgf_actor_t *out, int count);
WGF_API int wgf_ecs_count_behavior(const char *name);

/* The actors with a component, oldest first, the same way; NONE, or a value that isn't a
 * component, finds none. */
WGF_API int wgf_ecs_find_component(wgf_component_t component, wgf_actor_t *out, int count);
WGF_API int wgf_ecs_count_component(wgf_component_t component);

/* Every actor with a component or a behavior destroyed, with everything under it, oldest
 * first. */
WGF_API void wgf_ecs_clear(void);

/* The simulated actors as a scene's text (wgf_scene.h's format): each top one -- an actor
 * with a component or a behavior whose parent has none -- oldest first, as a `actor`
 * block, with its kind, its transform, its components, its behaviors, and the actors under
 * it, as they are now, so loading it as a scene and instantiating it makes the same actors
 * again. libwgf's to keep: valid until the next dump. "" when no actor ever had a
 * component or a behavior. */
WGF_API const char *wgf_ecs_dump(void);

#ifdef __cplusplus
}
#endif

#endif
