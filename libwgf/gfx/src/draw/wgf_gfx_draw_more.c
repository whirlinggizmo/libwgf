#include "wgf_draw.h"

#include <math.h>
#include <stdlib.h>

#include "render/wgf_gfx_render_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_render.h"

/* More of immediate mode's 2D, raylib's that libwgf lacked (the parity checklist): rings,
 * sectors, and arcs, ellipses, gradients, a texture turned about a pivot, and a texture
 * filled up to an amount (a cooldown's sweep, a health bar). A file of its own, so a
 * program drawing none of them links none of it. Angles are radians from +x, toward +y
 * (clockwise on the screen, as y runs down). */

#define TAU 6.28318530717958647692f

static void set_color(wgf_color_t color)
{
    sgl_c4b((uint8_t)wgf_color_get_red(color), (uint8_t)wgf_color_get_green(color),
            (uint8_t)wgf_color_get_blue(color), (uint8_t)wgf_color_get_alpha(color));
}

/* Segments for `radians` of a curve of `radius` logical pixels: about one every 3 pixels of
 * its length on the frame's pixels, 4 to 512. */
static int segments_for(float radius, float radians)
{
    const float pixels = fabsf(radius * radians) * wgf_render_get_dpi_scale();
    const int n = (int)ceilf(pixels / 3.0f);
    return n < 4 ? 4 : n > 512 ? 512 : n;
}

void wgf_draw_ring(float center_x, float center_y, float inner_radius, float outer_radius, float start, float end,
                   wgf_color_t color)
{
    const float sweep = end - start;
    int n, i;
    if (!wgf_gfx_priv_is_in_frame() || outer_radius <= 0.0f || sweep == 0.0f) return;
    if (inner_radius < 0.0f) inner_radius = 0.0f;
    n = segments_for(outer_radius, sweep);
    sgl_begin_triangles();
    set_color(color);
    for (i = 0; i < n; i++) {
        const float a0 = start + sweep * (float)i / (float)n, a1 = start + sweep * (float)(i + 1) / (float)n;
        const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        sgl_v2f(center_x + c0 * inner_radius, center_y + s0 * inner_radius);
        sgl_v2f(center_x + c0 * outer_radius, center_y + s0 * outer_radius);
        sgl_v2f(center_x + c1 * outer_radius, center_y + s1 * outer_radius);
        if (inner_radius > 0.0f) {
            sgl_v2f(center_x + c0 * inner_radius, center_y + s0 * inner_radius);
            sgl_v2f(center_x + c1 * outer_radius, center_y + s1 * outer_radius);
            sgl_v2f(center_x + c1 * inner_radius, center_y + s1 * inner_radius);
        }
    }
    sgl_end();
}

void wgf_draw_arc(float center_x, float center_y, float radius, float start, float end, float thickness,
                  wgf_color_t color)
{
    const float h = (thickness > 0.0f ? thickness : 1.0f) * 0.5f;
    wgf_draw_ring(center_x, center_y, radius - h, radius + h, start, end, color);
}

void wgf_draw_ellipse(float center_x, float center_y, float radius_x, float radius_y, wgf_color_t color)
{
    const int n = segments_for(radius_x > radius_y ? radius_x : radius_y, TAU);
    int i;
    if (!wgf_gfx_priv_is_in_frame() || radius_x <= 0.0f || radius_y <= 0.0f) return;
    sgl_begin_triangles();
    set_color(color);
    for (i = 0; i < n; i++) {
        const float a0 = TAU * (float)i / (float)n, a1 = TAU * (float)(i + 1) / (float)n;
        sgl_v2f(center_x, center_y);
        sgl_v2f(center_x + cosf(a0) * radius_x, center_y + sinf(a0) * radius_y);
        sgl_v2f(center_x + cosf(a1) * radius_x, center_y + sinf(a1) * radius_y);
    }
    sgl_end();
}

