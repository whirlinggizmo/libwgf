#ifndef WGF_GFX_NODE_PRIV_H
#define WGF_GFX_NODE_PRIV_H

#include <stdbool.h>

#include "wgf_color.h"
#include "wgf_font.h"
#include "wgf_mat4.h"
#include "wgf_node.h"
#include "wgf_quat.h"
#include "wgf_text.h"
#include "wgf_texture.h"
#include "wgf_vec3.h"

/* Nodes, for gfx's own code: one pool for every type, each type's own fields beside
 * the shared ones, and the transforms as matrices. libwgt's, with its 2D types. */

typedef struct wgf_gfx_priv_sprite_t {
    wgf_texture_t texture;
    float source[4]; /* x, y, width, height; width or height <= 0: the whole texture */
    float size[2];   /* <= 0: the source's size */
    float pivot[2];
    wgf_color_t tint;
} wgf_gfx_priv_sprite_t;

typedef struct wgf_gfx_priv_shape2d_t {
    int kind;      /* wgf_shape2d_kind_t */
    float dim[5];  /* rectangle: width, height; circle: radius; line: x0, y0, x1, y1 */
    float *points; /* a polygon's, x and y in turn, malloc'd */
    int point_floats;
    float pivot[2];
    bool has_pivot; /* false: the kind's own origin */
    float outline;  /* the line's thickness for outlines and lines; 0 fills (a line: a hairline) */
    wgf_color_t color;
} wgf_gfx_priv_shape2d_t;

typedef struct wgf_gfx_priv_canvas_t {
    wgf_node_t camera;
} wgf_gfx_priv_canvas_t;

typedef struct wgf_gfx_priv_text_t {
    wgf_font_t font;
    char *string; /* malloc'd; NULL for none */
    float size;   /* 0: unset, the default */
    wgf_color_t color;
    float wrap_width;
    wgf_text_halign_t halign;
    wgf_text_valign_t valign;
} wgf_gfx_priv_text_t;

typedef struct wgf_gfx_priv_node_t {
    wgf_node_type_t type;
    char *name; /* malloc'd; NULL for none */
    wgf_node_t parent;
    int slot; /* where it is in its parent's children */
    /* Its children, in drawing order. A child that leaves leaves a hole (0), so leaving
     * costs nothing however many siblings it has; the holes are closed in one pass when
     * the array is next read whole (wgf_gfx_priv_node_children). */
    wgf_node_t *children;
    int child_count;    /* slots in use, holes included */
    int child_capacity;
    int live_children;  /* children, holes not counted */
    bool child_holes;
    wgf_vec3_t position;
    wgf_quat_t rotation; /* a quaternion; the public calls give and take angles */
    wgf_vec3_t scale;
    /* The transforms as matrices, kept until something changes them: the local one
     * when the node's own transform changes, the world one when it or anything above
     * it does. A node whose world is dirty has every node under it dirty too. */
    wgf_mat4_t local;
    wgf_mat4_t world;
    bool local_dirty;
    bool world_dirty;
    bool enabled;
    bool visible;
    union {
        wgf_gfx_priv_sprite_t sprite;
        wgf_gfx_priv_text_t text;
        float camera2d_zoom;
        wgf_gfx_priv_canvas_t canvas;
        wgf_gfx_priv_shape2d_t shape2d;
        struct wgf_gfx_priv_emitter_t *emitter; /* malloc'd (emitter/wgf_gfx_emitter_priv.h) */
    } as;
} wgf_gfx_priv_node_t;

/* `node`'s children, dense and in order (its holes closed first, if it has any), and how
 * many: valid until the tree under it next changes. NULL and 0 for none. */
const wgf_node_t *wgf_gfx_priv_node_children(wgf_node_t node, int *count);

/* A new node of `type`, with the shared fields at their defaults and the rest
 * zeroed; 0 when there is no room. */
wgf_node_t wgf_gfx_priv_node_create(wgf_node_type_t type);

/* The node `node` names; NULL for anything else. The pointer moves when a node is
 * created: don't keep it across one. */
wgf_gfx_priv_node_t *wgf_gfx_priv_node_of(wgf_node_t node);

/* The node's own transform, and its world transform (every parent's, composed),
 * each rebuilt only when it is dirty. */
wgf_mat4_t wgf_gfx_priv_node_get_local_matrix(wgf_gfx_priv_node_t *node_ptr);
wgf_mat4_t wgf_gfx_priv_node_get_world_matrix(wgf_node_t node);

/* For a walk of the tree that already holds `node`'s record (its pointer, good while
 * no node is created): its world matrix and its children, without looking it up again.
 * Its world, when its parent's is clean (a walk top down has made it so), is one
 * composing; otherwise as get_world_matrix. */
wgf_mat4_t wgf_gfx_priv_node_world_of(wgf_node_t node, wgf_gfx_priv_node_t *node_ptr);
const wgf_node_t *wgf_gfx_priv_node_children_of(wgf_gfx_priv_node_t *node_ptr, int *count);

/* The node's own transform changed: its local matrix, and its world matrix and
 * everything under it, are dirty. */
void wgf_gfx_priv_node_transform_changed(wgf_node_t node);

/* The type of the tree `node` is in: the type of its topmost ancestor, or its own
 * with none. */
wgf_node_type_t wgf_gfx_priv_node_get_root_type(wgf_node_t node);

/* What only the part making a node type knows, set by that part at its first create,
 * so freeing or drawing a node names no part: a program that makes no emitter links no
 * emitter. NULL again after gfx's stop, and a NULL member does nothing. */
typedef struct wgf_gfx_priv_node_kind_t {
    /* what it holds let go of: a sprite's texture reference, a text's font reference
       and string, an emitter's particles */
    void (*free)(wgf_node_t node, wgf_gfx_priv_node_t *node_ptr);
    /* drawn in a canvas: `placed` is its world matrix through the canvas's view, into the
       frame's logical pixels; `view` the view alone (canvas units to logical pixels) */
    void (*draw)(wgf_node_t node, const wgf_gfx_priv_node_t *node_ptr, const wgf_mat4_t *placed,
                 const wgf_mat4_t *view);
} wgf_gfx_priv_node_kind_t;
void wgf_gfx_priv_node_set_kind(wgf_node_type_t type, const wgf_gfx_priv_node_kind_t *kind);
const wgf_gfx_priv_node_kind_t *wgf_gfx_priv_node_get_kind(wgf_node_type_t type); /* NULL when unset */

/* With the surface: every node destroyed at shutdown, its references released. */
void wgf_gfx_priv_node_shutdown(void);

#endif
