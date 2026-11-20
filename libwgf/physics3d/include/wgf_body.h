#ifndef WGF_BODY_H
#define WGF_BODY_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_vec2.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A body (WGF_COMPONENT_BODY, wgf_component.h): an actor in the physics world
 * (wgf_physics.h), as a rigid shape. Defaults: dynamic, a box of 1 by 1 by 1, its mass from
 * its size (1000 kg a cubic meter), friction 0.5, no bounce, damping of 0.05, layer 1,
 * mask every layer. Friction, bounce, and damping take effect at once, its motion kept (a
 * car's drag on grass, set as it goes); its other settings take effect at the next tick,
 * when the world's body is made again from them, at rest: set them together, before the
 * first tick, as a scene does.
 *
 * The actor's transform is the body's: where the world moves a dynamic body, each tick,
 * the actor is set; a transform the program sets puts the body there (a static or
 * dynamic one jumps; a kinematic one is moved there over the tick, pushing what it meets).
 * A box's, sphere's, or capsule's size isn't scaled with its actor; a convex or mesh shape
 * is read from the models at and under the actor, as they are drawn: the actor's own scale
 * and each model's place under it included. Its place is in its 3D
 * stage's space, the actor anywhere in the stage's tree. The calls are false (or 0) for an actor without a
 * body. */
typedef enum wgf_body_type_t {
    WGF_BODY_TYPE_STATIC = 0,    /* never moves: the track, the ground */
    WGF_BODY_TYPE_DYNAMIC = 1,   /* moved by the world: falling, pushed, a car */
    WGF_BODY_TYPE_KINEMATIC = 2, /* moved by the program, pushing dynamic ones aside */
    WGF_BODY_TYPE_SENSOR = 3,    /* meets nothing, but tells: a checkpoint (below) */
    WGF_BODY_TYPE_CHARACTER = 4  /* moved by its velocity, sliding and climbing: a player (below) */
} wgf_body_type_t;

typedef enum wgf_body_shape_t {
    WGF_BODY_SHAPE_BOX = 0,     /* x, y, z: its size */
    WGF_BODY_SHAPE_SPHERE = 1,  /* x: its radius */
    WGF_BODY_SHAPE_CAPSULE = 2, /* x: its radius, y: its height end to end, along its y */
    WGF_BODY_SHAPE_CONVEX = 3,  /* the hull of the models at and under the actor */
    WGF_BODY_SHAPE_MESH = 4     /* the triangles of the models at and under the actor: a static's (a track) */
} wgf_body_shape_t;

WGF_API bool wgf_body_set_type(wgf_actor_t actor, wgf_body_type_t type);
WGF_API wgf_body_type_t wgf_body_get_type(wgf_actor_t actor);

/* Its shape, centered on the actor; x, y, z as the shape says (wgf_body_shape_t), each
 * at least 0.01, and nothing for CONVEX and MESH, whose models' meshes are read when
 * the body is made (a model not yet loaded is left out). get_size is x, y, z as set. A
 * mesh can only be static: on another type it is its convex hull. */
WGF_API bool wgf_body_set_shape(wgf_actor_t actor, wgf_body_shape_t shape, float x, float y, float z);
WGF_API wgf_body_shape_t wgf_body_get_shape(wgf_actor_t actor);
WGF_API wgf_vec3_t wgf_body_get_size(wgf_actor_t actor);

/* Where its shape is centered from its actor (a gate's sensor on the road, its actor at
 * the gate's foot), and how far its center of mass is from its shape's own (a car's, low,
 * so it doesn't roll): each 0, 0, 0 by default, in the actor's space. */
WGF_API bool wgf_body_set_offset(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_body_get_offset(wgf_actor_t actor);
WGF_API bool wgf_body_set_mass_offset(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_body_get_mass_offset(wgf_actor_t actor);

/* Kilograms, for a dynamic body; 0 (the default) for its shape's at 1000 a cubic meter. */
WGF_API bool wgf_body_set_mass(wgf_actor_t actor, float kilograms);
WGF_API float wgf_body_get_mass(wgf_actor_t actor);

/* Friction (0 ice, 1 rubber; clamped to 0 or more) and bounce (0 none, 1 all of it;
 * clamped to 0 to 1), each a pair's taken together. */
WGF_API bool wgf_body_set_friction(wgf_actor_t actor, float friction);
WGF_API float wgf_body_get_friction(wgf_actor_t actor);
WGF_API bool wgf_body_set_bounce(wgf_actor_t actor, float bounce);
WGF_API float wgf_body_get_bounce(wgf_actor_t actor);

/* The share of its velocity and of its spin it loses each second, 0 to 1: get_damping is
 * (linear, angular). */
WGF_API bool wgf_body_set_damping(wgf_actor_t actor, float linear, float angular);
WGF_API wgf_vec2_t wgf_body_get_damping(wgf_actor_t actor);

/* What it meets: two bodies meet (collide, or a sensor tells of the other) when each
 * one's layer has a bit in the other's mask. 15 bits each (1 to 0x7FFF); a static
 * body meets every moving one its mask lets, and every moving one meets the static world. */
WGF_API bool wgf_body_set_layer(wgf_actor_t actor, int layer);
WGF_API int wgf_body_get_layer(wgf_actor_t actor);
WGF_API bool wgf_body_set_mask(wgf_actor_t actor, int mask);
WGF_API int wgf_body_get_mask(wgf_actor_t actor);

/* Its velocity (units a second) and spin (radians a second about each axis), in its
 * stage's space; and a push, an impulse (kg units a second) through its center. A body not
 * yet made in the world (before its first tick) reads 0 and takes them when it is. */
WGF_API bool wgf_body_set_velocity(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_body_get_velocity(wgf_actor_t actor);
WGF_API bool wgf_body_set_spin(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_body_get_spin(wgf_actor_t actor);
WGF_API bool wgf_body_add_impulse(wgf_actor_t actor, float x, float y, float z);

/* A character (Jolt's CharacterVirtual) is moved by its velocity each tick, sliding along
 * walls and slopes, climbing steps up to its step height and kept on the floor going down
 * them, pushing dynamic bodies, standing on ground no steeper than its max slope: a
 * player, an enemy that walks. Its velocity is its own: what is set stays across (a walk),
 * while its up part falls under the world's gravity whenever it isn't standing, and is 0
 * on the ground; a jump sets it upward once. Its turn is its actor's; a transform the
 * program sets puts it there. Rays, sensors, and queries find it as a body of its shape.
 * A capsule is its usual shape, its center at the actor. */
WGF_API bool wgf_body_is_grounded(wgf_actor_t actor);
/* The ground it stands on's normal; 0, 0, 0 when it isn't standing, or isn't a character. */
WGF_API wgf_vec3_t wgf_body_get_ground_normal(wgf_actor_t actor);
/* The steps it climbs (default 0.3), at once. Refuses a negative or non-finite one. */
WGF_API bool wgf_body_set_step_height(wgf_actor_t actor, float height);
WGF_API float wgf_body_get_step_height(wgf_actor_t actor);
/* The steepest ground it stands on, in radians from level (default 50 degrees), at the
 * next tick. Refuses one outside 0 to a right angle. */
WGF_API bool wgf_body_set_max_slope(wgf_actor_t actor, float radians);
WGF_API float wgf_body_get_max_slope(wgf_actor_t actor);

/* A contact is told for two solid bodies that begin to touch, one of them moving (dynamic,
 * kinematic, or a character's): a CONTACT_BEGIN to each, with the other's layer, and a
 * CONTACT_END when they stop (or one goes) (wgf_world.h); a behavior's onContactBegin.
 *
 * A sensor tells, as a collider does (wgf_collider.h, wgf_world.h): a TRIGGER_ENTER when a
 * moving body it may meet begins to overlap it, and a TRIGGER_EXIT when it stops (or goes),
 * each to both, with the other's layer; a behavior's onTriggerEnter. */

#ifdef __cplusplus
}
#endif

#endif
