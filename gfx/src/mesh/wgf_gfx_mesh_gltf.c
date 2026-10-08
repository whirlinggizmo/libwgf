#include "wgf_mesh.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgltf.h"
#include "material/wgf_gfx_material_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "mesh/wgf_gfx_mesh_record_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_model_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_light.h"
#include "wgf_log.h"
#include "wgf_material.h"
#include "wgf_resource.h"
#include "wgf_texture.h"

/* A mesh from a glTF file (`.gltf` with its buffers and images, or `.glb`), read by cgltf:
 * its loader (prepare on a worker, finish on the main thread a step at a time), the files
 * it names for the asset part (the lister, registered when this object is linked), and
 * wgf_mesh_create, the one call that references this object, so a program with no glTF
 * links none of cgltf (libwgt's design; its HISTORY, "The glTF leak"). libwgt's
 * wgt_gfx_mesh_gltf.c, without its skinning, animation, and compressed textures (milestone
 * 3's, and step 10's), and with the file's lights (KHR_lights_punctual), which libwgt didn't
 * read. The file's node tree is kept in the mesh, for a model to make actors of, and each
 * of the file's meshes is a mesh of its own, shared by the nodes showing it. */

typedef wgf_gfx_priv_mesh_record_primitive_t primitive_t;
typedef wgf_gfx_priv_mesh_record_t mesh_t;

/* An image of the file being loaded: decoded while preparing, uploaded as a texture while
 * finishing. The loader holds one reference to each texture; materials add their own. */
typedef struct gltf_image_t {
    void *decoded;        /* freed once uploaded */
    wgf_texture_t texture; /* 0: not uploaded (yet) */
    bool failed;          /* couldn't be read or decoded (warned) */
} gltf_image_t;

/* A glTF file loaded into CPU data (prepare, any thread), made on the main thread in steps:
 * buffers, then one texture a step, then materials. */
typedef struct prepared_t {
    mesh_t mesh;         /* its primitives (the glTF meshes' in order) and tree, moved into meshes when finished */
    int *mesh_first, *mesh_count; /* each glTF mesh's run of the primitives */
    unsigned char *file; /* the file's bytes: a .glb's buffers point into them */
    cgltf_data *gltf;
    gltf_image_t *images;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
    int step; /* 0: buffers, then images, then materials */
    size_t next_image;
    bool texcoord_warned;
} prepared_t;

/* `uri`, relative to the file at `path`, as a path under fs's root, into `out`: false for a
 * URI that isn't relative (a scheme, or absolute) or leaves the root. */
static bool join_relative(const char *path, const char *uri, char *out, size_t out_size)
{
    char joined[WGF_CORE_PRIV_FS_PATH_MAX];
    const char *slash = strrchr(path, '/');
    const int dir = slash != NULL ? (int)(slash - path) + 1 : 0;
    if (uri[0] == '/' || uri[0] == '\\' || strchr(uri, ':') != NULL) return false;
    if (snprintf(joined, sizeof(joined), "%.*s%s", dir, path, uri) >= (int)sizeof(joined)) return false;
    cgltf_decode_uri(joined + dir); /* "%20" and the like, as cgltf does for buffers */
    return wgf_core_priv_fs_normalize_path(joined, out, out_size);
}

/* cgltf reads a .gltf's buffer files through these, so they come from fs. */
static cgltf_result read_gltf_file(const cgltf_memory_options *memory, const cgltf_file_options *file, const char *path,
                                   cgltf_size *size, void **data)
{
    char normalized[WGF_CORE_PRIV_FS_PATH_MAX];
    unsigned char *bytes;
    int n = 0;
    (void)memory;
    (void)file;
    if (!wgf_core_priv_fs_normalize_path(path, normalized, sizeof(normalized)) ||
        !wgf_core_priv_fs_read(normalized, &bytes, &n)) {
        return cgltf_result_file_not_found;
    }
    *data = bytes;
    *size = (cgltf_size)n;
    return cgltf_result_success;
}

static void release_gltf_file(const cgltf_memory_options *memory, const cgltf_file_options *file, void *data,
                              cgltf_size size)
{
    (void)memory;
    (void)file;
    (void)size;
    wgf_core_priv_fs_read_free((unsigned char *)data);
}

/* Decode a glTF image (any thread): from a buffer view, a data: URI, or a file beside the
 * glTF file. Failures are warned and marked. */
