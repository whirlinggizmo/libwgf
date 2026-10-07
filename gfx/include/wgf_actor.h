#ifndef WGF_ACTOR_H
#define WGF_ACTOR_H

#include <stdbool.h>

#include "wgf_handle.h"
#include "wgf_api.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Everything placed is an actor, in a tree. A plain actor is only a transform: a group,
 * or a pivot. The other kinds -- 2D stages, sprites, shapes, text, particle emitters,
 * cameras -- are actors with content, made by their own create, and every actor call
 * below works on any of them. An actor's transform is relative to its parent, so moving a parent moves
 * everything under it. On a 2D stage, an actor uses x and y, and rotates about z.
 *
 * An actor is an object: owned by whoever created it, and ended with
 * wgf_actor_destroy (docs/ARCHITECTURE.md, "Resources and objects"). Every call
 * on a handle that isn't an actor does nothing, returning false or 0. */

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
    WGF_ACTOR_KIND_SHAPE3D = 12
} wgf_actor_kind_t;

/* What wgf_actor_destroy does with the actor's children. */
typedef enum wgf_actor_destroy_t {
    WGF_ACTOR_DESTROY_CHILDREN = 0, /* the actor and everything under it */
    WGF_ACTOR_KEEP_CHILDREN = 1     /* the actor alone: its children move up to its parent, in its place,
                                         keeping where they are (their world transform). Under an actor with
                                         no parent, they are left with none. */
} wgf_actor_destroy_t;

/* A plain actor: a transform, with no parent. 0 when there is no room for another. */
WGF_API wgf_actor_t wgf_actor_create(void);

/* End an actor of any kind, at once. What it held is released, never destroyed: a
 * sprite drops its reference to its texture. */
WGF_API void wgf_actor_destroy(wgf_actor_t actor, wgf_actor_destroy_t children);

WGF_API wgf_actor_kind_t wgf_actor_get_kind(wgf_actor_t actor);

/* Put `actor` under `parent`, last among its children, keeping its own transform
 * (so it moves to where it sits relative to the new parent); 0 detaches it, and a
 * detached actor is drawn by nothing. False when `parent` isn't an actor, or is
 * `actor` or under it, or when `actor` is a 2D or 3D stage, which are always roots. */
WGF_API bool wgf_actor_set_parent(wgf_actor_t actor, wgf_actor_t parent);
WGF_API wgf_actor_t wgf_actor_get_parent(wgf_actor_t actor);
WGF_API int wgf_actor_get_child_count(wgf_actor_t actor);
/* The child at `index`, 0 first; 0 when there is none. */
WGF_API wgf_actor_t wgf_actor_get_child(wgf_actor_t actor, int index);

/* Where `actor` is among its parent's children, which is its drawing order in a
 * 2D stage: later children draw over earlier ones. An index past either end is
 * clamped to it, so a large one moves it to the front. False when it has no
 * parent. */
WGF_API bool wgf_actor_set_index(wgf_actor_t actor, int index);
WGF_API int wgf_actor_get_index(wgf_actor_t actor);

/* The transform, relative to the parent. Rotation is three angles in radians,
 * about x, then y, then z (wgf_quat_from_euler), kept as the rotation they make:
 * get_rotation gives angles for the same rotation, which may not be the ones given
 * (past a half turn, say). Scale 1 is its own size. */
