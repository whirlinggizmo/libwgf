#ifndef WGF_ACTOR_H
#define WGF_ACTOR_H

#include <stdbool.h>

#include "wgf_handle.h"
#include "wgf_api.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Actors: everything placed in a scene tree.
 *
 * A plain actor is only a transform, used as a group or a pivot. The other kinds are actors
 * with content: 2D stages, sprites, shapes, text, particle emitters, and cameras. Each is made
 * by its own create call, and every actor call below works on any of them.
 *
 * - An actor's transform is relative to its parent, so moving a parent moves every actor
 *   under it.
 * - On a 2D stage, an actor uses x and y, and rotates about z.
 *
 * An actor is an object. Whoever created it owns it, and ends it with wgf_actor_destroy (see
 * docs/ARCHITECTURE.md, "Resources and objects"). Every call on a handle that isn't an actor
 * does nothing, and returns false or 0. */

/* An actor of any type: a handle of kind "gfx.actor". */
typedef wgf_handle_t wgf_actor_t;

typedef enum wgf_actor_kind_t {
    WGF_ACTOR_KIND_NONE = 0, /* not an actor */
    WGF_ACTOR_KIND_PLAIN = 1, /* a plain actor */
    WGF_ACTOR_KIND_STAGE2D = 2,
    WGF_ACTOR_KIND_SPRITE = 3,
    WGF_ACTOR_KIND_CAMERA2D = 4,
    WGF_ACTOR_KIND_TEXT = 5,
    WGF_ACTOR_KIND_SHAPE2D = 6,
    WGF_ACTOR_KIND_EMITTER2D = 7,
    WGF_ACTOR_KIND_CAMERA3D = 8,
    WGF_ACTOR_KIND_STAGE3D = 9,
    WGF_ACTOR_KIND_LIGHT = 10,
    WGF_ACTOR_KIND_MODEL = 11,
    WGF_ACTOR_KIND_SHAPE3D = 12,
    WGF_ACTOR_KIND_EMITTER3D = 13,
    WGF_ACTOR_KIND_SPRITE_BATCH = 14,
    WGF_ACTOR_KIND_TILEMAP_LAYER = 15 /* a layer of a placed Tiled map (wgf_tilemap_layer.h) */
} wgf_actor_kind_t;

/* What wgf_actor_destroy does with the actor's children. */
typedef enum wgf_actor_destroy_t {
    WGF_ACTOR_DESTROY_CHILDREN = 0, /* destroys the actor and every actor under it */
    WGF_ACTOR_KEEP_CHILDREN = 1     /* destroys the actor alone. Its children move up to its parent, in its
                                         place, and keep their world transform. If it has no parent, they are
                                         left with none. */
} wgf_actor_destroy_t;

/* Creates a plain actor: a transform, with no parent.
 *
 * Returns 0 if there is no room for another actor. */
WGF_API wgf_actor_t wgf_actor_create(void);

/* Destroys an actor of any kind, at once.
 *
 * The resources it held are released, never destroyed. For example, a sprite drops its
 * reference to its texture. */
WGF_API void wgf_actor_destroy(wgf_actor_t actor, wgf_actor_destroy_t children);

/* Returns the actor's kind (a sprite, a model, a stage), which says which calls it takes.
 *
 * Returns NONE if `actor` isn't an actor, or is gone. */
WGF_API wgf_actor_kind_t wgf_actor_get_kind(wgf_actor_t actor);

/* Sets or gets the actor's parent, and reads its children.
 *
 * set_parent puts `actor` under `parent`, last among its children. The actor keeps its own
 * transform, so it moves to where that transform places it relative to the new parent.
 * A `parent` of 0 detaches it, and a detached actor is not drawn.
 *
 * set_parent returns false if:
 * - `parent` isn't an actor.
 * - `parent` is `actor`, or is under it.
 * - `actor` is a 2D or 3D stage. Stages are always roots. */
WGF_API bool wgf_actor_set_parent(wgf_actor_t actor, wgf_actor_t parent);
WGF_API wgf_actor_t wgf_actor_get_parent(wgf_actor_t actor);
WGF_API int wgf_actor_get_child_count(wgf_actor_t actor);
/* Returns the child at `index`, 0 first. Returns 0 if there is none. */
WGF_API wgf_actor_t wgf_actor_get_child(wgf_actor_t actor, int index);

/* Sets or gets where `actor` is among its parent's children.
 *
 * On a 2D stage this is the drawing order: later children draw over earlier ones. An index
 * past either end is clamped to that end, so a large index moves the actor to the front.
 *
 * set_index returns false if the actor has no parent. */
