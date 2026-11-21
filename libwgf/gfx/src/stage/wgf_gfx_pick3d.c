#include "wgf_stage3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "stage/wgf_gfx_model_priv.h"
#include "draw/wgf_gfx_draw3d_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "wgf_camera3d.h"
#include "wgf_collide3d.h"
#include "wgf_log.h"
#include "wgf_model.h"
#include "wgf_resource.h"
#include "wgf_shape3d.h"

/* Picking in a 3D stage, libwgt's scene pick: a ray tested against each model's box, placed
 * as it is now, and then, when the box is met nearer than the nearest hit so far, against its
 * triangles as they are drawn -- a skinned part in its pose this frame, posed on the CPU as
 * immediate mode's model draws pose it (wgf_gfx_priv_draw_model_pose), with no box first --
 * and 3D shapes' cubes and spheres by their own math. A hit is wgf_collide3d.h's ray hit,
 * the nearest kept. Its own file, so a program that never picks links none of it. */

#define HIT_FLOATS 8 /* wgf_collide3d.h's hit: normal, depth, point, time */

typedef struct ray_t {
    float o[3], d[3];
} ray_t;

typedef struct best_t {
    bool any;
    float hit[HIT_FLOATS];
    wgf_actor_t actor;
} best_t;

/* A triangle test's state: the ray, the nearest so far, and what a hit reports. */
typedef struct triangles_t {
    const ray_t *ray;
    best_t *best;
    wgf_actor_t report;
} triangles_t;

static void keep(best_t *best, const float *hit, wgf_actor_t actor)
{
    int i;
    for (i = 0; i < HIT_FLOATS; i++) best->hit[i] = hit[i];
    best->any = true;
    best->actor = actor;
}

static void test_triangles(const float *positions, int vertex_count, const uint32_t *indices, int index_count,
                           void *user)
{
    triangles_t *t = (triangles_t *)user;
    const ray_t *ray = t->ray;
    int i;
    for (i = 0; i + 2 < index_count; i += 3) {
        float points[9] = {0}, hit[HIT_FLOATS] = {0};
        int k;
        for (k = 0; k < 3; k++) {
            const uint32_t v = indices[i + k];
            if (v >= (uint32_t)vertex_count) break;
            points[3 * k] = positions[3 * v];
            points[3 * k + 1] = positions[3 * v + 1];
            points[3 * k + 2] = positions[3 * v + 2];
        }
        if (k < 3) continue;
        if (wgf_collide3d_ray_triangle(ray->o[0], ray->o[1], ray->o[2], ray->d[0], ray->d[1], ray->d[2], points, 9,
                                       hit, HIT_FLOATS) &&
            (!t->best->any || hit[7] < t->best->hit[7])) {
            keep(t->best, hit, t->report);
        }
    }
}

/* The ray taken into the space `world` places things from. */
static ray_t ray_into(const ray_t *ray, const wgf_mat4_t *inverse)
{
    const wgf_vec3_t o = wgf_mat4_transform_point(*inverse, wgf_vec3_make(ray->o[0], ray->o[1], ray->o[2]));
    const wgf_vec3_t d = wgf_mat4_transform_direction(*inverse, wgf_vec3_make(ray->d[0], ray->d[1], ray->d[2]));
    ray_t local;
    local.o[0] = o.x, local.o[1] = o.y, local.o[2] = o.z;
    local.d[0] = d.x, local.d[1] = d.y, local.d[2] = d.z;
    return local;
}

/* A hit found in a space placed by `world` (`inverse` its inverse) brought back to the
 * stage's: its point placed, its normal turned by the inverse's transpose, made a unit
 * facing the ray again; its time is the same along the ray in either space. */
static void hit_out_of(float *hit, const wgf_mat4_t *world, const wgf_mat4_t *inverse, const ray_t *ray)
{
    const wgf_vec3_t p = wgf_mat4_transform_point(*world, wgf_vec3_make(hit[4], hit[5], hit[6]));
    const float *m = inverse->m;
    wgf_vec3_t n = wgf_vec3_make(m[0] * hit[0] + m[1] * hit[1] + m[2] * hit[2],
                                 m[4] * hit[0] + m[5] * hit[1] + m[6] * hit[2],
                                 m[8] * hit[0] + m[9] * hit[1] + m[10] * hit[2]);
    if (wgf_vec3_length(n) > 0.0f) {
        n = wgf_vec3_normalize(n);
        if (n.x * ray->d[0] + n.y * ray->d[1] + n.z * ray->d[2] > 0.0f) n = wgf_vec3_scale(n, -1.0f);
    }
    hit[0] = n.x, hit[1] = n.y, hit[2] = n.z;
    hit[4] = p.x, hit[5] = p.y, hit[6] = p.z;
}

