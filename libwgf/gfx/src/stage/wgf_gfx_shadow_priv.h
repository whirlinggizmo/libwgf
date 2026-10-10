#ifndef WGF_GFX_SHADOW_PRIV_H
#define WGF_GFX_SHADOW_PRIV_H

#include <stdbool.h>

#include "mesh/wgf_gfx_mesh_priv.h"
#include "sokol_gfx.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "wgf_light.h"
#include "wgf_mat4.h"
#include "wgf_material.h"
#include "wgf_vec3.h"

/* Shadow maps, libwgt's (wgrender's wgr_shadow): once a frame, before anything is shaded,
 * each casting light of the frame's first stage draw with any (up to 4) draws the casters
 * into its layer of one depth array, and the model shader compares each surface against it
 * through a comparison sampler. A part (wgf_gfx_shadow.c), installed by the first light set
 * to cast, so a program that never casts links none of it: the stage records what the pass
 * needs as it draws (each light's shadow settings, each part's casting and receiving, and
 * the casters out of view whose shadows reach it), and binds what the pass made, or an
 * empty map when nothing cast. */

#define WGF_GFX_PRIV_MAX_SHADOW_LIGHTS 4

/* What a light sees: the world -> its clip matrix (GL's -1..1 depth), the world size of
 * one shadow texel, and the world depth its map spans. */
typedef struct wgf_gfx_priv_shadow_fit_t {
    wgf_mat4_t view_proj;
    float texel_world;
    float depth_range;
    bool perspective; /* a spot's: depth stored non-linearly, between these planes */
    float near_z, far_z;
} wgf_gfx_priv_shadow_fit_t;

/* The camera a fit covers the view of: where it is and faces, and its lens. */
typedef struct wgf_gfx_priv_shadow_camera_t {
    wgf_vec3_t position, forward, up;
    bool orthographic;
    float fov, ortho_height, near_z, aspect;
} wgf_gfx_priv_shadow_camera_t;

/* A directional light's view of what the camera sees out to `distance`: an orthographic
 * square around that slice, snapped to whole texels so the shadow doesn't crawl as the
 * camera moves, its near plane pulled back by `pullback` so casters behind the camera
 * still cast. Pure. */
wgf_gfx_priv_shadow_fit_t wgf_gfx_priv_shadow_fit_directional(const wgf_gfx_priv_shadow_camera_t *camera,
                                                              wgf_vec3_t light_direction, float distance, int map_size,
                                                              float pullback);

/* A spot light's view of its own cone: a perspective frustum from `position` along
 * `direction`, wide enough for the outer cone, reaching `distance`. Pure. */
wgf_gfx_priv_shadow_fit_t wgf_gfx_priv_shadow_fit_spot(wgf_vec3_t position, wgf_vec3_t direction, float cos_outer,
                                                       float distance, float near_plane, int map_size);

/* How far a casting light's shadows reach: its distance, or a spot's range when nearer. */
static inline float wgf_gfx_priv_shadow_reach(const wgf_gfx_priv_stage3d_light_t *light)
{
    return light->type == WGF_LIGHT_TYPE_SPOT && light->range > 0.0f && light->range < light->shadow_distance
               ? light->range
               : light->shadow_distance;
}

/* One casting light's layer, and how the model shader reads it. */
typedef struct wgf_gfx_priv_shadow_slot_t {
    wgf_mat4_t view_proj;            /* world -> that light's clip space */
    float texel;                     /* 1 / map size, for the sampling kernel */
    float bias_constant, bias_slope; /* in shadow texels */
    float bias_scale;                /* texels -> the map's depth units */
    float texel_world;               /* world units one texel covers */
    float strength;
    float tint[3]; /* linear */
    bool perspective; /* the fit's */
    float near_z, far_z;
} wgf_gfx_priv_shadow_slot_t;

/* What a stage draw needs to shade with the frame's shadows: valid once the pass drew its
 * casting lights' maps (a light's slot is its stage3d light's `shadow_slot`). */
typedef struct wgf_gfx_priv_shadow_binding_t {
    bool valid;
    sg_view map;
    sg_sampler sampler;
    int count;
    wgf_gfx_priv_shadow_slot_t slots[WGF_GFX_PRIV_MAX_SHADOW_LIGHTS];
    bool flipped; /* the map's first row is its top */
} wgf_gfx_priv_shadow_binding_t;

/* The stage draw the pass draws for, from the stage (wgf_gfx_stage3d.c): the frame's first
 * with a casting light, something that casts, and something that receives. Its camera, its
 * casting lights a slot each, its items (into the frame's), and where the pass puts its
 * maps. False when there is none. */
typedef struct wgf_gfx_priv_shadow_frame_t {
    wgf_gfx_priv_shadow_camera_t camera;
    const wgf_gfx_priv_stage3d_light_t *lights[WGF_GFX_PRIV_MAX_SHADOW_LIGHTS];
    int light_count;
    int first, count;
    wgf_gfx_priv_shadow_binding_t *binding;
} wgf_gfx_priv_shadow_frame_t;
bool wgf_gfx_priv_stage3d_shadow_frame(wgf_gfx_priv_shadow_frame_t *out);

/* Item `item` of the frame's as a caster: an opaque part of a model that casts, placed by
 * `world`, its box in the stage, its primitive, and its material. False for any other. */
bool wgf_gfx_priv_stage3d_caster(int item, wgf_mat4_t *world, wgf_vec3_t *lo, wgf_vec3_t *hi,
                                 wgf_gfx_priv_mesh_primitive_t *primitive, wgf_material_t *material);

/* The part installed: the first light set to cast. */
void wgf_gfx_priv_shadow_install(void);

/* For tests and the shadow benchmark: the casters the last frame's depth pass drew (every
 * layer's), and how many layers it drew. */
int wgf_gfx_priv_shadow_get_draw_calls(void);
int wgf_gfx_priv_shadow_get_layers(void);

#endif
