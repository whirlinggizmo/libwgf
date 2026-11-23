#ifndef WGF_MODEL_H
#define WGF_MODEL_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_actor.h"
#include "wgf_play_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A model: an actor that shows a mesh on a stage (wgf_stage3d.h), lit by the stage's lights.
 *
 * - Each of the mesh's material slots is drawn with the mesh's material, or with one of the
 *   model's own (wgf_material.h).
 * - The model holds a reference to its mesh and to its own materials.
 * - Place it with the actor calls.
 * - On a 2D stage it draws nothing. */

/* Creates a model of `mesh`. A `mesh` of 0 means none yet.
 *
 * The model is tinted white, so it draws as its materials are.
 *
 * Returns 0 if `mesh` isn't a mesh, or if there is no room for another actor. */
WGF_API wgf_actor_t wgf_model_create(wgf_mesh_t mesh);

/* Sets or gets the model's mesh. 0 means none, and nothing is drawn.
 *
 * A glTF file's mesh (wgf_mesh_create) makes the model the file's root. Once the mesh is
 * READY, the file's node tree is made the model's children. The mesh is then the model's for
 * good.
 *
 * To tell that a model has loaded, test its mesh's status (wgf_resource_get_status of
 * wgf_model_get_mesh). The moment it reads READY, the model's children are made and its
 * animations are there, so there is no need to test their counts.
 *
 * When the file is loaded again (wgf_asset_reload), the tree is made again in place:
 * - A node's actor is kept, found by its name under its parent's actor, and moved.
 * - Its mesh keeps the same handle, with the new triangles.
 * - A node the file no longer has is gone, with its actor and every actor under it.
 * - A new node is made.
 * - The file's lights are made again.
 *
 * set_mesh returns false if:
 * - `mesh` isn't a mesh.
 * - `mesh` is a file's mesh, and the model has children.
 * - The model is a file's root or one of its nodes, whose meshes are the file's. */
WGF_API bool wgf_model_set_mesh(wgf_actor_t model, wgf_mesh_t mesh);
WGF_API wgf_mesh_t wgf_model_get_mesh(wgf_actor_t model);

/* Sets or gets a color the model's materials are multiplied by (default: white).
 *
 * Alpha below 255 makes the model see-through, blended back to front.
 *
 * A glTF file's root draws nothing itself, and stands for its file. Its tint multiplies its
 * nodes' own, so one call tints the whole file: an enemy in red, or a hit's flash. */
WGF_API bool wgf_model_set_tint(wgf_actor_t model, wgf_color_t color);
WGF_API wgf_color_t wgf_model_get_tint(wgf_actor_t model);

/* Sets or gets the material a slot is drawn with, in place of the mesh's.
 *
 * - `slot` is 0..31. A `slot` of -1 sets every slot.
 * - A `material` of 0 goes back to the mesh's.
 * - The model holds its own reference. Its materials stay when the mesh changes.
 *
 * set_material returns false for a slot out of range, or a handle that isn't a material.
 *
 * get_material returns the material the slot draws with: the model's own or the mesh's. The
 * handle is borrowed. It returns 0 for neither. */
WGF_API bool wgf_model_set_material(wgf_actor_t model, int slot, wgf_material_t material);
WGF_API wgf_material_t wgf_model_get_material(wgf_actor_t model, int slot);

/* Sets or gets whether the model casts and receives shadows, when a light on its stage casts
 * them (see wgf_light_set_shadow_casting).
 *
 * - Casting (default: on): the model is drawn into the light's depth map, so it shadows what
 *   is behind it. Turn it off for a glow or a sky.
 * - Receiving (default: on): shadows darken the model.
 *
 * Each setting is the model's own. In a glTF file's tree, each node's model has its own
 * setting. A hidden model casts nothing, and its see-through parts never cast. */
WGF_API bool wgf_model_set_shadow_casting(wgf_actor_t model, bool casting);
WGF_API bool wgf_model_is_shadow_casting(wgf_actor_t model);
WGF_API bool wgf_model_set_shadow_receiving(wgf_actor_t model, bool receiving);
WGF_API bool wgf_model_is_shadow_receiving(wgf_actor_t model);

