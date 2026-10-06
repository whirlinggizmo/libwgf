#include "wgf_shape2d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "node/wgf_gfx_node_priv.h"
#include "wgf_draw.h"
#include "wgf_log.h"
#include "wgf_render.h"

/* 2D shapes: each its outline's points in its own units, placed through its node into
 * the frame's logical pixels, then filled or outlined by immediate mode (wgf_draw.h),
 * so a turned, scaled shape is drawn exactly as a plain one. The kind (its free and its
 * draw) is set at the first create, so a program with no shape links none of this. */

#define TAU 6.28318530717958647692f
#define MAX_POINTS 1024
#define CIRCLE_TOLERANCE 0.25f /* pixels a circle's segments may fall inside its curve */

static void free_shape(wgf_node_t node, wgf_gfx_priv_node_t *node_ptr)
{
    (void)node;
    free(node_ptr->as.shape2d.points);
    node_ptr->as.shape2d.points = NULL;
}

/* The pivot's offset, in its own units: where the node's position is in the shape. */
static void pivot_offset(const wgf_gfx_priv_shape2d_t *shape, float *dx, float *dy)
{
    *dx = *dy = 0.0f;
    if (!shape->has_pivot) return;
    if (shape->kind == WGF_SHAPE2D_KIND_RECTANGLE) {
        *dx = -shape->pivot[0] * shape->dim[0];
        *dy = -shape->pivot[1] * shape->dim[1];
    } else if (shape->kind == WGF_SHAPE2D_KIND_CIRCLE) {
        *dx = (0.5f - shape->pivot[0]) * 2.0f * shape->dim[0];
        *dy = (0.5f - shape->pivot[1]) * 2.0f * shape->dim[0];
    }
}

static void place(const wgf_mat4_t *m, float x, float y, float *out)
{
    out[0] = m->m[0] * x + m->m[4] * y + m->m[12];
    out[1] = m->m[1] * x + m->m[5] * y + m->m[13];
}

static void draw_shape(wgf_node_t node, const wgf_gfx_priv_node_t *node_ptr, const wgf_mat4_t *placed,
                       const wgf_mat4_t *view)
{
    const wgf_gfx_priv_shape2d_t *shape = &node_ptr->as.shape2d;
    float local[2 * MAX_POINTS], *pts;
    float dx, dy;
    int n = 0, i;
    (void)node;
    (void)view;
    if (shape->kind == WGF_SHAPE2D_KIND_LINE) {
        float a[2], b[2];
        place(placed, shape->dim[0], shape->dim[1], a);
        place(placed, shape->dim[2], shape->dim[3], b);
        wgf_draw_line(a[0], a[1], b[0], b[1], shape->outline, shape->color);
        return;
    }
    pivot_offset(shape, &dx, &dy);
    if (shape->kind == WGF_SHAPE2D_KIND_RECTANGLE) {
        const float w = shape->dim[0], h = shape->dim[1];
        const float corners[8] = {0, 0, w, 0, w, h, 0, h};
        memcpy(local, corners, sizeof(corners));
        n = 4;
    } else if (shape->kind == WGF_SHAPE2D_KIND_CIRCLE) {
        /* enough segments for its size on screen: its radius through the node's scale */
        const float r = shape->dim[0];
        const float sx = sqrtf(placed->m[0] * placed->m[0] + placed->m[1] * placed->m[1]);
        const float sy = sqrtf(placed->m[4] * placed->m[4] + placed->m[5] * placed->m[5]);
        const float pixels = r * (sx > sy ? sx : sy) * wgf_render_get_dpi_scale();
        n = 8;
        if (pixels > CIRCLE_TOLERANCE) {
            const float step = 2.0f * acosf(1.0f - CIRCLE_TOLERANCE / pixels);
            n = step > 0.0f ? (int)ceilf(TAU / step) : MAX_POINTS;
        }
        n = n < 8 ? 8 : (n > 512 ? 512 : n);
        for (i = 0; i < n; i++) {
            local[2 * i] = cosf(TAU * (float)i / (float)n) * r;
            local[2 * i + 1] = sinf(TAU * (float)i / (float)n) * r;
        }
    } else if (shape->kind == WGF_SHAPE2D_KIND_POLYGON) {
        n = shape->point_floats / 2;
        memcpy(local, shape->points, sizeof(float) * (size_t)shape->point_floats);
    }
    if (n < 3) return;
    pts = local; /* placed in place: each point's own is no longer needed */
    for (i = 0; i < n; i++) place(placed, local[2 * i] + dx, local[2 * i + 1] + dy, &pts[2 * i]);
    if (shape->outline > 0.0f) wgf_draw_polyline(pts, 2 * n, true, shape->outline, shape->color);
    else wgf_draw_polygon(pts, 2 * n, shape->color);
}

