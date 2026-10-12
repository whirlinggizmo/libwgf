#include "wgf_material.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "material/wgf_gfx_material_priv.h"
#include "material/wgf_gfx_shader_priv.h"
#include "stage/wgf_gfx_stage3d_draw_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_log.h"

/* Custom materials, libwgt's (wgrender's): a material drawn by a shader of its own
 * (wgf_shader.h), its values and textures kept by the shader's names. Set before the
 * shader is READY, they are checked against it once it is (a name it doesn't have, or a
 * value of another type, is warned about and dropped); after, each as it is set. A part of
 * its own, reached through the hooks wgf_material_create_custom sets on the material it
 * makes (material.c's setters, and the stage's draw), so a program without one links none
 * of it. */

#define CUSTOM_VALUES WGF_GFX_PRIV_SHADER_MAX_PARAMS
#define CUSTOM_TEXTURES WGF_GFX_PRIV_SHADER_MAX_TEXTURES
#define BLOCK_MAX 1024 /* the shader's two parameter blocks, the fragment's then the vertex's */

typedef struct wgf_gfx_priv_material_custom_t {
    bool bound; /* checked against its READY shader */
    bool told;  /* its shader FAILED, or its blocks don't fit: warned once */
    int value_count;
    struct {
        char name[WGF_GFX_PRIV_SHADER_NAME_MAX];
        wgf_gfx_priv_material_kind_t kind;
        float values[4]; /* an int's bits in values[0] */
    } values[CUSTOM_VALUES];
    int texture_count;
    char texture_names[CUSTOM_TEXTURES][WGF_GFX_PRIV_SHADER_NAME_MAX];
    wgf_gfx_priv_material_texture_t textures[CUSTOM_TEXTURES];
    bool block_stale;
    /* bound: the shader's parameter blocks. Floats, so it is 4-byte aligned: the web's
       glUniform4fv reads the heap as floats from pointer / 4, misreading anything not */
    float block[BLOCK_MAX / 4];
} wgf_gfx_priv_material_custom_t;

/* Whether a value set as `kind` fits a parameter of `type`. */
static bool kind_fits(wgf_gfx_priv_material_kind_t kind, wgf_gfx_priv_shader_param_type_t type)
{
    switch (kind) {
        case WGF_GFX_PRIV_MATERIAL_INT: return type == WGF_GFX_PRIV_SHADER_PARAM_INT;
        case WGF_GFX_PRIV_MATERIAL_FLOAT: return type == WGF_GFX_PRIV_SHADER_PARAM_FLOAT;
        case WGF_GFX_PRIV_MATERIAL_VEC2: return type == WGF_GFX_PRIV_SHADER_PARAM_VEC2;
        case WGF_GFX_PRIV_MATERIAL_VEC3: return type == WGF_GFX_PRIV_SHADER_PARAM_VEC3;
        case WGF_GFX_PRIV_MATERIAL_VEC4: return type == WGF_GFX_PRIV_SHADER_PARAM_VEC4;
        case WGF_GFX_PRIV_MATERIAL_COLOR:
            return type == WGF_GFX_PRIV_SHADER_PARAM_VEC3 || type == WGF_GFX_PRIV_SHADER_PARAM_VEC4;
        default: return false;
    }
}

static int type_size(wgf_gfx_priv_shader_param_type_t type)
{
    return type == WGF_GFX_PRIV_SHADER_PARAM_VEC2 ? 8
           : type == WGF_GFX_PRIV_SHADER_PARAM_VEC3 ? 12
           : type == WGF_GFX_PRIV_SHADER_PARAM_VEC4 ? 16
                                                    : 4;
}

