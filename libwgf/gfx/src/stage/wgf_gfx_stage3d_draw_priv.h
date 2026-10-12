#ifndef WGF_GFX_STAGE3D_DRAW_PRIV_H
#define WGF_GFX_STAGE3D_DRAW_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "sokol_gfx.h"
#include "stage/wgf_gfx_environment_priv.h"
#include "stage/wgf_gfx_shadow_priv.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "wgf_mat4.h"
#include "wgf_material.h"
#include "wgf_mesh.h"

/* A stage's frame as wgf_gfx_stage3d.c records it -- its items, its draws, and the GPU's
 * objects they are drawn with -- for the code that draws an item it doesn't: a custom
 * material's (wgf_gfx_stage3d_custom.c), reached through the material's hooks
 * (material/wgf_gfx_material_priv.h), so a program without one links none of it. */

typedef struct wgf_gfx_priv_stage3d_item_t {
    wgf_mat4_t world;
    wgf_mesh_t mesh;
    int primitive;
    wgf_material_t material;
    float tint[4];   /* linear */
    bool blended;    /* drawn after the opaque ones, back to front, depth not written */
    bool additive;   /* an ADD emitter's: after the blended ones */
    int particles;   /* an emitter's draw (the particles part's); -1, a model's part */
    int sprite;      /* a see-through sprite's, into the frame's sprites, until it is recorded; -1 none */
    int sprite_batch, sprite_batches; /* sprites: their first batch and how many (0: in the one before) */
    int text;        /* a text's: its index into the draw's texts until recorded, then its side layer; -1 none */
    int record;      /* a model part's, in the instance texture: model parts numbered in drawing order */
    bool mirrored;   /* its placement turns its triangles' winding (a negative determinant) */
    float depth;     /* along the camera's view, for the sort */
    int order;       /* as found, so equal depths keep it */
    uint64_t group;  /* its group key's hash: alike opaque ones sorted together */
    int lights[WGF_GFX_PRIV_MAX_DRAW_LIGHTS]; /* into the draw's lights */
    int light_count;
    bool casting, receiving; /* shadows: its model's */
    bool shadow_only;        /* out of view, kept for its shadow: drawn into the shadow map alone */
    wgf_vec3_t box_lo, box_hi; /* its model's box in the stage */
} wgf_gfx_priv_stage3d_item_t;

typedef struct wgf_gfx_priv_stage3d_draw_t {
    int first, count; /* its items */
    int first_light, light_count;
    int shapes; /* its 3D shapes' side layer of immediate mode (-1: none), drawn after its opaque models */
    wgf_actor_t stage;
    int pass;            /* the frame's pass it is drawn in (0 the screen, else a render target) */
    bool flipped;        /* drawn with clip space's y flipped (a render target on GL) */
    wgf_mat4_t view_proj;
    wgf_mat4_t clip_proj; /* what its own drawing sees through: view_proj, flipped when it is */
    wgf_vec3_t camera_position;
    float ambient[3]; /* linear, times its intensity */
    int tonemap;
    float exposure;
    int viewport[4]; /* the visible area, in the pass's pixels */
    wgf_gfx_priv_shadow_camera_t shadow_camera; /* what its shadows are fitted around */
    int shadow_count;                           /* its lights with a shadow slot */
    int shadow_lights[WGF_GFX_PRIV_MAX_SHADOW_LIGHTS]; /* into the frame's lights, by slot */
    wgf_gfx_priv_shadow_binding_t shadow;       /* the shadow pass's maps, valid once it drew them */
    wgf_handle_t environment, background;       /* 0: none */
    float environment_intensity, environment_rotation;
    float background_intensity, background_rotation, background_blur;
} wgf_gfx_priv_stage3d_draw_t;

typedef struct wgf_gfx_priv_stage3d_frame_t {
    wgf_gfx_priv_stage3d_item_t *items;
    int item_count, item_capacity;
    struct text_record_t *texts; /* a draw's texts, for the sort */
    int text_count, text_capacity;
    struct sprite_record_t *sprites; /* a draw's see-through sprites, for the sort */
    int sprite_count, sprite_capacity;
    wgf_gfx_priv_stage3d_draw_t *draws;
    int draw_count, draw_capacity;
    int shadowed; /* the draw the shadows were fit for (-1: none) */
    wgf_gfx_priv_stage3d_light_t *lights;
    int light_count, light_capacity;
    wgf_actor_t *todo; /* the walk's */
    int todo_capacity;
    /* the GPU's, made at the first replay */
    bool ready;
    sg_shader shader;
    sg_pipeline pipelines[8]; /* by blended, double sided, mirrored */
    sg_image white_image, flat_normal_image;
    sg_view white_view, flat_normal_view;
    sg_sampler sampler;
    sg_image no_shadow_image; /* an empty depth array, bound when nothing casts */
    sg_view no_shadow_view;
    sg_sampler no_shadow_sampler;
    sg_image no_environment_image; /* a black cube, bound with no environment */
    sg_view no_environment_view;
    /* the instance records: an item's at its index, written as the frame flushes */
    float *instances;
    int instance_capacity; /* records */
    sg_image instance_image;
    sg_view instance_view;
    sg_sampler instance_sampler;
    int instance_rows;
    bool instances_written;       /* this frame's are up */
    bool instance_overflow_logged;
    int draw_calls, last_draw_calls; /* for tests: the shading pass's draws */
} wgf_gfx_priv_stage3d_frame_t;

/* The custom part's (wgf_gfx_stage3d_custom.c), the custom material's hooks' draw: items
 * `first` to `first + count - 1` of `frame`, alike (a run sharing one custom material),
 * drawn by its surface shader's model program through `draw`'s camera, shadowed by
 * `shadow`, lit by `env` (NULL: no environment). */
void wgf_gfx_priv_stage3d_draw_custom(wgf_gfx_priv_stage3d_frame_t *frame, const wgf_gfx_priv_stage3d_draw_t *draw,
                                      int first, int count, const wgf_gfx_priv_shadow_binding_t *shadow,
                                      const wgf_gfx_priv_env_lighting_t *env);

#endif
