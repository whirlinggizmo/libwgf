#ifndef WGF_GFX_ACTOR_PRIV_H
#define WGF_GFX_ACTOR_PRIV_H

#include "wgf_core_handle_priv.h" /* WGF_CORE_PRIV_CALLER */
#include <stdbool.h>

#include "wgf_color.h"
#include "wgf_font.h"
#include "wgf_mat4.h"
#include "wgf_material.h"
#include "wgf_actor.h"
#include "wgf_mesh.h"
#include "wgf_quat.h"
#include "wgf_text.h"
#include "wgf_texture.h"
#include "wgf_vec3.h"

/* Actors, for gfx's own code: one pool for every type, each type's own fields beside
 * the shared ones, and the transforms as matrices. libwgt's, with its 2D types. */

typedef struct wgf_gfx_priv_sprite_t {
    /* what a 2D stage's walk reads first (wgf_gfx_priv_actor_t's) */
    wgf_texture_t texture;
    wgf_color_t tint;
    int alpha_mode; /* wgf_alpha_mode_t */
    float source[4]; /* x, y, width, height; width or height <= 0: the whole texture */
    float size[2];   /* <= 0: the source's size */
    float pivot[2];
    float alpha_cutoff; /* MASK's */
    int facing;         /* wgf_sprite_facing_t: on a 3D stage */
    /* a frame trimmed when packed (a sheet's atlas): its texels' offset into the whole
       picture, and the picture's size; trim[2] <= 0: none. The sprite is the whole picture:
       its default size, and its pivot a fraction of it; what is drawn, the region within it. */
    float trim[4];
    float nine[4]; /* a nine-slice's borders in texels (left, top, right, bottom); all 0: none */
    float pick_alpha; /* a pick passes through its texels with less alpha (0..1); 0: its whole quad */
    float depth_offset; /* on a 3D stage: drawn this far nearer the camera, on its line of sight */
    wgf_material_t material; /* on a 3D stage: drawn with it, as a model is (wgf_sprite_set_material); 0 none */
} wgf_gfx_priv_sprite_t;

/* A trimmed sprite's drawing: the region's size `region_w` by `region_h` within the whole
 * picture drawn at `size`, its pivot `pivot`, into the size and pivot of the region's own
 * quad (as they are for a sprite not trimmed). */
static inline void wgf_gfx_priv_sprite_trimmed(const wgf_gfx_priv_sprite_t *sprite, float region_w, float region_h,
                                               float size[2], float pivot[2])
{
    if (sprite->trim[2] <= 0.0f || sprite->trim[3] <= 0.0f || region_w <= 0.0f || region_h <= 0.0f) return;
    pivot[0] = (pivot[0] * sprite->trim[2] - sprite->trim[0]) / region_w;
    pivot[1] = (pivot[1] * sprite->trim[3] - sprite->trim[1]) / region_h;
    size[0] *= region_w / sprite->trim[2];
    size[1] *= region_h / sprite->trim[3];
}

/* Its trim set (a sheet's frame showing): offset x, y into a whole picture `w` by `h`; w or h
 * <= 0 for none. */
void wgf_gfx_priv_sprite_set_trim(wgf_actor_t sprite, float x, float y, float w, float h);

typedef struct wgf_gfx_priv_shape2d_t {
    int kind;      /* wgf_shape2d_kind_t */
    float dim[5];  /* rectangle: width, height; circle: radius; line: x0, y0, x1, y1 */
    float *points; /* a polygon's, x and y in turn, malloc'd */
    int point_floats;
    float pivot[2]; /* a fraction of a rectangle or circle; 0.5, 0.5 (its center) as made */
    float outline;  /* the line's thickness for outlines and lines; 0 fills (a line: a hairline) */
    float corner;   /* a rectangle's corners' radius, in its own units; 0 square */
    wgf_color_t color;
} wgf_gfx_priv_shape2d_t;