static const wgf_gfx_priv_node_kind_t kind = {free_shape, draw_shape};

static wgf_gfx_priv_shape2d_t *shape_of(wgf_node_t shape)
{
    wgf_gfx_priv_node_t *node_ptr = wgf_gfx_priv_node_of(shape);
    return node_ptr != NULL && node_ptr->type == WGF_NODE_TYPE_SHAPE2D ? &node_ptr->as.shape2d : NULL;
}

wgf_node_t wgf_shape2d_create(void)
{
    const wgf_node_t shape = wgf_gfx_priv_node_create(WGF_NODE_TYPE_SHAPE2D);
    wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    if (shape_ptr == NULL) return 0;
    shape_ptr->color = 0xFFFFFFFFu;
    wgf_gfx_priv_node_set_kind(WGF_NODE_TYPE_SHAPE2D, &kind);
    return shape;
}

wgf_shape2d_kind_t wgf_shape2d_get_kind(wgf_node_t shape)
{
    const wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    return shape_ptr != NULL ? (wgf_shape2d_kind_t)shape_ptr->kind : WGF_SHAPE2D_KIND_NONE;
}

/* `shape` made another kind: its old points let go, its dimensions cleared. */
static wgf_gfx_priv_shape2d_t *become(wgf_node_t shape, wgf_shape2d_kind_t kind_of)
{
    wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    if (shape_ptr == NULL) return NULL;
    free(shape_ptr->points);
    shape_ptr->points = NULL;
    shape_ptr->point_floats = 0;
    memset(shape_ptr->dim, 0, sizeof(shape_ptr->dim));
    shape_ptr->kind = kind_of;
    return shape_ptr;
}

bool wgf_shape2d_set_rectangle(wgf_node_t shape, float width, float height)
{
    wgf_gfx_priv_shape2d_t *shape_ptr;
    if (!(width >= 0.0f) || !(height >= 0.0f) || (shape_ptr = become(shape, WGF_SHAPE2D_KIND_RECTANGLE)) == NULL) {
        return false;
    }
    shape_ptr->dim[0] = width;
    shape_ptr->dim[1] = height;
    return true;
}

bool wgf_shape2d_set_circle(wgf_node_t shape, float radius)
{
    wgf_gfx_priv_shape2d_t *shape_ptr;
    if (!(radius >= 0.0f) || (shape_ptr = become(shape, WGF_SHAPE2D_KIND_CIRCLE)) == NULL) return false;
    shape_ptr->dim[0] = radius;
    return true;
}

bool wgf_shape2d_set_line(wgf_node_t shape, float x0, float y0, float x1, float y1)
{
    wgf_gfx_priv_shape2d_t *shape_ptr = become(shape, WGF_SHAPE2D_KIND_LINE);
    if (shape_ptr == NULL) return false;
    shape_ptr->dim[0] = x0;
    shape_ptr->dim[1] = y0;
    shape_ptr->dim[2] = x1;
    shape_ptr->dim[3] = y1;
    return true;
}

