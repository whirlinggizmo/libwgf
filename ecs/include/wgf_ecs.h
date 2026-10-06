#ifndef WGF_ECS_H
#define WGF_ECS_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The entity system as a whole: libwgf's systems, the events they raise, finding
 * entities, and the world written out as text.
 *
 * Each tick, after the program's, the systems run in this order: lifetimes count down
 * (an entity whose time is up destroyed), motion moves each entity by its velocity and
 * spin (damped, its speed capped), bounds wrap, clamp, or destroy what has left its
 * rectangle, and colliders find the overlaps. Each frame, before the program's frame,
 * every entity's node is given its transform interpolated between the last two ticks.
 *
 * Events are what the program's behaviors are told (wgf_behavior.h), queued as they
 * happen and taken by the program, in order: a behavior added (CREATED); an entity with
 * a behavior destroyed (DESTROYED, its handle stale by then: a name for what it was); two
 * colliders starting to overlap (TRIGGER_ENTER) and ceasing to (TRIGGER_EXIT), told to
 * each of the two, the other its `other`. An entity destroyed while it overlaps another
 * raises no TRIGGER_EXIT. Nothing is called back: a binding takes them at its tick's
 * start and calls its behaviors itself. At most 65536 wait; past that the oldest are
 * dropped, warned once. */
typedef enum wgf_ecs_event_t {
    WGF_ECS_EVENT_NONE = 0,
    WGF_ECS_EVENT_CREATED = 1,
    WGF_ECS_EVENT_DESTROYED = 2,
    WGF_ECS_EVENT_TRIGGER_ENTER = 3,
    WGF_ECS_EVENT_TRIGGER_EXIT = 4
} wgf_ecs_event_t;

/* Events waiting, and the oldest taken off the queue into `out`, three ints each -- the
 * event, the entity, the other entity (0 for none) -- as many whole events as fit in
 * `count` ints, returning how many ints it filled. */
WGF_API int wgf_ecs_get_event_count(void);
WGF_API int wgf_ecs_take_events(int *out, int count);

/* The live entities with the behavior `name`, oldest first, into `out`, as many as fit in
 * `count`, returning how many it filled; and how many there are, to size `out`. */
WGF_API int wgf_ecs_find_behavior(const char *name, wgf_entity_t *out, int count);
WGF_API int wgf_ecs_count_behavior(const char *name);

/* Every live entity destroyed, as wgf_entity_destroy would, oldest first. */
WGF_API void wgf_ecs_clear(void);

/* The world as a scene's text (wgf_scene.h's format): every live entity, oldest first,
 * with its transform and every component as it is now, so loading it as a scene and
 * instantiating it makes the same world again. libwgf's to keep: valid until the next
 * dump. "" when the ecs hasn't started (no entity was ever made). */
WGF_API const char *wgf_ecs_dump(void);

#ifdef __cplusplus
}
#endif

#endif