typedef struct wgf_gfx_priv_stage2d_t {
    wgf_actor_t camera;
    unsigned drawn_frame; /* when it was last drawn on the screen, and its place then (render_note_drawn) */
    int drawn_order;
} wgf_gfx_priv_stage2d_t;

typedef struct wgf_gfx_priv_text_t {
    wgf_font_t font;
    char *string; /* malloc'd; NULL for none */
    float size;   /* 0: unset, the default */
    wgf_color_t color;
    float wrap_width;
    wgf_text_halign_t halign;
    wgf_text_valign_t valign;
    int facing; /* on a stage3d: a wgf_sprite_facing_t (CAMERA by default) */
    float letter_spacing, line_spacing; /* logical pixels added */
    float depth_offset; /* on a stage3d: drawn this far nearer the camera, on its line of sight */
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
    /* its environment (wgf_environment.h; a reference held on each, released as the stage
       goes): the one lighting it, and the one behind it */
    wgf_handle_t environment;
    float environment_intensity, environment_rotation;
    wgf_handle_t background;
    float background_blur;
    unsigned drawn_frame; /* when it was last drawn on the screen through its camera, and its place then */
    int drawn_order;
} wgf_gfx_priv_stage3d_t;

typedef struct wgf_gfx_priv_light_t {
    int type; /* wgf_light_type_t */
    wgf_color_t color;
    float intensity;
    float range;
    float inner_angle, outer_angle;
    /* its shadows (wgf_gfx_shadow.c's calls; their defaults set as it is made) */
    bool shadow_casting;
    float shadow_distance;
    int shadow_map_size;
    float shadow_strength;
    wgf_color_t shadow_color;
    float shadow_bias_constant, shadow_bias_slope; /* shadow texels */
} wgf_gfx_priv_light_t;

/* Material slots a model can draw with its own material, as wgrender's */
#define WGF_GFX_PRIV_MODEL_MATERIAL_SLOTS 32

typedef struct wgf_gfx_priv_model_t {
    wgf_handle_t mesh;         /* referenced; 0 none */
    wgf_color_t tint;
    wgf_handle_t *materials;   /* its own, a slot each, referenced (0: the mesh's); malloc'd, NULL for none */
    bool file_root; /* of a glTF file's mesh: its tree is actors under it, and it draws nothing itself */
    bool shadow_off, receive_off; /* casting and receiving shadows, each on unless set off (wgf_gfx_shadow.c) */
    /* a file root's, once its tree is made (stage/wgf_gfx_model_priv.h): its nodes' actors and
       its skins' joints for the frame; malloc'd, NULL for any other model */
    struct wgf_gfx_priv_model_tree_t *tree;
    /* a file root's animation player (stage/wgf_gfx_animation.c), once an animation call made
       one; malloc'd, NULL for none */
    struct wgf_gfx_priv_animation_t *animation;
    /* a file's node showing a skinned mesh: the root whose skin `skin` (into its file's skins)
       places its skinned parts; 0 for none */
    wgf_actor_t skin_root;
    int skin;
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
    /* first what a 2D stage's walk reads of each actor, together, then the rest: drawing
       50,000 sprites in a page was bound by the cache lines each record touched */
    wgf_actor_kind_t type;
    int live_children; /* children, holes not counted */
    bool enabled;
    bool visible;
    bool world_dirty;
    bool child_holes;
    /* Simulated (it has a component the ticks move, ecs/): `position`, `rotation`, and
     * `scale` are the simulation's, at the tick rate, and the actor is drawn between
     * `previous`, kept as the last tick began, and them, at the frame's tick fraction.
     * malloc'd; NULL for an actor nothing simulates, so it carries none of it (the actor
     * benchmark's: 40 bytes an actor, and its pool's slack). */
    wgf_gfx_priv_previous_t *previous;
    wgf_vec3_t position;
    wgf_quat_t rotation; /* a quaternion; the public calls give and take angles */
    wgf_vec3_t scale;
    union { /* a sprite's hot fields first (wgf_gfx_priv_sprite_t) */
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
        struct wgf_gfx_priv_sprite_batch_t *sprite_batch; /* malloc'd (sprite/wgf_gfx_sprite_batch2d.c) */
        struct wgf_gfx_priv_tilemap_layer_t *tilemap_layer; /* malloc'd (tilemap/wgf_gfx_tilemap_layer.c) */
    } as;
    char *name; /* malloc'd; NULL for none */
    wgf_actor_t parent;
    int slot; /* where it is in its parent's children */
    /* Its children, in drawing order. A child that leaves leaves a hole (0), so leaving
     * costs nothing however many siblings it has; the holes are closed in one pass when
     * the array is next read whole (wgf_gfx_priv_actor_children). */
    wgf_actor_t *children;
    int child_count;    /* slots in use, holes included */
    int child_capacity;
    /* Its world transform as a matrix, kept until it or anything above it changes. An
     * actor whose world is dirty has every actor under it dirty too. Its local one isn't
     * kept: it is only read to make this one (64 bytes an actor). */
    wgf_mat4_t world;
    bool paused; /* it and everything under it frozen: drawn, not moved on */
    bool pickable; /* hit by a pick (wgf_stage2d_pick, wgf_stage3d_pick); off, a pick passes through it */
    bool from_file; /* made from a glTF file's nodes, under its root: the root's path makes it again */
    wgf_handle_t components; /* the ecs's record of its components and behaviors; 0 for none */
} wgf_gfx_priv_actor_t;