WGF_API bool wgf_actor_set_position(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_actor_get_position(wgf_actor_t actor);
WGF_API bool wgf_actor_set_rotation(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_actor_get_rotation(wgf_actor_t actor);
WGF_API bool wgf_actor_set_scale(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_actor_get_scale(wgf_actor_t actor);
/* All three at once: the cheapest way to move something each frame. */
WGF_API bool wgf_actor_set_transform(wgf_actor_t actor, float position_x, float position_y,
                                          float position_z, float rotation_x, float rotation_y, float rotation_z,
                                          float scale_x, float scale_y, float scale_z);

/* An actor with a simulated component (motion, bounds, lifetime, a collider: wgf_component.h)
 * moves at the tick rate: its transform is the simulation's, and it is drawn between its
 * last two ticks' at each frame's tick fraction, so it moves smoothly at any frame rate.
 * A change in a tick is so smoothed into the frames after it; snap ends the smoothing, so
 * the actor is drawn where it is now (put somewhere new, it isn't seen sweeping there).
 * False for an actor that isn't one; true and nothing to do for one not simulated. */
WGF_API bool wgf_actor_snap(wgf_actor_t actor);

/* Many positions at once: each of `actors` (count of them) read into `out` as x, y, z in
 * turn, as many as fit in `out_count` floats, returning how many floats it filled (a
 * handle that isn't an actor reads 0, 0, 0); and each set from `positions` (x, y, z in
 * turn, positions_count floats), false when there aren't three floats for each, skipping
 * a handle that isn't an actor. */
WGF_API int wgf_actor_get_positions(const wgf_actor_t *actors, int count, float *out, int out_count);
WGF_API bool wgf_actor_set_positions(const wgf_actor_t *actors, int count, const float *positions,
                                    int positions_count);

/* Where the actor is in its tree's root's space: on a 2D stage, stage units. The world
 * position is the simulation's, as its transform is (above), in a tick and in a frame
 * alike; the drawn position is where it is drawn this frame, a simulated actor (or one
 * under one) between its last two ticks: what a camera following it in the frame reads,
 * so the camera moves as smoothly as the actor does. They are the same for an actor with
 * nothing simulated at or above it. */
WGF_API wgf_vec3_t wgf_actor_get_world_position(wgf_actor_t actor);
WGF_API wgf_vec3_t wgf_actor_get_drawn_position(wgf_actor_t actor);

/* The direction (x, y, z), in the actor's own space, turned into its tree's root's space
 * and made unit length: its heading in the world, say, from (0, 0, -1), with no angles to
 * read back (get_rotation's may not be the ones set). The world direction is the
 * simulation's and the drawn one this frame's, as the positions above. 0, 0, 0 for (0, 0,
 * 0), or a handle that isn't an actor. */
WGF_API wgf_vec3_t wgf_actor_get_world_direction(wgf_actor_t actor, float x, float y, float z);
WGF_API wgf_vec3_t wgf_actor_get_drawn_direction(wgf_actor_t actor, float x, float y, float z);

/* Turn the actor so its -z points at the target (x, y, z) and its +y is as near to the
 * up direction (up_x, up_y, up_z) as can be, both in its tree's root's space: how a
 * camera is aimed, since it looks down its -z. Its rotation is set relative to its
 * parent, so it points there as it is placed now. False when the target is where the
 * actor is, or the up direction is along the line to it. */
WGF_API bool wgf_actor_look_at(wgf_actor_t actor, float x, float y, float z, float up_x, float up_y, float up_z);

/* A name to find the actor by; NULL or "" for none, the default. get_name is "" for none: borrowed, valid until the
 * name changes or the actor goes. A name is found by its stage (wgf_stage2d_find, wgf_stage3d_find) when it is the
 * only one so named there, and by a path from any actor (wgf_actor_find) when it is unique among its siblings. */
WGF_API bool wgf_actor_set_name(wgf_actor_t actor, const char *name);
WGF_API const char *wgf_actor_get_name(wgf_actor_t actor);

/* The actor at `path` from `actor`: child names joined by '/', so "wheel_rl/smoke" is the
 * child "smoke" of the child "wheel_rl". Each step looks only among one actor's children
 * (the first so named, in order), never through the tree. 0 for none, an empty step
 * ("a//b"), or an empty or NULL path. */
WGF_API wgf_actor_t wgf_actor_find(wgf_actor_t actor, const char *path);

/* Two flags say what an actor takes part in, each the actor's own, read back as set:
 *
 *   enabled   off: the actor is skipped altogether, with everything under it, as if
 *             it weren't in the tree, by drawing and by every update (an emitter
 *             under it isn't moved on): the switch on a part of the game that is off
 *             for now. The only one that reaches the children; their own flags are
 *             kept, so enabling it again brings each back as it was set
 *   visible   off: the actor's own output is off -- a sprite, shape, text, or
 *             emitter isn't drawn -- while it is still placed and its children are
 *             as they are
 *
 * Defaults: enabled, visible. */
WGF_API bool wgf_actor_set_enabled(wgf_actor_t actor, bool enabled);
WGF_API bool wgf_actor_is_enabled(wgf_actor_t actor);

WGF_API bool wgf_actor_set_visible(wgf_actor_t actor, bool visible);
WGF_API bool wgf_actor_is_visible(wgf_actor_t actor);

#ifdef __cplusplus
}
#endif

#endif