static void decode_image(prepared_t *prepared, const cgltf_image *img)
{
    gltf_image_t *image = &prepared->images[img - prepared->gltf->images];
    const unsigned char *bytes = NULL;
    unsigned char *read = NULL;
    void *owned = NULL;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
    int size = 0;

    if (image->decoded != NULL || image->failed) return;
    if (img->buffer_view != NULL) {
        const cgltf_buffer_view *view = img->buffer_view;
        if (view->buffer != NULL && view->buffer->data != NULL) {
            bytes = (const unsigned char *)view->buffer->data + view->offset;
            size = (int)view->size;
        }
    } else if (img->uri != NULL && strncmp(img->uri, "data:", 5) == 0) {
        const char *base64 = strstr(img->uri, ";base64,");
        if (base64 != NULL) {
            size_t len, padding = 0;
            cgltf_options options;
            memset(&options, 0, sizeof(options));
            base64 += 8;
            len = strlen(base64);
            while (len > 0 && base64[len - 1] == '=') len--, padding++;
            size = (int)((len + padding) / 4 * 3 - padding);
            if (cgltf_load_buffer_base64(&options, (cgltf_size)size, base64, &owned) == cgltf_result_success) {
                bytes = (const unsigned char *)owned;
            }
        }
    } else if (img->uri != NULL && join_relative(prepared->path, img->uri, path, sizeof(path)) &&
               wgf_core_priv_fs_read(path, &read, &size)) {
        bytes = read;
    }
    if (bytes == NULL) {
        wgf_log_warn("wgf_gfx_mesh: %s: image %d (%s) couldn't be read; using the placeholder texture",
                     prepared->path, (int)(img - prepared->gltf->images),
                     img->uri != NULL && strncmp(img->uri, "data:", 5) != 0 ? img->uri : "embedded");
        image->failed = true;
        return;
    }
    image->decoded = wgf_gfx_priv_texture_decode(bytes, size, prepared->path);
    image->failed = image->decoded == NULL; /* warned */
    free(owned);
    if (read != NULL) wgf_core_priv_fs_read_free(read);
}

static wgf_texture_wrap_t gltf_wrap(cgltf_wrap_mode mode)
{
    switch (mode) {
        case cgltf_wrap_mode_clamp_to_edge: return WGF_TEXTURE_WRAP_CLAMP;
        case cgltf_wrap_mode_mirrored_repeat: return WGF_TEXTURE_WRAP_MIRROR;
        default: return WGF_TEXTURE_WRAP_REPEAT;
    }
}

/* Give material parameter `name` a glTF texture, with its texture coordinate set,
 * KHR_texture_transform, and sampler. An image that couldn't be loaded leaves a data
 * texture (normal, metallic-roughness, occlusion) unset, where a checker would only distort
 * the lighting, and gives a color one the placeholder texture, so the problem shows. */
static void set_texture(prepared_t *prepared, wgf_material_t material, const char *name,
                        const cgltf_texture_view *view)
{
    const cgltf_sampler *sampler;
    wgf_texture_t texture = 0;
    char param[64];
    int texcoord;

    if (view->texture == NULL) return;
    if (view->texture->image == NULL) {
        wgf_log_warn("wgf_gfx_mesh: %s: a texture has no image in a format it reads (PNG, JPEG); using the "
                     "placeholder texture",
                     prepared->path);
    } else {
        texture = prepared->images[view->texture->image - prepared->gltf->images].texture;
    }
    if (texture == 0) {
        if (strcmp(name, "base_color_texture") != 0 && strcmp(name, "emissive_texture") != 0) return;
        texture = wgf_gfx_priv_texture_get_placeholder();
    }
    wgf_material_set_texture(material, name, texture);

    texcoord = view->has_transform && view->transform.has_texcoord ? view->transform.texcoord : view->texcoord;
    snprintf(param, sizeof(param), "%s_texcoord", name);
    if (texcoord > 1) {
        if (!prepared->texcoord_warned) {
            wgf_log_warn("wgf_gfx_mesh: %s: texture coordinate set %d isn't supported (0 and 1 are); set 0 is used",
                         prepared->path, texcoord);
            prepared->texcoord_warned = true;
        }
        texcoord = 0;
    }
    wgf_material_set_int(material, param, texcoord);

    if (view->has_transform) {
        snprintf(param, sizeof(param), "%s_offset", name);
        wgf_material_set_vec2(material, param, view->transform.offset[0], view->transform.offset[1]);
        snprintf(param, sizeof(param), "%s_rotation", name);
        wgf_material_set_float(material, param, view->transform.rotation);
        snprintf(param, sizeof(param), "%s_scale", name);
        wgf_material_set_vec2(material, param, view->transform.scale[0], view->transform.scale[1]);
    }

    sampler = view->texture->sampler;
    if (sampler != NULL) {
        const cgltf_filter_type min = sampler->min_filter, mag = sampler->mag_filter;
        const bool nearest_min = min == cgltf_filter_type_nearest || min == cgltf_filter_type_nearest_mipmap_nearest ||
                                 min == cgltf_filter_type_nearest_mipmap_linear;
        /* one filter for both: magnification decides when given (it's what shows up close) */
        const bool nearest = mag == cgltf_filter_type_nearest || (mag == cgltf_filter_type_undefined && nearest_min);
        wgf_material_set_texture_sampling(material, name, gltf_wrap(sampler->wrap_s), gltf_wrap(sampler->wrap_t),
                                          nearest ? WGF_TEXTURE_FILTER_NEAREST : WGF_TEXTURE_FILTER_LINEAR);
        wgf_gfx_priv_material_set_texture_mipmaps(material, name,
                                                  min != cgltf_filter_type_nearest && min != cgltf_filter_type_linear);
    }
}

