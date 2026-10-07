#include "wgf_mesh.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mesh/wgf_gfx_mesh_priv.h"
#include "mesh/wgf_gfx_mesh_shapes_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"

/* Meshes, libwgt's generated half: primitives made on the CPU, uploaded to the GPU the
 * first time they are drawn (so one can be made before gfx starts), reference counted,
 * and shared by their parameters, as wgrender's are: the same shape asked for twice is
 * one mesh. Vertices are kept on the CPU after they're uploaded, as libwgt keeps them
 * for picking (milestone 2.5's). A part, installed by the first mesh made; its stop at
 * gfx's frees every mesh, before the materials they hold. */

#define KEY_MAX 64 /* a generated mesh's: its shape and its parameters' bits */

typedef struct primitive_t {
    float *vertices;
    uint32_t *indices;
    int vertex_count, index_count;
    int material; /* its slot in the mesh's materials */
    wgf_vec3_t bounds_min, bounds_max;
    sg_buffer vertex_buffer, index_buffer; /* SG_INVALID_ID until uploaded */
} primitive_t;

typedef struct mesh_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, references) */
    char key[KEY_MAX];                 /* the parameters it was made with, which find it again */
    primitive_t *primitives;
    int primitive_count;
    wgf_material_t *materials; /* one a slot, referenced */
    int material_count;
    wgf_vec3_t bounds_min, bounds_max;
} mesh_t;

static bool pool_ready;
static wgf_core_priv_handle_pool_t pool;
static mesh_t *meshes;

static mesh_t *record_of(wgf_mesh_t mesh)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve(&pool, mesh, &index)) return NULL;
    return &meshes[index];
}

/* What a mesh holds: its primitives (CPU and GPU), and its materials. */
static void free_mesh(wgf_handle_t mesh, void *record)
{
    mesh_t *mesh_ptr = (mesh_t *)record;
    int i;
    (void)mesh;
    for (i = 0; i < mesh_ptr->primitive_count; i++) {
        primitive_t *primitive = &mesh_ptr->primitives[i];
        if (primitive->vertex_buffer.id != SG_INVALID_ID) sg_destroy_buffer(primitive->vertex_buffer);
        if (primitive->index_buffer.id != SG_INVALID_ID) sg_destroy_buffer(primitive->index_buffer);
        free(primitive->vertices);
        free(primitive->indices);
    }
    free(mesh_ptr->primitives);
    for (i = 0; i < mesh_ptr->material_count; i++) wgf_resource_release(mesh_ptr->materials[i]);
    free(mesh_ptr->materials);
    mesh_ptr->primitives = NULL;
    mesh_ptr->materials = NULL;
    mesh_ptr->primitive_count = mesh_ptr->material_count = 0;
}

static const wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_mesh_create", .free = free_mesh};

static void stop(void)
{
    if (!pool_ready) return;
    wgf_core_priv_resource_unregister(&pool);
    wgf_core_priv_handle_pool_destroy(&pool);
    meshes = NULL;
    pool_ready = false;
}

static wgf_core_priv_part_t part = {.name = "meshes",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_MESHES,
                                    .stop = stop};

/* Tangents for normal mapping from triangle positions and texture coordinates
 * (per-vertex average, orthogonalized against the normal), in glTF's convention: the
 * tangent follows increasing u, and w is the bitangent sign, with bitangent =
 * cross(normal, tangent.xyz) * w pointing toward decreasing v (texture up). Vertices
 * without a usable texture mapping get any tangent perpendicular to their normal.
 * libwgt's (wgrender's wgri_model_generate_tangents). */
