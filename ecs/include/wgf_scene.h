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

/* A scene: a text file of actor trees -- their kinds, components, and behaviors -- and
 * prefabs, loaded as a resource (wgf_resource.h: on create, PENDING then READY, or FAILED
 * with each bad line logged). Instantiating it makes its trees; a prefab is a tree kept in
 * the file to make at run time, as often as wanted (a rock, a bullet). Text, a line per
 * fact, so a scene diffs and merges; wgf_world_dump writes the simulated actors in the same
 * format.
 *
 * The format, `wgf-scene 2`, is BUILDING.md's ("Scene files"): flat `actor` and `prefab`
 * blocks, each naming its parent by path (`prefab ship/flame`), a kind line, component
 * lines, and behavior lines whose parameters may refer to other actors by name or path
 * (`target=@start_gate`, wgf_behavior_get_param_actor). Every refusal -- an unknown line,
 * kind, component, or key, a value of the wrong shape, a path with no parent above it, a
 * block inside another, a file at version 1 (tools/update_scene.py updates one) -- FAILS
 * the scene, the line logged with its number. */
typedef wgf_handle_t wgf_scene_t;

/* One of a scene's prefabs, found once by its name (wgf_scene_find_prefab) and kept: what
 * is spawned, as often as wanted, with no lookup. Valid while its scene is. */
typedef wgf_handle_t wgf_prefab_t;

/* The scene at `path`, loading. The same path again gives the same scene, with one more
 * reference. 0 only when there is no room for another. */
WGF_API wgf_scene_t wgf_scene_create(const char *path);

/* Make the scene's top actors' trees, in file order, under `parent` (0: none), each actor
 * as the file says and each behavior's CREATED raised. The number of top actors made; 0
 * for a scene that isn't READY, or a `parent` that isn't an actor (0 is). */
WGF_API int wgf_scene_instantiate(wgf_scene_t scene, wgf_actor_t parent);

/* The prefab `name` of a READY scene, to spawn: the same handle each time it is asked
 * for. 0 for a scene that isn't READY, or a prefab it doesn't have. A released scene's
 * prefabs go with it (their handles stale). */
WGF_API wgf_prefab_t wgf_scene_find_prefab(wgf_scene_t scene, const char *name);

/* What the file holds, once READY: its top actors and its prefabs, and each prefab's name
 * in file order ("" past the end). 0 and "" until then. */
WGF_API int wgf_scene_get_actor_count(wgf_scene_t scene);
WGF_API int wgf_scene_get_prefab_count(wgf_scene_t scene);
WGF_API const char *wgf_scene_get_prefab_name(wgf_scene_t scene, int index);
WGF_API bool wgf_scene_has_prefab(wgf_scene_t scene, const char *name);

/* Make one tree of the prefab under `parent` (0: none): its top actor, each actor as the
 * file says, each behavior's CREATED raised, and its references found. 0 for a prefab
 * that isn't one (or whose scene went), or a `parent` that isn't an actor. */
WGF_API wgf_actor_t wgf_prefab_spawn(wgf_prefab_t prefab, wgf_actor_t parent);

/* The same, its top actor placed at (x, y, z) in `parent`'s space and turned `angle`
 * radians about its stage's up -- y on a 3D stage, z otherwise -- its other angles and its
 * scale the prefab's, then snapped: drawn there from its first frame, and what it bursts
 * as it is made (an emitter's `burst=`) starts there. */
WGF_API wgf_actor_t wgf_prefab_spawn_at(wgf_prefab_t prefab, wgf_actor_t parent, float x, float y, float z,
                                        float angle);

#ifdef __cplusplus
}
#endif

#endif