/* A material from a glTF material; NULL gives glTF's default material. */
static wgf_material_t create_gltf_material(prepared_t *prepared, const cgltf_material *src)
{
    const wgf_material_t material =
        wgf_material_create(src != NULL && src->unlit ? WGF_MATERIAL_SHADING_UNLIT : WGF_MATERIAL_SHADING_PBR);
    float strength;

    if (material == 0 || src == NULL) return material;
    switch (src->alpha_mode) {
        case cgltf_alpha_mode_mask: wgf_material_set_alpha_mode(material, WGF_ALPHA_MODE_MASK, src->alpha_cutoff); break;
        case cgltf_alpha_mode_blend: wgf_material_set_alpha_mode(material, WGF_ALPHA_MODE_BLEND, src->alpha_cutoff); break;
        default: break;
    }
    wgf_material_set_double_sided(material, src->double_sided);
    if (src->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness *pbr = &src->pbr_metallic_roughness;
        wgf_material_set_vec4(material, "base_color", pbr->base_color_factor[0], pbr->base_color_factor[1],
                              pbr->base_color_factor[2], pbr->base_color_factor[3]);
        wgf_material_set_float(material, "metallic", pbr->metallic_factor);
        wgf_material_set_float(material, "roughness", pbr->roughness_factor);
        set_texture(prepared, material, "base_color_texture", &pbr->base_color_texture);
        set_texture(prepared, material, "metallic_roughness_texture", &pbr->metallic_roughness_texture);
    }
    if (src->normal_texture.texture != NULL) {
        set_texture(prepared, material, "normal_texture", &src->normal_texture);
        wgf_material_set_float(material, "normal_scale", src->normal_texture.scale);
    }
    if (src->occlusion_texture.texture != NULL) {
        set_texture(prepared, material, "occlusion_texture", &src->occlusion_texture);
        wgf_material_set_float(material, "occlusion_strength", src->occlusion_texture.scale);
    }
    strength = src->has_emissive_strength ? src->emissive_strength.emissive_strength : 1.0f;
    wgf_material_set_vec3(material, "emissive", src->emissive_factor[0] * strength, src->emissive_factor[1] * strength,
                          src->emissive_factor[2] * strength);
    set_texture(prepared, material, "emissive_texture", &src->emissive_texture);
    return material;
}

/* One material per glTF material, plus glTF's default material, last, if a primitive uses
 * it. */
static void load_materials(prepared_t *prepared)
{
    mesh_t *mesh = &prepared->mesh;
    const int count = (int)prepared->gltf->materials_count;
    bool uses_default = false;
    int p, i;

    for (p = 0; p < mesh->primitive_count; p++) uses_default = uses_default || mesh->primitives[p].material == count;
    mesh->material_count = count + (uses_default ? 1 : 0);
    if (mesh->material_count == 0) return;
    mesh->materials = (wgf_material_t *)calloc((size_t)mesh->material_count, sizeof(wgf_material_t));
    if (mesh->materials == NULL) {
        mesh->material_count = 0;
        return;
    }
    for (i = 0; i < mesh->material_count; i++) {
        mesh->materials[i] = create_gltf_material(prepared, i < count ? &prepared->gltf->materials[i] : NULL);
    }
}

/* A primitive's CPU data (any thread), in its node's space, in the vertex layout every
 * shader takes: tangents from the file, or made from texture coordinate set 0. */
