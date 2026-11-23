#ifndef WGF_BODY3D_H
#define WGF_BODY3D_H

#include <stdbool.h>

#include "wgf_actor.h"
#include "wgf_api.h"
#include "wgf_vec2.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A body (WGF_COMPONENT_BODY3D, wgf_component.h): an actor in the physics world
 * (wgf_physics3d.h), as a rigid shape.
 *
 * Defaults:
 * - dynamic
 * - a box of 1 by 1 by 1 meters
 * - a mass from its size (1000 kilograms a cubic meter)
 * - friction 0.5, no bounce, and damping of 0.05
 * - layer 1, and a mask of every layer
 *
 * Friction, bounce, and damping take effect at once, and the body's motion is kept. A car's
 * drag on grass can be set as it goes. Its other settings take effect at the next tick, when
 * the world's body is made again from them, at rest. Set them together, before the first
 * tick, as a scene does.
 *
 * The actor's transform is the body's:
 * - Each tick, the actor is set to where the world moved a dynamic body.
 * - A transform your game sets puts the body there. A static or dynamic body jumps there. A
 *   kinematic body is moved there over the tick, pushing the bodies it meets.
 *
 * A box's, sphere's, or capsule's size isn't scaled with its actor. A convex or mesh shape is
 * read from the models at and under the actor, as they are drawn. That includes the actor's
 * own scale and each model's place under it.
 *
 * The body's place is in its 3D stage's space, and the actor may be anywhere in the stage's
 * tree.
 *
 * The calls return false (or 0) for an actor without a body. */
typedef enum wgf_body3d_type_t {
    WGF_BODY3D_TYPE_STATIC = 0,    /* never moves, such as the track or the ground */
    WGF_BODY3D_TYPE_DYNAMIC = 1,   /* moved by the world: falling, pushed, a car */
    WGF_BODY3D_TYPE_KINEMATIC = 2, /* moved by your game, pushing dynamic bodies aside */
    WGF_BODY3D_TYPE_SENSOR = 3,    /* meets no body, but tells of overlaps, such as a checkpoint (see below) */
    WGF_BODY3D_TYPE_CHARACTER = 4  /* moved by its velocity, sliding and climbing, such as a player (see below) */
} wgf_body3d_type_t;

typedef enum wgf_body3d_shape_t {
    WGF_BODY3D_SHAPE_BOX = 0,     /* x, y, z: its size */
    WGF_BODY3D_SHAPE_SPHERE = 1,  /* x: its radius */
    WGF_BODY3D_SHAPE_CAPSULE = 2, /* x: its radius; y: its height end to end, along its y axis */
    WGF_BODY3D_SHAPE_CONVEX = 3,  /* the hull of the models at and under the actor */
    WGF_BODY3D_SHAPE_MESH = 4     /* the triangles of the models at and under the actor, for a static body (a track) */
} wgf_body3d_shape_t;

WGF_API bool wgf_body3d_set_type(wgf_actor_t actor, wgf_body3d_type_t type);
WGF_API wgf_body3d_type_t wgf_body3d_get_type(wgf_actor_t actor);

/* Sets or gets the body's shape, centered on the actor (default: a box of 1 by 1 by 1).
 *
 * - `x`, `y`, `z` mean what the shape says (see wgf_body3d_shape_t). Each is at least 0.01.
 * - CONVEX and MESH take no size. Their models' meshes are read when the body is made, and
 *   a model not yet loaded is left out.
 * - A mesh can only be static. On another type it is its convex hull.
 *
 * get_size returns `x`, `y`, `z` as set. */
WGF_API bool wgf_body3d_set_shape(wgf_actor_t actor, wgf_body3d_shape_t shape, float x, float y, float z);
WGF_API wgf_body3d_shape_t wgf_body3d_get_shape(wgf_actor_t actor);
WGF_API wgf_vec3_t wgf_body3d_get_size(wgf_actor_t actor);

/* Sets or gets the shape's offset and the center of mass's offset, in the actor's space
 * (default: 0, 0, 0 for each).
 *
 * - The offset is where the shape is centered from its actor. Use it for a gate's sensor on
 *   the road, with its actor at the gate's foot.
 * - The mass offset is how far the center of mass is from the shape's own. Set it low on a
 *   car, so it doesn't roll. */