bool wgf_shape2d_set_polygon(wgf_node_t shape, const float *points, int count)
{
    wgf_gfx_priv_shape2d_t *shape_ptr;
    float *copy;
    if (shape_of(shape) == NULL || points == NULL || count % 2 != 0 || count < 6 || count > 2 * MAX_POINTS) {
        return false;
    }
    copy = (float *)malloc(sizeof(float) * (size_t)count);
    if (copy == NULL) {
        wgf_log_error("wgf_shape2d_set_polygon: out of memory");
        return false;
    }
    memcpy(copy, points, sizeof(float) * (size_t)count);
    shape_ptr = become(shape, WGF_SHAPE2D_KIND_POLYGON);
    shape_ptr->points = copy;
    shape_ptr->point_floats = count;
    return true;
}

wgf_vec2_t wgf_shape2d_get_size(wgf_node_t shape)
{
    const wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    if (shape_ptr == NULL || shape_ptr->kind != WGF_SHAPE2D_KIND_RECTANGLE) return wgf_vec2_make(0.0f, 0.0f);
    return wgf_vec2_make(shape_ptr->dim[0], shape_ptr->dim[1]);
}

float wgf_shape2d_get_radius(wgf_node_t shape)
{
    const wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    return shape_ptr != NULL && shape_ptr->kind == WGF_SHAPE2D_KIND_CIRCLE ? shape_ptr->dim[0] : 0.0f;
}

wgf_vec2_t wgf_shape2d_get_line_start(wgf_node_t shape)
{
    const wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    if (shape_ptr == NULL || shape_ptr->kind != WGF_SHAPE2D_KIND_LINE) return wgf_vec2_make(0.0f, 0.0f);
    return wgf_vec2_make(shape_ptr->dim[0], shape_ptr->dim[1]);
}

wgf_vec2_t wgf_shape2d_get_line_end(wgf_node_t shape)
{
    const wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    if (shape_ptr == NULL || shape_ptr->kind != WGF_SHAPE2D_KIND_LINE) return wgf_vec2_make(0.0f, 0.0f);
    return wgf_vec2_make(shape_ptr->dim[2], shape_ptr->dim[3]);
}

int wgf_shape2d_get_point_count(wgf_node_t shape)
{
    const wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    return shape_ptr != NULL ? shape_ptr->point_floats / 2 : 0;
}

int wgf_shape2d_get_points(wgf_node_t shape, float *out, int count)
{
    const wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    int n;
    if (shape_ptr == NULL || out == NULL || count <= 0) return 0;
    n = shape_ptr->point_floats < count ? shape_ptr->point_floats : count;
    if (n > 0) memcpy(out, shape_ptr->points, sizeof(float) * (size_t)n);
    return n;
}

bool wgf_shape2d_set_pivot(wgf_node_t shape, float x, float y)
{
    wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    if (shape_ptr == NULL) return false;
    shape_ptr->pivot[0] = x;
    shape_ptr->pivot[1] = y;
    shape_ptr->has_pivot = true;
    return true;
}

wgf_vec2_t wgf_shape2d_get_pivot(wgf_node_t shape)
{
    const wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    if (shape_ptr == NULL) return wgf_vec2_make(0.0f, 0.0f);
    if (shape_ptr->has_pivot) return wgf_vec2_make(shape_ptr->pivot[0], shape_ptr->pivot[1]);
    return shape_ptr->kind == WGF_SHAPE2D_KIND_CIRCLE ? wgf_vec2_make(0.5f, 0.5f) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_shape2d_set_outline(wgf_node_t shape, float thickness)
{
    wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    if (shape_ptr == NULL) return false;
    shape_ptr->outline = thickness > 0.0f ? thickness : 0.0f;
    return true;
}

float wgf_shape2d_get_outline(wgf_node_t shape)
{
    const wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    return shape_ptr != NULL ? shape_ptr->outline : 0.0f;
}

bool wgf_shape2d_set_color(wgf_node_t shape, wgf_color_t color)
{
    wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    if (shape_ptr == NULL) return false;
    shape_ptr->color = color;
    return true;
}

wgf_color_t wgf_shape2d_get_color(wgf_node_t shape)
{
    const wgf_gfx_priv_shape2d_t *shape_ptr = shape_of(shape);
    return shape_ptr != NULL ? shape_ptr->color : 0u;
}