static int find_value(const wgf_gfx_priv_material_custom_t *custom, const char *name)
{
    int i;
    for (i = 0; name != NULL && i < custom->value_count; i++) {
        if (strcmp(custom->values[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_texture(const wgf_gfx_priv_material_custom_t *custom, const char *name)
{
    int i;
    for (i = 0; name != NULL && i < custom->texture_count; i++) {
        if (strcmp(custom->texture_names[i], name) == 0) return i;
    }
    return -1;
}

/* The material's shader when it is READY, its values checked against it the first time:
 * what it has no parameter or texture for, or holds of another type, warned about and
 * dropped. NULL while it loads, and once it has FAILED (warned once). */
static const wgf_gfx_priv_shader_t *bind(wgf_gfx_priv_material_t *material_ptr)
{
    wgf_gfx_priv_material_custom_t *custom = material_ptr->values;
    const wgf_gfx_priv_shader_t *shader = wgf_gfx_priv_shader_get(material_ptr->shader);
    int i, kept, p;

    if (shader == NULL) {
        if (!custom->told && wgf_resource_get_status(material_ptr->shader) == WGF_RESOURCE_STATUS_FAILED) {
            const wgf_core_priv_resource_t *resource = wgf_core_priv_resource_get(material_ptr->shader);
            wgf_log_warn("wgf_gfx_material: its shader (%s) failed to load: it draws nothing",
                         resource != NULL ? resource->path : "");
            custom->told = true;
        }
        return NULL;
    }
    if (custom->bound) return shader;
    if (shader->block_size[WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS] + shader->block_size[WGF_GFX_PRIV_SHADER_BLOCK_VS_PARAMS] >
        BLOCK_MAX) {
        if (!custom->told) wgf_log_warn("wgf_gfx_material: its shader's parameters take more than %d bytes", BLOCK_MAX);
        custom->told = true;
        return NULL;
    }
    for (i = kept = 0; i < custom->value_count; i++) {
        p = wgf_gfx_priv_shader_find_param(shader, custom->values[i].name);
        if (p < 0) {
            wgf_log_warn("wgf_gfx_material: its shader has no parameter '%s'", custom->values[i].name);
        } else if (!kind_fits(custom->values[i].kind, shader->params[p].type)) {
            wgf_log_warn("wgf_gfx_material: its shader's parameter '%s' is of another type", custom->values[i].name);
        } else {
            custom->values[kept++] = custom->values[i];
        }
    }
    custom->value_count = kept;
    for (i = kept = 0; i < custom->texture_count; i++) {
        if (wgf_gfx_priv_shader_find_texture(shader, custom->texture_names[i]) < 0) {
            wgf_log_warn("wgf_gfx_material: its shader has no texture '%s'", custom->texture_names[i]);
            if (custom->textures[i].texture != 0) wgf_resource_release(custom->textures[i].texture);
            continue;
        }
        memcpy(custom->texture_names[kept], custom->texture_names[i], sizeof(custom->texture_names[i]));
        custom->textures[kept++] = custom->textures[i];
    }
    custom->texture_count = kept;
    custom->bound = true;
    custom->block_stale = true;
    return shader;
}

/* ----------------------------------------------------------- the hooks ---- */

static float *value(wgf_gfx_priv_material_t *material_ptr, const char *name, wgf_gfx_priv_material_kind_t kind,
                    bool make)
{
    wgf_gfx_priv_material_custom_t *custom = material_ptr->values;
    const wgf_gfx_priv_shader_t *shader = bind(material_ptr);
    int at = find_value(custom, name), p;

    if (!make) { /* a getter's: what was set, as it was set (a color found as a vec3 or vec4 too) */
        if (at < 0) return NULL;
        if (custom->values[at].kind == kind ||
            (custom->values[at].kind == WGF_GFX_PRIV_MATERIAL_COLOR &&
             (kind == WGF_GFX_PRIV_MATERIAL_VEC3 || kind == WGF_GFX_PRIV_MATERIAL_VEC4))) {
            return custom->values[at].values;
        }
        return NULL;
    }
    if (name == NULL || name[0] == '\0' || strlen(name) >= WGF_GFX_PRIV_SHADER_NAME_MAX) {
        wgf_log_warn("wgf_gfx_material: no parameter '%s'", name != NULL ? name : "(null)");
        return NULL;
    }
    if (shader != NULL) {
        p = wgf_gfx_priv_shader_find_param(shader, name);
        if (p < 0) {
            wgf_log_warn("wgf_gfx_material: its shader has no parameter '%s'", name);
            return NULL;
        }
        if (!kind_fits(kind, shader->params[p].type)) {
            wgf_log_warn("wgf_gfx_material: its shader's parameter '%s' is of another type", name);
            return NULL;
        }
    }
    if (at < 0) {
        if (custom->value_count >= CUSTOM_VALUES) {
            wgf_log_warn("wgf_gfx_material: no room for parameter '%s'", name);
            return NULL;
        }
        at = custom->value_count++;
        snprintf(custom->values[at].name, sizeof(custom->values[at].name), "%s", name);
    }
    custom->values[at].kind = kind;
    memset(custom->values[at].values, 0, sizeof(custom->values[at].values));
    custom->block_stale = true; /* the setter writes it next */
    return custom->values[at].values;
}

static wgf_gfx_priv_material_texture_t *texture(wgf_gfx_priv_material_t *material_ptr, const char *name, bool make)
{
    wgf_gfx_priv_material_custom_t *custom = material_ptr->values;
    const wgf_gfx_priv_shader_t *shader = bind(material_ptr);
    int at = find_texture(custom, name);

    if (at >= 0) return &custom->textures[at];
    if (!make) return NULL;
    if (name == NULL || name[0] == '\0' || strlen(name) >= WGF_GFX_PRIV_SHADER_NAME_MAX ||
        (shader != NULL && wgf_gfx_priv_shader_find_texture(shader, name) < 0)) {
        wgf_log_warn("wgf_gfx_material: its shader has no texture '%s'", name != NULL ? name : "(null)");
        return NULL;
    }
    if (custom->texture_count >= CUSTOM_TEXTURES) {
        wgf_log_warn("wgf_gfx_material: no room for texture '%s'", name);
        return NULL;
    }
    at = custom->texture_count++;
    snprintf(custom->texture_names[at], sizeof(custom->texture_names[at]), "%s", name);
    memset(&custom->textures[at], 0, sizeof(custom->textures[at]));
    custom->textures[at].scale[0] = custom->textures[at].scale[1] = 1.0f;
    custom->textures[at].mipmaps = true;
    return &custom->textures[at];
}

static void clear(wgf_gfx_priv_material_t *material_ptr)
{
    wgf_gfx_priv_material_custom_t *custom = material_ptr->values;
    int i;
    for (i = 0; custom != NULL && i < custom->texture_count; i++) {
        if (custom->textures[i].texture != 0) wgf_resource_release(custom->textures[i].texture);
    }
    free(custom);
    material_ptr->values = NULL;
    if (material_ptr->shader != 0) wgf_resource_release(material_ptr->shader);
    material_ptr->shader = 0;
}

static const wgf_gfx_priv_material_custom_hooks_t hooks = {value, texture, clear, wgf_gfx_priv_stage3d_draw_custom};

/* -------------------------------------------------------------- internal ---- */

const wgf_gfx_priv_shader_t *wgf_gfx_priv_material_custom_bind(
    wgf_material_t material, const unsigned char **params,
    const wgf_gfx_priv_material_texture_t *textures[WGF_GFX_PRIV_SHADER_MAX_TEXTURES])
{
    /* the record is the material part's, read-only to the stage; its values are this part's */
    wgf_gfx_priv_material_t *material_ptr = (wgf_gfx_priv_material_t *)wgf_gfx_priv_material_get(material);
    const wgf_gfx_priv_shader_t *shader;
    wgf_gfx_priv_material_custom_t *custom;
    int i, at;
    if (material_ptr == NULL || material_ptr->custom == NULL || (shader = bind(material_ptr)) == NULL) return NULL;
    custom = material_ptr->values;
    if (custom->block_stale) {
        memset(custom->block, 0, sizeof(custom->block));
        for (i = 0; i < custom->value_count; i++) {
            const wgf_gfx_priv_shader_param_t *param =
                &shader->params[wgf_gfx_priv_shader_find_param(shader, custom->values[i].name)];
            const int offset = param->offset + (param->block == WGF_GFX_PRIV_SHADER_BLOCK_VS_PARAMS
                                                    ? shader->block_size[WGF_GFX_PRIV_SHADER_BLOCK_FS_PARAMS]
                                                    : 0);
            memcpy((unsigned char *)custom->block + offset, custom->values[i].values, (size_t)type_size(param->type));
        }
        custom->block_stale = false;
    }
    *params = (const unsigned char *)custom->block;
    for (i = 0; i < WGF_GFX_PRIV_SHADER_MAX_TEXTURES; i++) {
        at = i < shader->texture_count ? find_texture(custom, shader->textures[i]) : -1;
        textures[i] = at >= 0 ? &custom->textures[at] : NULL;
    }
    return shader;
}

/* ------------------------------------------------------------ public API ---- */

wgf_material_t wgf_material_create_custom(wgf_shader_t shader)
{
    wgf_gfx_priv_material_custom_t *custom;
    wgf_gfx_priv_material_t *material_ptr;
    wgf_material_t material;

    if (!wgf_gfx_priv_shader_is(shader)) {
        wgf_log_error("wgf_material_create_custom: needs a shader (wgf_shader_create)");
        return 0;
    }
    custom = (wgf_gfx_priv_material_custom_t *)calloc(1, sizeof(*custom));
    material = custom != NULL ? wgf_material_create(WGF_MATERIAL_SHADING_UNLIT) : 0;
    material_ptr = wgf_gfx_priv_material_make_custom(material);
    if (material_ptr == NULL) {
        free(custom);
        return 0;
    }
    material_ptr->custom = &hooks;
    material_ptr->values = custom;
    material_ptr->shader = shader;
    wgf_core_priv_resource_retain(shader);
    return material;
}

wgf_shader_t wgf_material_get_shader(wgf_material_t material)
{
    const wgf_gfx_priv_material_t *material_ptr = wgf_gfx_priv_material_get(material);
    return material_ptr != NULL ? material_ptr->shader : 0;
}
