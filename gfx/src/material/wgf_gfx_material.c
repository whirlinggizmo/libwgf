#include "wgf_material.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "material/wgf_gfx_material_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_log.h"

/* Materials, libwgt's (wgrender's wgr_material) without its custom shaders: a resource
 * made from numbers, its parameters set by name from a table. A part, installed by the
 * first material made, so a program that makes none links none of it; its stop at
 * gfx's frees every material, after the meshes holding theirs. */

#define MATERIALS_INITIAL 64 /* slots to start with; the pool doubles as needed */

static bool pool_ready;
static wgf_core_priv_handle_pool_t pool;
static wgf_gfx_priv_material_t *materials; /* grown by the pool: don't hold a pointer across a create */

/* ------------------------------------------------------------ parameters ---- */

typedef enum param_kind_t {
    PARAM_INT,
    PARAM_FLOAT,
    PARAM_VEC2,
    PARAM_VEC3,
    PARAM_VEC4,
    PARAM_TEXTURE
} param_kind_t;

typedef struct param_t {
    const char *name;
    param_kind_t kind;
    size_t offset; /* into wgf_gfx_priv_material_t, or the texture slot for PARAM_TEXTURE */
} param_t;

#define TEXTURE_PARAMS(prefix, slot)                                                                    \
    {prefix, PARAM_TEXTURE, slot},                                                                      \
        {prefix "_texcoord", PARAM_INT, offsetof(wgf_gfx_priv_material_t, textures[slot].texcoord)},    \
        {prefix "_offset", PARAM_VEC2, offsetof(wgf_gfx_priv_material_t, textures[slot].offset)},       \
        {prefix "_rotation", PARAM_FLOAT, offsetof(wgf_gfx_priv_material_t, textures[slot].rotation)},  \
        {prefix "_scale", PARAM_VEC2, offsetof(wgf_gfx_priv_material_t, textures[slot].scale)}

static const param_t PARAMS[] = {
    {"base_color", PARAM_VEC4, offsetof(wgf_gfx_priv_material_t, base_color)},
    TEXTURE_PARAMS("base_color_texture", WGF_GFX_PRIV_MATERIAL_TEXTURE_BASE_COLOR),
    {"metallic", PARAM_FLOAT, offsetof(wgf_gfx_priv_material_t, metallic)},
    {"roughness", PARAM_FLOAT, offsetof(wgf_gfx_priv_material_t, roughness)},
    TEXTURE_PARAMS("metallic_roughness_texture", WGF_GFX_PRIV_MATERIAL_TEXTURE_METALLIC_ROUGHNESS),
    TEXTURE_PARAMS("normal_texture", WGF_GFX_PRIV_MATERIAL_TEXTURE_NORMAL),
    {"normal_scale", PARAM_FLOAT, offsetof(wgf_gfx_priv_material_t, normal_scale)},
    TEXTURE_PARAMS("occlusion_texture", WGF_GFX_PRIV_MATERIAL_TEXTURE_OCCLUSION),
    {"occlusion_strength", PARAM_FLOAT, offsetof(wgf_gfx_priv_material_t, occlusion_strength)},
    {"emissive", PARAM_VEC3, offsetof(wgf_gfx_priv_material_t, emissive)},
    TEXTURE_PARAMS("emissive_texture", WGF_GFX_PRIV_MATERIAL_TEXTURE_EMISSIVE),
};

static wgf_gfx_priv_material_t *resolve(wgf_material_t handle)
{
    uint16_t index = 0;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve(&pool, handle, &index)) return NULL;
    return &materials[index];
}

/* The material and its parameter `name`; NULL when either doesn't exist, or when the
 * parameter's kind isn't `kind` (or `alt_kind`), logged when `log` is set. */
static const param_t *lookup(wgf_material_t material, const char *name, param_kind_t kind, param_kind_t alt_kind,
                             wgf_gfx_priv_material_t **material_out, bool log)
{
    wgf_gfx_priv_material_t *material_ptr = resolve(material);
    const param_t *param = NULL;
    size_t i;
    if (material_ptr == NULL) return NULL;
    for (i = 0; param == NULL && name != NULL && i < sizeof(PARAMS) / sizeof(PARAMS[0]); i++) {
        if (strcmp(PARAMS[i].name, name) == 0) param = &PARAMS[i];
    }
    if (param == NULL) {
        if (log) wgf_log_warn("wgf_gfx_material: no parameter '%s'", name != NULL ? name : "(null)");
        return NULL;
    }
    if (param->kind != kind && param->kind != alt_kind) {
        if (log) wgf_log_warn("wgf_gfx_material: parameter '%s' is of another kind", name);
        return NULL;
    }
    *material_out = material_ptr;
    return param;
}