void wgf_draw_ellipse_lines(float center_x, float center_y, float radius_x, float radius_y, float thickness,
                            wgf_color_t color)
{
    const int n = segments_for(radius_x > radius_y ? radius_x : radius_y, TAU);
    const float h = (thickness > 0.0f ? thickness : 1.0f) * 0.5f;
    int i;
    if (!wgf_gfx_priv_is_in_frame() || radius_x <= 0.0f || radius_y <= 0.0f) return;
    sgl_begin_triangles();
    set_color(color);
    for (i = 0; i < n; i++) {
        const float a0 = TAU * (float)i / (float)n, a1 = TAU * (float)(i + 1) / (float)n;
        const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        const float ix0 = center_x + c0 * (radius_x - h), iy0 = center_y + s0 * (radius_y - h);
        const float ox0 = center_x + c0 * (radius_x + h), oy0 = center_y + s0 * (radius_y + h);
        const float ix1 = center_x + c1 * (radius_x - h), iy1 = center_y + s1 * (radius_y - h);
        const float ox1 = center_x + c1 * (radius_x + h), oy1 = center_y + s1 * (radius_y + h);
        sgl_v2f(ix0, iy0), sgl_v2f(ox0, oy0), sgl_v2f(ox1, oy1);
        sgl_v2f(ix0, iy0), sgl_v2f(ox1, oy1), sgl_v2f(ix1, iy1);
    }
    sgl_end();
}

void wgf_draw_rectangle_gradient(float x, float y, float width, float height, wgf_color_t top_left,
                                 wgf_color_t top_right, wgf_color_t bottom_right, wgf_color_t bottom_left)
{
    if (!wgf_gfx_priv_is_in_frame()) return;
    sgl_begin_quads();
    set_color(top_left);
    sgl_v2f(x, y);
    set_color(top_right);
    sgl_v2f(x + width, y);
    set_color(bottom_right);
    sgl_v2f(x + width, y + height);
    set_color(bottom_left);
    sgl_v2f(x, y + height);
    sgl_end();
}

void wgf_draw_circle_gradient(float center_x, float center_y, float radius, wgf_color_t inner, wgf_color_t outer)
{
    const int n = segments_for(radius, TAU);
    int i;
    if (!wgf_gfx_priv_is_in_frame() || radius <= 0.0f) return;
    sgl_begin_triangles();
    for (i = 0; i < n; i++) {
        const float a0 = TAU * (float)i / (float)n, a1 = TAU * (float)(i + 1) / (float)n;
        set_color(inner);
        sgl_v2f(center_x, center_y);
        set_color(outer);
        sgl_v2f(center_x + cosf(a0) * radius, center_y + sinf(a0) * radius);
        sgl_v2f(center_x + cosf(a1) * radius, center_y + sinf(a1) * radius);
    }
    sgl_end();
}

/* The texture's region and its uvs: what wgf_draw_texture_region draws. False when it
 * draws nothing (PENDING, or no texture). */
static bool region_of(wgf_texture_t texture, float source[4], float uv[4], sg_view *view, sg_sampler *sampler)
{
    int tw, th;
    bool placeholder;
    if (!wgf_gfx_priv_is_in_frame() || !wgf_gfx_priv_texture_get_binding(texture, view, sampler, &tw, &th, &placeholder)) {
        return false;
    }
    if (placeholder || source[2] <= 0.0f || source[3] <= 0.0f) {
        source[0] = source[1] = 0.0f;
        source[2] = (float)tw;
        source[3] = (float)th;
    }
    uv[0] = placeholder ? 0.0f : source[0] / (float)tw;
    uv[1] = placeholder ? 0.0f : source[1] / (float)th;
    uv[2] = placeholder ? 1.0f : (source[0] + source[2]) / (float)tw;
    uv[3] = placeholder ? 1.0f : (source[1] + source[3]) / (float)th;
    return true;
}

void wgf_draw_texture_rotated(wgf_texture_t texture, float source_x, float source_y, float source_width,
                              float source_height, float x, float y, float width, float height, float pivot_x,
                              float pivot_y, float radians, wgf_color_t tint)
{
    float source[4] = {source_x, source_y, source_width, source_height}, uv[4];
    float c, s, corners[4][2];
    sg_view view;
    sg_sampler sampler;
    int i;
    if (!region_of(texture, source, uv, &view, &sampler)) return;
    if (width <= 0.0f || height <= 0.0f) width = source[2], height = source[3];
    c = cosf(radians), s = sinf(radians);
    for (i = 0; i < 4; i++) { /* the corners about the pivot (a share of the size), turned, at x, y */
        const float lx = ((i == 1 || i == 2) ? 1.0f - pivot_x : -pivot_x) * width;
        const float ly = (i >= 2 ? 1.0f - pivot_y : -pivot_y) * height;
        corners[i][0] = x + lx * c - ly * s;
        corners[i][1] = y + lx * s + ly * c;
    }
    sgl_enable_texture();
    sgl_texture(view, sampler);
    sgl_begin_quads();
    set_color(tint);
    sgl_v2f_t2f(corners[0][0], corners[0][1], uv[0], uv[1]);
    sgl_v2f_t2f(corners[1][0], corners[1][1], uv[2], uv[1]);
    sgl_v2f_t2f(corners[2][0], corners[2][1], uv[2], uv[3]);
    sgl_v2f_t2f(corners[3][0], corners[3][1], uv[0], uv[3]);
    sgl_end();
    sgl_disable_texture();
}

