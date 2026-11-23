#include "wgf_mesh.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "material/wgf_gfx_material_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "mesh/wgf_gfx_mesh_record_priv.h"
#include "stage/wgf_gfx_model_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"
#include "wgf_mat4.h"
#include "wgf_resource.h"

/* Meshes merged (wgf_mesh_create_merged): helix-raylib's LoadModelFromModels, in libwgf's
 * terms. The merged mesh waits, PENDING, for its pieces (a part's update looks each frame),
 * then is made on the CPU in one go: each piece's primitives, placed or bound to the
 * skeleton's joints, appended to a group of what one material draws, a group one primitive.
 * With a skeleton the mesh is a file's, its tree the skeleton's nodes, so a model of it is a
 * file's root (its joints actors, its animations, sockets, and set_animations by name); with
 * none it is a plain mesh of its groups. Linked only by a program that calls it. */

typedef wgf_gfx_priv_mesh_record_primitive_t primitive_t;
typedef wgf_gfx_priv_mesh_record_t mesh_t;
typedef wgf_gfx_priv_mesh_skin_t skin_t;

enum { VERTEX = WGF_GFX_PRIV_MESH_VERTEX_FLOATS, SKIN = WGF_GFX_PRIV_MESH_SKIN_FLOATS };
enum { MAX_PIECES = 64 };

/* A merge waiting for its pieces: each held (a reference) until it is made. */
typedef struct pending_t {
    wgf_mesh_t merged;
    wgf_mesh_t pieces[MAX_PIECES];
    int piece_count;
    wgf_mesh_t skeleton; /* among the pieces' references, or one of its own; 0 for none */
} pending_t;

static pending_t *pending;
static int pending_count, pending_capacity;

/* What one material draws, the merged mesh's one primitive: its slot's material (a white copy
 * when its members' base colors differ, the colors then in the vertices), skinned or placed
 * where the file put it, and its triangles as they are appended. */
typedef struct group_t {
    wgf_material_t material; /* its first member's; the slot's is `slot_material` */
    wgf_material_t slot_material;
    float color[4];          /* the first member's base color */
    bool differs;            /* a member's base color isn't the first's: baked into the vertices */
    bool skinned;
    float *vertices, *skin;
    uint32_t *indices;
    int vertex_count, index_count, vertex_capacity, index_capacity;
} group_t;

/* How one piece's primitive is brought in: placed by `place` (no skin, or a skin dropped), as
 * a rigid part of joint `joint` placed by `place` (unskinned, following a joint), or bound to
 * the skeleton through `joint_map` (each of its skin's joints' in the merged skin, -1 none),
 * each joint's vertices moved by `rebind` (NULL: none moves). */
enum { BRING_PLACED, BRING_RIGID, BRING_SKINNED };
typedef struct bring_t {
    const primitive_t *primitive;
    int group;
    int how;
    wgf_mat4_t place;
    int joint;
    const int *joint_map;
    const wgf_mat4_t *rebind;
    float color[4];
} bring_t;

/* --- the materials ------------------------------------------------------------------- */

static bool same_texture(const wgf_gfx_priv_material_texture_t *a, const wgf_gfx_priv_material_texture_t *b)
{
    return a->texture == b->texture && a->texcoord == b->texcoord && a->offset[0] == b->offset[0] &&
           a->offset[1] == b->offset[1] && a->rotation == b->rotation && a->scale[0] == b->scale[0] &&
           a->scale[1] == b->scale[1] && a->wrap_u == b->wrap_u && a->wrap_v == b->wrap_v && a->filter == b->filter &&
           a->mipmaps == b->mipmaps && a->cube == b->cube;
}

/* Whether two materials draw alike but for their base color: a custom one only like itself. */
static bool alike_but_color(wgf_material_t a, wgf_material_t b)
{
    const wgf_gfx_priv_material_t *x = wgf_gfx_priv_material_get(a), *y = wgf_gfx_priv_material_get(b);
    int i;
    if (a == b) return true;
    if (x == NULL || y == NULL || x->custom != NULL || y->custom != NULL) return false;
    if (x->shading != y->shading || x->alpha_mode != y->alpha_mode || x->alpha_cutoff != y->alpha_cutoff ||
        x->double_sided != y->double_sided || x->metallic != y->metallic || x->roughness != y->roughness ||
        x->normal_scale != y->normal_scale || x->occlusion_strength != y->occlusion_strength ||
        x->emissive[0] != y->emissive[0] || x->emissive[1] != y->emissive[1] || x->emissive[2] != y->emissive[2]) {
        return false;
    }
    for (i = 0; i < WGF_GFX_PRIV_MATERIAL_TEXTURE_COUNT; i++) {
        if (!same_texture(&x->textures[i], &y->textures[i])) return false;
    }
    return true;
}

static void base_color_of(wgf_material_t material, float out[4])
{
    const wgf_gfx_priv_material_t *m = wgf_gfx_priv_material_get(material);
    int i;
    for (i = 0; i < 4; i++) out[i] = m != NULL && m->custom == NULL ? m->base_color[i] : 1.0f;
}