/* A 2D sprite's quad as it is drawn, in its own units (x0, y0, x1, y1), and the texels it
 * shows (x, y, width, height); `whole` when its texels don't map straight onto the quad (a
 * nine-slice, or the placeholder). False for an actor that isn't a sprite, or one with
 * nothing to draw (no texture, or one PENDING). */
bool wgf_gfx_priv_sprite_quad_2d(const wgf_gfx_priv_actor_t *actor_ptr, float quad[4], float region[4], bool *whole);

/* `actor`'s children, dense and in order (its holes closed first, if it has any), and how
 * many: valid until the tree under it next changes. NULL and 0 for none. */
const wgf_actor_t *wgf_gfx_priv_actor_children(wgf_actor_t actor, int *count);

/* A new actor of `type`, with the shared fields at their defaults and the rest
 * zeroed; 0 when there is no room. */
wgf_actor_t wgf_gfx_priv_actor_create(wgf_actor_kind_t type);

/* Its slot in the actors' pool: what a part keeping something for some actors indexes by. */
int wgf_gfx_priv_actor_index_of(const wgf_gfx_priv_actor_t *actor_ptr);

/* The one actor named `name` on the tree whose root is `stage`, the stage itself not
 * counted, found by the names' index; 0 for none, and 0 (warned) for two or more. */
wgf_actor_t wgf_gfx_priv_actor_find_on_stage(wgf_actor_t stage, const char *name);

/* The root of the tree `actor` is in: its stage, or the actor itself when it has no parent. */
wgf_actor_t wgf_gfx_priv_actor_get_root(wgf_actor_t actor);

/* The actor `actor` names; NULL for anything else. The pointer moves when an actor is
 * created: don't keep it across one. */
wgf_gfx_priv_actor_t *wgf_gfx_priv_actor_of_at(wgf_actor_t actor, const char *caller);
/* The same, never warned: a check that a handle is an actor. */
wgf_gfx_priv_actor_t *wgf_gfx_priv_actor_of_silently(wgf_actor_t actor);
/* The actor's record, NULL for a handle that isn't a live actor (a dead one warned of in a
 * debug build, naming the call it reached: wgf_core_priv_handle_stale). A release build,
 * which warns of nothing, resolves without the caller: a find resolves one an actor it
 * passes, and the caller's argument and warning branch cost a name find 14%. */
