#ifndef WGF_GFX_ACTOR_PRIV_H
#define WGF_GFX_ACTOR_PRIV_H

#include "wgf_core_handle_priv.h" /* WGF_CORE_PRIV_CALLER */
#include <stdbool.h>

#include "wgf_color.h"
#include "wgf_font.h"
#include "wgf_mat4.h"
#include "wgf_actor.h"
#include "wgf_quat.h"
#include "wgf_text.h"
#include "wgf_texture.h"
#include "wgf_vec3.h"

/* Actors, for gfx's own code: one pool for every type, each type's own fields beside
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

typedef struct wgf_gfx_priv_stage2d_t {
    wgf_actor_t camera;
} wgf_gfx_priv_stage2d_t;

typedef struct wgf_gfx_priv_text_t {
    wgf_font_t font;
    char *string; /* malloc'd; NULL for none */
    float size;   /* 0: unset, the default */
    wgf_color_t color;
    float wrap_width;
    wgf_text_halign_t halign;
    wgf_text_valign_t valign;
} wgf_gfx_priv_text_t;

typedef struct wgf_gfx_priv_camera3d_t {
    float fov; /* vertical, radians */
    float near_z, far_z;
    bool orthographic;
    float ortho_height;
} wgf_gfx_priv_camera3d_t;

typedef struct wgf_gfx_priv_stage3d_t {
    wgf_actor_t camera;
    wgf_color_t ambient_color;
    float ambient_intensity;
    int tonemap; /* wgf_stage3d_tonemap_t */
    float exposure;
    bool culling;
} wgf_gfx_priv_stage3d_t;

typedef struct wgf_gfx_priv_light_t {
    int type; /* wgf_light_type_t */
    wgf_color_t color;
    float intensity;
    float range;
    float inner_angle, outer_angle;
} wgf_gfx_priv_light_t;

/* Material slots a model can draw with its own material, as wgrender's */
#define WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS 32

typedef struct wgf_gfx_priv_model_t {
    wgf_handle_t mesh;         /* referenced; 0 none */
    wgf_color_t tint;
    wgf_handle_t *materials;   /* its own, a slot each, referenced (0: the mesh's); malloc'd, NULL for none */
} wgf_gfx_priv_model_t;

typedef struct wgf_gfx_priv_shape3d_t {
    int kind;      /* wgf_shape3d_kind_t */
    float dim[6];  /* cube: width, height, length; sphere, circle: radius; rectangle: width, height; line: ends */
    float *points; /* a line strip's, x, y, and z in turn, malloc'd */
    int point_floats;
    wgf_color_t color;
} wgf_gfx_priv_shape3d_t;

/* A simulated actor's transform as the last tick began: what it is drawn from. */
typedef struct wgf_gfx_priv_previous_t {
    wgf_vec3_t position, scale;
    wgf_quat_t rotation;
} wgf_gfx_priv_previous_t;

typedef struct wgf_gfx_priv_actor_t {
    wgf_actor_kind_t type;
    char *name; /* malloc'd; NULL for none */
    wgf_actor_t parent;
    int slot; /* where it is in its parent's children */
    /* Its children, in drawing order. A child that leaves leaves a hole (0), so leaving
     * costs nothing however many siblings it has; the holes are closed in one pass when
     * the array is next read whole (wgf_gfx_priv_actor_children). */
    wgf_actor_t *children;
    int child_count;    /* slots in use, holes included */
    int child_capacity;
    int live_children;  /* children, holes not counted */
    bool child_holes;
    wgf_vec3_t position;
    wgf_quat_t rotation; /* a quaternion; the public calls give and take angles */
    wgf_vec3_t scale;
    /* The transforms as matrices, kept until something changes them: the local one
     * when the actor's own transform changes, the world one when it or anything above
     * it does. An actor whose world is dirty has every actor under it dirty too. */
    wgf_mat4_t local;
    wgf_mat4_t world;
    bool local_dirty;
    bool world_dirty;
    bool enabled;
    bool visible;
    /* Simulated (it has a component the ticks move, ecs/): `position`, `rotation`, and
     * `scale` are the simulation's, at the tick rate, and the actor is drawn between
     * `previous`, kept as the last tick began, and them, at the frame's tick fraction.
     * malloc'd; NULL for an actor nothing simulates, so it carries none of it (the actor
     * benchmark's: 40 bytes an actor, and its pool's slack). */
    wgf_gfx_priv_previous_t *previous;
    wgf_handle_t components; /* the ecs's record of its components and behaviors; 0 for none */
    union {
        wgf_gfx_priv_sprite_t sprite;
        wgf_gfx_priv_text_t text;
        float camera2d_zoom;
        wgf_gfx_priv_camera3d_t camera3d;
        wgf_gfx_priv_stage3d_t stage3d;
        wgf_gfx_priv_light_t light;
        wgf_gfx_priv_model_t model;
        wgf_gfx_priv_shape3d_t shape3d;
        wgf_gfx_priv_stage2d_t stage2d;
        wgf_gfx_priv_shape2d_t shape2d;
        struct wgf_gfx_priv_emitter_t *emitter; /* malloc'd (emitter/wgf_gfx_emitter_priv.h) */
    } as;
} wgf_gfx_priv_actor_t;

/* `actor`'s children, dense and in order (its holes closed first, if it has any), and how
 * many: valid until the tree under it next changes. NULL and 0 for none. */
const wgf_actor_t *wgf_gfx_priv_actor_children(wgf_actor_t actor, int *count);

