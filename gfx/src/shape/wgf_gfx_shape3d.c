#include "wgf_shape3d.h"

#include <stdlib.h>
#include <string.h>

#include "draw/wgf_gfx_draw3d_priv.h"
#include "node/wgf_gfx_node_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_log.h"

/* 3D shape nodes, libwgt's (wgrender's shape3d): their settings, and their drawing
 * through immediate mode's 3D geometry (wgf_gfx_draw3d.c), which a stage's draw
 * records into a layer of its own and draws among its models. A line strip's points
 * come from a caller-owned array, as a 2D polygon's do. */

#define MAX_STRIP_POINTS 65536

static wgf_gfx_priv_node_t *shape_of(wgf_node_t shape)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(shape);
    return node_ptr != NULL && node_ptr->type == WGF_NODE_TYPE_SHAPE3D ? node_ptr : NULL;
}

/* A 3D shape let go of: its line strip's points. */
static void shape3d_free(wgf_node_t shape, wgf_gfx_priv_node_t *node_ptr)
{
    (void)shape;
    free(node_ptr->as.shape3d.points);
    node_ptr->as.shape3d.points = NULL;
    node_ptr->as.shape3d.point_floats = 0;
}

static const wgf_gfx_priv_node_kind_t node_kind = {shape3d_free, NULL};

wgf_node_t wgf_shape3d_create(void)
{
    const wgf_node_t shape = wgf_gfx_priv_node_create(WGF_NODE_TYPE_SHAPE3D);
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(shape);
    if (node_ptr == NULL) return 0;
    wgf_gfx_priv_node_set_kind(WGF_NODE_TYPE_SHAPE3D, &node_kind);
    node_ptr->as.shape3d.kind = WGF_SHAPE3D_KIND_NONE;
    node_ptr->as.shape3d.color = 0xFFFFFFFFu;
    return shape;
}

wgf_shape3d_kind_t wgf_shape3d_get_kind(wgf_node_t shape)
{
    const wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    return node_ptr != NULL ? (wgf_shape3d_kind_t)node_ptr->as.shape3d.kind : WGF_SHAPE3D_KIND_NONE;
}

static bool set_kind(wgf_node_t shape, wgf_shape3d_kind_t kind, const float *dim, int count)
{
    wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    int i;
    if (node_ptr == NULL) return false;
    for (i = 0; kind != WGF_SHAPE3D_KIND_LINE && i < count; i++) {
        if (dim[i] < 0.0f) return false;
    }
    node_ptr->as.shape3d.kind = kind;
    memset(node_ptr->as.shape3d.dim, 0, sizeof(node_ptr->as.shape3d.dim));
    if (count > 0) memcpy(node_ptr->as.shape3d.dim, dim, (size_t)count * sizeof(float));
    return true;
}

bool wgf_shape3d_set_cube(wgf_node_t shape, float width, float height, float length)
{
    const float dim[3] = {width, height, length};
    return set_kind(shape, WGF_SHAPE3D_KIND_CUBE, dim, 3);
}

bool wgf_shape3d_set_sphere(wgf_node_t shape, float radius)
{
    return set_kind(shape, WGF_SHAPE3D_KIND_SPHERE, &radius, 1);
}

bool wgf_shape3d_set_rectangle(wgf_node_t shape, float width, float height)
{
    const float dim[2] = {width, height};
    return set_kind(shape, WGF_SHAPE3D_KIND_RECTANGLE, dim, 2);
}

bool wgf_shape3d_set_circle(wgf_node_t shape, float radius)
{
    return set_kind(shape, WGF_SHAPE3D_KIND_CIRCLE, &radius, 1);
}

bool wgf_shape3d_set_line(wgf_node_t shape, float x0, float y0, float z0, float x1, float y1, float z1)
{
    const float dim[6] = {x0, y0, z0, x1, y1, z1};
    return set_kind(shape, WGF_SHAPE3D_KIND_LINE, dim, 6);
}