#ifdef NDEBUG
#define wgf_gfx_priv_actor_of(actor) wgf_gfx_priv_actor_of_silently(actor)
#else
#define wgf_gfx_priv_actor_of(actor) wgf_gfx_priv_actor_of_at((actor), WGF_CORE_PRIV_CALLER)
#endif

/* Whether `actor` or one above it is paused (wgf_actor_set_paused): what moves by itself under
 * it holds still. */
bool wgf_gfx_priv_actor_is_frozen(wgf_actor_t actor);

/* The actor's own transform, worked each time, and its world transform (every parent's,
 * composed), rebuilt only when it is dirty. */
wgf_mat4_t wgf_gfx_priv_actor_get_local_matrix(const wgf_gfx_priv_actor_t *actor_ptr);
wgf_mat4_t wgf_gfx_priv_actor_get_world_matrix(wgf_actor_t actor);

/* For a walk of the tree that already holds `actor`'s record (its pointer, good while
 * no actor is created): its world matrix and its children, without looking it up again.
 * Its world, when its parent's is clean (a walk top down has made it so), is one
 * composing; otherwise as get_world_matrix. */
wgf_mat4_t wgf_gfx_priv_actor_world_of(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr);
/* As world_of, for a child whose parent's record is in hand with its world clean: a walk
 * top down, which made the parent clean just before. No lookup. */
const wgf_mat4_t *wgf_gfx_priv_actor_world_under(wgf_gfx_priv_actor_t *actor_ptr,
                                                 const wgf_gfx_priv_actor_t *parent_ptr);
const wgf_actor_t *wgf_gfx_priv_actor_children_of(wgf_gfx_priv_actor_t *actor_ptr, int *count);

/* The actor's own transform changed: its world matrix, and everything under it, are
 * dirty. */
void wgf_gfx_priv_actor_transform_changed(wgf_actor_t actor);

/* The type of the tree `actor` is in: the type of its topmost ancestor, or its own
 * with none. */
wgf_actor_kind_t wgf_gfx_priv_actor_get_root_type(wgf_actor_t actor);

/* What only the part making an actor type knows, set by that part at its first create,
 * so freeing or drawing an actor names no part: a program that makes no emitter links no
 * emitter. NULL again after gfx's stop, and a NULL member does nothing. */
/* A 3D camera's axes for things facing it (wgf_gfx_sprite3d.c's wgf_gfx_priv_sprite_view_of):
 * spherical (facing CAMERA, parallel to the view plane, `right` and `up`) and cylindrical
 * (CAMERA_FIXED_Y: turning about world Y, staying upright, `upright` its right; the
 * camera's own right when it looks straight up or down). */
typedef struct wgf_gfx_priv_sprite_view_t {
    wgf_vec3_t right, up, upright;
} wgf_gfx_priv_sprite_view_t;

typedef struct wgf_gfx_priv_actor_kind_t {
    /* what it holds let go of: a sprite's texture reference, a text's font reference
       and string, an emitter's particles */
    void (*free)(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr);
    /* drawn on a 2D stage: `placed` is its world matrix through the stage's view, into the
       frame's logical pixels; `view` the view alone (stage units to logical pixels) */
    void (*draw)(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *placed,
                 const wgf_mat4_t *view);
    /* placed on a 3D stage (text's): the matrix its block's plane goes into the stage by,
       and its corners there (culling's); false with nothing to show */
    bool (*place_3d)(const wgf_gfx_priv_actor_t *actor_ptr, wgf_mat4_t world, const wgf_gfx_priv_sprite_view_t *view,
                     wgf_mat4_t *plane, wgf_vec3_t corners[4]);
    /* drawn on a 3D stage through `plane`, into the immediate-mode layer current */
    void (*draw_3d)(const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *plane);
    /* a 2D stage's fast way for one with no children (a sprite's): drawn as `draw` would
       draw it, placed in its parent, `parent` the parent's world matrix through the view,
       without making its own world matrix. False, having drawn nothing, when it can't
       (turned, simulated, its quad not kept): the walk then draws it the usual way. */
    bool (*draw_leaf_2d)(const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *parent);
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
    bool (*set_path)(wgf_actor_t model, const char *path); /* a glTF file's mesh (wgf_mesh_create) */
    const char *(*get_path)(wgf_actor_t model);            /* a file root's path; NULL for any other model */
    /* whether it casts (`cast`) or receives shadows, set and read without the shadow part */
    void (*set_shadows)(wgf_actor_t model, bool cast, bool on);
    bool (*get_shadows)(wgf_actor_t model, bool cast);
} wgf_gfx_priv_model_hooks_t;
void wgf_gfx_priv_set_model_hooks(const wgf_gfx_priv_model_hooks_t *hooks);
const wgf_gfx_priv_model_hooks_t *wgf_gfx_priv_get_model_hooks(void);

