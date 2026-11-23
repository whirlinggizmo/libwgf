#ifndef WGF_MATERIAL_H
#define WGF_MATERIAL_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_cubemap.h"
#include "wgf_handle.h"
#include "wgf_resource.h"
#include "wgf_shader.h"
#include "wgf_texture.h"
#include "wgf_vec2.h"
#include "wgf_vec3.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A material: how a surface is shaded.
 *
 * A material is a resource (wgf_resource.h). It is shared and reference counted, and it
 * is always made in code, never loaded from a file.
 *
 * - A mesh comes with its own materials, one for each slot (wgf_mesh_get_material). Every
 *   model of that mesh shares them, so changing one changes all of those models. To
 *   change a single model, create a material and give it to that model
 *   (wgf_model_set_material).
 * - wgf_material_create returns a material with glTF's defaults: white, fully metallic,
 *   fully rough, opaque, and single sided. You hold one reference to it. Models take their
 *   own, so release yours when you are done with it (see wgf_resource_release).
 * - Shading follows glTF's metallic-roughness model, lit in linear color space.
 * - Colors given as wgf_color_t are sRGB, and so are color textures (base color and
 *   emissive). Both are converted to linear.
 * - Numeric colors (vec3 and vec4 values) are already linear, as glTF's factors are.
 * - Metallic-roughness, normal, and occlusion textures hold linear data.
 *
 * Parameters are set by name. A setter returns false for an unknown name, or for a value
 * of the wrong kind (logged). A getter returns 0 in either case.
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
 * Each texture <t> above (base_color_texture, and so on) also has these:
 *
 *   <t>_texcoord                  int      0          texture coordinate set: 0 or 1
 *   <t>_offset                    vec2     0,0        texture transform (glTF
 *   <t>_rotation                  float    0          KHR_texture_transform):
 *   <t>_scale                     vec2     1,1        uv' = offset + rotate(scale * uv)
 *
 * Scale tiles the texture: 2,2 repeats it twice in each direction. Rotation is in radians,
 * counterclockwise in texture space.
 *
 * Textures repeat, sample smoothly, and use their mipmaps, unless
 * wgf_material_set_texture_sampling says otherwise. A texture that is still loading draws
 * as if it weren't set. The one exception is a base color texture that has FAILED: it
 * draws the placeholder checker, as a sprite does.
 *
 * A custom material (wgf_material_create_custom) is drawn by a shader of your own
 * (wgf_shader.h).
 *
 * - Its parameters and textures are the ones that shader declares. The names above don't
 *   apply.
 * - They are set with the same calls, by the shader's names: float, int, vec2, vec3, and
 *   vec4. set_color sets a vec3 or vec4, and set_int takes any int.
 * - Values set while the shader is still loading are kept, and checked once it is READY.
 *   A name the shader doesn't have, or a value of another type, is then dropped with a
 *   warning.
 * - Its alpha mode, cutoff, and double-sided setting work as they do for any material.
 * - It draws nothing until its shader is READY, and nothing at all if the shader FAILED.
 *   That failure is warned about once. */
typedef wgf_handle_t wgf_material_t;

typedef enum wgf_material_shading_t {
    WGF_MATERIAL_SHADING_PBR = 0,   /* glTF metallic-roughness, lit by the stage's lights */
    WGF_MATERIAL_SHADING_UNLIT = 1, /* base color times texture times tint, ignoring lights (KHR_materials_unlit) */
    WGF_MATERIAL_SHADING_CUSTOM = 2 /* drawn by your own shader (wgf_material_create_custom); create and set_shading refuse it */
} wgf_material_shading_t;

/* How a material, sprite, particle, or 2D drawing uses its alpha.
 *
 * On a stage, the modes draw in this order:
 * - OPAQUE and MASK write depth and are drawn first, unsorted.
 * - BLEND is drawn next, sorted back to front.
 * - ADD is drawn last. It is for particles only: a material refuses it.
 *
 * MULTIPLY and PREMULTIPLIED are for 2D drawing only: immediate mode
 * (wgf_draw_set_blend_mode), and sprites on a 2D stage. A sprite on a 3D stage draws them
 * as BLEND. Materials, emitters, and sprite batches refuse them. */
typedef enum wgf_alpha_mode_t {
    WGF_ALPHA_MODE_OPAQUE = 0,       /* alpha ignored */
    WGF_ALPHA_MODE_MASK = 1,         /* drawn where alpha reaches the cutoff, not elsewhere */
    WGF_ALPHA_MODE_BLEND = 2,        /* alpha blended, back to front */
    WGF_ALPHA_MODE_ADD = 3,          /* added, alpha its weight: glows, sparks, fire */
    WGF_ALPHA_MODE_MULTIPLY = 4,     /* what is under it multiplied by its color: shadows, tints, darkening */
    WGF_ALPHA_MODE_PREMULTIPLIED = 5 /* its color already multiplied by its alpha (a premultiplied image's) */
} wgf_alpha_mode_t;