bool wgf_shape3d_set_line_strip(wgf_node_t shape, const float *points, int float_count)
{
    wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    float *copy = NULL;
    if (node_ptr == NULL || float_count < 0 || float_count % 3 != 0 || float_count > MAX_STRIP_POINTS * 3 ||
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
    node_ptr = shape_of(shape);
    free(node_ptr->as.shape3d.points);
    node_ptr->as.shape3d.points = copy;
    node_ptr->as.shape3d.point_floats = float_count;
    return true;
}

int wgf_shape3d_get_point_count(wgf_node_t shape)
{
    const wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    return node_ptr != NULL && node_ptr->as.shape3d.kind == WGF_SHAPE3D_KIND_LINE_STRIP
               ? node_ptr->as.shape3d.point_floats / 3
               : 0;
}

int wgf_shape3d_get_points(wgf_node_t shape, float *points, int float_count)
{
    const wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    int count;
    if (node_ptr == NULL || points == NULL || float_count <= 0) return 0;
    count = wgf_shape3d_get_point_count(shape) * 3;
    if (count > float_count) count = float_count;
    if (count > 0) memcpy(points, node_ptr->as.shape3d.points, sizeof(float) * (size_t)count);
    return count;
}

wgf_vec3_t wgf_shape3d_get_size(wgf_node_t shape)
{
    const wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    if (node_ptr == NULL) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    if (node_ptr->as.shape3d.kind == WGF_SHAPE3D_KIND_CUBE) {
        return wgf_vec3_make(node_ptr->as.shape3d.dim[0], node_ptr->as.shape3d.dim[1], node_ptr->as.shape3d.dim[2]);
    }
    if (node_ptr->as.shape3d.kind == WGF_SHAPE3D_KIND_RECTANGLE) {
        return wgf_vec3_make(node_ptr->as.shape3d.dim[0], node_ptr->as.shape3d.dim[1], 0.0f);
    }
    return wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

float wgf_shape3d_get_radius(wgf_node_t shape)
{
    const wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    if (node_ptr == NULL) return 0.0f;
    return node_ptr->as.shape3d.kind == WGF_SHAPE3D_KIND_SPHERE || node_ptr->as.shape3d.kind == WGF_SHAPE3D_KIND_CIRCLE
               ? node_ptr->as.shape3d.dim[0]
               : 0.0f;
}

wgf_vec3_t wgf_shape3d_get_line_start(wgf_node_t shape)
{
    const wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    if (node_ptr == NULL || node_ptr->as.shape3d.kind != WGF_SHAPE3D_KIND_LINE) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return wgf_vec3_make(node_ptr->as.shape3d.dim[0], node_ptr->as.shape3d.dim[1], node_ptr->as.shape3d.dim[2]);
}

wgf_vec3_t wgf_shape3d_get_line_end(wgf_node_t shape)
{
    const wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    if (node_ptr == NULL || node_ptr->as.shape3d.kind != WGF_SHAPE3D_KIND_LINE) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return wgf_vec3_make(node_ptr->as.shape3d.dim[3], node_ptr->as.shape3d.dim[4], node_ptr->as.shape3d.dim[5]);
}

bool wgf_shape3d_set_color(wgf_node_t shape, wgf_color_t color)
{
    wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    if (node_ptr == NULL) return false;
    node_ptr->as.shape3d.color = color;
    return true;
}

wgf_color_t wgf_shape3d_get_color(wgf_node_t shape)
{
    const wgf_gfx_priv_node_t *node_ptr = shape_of(shape);
    return node_ptr != NULL ? node_ptr->as.shape3d.color : 0u;
}

void wgf_gfx_priv_shape3d_draw(const wgf_gfx_priv_node_t *node_ptr, const wgf_mat4_t *world)
{
    const wgf_gfx_priv_shape3d_t *s = &node_ptr->as.shape3d;
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