static bool build_primitive(const cgltf_data *g, const cgltf_primitive *prim, int default_material, primitive_t *out)
{
    const cgltf_accessor *pos = NULL, *nrm = NULL, *uv[2] = {NULL, NULL}, *tan = NULL, *col = NULL;
    const size_t stride = WGF_GFX_PRIV_MESH_VERTEX_FLOATS;
    cgltf_size vcount, icount, i, a;
    float *verts = NULL, *positions = NULL, *normals = NULL, *uvs[2] = {NULL, NULL}, *tangents = NULL, *colors = NULL;
    uint32_t *indices = NULL;
    bool ok = false;
    int set;

    for (a = 0; a < prim->attributes_count; a++) {
        const cgltf_attribute *attr = &prim->attributes[a];
        switch (attr->type) {
            case cgltf_attribute_type_position: pos = attr->data; break;
            case cgltf_attribute_type_normal: nrm = attr->data; break;
            case cgltf_attribute_type_tangent: tan = attr->data; break;
            case cgltf_attribute_type_texcoord:
                if (attr->index < 2) uv[attr->index] = attr->data;
                break;
            case cgltf_attribute_type_color:
                if (attr->index == 0) col = attr->data;
                break;
            default: break;
        }
    }
    if (pos == NULL || pos->count == 0) return false;
    vcount = pos->count;
    memset(out, 0, sizeof(*out));
    positions = (float *)calloc(vcount * 3, sizeof(float));
    normals = (float *)calloc(vcount * 3, sizeof(float));
    uvs[0] = (float *)calloc(vcount * 2, sizeof(float));
    uvs[1] = (float *)calloc(vcount * 2, sizeof(float));
    colors = (float *)malloc(vcount * 4 * sizeof(float));
    tangents = (float *)calloc(vcount * 4, sizeof(float));
    verts = (float *)calloc(vcount * stride, sizeof(float));
    icount = prim->indices != NULL ? prim->indices->count : vcount;
    indices = (uint32_t *)calloc(icount > 0 ? icount : 1, sizeof(uint32_t));
    if (positions == NULL || normals == NULL || uvs[0] == NULL || uvs[1] == NULL || colors == NULL || tangents == NULL ||
        verts == NULL || indices == NULL) {
        goto done;
    }

    out->bounds_min = wgf_vec3_make(1e30f, 1e30f, 1e30f);
    out->bounds_max = wgf_vec3_make(-1e30f, -1e30f, -1e30f);
    for (i = 0; i < vcount; i++) {
        float p[3] = {0, 0, 0}, n[3] = {0, 1, 0}, c[4] = {1, 1, 1, 1};
        cgltf_accessor_read_float(pos, i, p, 3);
        memcpy(&positions[i * 3], p, sizeof(p));
        out->bounds_min = wgf_vec3_make(fminf(out->bounds_min.x, p[0]), fminf(out->bounds_min.y, p[1]),
                                        fminf(out->bounds_min.z, p[2]));
        out->bounds_max = wgf_vec3_make(fmaxf(out->bounds_max.x, p[0]), fmaxf(out->bounds_max.y, p[1]),
                                        fmaxf(out->bounds_max.z, p[2]));
        if (nrm != NULL) cgltf_accessor_read_float(nrm, i, n, 3);
        memcpy(&normals[i * 3], n, sizeof(n));
        for (set = 0; set < 2; set++) {
            if (uv[set] != NULL) cgltf_accessor_read_float(uv[set], i, &uvs[set][i * 2], 2);
        }
        /* linear rgba; normalized integer colors are read as 0..1 */
        if (col != NULL) cgltf_accessor_read_float(col, i, c, col->type == cgltf_type_vec4 ? 4 : 3);
        memcpy(&colors[i * 4], c, sizeof(c));
    }
    for (i = 0; i < icount; i++) {
        indices[i] = prim->indices != NULL ? (uint32_t)cgltf_accessor_read_index(prim->indices, i) : (uint32_t)i;
        if (indices[i] >= vcount) indices[i] = 0; /* a broken file: never read past the vertices */
    }
    if (tan != NULL) {
        for (i = 0; i < vcount; i++) {
            float t[4] = {1, 0, 0, 1};
            cgltf_accessor_read_float(tan, i, t, 4);
            memcpy(&tangents[i * 4], t, sizeof(t));
        }
    } else {
        /* from texture coordinate set 0: normal maps using set 1 need tangents in the file */
        wgf_gfx_priv_mesh_generate_tangents(positions, normals, uvs[0], (int)vcount, indices, (int)icount, tangents);
    }
    for (i = 0; i < vcount; i++) {
        float *v = &verts[i * stride];
        memcpy(v, &positions[i * 3], 3 * sizeof(float));
        memcpy(v + 3, &normals[i * 3], 3 * sizeof(float));
        memcpy(v + 6, &uvs[0][i * 2], 2 * sizeof(float));
        memcpy(v + 8, &uvs[1][i * 2], 2 * sizeof(float));
        memcpy(v + 10, &tangents[i * 4], 4 * sizeof(float));
        memcpy(v + 14, &colors[i * 4], 4 * sizeof(float));
    }
    out->vertices = verts;
    out->vertex_count = (int)vcount;
    out->indices = indices;
    out->index_count = (int)icount;
    out->material = prim->material != NULL ? (int)(prim->material - g->materials) : default_material;
    out->vertex_buffer.id = SG_INVALID_ID;
    out->index_buffer.id = SG_INVALID_ID;
    verts = NULL;
    indices = NULL;
    ok = true;

done:
    free(positions);
    free(normals);
    free(uvs[0]);
    free(uvs[1]);
    free(colors);
    free(tangents);
    free(verts);
    free(indices);
    return ok;
}