static float *lookup_values(wgf_material_t material, const char *name, param_kind_t kind, bool log)
{
    wgf_gfx_priv_material_t *material_ptr = NULL;
    const param_t *param_ptr = lookup(material, name, kind, kind, &material_ptr, log);
    return param_ptr != NULL ? (float *)((char *)material_ptr + param_ptr->offset) : NULL;
}

static wgf_gfx_priv_material_texture_t *lookup_texture(wgf_material_t material, const char *name, bool log)
{
    wgf_gfx_priv_material_t *material_ptr = NULL;
    const param_t *param_ptr = lookup(material, name, PARAM_TEXTURE, PARAM_TEXTURE, &material_ptr, log);
    return param_ptr != NULL ? &material_ptr->textures[param_ptr->offset] : NULL;
}

/* What a material holds past the header: its textures. */
static void clear(wgf_handle_t material, void *record)
{
    wgf_gfx_priv_material_t *material_ptr = (wgf_gfx_priv_material_t *)record;
    int i;
    (void)material;
    for (i = 0; i < WGF_GFX_PRIV_MATERIAL_TEXTURE_COUNT; i++) {
        if (material_ptr->textures[i].texture != 0) wgf_resource_release(material_ptr->textures[i].texture);
    }
}

static const wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_material_create", .free = clear};

static void stop(void)
{
    if (!pool_ready) return;
    wgf_core_priv_resource_unregister(&pool);
    wgf_core_priv_handle_pool_destroy(&pool);
    materials = NULL;
    pool_ready = false;
}

static wgf_core_priv_part_t part = {.name = "materials",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_MATERIALS,
                                    .stop = stop};

/* -------------------------------------------------------------- internal ---- */

float wgf_gfx_priv_srgb_to_linear(float c)
{
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

void wgf_gfx_priv_material_uv_matrix(const wgf_gfx_priv_material_texture_t *texture, float m[6])
{
    const float c = cosf(texture->rotation), s = sinf(texture->rotation);
    m[0] = c * texture->scale[0];
    m[1] = s * texture->scale[1];
    m[2] = texture->offset[0];
    m[3] = -s * texture->scale[0];
    m[4] = c * texture->scale[1];
    m[5] = texture->offset[1];
}

const wgf_gfx_priv_material_t *wgf_gfx_priv_material_get(wgf_material_t material)
{
    return resolve(material);
}

/* ------------------------------------------------------------ public API ---- */

wgf_material_t wgf_material_create(wgf_material_shading_t shading)
{
    wgf_material_t handle;
    wgf_gfx_priv_material_t *material_ptr;
    int i;

    if (shading != WGF_MATERIAL_SHADING_PBR && shading != WGF_MATERIAL_SHADING_UNLIT) {
        wgf_log_error("wgf_material_create: no shading %d", (int)shading);
        return 0;
    }
    if (!pool_ready) {
        pool_ready = wgf_core_priv_handle_pool_init(&pool, WGF_CORE_PRIV_HANDLE_KIND_MATERIAL, (void **)&materials,
                                                    sizeof(wgf_gfx_priv_material_t), MATERIALS_INITIAL, 65535);
        if (pool_ready) wgf_core_priv_resource_register(&pool, &resource_kind);
    }
    wgf_core_priv_part_install(&part);
    handle = pool_ready ? wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_MATERIAL) : 0;
    material_ptr = resolve(handle);
    if (material_ptr == NULL) return 0;
    material_ptr->shading = shading;
    material_ptr->alpha_mode = WGF_ALPHA_MODE_OPAQUE;
    material_ptr->alpha_cutoff = 0.5f;
    material_ptr->base_color[0] = material_ptr->base_color[1] = material_ptr->base_color[2] =
        material_ptr->base_color[3] = 1.0f;
    material_ptr->metallic = 1.0f;
    material_ptr->roughness = 1.0f;
    material_ptr->normal_scale = 1.0f;
    material_ptr->occlusion_strength = 1.0f;
    for (i = 0; i < WGF_GFX_PRIV_MATERIAL_TEXTURE_COUNT; i++) {
        material_ptr->textures[i].scale[0] = material_ptr->textures[i].scale[1] = 1.0f;
    }
    return handle;
}