/* The group drawing `material` (skinned or not), made when there is none yet; -1 out of memory. */
static int group_for(group_t **groups, int *count, int *capacity, wgf_material_t material, bool skinned)
{
    float color[4];
    int g;
    base_color_of(material, color);
    for (g = 0; g < *count; g++) {
        group_t *group = &(*groups)[g];
        if (group->skinned != skinned || !alike_but_color(group->material, material)) continue;
        if (memcmp(group->color, color, sizeof(color)) != 0) group->differs = true;
        return g;
    }
    if (*count == *capacity) {
        const int grown = *capacity > 0 ? *capacity * 2 : 8;
        group_t *more = (group_t *)realloc(*groups, sizeof(group_t) * (size_t)grown);
        if (more == NULL) return -1;
        *groups = more;
        *capacity = grown;
    }
    memset(&(*groups)[*count], 0, sizeof(group_t));
    (*groups)[*count].material = material;
    (*groups)[*count].skinned = skinned;
    memcpy((*groups)[*count].color, color, sizeof(color));
    return (*count)++;
}

/* --- the skeleton ------------------------------------------------------------------- */

/* Each node's place in its file at rest, from the top: parents come before their children. */
static wgf_mat4_t *rest_worlds(const mesh_t *file)
{
    wgf_mat4_t *worlds = (wgf_mat4_t *)malloc(sizeof(wgf_mat4_t) * (size_t)(file->node_count > 0 ? file->node_count : 1));
    int n;
    if (worlds == NULL) return NULL;
    for (n = 0; n < file->node_count; n++) {
        const wgf_gfx_priv_mesh_node_t *node = &file->nodes[n];
        const wgf_mat4_t local = wgf_mat4_from_trs(node->position, node->rotation, node->scale);
        worlds[n] = node->parent >= 0 && node->parent < n ? wgf_mat4_mul(worlds[node->parent], local) : local;
    }
    return worlds;
}

static int find_named(const mesh_t *file, const char *name)
{
    int n;
    if (name == NULL || name[0] == '\0') return -1;
    for (n = 0; n < file->node_count; n++) {
        if (file->nodes[n].name != NULL && strcmp(file->nodes[n].name, name) == 0) return n;
    }
    return -1;
}

static int joint_of(const skin_t *skin, int node)
{
    int j;
    for (j = 0; j < skin->joint_count; j++) {
        if (skin->joints[j] == node) return j;
    }
    return -1;
}

/* The merged skin's joint a piece's node follows: the first of it and its ancestors named as
 * one of the skeleton's nodes, that node or its nearest ancestor a joint of the skin; -1 none.
 * The piece's node matched, into `matched`. */
static int follow(const mesh_t *piece, int node, const mesh_t *skeleton, const skin_t *skin, int *matched)
{
    int at, steps = 0;
    for (at = node; at >= 0 && at < piece->node_count && steps++ <= piece->node_count; at = piece->nodes[at].parent) {
        int s = find_named(skeleton, piece->nodes[at].name), up = 0;
        if (s < 0) continue;
        for (; s >= 0 && s < skeleton->node_count && up++ <= skeleton->node_count; s = skeleton->nodes[s].parent) {
            const int j = joint_of(skin, s);
            if (j >= 0) {
                *matched = at;
                return j;
            }
        }
    }
    return -1;
}

static bool near_identity(wgf_mat4_t m)
{
    const wgf_mat4_t identity = wgf_mat4_identity();
    int i;
    for (i = 0; i < 16; i++) {
        if (fabsf(m.m[i] - identity.m[i]) > 1e-4f) return false;
    }
    return true;
}

/* --- the triangles ------------------------------------------------------------------- */

static bool reserve(group_t *group, int vertices, int indices)
{
    if (group->vertex_count + vertices > group->vertex_capacity) {
        int grown = group->vertex_capacity > 0 ? group->vertex_capacity : 256;
        float *v, *s;
        while (grown < group->vertex_count + vertices) grown *= 2;
        v = (float *)realloc(group->vertices, sizeof(float) * VERTEX * (size_t)grown);
        if (v == NULL) return false;
        group->vertices = v;
        if (group->skinned) {
            s = (float *)realloc(group->skin, sizeof(float) * SKIN * (size_t)grown);
            if (s == NULL) return false;
            group->skin = s;
        }
        group->vertex_capacity = grown;
    }
    if (group->index_count + indices > group->index_capacity) {
        int grown = group->index_capacity > 0 ? group->index_capacity : 768;
        uint32_t *i;
        while (grown < group->index_count + indices) grown *= 2;
        i = (uint32_t *)realloc(group->indices, sizeof(uint32_t) * (size_t)grown);
        if (i == NULL) return false;
        group->indices = i;
        group->index_capacity = grown;
    }
    return true;
}

static void set3(float *at, wgf_vec3_t v)
{
    at[0] = v.x;
    at[1] = v.y;
    at[2] = v.z;
}

static wgf_vec3_t unit(wgf_vec3_t v)
{
    const float length = wgf_vec3_length(v);
    return length > 0.0f ? wgf_vec3_scale(v, 1.0f / length) : v;
}