void wgf_gfx_priv_mesh_generate_tangents(const float *positions, const float *normals, const float *uvs,
                                        int vertex_count, const uint32_t *indices, int index_count, float *tangents)
{
    float *bitangents = (float *)calloc((size_t)vertex_count * 3, sizeof(float));
    int k, i, c, v;

    memset(tangents, 0, (size_t)vertex_count * 4 * sizeof(float));
    for (k = 0; bitangents != NULL && k + 2 < index_count; k += 3) {
        const uint32_t i0 = indices[k], i1 = indices[k + 1], i2 = indices[k + 2];
        float e1[3], e2[3], sdir[3], tdir[3], du1, dv1, du2, dv2, det;
        if ((int)i0 >= vertex_count || (int)i1 >= vertex_count || (int)i2 >= vertex_count) continue;
        for (c = 0; c < 3; c++) {
            e1[c] = positions[i1 * 3 + c] - positions[i0 * 3 + c];
            e2[c] = positions[i2 * 3 + c] - positions[i0 * 3 + c];
        }
        du1 = uvs[i1 * 2] - uvs[i0 * 2];
        dv1 = uvs[i1 * 2 + 1] - uvs[i0 * 2 + 1];
        du2 = uvs[i2 * 2] - uvs[i0 * 2];
        dv2 = uvs[i2 * 2 + 1] - uvs[i0 * 2 + 1];
        det = du1 * dv2 - du2 * dv1;
        if (det > -1e-12f && det < 1e-12f) continue;
        det = 1.0f / det;
        for (c = 0; c < 3; c++) {
            sdir[c] = (e1[c] * dv2 - e2[c] * dv1) * det;
            tdir[c] = (e2[c] * du1 - e1[c] * du2) * det;
        }
        for (v = 0; v < 3; v++) {
            const uint32_t at = indices[k + v];
            for (c = 0; c < 3; c++) {
                tangents[at * 4 + c] += sdir[c];
                bitangents[at * 3 + c] += tdir[c];
            }
        }
    }
    for (i = 0; i < vertex_count; i++) {
        const wgf_vec3_t n = wgf_vec3_make(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
        wgf_vec3_t t = wgf_vec3_make(tangents[i * 4], tangents[i * 4 + 1], tangents[i * 4 + 2]);
        const wgf_vec3_t b = bitangents != NULL
                                 ? wgf_vec3_make(bitangents[i * 3], bitangents[i * 3 + 1], bitangents[i * 3 + 2])
                                 : wgf_vec3_make(0, 0, 0);
        float w = 1.0f;
        t = wgf_vec3_sub(t, wgf_vec3_scale(n, wgf_vec3_dot(n, t)));
        if (wgf_vec3_dot(t, t) < 1e-12f) {
            /* no texture mapping here: any direction perpendicular to the normal */
            t = fabsf(n.x) < 0.9f ? wgf_vec3_make(1, 0, 0) : wgf_vec3_make(0, 1, 0);
            t = wgf_vec3_sub(t, wgf_vec3_scale(n, wgf_vec3_dot(n, t)));
        } else if (wgf_vec3_dot(wgf_vec3_cross(n, t), b) > 0.0f) {
            w = -1.0f; /* the normal map's up (green) points toward decreasing v: bitangent = -dP/dv */
        }
        t = wgf_vec3_normalize(t);
        tangents[i * 4] = t.x;
        tangents[i * 4 + 1] = t.y;
        tangents[i * 4 + 2] = t.z;
        tangents[i * 4 + 3] = w;
    }
    free(bitangents);
}

/* The generated mesh made with `key`, shared (one more reference), or 0. */
static wgf_mesh_t find_mesh(const char *key)
{
    uint16_t i;
    if (!pool_ready) return 0;
    for (i = 1; i < pool.capacity; i++) {
        const wgf_mesh_t mesh = wgf_core_priv_handle_pool_handle_from_index(&pool, i);
        if (mesh != 0 && strcmp(meshes[i].key, key) == 0) {
            wgf_core_priv_resource_retain(mesh);
            return mesh;
        }
    }
    return 0;
}

/* A mesh of generated geometry, under `key`: one primitive in the vertex layout every
 * shader takes, with generated tangents, both texture coordinate sets the same, white
 * vertex colors, and its bounds, and one material: white, not metallic, roughness 0.5.
 * Takes the shape's arrays. libwgt's (wgrender's create_generated). */
static wgf_mesh_t create_generated(const char *key, wgf_gfx_priv_mesh_shape_t *shape)
{
    const int count = shape->vertex_count;
    const size_t stride = WGF_GFX_PRIV_MESH_VERTEX_FLOATS;
    float *tangents = (float *)calloc((size_t)count * 4, sizeof(float));
    float *vertices = (float *)calloc((size_t)count * stride, sizeof(float));
    primitive_t *primitive = (primitive_t *)calloc(1, sizeof(primitive_t));
    wgf_material_t *materials = (wgf_material_t *)calloc(1, sizeof(wgf_material_t));
    wgf_vec3_t lo = wgf_vec3_make(1e30f, 1e30f, 1e30f), hi = wgf_vec3_make(-1e30f, -1e30f, -1e30f);
    wgf_mesh_t handle = 0;
    mesh_t *mesh_ptr = NULL;
    int i;

    if (tangents != NULL && vertices != NULL && primitive != NULL && materials != NULL) {
        if (!pool_ready) {
            pool_ready = wgf_core_priv_handle_pool_init(&pool, WGF_CORE_PRIV_HANDLE_KIND_MESH, (void **)&meshes,
                                                        sizeof(mesh_t), 16, 65535);
            if (pool_ready) wgf_core_priv_resource_register(&pool, &resource_kind);
        }
        wgf_core_priv_part_install(&part);
        handle = pool_ready ? wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_MESH) : 0;
        mesh_ptr = record_of(handle);
    }
    if (mesh_ptr == NULL) {
        wgf_log_error("wgf_gfx_mesh: out of memory, or no room for another mesh");
        free(tangents);
        free(vertices);
        free(primitive);
        free(materials);
        wgf_gfx_priv_mesh_shape_free(shape);
        return 0;
    }
    wgf_gfx_priv_mesh_generate_tangents(shape->positions, shape->normals, shape->uvs, count, shape->indices,
                                        shape->index_count, tangents);
    for (i = 0; i < count; i++) {
        const float *p = &shape->positions[i * 3];
        float *v = &vertices[(size_t)i * stride];
        memcpy(v, p, 3 * sizeof(float));
        memcpy(v + 3, &shape->normals[i * 3], 3 * sizeof(float));
        memcpy(v + 6, &shape->uvs[i * 2], 2 * sizeof(float));
        memcpy(v + 8, &shape->uvs[i * 2], 2 * sizeof(float)); /* both texture coordinate sets */
        memcpy(v + 10, &tangents[i * 4], 4 * sizeof(float));
        v[14] = v[15] = v[16] = v[17] = 1.0f; /* white */
        lo = wgf_vec3_make(fminf(lo.x, p[0]), fminf(lo.y, p[1]), fminf(lo.z, p[2]));
        hi = wgf_vec3_make(fmaxf(hi.x, p[0]), fmaxf(hi.y, p[1]), fmaxf(hi.z, p[2]));
    }
    free(tangents);
    snprintf(mesh_ptr->key, sizeof(mesh_ptr->key), "%s", key);
    primitive->vertices = vertices;
    primitive->vertex_count = count;
    primitive->indices = shape->indices; /* the mesh's now */
    primitive->index_count = shape->index_count;
    primitive->bounds_min = lo;
    primitive->bounds_max = hi;
    mesh_ptr->primitives = primitive;
    mesh_ptr->primitive_count = 1;
    mesh_ptr->bounds_min = lo;
    mesh_ptr->bounds_max = hi;
    materials[0] = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_float(materials[0], "metallic", 0.0f);
    wgf_material_set_float(materials[0], "roughness", 0.5f);
    mesh_ptr->materials = materials;
    mesh_ptr->material_count = materials[0] != 0 ? 1 : 0;
    shape->indices = NULL;
    wgf_gfx_priv_mesh_shape_free(shape);
    return handle;
}