/* A node's transform as position, rotation, and scale: a matrix is taken apart. */
static void node_local_trs(const cgltf_node *n, wgf_gfx_priv_mesh_node_t *out)
{
    out->position = wgf_vec3_make(0.0f, 0.0f, 0.0f);
    out->rotation = wgf_quat_identity();
    out->scale = wgf_vec3_make(1.0f, 1.0f, 1.0f);
    if (n->has_matrix) {
        const float *m = n->matrix;
        const float sx = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        const float sy = sqrtf(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
        const float sz = sqrtf(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
        out->position = wgf_vec3_make(m[12], m[13], m[14]);
        out->scale = wgf_vec3_make(sx, sy, sz);
        if (sx > 1e-6f && sy > 1e-6f && sz > 1e-6f) {
            const float tr = m[0] / sx + m[5] / sy + m[10] / sz;
            if (tr > 0.0f) {
                const float qs = sqrtf(tr + 1.0f) * 2.0f;
                out->rotation = wgf_quat_make((m[6] / sy - m[9] / sz) / qs, (m[8] / sz - m[2] / sx) / qs,
                                              (m[1] / sx - m[4] / sy) / qs, 0.25f * qs);
            }
        }
        return;
    }
    if (n->has_translation) out->position = wgf_vec3_make(n->translation[0], n->translation[1], n->translation[2]);
    if (n->has_rotation) out->rotation = wgf_quat_make(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]);
    if (n->has_scale) out->scale = wgf_vec3_make(n->scale[0], n->scale[1], n->scale[2]);
}

static char *copy_name(const char *name)
{
    const size_t n = name != NULL ? strlen(name) : 0;
    char *copy = (char *)malloc(n + 1);
    if (copy != NULL) {
        if (n > 0) memcpy(copy, name, n);
        copy[n] = '\0';
    }
    return copy;
}

/* Template node for glTF node `n` under `parent`, its primitives, its light, and then its
 * children's, depth first. `built` maps the file's nodes to the tree's (-1: not built), and
 * guards against a file whose nodes loop. */
static void add_node(mesh_t *mesh, const cgltf_data *g, const cgltf_node *n, int parent, int *built, const int *counts)
{
    wgf_gfx_priv_mesh_node_t *node;
    const int index = mesh->node_count;
    cgltf_size c;
    if (built[n - g->nodes] >= 0) return;
    built[n - g->nodes] = index;
    node = &mesh->nodes[mesh->node_count++];
    memset(node, 0, sizeof(*node));
    node->name = copy_name(n->name);
    node->parent = parent;
    node->light = n->light != NULL ? (int)(n->light - g->lights) : -1;
    node_local_trs(n, node);
    node->mesh = n->mesh != NULL && counts[n->mesh - g->meshes] > 0 ? (int)(n->mesh - g->meshes) : -1;
    for (c = 0; c < n->children_count; c++) add_node(mesh, g, n->children[c], index, built, counts);
}

/* The file's lights (KHR_lights_punctual): their type, linear color, intensity, range, and
 * cone, as the extension gives them (its defaults where it gives none: a cone of 0 to a
 * quarter turn's half). */
static bool parse_lights(mesh_t *mesh, const cgltf_data *g)
{
    cgltf_size i;
    if (g->lights_count == 0) return true;
    mesh->lights = (wgf_gfx_priv_mesh_light_t *)calloc(g->lights_count, sizeof(wgf_gfx_priv_mesh_light_t));
    if (mesh->lights == NULL) return false;
    for (i = 0; i < g->lights_count; i++) {
        const cgltf_light *src = &g->lights[i];
        wgf_gfx_priv_mesh_light_t *light = &mesh->lights[i];
        light->type = src->type == cgltf_light_type_directional ? WGF_LIGHT_TYPE_DIRECTIONAL
                      : src->type == cgltf_light_type_spot      ? WGF_LIGHT_TYPE_SPOT
                                                                : WGF_LIGHT_TYPE_POINT;
        memcpy(light->color, src->color, sizeof(light->color));
        light->intensity = src->intensity;
        light->range = src->range;
        light->inner = src->spot_inner_cone_angle;
        light->outer = src->spot_outer_cone_angle;
    }
    mesh->light_count = (int)g->lights_count;
    return true;
}

/* The whole mesh's box: its nodes' primitives', each in its own node's space (a model
 * culls each node by its own box; this one is a whole mesh's, unplaced). */
static void merge_bounds(mesh_t *mesh)
{
    int p;
    mesh->bounds_min = wgf_vec3_make(1e30f, 1e30f, 1e30f);
    mesh->bounds_max = wgf_vec3_make(-1e30f, -1e30f, -1e30f);
    for (p = 0; p < mesh->primitive_count; p++) {
        const primitive_t *prim = &mesh->primitives[p];
        mesh->bounds_min = wgf_vec3_make(fminf(mesh->bounds_min.x, prim->bounds_min.x),
                                         fminf(mesh->bounds_min.y, prim->bounds_min.y),
                                         fminf(mesh->bounds_min.z, prim->bounds_min.z));
        mesh->bounds_max = wgf_vec3_make(fmaxf(mesh->bounds_max.x, prim->bounds_max.x),
                                         fmaxf(mesh->bounds_max.y, prim->bounds_max.y),
                                         fmaxf(mesh->bounds_max.z, prim->bounds_max.z));
    }
}

/* A glTF file parsed into a mesh's primitives, tree, and lights (any thread): the default
 * scene's nodes (or, with no scene, every node without a parent), depth first. A file whose
 * nodes show no mesh gets a node for each of its meshes instead. On success the parsed file
 * stays, for its images and materials. */
static bool parse_model(prepared_t *prepared, int size)
{
    cgltf_options options;
    mesh_t *mesh = &prepared->mesh;
    const cgltf_scene *scene;
    cgltf_data *g = NULL;
    int *built;
    bool shown;
    size_t total = 0;
    cgltf_size m, n, p;
    int i;

    memset(&options, 0, sizeof(options));
    options.file.read = read_gltf_file;
    options.file.release = release_gltf_file;
    if (cgltf_parse(&options, prepared->file, (cgltf_size)size, &g) != cgltf_result_success) return false;
    prepared->gltf = g;
    if (cgltf_load_buffers(&options, g, prepared->path) != cgltf_result_success) {
        wgf_log_warn("wgf_gfx_mesh: %s: its buffers couldn't be read", prepared->path);
        return false;
    }
    if (cgltf_validate(g) != cgltf_result_success) {
        wgf_log_warn("wgf_gfx_mesh: %s: not a valid glTF file", prepared->path);
        return false;
    }
    for (m = 0; m < g->meshes_count; m++) total += g->meshes[m].primitives_count;
    mesh->primitives = (primitive_t *)calloc(total > 0 ? total : 1, sizeof(primitive_t));
    mesh->nodes =
        (wgf_gfx_priv_mesh_node_t *)calloc(g->nodes_count + g->meshes_count + 1, sizeof(wgf_gfx_priv_mesh_node_t));
    prepared->mesh_first = (int *)calloc(g->meshes_count + 1, sizeof(int));
    prepared->mesh_count = (int *)calloc(g->meshes_count + 1, sizeof(int));
    built = (int *)malloc((g->nodes_count + 1) * sizeof(int));
    if (mesh->primitives == NULL || mesh->nodes == NULL || prepared->mesh_first == NULL || prepared->mesh_count == NULL ||
        built == NULL || !parse_lights(mesh, g)) {
        free(built);
        return false;
    }
    /* each glTF mesh once, however many nodes show it: they share it */
    for (m = 0; m < g->meshes_count; m++) {
        prepared->mesh_first[m] = mesh->primitive_count;
        for (p = 0; p < g->meshes[m].primitives_count; p++) {
            if (g->meshes[m].primitives[p].type != cgltf_primitive_type_triangles) continue;
            if (build_primitive(g, &g->meshes[m].primitives[p], (int)g->materials_count,
                                &mesh->primitives[mesh->primitive_count])) {
                mesh->primitive_count++;
            }
        }
        prepared->mesh_count[m] = mesh->primitive_count - prepared->mesh_first[m];
    }
    for (n = 0; n < g->nodes_count; n++) built[n] = -1;
    scene = g->scene != NULL ? g->scene : (g->scenes_count > 0 ? &g->scenes[0] : NULL);
    if (scene != NULL) {
        for (n = 0; n < scene->nodes_count; n++) add_node(mesh, g, scene->nodes[n], -1, built, prepared->mesh_count);
    } else {
        for (n = 0; n < g->nodes_count; n++) {
            if (g->nodes[n].parent == NULL) add_node(mesh, g, &g->nodes[n], -1, built, prepared->mesh_count);
        }
    }
    free(built);
    shown = false;
    for (i = 0; i < mesh->node_count; i++) shown = shown || mesh->nodes[i].mesh >= 0;
    if (!shown) { /* no node shows a mesh: a node for each mesh */
        for (m = 0; m < g->meshes_count; m++) {
            wgf_gfx_priv_mesh_node_t *node;
            if (prepared->mesh_count[m] == 0) continue;
            node = &mesh->nodes[mesh->node_count++];
            memset(node, 0, sizeof(*node));
            node->name = copy_name(g->meshes[m].name);
            node->parent = -1;
            node->light = -1;
            node->rotation = wgf_quat_identity();
            node->scale = wgf_vec3_make(1.0f, 1.0f, 1.0f);
            node->mesh = (int)m;
        }
    }
    for (i = 0; i < mesh->node_count; i++) {
        if (mesh->nodes[i].name == NULL) return false; /* out of memory */
    }
    if (mesh->primitive_count == 0 && mesh->light_count == 0) {
        wgf_log_warn("wgf_gfx_mesh: %s: no triangles or lights in it", prepared->path);
        return false;
    }
    merge_bounds(mesh);
    return true;
}

static void discard(void *data)
{
    prepared_t *prepared = (prepared_t *)data;
    cgltf_size i;
    if (prepared == NULL) return;
    if (prepared->images != NULL) {
        for (i = 0; i < prepared->gltf->images_count; i++) {
            wgf_gfx_priv_texture_decoded_free(prepared->images[i].decoded);
            if (prepared->images[i].texture != 0) wgf_resource_release(prepared->images[i].texture); /* materials hold theirs */
        }
    }
    free(prepared->images);
    free(prepared->mesh_first);
    free(prepared->mesh_count);
    wgf_gfx_priv_mesh_free_data(&prepared->mesh); /* empty once moved into the mesh */
    cgltf_free(prepared->gltf);
    if (prepared->file != NULL) wgf_core_priv_fs_read_free(prepared->file);
    free(prepared);
}

static void *prepare(const char *path)
{
    prepared_t *prepared = (prepared_t *)calloc(1, sizeof(prepared_t));
    int size = 0;
    cgltf_size t;

    if (prepared == NULL) return NULL;
    snprintf(prepared->path, sizeof(prepared->path), "%s", path);
    if (!wgf_core_priv_fs_read(path, &prepared->file, &size)) {
        wgf_log_warn("wgf_gfx_mesh: %s: couldn't be read", path);
        discard(prepared);
        return NULL;
    }
    if (!parse_model(prepared, size)) {
        wgf_log_warn("wgf_gfx_mesh: %s: couldn't be loaded", path);
        discard(prepared);
        return NULL;
    }
    prepared->images = (gltf_image_t *)calloc(prepared->gltf->images_count + 1, sizeof(gltf_image_t));
    if (prepared->images == NULL) {
        discard(prepared);
        return NULL;
    }
    /* the images textures use, decoded here: the slow part of most models */
    for (t = 0; t < prepared->gltf->textures_count; t++) {
        if (prepared->gltf->textures[t].image != NULL) decode_image(prepared, prepared->gltf->textures[t].image);
    }
    return prepared;
}

/* The buffers, then a texture a step, then the materials, and the mesh is ready, and the
 * models waiting for it make their trees. */
/* Each glTF mesh made a mesh of its own, its primitives moved into it, drawn with the
 * file's materials: the file's meshes, which the nodes showing them share (Rob's decision
 * over a copy a node: the toy car's four wheels are one mesh). The skin a node gives with
 * its mesh (milestone 3's) is kept beside the mesh, never in it, so a skinned mesh is
 * shared too. Made before `file`'s record is resolved again: each is a resource added, and
 * the pool may move. False (the parts made let go of) out of memory. */
static bool make_parts(prepared_t *prepared, wgf_mesh_t file)
{
    const int count = (int)prepared->gltf->meshes_count;
    wgf_mesh_t *parts = (wgf_mesh_t *)calloc((size_t)(count > 0 ? count : 1), sizeof(wgf_mesh_t));
    mesh_t *mesh_ptr;
    int m;
    if (parts == NULL) return false;
    for (m = 0; m < count; m++) {
        const int n = prepared->mesh_count[m];
        primitive_t *own;
        if (n == 0) continue;
        own = (primitive_t *)malloc(sizeof(primitive_t) * (size_t)n);
        if (own != NULL) {
            memcpy(own, &prepared->mesh.primitives[prepared->mesh_first[m]], sizeof(primitive_t) * (size_t)n);
            memset(&prepared->mesh.primitives[prepared->mesh_first[m]], 0, sizeof(primitive_t) * (size_t)n); /* moved */
            parts[m] = wgf_gfx_priv_mesh_create_from(own, n, prepared->mesh.materials, prepared->mesh.material_count);
        }
        if (parts[m] == 0) {
            int made;
            for (made = 0; made < m; made++) {
                if (parts[made] != 0) wgf_resource_release(parts[made]);
            }
            free(parts);
            return false;
        }
    }
    free(prepared->mesh.primitives); /* every one moved */
    prepared->mesh.primitives = NULL;
    prepared->mesh.primitive_count = 0;
    mesh_ptr = wgf_gfx_priv_mesh_record(file);
    mesh_ptr->parts = parts;
    mesh_ptr->part_count = count;
    return true;
}

static wgf_core_priv_load_step_t finish(void *data, wgf_handle_t resource)
{
    prepared_t *prepared = (prepared_t *)data;
    mesh_t *mesh_ptr = wgf_gfx_priv_mesh_record(resource);
    int p;

    if (mesh_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (!wgf_gfx_priv_render_is_running()) return WGF_CORE_PRIV_LOAD_MORE; /* no GPU yet: wait for one */
    if (prepared->step == 0) {
        for (p = 0; p < prepared->mesh.primitive_count; p++) {
            prepared->mesh.primitives[p].vertex_buffer.id = SG_INVALID_ID;
            prepared->mesh.primitives[p].index_buffer.id = SG_INVALID_ID;
            if (!wgf_gfx_priv_mesh_upload(&prepared->mesh.primitives[p])) return WGF_CORE_PRIV_LOAD_FAILED;
        }
        prepared->step = 1;
        return WGF_CORE_PRIV_LOAD_MORE;
    }
    while (prepared->next_image < prepared->gltf->images_count) {
        gltf_image_t *image = &prepared->images[prepared->next_image++];
        if (image->decoded != NULL) {
            image->texture = wgf_gfx_priv_texture_create_decoded(image->decoded); /* takes it */
            image->decoded = NULL;
            image->failed = image->texture == 0;
            return WGF_CORE_PRIV_LOAD_MORE;
        }
    }
    load_materials(prepared);
    if (!make_parts(prepared, resource)) return WGF_CORE_PRIV_LOAD_FAILED;
    mesh_ptr = wgf_gfx_priv_mesh_record(resource); /* resolved again: the parts' adds may have moved the pool */
    mesh_ptr->nodes = prepared->mesh.nodes;
    mesh_ptr->node_count = prepared->mesh.node_count;
    mesh_ptr->lights = prepared->mesh.lights;
    mesh_ptr->light_count = prepared->mesh.light_count;
    mesh_ptr->materials = prepared->mesh.materials;
    mesh_ptr->material_count = prepared->mesh.material_count;
    mesh_ptr->bounds_min = prepared->mesh.bounds_min;
    mesh_ptr->bounds_max = prepared->mesh.bounds_max;
    memset(&prepared->mesh, 0, sizeof(prepared->mesh));
    wgf_core_priv_resource_loaded(resource, NULL);
    wgf_gfx_priv_model_mesh_done(resource);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void fail(wgf_handle_t resource)
{
    if (wgf_gfx_priv_mesh_record(resource) == NULL) return;
    wgf_core_priv_resource_failed(resource);
    wgf_gfx_priv_model_mesh_done(resource);
}

static const wgf_core_priv_loader_t loader = {"mesh", prepare, finish, discard, fail, NULL};

/* The files a glTF names, for the asset part to make local with it (core's lister,
 * wgf_core_load_priv.h): its buffers, required; its images, optional (a missing one gets the
 * placeholder). */
static void list_gltf_dependencies(const unsigned char *data, int size, wgf_core_priv_load_add_fn add, void *context)
{
    cgltf_options options;
    cgltf_data *g = NULL;
    cgltf_size i;
    memset(&options, 0, sizeof(options));
    if (cgltf_parse(&options, data, (cgltf_size)size, &g) != cgltf_result_success) {
        return; /* wgf_mesh_create reports the broken file */
    }
    for (i = 0; i < g->buffers_count; i++) {
        if (g->buffers[i].uri != NULL && strncmp(g->buffers[i].uri, "data:", 5) != 0) {
            add(g->buffers[i].uri, NULL, true, context);
        }
    }
    for (i = 0; i < g->images_count; i++) {
        if (g->images[i].uri != NULL && g->images[i].buffer_view == NULL && strncmp(g->images[i].uri, "data:", 5) != 0) {
            add(g->images[i].uri, NULL, false, context);
        }
    }
    cgltf_free(g);
}

/* What the asset part ensures with a glTF, from the start: this file is linked by
 * wgf_mesh_create, so glTF loading in the program is enough, before its first create. */
WGF_CORE_PRIV_ON_LINK(register_gltf_listers)
{
    wgf_core_priv_load_set_lister(".gltf", list_gltf_dependencies);
    wgf_core_priv_load_set_lister(".glb", list_gltf_dependencies);
    wgf_gfx_priv_model_file_install(); /* a file's models, and a scene's model path= */
}

/* Whether `path` names a .gltf or a .glb, in any case, before a "?" or "#" it ends with. */
static bool is_gltf_path(const char *path)
{
    static const char *const extensions[] = {".gltf", ".glb"};
    const size_t length = strcspn(path, "?#");
    size_t e, k;
    for (e = 0; e < sizeof(extensions) / sizeof(extensions[0]); e++) {
        const size_t n = strlen(extensions[e]);
        bool same = length > n;
        for (k = 0; same && k < n; k++) {
            same = tolower((unsigned char)path[length - n + k]) == extensions[e][k];
        }
        if (same) return true;
    }
    return false;
}

wgf_mesh_t wgf_mesh_create(const char *path)
{
    wgf_mesh_t mesh;
    wgf_gfx_priv_mesh_record_t *record;
    if (path == NULL || !is_gltf_path(path)) {
        wgf_log_error("wgf_mesh_create: \"%s\" isn't a glTF file's path (.gltf or .glb)", path != NULL ? path : "");
        return 0;
    }
    if (!wgf_gfx_priv_mesh_ensure_pool()) return 0;
    wgf_gfx_priv_mesh_set_loader(&loader); /* this object's: what a mesh from a path loads through */
    mesh = wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_MESH, path);
    record = wgf_gfx_priv_mesh_record(mesh);
    if (record != NULL) record->from_file = true;
    return mesh;
}