/* A vertex moved by `m`: its place, its normal by m's inverse transposed, its tangent by m. */
static void move_vertex(float *v, wgf_mat4_t m)
{
    const wgf_mat4_t normals = wgf_mat4_transpose(wgf_mat4_invert(m));
    set3(&v[0], wgf_mat4_transform_point(m, wgf_vec3_make(v[0], v[1], v[2])));
    set3(&v[3], unit(wgf_mat4_transform_direction(normals, wgf_vec3_make(v[3], v[4], v[5]))));
    set3(&v[10], unit(wgf_mat4_transform_direction(m, wgf_vec3_make(v[10], v[11], v[12]))));
}

/* One piece's primitive appended to its group: its vertices placed or bound, its color baked
 * when the group's differ, its joints the merged skin's. */
static bool bring(group_t *group, const bring_t *b)
{
    const primitive_t *p = b->primitive;
    const int base = group->vertex_count;
    const bool mirrored = b->how != BRING_SKINNED && wgf_mat4_determinant(b->place) < 0.0f;
    int v, i;
    if (!reserve(group, p->vertex_count, p->index_count)) return false;
    for (v = 0; v < p->vertex_count; v++) {
        float *out = &group->vertices[(size_t)(base + v) * VERTEX];
        memcpy(out, &p->vertices[(size_t)v * VERTEX], sizeof(float) * VERTEX);
        if (group->differs) {
            for (i = 0; i < 4; i++) out[14 + i] *= b->color[i];
        }
        if (b->how != BRING_SKINNED) {
            move_vertex(out, b->place);
            if (group->skinned) { /* rigid, following one joint */
                float *s = &group->skin[(size_t)(base + v) * SKIN];
                memset(s, 0, sizeof(float) * SKIN);
                s[0] = (float)b->joint;
                s[4] = 1.0f;
            }
            continue;
        }
        { /* bound: each joint the merged skin's, a weight to none dropped, the rest made 1 again */
            const float *in = p->skin != NULL ? &p->skin[(size_t)v * SKIN] : NULL;
            float *s = &group->skin[(size_t)(base + v) * SKIN];
            wgf_mat4_t blend;
            float total = 0.0f, moved = 0.0f;
            int k;
            memset(s, 0, sizeof(float) * SKIN);
            memset(&blend, 0, sizeof(blend));
            for (k = 0; in != NULL && k < 4; k++) {
                const int joint = (int)in[k];
                const float weight = in[4 + k];
                const int to = weight > 0.0f && joint >= 0 && b->joint_map != NULL ? b->joint_map[joint] : -1;
                if (to < 0) continue;
                s[k] = (float)to;
                s[4 + k] = weight;
                total += weight;
                if (b->rebind != NULL) {
                    for (i = 0; i < 16; i++) blend.m[i] += b->rebind[joint].m[i] * weight;
                    moved += weight;
                }
            }
            if (total <= 0.0f) { /* follows none of the skeleton's joints: its first */
                memset(s, 0, sizeof(float) * SKIN);
                s[4] = 1.0f;
                continue;
            }
            for (k = 0; k < 4; k++) s[4 + k] /= total;
            if (moved > 0.0f) {
                for (i = 0; i < 16; i++) blend.m[i] /= moved;
                move_vertex(out, blend);
            }
        }
    }
    for (i = 0; i < p->index_count; i++) group->indices[group->index_count + i] = base + p->indices[i];
    if (mirrored) { /* a mirroring placement turns its triangles: their winding turned back */
        for (i = 0; i + 2 < p->index_count; i += 3) {
            const uint32_t keep = group->indices[group->index_count + i + 1];
            group->indices[group->index_count + i + 1] = group->indices[group->index_count + i + 2];
            group->indices[group->index_count + i + 2] = keep;
        }
    }
    group->vertex_count += p->vertex_count;
    group->index_count += p->index_count;
    return true;
}

/* --- making it ----------------------------------------------------------------------- */

