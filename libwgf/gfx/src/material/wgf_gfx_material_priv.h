#ifndef WGF_GFX_MATERIAL_PRIV_H
#define WGF_GFX_MATERIAL_PRIV_H

#include <stdbool.h>

#include "wgf_core_resource_priv.h"
#include "wgf_material.h"
#include "wgf_texture.h"

/* Materials, for the stage's drawing: libwgt's material record, without its custom
 * shaders (milestone 2, step 12). See wgf_material.h. */

typedef enum wgf_gfx_priv_material_texture_slot_t {
    WGF_GFX_PRIV_MATERIAL_TEXTURE_BASE_COLOR = 0,
    WGF_GFX_PRIV_MATERIAL_TEXTURE_METALLIC_ROUGHNESS,
    WGF_GFX_PRIV_MATERIAL_TEXTURE_NORMAL,
    WGF_GFX_PRIV_MATERIAL_TEXTURE_OCCLUSION,
    WGF_GFX_PRIV_MATERIAL_TEXTURE_EMISSIVE,
    WGF_GFX_PRIV_MATERIAL_TEXTURE_COUNT
} wgf_gfx_priv_material_texture_slot_t;

/* A material texture and how it's sampled. */
typedef struct wgf_gfx_priv_material_texture_t {
    wgf_texture_t texture; /* referenced; 0 = none */
    int texcoord;          /* texture coordinate set, 0 or 1 */
    float offset[2];       /* texture transform (KHR_texture_transform) */
    float rotation;
    float scale[2];
    wgf_texture_wrap_t wrap_u;
    wgf_texture_wrap_t wrap_v;
    wgf_texture_filter_t filter;
    bool mipmaps; /* sampled through its mipmaps (the default), or its base level alone (a glTF sampler's) */
} wgf_gfx_priv_material_texture_t;

typedef struct wgf_gfx_priv_material_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (references; READY, no path) */
    wgf_material_shading_t shading;
    wgf_alpha_mode_t alpha_mode;
    float alpha_cutoff;
    bool double_sided;
    float base_color[4]; /* linear rgba */
    float emissive[3];   /* linear rgb */
    float metallic;
    float roughness;
    float normal_scale;
    float occlusion_strength;
    wgf_gfx_priv_material_texture_t textures[WGF_GFX_PRIV_MATERIAL_TEXTURE_COUNT];
} wgf_gfx_priv_material_t;

/* The material, without logging; NULL for 0 or a handle that isn't one. */
const wgf_gfx_priv_material_t *wgf_gfx_priv_material_get(wgf_material_t material);

/* The texture transform as a 2x3 matrix, rows (m[0] m[1] m[2]) and (m[3] m[4] m[5]):
 * u' = m[0] u + m[1] v + m[2], v' = m[3] u + m[4] v + m[5]. As glTF's
 * KHR_texture_transform: translation * rotation * scale. */
void wgf_gfx_priv_material_uv_matrix(const wgf_gfx_priv_material_texture_t *texture, float m[6]);

/* Whether material texture `name` is sampled through its mipmaps; false for a name that
 * isn't one of its textures. */
bool wgf_gfx_priv_material_set_texture_mipmaps(wgf_material_t material, const char *name, bool mipmaps);

/* An sRGB channel (0..1) as linear light. */
float wgf_gfx_priv_srgb_to_linear(float c);

#endif