/* Plays a glTF file's animations on its root: a model of wgf_mesh_create's mesh.
 *
 * An animation moves the actors of the file's tree. Each joint of a skin is one of those
 * actors, and an actor put under a joint moves with it. That poses the skinned parts its skins
 * place, on the GPU, with up to 4 joints a vertex.
 *
 * A clip's translation, rotation, and scale channels play as the file has them: linear (a
 * rotation takes the shorter arc), step, or cubic spline.
 *
 * What plays has one state, a wgf_play_state.h state. Its transitions are those of play,
 * pause, resume, and stop, as a voice's are. libwgf advances it once a frame, by the frame's
 * time, before the frame callback, and poses the model then. So the frame reads this frame's
 * pose.
 *
 *   count, name, find, duration  the file's animations: how many there are (0 until it is
 *                    READY), one's name ("" for none), the first one so named (-1 for
 *                    none), and its length in seconds (its last key's time; 0 for none)
 *   play             plays `animation` from its start (from its end with a negative speed),
 *                    PLAYING, from any state. It is posed at once. A `fade` > 0 crossfades
 *                    into it over that many seconds, from what shows now: the clip that was
 *                    playing, still moving, or the pose held (a crossfade's middle, or a
 *                    clip not moving). Each node's place, turn, and scale are blended. A
 *                    `fade` of 0 or less cuts to it. A node that another animation of the
 *                    file moves, and this one doesn't, is at the file's rest pose. Played
 *                    before the file is READY, it plays once the file is there. If the file
 *                    hasn't that animation, it is dropped then (warned). Returns false for
 *                    a model that isn't a file's root, a negative animation, a fade that
 *                    isn't a number, a file that FAILED, or an animation the READY file
 *                    hasn't
 *   get_animation    the animation played (or stopped at, or paused in); -1 for none
 *   weight           how much of the pose is the played animation's: 0 as its crossfade
 *                    begins, to 1 when it is through. 1 with no crossfade, 0 with no
 *                    animation
 *   pause, resume    pause works only from PLAYING, and resume only from PAUSED. The
 *                    animation is held where it was, its crossfade too
 *   stop             goes to STOPPED from any state, at time 0, showing the clip's first
 *                    pose. Its crossfade is dropped
 *   state            COMPLETE when an animation that isn't looping has run to the end it
 *                    runs toward, and is held there. Also COMPLETE when its file FAILED
 *                    while it played
 *   speed            1 is normal (the default). Negative plays backwards. 0 holds, still
 *                    PLAYING. Returns false for a speed that isn't finite
 *   loop             wraps at the end (the default), or holds the last pose and becomes
 *                    COMPLETE
 *   time             where it is, in seconds from its start. set_time poses it there at
 *                    once, wrapped when looping and clamped to [0, duration] when not. It
 *                    leaves the state as it is. Returns false for a time that isn't finite
 *
 * Each call returns false (or 0, -1, or "") for a model that isn't a file's root.
 *
 * Because the frame moves an animation on, an animation is for what is seen. For what a game
 * decides by (a hit frame, a replay), drive it by hand: set speed 0, and call set_time from
 * the game's fixed tick.
 *
 * A custom material (wgf_material_create_custom) on a skinned part draws it posed too. Its
 * vertex hook runs before the joints pose it. */
WGF_API int wgf_model_get_animation_count(wgf_actor_t model);
WGF_API const char *wgf_model_get_animation_name(wgf_actor_t model, int animation);
WGF_API int wgf_model_find_animation(wgf_actor_t model, const char *name);
WGF_API float wgf_model_get_animation_duration(wgf_actor_t model, int animation);
WGF_API bool wgf_model_play_animation(wgf_actor_t model, int animation, float fade);
WGF_API int wgf_model_get_animation(wgf_actor_t model);
WGF_API float wgf_model_get_animation_weight(wgf_actor_t model);
WGF_API bool wgf_model_pause_animation(wgf_actor_t model);
WGF_API bool wgf_model_resume_animation(wgf_actor_t model);
WGF_API bool wgf_model_stop_animation(wgf_actor_t model);
WGF_API wgf_play_state_t wgf_model_get_animation_state(wgf_actor_t model);
WGF_API bool wgf_model_set_animation_speed(wgf_actor_t model, float speed);
WGF_API float wgf_model_get_animation_speed(wgf_actor_t model);
WGF_API bool wgf_model_set_animation_loop(wgf_actor_t model, bool loop);
WGF_API bool wgf_model_is_animation_loop(wgf_actor_t model);
WGF_API bool wgf_model_set_animation_time(wgf_actor_t model, float seconds);
WGF_API float wgf_model_get_animation_time(wgf_actor_t model);