bool wgf_material_set_shading(wgf_material_t material, wgf_material_shading_t shading)
{
    wgf_gfx_priv_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL || (shading != WGF_MATERIAL_SHADING_PBR && shading != WGF_MATERIAL_SHADING_UNLIT)) {
        return false;
    }
    material_ptr->shading = shading;
    return true;
}

wgf_material_shading_t wgf_material_get_shading(wgf_material_t material)
{
    const wgf_gfx_priv_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->shading : WGF_MATERIAL_SHADING_PBR;
}

bool wgf_material_set_alpha_mode(wgf_material_t material, wgf_alpha_mode_t mode, float cutoff)
{
    wgf_gfx_priv_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL || (int)mode < WGF_ALPHA_MODE_OPAQUE || mode > WGF_ALPHA_MODE_BLEND) return false;
    material_ptr->alpha_mode = mode;
    material_ptr->alpha_cutoff = cutoff < 0.0f ? 0.0f : cutoff;
    return true;
}

wgf_alpha_mode_t wgf_material_get_alpha_mode(wgf_material_t material)
{
    const wgf_gfx_priv_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->alpha_mode : WGF_ALPHA_MODE_OPAQUE;
}

float wgf_material_get_alpha_cutoff(wgf_material_t material)
{
    const wgf_gfx_priv_material_t *material_ptr = resolve(material);
    return material_ptr != NULL ? material_ptr->alpha_cutoff : 0.0f;
}

bool wgf_material_set_double_sided(wgf_material_t material, bool double_sided)
{
    wgf_gfx_priv_material_t *material_ptr = resolve(material);
    if (material_ptr == NULL) return false;
    material_ptr->double_sided = double_sided;
    return true;
}

bool wgf_material_is_double_sided(wgf_material_t material)
{
    const wgf_gfx_priv_material_t *material_ptr = resolve(material);
    return material_ptr != NULL && material_ptr->double_sided;
}

bool wgf_material_set_int(wgf_material_t material, const char *name, int value)
{
    wgf_gfx_priv_material_t *material_ptr = NULL;
    const param_t *param_ptr = lookup(material, name, PARAM_INT, PARAM_INT, &material_ptr, true);
    if (param_ptr == NULL) return false;
    if (value < 0 || value > 1) { /* the only int parameters are texture coordinate sets */
        wgf_log_warn("wgf_gfx_material: '%s' is 0 or 1, not %d", name, value);
        return false;
    }
    *(int *)((char *)material_ptr + param_ptr->offset) = value;
    return true;
}

int wgf_material_get_int(wgf_material_t material, const char *name)
{
    wgf_gfx_priv_material_t *material_ptr = NULL;
    const param_t *param_ptr = lookup(material, name, PARAM_INT, PARAM_INT, &material_ptr, false);
    return param_ptr != NULL ? *(const int *)((const char *)material_ptr + param_ptr->offset) : 0;
}

bool wgf_material_set_float(wgf_material_t material, const char *name, float value)
{
    float *values = lookup_values(material, name, PARAM_FLOAT, true);
    if (values == NULL) return false;
    values[0] = value;
    return true;
}

float wgf_material_get_float(wgf_material_t material, const char *name)
{
    const float *values = lookup_values(material, name, PARAM_FLOAT, false);
    return values != NULL ? values[0] : 0.0f;
}

bool wgf_material_set_vec2(wgf_material_t material, const char *name, float x, float y)
{
    float *values = lookup_values(material, name, PARAM_VEC2, true);
    if (values == NULL) return false;
    values[0] = x;
    values[1] = y;
    return true;
}