WGF_API bool wgf_body3d_set_offset(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_body3d_get_offset(wgf_actor_t actor);
WGF_API bool wgf_body3d_set_mass_offset(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_body3d_get_mass_offset(wgf_actor_t actor);

/* Sets or gets a dynamic body's mass, in kilograms (default: 0). 0 takes the mass from its
 * shape, at 1000 kilograms a cubic meter. */
WGF_API bool wgf_body3d_set_mass(wgf_actor_t actor, float kilograms);
WGF_API float wgf_body3d_get_mass(wgf_actor_t actor);

/* Sets or gets friction and bounce. Each is taken for a pair of bodies together.
 *
 * - friction: 0 is ice and 1 is rubber (default: 0.5). It is clamped to 0 or more.
 * - bounce: 0 is none and 1 is all of it (default: 0). It is clamped to 0 to 1. */
WGF_API bool wgf_body3d_set_friction(wgf_actor_t actor, float friction);
WGF_API float wgf_body3d_get_friction(wgf_actor_t actor);
WGF_API bool wgf_body3d_set_bounce(wgf_actor_t actor, float bounce);
WGF_API float wgf_body3d_get_bounce(wgf_actor_t actor);

/* Sets or gets damping: the share of its velocity (linear) and of its spin (angular) the body
 * loses each second, 0 to 1 (default: 0.05). get_damping returns (linear, angular). */
WGF_API bool wgf_body3d_set_damping(wgf_actor_t actor, float linear, float angular);
WGF_API wgf_vec2_t wgf_body3d_get_damping(wgf_actor_t actor);

/* Sets or gets the body's layer and mask: what it meets (defaults: layer 1, mask every layer).
 *
 * Two bodies meet when either one's mask has a bit of the other's layer, as two colliders do
 * (wgf_collider.h). Meeting means they collide, or a sensor tells of the other.
 *
 * - Each is 15 bits (1 to 0x7FFF).
 * - Put the mask on the body that cares: a bullet whose mask has the enemies' layer meets
 *   them, whatever their masks are.
 * - To make two bodies pass through each other, leave each one's layer out of the other's
 *   mask. That goes for a moving body and a static one too.
 * - Two static bodies never meet. */
WGF_API bool wgf_body3d_set_layer(wgf_actor_t actor, int layer);
WGF_API int wgf_body3d_get_layer(wgf_actor_t actor);
WGF_API bool wgf_body3d_set_mask(wgf_actor_t actor, int mask);
WGF_API int wgf_body3d_get_mask(wgf_actor_t actor);

/* Sets or gets the body's velocity and spin, in its stage's space, or gives it a push.
 *
 * - The velocity is in units a second.
 * - The spin is in radians a second about each axis.
 * - add_impulse pushes it through its center, in kilogram units a second.
 *
 * A body not yet made in the world (before its first tick) reads 0, and takes them when it
 * is made. */
WGF_API bool wgf_body3d_set_velocity(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_body3d_get_velocity(wgf_actor_t actor);
WGF_API bool wgf_body3d_set_spin(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_body3d_get_spin(wgf_actor_t actor);
WGF_API bool wgf_body3d_add_impulse(wgf_actor_t actor, float x, float y, float z);

/* Returns whether a character is standing on ground.
 *
 * A character (Jolt's CharacterVirtual) is moved by its velocity each tick. Use it for a
 * player, or an enemy that walks.
 * - It slides along walls and slopes.
 * - It climbs steps up to its step height, and is kept on the floor going down them.
 * - It pushes dynamic bodies.
 * - It stands on ground no steeper than its max slope.
 *
 * Its velocity is its own. What you set stays, as for a walk. Its up part falls under the
 * world's gravity whenever it isn't standing, and is 0 on the ground. A jump sets it upward
 * once.
 *
 * Its turn is its actor's. A transform your game sets puts it there. Rays, sensors, and
 * queries find it as a body of its shape. A capsule is its usual shape, with its center at the
 * actor. */
WGF_API bool wgf_body3d_is_grounded(wgf_actor_t actor);
/* Returns the normal of the ground a character stands on. Returns 0, 0, 0 when it isn't
 * standing, or isn't a character. */
WGF_API wgf_vec3_t wgf_body3d_get_ground_normal(wgf_actor_t actor);
/* Sets or gets the height of the steps a character climbs (default: 0.3). It takes effect at
 * once. The setter refuses a negative or non-finite height. */
WGF_API bool wgf_body3d_set_step_height(wgf_actor_t actor, float height);
WGF_API float wgf_body3d_get_step_height(wgf_actor_t actor);
/* Sets or gets the steepest ground a character stands on, in radians from level (default: 50
 * degrees). It takes effect at the next tick. The setter refuses a slope outside 0 to a right
 * angle. */
WGF_API bool wgf_body3d_set_max_slope(wgf_actor_t actor, float radians);
WGF_API float wgf_body3d_get_max_slope(wgf_actor_t actor);

/* Contacts and sensors raise world events (wgf_world.h).
 *
 * A contact is told for two solid bodies that begin to touch, one of them moving: dynamic,
 * kinematic, or a character.
 * - CONTACT_BEGIN is told to each, with the other's layer.
 * - CONTACT_END is told when they stop touching, or one is destroyed.
 * - A behavior hears it as onContactBegin.
 *
 * A sensor tells of overlaps, as a collider does (wgf_collider.h).
 * - TRIGGER_ENTER is told when a moving body it may meet begins to overlap it.
 * - TRIGGER_EXIT is told when it stops overlapping, or is destroyed.
 * - Each is told to both, with the other's layer.
 * - A behavior hears it as onTriggerEnter. */

#ifdef __cplusplus
}
#endif

#endif