static char *copy_text(const char *text)
{
    const size_t length = text != NULL ? strlen(text) : 0;
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) {
        if (length > 0) memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

/* The skeleton's tree, skin, and animations, copied into `out` (its nodes showing nothing),
 * with `extra` nodes more at the top for the merged meshes; false out of memory. */
static bool copy_skeleton(const mesh_t *skeleton, int extra, mesh_t *out)
{
    const skin_t *from = &skeleton->skins[0];
    skin_t *skin;
    int n, a, c, count = from->joint_count;
    out->nodes = (wgf_gfx_priv_mesh_node_t *)calloc((size_t)(skeleton->node_count + extra), sizeof(*out->nodes));
    out->skins = (skin_t *)calloc(1, sizeof(skin_t));
    if (out->nodes == NULL || out->skins == NULL) return false;
    out->skin_count = 1;
    for (n = 0; n < skeleton->node_count; n++) {
        out->nodes[n] = skeleton->nodes[n];
        out->nodes[n].name = copy_text(skeleton->nodes[n].name);
        out->nodes[n].mesh = out->nodes[n].light = out->nodes[n].skin = -1;
        out->node_count = n + 1;
        if (out->nodes[n].name == NULL) return false;
    }
    skin = &out->skins[0];
    skin->joints = (int *)calloc((size_t)count + 1, sizeof(int));
    skin->inverse_bind = (wgf_mat4_t *)calloc((size_t)count + 1, sizeof(wgf_mat4_t));
    skin->centers = (wgf_vec3_t *)calloc((size_t)count + 1, sizeof(wgf_vec3_t));
    skin->radii = (float *)calloc((size_t)count + 1, sizeof(float));
    if (skin->joints == NULL || skin->inverse_bind == NULL || skin->centers == NULL || skin->radii == NULL) return false;
    skin->joint_count = count;
    memcpy(skin->joints, from->joints, sizeof(int) * (size_t)count);
    memcpy(skin->inverse_bind, from->inverse_bind, sizeof(wgf_mat4_t) * (size_t)count);
    memcpy(skin->centers, from->centers, sizeof(wgf_vec3_t) * (size_t)count);
    for (c = 0; c < count; c++) skin->radii[c] = -1.0f; /* measured again over the merged vertices */
    if (skeleton->animation_count > 0) {
        out->animations = (wgf_gfx_priv_mesh_animation_t *)calloc((size_t)skeleton->animation_count,
                                                                    sizeof(*out->animations));
        if (out->animations == NULL) return false;
        for (a = 0; a < skeleton->animation_count; a++) {
            const wgf_gfx_priv_mesh_animation_t *src = &skeleton->animations[a];
            wgf_gfx_priv_mesh_animation_t *dst = &out->animations[a];
            out->animation_count = a + 1;
            dst->duration = src->duration;
            dst->name = copy_text(src->name);
            dst->channels = (wgf_gfx_priv_mesh_channel_t *)calloc((size_t)(src->channel_count > 0 ? src->channel_count : 1),
                                                                   sizeof(*dst->channels));
            if (dst->name == NULL || dst->channels == NULL) return false;
            for (c = 0; c < src->channel_count; c++) {
                const wgf_gfx_priv_mesh_channel_t *sc = &src->channels[c];
                wgf_gfx_priv_mesh_channel_t *dc = &dst->channels[c];
                const int floats = sc->key_count * (sc->path == WGF_GFX_PRIV_MESH_PATH_ROTATION ? 4 : 3) *
                                   (sc->interpolation == WGF_GFX_PRIV_MESH_LERP_CUBIC ? 3 : 1);
                *dc = *sc;
                dc->times = (float *)malloc(sizeof(float) * (size_t)(sc->key_count > 0 ? sc->key_count : 1));
                dc->values = (float *)malloc(sizeof(float) * (size_t)(floats > 0 ? floats : 1));
                dst->channel_count = c + 1;
                if (dc->times == NULL || dc->values == NULL) return false;
                memcpy(dc->times, sc->times, sizeof(float) * (size_t)sc->key_count);
                memcpy(dc->values, sc->values, sizeof(float) * (size_t)floats);
            }
        }
    }
    if (skeleton->animated != NULL) {
        out->animated = (unsigned char *)calloc((size_t)(skeleton->node_count + extra), 1);
        if (out->animated == NULL) return false;
        memcpy(out->animated, skeleton->animated, (size_t)skeleton->node_count);
    }
    return true;
}

/* A node more at the top of `out`'s tree, showing part `part` with skin `skin` (-1 none). */
static bool add_top_node(mesh_t *out, const char *name, int part, int skin)
{
    wgf_gfx_priv_mesh_node_t *node = &out->nodes[out->node_count];
    memset(node, 0, sizeof(*node));
    node->name = copy_text(name);
    if (node->name == NULL) return false;
    node->parent = -1;
    node->rotation = wgf_quat_identity();
    node->scale = wgf_vec3_make(1.0f, 1.0f, 1.0f);
    node->mesh = part;
    node->light = -1;
    node->skin = skin;
    out->node_count++;
    return true;
}

static void primitive_bounds(primitive_t *p)
{
    int v;
    p->bounds_min = wgf_vec3_make(1e30f, 1e30f, 1e30f);
    p->bounds_max = wgf_vec3_make(-1e30f, -1e30f, -1e30f);
    for (v = 0; v < p->vertex_count; v++) {
        const float *at = &p->vertices[(size_t)v * VERTEX];
        p->bounds_min = wgf_vec3_make(fminf(p->bounds_min.x, at[0]), fminf(p->bounds_min.y, at[1]),
                                      fminf(p->bounds_min.z, at[2]));
        p->bounds_max = wgf_vec3_make(fmaxf(p->bounds_max.x, at[0]), fmaxf(p->bounds_max.y, at[1]),
                                      fmaxf(p->bounds_max.z, at[2]));
    }
    if (p->vertex_count == 0) p->bounds_min = p->bounds_max = wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

/* The groups of one kind (skinned or not) as primitives, each taking its group's arrays; NULL
 * for none of that kind (`count` 0) or out of memory (`count` -1). */
static primitive_t *primitives_of(group_t *groups, int group_count, bool skinned, int *count)
{
    primitive_t *made = (primitive_t *)calloc((size_t)(group_count > 0 ? group_count : 1), sizeof(primitive_t));
    int g;
    *count = 0;
    if (made == NULL) {
        *count = -1;
        return NULL;
    }
    for (g = 0; g < group_count; g++) {
        group_t *group = &groups[g];
        primitive_t *p;
        if (group->skinned != skinned || group->vertex_count == 0) continue;
        p = &made[(*count)++];
        p->vertices = group->vertices;
        p->indices = group->indices;
        p->skin = group->skin;
        p->vertex_count = group->vertex_count;
        p->index_count = group->index_count;
        p->material = g;
        group->vertices = group->skin = NULL;
        group->indices = NULL;
        primitive_bounds(p);
    }
    if (*count == 0) {
        free(made);
        return NULL;
    }
    return made;
}

/* Each joint's reach over the merged skinned vertices, as a file's skin measures it. */
static void measure_skin(skin_t *skin, const primitive_t *primitives, int count)
{
    int p, v, k;
    for (p = 0; p < count; p++) {
        for (v = 0; v < primitives[p].vertex_count; v++) {
            const float *s = &primitives[p].skin[(size_t)v * SKIN];
            const float *at = &primitives[p].vertices[(size_t)v * VERTEX];
            for (k = 0; k < 4; k++) {
                const int j = (int)s[k];
                float d;
                if (s[4 + k] <= 0.0f || j < 0 || j >= skin->joint_count) continue;
                d = wgf_vec3_length(wgf_vec3_sub(wgf_vec3_make(at[0], at[1], at[2]), skin->centers[j]));
                if (d > skin->radii[j]) skin->radii[j] = d;
            }
        }
    }
}

/* A piece's primitives, a node at a time, planned into groups: how each is brought in. The
 * joint maps and rebinds made are kept in `owned` to free. False out of memory. */
static bool plan_piece(wgf_mesh_t piece, const mesh_t *skeleton, group_t **groups, int *group_count,
                       int *group_capacity, bring_t **brings, int *bring_count, int *bring_capacity, void ***owned,
                       int *owned_count)
{
    const mesh_t *file = wgf_gfx_priv_mesh_record(piece);
    const skin_t *merged_skin = skeleton != NULL ? &skeleton->skins[0] : NULL;
    wgf_mat4_t *worlds = NULL;
    int **maps = NULL;
    wgf_mat4_t **rebinds = NULL;
    int n, p, s;
    bool ok = false;
    if (file == NULL) return false;
    if (file->nodes == NULL) { /* a mesh made in code: its primitives where they are, under a skeleton too */
        const wgf_mat4_t identity = wgf_mat4_identity();
        for (p = 0; p < file->primitive_count; p++) {
            const primitive_t *prim = &file->primitives[p];
            bring_t *b;
            int g;
            if (prim->vertex_count == 0 || prim->material < 0 || prim->material >= file->material_count) continue;
            g = group_for(groups, group_count, group_capacity, file->materials[prim->material], false);
            if (g < 0) return false;
            if (*bring_count == *bring_capacity) {
                const int grown = *bring_capacity > 0 ? *bring_capacity * 2 : 32;
                bring_t *more = (bring_t *)realloc(*brings, sizeof(bring_t) * (size_t)grown);
                if (more == NULL) return false;
                *brings = more;
                *bring_capacity = grown;
            }
            b = &(*brings)[(*bring_count)++];
            memset(b, 0, sizeof(*b));
            b->primitive = prim;
            b->group = g;
            b->how = BRING_PLACED;
            b->place = identity;
            base_color_of(file->materials[prim->material], b->color);
        }
        return true;
    }
    worlds = rest_worlds(file);
    if (worlds == NULL) return false;
    if (skeleton != NULL && file->skin_count > 0) { /* each skin's joints onto the merged skin's */
        maps = (int **)calloc((size_t)file->skin_count, sizeof(int *));
        rebinds = (wgf_mat4_t **)calloc((size_t)file->skin_count, sizeof(wgf_mat4_t *));
        if (maps == NULL || rebinds == NULL) goto done;
        for (s = 0; s < file->skin_count; s++) {
            const skin_t *skin = &file->skins[s];
            bool any = false;
            int j;
            maps[s] = (int *)malloc(sizeof(int) * (size_t)(skin->joint_count + 1));
            rebinds[s] = (wgf_mat4_t *)malloc(sizeof(wgf_mat4_t) * (size_t)(skin->joint_count + 1));
            if (maps[s] == NULL || rebinds[s] == NULL) goto done;
            for (j = 0; j < skin->joint_count; j++) {
                int matched = -1;
                const int to = follow(file, skin->joints[j], skeleton, merged_skin, &matched);
                maps[s][j] = to;
                rebinds[s][j] = wgf_mat4_identity();
                if (to < 0) continue;
                { /* from this piece's bind of the node matched to the skeleton's of its joint */
                    const int own = joint_of(skin, matched);
                    const wgf_mat4_t bind_inverse = own >= 0 ? skin->inverse_bind[own] : wgf_mat4_invert(worlds[matched]);
                    rebinds[s][j] = wgf_mat4_mul(wgf_mat4_invert(merged_skin->inverse_bind[to]), bind_inverse);
                    if (!near_identity(rebinds[s][j])) any = true;
                }
            }
            if (!any) {
                free(rebinds[s]);
                rebinds[s] = NULL;
            }
        }
    }
    for (n = 0; n < file->node_count; n++) {
        const wgf_gfx_priv_mesh_node_t *node = &file->nodes[n];
        const mesh_t *part = node->mesh >= 0 && node->mesh < file->part_count ? wgf_gfx_priv_mesh_record(file->parts[node->mesh])
                                                                              : NULL;
        const bool skinned = skeleton != NULL && maps != NULL && node->skin >= 0 && node->skin < file->skin_count;
        int matched = -1, rigid_joint = -1;
        wgf_mat4_t rigid_place = worlds[n];
        if (part == NULL) continue;
        if (skeleton != NULL && !skinned) { /* rigid: as if parented to the joint its nearest named ancestor is */
            rigid_joint = follow(file, n, skeleton, merged_skin, &matched);
            if (rigid_joint >= 0) {
                rigid_place = wgf_mat4_mul(wgf_mat4_mul(wgf_mat4_invert(merged_skin->inverse_bind[rigid_joint]),
                                                        wgf_mat4_invert(worlds[matched])),
                                           worlds[n]);
            }
        }
        for (p = 0; p < part->primitive_count; p++) {
            const primitive_t *prim = &part->primitives[p];
            const bool bound = skinned && prim->skin != NULL;
            const bool follows = bound || rigid_joint >= 0;
            bring_t *b;
            int g;
            if (prim->vertex_count == 0 || prim->material < 0 || prim->material >= part->material_count) continue;
            g = group_for(groups, group_count, group_capacity, part->materials[prim->material], follows);
            if (g < 0) goto done;
            if (*bring_count == *bring_capacity) {
                const int grown = *bring_capacity > 0 ? *bring_capacity * 2 : 32;
                bring_t *more = (bring_t *)realloc(*brings, sizeof(bring_t) * (size_t)grown);
                if (more == NULL) goto done;
                *brings = more;
                *bring_capacity = grown;
            }
            b = &(*brings)[(*bring_count)++];
            memset(b, 0, sizeof(*b));
            b->primitive = prim;
            b->group = g;
            base_color_of(part->materials[prim->material], b->color);
            if (bound) {
                b->how = BRING_SKINNED;
                b->joint_map = maps[node->skin];
                b->rebind = rebinds[node->skin];
            } else if (rigid_joint >= 0) {
                b->how = BRING_RIGID;
                b->joint = rigid_joint;
                b->place = rigid_place;
            } else { /* placed where its file put it; a skinned one with no skeleton at its bind pose */
                b->how = BRING_PLACED;
                b->place = prim->skin != NULL && node->skin >= 0 ? wgf_mat4_identity() : worlds[n];
            }
        }
    }
    ok = true;
done:
    free(worlds);
    if (maps != NULL) { /* kept until the merge is made: the brings point into them */
        void **more = (void **)realloc(*owned, sizeof(void *) * (size_t)(*owned_count + 2 * file->skin_count + 2));
        if (more != NULL) {
            *owned = more;
            for (s = 0; s < file->skin_count; s++) {
                (*owned)[(*owned_count)++] = maps[s];
                (*owned)[(*owned_count)++] = rebinds[s];
            }
            (*owned)[(*owned_count)++] = maps;
            (*owned)[(*owned_count)++] = rebinds;
        } else { /* out of memory: freed now, and the merge fails with it */
            for (s = 0; s < file->skin_count; s++) {
                free(maps[s]);
                free(rebinds[s]);
            }
            free(maps);
            free(rebinds);
            ok = false;
        }
    } else {
        free(rebinds);
    }
    return ok;
}

/* The merge made from its READY pieces, into its record: true when it is READY. */
static bool make(const pending_t *job)
{
    const mesh_t *skeleton = job->skeleton != 0 ? wgf_gfx_priv_mesh_record(job->skeleton) : NULL;
    group_t *groups = NULL;
    bring_t *brings = NULL;
    void **owned = NULL;
    wgf_material_t *materials = NULL;
    primitive_t *skinned = NULL, *placed = NULL;
    int group_count = 0, group_capacity = 0, bring_count = 0, bring_capacity = 0, owned_count = 0;
    int skinned_count = 0, placed_count = 0, i;
    mesh_t built, *record;
    bool ok = false;

    memset(&built, 0, sizeof(built));
    for (i = 0; i < job->piece_count; i++) {
        if (!plan_piece(job->pieces[i], skeleton, &groups, &group_count, &group_capacity, &brings, &bring_count,
                        &bring_capacity, &owned, &owned_count)) {
            wgf_log_error("wgf_mesh_create_merged: out of memory");
            goto done;
        }
    }
    for (i = 0; i < bring_count; i++) {
        if (!bring(&groups[brings[i].group], &brings[i])) {
            wgf_log_error("wgf_mesh_create_merged: out of memory");
            goto done;
        }
    }
    materials = (wgf_material_t *)calloc((size_t)(group_count > 0 ? group_count : 1), sizeof(wgf_material_t));
    if (materials == NULL) goto done;
    for (i = 0; i < group_count; i++) { /* a group of differing colors drawn by a white copy */
        materials[i] = groups[i].differs ? wgf_gfx_priv_material_copy_white(groups[i].material) : groups[i].material;
        if (materials[i] == 0) goto done;
        if (!groups[i].differs) wgf_core_priv_resource_retain(materials[i]);
    }
    skinned = primitives_of(groups, group_count, true, &skinned_count);
    placed = primitives_of(groups, group_count, false, &placed_count);
    if (skinned_count < 0 || placed_count < 0) goto done;

    if (wgf_gfx_priv_mesh_record(job->merged) == NULL) goto done; /* let go of while it was made */
    if (skeleton != NULL) { /* a file's: the skeleton's tree, and a node a kind of part at its top */
        if (!copy_skeleton(skeleton, (skinned_count > 0) + (placed_count > 0), &built)) goto done;
        measure_skin(&built.skins[0], skinned, skinned_count);
        built.parts = (wgf_mesh_t *)calloc(2, sizeof(wgf_mesh_t));
        if (built.parts == NULL) goto done;
        if (skinned_count > 0) {
            built.parts[built.part_count] = wgf_gfx_priv_mesh_create_from(skinned, skinned_count, materials, group_count);
            skinned = NULL; /* taken, and freed by it on a refusal */
            if (built.parts[built.part_count] == 0) goto done;
            if (!add_top_node(&built, "merged", built.part_count++, 0)) goto done;
        }
        if (placed_count > 0) {
            built.parts[built.part_count] = wgf_gfx_priv_mesh_create_from(placed, placed_count, materials, group_count);
            placed = NULL;
            if (built.parts[built.part_count] == 0) goto done;
            if (!add_top_node(&built, "merged placed", built.part_count++, -1)) goto done;
        }
        built.bounds_min = wgf_vec3_make(1e30f, 1e30f, 1e30f);
        built.bounds_max = wgf_vec3_make(-1e30f, -1e30f, -1e30f);
        for (i = 0; i < built.part_count; i++) {
            wgf_vec3_t lo, hi;
            if (!wgf_gfx_priv_mesh_get_bounds(built.parts[i], &lo, &hi)) continue;
            built.bounds_min = wgf_vec3_make(fminf(built.bounds_min.x, lo.x), fminf(built.bounds_min.y, lo.y),
                                             fminf(built.bounds_min.z, lo.z));
            built.bounds_max = wgf_vec3_make(fmaxf(built.bounds_max.x, hi.x), fmaxf(built.bounds_max.y, hi.y),
                                             fmaxf(built.bounds_max.z, hi.z));
        }
        if (built.part_count == 0) built.bounds_min = built.bounds_max = wgf_vec3_make(0.0f, 0.0f, 0.0f);
        built.from_file = true;
    } else { /* a plain mesh: its groups its primitives */
        built.primitives = placed;
        built.primitive_count = placed_count;
        placed = NULL;
        built.bounds_min = wgf_vec3_make(1e30f, 1e30f, 1e30f);
        built.bounds_max = wgf_vec3_make(-1e30f, -1e30f, -1e30f);
        for (i = 0; i < built.primitive_count; i++) {
            const wgf_vec3_t lo = built.primitives[i].bounds_min, hi = built.primitives[i].bounds_max;
            built.bounds_min = wgf_vec3_make(fminf(built.bounds_min.x, lo.x), fminf(built.bounds_min.y, lo.y),
                                             fminf(built.bounds_min.z, lo.z));
            built.bounds_max = wgf_vec3_make(fmaxf(built.bounds_max.x, hi.x), fmaxf(built.bounds_max.y, hi.y),
                                             fmaxf(built.bounds_max.z, hi.z));
        }
        if (built.primitive_count == 0) built.bounds_min = built.bounds_max = wgf_vec3_make(0.0f, 0.0f, 0.0f);
    }
    built.materials = materials; /* the record holds them: the parts hold their own */
    built.material_count = group_count;
    materials = NULL;
    record = wgf_gfx_priv_mesh_record(job->merged); /* resolved again: the parts' adds may have moved the pool */
    if (record == NULL) goto done;
    record->primitives = built.primitives;
    record->primitive_count = built.primitive_count;
    record->materials = built.materials;
    record->material_count = built.material_count;
    record->nodes = built.nodes;
    record->node_count = built.node_count;
    record->parts = built.parts;
    record->part_count = built.part_count;
    record->skins = built.skins;
    record->skin_count = built.skin_count;
    record->animations = built.animations;
    record->animation_count = built.animation_count;
    record->animated = built.animated;
    record->bounds_min = built.bounds_min;
    record->bounds_max = built.bounds_max;
    record->from_file = built.from_file;
    memset(&built, 0, sizeof(built));
    ok = true;
done:
    if (!ok) wgf_gfx_priv_mesh_free_data(&built);
    for (i = 0; i < group_count; i++) {
        free(groups[i].vertices);
        free(groups[i].skin);
        free(groups[i].indices);
    }
    if (materials != NULL) {
        for (i = 0; i < group_count; i++) {
            if (materials[i] != 0) wgf_resource_release(materials[i]);
        }
        free(materials);
    }
    if (skinned != NULL) {
        for (i = 0; i < skinned_count; i++) {
            free(skinned[i].vertices);
            free(skinned[i].indices);
            free(skinned[i].skin);
        }
        free(skinned);
    }
    if (placed != NULL) {
        for (i = 0; i < placed_count; i++) {
            free(placed[i].vertices);
            free(placed[i].indices);
            free(placed[i].skin);
        }
        free(placed);
    }
    for (i = 0; i < owned_count; i++) free(owned[i]);
    free(owned);
    free(groups);
    free(brings);
    return ok;
}

/* --- waiting for the pieces ------------------------------------------------------------ */

static void let_go(pending_t *job)
{
    int i;
    for (i = 0; i < job->piece_count; i++) wgf_resource_release(job->pieces[i]);
    if (job->skeleton != 0) wgf_resource_release(job->skeleton);
}

/* Each merge waiting: made once every piece is READY, FAILED once one is; one let go of
 * meanwhile forgotten. */
static void update(float dt)
{
    int i = 0;
    (void)dt;
    while (i < pending_count) {
        pending_t *job = &pending[i];
        bool waiting = false, failed = false, done = true;
        int p;
        if (wgf_gfx_priv_mesh_record(job->merged) == NULL) {
            done = true;
        } else {
            for (p = -1; p < job->piece_count; p++) {
                const wgf_mesh_t piece = p < 0 ? job->skeleton : job->pieces[p];
                wgf_resource_status_t status;
                if (piece == 0) continue;
                status = wgf_resource_get_status(piece);
                if (status == WGF_RESOURCE_STATUS_FAILED) failed = true;
                if (status == WGF_RESOURCE_STATUS_PENDING) waiting = true;
            }
            if (failed) {
                wgf_log_warn("wgf_mesh_create_merged: a piece FAILED, so the merged mesh did");
                wgf_core_priv_resource_failed(job->merged);
            } else if (waiting) {
                done = false;
            } else if (job->skeleton != 0 && wgf_gfx_priv_mesh_record(job->skeleton)->skin_count == 0) {
                wgf_log_warn("wgf_mesh_create_merged: its skeleton has no skin, so the merged mesh FAILED");
                wgf_core_priv_resource_failed(job->merged);
            } else if (make(job)) {
                const bool file = wgf_gfx_priv_mesh_record(job->merged)->from_file;
                wgf_core_priv_resource_loaded(job->merged, NULL);
                if (file) wgf_gfx_priv_model_mesh_done(job->merged);
            } else {
                wgf_core_priv_resource_failed(job->merged);
            }
        }
        if (!done) {
            i++;
            continue;
        }
        let_go(job);
        pending[i] = pending[--pending_count];
    }
}

static void stop(void)
{
    int i;
    for (i = 0; i < pending_count; i++) let_go(&pending[i]);
    free(pending);
    pending = NULL;
    pending_count = pending_capacity = 0;
}

static wgf_core_priv_part_t part = {.name = "merged meshes",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_MESH_MERGES,
                                    .update = update,
                                    .stop = stop};

wgf_mesh_t wgf_mesh_create_merged(const wgf_mesh_t *meshes, int count, wgf_mesh_t skeleton)
{
    pending_t *job;
    wgf_mesh_t merged;
    int i;
    if (meshes == NULL || count < 1 || count > MAX_PIECES) {
        wgf_log_error("wgf_mesh_create_merged: 1 to %d meshes, not %d", MAX_PIECES, meshes == NULL ? 0 : count);
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (wgf_gfx_priv_mesh_record(meshes[i]) == NULL) {
            wgf_log_error("wgf_mesh_create_merged: piece %d isn't a mesh", i);
            return 0;
        }
    }
    if (skeleton != 0 && (wgf_gfx_priv_mesh_record(skeleton) == NULL || !wgf_gfx_priv_mesh_record(skeleton)->from_file)) {
        wgf_log_error("wgf_mesh_create_merged: its skeleton isn't a glTF file's mesh");
        return 0;
    }
    if (pending_count == pending_capacity) {
        const int grown = pending_capacity > 0 ? pending_capacity * 2 : 8;
        pending_t *more = (pending_t *)realloc(pending, sizeof(pending_t) * (size_t)grown);
        if (more == NULL) {
            wgf_log_error("wgf_mesh_create_merged: out of memory");
            return 0;
        }
        pending = more;
        pending_capacity = grown;
    }
    merged = wgf_gfx_priv_mesh_ensure_pool() ? wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_MESH) : 0;
    if (merged == 0) {
        wgf_log_error("wgf_mesh_create_merged: no room for another mesh");
        return 0;
    }
    wgf_core_priv_part_install(&part);
    wgf_core_priv_resource_get(merged)->status = WGF_RESOURCE_STATUS_PENDING; /* until its pieces are in */
    wgf_gfx_priv_mesh_record(merged)->from_file = skeleton != 0; /* a model of it made meanwhile is a file's root */
    job = &pending[pending_count++];
    memset(job, 0, sizeof(*job));
    job->merged = merged;
    job->piece_count = count;
    for (i = 0; i < count; i++) {
        job->pieces[i] = meshes[i];
        wgf_core_priv_resource_retain(meshes[i]);
    }
    job->skeleton = skeleton;
    if (skeleton != 0) wgf_core_priv_resource_retain(skeleton);
    return merged;
}