/* A float's bits, in a key: the same sizes are the same key, and no float formatting is linked. */
static unsigned bits_of(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return (unsigned)bits;
}

/* The mesh `key` names, shared, or made by `made` (false: a size of 0 or less). */
static wgf_mesh_t generate(const char *key, bool made, wgf_gfx_priv_mesh_shape_t *shape, const char *call)
{
    const wgf_mesh_t existing = find_mesh(key);
    if (existing != 0) {
        if (made) wgf_gfx_priv_mesh_shape_free(shape);
        return existing;
    }
    if (!made) {
        wgf_log_error("%s: its sizes are more than 0 (or it ran out of memory)", call);
        return 0;
    }
    return create_generated(key, shape);
}

static int clamp_count(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

wgf_mesh_t wgf_mesh_create_plane(float width, float length, int subdivisions)
{
    char key[KEY_MAX];
    wgf_gfx_priv_mesh_shape_t shape;
    subdivisions = clamp_count(subdivisions, 0, WGF_GFX_PRIV_MESH_MAX_SUBDIVISIONS);
    snprintf(key, sizeof(key), "plane %x %x %d", bits_of(width), bits_of(length), subdivisions);
    return generate(key, wgf_gfx_priv_mesh_shape_plane(width, length, subdivisions, &shape), &shape,
                    "wgf_mesh_create_plane");
}

wgf_mesh_t wgf_mesh_create_cube(float width, float height, float length)
{
    char key[KEY_MAX];
    wgf_gfx_priv_mesh_shape_t shape;
    snprintf(key, sizeof(key), "cube %x %x %x", bits_of(width), bits_of(height), bits_of(length));
    return generate(key, wgf_gfx_priv_mesh_shape_cube(width, height, length, &shape), &shape, "wgf_mesh_create_cube");
}

wgf_mesh_t wgf_mesh_create_sphere(float radius, int rings, int segments)
{
    char key[KEY_MAX];
    wgf_gfx_priv_mesh_shape_t shape;
    rings = clamp_count(rings, WGF_GFX_PRIV_MESH_MIN_RINGS, WGF_GFX_PRIV_MESH_MAX_RINGS);
    segments = clamp_count(segments, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    snprintf(key, sizeof(key), "sphere %x %d %d", bits_of(radius), rings, segments);
    return generate(key, wgf_gfx_priv_mesh_shape_sphere(radius, rings, segments, &shape), &shape,
                    "wgf_mesh_create_sphere");
}

wgf_mesh_t wgf_mesh_create_cylinder(float radius, float height, int segments)
{
    char key[KEY_MAX];
    wgf_gfx_priv_mesh_shape_t shape;
    segments = clamp_count(segments, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    snprintf(key, sizeof(key), "cylinder %x %x %d", bits_of(radius), bits_of(height), segments);
    return generate(key, wgf_gfx_priv_mesh_shape_cylinder(radius, height, segments, &shape), &shape,
                    "wgf_mesh_create_cylinder");
}

wgf_mesh_t wgf_mesh_create_cone(float radius, float height, int segments)
{
    char key[KEY_MAX];
    wgf_gfx_priv_mesh_shape_t shape;
    segments = clamp_count(segments, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    snprintf(key, sizeof(key), "cone %x %x %d", bits_of(radius), bits_of(height), segments);
    return generate(key, wgf_gfx_priv_mesh_shape_cone(radius, height, segments, &shape), &shape,
                    "wgf_mesh_create_cone");
}

wgf_mesh_t wgf_mesh_create_capsule(float radius, float height, int rings, int segments)
{
    char key[KEY_MAX];
    wgf_gfx_priv_mesh_shape_t shape;
    rings = clamp_count(rings, WGF_GFX_PRIV_MESH_MIN_RINGS, WGF_GFX_PRIV_MESH_MAX_RINGS);
    segments = clamp_count(segments, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    snprintf(key, sizeof(key), "capsule %x %x %d %d", bits_of(radius), bits_of(height), rings, segments);
    return generate(key, wgf_gfx_priv_mesh_shape_capsule(radius, height, rings, segments, &shape), &shape,
                    "wgf_mesh_create_capsule");
}

wgf_mesh_t wgf_mesh_create_torus(float radius, float thickness, int rings, int segments)
{
    char key[KEY_MAX];
    wgf_gfx_priv_mesh_shape_t shape;
    rings = clamp_count(rings, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    segments = clamp_count(segments, WGF_GFX_PRIV_MESH_MIN_SEGMENTS, WGF_GFX_PRIV_MESH_MAX_SEGMENTS);
    snprintf(key, sizeof(key), "torus %x %x %d %d", bits_of(radius), bits_of(thickness), rings, segments);
    return generate(key, wgf_gfx_priv_mesh_shape_torus(radius, thickness, rings, segments, &shape), &shape,
                    "wgf_mesh_create_torus");
}

int wgf_mesh_get_material_count(wgf_mesh_t mesh)
{
    const mesh_t *mesh_ptr = record_of(mesh);
    return mesh_ptr != NULL ? mesh_ptr->material_count : 0;
}

wgf_material_t wgf_mesh_get_material(wgf_mesh_t mesh, int slot)
{
    const mesh_t *mesh_ptr = record_of(mesh);
    return mesh_ptr != NULL && slot >= 0 && slot < mesh_ptr->material_count ? mesh_ptr->materials[slot] : 0;
}

/* ------------------------------------------------------------- internal ---- */

bool wgf_gfx_priv_mesh_retain(wgf_mesh_t mesh)
{
    if (record_of(mesh) == NULL) return false;
    wgf_core_priv_resource_retain(mesh);
    return true;
}

bool wgf_gfx_priv_mesh_get_bounds(wgf_mesh_t mesh, wgf_vec3_t *min, wgf_vec3_t *max)
{
    const mesh_t *mesh_ptr = record_of(mesh);
    if (mesh_ptr == NULL || mesh_ptr->resource.status != WGF_RESOURCE_STATUS_READY) return false;
    *min = mesh_ptr->bounds_min;
    *max = mesh_ptr->bounds_max;
    return true;
}

int wgf_gfx_priv_mesh_get_primitive_count(wgf_mesh_t mesh)
{
    const mesh_t *mesh_ptr = record_of(mesh);
    return mesh_ptr != NULL && mesh_ptr->resource.status == WGF_RESOURCE_STATUS_READY ? mesh_ptr->primitive_count : 0;
}

/* The primitive's buffers made; false (logged) when the GPU didn't take them. */
static bool upload(primitive_t *primitive)
{
    sg_buffer_desc desc;
    if (primitive->vertex_buffer.id != SG_INVALID_ID) return true;
    memset(&desc, 0, sizeof(desc));
    desc.usage.vertex_buffer = true;
    desc.data.ptr = primitive->vertices;
    desc.data.size = sizeof(float) * WGF_GFX_PRIV_MESH_VERTEX_FLOATS * (size_t)primitive->vertex_count;
    primitive->vertex_buffer = sg_make_buffer(&desc);
    memset(&desc, 0, sizeof(desc));
    desc.usage.index_buffer = true;
    desc.data.ptr = primitive->indices;
    desc.data.size = sizeof(uint32_t) * (size_t)primitive->index_count;
    primitive->index_buffer = sg_make_buffer(&desc);
    if (sg_query_buffer_state(primitive->vertex_buffer) != SG_RESOURCESTATE_VALID ||
        sg_query_buffer_state(primitive->index_buffer) != SG_RESOURCESTATE_VALID) {
        wgf_log_error("wgf_gfx_mesh: the GPU didn't take a mesh's buffers");
        return false;
    }
    return true;
}

bool wgf_gfx_priv_mesh_get_primitive(wgf_mesh_t mesh, int index, wgf_gfx_priv_mesh_primitive_t *primitive)
{
    mesh_t *mesh_ptr = record_of(mesh);
    primitive_t *p;
    if (index < 0 || index >= wgf_gfx_priv_mesh_get_primitive_count(mesh) || !wgf_gfx_priv_render_is_running()) {
        return false;
    }
    p = &mesh_ptr->primitives[index];
    if (!upload(p)) return false;
    primitive->vertices = p->vertex_buffer;
    primitive->indices = p->index_buffer;
    primitive->index_count = p->index_count;
    primitive->material = p->material;
    primitive->bounds_min = p->bounds_min;
    primitive->bounds_max = p->bounds_max;
    return true;
}

const float *wgf_gfx_priv_mesh_get_vertices(wgf_mesh_t mesh, int *vertex_count)
{
    const mesh_t *mesh_ptr = record_of(mesh);
    const bool has = mesh_ptr != NULL && mesh_ptr->primitive_count > 0;
    *vertex_count = has ? mesh_ptr->primitives[0].vertex_count : 0;
    return has ? mesh_ptr->primitives[0].vertices : NULL;
}

const uint32_t *wgf_gfx_priv_mesh_get_indices(wgf_mesh_t mesh, int *index_count)
{
    const mesh_t *mesh_ptr = record_of(mesh);
    const bool has = mesh_ptr != NULL && mesh_ptr->primitive_count > 0;
    *index_count = has ? mesh_ptr->primitives[0].index_count : 0;
    return has ? mesh_ptr->primitives[0].indices : NULL;
}