/* A scene's way to emitters (wgf_gfx_emitter_scene.c), set as the emitter part's file is
 * linked, so a program loading scenes links no particles unless it makes an emitter (or,
 * a game, its scenes name one: tools/wgf/game.py keeps them): an emitter made, 2D or 3D;
 * a key of its scene line applied, its value parsed (numbers, colors, a truth, or text);
 * what is done once it is placed (a prewarm, a burst); and its line, as a scene writes it,
 * without its indent or end (borrowed until the next). NULL when it isn't linked. */
typedef struct wgf_gfx_priv_emitter_scene_t {
    wgf_actor_t (*create)(bool two_d);
    void (*apply)(wgf_actor_t emitter, const char *key, const double *numbers, int count, const wgf_color_t *colors,
                  int color_count, bool truth, const char *text);
    void (*placed)(wgf_actor_t emitter, float prewarm, int burst);
    const char *(*line)(wgf_actor_t emitter);
} wgf_gfx_priv_emitter_scene_t;
void wgf_gfx_priv_set_emitter_scene(const wgf_gfx_priv_emitter_scene_t *hooks);
const wgf_gfx_priv_emitter_scene_t *wgf_gfx_priv_get_emitter_scene(void);

/* The hooks above set: the stage's first create. */
void wgf_gfx_priv_model_install(void);

/* A glTF file's root as its file's load ends: its tree made, or made again in place (the
 * file loaded again), or its file FAILED (its mesh's status says which). Called by the file
 * part, it calls each hook added (up to 4, each once): the ecs's, by a scene with blocks
 * under such a root, made then; physics', whose bodies over the tree are made again. False
 * when there is no room for another. */
bool wgf_gfx_priv_add_model_built_hook(void (*built)(wgf_actor_t root));
void wgf_gfx_priv_model_built(wgf_actor_t root);
/* A file's mesh done loading, or loaded again, told to each of the other files' roots: the
 * animation part's hook, for a root that takes its clips from that file
 * (wgf_model_set_animations). Set as the part installs; nothing before. */
void wgf_gfx_priv_set_model_clips_hook(void (*loaded)(wgf_actor_t root, wgf_mesh_t clips));
void wgf_gfx_priv_model_clips_loaded(wgf_actor_t root, wgf_mesh_t clips);

/* An actor made simulated, or not: from here it is drawn between its last two ticks'
 * transforms (both its own as it is now, until a tick moves it). And the ecs's hook for an
 * actor with components going: its components and behaviors let go of, before the actor. */
void wgf_gfx_priv_actor_set_simulated(wgf_gfx_priv_actor_t *actor_ptr, bool simulated);

/* `actor`'s world matrix from the simulation's transforms, its own and every one above it
 * (the cached world matrix is the drawn one, between the ticks): where wgf_actor_get_world_position
 * says it is, and physics' place for its body. The identity for a handle that isn't an actor. */
wgf_mat4_t wgf_gfx_priv_actor_get_simulated_world(wgf_actor_t actor);
void wgf_gfx_priv_actor_set_components_hook(void (*gone)(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr));

/* With the surface: every actor destroyed at shutdown, its references released. */
void wgf_gfx_priv_actor_shutdown(void);

#endif