/* One model actor's own mesh (a plain model, or a file's node): its box first, unless a
 * skin poses it, then its triangles as drawn. */
static void test_model(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const ray_t *ray, best_t *best,
                       wgf_actor_t report)
{
    const wgf_mesh_t mesh = actor_ptr->as.model.mesh;
    triangles_t t;
    if (mesh == 0 || wgf_resource_get_status(mesh) != WGF_RESOURCE_STATUS_READY) return;
    if (actor_ptr->as.model.skin_root == 0) {
        const wgf_mat4_t world = wgf_gfx_priv_actor_get_world_matrix(actor);
        const wgf_mat4_t inverse = wgf_mat4_invert(world);
        const ray_t local = ray_into(ray, &inverse);
        wgf_vec3_t lo, hi;
        float box[HIT_FLOATS] = {0};
        if (!wgf_gfx_priv_mesh_get_bounds(mesh, &lo, &hi)) return;
        if (!wgf_collide3d_ray_box(local.o[0], local.o[1], local.o[2], local.d[0], local.d[1], local.d[2], lo.x, lo.y,
                                   lo.z, hi.x, hi.y, hi.z, box, HIT_FLOATS)) {
            return;
        }
        if (best->any && box[7] > best->hit[7]) return; /* met no nearer than what was hit */
    }
    t.ray = ray;
    t.best = best;
    t.report = report;
    wgf_gfx_priv_draw_model_pose(actor, test_triangles, &t);
}

/* A 3D shape's cube or sphere, in its own space (its scale may stretch it). */
static void test_shape(wgf_actor_t actor, const wgf_gfx_priv_actor_t *actor_ptr, const ray_t *ray, best_t *best)
{
    const wgf_gfx_priv_shape3d_t *s = &actor_ptr->as.shape3d;
    const wgf_mat4_t world = wgf_gfx_priv_actor_get_world_matrix(actor);
    const wgf_mat4_t inverse = wgf_mat4_invert(world);
    const ray_t local = ray_into(ray, &inverse);
    float hit[HIT_FLOATS] = {0};
    bool met = false;
    if (s->kind == WGF_SHAPE3D_KIND_CUBE) {
        const float hx = s->dim[0] * 0.5f, hy = s->dim[1] * 0.5f, hz = s->dim[2] * 0.5f;
        met = wgf_collide3d_ray_box(local.o[0], local.o[1], local.o[2], local.d[0], local.d[1], local.d[2], -hx, -hy,
                                    -hz, hx, hy, hz, hit, HIT_FLOATS);
    } else if (s->kind == WGF_SHAPE3D_KIND_SPHERE) {
        met = wgf_collide3d_ray_sphere(local.o[0], local.o[1], local.o[2], local.d[0], local.d[1], local.d[2], 0.0f,
                                       0.0f, 0.0f, s->dim[0], hit, HIT_FLOATS);
    }
    if (!met || (best->any && hit[7] >= best->hit[7])) return;
    hit_out_of(hit, &world, &inverse, ray);
    keep(best, hit, actor);
}

/* The model a file's node is reported as: the file's root above it (what the game made);
 * 0 when it has none. */
static wgf_actor_t file_root_of(const wgf_gfx_priv_actor_t *actor_ptr)
{
    int steps = 0;
    while (actor_ptr != NULL && actor_ptr->parent != 0 && steps++ < 4096) {
        const wgf_actor_t parent = actor_ptr->parent;
        actor_ptr = wgf_gfx_priv_actor_of_silently(parent);
        if (actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_MODEL && actor_ptr->as.model.file_root) {
            return parent;
        }
    }
    return 0;
}

/* Everything under `top` (enabled, with its children) tested against the ray: models and 3D
 * shapes, visible and pickable; or, `file` given (a file's root), that file's nodes alone,
 * pickable or not. */
