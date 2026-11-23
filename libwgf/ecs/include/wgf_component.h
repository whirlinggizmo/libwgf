#ifndef WGF_COMPONENT_H
#define WGF_COMPONENT_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_actor.h"
#include "wgf_voice.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Components: the simulation's data on an actor, run each tick by libwgf's systems
 * (wgf_world.h).
 *
 * Any actor of any kind can carry components, a 2D stage's UI actors and cameras included.
 * libwgf has one kind of object, the actor (SPEC.md), arranged like Godot's tree and given
 * components like Unity's:
 * - What an actor draws is its kind: a shape, a sprite, a model.
 * - What moves it, bounds it, ages it, and finds what it overlaps are its components.
 * - What your game does with it are its behaviors (wgf_behavior.h), several to an actor.
 *
 * An actor with a component or a behavior is simulated (see wgf_actor_snap). Its transform is
 * set and read at the tick rate, and it is drawn between its last two ticks, so it moves
 * smoothly at any frame rate.
 *
 * An actor's components are destroyed with it (wgf_actor_destroy). A system that ends an
 * actor, such as a lifetime run out or bounds of DESTROY, destroys it and every actor under
 * it, after the tick's systems have run. */
typedef enum wgf_component_t {
    WGF_COMPONENT_NONE = 0,
    WGF_COMPONENT_MOTION = 1,   /* velocity, spin, and damping (wgf_motion.h) */
    WGF_COMPONENT_BOUNDS = 2,   /* a rectangle the actor wraps around, is clamped to, or dies outside (wgf_bounds.h) */
    WGF_COMPONENT_LIFETIME = 3, /* seconds until the actor is destroyed (wgf_lifetime.h) */
    WGF_COMPONENT_COLLIDER = 4, /* a shape that overlaps other colliders, as triggers (wgf_collider.h) */
    WGF_COMPONENT_VOICE = 5,    /* a voice with no sound yet (wgf_voice.h), destroyed with the actor, ending a loop */
    WGF_COMPONENT_BODY3D = 6,     /* a rigid body in the physics world (wgf_body3d.h); start physics first */
    WGF_COMPONENT_VEHICLE = 7,  /* wheels, an engine, and a gearbox on a body (wgf_vehicle.h); start physics first */
    WGF_COMPONENT_ARCADE2D = 8    /* the arcade body: a box that moves and slides against solids (wgf_arcade2d.h) */
} wgf_component_t;

/* Adds, removes, or checks for a component on an actor.
 *
 * - add_component adds one with its defaults, which each component's header lists. Adding a
 *   component the actor already has keeps it as it is.
 * - remove_component ends it. A voice is stopped and destroyed.
 *
 * Returns false for a handle that isn't an actor, or a component that isn't one.
 * remove_component also returns false for a component the actor doesn't have.
 *
 * A body or a vehicle belongs to physics (wgf_physics3d.h), an optional part. Adding one
 * before physics has started is refused (logged once). */
WGF_API bool wgf_actor_add_component(wgf_actor_t actor, wgf_component_t component);
WGF_API bool wgf_actor_remove_component(wgf_actor_t actor, wgf_component_t component);
WGF_API bool wgf_actor_has_component(wgf_actor_t actor, wgf_component_t component);

/* Returns the voice component's voice, or 0 for an actor without one.
 *
 * The actor owns the voice: play it, but never destroy it. Use it for a sound that lasts as
 * long as the actor, such as a car's engine or a ship's thrust, looping. It stops when the
 * actor is destroyed, with nothing for your game to end. For a sound played once, use
 * wgf_sound_play. */
WGF_API wgf_voice_t wgf_actor_get_voice(wgf_actor_t actor);

/* Finds the actors with a component, oldest first.
 *
 * An actor's age is when its first component or behavior was added. find_with_component fills
 * `out` with as many as fit in `count`, and returns how many it filled. count_with_component
 * returns how many there are, to size `out`. Each component is indexed, so neither call looks
 * at every actor. NONE, or a value that isn't a component, finds none. */
WGF_API int wgf_actor_find_with_component(wgf_component_t component, wgf_actor_t *out, int count);
WGF_API int wgf_actor_count_with_component(wgf_component_t component);

#ifdef __cplusplus
}
#endif

#endif
