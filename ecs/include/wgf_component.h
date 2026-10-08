#ifndef WGF_COMPONENT_H
#define WGF_COMPONENT_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_actor.h"
#include "wgf_voice.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Components: the simulation's data on an actor -- any actor, of any kind, a 2D stage's UI
 * actors and cameras included -- run each tick by libwgf's systems (wgf_world.h). One kind
 * of object, the actor (SPEC.md), Godot's tree with Unity's components: what an actor draws
 * is its kind (a shape, a sprite, a model); what moves it, bounds it, ages it, and finds
 * what it overlaps are components on it; what the program does with it are its behaviors
 * (wgf_behavior.h), several to an actor.
 *
 * An actor with a component or a behavior is simulated (wgf_actor_snap): its transform is
 * set and read at the tick rate, and it is drawn between its last two ticks, so it moves
 * smoothly at any frame rate. Its components go when it does (wgf_actor_destroy), and a
 * system that ends it (a lifetime run out, bounds of DESTROY) destroys the actor, with
 * everything under it, after the tick's systems have run. */
typedef enum wgf_component_t {
    WGF_COMPONENT_NONE = 0,
    WGF_COMPONENT_MOTION = 1,   /* velocity, spin, damping (wgf_motion.h) */
    WGF_COMPONENT_BOUNDS = 2,   /* a rectangle it wraps around, is clamped to, or dies outside (wgf_bounds.h) */
    WGF_COMPONENT_LIFETIME = 3, /* seconds until it is destroyed (wgf_lifetime.h) */
    WGF_COMPONENT_COLLIDER = 4, /* a circle that overlaps others, as triggers (wgf_collider.h) */
    WGF_COMPONENT_VOICE = 5,    /* a voice of no sound yet (wgf_voice.h), destroyed with the actor */
    WGF_COMPONENT_BODY = 6,     /* a rigid body in the physics world (wgf_body.h): physics started first */
    WGF_COMPONENT_VEHICLE = 7   /* wheels, an engine, and a gearbox on a body (wgf_vehicle.h): physics started first */
} wgf_component_t;

/* add makes one with its defaults (each component's header says them); adding one it has
 * keeps it as it is. remove ends it (a voice stopped and destroyed). False for a handle
 * that isn't an actor, or a component that isn't one; remove is false too for one it
 * doesn't have. A body or a vehicle is physics' (wgf_physics.h), an optional part: added
 * before physics has started, it is refused, and logged once. */
WGF_API bool wgf_actor_add_component(wgf_actor_t actor, wgf_component_t component);
WGF_API bool wgf_actor_remove_component(wgf_actor_t actor, wgf_component_t component);
WGF_API bool wgf_actor_has_component(wgf_actor_t actor, wgf_component_t component);

/* The voice component's voice; 0 for an actor without one. The actor's: play it, never
 * destroy it. */
WGF_API wgf_voice_t wgf_actor_get_voice(wgf_actor_t actor);

/* The actors with a component, oldest first (by when their first component or behavior
 * came), into `out`, as many as fit in `count`, returning how many it filled; and how many
 * there are, to size `out`. Found by an index of each component, never by a look at every
 * actor; NONE, or a value that isn't a component, finds none. */
WGF_API int wgf_actor_find_with_component(wgf_component_t component, wgf_actor_t *out, int count);
WGF_API int wgf_actor_count_with_component(wgf_component_t component);

#ifdef __cplusplus
}
#endif

#endif
