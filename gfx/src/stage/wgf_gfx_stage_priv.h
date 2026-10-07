#ifndef WGF_GFX_STAGE_PRIV_H
#define WGF_GFX_STAGE_PRIV_H

#include <stdbool.h>

#include "node/wgf_gfx_node_priv.h"
#include "wgf_vec3.h"

/* Stages, their lights, and their models, for gfx's own code: lighting as libwgt's
 * (wgrender's wgr_light): a stage's draw gathers its lights into a list, and each model
 * is lit by the up to 8 of them that reach it most. */

#define WGF_GFX_PRIV_MAX_DRAW_LIGHTS 8   /* lights a model is drawn with (the shader's array) */
#define WGF_GFX_PRIV_MAX_STAGE_LIGHTS 64 /* lights considered in one stage's draw */

/* A light as it shades, in its stage's space. */
typedef struct wgf_gfx_priv_stage_light_t {
    int type;             /* wgf_light_type_t */
    wgf_vec3_t radiance;  /* linear color * intensity */
    wgf_vec3_t position;
    wgf_vec3_t direction; /* normalized, the way the light travels */
    float range;          /* 0 = unlimited */
    float cos_inner;
    float cos_outer;
} wgf_gfx_priv_stage_light_t;

/* `light` (a light node, its record `light_ptr`) as it shades, placed by `world`. */
void wgf_gfx_priv_light_resolve(const wgf_gfx_priv_node_t *light_ptr, wgf_mat4_t world,
                                wgf_gfx_priv_stage_light_t *out);

/* Up to `max_out` of the `count` lights for a model whose box in the stage is
 * [world_min, world_max], strongest estimated contribution first; equal ones keep the
 * lights' order. Writes indices into `lights` and returns how many. */
int wgf_gfx_priv_light_select(const wgf_gfx_priv_stage_light_t *lights, int count, wgf_vec3_t world_min,
                              wgf_vec3_t world_max, int *out_indices, int max_out);

/* For tests: how many parts the frame's stage draws kept (after culling), and how many
 * lights part `item` of them is lit by (-1 for none). */
int wgf_gfx_priv_stage_get_item_count(void);
int wgf_gfx_priv_stage_get_item_lights(int item);

#endif