WGF_API bool wgf_actor_set_index(wgf_actor_t actor, int index);
WGF_API int wgf_actor_get_index(wgf_actor_t actor);
/* Moves `actor` to the last place among its parent's children, or the first.
 *
 * On a 2D stage the last child draws over the others (the front), and the first under them
 * (the back). Each returns false if the actor has no parent. */
WGF_API bool wgf_actor_move_to_front(wgf_actor_t actor);
WGF_API bool wgf_actor_move_to_back(wgf_actor_t actor);

/* Sets or gets the actor's transform, relative to its parent.
 *
 * - Rotation is three angles in radians, about x, then y, then z (see wgf_quat_from_euler).
 *   It is kept as the rotation those angles make. get_rotation returns angles for the same
 *   rotation, which may not be the ones given (past a half turn, for example).
 * - Scale 1 is the actor's own size. */
WGF_API bool wgf_actor_set_position(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_actor_get_position(wgf_actor_t actor);
WGF_API bool wgf_actor_set_rotation(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_actor_get_rotation(wgf_actor_t actor);
WGF_API bool wgf_actor_set_scale(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_actor_get_scale(wgf_actor_t actor);
/* Sets position, rotation, and scale at once. It is the cheapest way to move an actor each frame. */
WGF_API bool wgf_actor_set_transform(wgf_actor_t actor, float position_x, float position_y,
                                          float position_z, float rotation_x, float rotation_y, float rotation_z,
                                          float scale_x, float scale_y, float scale_z);

/* Ends the smoothing of a simulated actor, so it is drawn where it is now.
 *
 * An actor with a simulated component (motion, bounds, lifetime, or a collider: see
 * wgf_component.h) moves at the tick rate. Its transform is the simulation's. Each frame it is
 * drawn between its last two ticks' transforms, at the frame's tick fraction, so it moves
 * smoothly at any frame rate. A change made in a tick is smoothed into the frames after it.
 * Snap ends that smoothing: an actor put somewhere new isn't seen sweeping there.
 *
 * Returns false if `actor` isn't an actor. Returns true, and does nothing, for an actor that
 * isn't simulated. */
WGF_API bool wgf_actor_snap(wgf_actor_t actor);

/* Sets or gets the positions, rotations, and scales of many actors at once: one call for all
 * of `actors` (`count` of them), where a call each would cross into the library once an actor.
 *
 * Each array holds x, y, z for each actor in turn, as the one-actor calls above take and give
 * them: a rotation as three angles in radians. Any of the three may be NULL, with a count of
 * 0: set_transforms leaves that part of each actor as it is, and get_transforms fills nothing
 * there. So moving many actors is set_transforms with positions alone.
 *
 * set_transforms skips a handle that isn't an actor. It returns false, and sets nothing, if
 * an array that isn't NULL hasn't three floats for each actor, or a count is below 0.
 *
 * get_transforms reads as many actors as fit in every array given, and returns how many floats
 * it filled in each: three an actor. A handle that isn't an actor reads as 0, 0, 0. */
WGF_API bool wgf_actor_set_transforms(const wgf_actor_t *actors, int count, const float *positions,
                                      int positions_count, const float *rotations, int rotations_count,
                                      const float *scales, int scales_count);
WGF_API int wgf_actor_get_transforms(const wgf_actor_t *actors, int count, float *positions, int positions_count,
                                     float *rotations, int rotations_count, float *scales, int scales_count);

/* Returns where the actor is in its tree's root's space. On a 2D stage, that is stage units.
 *
 * - The world position is the simulation's, as the transform is (see wgf_actor_snap). It is
 *   the same in a tick and in a frame.
 * - The drawn position is where the actor is drawn this frame. A simulated actor, or one under
 *   a simulated actor, is drawn between its last two ticks. A camera that follows the actor in
 *   the frame reads this, so the camera moves as smoothly as the actor does.
 *
 * The two are the same for an actor with nothing simulated at or above it. */
WGF_API wgf_vec3_t wgf_actor_get_world_position(wgf_actor_t actor);
WGF_API wgf_vec3_t wgf_actor_get_drawn_position(wgf_actor_t actor);

/* Turns a direction in the actor's own space into its tree's root's space, at unit length.
 *
 * For example, (0, 0, -1) gives the actor's heading in the world, with no angles to read back
 * (get_rotation's angles may not be the ones set). The world direction is the simulation's,
 * and the drawn direction is this frame's, as with the positions above.
 *
 * Returns 0, 0, 0 for a direction of (0, 0, 0), or if `actor` isn't an actor. */
WGF_API wgf_vec3_t wgf_actor_get_world_direction(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_actor_get_drawn_direction(wgf_actor_t actor, float x, float y, float z);

/* Turns the actor so its -z points at the target (x, y, z).
 *
 * Its +y is turned as near to the up direction (up_x, up_y, up_z) as it can be. The target
 * and up direction are both in its tree's root's space. This is how you aim a camera, since a
 * camera looks down its -z. The rotation is set relative to the parent, so the actor points at
 * the target from where it is placed now.
 *
 * Returns false if the target is where the actor is, or if the up direction lies along the
 * line to the target. */
WGF_API bool wgf_actor_look_at(wgf_actor_t actor, float x, float y, float z, float up_x, float up_y, float up_z);

/* Sets or gets a name to find the actor by.
 *
 * - NULL or "" means no name (default: no name).
 * - get_name returns "" for no name. The string is borrowed, and valid until the name changes
 *   or the actor is gone.
 * - Its stage finds the actor by name (wgf_stage2d_find, wgf_stage3d_find) when it is the only
 *   actor so named there.
 * - A path from any actor finds it (wgf_actor_find) when its name is unique among its
 *   siblings. */
WGF_API bool wgf_actor_set_name(wgf_actor_t actor, const char *name);
WGF_API const char *wgf_actor_get_name(wgf_actor_t actor);

/* Returns the actor at `path` from `actor`.
 *
 * A path is child names joined by '/'. So "wheel_rl/smoke" is the child "smoke" of the child
 * "wheel_rl". Each step looks only among one actor's children, never through the tree, and
 * takes the first child so named, in order.
 *
 * Returns 0 if no actor is found, if a step is empty ("a//b"), or if `path` is empty or
 * NULL. */
WGF_API wgf_actor_t wgf_actor_find(wgf_actor_t actor, const char *path);

/* Sets or gets the four flags that say what an actor takes part in.
 *
 * Each flag is the actor's own, and reads back as it was set.
 *
 *   enabled   off: the actor and every actor under it are skipped altogether, by drawing and
 *             by every update, as if they weren't in the tree. An emitter under it isn't
 *             moved on. Use it to switch off a part of the game for now. It is the only flag
 *             that reaches the children. Their own flags are kept, so enabling the actor
 *             again brings each back as it was set.
 *   visible   off: the actor's own output is off. A sprite, shape, text, or emitter isn't
 *             drawn. The actor is still placed, and its children are as they are. A glTF
 *             file's root (wgf_model.h) draws nothing itself, and stands for its file:
 *             hidden, its file's nodes aren't drawn or picked either. Actors a game put under
 *             those nodes are as they are. To hide a whole tree, turn enabled off.
 *   paused    on: the actor and every actor under it are frozen, but still drawn. Their
 *             behaviors don't tick. Their components' systems (motion, arcade bodies, bounds,
 *             lifetimes) and tweens leave them. What moves by itself holds still: an
 *             emitter's particles, a sprite's clip, a model's animation. Use it to hold a
 *             level under a pause menu (wgf_screen.h pauses the screens under the top one).
 *             Its colliders still meet as triggers. is_frozen returns true if the actor or
 *             any actor above it is paused.
 *   pickable  off: a pick (wgf_stage2d_pick, wgf_stage3d_pick) and the pointer
 *             (wgf_pointer.h) pass through the actor to what is under it. Its children can
 *             still be hit, as a panel that isn't clickable can hold buttons that are. Only
 *             actors with a surface to hit are ever picked: a sprite, a 2D shape, a text, a
 *             model, or a 3D shape. For any other kind this flag changes nothing.
 *
 * Defaults: enabled, visible, not paused, pickable. */
WGF_API bool wgf_actor_set_enabled(wgf_actor_t actor, bool enabled);
WGF_API bool wgf_actor_is_enabled(wgf_actor_t actor);

WGF_API bool wgf_actor_set_paused(wgf_actor_t actor, bool paused);
WGF_API bool wgf_actor_is_paused(wgf_actor_t actor);
WGF_API bool wgf_actor_is_frozen(wgf_actor_t actor);
WGF_API bool wgf_actor_set_visible(wgf_actor_t actor, bool visible);
WGF_API bool wgf_actor_is_visible(wgf_actor_t actor);
WGF_API bool wgf_actor_set_pickable(wgf_actor_t actor, bool pickable);
WGF_API bool wgf_actor_is_pickable(wgf_actor_t actor);

#ifdef __cplusplus
}
#endif

#endif
