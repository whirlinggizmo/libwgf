#include "wgf_shape3d.h"

#include <stdlib.h>
#include <string.h>

#include "draw/wgf_gfx_draw3d_priv.h"
#include "actor/wgf_gfx_actor_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_log.h"

/* 3D shape actors, libwgt's (wgrender's shape3d): their settings, and their drawing
 * through immediate mode's 3D geometry (wgf_gfx_draw3d.c), which a stage's draw
 * records into a layer of its own and draws among its models. A line strip's points
 * come from a caller-owned array, as a 2D polygon's do. */

#define MAX_STRIP_POINTS 65536

static wgf_gfx_priv_actor_t *shape_of(wgf_actor_t shape)
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(shape);
    return actor_ptr != NULL && actor_ptr->type == WGF_ACTOR_KIND_SHAPE3D ? actor_ptr : NULL;
}

/* A 3D shape let go of: its line strip's points. */
static void shape3d_free(wgf_actor_t shape, wgf_gfx_priv_actor_t *actor_ptr)
{
    (void)shape;
    free(actor_ptr->as.shape3d.points);
    actor_ptr->as.shape3d.points = NULL;
    actor_ptr->as.shape3d.point_floats = 0;
}

static const wgf_gfx_priv_actor_kind_t actor_kind = {shape3d_free, NULL};

wgf_actor_t wgf_shape3d_create(void)
{
    const wgf_actor_t shape = wgf_gfx_priv_actor_create(WGF_ACTOR_KIND_SHAPE3D);
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(shape);
    if (actor_ptr == NULL) return 0;
    wgf_gfx_priv_actor_set_kind(WGF_ACTOR_KIND_SHAPE3D, &actor_kind);
    actor_ptr->as.shape3d.kind = WGF_SHAPE3D_KIND_NONE;
    actor_ptr->as.shape3d.color = 0xFFFFFFFFu;
    return shape;
}

wgf_shape3d_kind_t wgf_shape3d_get_kind(wgf_actor_t shape)
{
    const wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    return actor_ptr != NULL ? (wgf_shape3d_kind_t)actor_ptr->as.shape3d.kind : WGF_SHAPE3D_KIND_NONE;
}

static bool set_kind(wgf_actor_t shape, wgf_shape3d_kind_t kind, const float *dim, int count)
{
    wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    int i;
    if (actor_ptr == NULL) return false;
    for (i = 0; kind != WGF_SHAPE3D_KIND_LINE && i < count; i++) {
        if (dim[i] < 0.0f) return false;
    }
    actor_ptr->as.shape3d.kind = kind;
    memset(actor_ptr->as.shape3d.dim, 0, sizeof(actor_ptr->as.shape3d.dim));
    if (count > 0) memcpy(actor_ptr->as.shape3d.dim, dim, (size_t)count * sizeof(float));
    return true;
}

bool wgf_shape3d_set_cube(wgf_actor_t shape, float width, float height, float length)
{
    const float dim[3] = {width, height, length};
    return set_kind(shape, WGF_SHAPE3D_KIND_CUBE, dim, 3);
}

bool wgf_shape3d_set_sphere(wgf_actor_t shape, float radius)
{
    return set_kind(shape, WGF_SHAPE3D_KIND_SPHERE, &radius, 1);
}

bool wgf_shape3d_set_rectangle(wgf_actor_t shape, float width, float height)
{
    const float dim[2] = {width, height};
    return set_kind(shape, WGF_SHAPE3D_KIND_RECTANGLE, dim, 2);
}

bool wgf_shape3d_set_circle(wgf_actor_t shape, float radius)
{
    return set_kind(shape, WGF_SHAPE3D_KIND_CIRCLE, &radius, 1);
}

bool wgf_shape3d_set_line(wgf_actor_t shape, float x0, float y0, float z0, float x1, float y1, float z1)
{
    const float dim[6] = {x0, y0, z0, x1, y1, z1};
    return set_kind(shape, WGF_SHAPE3D_KIND_LINE, dim, 6);
}

bool wgf_shape3d_set_line_strip(wgf_actor_t shape, const float *points, int float_count)
{
    wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    float *copy = NULL;
    if (actor_ptr == NULL || float_count < 0 || float_count % 3 != 0 || float_count > MAX_STRIP_POINTS * 3 ||
        (float_count > 0 && points == NULL)) {
        return false;
    }
    if (float_count > 0) {
        copy = (float *)malloc(sizeof(float) * (size_t)float_count);
        if (copy == NULL) {
            wgf_log_error("wgf_shape3d_set_line_strip: out of memory");
            return false;
        }
        memcpy(copy, points, sizeof(float) * (size_t)float_count);
    }
    set_kind(shape, WGF_SHAPE3D_KIND_LINE_STRIP, NULL, 0);
    actor_ptr = shape_of(shape);
    free(actor_ptr->as.shape3d.points);
    actor_ptr->as.shape3d.points = copy;
    actor_ptr->as.shape3d.point_floats = float_count;
    return true;
}