/* Creates a material with glTF's defaults.
 *
 * Returns 0 if `shading` isn't PBR or UNLIT (logged), or if there is no room for another
 * material. */
WGF_API wgf_material_t wgf_material_create(wgf_material_shading_t shading);

/* Creates a material drawn by `shader` (wgf_shader_create), which may still be loading.
 * The material holds its own reference to the shader.
 *
 * - With a surface shader, the material draws a stage's models.
 * - With a screen effect's shader, the material is added to the frame's effects
 *   (wgf_render_add_effect).
 *
 * Returns 0 if `shader` isn't a shader (logged), or if there is no room for another
 * material. */
WGF_API wgf_material_t wgf_material_create_custom(wgf_shader_t shader);

/* Returns a custom material's shader, or 0 for a material with built-in shading. */
WGF_API wgf_shader_t wgf_material_get_shader(wgf_material_t material);

/* Sets or gets the material's shading. The setter returns false if `shading` isn't PBR or
 * UNLIT, or if the material is a custom one. */
WGF_API bool wgf_material_set_shading(wgf_material_t material, wgf_material_shading_t shading);
WGF_API wgf_material_shading_t wgf_material_get_shading(wgf_material_t material);

/* Sets how the material uses alpha (default: OPAQUE), and the cutoff for MASK (default: 0.5).
 * MASK draws only where alpha reaches the cutoff. A cutoff below 0 is clamped to 0.
 * Returns false if `mode` isn't an alpha mode. */
WGF_API bool wgf_material_set_alpha_mode(wgf_material_t material, wgf_alpha_mode_t mode, float cutoff);
WGF_API wgf_alpha_mode_t wgf_material_get_alpha_mode(wgf_material_t material);
WGF_API float wgf_material_get_alpha_cutoff(wgf_material_t material);

/* Sets whether both sides of a surface are drawn (default: single sided). The back faces
 * of a double-sided surface are lit from their own side. */
WGF_API bool wgf_material_set_double_sided(wgf_material_t material, bool double_sided);
WGF_API bool wgf_material_is_double_sided(wgf_material_t material);

/* Sets or gets a parameter by name, from the table above.
 *
 * - set_int refuses a texture coordinate set other than 0 or 1.
 * - set_color takes an sRGB color for a vec3 or vec4 parameter and stores it as linear.
 *   Its alpha is stored unchanged. Read it back with get_vec3 or get_vec4. */
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

/* Sets or gets a texture parameter by name. The material holds its own reference to the
 * texture, and 0 clears it. Returns false if `texture` isn't a texture. */
WGF_API bool wgf_material_set_texture(wgf_material_t material, const char *name, wgf_texture_t texture);
WGF_API wgf_texture_t wgf_material_get_texture(wgf_material_t material, const char *name);

/* Sets or gets a custom material's cubemap (wgf_cubemap.h): its shader's textureCube
 * `name`, sampled as a samplerCube (wgf.glsl). The material holds its own reference to the
 * cubemap, and 0 clears it.
 *
 * Returns false for a material with built-in shading, a handle that isn't a cubemap, or a
 * name that is one of the shader's texture2Ds (set those with wgf_material_set_texture).
 * If the shader is still loading, the cubemap is kept and checked once the shader is
 * READY. A wrong name is then dropped with a warning.
 *
 * set_texture refuses a textureCube's name in the same way, and each getter returns 0 for
 * the other kind. */
WGF_API bool wgf_material_set_cubemap(wgf_material_t material, const char *name, wgf_cubemap_t cubemap);
WGF_API wgf_cubemap_t wgf_material_get_cubemap(wgf_material_t material, const char *name);

/* Sets or gets how texture parameter `name` is sampled (default: repeat, linear). A
 * cubemap uses only the filter, and is always clamped at its edges. Returns false for a
 * wrap or filter that isn't one. */
WGF_API bool wgf_material_set_texture_sampling(wgf_material_t material, const char *name, wgf_texture_wrap_t wrap_u,
                                               wgf_texture_wrap_t wrap_v, wgf_texture_filter_t filter);
WGF_API wgf_texture_wrap_t wgf_material_get_texture_wrap_u(wgf_material_t material, const char *name);
WGF_API wgf_texture_wrap_t wgf_material_get_texture_wrap_v(wgf_material_t material, const char *name);
WGF_API wgf_texture_filter_t wgf_material_get_texture_filter(wgf_material_t material, const char *name);

#ifdef __cplusplus
}
#endif

#endif
