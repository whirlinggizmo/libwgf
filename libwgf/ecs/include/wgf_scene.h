#ifndef WGF_SCENE_H
#define WGF_SCENE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_actor.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A scene: a text file of actor trees and prefabs, loaded as a resource.
 *
 * The trees hold actors with their kinds, components, and behaviors. A scene loads like any
 * resource (wgf_resource.h): on create it is PENDING, then READY, or FAILED with each bad line
 * logged.
 *
 * - Instantiating a scene makes its trees.
 * - A prefab is a tree kept in the file to make at run time, as often as you want, such as a
 *   rock or a bullet.
 *
 * The file is text, a line per fact, so a scene diffs and merges. wgf_world_dump writes the
 * simulated actors in the same format.
 *
 * The format, `wgf-scene 2`, is described in BUILDING.md ("Scene files"):
 * - Flat `actor` and `prefab` blocks, each naming its parent by path (`prefab ship/flame`).
 * - In each block, a kind line, component lines, and behavior lines.
 * - A behavior's parameters may refer to other actors by name or path (`target=@start_gate`,
 *   see wgf_behavior_get_param_actor).
 *
 * A block under a glTF file's model (`model path=`) that names one of the file's nodes is
 * that node. Its lines are applied to the node, and the blocks under it are made, once the
 * file has loaded. A block that names no node is a new actor there, also made then. The scene
 * is held until then, and the references in that tree are found then.
 *
 * Every refusal FAILS the scene, logging the line with its number. The refusals are:
 * - an unknown line, kind, component, or key
 * - a value of the wrong shape
 * - a path with no parent above it
 * - a block inside another
 * - a file at version 1 (tools/update_scene.py updates one) */
typedef wgf_handle_t wgf_scene_t;

/* One of a scene's prefabs, found once by its name (wgf_scene_find_prefab) and kept. You spawn
 * it as often as you want, with no lookup. It is valid while its scene is. */
typedef wgf_handle_t wgf_prefab_t;

/* Creates the scene at `path`, and starts loading it. The same path again returns the same
 * scene, with one more reference. Returns 0 only when there is no room for another scene. */
WGF_API wgf_scene_t wgf_scene_create(const char *path);

/* Makes the scene's top actors' trees, in file order, under `parent` (0 for none).
 *
 * Each actor is made as the file says, and each behavior's CREATED is raised. Returns the
 * number of top actors made. Returns 0 for a scene that isn't READY, or a `parent` that isn't
 * an actor (0 is allowed). */
WGF_API int wgf_scene_instantiate(wgf_scene_t scene, wgf_actor_t parent);

/* Returns the prefab `name` of a READY scene, to spawn. It returns the same handle each time
 * you ask.
 *
 * Returns 0 for a scene that isn't READY, or a prefab it doesn't have. A released scene's
 * prefabs are released with it, and their handles go stale. */
WGF_API wgf_prefab_t wgf_scene_find_prefab(wgf_scene_t scene, const char *name);

/* Returns what the file holds, once the scene is READY.
 *
 * - get_actor_count returns the number of top actors in the file: those with no parent
 *   there, whatever they are. It is not how many actors are simulated now
 *   (wgf_actor_get_count).
 * - get_prefab_count returns the number of prefabs.
 * - get_prefab_name returns each prefab's name in file order, or "" past the end.
 *
 * They return 0 and "" until the scene is READY. */
WGF_API int wgf_scene_get_actor_count(wgf_scene_t scene);
WGF_API int wgf_scene_get_prefab_count(wgf_scene_t scene);
WGF_API const char *wgf_scene_get_prefab_name(wgf_scene_t scene, int index);
WGF_API bool wgf_scene_has_prefab(wgf_scene_t scene, const char *name);

/* Makes one tree of the prefab under `parent` (0 for none), and returns its top actor.
 *
 * Each actor is made as the file says, each behavior's CREATED is raised, and the tree's
 * references are found. Returns 0 for a prefab that isn't one (or whose scene is gone), or a
 * `parent` that isn't an actor. */
WGF_API wgf_actor_t wgf_prefab_spawn(wgf_prefab_t prefab, wgf_actor_t parent);

/* Makes one tree of the prefab, as wgf_prefab_spawn does, with its top actor placed and turned.
 *
 * - The top actor is placed at `x`, `y`, `z` in `parent`'s space.
 * - It is turned `angle` radians about its stage's up axis: y on a 3D stage, z otherwise.
 * - Its other angles and its scale are the prefab's.
 *
 * The actor is then snapped, so it is drawn there from its first frame. Whatever it bursts as
 * it is made (an emitter's `burst=`) starts there too. */
WGF_API wgf_actor_t wgf_prefab_spawn_at(wgf_prefab_t prefab, wgf_actor_t parent, float x, float y, float z,
                                        float angle);

/* Makes actors from a placed Tiled map's object layer (wgf_tilemap_layer.h).
 *
 * For each object whose class (wgf_tilemap_get_object_class) names one of the scene's
 * prefabs, that prefab is spawned under the layer (see wgf_prefab_spawn_at):
 * - It is placed at the object's middle (wgf_tilemap_get_object_middle). That is a point
 *   object's point, a polygon's or polyline's place, or a tile object's tile as drawn.
 * - It takes the place of a tile object's sprite, which is destroyed.
 * - It is turned as the object is.
 * - It is named for the object, when the object has a name.
 * - It is hidden when Tiled hides the object.
 * - Each of the object's custom properties is set, as text, as a parameter of each behavior
 *   on its top actor (wgf_tilemap_get_property). A value starting with '@' is a reference
 *   (wgf_behavior.h). This happens before the binding takes their CREATED.
 *
 * Objects with no class, or a class the scene has no prefab of, are left for your game to
 * read.
 *
 * Returns the number spawned. Returns 0 for a scene that isn't READY, or an actor that isn't
 * a placed object layer. */
WGF_API int wgf_scene_spawn_objects(wgf_scene_t scene, wgf_actor_t layer);

#ifdef __cplusplus
}
#endif

#endif