/* A new actor of `type`, with the shared fields at their defaults and the rest
 * zeroed; 0 when there is no room. */
wgf_actor_t wgf_gfx_priv_actor_create(wgf_actor_kind_t type);

/* The one actor named `name` on the tree whose root is `stage`, the stage itself not
 * counted, found by the names' index; 0 for none, and 0 (warned) for two or more. */
wgf_actor_t wgf_gfx_priv_actor_find_on_stage(wgf_actor_t stage, const char *name);

/* The root of the tree `actor` is in: its stage, or the actor itself when it has no parent. */
wgf_actor_t wgf_gfx_priv_actor_get_root(wgf_actor_t actor);

/* The actor `actor` names; NULL for anything else. The pointer moves when an actor is
 * created: don't keep it across one. */
wgf_gfx_priv_actor_t *wgf_gfx_priv_actor_of_at(wgf_actor_t actor, const char *caller);
/* The actor's record, NULL for a handle that isn't a live actor (a dead one warned of in a
 * debug build, naming the call it reached: wgf_core_priv_handle_stale). */
#define wgf_gfx_priv_actor_of(actor) wgf_gfx_priv_actor_of_at((actor), WGF_CORE_PRIV_CALLER)

/* The actor's own transform, and its world transform (every parent's, composed),
 * each rebuilt only when it is dirty. */
wgf_mat4_t wgf_gfx_priv_actor_get_local_matrix(wgf_gfx_priv_actor_t *actor_ptr);
wgf_mat4_t wgf_gfx_priv_actor_get_world_matrix(wgf_actor_t actor);

/* For a walk of the tree that already holds `actor`'s record (its pointer, good while
 * no actor is created): its world matrix and its children, without looking it up again.
 * Its world, when its parent's is clean (a walk top down has made it so), is one
 * composing; otherwise as get_world_matrix. */
wgf_mat4_t wgf_gfx_priv_actor_world_of(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr);
const wgf_actor_t *wgf_gfx_priv_actor_children_of(wgf_gfx_priv_actor_t *actor_ptr, int *count);

/* The actor's own transform changed: its local matrix, and its world matrix and
 * everything under it, are dirty. */
void wgf_gfx_priv_actor_transform_changed(wgf_actor_t actor);

/* The type of the tree `actor` is in: the type of its topmost ancestor, or its own
 * with none. */
wgf_actor_kind_t wgf_gfx_priv_actor_get_root_type(wgf_actor_t actor);

/* What only the part making an actor type knows, set by that part at its first create,
 * so freeing or drawing an actor names no part: a program that makes no emitter links no
 * emitter. NULL again after gfx's stop, and a NULL member does nothing. */
typedef struct wgf_gfx_priv_actor_kind_t {
    /* what it holds let go of: a sprite's texture reference, a text's font reference
       and string, an emitter's particles */
    void (*free)(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr);
    /* drawn on a 2D stage: `placed` is its world matrix through the stage's view, into the
       frame's logical pixels; `view` the view alone (stage units to logical pixels) */
    void (*draw)(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *placed,
                 const wgf_mat4_t *view);
} wgf_gfx_priv_actor_kind_t;
void wgf_gfx_priv_actor_set_kind(wgf_actor_kind_t type, const wgf_gfx_priv_actor_kind_t *kind);
const wgf_gfx_priv_actor_kind_t *wgf_gfx_priv_actor_get_kind(wgf_actor_kind_t type); /* NULL when unset */

/* What a 3D camera sees, at `aspect` (width over height): its projection times its
 * view, into OpenGL's clip space, and where it is, in its tree's root's space. The
 * identity for an actor that isn't a 3D camera. */
wgf_mat4_t wgf_gfx_priv_camera3d_view_projection(wgf_actor_t camera, float aspect, wgf_vec3_t *position);

/* The ecs's model component's way to models (wgf_gfx_model.c), installed by the first
 * stage made, so a program loading scenes links no 3D until it makes a stage: a model
 * actor made; given the generated mesh `shape` names ("cube") with its create call's
 * parameters (false for a shape that isn't one); its mesh described so (NULL for one of
 * no generated shape); and its tint. NULL before a stage is made. */
typedef struct wgf_gfx_priv_model_hooks_t {
    wgf_actor_t (*create)(void);
    bool (*set_shape)(wgf_actor_t model, const char *shape, const float *params, int count);
    const char *(*describe)(wgf_actor_t model, float params[4], int *count);
    bool (*set_tint)(wgf_actor_t model, wgf_color_t tint);
    wgf_color_t (*get_tint)(wgf_actor_t model);
} wgf_gfx_priv_model_hooks_t;
void wgf_gfx_priv_set_model_hooks(const wgf_gfx_priv_model_hooks_t *hooks);
const wgf_gfx_priv_model_hooks_t *wgf_gfx_priv_get_model_hooks(void);

/* The hooks above set: the stage's first create. */
void wgf_gfx_priv_model_install(void);

/* An actor made simulated, or not: from here it is drawn between its last two ticks'
 * transforms (both its own as it is now, until a tick moves it). And the ecs's hook for an
 * actor with components going: its components and behaviors let go of, before the actor. */
void wgf_gfx_priv_actor_set_simulated(wgf_gfx_priv_actor_t *actor_ptr, bool simulated);
void wgf_gfx_priv_actor_set_components_hook(void (*gone)(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr));

/* With the surface: every actor destroyed at shutdown, its references released. */
void wgf_gfx_priv_actor_shutdown(void);

#endif