wgf_vec2_t wgf_material_get_vec2(wgf_material_t material, const char *name)
{
    const float *values = lookup_values(material, name, PARAM_VEC2, false);
    return values != NULL ? wgf_vec2_make(values[0], values[1]) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_material_set_vec3(wgf_material_t material, const char *name, float x, float y, float z)
{
    float *values = lookup_values(material, name, PARAM_VEC3, true);
    if (values == NULL) return false;
    values[0] = x;
    values[1] = y;
    values[2] = z;
    return true;
}

wgf_vec3_t wgf_material_get_vec3(wgf_material_t material, const char *name)
{
    const float *values = lookup_values(material, name, PARAM_VEC3, false);
    return values != NULL ? wgf_vec3_make(values[0], values[1], values[2]) : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_material_set_vec4(wgf_material_t material, const char *name, float x, float y, float z, float w)
{
    float *values = lookup_values(material, name, PARAM_VEC4, true);
    if (values == NULL) return false;
    values[0] = x;
    values[1] = y;
    values[2] = z;
    values[3] = w;
    return true;
}

wgf_vec4_t wgf_material_get_vec4(wgf_material_t material, const char *name)
{
    const float *values = lookup_values(material, name, PARAM_VEC4, false);
    return values != NULL ? wgf_vec4_make(values[0], values[1], values[2], values[3])
                          : wgf_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
}

bool wgf_material_set_color(wgf_material_t material, const char *name, wgf_color_t color)
{
    wgf_gfx_priv_material_t *material_ptr = NULL;
    const param_t *param_ptr = lookup(material, name, PARAM_VEC3, PARAM_VEC4, &material_ptr, true);
    float *values;
    if (param_ptr == NULL) return false;
    values = (float *)((char *)material_ptr + param_ptr->offset);
    values[0] = wgf_gfx_priv_srgb_to_linear((float)wgf_color_get_red(color) / 255.0f);
    values[1] = wgf_gfx_priv_srgb_to_linear((float)wgf_color_get_green(color) / 255.0f);
    values[2] = wgf_gfx_priv_srgb_to_linear((float)wgf_color_get_blue(color) / 255.0f);
    if (param_ptr->kind == PARAM_VEC4) values[3] = (float)wgf_color_get_alpha(color) / 255.0f; /* alpha is linear */
    return true;
}

bool wgf_material_set_texture(wgf_material_t material, const char *name, wgf_texture_t texture)
{
    wgf_gfx_priv_material_texture_t *slot_ptr = lookup_texture(material, name, true);
    if (slot_ptr == NULL) return false;
    if (texture != 0 && (WGF_CORE_PRIV_HANDLE_KIND(texture) != WGF_CORE_PRIV_HANDLE_KIND_TEXTURE ||
                         wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_NONE)) {
        wgf_log_warn("wgf_gfx_material: '%s' takes a texture", name);
        return false;
    }
    if (slot_ptr->texture != texture) {
        if (texture != 0) wgf_gfx_priv_texture_retain(texture); /* before releasing, in case they're the same */
        if (slot_ptr->texture != 0) wgf_resource_release(slot_ptr->texture);
        slot_ptr->texture = texture;
    }
    return true;
}

wgf_texture_t wgf_material_get_texture(wgf_material_t material, const char *name)
{
    const wgf_gfx_priv_material_texture_t *slot_ptr = lookup_texture(material, name, false);
    return slot_ptr != NULL ? slot_ptr->texture : 0;
}

bool wgf_material_set_texture_sampling(wgf_material_t material, const char *name, wgf_texture_wrap_t wrap_u,
                                       wgf_texture_wrap_t wrap_v, wgf_texture_filter_t filter)
{
    wgf_gfx_priv_material_texture_t *texture_ptr = lookup_texture(material, name, true);
    if (texture_ptr == NULL) return false;
    if ((int)wrap_u < WGF_TEXTURE_WRAP_REPEAT || wrap_u > WGF_TEXTURE_WRAP_MIRROR ||
        (int)wrap_v < WGF_TEXTURE_WRAP_REPEAT || wrap_v > WGF_TEXTURE_WRAP_MIRROR ||
        (int)filter < WGF_TEXTURE_FILTER_LINEAR || filter > WGF_TEXTURE_FILTER_NEAREST) {
        return false;
    }
    texture_ptr->wrap_u = wrap_u;
    texture_ptr->wrap_v = wrap_v;
    texture_ptr->filter = filter;
    return true;
}

wgf_texture_wrap_t wgf_material_get_texture_wrap_u(wgf_material_t material, const char *name)
{
    const wgf_gfx_priv_material_texture_t *texture_ptr = lookup_texture(material, name, false);
    return texture_ptr != NULL ? texture_ptr->wrap_u : WGF_TEXTURE_WRAP_REPEAT;
}

wgf_texture_wrap_t wgf_material_get_texture_wrap_v(wgf_material_t material, const char *name)
{
    const wgf_gfx_priv_material_texture_t *texture_ptr = lookup_texture(material, name, false);
    return texture_ptr != NULL ? texture_ptr->wrap_v : WGF_TEXTURE_WRAP_REPEAT;
}

wgf_texture_filter_t wgf_material_get_texture_filter(wgf_material_t material, const char *name)
{
    const wgf_gfx_priv_material_texture_t *texture_ptr = lookup_texture(material, name, false);
    return texture_ptr != NULL ? texture_ptr->filter : WGF_TEXTURE_FILTER_LINEAR;
}