int wgf_shape3d_get_point_count(wgf_actor_t shape)
{
    const wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    return actor_ptr != NULL && actor_ptr->as.shape3d.kind == WGF_SHAPE3D_KIND_LINE_STRIP
               ? actor_ptr->as.shape3d.point_floats / 3
               : 0;
}

int wgf_shape3d_get_points(wgf_actor_t shape, float *points, int float_count)
{
    const wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    int count;
    if (actor_ptr == NULL || points == NULL || float_count <= 0) return 0;
    count = wgf_shape3d_get_point_count(shape) * 3;
    if (count > float_count) count = float_count;
    if (count > 0) memcpy(points, actor_ptr->as.shape3d.points, sizeof(float) * (size_t)count);
    return count;
}

wgf_vec3_t wgf_shape3d_get_size(wgf_actor_t shape)
{
    const wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    if (actor_ptr == NULL) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    if (actor_ptr->as.shape3d.kind == WGF_SHAPE3D_KIND_CUBE) {
        return wgf_vec3_make(actor_ptr->as.shape3d.dim[0], actor_ptr->as.shape3d.dim[1], actor_ptr->as.shape3d.dim[2]);
    }
    if (actor_ptr->as.shape3d.kind == WGF_SHAPE3D_KIND_RECTANGLE) {
        return wgf_vec3_make(actor_ptr->as.shape3d.dim[0], actor_ptr->as.shape3d.dim[1], 0.0f);
    }
    return wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

float wgf_shape3d_get_radius(wgf_actor_t shape)
{
    const wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    if (actor_ptr == NULL) return 0.0f;
    return actor_ptr->as.shape3d.kind == WGF_SHAPE3D_KIND_SPHERE || actor_ptr->as.shape3d.kind == WGF_SHAPE3D_KIND_CIRCLE
               ? actor_ptr->as.shape3d.dim[0]
               : 0.0f;
}

wgf_vec3_t wgf_shape3d_get_line_start(wgf_actor_t shape)
{
    const wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    if (actor_ptr == NULL || actor_ptr->as.shape3d.kind != WGF_SHAPE3D_KIND_LINE) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return wgf_vec3_make(actor_ptr->as.shape3d.dim[0], actor_ptr->as.shape3d.dim[1], actor_ptr->as.shape3d.dim[2]);
}

wgf_vec3_t wgf_shape3d_get_line_end(wgf_actor_t shape)
{
    const wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    if (actor_ptr == NULL || actor_ptr->as.shape3d.kind != WGF_SHAPE3D_KIND_LINE) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return wgf_vec3_make(actor_ptr->as.shape3d.dim[3], actor_ptr->as.shape3d.dim[4], actor_ptr->as.shape3d.dim[5]);
}

bool wgf_shape3d_set_color(wgf_actor_t shape, wgf_color_t color)
{
    wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    if (actor_ptr == NULL) return false;
    actor_ptr->as.shape3d.color = color;
    return true;
}

wgf_color_t wgf_shape3d_get_color(wgf_actor_t shape)
{
    const wgf_gfx_priv_actor_t *actor_ptr = shape_of(shape);
    return actor_ptr != NULL ? actor_ptr->as.shape3d.color : 0u;
}

void wgf_gfx_priv_shape3d_draw(const wgf_gfx_priv_actor_t *actor_ptr, const wgf_mat4_t *world)
{
    const wgf_gfx_priv_shape3d_t *s = &actor_ptr->as.shape3d;
    sgl_matrix_mode_modelview();
    sgl_load_matrix(world->m);
    switch (s->kind) {
        case WGF_SHAPE3D_KIND_CUBE: wgf_gfx_priv_draw3d_cube(0, 0, 0, s->dim[0], s->dim[1], s->dim[2], s->color); break;
        case WGF_SHAPE3D_KIND_SPHERE: wgf_gfx_priv_draw3d_sphere(0, 0, 0, s->dim[0], s->color); break;
        case WGF_SHAPE3D_KIND_RECTANGLE: wgf_gfx_priv_draw3d_rectangle(s->dim[0], s->dim[1], s->color); break;
        case WGF_SHAPE3D_KIND_CIRCLE: wgf_gfx_priv_draw3d_circle(s->dim[0], s->color); break;
        case WGF_SHAPE3D_KIND_LINE:
            wgf_gfx_priv_draw3d_line(s->dim[0], s->dim[1], s->dim[2], s->dim[3], s->dim[4], s->dim[5], s->color);
            break;
        case WGF_SHAPE3D_KIND_LINE_STRIP: wgf_gfx_priv_draw3d_strip(s->points, s->point_floats / 3, s->color); break;
        default: break;
    }
}
