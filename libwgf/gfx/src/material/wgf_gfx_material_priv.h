#ifndef WGF_GFX_MATERIAL_PRIV_H
#define WGF_GFX_MATERIAL_PRIV_H

#include <stdbool.h>

#include "wgf_core_resource_priv.h"
#include "wgf_material.h"
#include "wgf_texture.h"

/* Materials, for the stage's drawing: libwgt's material record. See wgf_material.h. */

/* What a parameter is, as its setter takes it. COLOR is a custom material's value set by
 * wgf_material_set_color, linear rgba, for its shader's vec3 or vec4. */
typedef enum wgf_gfx_priv_material_kind_t {
    WGF_GFX_PRIV_MATERIAL_INT,
    WGF_GFX_PRIV_MATERIAL_FLOAT,
    WGF_GFX_PRIV_MATERIAL_VEC2,
    WGF_GFX_PRIV_MATERIAL_VEC3,
    WGF_GFX_PRIV_MATERIAL_VEC4,
    WGF_GFX_PRIV_MATERIAL_TEXTURE,
    WGF_GFX_PRIV_MATERIAL_COLOR
} wgf_gfx_priv_material_kind_t;

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

struct wgf_gfx_priv_material_t;
struct wgf_gfx_priv_material_custom_t; /* a custom material's values (material/wgf_gfx_material_custom.c) */
struct wgf_gfx_priv_stage3d_frame_t;
struct wgf_gfx_priv_stage3d_draw_t;
struct wgf_gfx_priv_shadow_binding_t;
struct wgf_gfx_priv_env_lighting_t;

/* A custom material's calls, the custom part's (material/wgf_gfx_material_custom.c), set
 * by wgf_material_create_custom on the material it makes: the setters and the stage reach
 * them through the material, so a program without a custom material links none. */
typedef struct wgf_gfx_priv_material_custom_hooks_t {
    /* The 4 floats value `name` of `kind` is kept in (an int's bits in the first), made
     * when `make` (a setter's: refused, logged, when the shader is READY and has no
     * parameter of that name and type); NULL when there is none. A getter's (not `make`)
     * of VEC3 or VEC4 finds a COLOR too. */
    float *(*value)(struct wgf_gfx_priv_material_t *material, const char *name, wgf_gfx_priv_material_kind_t kind,
                    bool make);
    /* Texture `name`, made when `make` (refused, logged, as a value is); NULL otherwise. */
    struct wgf_gfx_priv_material_texture_t *(*texture)(struct wgf_gfx_priv_material_t *material, const char *name,
                                                        bool make);
    /* What it holds let go of: its values, textures, and shader. */
    void (*clear)(struct wgf_gfx_priv_material_t *material);
    /* A stage's run of its items (stage/wgf_gfx_stage3d_draw_priv.h's custom draw). */
    void (*draw)(struct wgf_gfx_priv_stage3d_frame_t *frame, const struct wgf_gfx_priv_stage3d_draw_t *draw,
                 int first, int count, const struct wgf_gfx_priv_shadow_binding_t *shadow,
                 const struct wgf_gfx_priv_env_lighting_t *env);
} wgf_gfx_priv_material_custom_hooks_t;

typedef struct wgf_gfx_priv_material_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (references; READY, no path) */
    const wgf_gfx_priv_material_custom_hooks_t *custom; /* a custom material's; NULL for a built-in shading */
    struct wgf_gfx_priv_material_custom_t *values;      /* a custom material's */
    wgf_handle_t shader;                                /* a custom material's, a reference held */
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

/* `material`, a new one, made a custom material's (its shading CUSTOM), to be filled in by
 * the custom part (wgf_material_create_custom); NULL for a handle that isn't one. */
wgf_gfx_priv_material_t *wgf_gfx_priv_material_make_custom(wgf_material_t material);

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