/* Plays animation layers over the animation playing: WGF_MODEL_LAYERS of them, `index` 0 up.
 *
 * Each layer is blended over what is under it, in order: the clip below, then layer 0, then
 * 1, and so on. For example, an attack from the waist up while the legs run on, and a flinch
 * of the head over both.
 *
 * - play_layer_animation plays `animation` on layer `index`. It moves the file's node named
 *   `bone` and every node under it. That is a mask: "Spine" for the upper body.
 * - The layer is blended by its weight, which comes in over `fade` seconds (0 or less: at
 *   once).
 * - With `loop`, it loops. Without, it plays once, then fades out over the same `fade` as it
 *   ends (COMPLETE), and is gone.
 * - Its time runs at the model's speed while the animation below plays. It always runs
 *   forward, even when that speed is negative.
 * - stop_layer_animation fades it out over `fade`.
 * - The getters return its animation (-1 for none), its state, and its weight now (0 to 1).
 *
 * play_layer_animation returns false if:
 * - `index` is outside the layers.
 * - The model isn't a file's root.
 * - Its file isn't READY, or has no animation playing yet.
 * - The file hasn't `animation`, or hasn't `bone`.
 * - `fade` isn't finite. */
#define WGF_MODEL_LAYERS 4
WGF_API bool wgf_model_play_layer_animation(wgf_actor_t model, int index, int animation, const char *bone, float fade,
                                            bool loop);
WGF_API bool wgf_model_stop_layer_animation(wgf_actor_t model, int index, float fade);
WGF_API int wgf_model_get_layer_animation(wgf_actor_t model, int index);
WGF_API wgf_play_state_t wgf_model_get_layer_state(wgf_actor_t model, int index);
WGF_API float wgf_model_get_layer_weight(wgf_actor_t model, int index);

/* Sets `model` playing as `from` plays now.
 *
 * It copies the animation, time, speed, loop, and state, and the layers (each layer's bone
 * found again by name). A crossfade running in `from` ends at its clip.
 *
 * Use it for a part of a character put on while the others play, so it doesn't start its clip
 * again. For example, a part model swapped in, with each part given the same clips
 * (wgf_model_set_animations).
 *
 * Returns false if `model` isn't a file's root, if `from` has no animation, or if `model`
 * hasn't `from`'s clips. */
WGF_API bool wgf_model_sync_animation(wgf_actor_t model, wgf_actor_t from);

/* Returns a socket: the first node named `name` under a file's root, at any depth.
 *
 * A file's joints are actors, so `name` can be a bone such as "Hand.R" or "Head". A sword
 * parented to the hand (wgf_actor_set_parent) follows it through every clip.
 *
 * Returns 0 if there is no such node, or if `model` isn't a file's root. */
WGF_API wgf_actor_t wgf_model_find_node(wgf_actor_t model, const char *name);

/* Sets or gets where the model's animations come from: another glTF file, `clips`.
 *
 * `clips` is a file of animations alone, with its skeleton named as this file's is. For
 * example, Wardens' Animations.glb for each of a character's part models. Each clip's
 * channels move this file's nodes of the same names.
 *
 * - A `clips` of 0 goes back to the model's own animations.
 * - What was playing stops.
 * - Each part of a character given the same clips, and played the same way, moves as one.
 *
 * set_animations returns false if `model` isn't a file's root, or if `clips` isn't a mesh. */
WGF_API bool wgf_model_set_animations(wgf_actor_t model, wgf_mesh_t clips);
WGF_API wgf_mesh_t wgf_model_get_animations(wgf_actor_t model);

/* Returns the box around every model at and under `actor`: its least and greatest corner.
 *
 * Each mesh's box is placed as the model is now, in the tree's root's space. Use it to place
 * a glTF file's tree on the ground, or to frame it in a camera.
 *
 * Returns 0, 0, 0 for both if no model there has a READY mesh. */
WGF_API wgf_vec3_t wgf_model_get_bounds_min(wgf_actor_t actor);
WGF_API wgf_vec3_t wgf_model_get_bounds_max(wgf_actor_t actor);

/* Tests where a ray meets this model, as it is drawn now.
 *
 * - For a model, it tests its triangles. Its box is tested first. A skinned model is tested in
 *   its pose this frame.
 * - For a glTF file's root, it tests its file's visible nodes.
 * - It tests the model whether it is pickable or not (wgf_actor_set_pickable). To find a model
 *   among others, see wgf_stage3d_pick.
 *
 * The ray is its origin and its direction, in its stage's space. The direction need not be a
 * unit vector. The hit goes into `hit` as wgf_stage3d_pick_ray's does, all 0 after a miss.
 *
 * Returns false for a miss, a handle that isn't a model, a mesh that isn't READY, or a
 * direction of 0 or not finite. */
WGF_API bool wgf_model_pick_ray(wgf_actor_t model, float ox, float oy, float oz, float dx, float dy, float dz,
                                float *hit, int hit_count);

#ifdef __cplusplus
}
#endif

#endif
