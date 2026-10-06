#ifndef WGF_NODE_H
#define WGF_NODE_H

#include <stdbool.h>

#include "wgf_handle.h"
#include "wgf_api.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Everything placed is a node, in a tree. A plain node is only a transform: a group,
 * or a pivot. The other kinds -- canvases, sprites, shapes, text, particle emitters,
 * cameras -- are nodes with content, made by their own create, and every node call
 * below works on any of them. A node's transform is relative to its parent, so moving a parent moves
 * everything under it. In a canvas, a node uses x and y, and rotates about z.
 *
 * A node is an object: owned by whoever created it, and ended with
 * wgf_node_destroy (docs/ARCHITECTURE.md, "Resources and objects"). Every call
 * on a handle that isn't a node does nothing, returning false or 0. */

/* A node of any type: a handle of kind "gfx.node". */
typedef wgf_handle_t wgf_node_t;

typedef enum wgf_node_type_t {
    WGF_NODE_TYPE_NONE = 0, /* not a node */
    WGF_NODE_TYPE_NODE = 1, /* a plain node */
    WGF_NODE_TYPE_CANVAS = 2,
    WGF_NODE_TYPE_SPRITE = 3,
    WGF_NODE_TYPE_CAMERA2D = 4,
    WGF_NODE_TYPE_TEXT = 5,
    WGF_NODE_TYPE_SHAPE2D = 6,
    WGF_NODE_TYPE_EMITTER2D = 7
} wgf_node_type_t;

/* What wgf_node_destroy does with the node's children. */
typedef enum wgf_node_destroy_t {
    WGF_NODE_DESTROY_CHILDREN = 0, /* the node and everything under it */
    WGF_NODE_KEEP_CHILDREN = 1     /* the node alone: its children move up to its parent, in its place,
                                         keeping where they are (their world transform). Under a node with
                                         no parent, they are left with none. */
} wgf_node_destroy_t;

/* A plain node: a transform, with no parent. 0 when there is no room for another. */
WGF_API wgf_node_t wgf_node_create(void);

/* End a node of any kind, at once. What it held is released, never destroyed: a
 * sprite drops its reference to its texture. */
WGF_API void wgf_node_destroy(wgf_node_t node, wgf_node_destroy_t children);

WGF_API wgf_node_type_t wgf_node_get_type(wgf_node_t node);

/* Put `node` under `parent`, last among its children, keeping its own transform
 * (so it moves to where it sits relative to the new parent); 0 detaches it, and a
 * detached node is drawn by nothing. False when `parent` isn't a node, or is
 * `node` or under it, or when `node` is a canvas, which is always a root. */
WGF_API bool wgf_node_set_parent(wgf_node_t node, wgf_node_t parent);
WGF_API wgf_node_t wgf_node_get_parent(wgf_node_t node);
WGF_API int wgf_node_get_child_count(wgf_node_t node);
/* The child at `index`, 0 first; 0 when there is none. */
WGF_API wgf_node_t wgf_node_get_child(wgf_node_t node, int index);

/* Where `node` is among its parent's children, which is its drawing order in a
 * canvas: later children draw over earlier ones. An index past either end is
 * clamped to it, so a large one moves it to the front. False when it has no
 * parent. */
WGF_API bool wgf_node_set_index(wgf_node_t node, int index);
WGF_API int wgf_node_get_index(wgf_node_t node);

/* The transform, relative to the parent. Rotation is three angles in radians,
 * about x, then y, then z (wgf_quat_from_euler), kept as the rotation they make:
 * get_rotation gives angles for the same rotation, which may not be the ones given
 * (past a half turn, say). Scale 1 is its own size. */
WGF_API bool wgf_node_set_position(wgf_node_t node, float x, float y, float z);
WGF_API wgf_vec3_t wgf_node_get_position(wgf_node_t node);
WGF_API bool wgf_node_set_rotation(wgf_node_t node, float x, float y, float z);
WGF_API wgf_vec3_t wgf_node_get_rotation(wgf_node_t node);
WGF_API bool wgf_node_set_scale(wgf_node_t node, float x, float y, float z);
WGF_API wgf_vec3_t wgf_node_get_scale(wgf_node_t node);
/* All three at once: the cheapest way to move something each frame. */
WGF_API bool wgf_node_set_transform(wgf_node_t node, float position_x, float position_y,
                                          float position_z, float rotation_x, float rotation_y, float rotation_z,
                                          float scale_x, float scale_y, float scale_z);

/* Where the node is in its tree's root's space: in a canvas, canvas units. */
WGF_API wgf_vec3_t wgf_node_get_world_position(wgf_node_t node);

/* A name to find the node by; NULL or "" for none, the default. get_name is "" for none: borrowed, valid until the
 * name changes or the node goes. Names needn't be unique. */
WGF_API bool wgf_node_set_name(wgf_node_t node, const char *name);
WGF_API const char *wgf_node_get_name(wgf_node_t node);

/* The first node named `name` in the tree from `root`, `root` included, depth
 * first, children in order; 0 for none. */
WGF_API wgf_node_t wgf_node_find(wgf_node_t root, const char *name);

/* Two flags say what a node takes part in, each the node's own, read back as set:
 *
 *   enabled   off: the node is skipped altogether, with everything under it, as if
 *             it weren't in the tree: the switch on a part of the game that is off
 *             for now. The only one that reaches the children; their own flags are
 *             kept, so enabling it again brings each back as it was set
 *   visible   off: the node's own output is off -- a sprite, shape, text, or
 *             emitter isn't drawn -- while it is still placed and its children are
 *             as they are
 *
 * Defaults: enabled, visible. */
WGF_API bool wgf_node_set_enabled(wgf_node_t node, bool enabled);
WGF_API bool wgf_node_is_enabled(wgf_node_t node);

WGF_API bool wgf_node_set_visible(wgf_node_t node, bool visible);
WGF_API bool wgf_node_is_visible(wgf_node_t node);

#ifdef __cplusplus
}
#endif

#endif
