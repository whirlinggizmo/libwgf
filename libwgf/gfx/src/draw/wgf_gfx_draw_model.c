#include "wgf_draw.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "draw/wgf_gfx_draw3d_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "mesh/wgf_gfx_mesh_record_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_model_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_model.h"
#include "wgf_resource.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"

/* Immediate mode's model draws, raylib's DrawModelWires and DrawModelPoints and
 * helix-raylib's skeleton: a model as it stands this frame, its skinned parts posed on the
 * CPU with the joint matrices the stage poses them with on the GPU
 * (wgf_gfx_priv_model_joint_matrix), recorded through the 3D camera's view. Its own file,
 * so a program drawing no model this way links none of it. */

#define POINT_SIZE 3.0f      /* a point's width, in pixels */
#define MARK_SHARE 0.02f     /* a skeleton's cross, beside the model's size: helix-raylib's */
#define MARK_SMALLEST 0.01f  /* a cross's arm when the model has no size */

static void set_color(wgf_color_t color)
{
    sgl_c4b((uint8_t)wgf_color_get_red(color), (uint8_t)wgf_color_get_green(color), (uint8_t)wgf_color_get_blue(color),
            (uint8_t)wgf_color_get_alpha(color));
}

/* `p` (3 floats) moved by `m` (column-major, its last row 0, 0, 0, 1), scaled by `weight`,
 * added to `out`. */
static void add_point(const float *m, const float *p, float weight, float *out)
{
    out[0] += weight * (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]);
    out[1] += weight * (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]);
    out[2] += weight * (m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]);
}

/* The skin placing the skinned parts of `actor_ptr` (a file's node), and its root's tree
 * and world matrix: NULL for a model with none, or whose root has no tree. */
static const wgf_gfx_priv_mesh_skin_t *skin_of(const wgf_gfx_priv_actor_t *actor_ptr,
                                               const wgf_gfx_priv_model_tree_t **tree, wgf_mat4_t *root_world)
{
    const wgf_actor_t root = actor_ptr->as.model.skin_root;
    const int skin = actor_ptr->as.model.skin;
    const wgf_gfx_priv_actor_t *root_ptr = root != 0 ? wgf_gfx_priv_actor_of_silently(root) : NULL;
    const wgf_gfx_priv_mesh_record_t *file;
    if (root_ptr == NULL || root_ptr->type != WGF_ACTOR_KIND_MODEL || root_ptr->as.model.tree == NULL) return NULL;
    file = wgf_gfx_priv_mesh_record(root_ptr->as.model.mesh);
    *tree = root_ptr->as.model.tree;
    if (file == NULL || skin < 0 || skin >= file->skin_count || skin >= (*tree)->skin_count ||
        file->skins[skin].joint_count == 0) {
        return NULL;
    }
    *root_world = wgf_gfx_priv_actor_get_world_matrix(root);
    return &file->skins[skin];
}

/* One model actor's own mesh posed, a primitive at a time, as the stage draws it: a static
 * part placed by the actor, a skinned one by its root, moved by its joints. False when its
 * mesh isn't READY. */
static bool pose_mesh(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, wgf_gfx_priv_draw_posed_t posed,
                      void *user)
{
    const wgf_mesh_t mesh = actor_ptr->as.model.mesh;
    const wgf_gfx_priv_mesh_record_t *record = wgf_gfx_priv_mesh_record(mesh);
    const wgf_gfx_priv_model_tree_t *tree = NULL;
    const wgf_gfx_priv_mesh_skin_t *skin;
    const wgf_mat4_t world = wgf_gfx_priv_actor_get_world_matrix(actor);
    wgf_mat4_t root_world = wgf_mat4_identity();
    int p;
    if (record == NULL || wgf_resource_get_status(mesh) != WGF_RESOURCE_STATUS_READY) return false;
    skin = skin_of(actor_ptr, &tree, &root_world);
    for (p = 0; p < record->primitive_count; p++) {
        const wgf_gfx_priv_mesh_record_primitive_t *primitive = &record->primitives[p];
        const bool skinned = skin != NULL && primitive->skin != NULL;
        const int joints = skinned ? skin->joint_count : 0;
        float *matrices, *positions;
        int v, j;
        if (primitive->vertices == NULL || primitive->indices == NULL || primitive->vertex_count <= 0) continue;
        matrices = wgf_gfx_priv_draw_scratch(16 * joints + 3 * primitive->vertex_count);
        if (matrices == NULL) return true; /* out of memory: nothing more drawn */
        positions = matrices + 16 * joints;
        if (skinned) { /* each joint's matrix, placed in the world by the root */
            const wgf_mat4_t to_root = wgf_mat4_invert(root_world);
            for (j = 0; j < joints; j++) {
                const wgf_mat4_t m = wgf_mat4_mul(root_world, wgf_gfx_priv_model_joint_matrix(tree, skin, j, &to_root));
                memcpy(&matrices[16 * j], m.m, sizeof(m.m));
            }
        }
        for (v = 0; v < primitive->vertex_count; v++) {
            const float *at = &primitive->vertices[(size_t)v * WGF_GFX_PRIV_MESH_VERTEX_FLOATS];
            float *out = &positions[3 * v];
            out[0] = out[1] = out[2] = 0.0f;
            if (skinned) { /* its four joints' places, by their weights, as the shader blends them */
                const float *weights = &primitive->skin[(size_t)v * WGF_GFX_PRIV_MESH_SKIN_FLOATS];
                int k;
                for (k = 0; k < 4; k++) {
                    const int joint = (int)weights[k];
                    if (weights[4 + k] != 0.0f && joint >= 0 && joint < joints) {
                        add_point(&matrices[16 * joint], at, weights[4 + k], out);
                    }
                }
            } else {
                add_point(world.m, at, 1.0f, out);
            }
        }
        posed(positions, primitive->vertex_count, primitive->indices, primitive->index_count, user);
    }
    return true;
}