void wgf_draw_texture_fill(wgf_texture_t texture, float x, float y, float width, float height, float amount,
                           wgf_draw_fill_t fill, wgf_color_t tint)
{
    float source[4] = {0.0f, 0.0f, 0.0f, 0.0f}, uv[4];
    sg_view view;
    sg_sampler sampler;
    if (amount <= 0.0f || (int)fill < WGF_DRAW_FILL_LEFT_TO_RIGHT || fill > WGF_DRAW_FILL_CLOCKWISE) return;
    if (!region_of(texture, source, uv, &view, &sampler)) return;
    if (amount > 1.0f) amount = 1.0f;
    if (width <= 0.0f || height <= 0.0f) width = source[2], height = source[3];
    sgl_enable_texture();
    sgl_texture(view, sampler);
    if (fill == WGF_DRAW_FILL_CLOCKWISE) { /* a sweep from 12 o'clock, clockwise: a fan over the rectangle */
        const float cx = x + width * 0.5f, cy = y + height * 0.5f, r = sqrtf(width * width + height * height);
        const float sweep = TAU * amount, start = -TAU * 0.25f;
        const int n = segments_for(r, sweep);
        int i;
        sgl_begin_triangles();
        set_color(tint);
        for (i = 0; i < n; i++) {
            float p[2][2];
            int k;
            for (k = 0; k < 2; k++) { /* the ray at this angle, cut to the rectangle */
                const float a = start + sweep * (float)(i + k) / (float)n, dx = cosf(a), dy = sinf(a);
                const float tx = dx != 0.0f ? (width * 0.5f) / fabsf(dx) : 1e9f, ty = dy != 0.0f ? (height * 0.5f) / fabsf(dy) : 1e9f;
                const float t = tx < ty ? tx : ty;
                p[k][0] = cx + dx * t;
                p[k][1] = cy + dy * t;
            }
            sgl_v2f_t2f(cx, cy, (uv[0] + uv[2]) * 0.5f, (uv[1] + uv[3]) * 0.5f);
            for (k = 0; k < 2; k++) {
                sgl_v2f_t2f(p[k][0], p[k][1], uv[0] + (uv[2] - uv[0]) * (p[k][0] - x) / width,
                            uv[1] + (uv[3] - uv[1]) * (p[k][1] - y) / height);
            }
        }
        sgl_end();
    } else {
        const bool across = fill == WGF_DRAW_FILL_LEFT_TO_RIGHT;
        const float w = across ? width * amount : width, h = across ? height : height * amount;
        const float top = across ? y : y + height - h;
        const float u1 = across ? uv[0] + (uv[2] - uv[0]) * amount : uv[2];
        const float v0 = across ? uv[1] : uv[3] - (uv[3] - uv[1]) * amount;
        sgl_begin_quads();
        set_color(tint);
        sgl_v2f_t2f(x, top, uv[0], v0);
        sgl_v2f_t2f(x + w, top, u1, v0);
        sgl_v2f_t2f(x + w, top + h, u1, uv[3]);
        sgl_v2f_t2f(x, top + h, uv[0], uv[3]);
        sgl_end();
    }
    sgl_disable_texture();
}

bool wgf_draw_spline(wgf_spline_kind_t kind, const float *points, int count, int steps, float thickness, wgf_color_t color)
{
    const int segments = wgf_spline_get_segment_count(kind, points, count);
    float *line;
    int n, i;
    bool drawn;
    if (segments == 0) return false;
    if (steps <= 0) steps = 24;
    n = segments * steps + 1;
    line = (float *)malloc(sizeof(float) * 2 * (size_t)n);
    if (line == NULL) return false;
    for (i = 0; i < n; i++) {
        const wgf_vec2_t p = wgf_spline_get_point(kind, points, count, (float)i / (float)(n - 1));
        line[2 * i] = p.x;
        line[2 * i + 1] = p.y;
    }
    drawn = wgf_draw_polyline(line, 2 * n, false, thickness, color);
    free(line);
    return drawn;
}