static void walk(wgf_actor_t top, const ray_t *ray, best_t *best, wgf_actor_t file)
{
    wgf_actor_t *todo;
    int count = 0, capacity = 64;
    todo = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)capacity);
    if (todo == NULL) {
        wgf_log_error("wgf_gfx_pick3d: out of memory picking");
        return;
    }
    todo[count++] = top;
    while (count > 0) {
        const wgf_actor_t next = todo[--count];
        wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_silently(next);
        const wgf_actor_t *dense;
        int i, dense_count;
        if (actor_ptr == NULL || !actor_ptr->enabled) continue; /* with its children */
        if (actor_ptr->visible && (actor_ptr->pickable || file != 0)) {
            if (actor_ptr->type == WGF_ACTOR_KIND_MODEL && !actor_ptr->as.model.file_root &&
                actor_ptr->as.model.mesh != 0) {
                const wgf_actor_t root = actor_ptr->from_file ? file_root_of(actor_ptr) : 0;
                wgf_color_t tint = 0;
                if (file != 0 ? root == file
                              : (root == 0 || (wgf_actor_is_pickable(root) && wgf_gfx_priv_model_file_shown(actor_ptr, &tint)))) {
                    test_model(next, actor_ptr, ray, best, root != 0 ? root : next);
                }
                actor_ptr = wgf_gfx_priv_actor_of_silently(next); /* nothing is made, but read it again all the same */
                if (actor_ptr == NULL) continue;
            } else if (actor_ptr->type == WGF_ACTOR_KIND_SHAPE3D && file == 0) {
                test_shape(next, actor_ptr, ray, best);
            }
        }
        dense = wgf_gfx_priv_actor_children_of(actor_ptr, &dense_count);
        for (i = 0; i < dense_count; i++) {
            if (count == capacity) {
                wgf_actor_t *grown = (wgf_actor_t *)realloc(todo, sizeof(wgf_actor_t) * (size_t)(capacity * 2));
                if (grown == NULL) break;
                todo = grown;
                capacity *= 2;
            }
            todo[count++] = dense[i];
        }
    }
    free(todo);
}

static void put_hit(const best_t *best, float *hit, int hit_count)
{
    int i;
    if (hit == NULL) return;
    for (i = 0; i < hit_count && i < HIT_FLOATS; i++) hit[i] = best->any ? best->hit[i] : 0.0f;
}

static bool make_ray(float ox, float oy, float oz, float dx, float dy, float dz, ray_t *ray)
{
    ray->o[0] = ox, ray->o[1] = oy, ray->o[2] = oz;
    ray->d[0] = dx, ray->d[1] = dy, ray->d[2] = dz;
    return isfinite(ox) && isfinite(oy) && isfinite(oz) && isfinite(dx) && isfinite(dy) && isfinite(dz) &&
           (dx != 0.0f || dy != 0.0f || dz != 0.0f);
}

wgf_actor_t wgf_stage3d_pick_ray(wgf_actor_t stage, float ox, float oy, float oz, float dx, float dy, float dz,
                                 float *hit, int hit_count)
{
    best_t best = {0};
    ray_t ray;
    const wgf_gfx_priv_actor_t *stage_ptr = wgf_gfx_priv_actor_of(stage); /* a dead one warned of */
    if (stage_ptr != NULL && stage_ptr->type == WGF_ACTOR_KIND_STAGE3D && make_ray(ox, oy, oz, dx, dy, dz, &ray)) {
        walk(stage, &ray, &best, 0);
    }
    put_hit(&best, hit, hit_count);
    return best.any ? best.actor : 0;
}

wgf_actor_t wgf_stage3d_pick(wgf_actor_t stage, float x, float y, float *hit, int hit_count)
{
    const wgf_actor_t camera = wgf_stage3d_get_camera(stage);
    wgf_vec3_t o, d;
    if (camera == 0) {
        const best_t none = {0};
        put_hit(&none, hit, hit_count);
        return 0;
    }
    o = wgf_camera3d_screen_to_origin(camera, x, y);
    d = wgf_camera3d_screen_to_direction(camera, x, y);
    return wgf_stage3d_pick_ray(stage, o.x, o.y, o.z, d.x, d.y, d.z, hit, hit_count);
}

bool wgf_model_pick_ray(wgf_actor_t model, float ox, float oy, float oz, float dx, float dy, float dz, float *hit,
                        int hit_count)
{
    best_t best = {0};
    ray_t ray;
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(model);
    if (actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_MODEL && make_ray(ox, oy, oz, dx, dy, dz, &ray)) {
        if (!actor_ptr->as.model.file_root) {
            test_model(model, actor_ptr, &ray, &best, model);
        } else { /* a file's root: its nodes, as they are drawn */
            walk(model, &ray, &best, model);
        }
    }
    put_hit(&best, hit, hit_count);
    return best.any;
}
