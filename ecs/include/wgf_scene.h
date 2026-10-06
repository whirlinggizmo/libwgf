#ifndef WGF_SCENE_H
#define WGF_SCENE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_entity.h"
#include "wgf_handle.h"
#include "wgf_node.h"
#include "wgf_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A scene: a text file of entities, their components, and prefabs, loaded as a resource
 * (wgf_resource.h: on create, PENDING then READY, or FAILED with each bad line logged).
 * Instantiating it makes its entities; a prefab is an entity kept in the file to make
 * at run time, as often as wanted (a rock, a bullet). Text, so a scene diffs and merges;
 * wgf_ecs_dump writes the world in the same format.
 *
 * The format, a line each, `#` starting a comment, words split by spaces:
 *
 *   wgf-scene 1                  the first line: the format and its version
 *   prefab <name>                a prefab, until its `end`
 *   entity [<name>]              an entity made by instantiating, until its `end`
 *     from <prefab>              (first in an entity or prefab) starting as that prefab,
 *                                the lines after it changing it
 *     <component> key=value ...  a component, with its settings
 *   end
 *
 * A value is a number, a list of numbers (`1,2,3`), a color (`#RRGGBB` or `#RRGGBBAA`), a
 * word, or text in double quotes (`"a \"b\" \\ c"`, with \" and \\ escapes). Settings
 * not given keep their defaults. A component line given twice changes the first. The
 * components and their keys:
 *
 *   transform  position=x,y,z  rotation=x,y,z (radians)  scale=x,y,z
 *   motion     velocity=x,y,z  spin=x,y,z  damping=d  max_speed=s
 *   bounds     rect=x,y,w,h  mode=wrap|clamp|destroy  margin=m
 *   lifetime   seconds=s
 *   collider   radius=r  layer=bits  mask=bits
 *   behavior   name=<name>, and any other key=value, its parameters
 *   shape2d    rectangle=w,h | circle=r | line=x0,y0,x1,y1 | polygon=x,y,x,y,...
 *              outline=t  color=#..  pivot=x,y
 *   sprite     texture="path"  source=x,y,w,h  size=w,h  pivot=x,y  tint=#..
 *   text       string="..."  font="path"  size=s  color=#..  wrap=w
 *              align=left|center|right,top|middle|bottom
 *   emitter2d  rate=r  emitting=true|false  capacity=n  life=min,max  direction=a
 *              spread=s  speed=min,max  radius=r  gravity=x,y  drag=d  size=start,end
 *              color=#..,#..  stretch=s  burst=n (that many at once, as it is made)
 *   voice      sound="path"  streamed=true|false  volume=v  pitch=p  pan=p  loop=true|false
 *              play=true|false (played as it is made)
 *
 * Paths name files as wgf_texture_create and the others take them, through the asset
 * layer. A file of more than 4 MB, a line of more than 4096 bytes, an unknown line,
 * component, or key, a value of the wrong shape, a duplicate or unknown prefab, an
 * `entity` or `prefab` inside another, or one left without its `end`, is refused: the
 * scene FAILED, each bad line logged with its number. */
typedef wgf_handle_t wgf_scene_t;

/* The scene at `path`, loading. The same path again gives the same scene, with one more
 * reference. 0 only when there is no room for another. */
WGF_API wgf_scene_t wgf_scene_create(const char *path);

/* Make the scene's entities, in file order, their nodes under `parent` (0: none), each
 * with its components as the file says and a behavior's CREATED raised. The number made;
 * 0 for a scene that isn't READY, or a `parent` that isn't a node (0 is). */
WGF_API int wgf_scene_instantiate(wgf_scene_t scene, wgf_node_t parent);

/* Make one entity of the prefab `name`, its node under `parent`; 0 for a scene that
 * isn't READY, a prefab it doesn't have, or a `parent` that isn't a node. */
WGF_API wgf_entity_t wgf_scene_spawn(wgf_scene_t scene, const char *name, wgf_node_t parent);

/* What the file holds, once READY: its entities and its prefabs, and each prefab's name
 * in file order ("" past the end). 0 and "" until then. */
WGF_API int wgf_scene_get_entity_count(wgf_scene_t scene);
WGF_API int wgf_scene_get_prefab_count(wgf_scene_t scene);
WGF_API const char *wgf_scene_get_prefab_name(wgf_scene_t scene, int index);
WGF_API bool wgf_scene_has_prefab(wgf_scene_t scene, const char *name);

#ifdef __cplusplus
}
#endif

#endif