bool wgf_gfx_priv_draw_model_pose(wgf_actor_t model, wgf_gfx_priv_draw_posed_t posed, void *user)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_silently(model);
    const wgf_gfx_priv_model_tree_t *tree;
    int i;
    if (actor_ptr == NULL || actor_ptr->type != WGF_ACTOR_KIND_MODEL) return false;
    if (!actor_ptr->as.model.file_root) return pose_mesh(model, actor_ptr, posed, user);
    tree = actor_ptr->as.model.tree;
    if (tree == NULL || wgf_resource_get_status(actor_ptr->as.model.mesh) != WGF_RESOURCE_STATUS_READY) return false;
    for (i = 0; i < tree->node_count; i++) { /* nothing is made below: the pointers hold */
        const wgf_actor_t node = tree->nodes[i];
        const wgf_gfx_priv_actor_t *node_ptr = node != 0 ? wgf_gfx_priv_actor_of_silently(node) : NULL;
        if (node_ptr == NULL || node_ptr->type != WGF_ACTOR_KIND_MODEL || node_ptr->as.model.mesh == 0 ||
            !node_ptr->enabled || !node_ptr->visible) {
            continue;
        }
        pose_mesh(node, node_ptr, posed, user);
    }
    return true;
}

/* Whether a model call may draw: in 3D, given a live model (a dead one warned of, naming
 * the call). */
static bool can_draw(wgf_actor_t model, const char *caller)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(model, caller);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_MODEL && wgf_gfx_priv_draw3d_is_on();
}

static void record_wires(const float *positions, int vertex_count, const uint32_t *indices, int index_count, void *user)
{
    int t;
    sgl_begin_lines();
    set_color(*(const wgf_color_t *)user);
    for (t = 0; t + 2 < index_count; t += 3) {
        const uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
        const float *pa, *pb, *pc;
        if (a >= (uint32_t)vertex_count || b >= (uint32_t)vertex_count || c >= (uint32_t)vertex_count) continue;
        pa = &positions[3 * a];
        pb = &positions[3 * b];
        pc = &positions[3 * c];
        sgl_v3f(pa[0], pa[1], pa[2]);
        sgl_v3f(pb[0], pb[1], pb[2]);
        sgl_v3f(pb[0], pb[1], pb[2]);
        sgl_v3f(pc[0], pc[1], pc[2]);
        sgl_v3f(pc[0], pc[1], pc[2]);
        sgl_v3f(pa[0], pa[1], pa[2]);
    }
    sgl_end();
}

static void record_points(const float *positions, int vertex_count, const uint32_t *indices, int index_count,
                          void *user)
{
    int v;
    (void)indices;
    (void)index_count;
    sgl_point_size(POINT_SIZE);
    sgl_begin_points();
    set_color(*(const wgf_color_t *)user);
    for (v = 0; v < vertex_count; v++) sgl_v3f(positions[3 * v], positions[3 * v + 1], positions[3 * v + 2]);
    sgl_end();
    sgl_point_size(1.0f);
}

bool wgf_draw_model_wires(wgf_actor_t model, wgf_color_t color)
{
    return can_draw(model, WGF_CORE_PRIV_CALLER) && wgf_gfx_priv_draw_model_pose(model, record_wires, &color);
}

