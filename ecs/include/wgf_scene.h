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
 * fact, so a scene diffs and merges; wgf_ecs_dump writes the simulated actors in the same
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

/* The scene at `path`, loading. The same path again gives the same scene, with one more
 * reference. 0 only when there is no room for another. */
WGF_API wgf_scene_t wgf_scene_create(const char *path);

/* Make the scene's top actors' trees, in file order, under `parent` (0: none), each actor
 * as the file says and each behavior's CREATED raised. The number of top actors made; 0
 * for a scene that isn't READY, or a `parent` that isn't an actor (0 is). */
WGF_API int wgf_scene_instantiate(wgf_scene_t scene, wgf_actor_t parent);

/* Make one tree of the prefab `name` under `parent`: its top actor; 0 for a scene that
 * isn't READY, a prefab it doesn't have, or a `parent` that isn't an actor. */
WGF_API wgf_actor_t wgf_scene_spawn(wgf_scene_t scene, const char *name, wgf_actor_t parent);

/* What the file holds, once READY: its top actors and its prefabs, and each prefab's name
 * in file order ("" past the end). 0 and "" until then. */
WGF_API int wgf_scene_get_actor_count(wgf_scene_t scene);
WGF_API int wgf_scene_get_prefab_count(wgf_scene_t scene);
WGF_API const char *wgf_scene_get_prefab_name(wgf_scene_t scene, int index);
WGF_API bool wgf_scene_has_prefab(wgf_scene_t scene, const char *name);

#ifdef __cplusplus
}
#endif

#endif
