#ifndef WGF_MATERIAL_H
#define WGF_MATERIAL_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_handle.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_vec2.h"
#include "wgf_vec3.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A material: how a surface is shaded, libwgt's (wgrender's). It is a resource
 * (wgf_resource.h): shared and reference counted, made from numbers, never a file.
 *
 * - A mesh comes with its materials, one a slot (wgf_mesh_get_material), shared by
 *   every model of that mesh, so changing one changes all of them. To change one model
 *   only, create a material and give it to the model (wgf_model_set_material).
 * - wgf_material_create returns a material with glTF's defaults (white, fully
 *   metallic, fully rough, opaque, single sided), with a reference of the caller's.
 *   Models hold their own reference, so release yours (wgf_resource_release) when done.
 * - Shading follows glTF metallic-roughness, lit in linear color space. Colors given as
 *   wgf_color_t, and color textures (base color, emissive), are sRGB and converted to
 *   linear; numeric color values (vec3, vec4) are already linear, as glTF's factors
 *   are. Metallic-roughness, normal, and occlusion textures are linear data.
 *
 * Parameters are set by name. A setter returns false for an unknown name or a value of
 * the wrong kind (logged); a getter returns 0 for either.
 *
 *   name                          kind     default    notes
 *   base_color                    vec4     1,1,1,1    linear rgba; alpha drives MASK and BLEND
 *   base_color_texture            texture  none       sRGB rgba
 *   metallic                      float    1          0..1
 *   roughness                     float    1          0..1
 *   metallic_roughness_texture    texture  none       green = roughness, blue = metallic
 *   normal_texture                texture  none       tangent-space normal map
 *   normal_scale                  float    1
 *   occlusion_texture             texture  none       red = ambient occlusion
 *   occlusion_strength            float    1          0..1
 *   emissive                      vec3     0,0,0      linear rgb, may exceed 1
 *   emissive_texture              texture  none       sRGB rgb
 *
 * Each texture <t> above (base_color_texture, ...) also has:
 *
 *   <t>_texcoord                  int      0          texture coordinate set: 0 or 1
 *   <t>_offset                    vec2     0,0        texture transform (glTF
 *   <t>_rotation                  float    0          KHR_texture_transform):
 *   <t>_scale                     vec2     1,1        uv' = offset + rotate(scale * uv)
 *
 * Scale tiles the texture (2,2 repeats it twice each way); rotation is radians,
 * counterclockwise in texture space. Textures repeat, sample smoothly, and use their
 * mipmaps unless wgf_material_set_texture_sampling says otherwise. A texture still
 * loading draws as nothing set would, but a base color texture, which draws the
 * placeholder checker once it has FAILED, as sprites do. */
typedef wgf_handle_t wgf_material_t;

typedef enum wgf_material_shading_t {
    WGF_MATERIAL_SHADING_PBR = 0,  /* glTF metallic-roughness, lit by the stage's lights */
    WGF_MATERIAL_SHADING_UNLIT = 1 /* base color x texture x tint; ignores lights (KHR_materials_unlit) */
} wgf_material_shading_t;

/* How something drawn uses its alpha. In a stage, OPAQUE and MASK parts write depth and
 * aren't sorted; BLEND parts are drawn after them, sorted back to front. */
typedef enum wgf_alpha_mode_t {
    WGF_ALPHA_MODE_OPAQUE = 0, /* alpha ignored */
    WGF_ALPHA_MODE_MASK = 1,   /* drawn where alpha reaches the cutoff, not elsewhere */
    WGF_ALPHA_MODE_BLEND = 2   /* alpha blended, back to front */
} wgf_alpha_mode_t;

/* A material with glTF's defaults. 0 when `shading` isn't one (logged), or there is no
 * room for another material. */
WGF_API wgf_material_t wgf_material_create(wgf_material_shading_t shading);

/* False when `shading` isn't one. */
WGF_API bool wgf_material_set_shading(wgf_material_t material, wgf_material_shading_t shading);
WGF_API wgf_material_shading_t wgf_material_get_shading(wgf_material_t material);

/* How alpha is used, and the cutoff MASK draws from (default OPAQUE, 0.5; a cutoff below
 * 0 is clamped to 0). False when `mode` isn't one. */
WGF_API bool wgf_material_set_alpha_mode(wgf_material_t material, wgf_alpha_mode_t mode, float cutoff);
WGF_API wgf_alpha_mode_t wgf_material_get_alpha_mode(wgf_material_t material);
WGF_API float wgf_material_get_alpha_cutoff(wgf_material_t material);

/* A double-sided surface isn't culled from behind; its back faces are lit from their
 * side (default single sided). */
WGF_API bool wgf_material_set_double_sided(wgf_material_t material, bool double_sided);
WGF_API bool wgf_material_is_double_sided(wgf_material_t material);

/* The parameters, by name (the table above). set_int refuses a texture coordinate set
 * other than 0 or 1. set_color takes an sRGB color for a vec3 or vec4 parameter,
 * stored linear, its alpha as it is (read back with get_vec3 or get_vec4). */
WGF_API bool wgf_material_set_int(wgf_material_t material, const char *name, int value);
WGF_API int wgf_material_get_int(wgf_material_t material, const char *name);
WGF_API bool wgf_material_set_float(wgf_material_t material, const char *name, float value);
WGF_API float wgf_material_get_float(wgf_material_t material, const char *name);
WGF_API bool wgf_material_set_vec2(wgf_material_t material, const char *name, float x, float y);
WGF_API wgf_vec2_t wgf_material_get_vec2(wgf_material_t material, const char *name);
WGF_API bool wgf_material_set_vec3(wgf_material_t material, const char *name, float x, float y, float z);
WGF_API wgf_vec3_t wgf_material_get_vec3(wgf_material_t material, const char *name);
WGF_API bool wgf_material_set_vec4(wgf_material_t material, const char *name, float x, float y, float z, float w);
WGF_API wgf_vec4_t wgf_material_get_vec4(wgf_material_t material, const char *name);
WGF_API bool wgf_material_set_color(wgf_material_t material, const char *name, wgf_color_t color);

/* A texture parameter: the material holds its own reference to the texture; 0 clears
 * it. False when `texture` isn't a texture. */
WGF_API bool wgf_material_set_texture(wgf_material_t material, const char *name, wgf_texture_t texture);
WGF_API wgf_texture_t wgf_material_get_texture(wgf_material_t material, const char *name);

/* How texture parameter `name` is sampled (default repeat, linear). False for a wrap or
 * filter that isn't one. */
WGF_API bool wgf_material_set_texture_sampling(wgf_material_t material, const char *name, wgf_texture_wrap_t wrap_u,
                                               wgf_texture_wrap_t wrap_v, wgf_texture_filter_t filter);
WGF_API wgf_texture_wrap_t wgf_material_get_texture_wrap_u(wgf_material_t material, const char *name);
WGF_API wgf_texture_wrap_t wgf_material_get_texture_wrap_v(wgf_material_t material, const char *name);
WGF_API wgf_texture_filter_t wgf_material_get_texture_filter(wgf_material_t material, const char *name);

#ifdef __cplusplus
}
#endif

#endif