bool wgf_draw_model_points(wgf_actor_t model, wgf_color_t color)
{
    return can_draw(model, WGF_CORE_PRIV_CALLER) && wgf_gfx_priv_draw_model_pose(model, record_points, &color);
}

/* Whether node `node` is one of `skin`'s joints. */
static bool is_joint(const wgf_gfx_priv_mesh_skin_t *skin, int node)
{
    int j;
    for (j = 0; j < skin->joint_count; j++) {
        if (skin->joints[j] == node) return true;
    }
    return false;
}

/* Where node `node`'s actor is in the world; false when it wasn't made, or is gone. */
static bool node_place(const wgf_gfx_priv_model_tree_t *tree, int node, wgf_vec3_t *at)
{
    const wgf_actor_t actor = node >= 0 && node < tree->node_count ? tree->nodes[node] : 0;
    if (actor == 0 || wgf_gfx_priv_actor_of_silently(actor) == NULL) return false;
    *at = wgf_mat4_get_translation(wgf_gfx_priv_actor_get_world_matrix(actor));
    return true;
}

bool wgf_draw_model_skeleton(wgf_actor_t model, wgf_color_t color)
{
    const wgf_gfx_priv_actor_t *actor_ptr;
    const wgf_gfx_priv_model_tree_t *tree;
    const wgf_gfx_priv_mesh_record_t *file;
    wgf_actor_t root;
    wgf_vec3_t lo, hi, size;
    float arm;
    int first, last, s;
    if (!can_draw(model, WGF_CORE_PRIV_CALLER)) return false;
    actor_ptr = wgf_gfx_priv_actor_of_silently(model);
    root = actor_ptr->as.model.file_root ? model : actor_ptr->as.model.skin_root;
    if (root == 0) { /* a model of no file: no skin, its mesh READY or not */
        return wgf_resource_get_status(actor_ptr->as.model.mesh) == WGF_RESOURCE_STATUS_READY;
    }
    actor_ptr = wgf_gfx_priv_actor_of_silently(root);
    if (actor_ptr == NULL || actor_ptr->type != WGF_ACTOR_KIND_MODEL || actor_ptr->as.model.tree == NULL) return false;
    tree = actor_ptr->as.model.tree;
    file = wgf_gfx_priv_mesh_record(actor_ptr->as.model.mesh);
    if (file == NULL || wgf_resource_get_status(actor_ptr->as.model.mesh) != WGF_RESOURCE_STATUS_READY) return false;
    first = 0;
    last = file->skin_count < tree->skin_count ? file->skin_count : tree->skin_count;
    if (root != model) { /* a node: its own skin alone */
        const int skin = wgf_gfx_priv_actor_of_silently(model)->as.model.skin;
        first = skin >= 0 ? skin : last;
        last = skin >= 0 && skin + 1 < last ? skin + 1 : last;
    }
    lo = wgf_model_get_bounds_min(root); /* the crosses a share of the model's size, as helix-raylib's */
    hi = wgf_model_get_bounds_max(root);
    size = wgf_vec3_sub(hi, lo);
    arm = MARK_SHARE * sqrtf(wgf_vec3_dot(size, size));
    if (arm < MARK_SMALLEST) arm = MARK_SMALLEST;
    sgl_begin_lines();
    set_color(color);
    for (s = first; s < last; s++) {
        const wgf_gfx_priv_mesh_skin_t *skin = &file->skins[s];
        int j;
        for (j = 0; j < skin->joint_count; j++) {
            const int node = skin->joints[j];
            int above = node >= 0 && node < file->node_count ? file->nodes[node].parent : -1, steps = 0;
            wgf_vec3_t at = wgf_vec3_make(0.0f, 0.0f, 0.0f), from = at;
            if (!node_place(tree, node, &at)) continue;
            while (above >= 0 && above < file->node_count && !is_joint(skin, above) && steps++ < file->node_count) {
                above = file->nodes[above].parent; /* the nearest joint above it */
            }
            if (above >= 0 && above < file->node_count && is_joint(skin, above) && node_place(tree, above, &from)) {
                sgl_v3f(from.x, from.y, from.z);
                sgl_v3f(at.x, at.y, at.z);
            } else {
                sgl_v3f(at.x - arm, at.y, at.z);
                sgl_v3f(at.x + arm, at.y, at.z);
                sgl_v3f(at.x, at.y - arm, at.z);
                sgl_v3f(at.x, at.y + arm, at.z);
                sgl_v3f(at.x, at.y, at.z - arm);
                sgl_v3f(at.x, at.y, at.z + arm);
            }
        }
    }
    sgl_end();
    return true;
}
