#include "wgf_draw.h"

#include <math.h>
#include <stdlib.h>

#include "render/wgf_gfx_render_priv.h"
#include "text/wgf_gfx_font_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_render.h"

/* Immediate mode 2D shapes, recorded into the frame's sokol_gl context as triangles:
 * libwgt's drew lines through GL's own lines, one pixel wide whatever was asked;
 * libwgf's have a thickness, for vector art (docs/HISTORY.md, "gfx, 2D"). */

#define TAU 6.28318530717958647692f
#define MITER_LIMIT 4.0f      /* a miter reaching past this many half thicknesses is bevelled */
#define CIRCLE_TOLERANCE 0.25f /* pixels a circle's segments may fall inside its curve */

static void set_color(wgf_color_t color)
{
    sgl_c4b((uint8_t)wgf_color_get_red(color), (uint8_t)wgf_color_get_green(color),
            (uint8_t)wgf_color_get_blue(color), (uint8_t)wgf_color_get_alpha(color));
}

/* Half a line's width, in logical pixels: a hairline is one pixel. */
static float half_width(float thickness)
{
    return (thickness > 0.0f ? thickness : 1.0f) * 0.5f;
}

/* How many segments a circle of `radius` logical pixels needs to look round: each
 * falls at most CIRCLE_TOLERANCE pixels inside the curve, on the frame's pixels. */
static int circle_segments(float radius)
{
    const float pixels = radius * wgf_render_get_dpi_scale();
    int segments = 8;
    if (pixels > CIRCLE_TOLERANCE) {
        /* the angle whose chord sags CIRCLE_TOLERANCE: 2 acos(1 - t/r), to within a small
           fraction of a segment for every radius drawn, without acos's code */
        const float step = 2.0f * sqrtf(2.0f * CIRCLE_TOLERANCE / pixels);
        segments = step > 0.0f ? (int)ceilf(TAU / step) : 512;
    }
    return segments < 8 ? 8 : (segments > 512 ? 512 : segments);
}

static void quad(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy)
{
    sgl_v2f(ax, ay);
    sgl_v2f(bx, by);
    sgl_v2f(cx, cy);
    sgl_v2f(ax, ay);
    sgl_v2f(cx, cy);
    sgl_v2f(dx, dy);
}

void wgf_draw_rectangle(float x, float y, float width, float height, wgf_color_t color)
{
    if (!wgf_gfx_priv_is_in_frame()) return;
    sgl_begin_triangles();
    set_color(color);
    quad(x, y, x + width, y, x + width, y + height, x, y + height);
    sgl_end();
}

/* Drawn as its own quads, not a polyline, so a program drawing outlines and lines alone
 * doesn't link the polyline's joins (docs/HISTORY.md, "Same rows, a target"): four bands
 * centered on the edges, meeting at the corners without overlapping, as a mitred closed
 * polyline's are. */
void wgf_draw_rectangle_lines(float x, float y, float width, float height, float thickness, wgf_color_t color)
{
    const float h = half_width(thickness);
    const float x0 = x - h, x1 = x + width + h, y0 = y - h, y1 = y + height + h;
    if (!wgf_gfx_priv_is_in_frame()) return;
    sgl_begin_triangles();
    set_color(color);
    quad(x0, y0, x1, y0, x1, y + h, x0, y + h);                                 /* top */
    quad(x0, y + height - h, x1, y + height - h, x1, y1, x0, y1);               /* bottom */
    quad(x0, y + h, x + h, y + h, x + h, y + height - h, x0, y + height - h);   /* left */
    quad(x1 - 2 * h, y + h, x1, y + h, x1, y + height - h, x1 - 2 * h, y + height - h); /* right */
    sgl_end();
}

/* One quad, its ends flat at the points, as an open polyline of two points draws. */
void wgf_draw_line(float x0, float y0, float x1, float y1, float thickness, wgf_color_t color)
{
    const float dx = x1 - x0, dy = y1 - y0, length = sqrtf(dx * dx + dy * dy), h = half_width(thickness);
    float nx, ny;
    if (!wgf_gfx_priv_is_in_frame() || length == 0.0f) return;
    nx = -dy / length * h;
    ny = dx / length * h;
    sgl_begin_triangles();
    set_color(color);
    quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny);
    sgl_end();
}

void wgf_draw_circle(float center_x, float center_y, float radius, wgf_color_t color)
{
    const int segments = circle_segments(radius);
    int i;
    if (!wgf_gfx_priv_is_in_frame()) return;
    sgl_begin_triangles();
    set_color(color);
    for (i = 0; i < segments; i++) {
        const float a0 = TAU * (float)i / (float)segments, a1 = TAU * (float)(i + 1) / (float)segments;
        sgl_v2f(center_x, center_y);
        sgl_v2f(center_x + cosf(a0) * radius, center_y + sinf(a0) * radius);
        sgl_v2f(center_x + cosf(a1) * radius, center_y + sinf(a1) * radius);
    }
    sgl_end();
}

void wgf_draw_circle_lines(float center_x, float center_y, float radius, float thickness, wgf_color_t color)
{
    const int segments = circle_segments(radius);
    const float h = half_width(thickness);
    const float inner = radius - h > 0.0f ? radius - h : 0.0f, outer = radius + h;
    int i;
    if (!wgf_gfx_priv_is_in_frame()) return;
    sgl_begin_triangles();
    set_color(color);
    for (i = 0; i < segments; i++) {
        const float a0 = TAU * (float)i / (float)segments, a1 = TAU * (float)(i + 1) / (float)segments;
        const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        quad(center_x + c0 * inner, center_y + s0 * inner, center_x + c0 * outer, center_y + s0 * outer,
             center_x + c1 * outer, center_y + s1 * outer, center_x + c1 * inner, center_y + s1 * inner);
    }
    sgl_end();
}

void wgf_draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2, wgf_color_t color)
{
    if (!wgf_gfx_priv_is_in_frame()) return;
    sgl_begin_triangles();
    set_color(color);
    sgl_v2f(x0, y0);
    sgl_v2f(x1, y1);
    sgl_v2f(x2, y2);
    sgl_end();
}

static float cross(const float *p, int a, int b, int c)
{
    return (p[2 * b] - p[2 * a]) * (p[2 * c + 1] - p[2 * a + 1]) - (p[2 * b + 1] - p[2 * a + 1]) * (p[2 * c] - p[2 * a]);
}

/* Whether point `p` is inside triangle a, b, c (wound as `sign` says), or on its edge. */
static bool inside(const float *pts, int a, int b, int c, int p, float sign)
{
    return sign * cross(pts, a, b, p) >= 0.0f && sign * cross(pts, b, c, p) >= 0.0f &&
           sign * cross(pts, c, a, p) >= 0.0f;
}

/* Fill a simple polygon by ear clipping: each corner that turns the polygon's way and
 * holds no other point is a triangle cut off, until one is left. O(n^2), for the small
 * polygons a game draws. False when no ear is found (the edges cross): the caller fans. */
static bool fill_by_ears(const float *pts, int n)
{
    int *left = (int *)malloc(sizeof(int) * (size_t)n);
    float area = 0.0f, sign;
    int remaining = n, i, guard = 0;
    if (left == NULL) return false;
    for (i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        left[i] = i;
        area += pts[2 * i] * pts[2 * j + 1] - pts[2 * j] * pts[2 * i + 1];
    }
    sign = area >= 0.0f ? 1.0f : -1.0f;
    i = 0;
    while (remaining > 3) {
        const int a = left[(i + remaining - 1) % remaining], b = left[i % remaining], c = left[(i + 1) % remaining];
        bool ear = sign * cross(pts, a, b, c) > 0.0f;
        int k;
        for (k = 0; ear && k < remaining; k++) {
            const int p = left[k];
            if (p != a && p != b && p != c && inside(pts, a, b, c, p, sign)) ear = false;
        }
        if (ear) {
            sgl_v2f(pts[2 * a], pts[2 * a + 1]);
            sgl_v2f(pts[2 * b], pts[2 * b + 1]);
            sgl_v2f(pts[2 * c], pts[2 * c + 1]);
            for (k = i % remaining; k < remaining - 1; k++) left[k] = left[k + 1];
            remaining--;
            guard = 0;
        } else {
            i++;
            if (++guard > remaining) { /* round once with no ear: the edges cross */
                free(left);
                return false;
            }
        }
    }
    sgl_v2f(pts[2 * left[0]], pts[2 * left[0] + 1]);
    sgl_v2f(pts[2 * left[1]], pts[2 * left[1] + 1]);
    sgl_v2f(pts[2 * left[2]], pts[2 * left[2] + 1]);
    free(left);
    return true;
}

bool wgf_draw_polygon(const float *points, int count, wgf_color_t color)
{
    if (points == NULL || count < 6 || count % 2 != 0) return false;
    if (!wgf_gfx_priv_is_in_frame()) return true;
    sgl_begin_triangles();
    set_color(color);
    if (!fill_by_ears(points, count / 2)) {
        /* crossing itself, or out of memory: the fan from the first point. The triangles
           an ear-clip cut before it found none are drawn under the fan, which covers them */
        int i;
        for (i = 2; i + 3 < count; i += 2) {
            sgl_v2f(points[0], points[1]);
            sgl_v2f(points[i], points[i + 1]);
            sgl_v2f(points[i + 2], points[i + 3]);
        }
    }
    sgl_end();
    return true;
}

/* A segment's unit normal (its direction turned a quarter, to the left with y down). */
static void normal_of(float ax, float ay, float bx, float by, float *nx, float *ny)
{
    const float dx = bx - ax, dy = by - ay, length = sqrtf(dx * dx + dy * dy);
    *nx = -dy / length;
    *ny = dx / length;
}

/* The polyline's points with repeats dropped, kept between calls and grown as needed,
 * so drawing shapes every frame allocates nothing once warm. The main thread's. */
static float *scratch;
static int scratch_capacity;

bool wgf_draw_polyline(const float *points, int count, bool closed, float thickness, wgf_color_t color)
{
    const float h = half_width(thickness);
    float *p; /* the points, with any repeated one dropped: a segment of no length has no normal */
    int n = 0, segments, i;
    if (points == NULL || count < 4 || count % 2 != 0) return false;
    if (!wgf_gfx_priv_is_in_frame()) return true;
    if (count > scratch_capacity) {
        float *grown = (float *)realloc(scratch, sizeof(float) * (size_t)count);
        if (grown == NULL) return true;
        scratch = grown;
        scratch_capacity = count;
    }
    p = scratch;
    for (i = 0; i < count; i += 2) {
        if (n > 0 && p[n - 2] == points[i] && p[n - 1] == points[i + 1]) continue;
        p[n++] = points[i];
        p[n++] = points[i + 1];
    }
    if (closed && n >= 6 && p[0] == p[n - 2] && p[1] == p[n - 1]) n -= 2; /* the first given again at the end */
    n /= 2;
    if (n < 2) return true; /* every point the same: nothing to see */
    if (n == 2) closed = false; /* there and back is one line */
    segments = closed ? n : n - 1;
    sgl_begin_triangles();
    set_color(color);
    for (i = 0; i < segments; i++) {
        const int a = i, b = (i + 1) % n;
        const float ax = p[2 * a], ay = p[2 * a + 1], bx = p[2 * b], by = p[2 * b + 1];
        float nx, ny, start[4], end[4];
        int k;
        normal_of(ax, ay, bx, by, &nx, &ny);
        /* each end: where its corner's two edges meet (the miter), or this segment's own
           end, bevelled, where they meet too far out or the line ends there */
        for (k = 0; k < 2; k++) {
            const int at = k == 0 ? a : b, other = k == 0 ? (a - 1 + n) % n : (b + 1) % n;
            const bool joined = closed || (k == 0 ? a > 0 : b < n - 1);
            const float px = p[2 * at], py = p[2 * at + 1];
            float *out = k == 0 ? start : end;
            float ox = nx, oy = ny, scale = h;
            if (joined) {
                float mx, my, other_nx, other_ny, d, length;
                if (k == 0) normal_of(p[2 * other], p[2 * other + 1], px, py, &other_nx, &other_ny);
                else normal_of(px, py, p[2 * other], p[2 * other + 1], &other_nx, &other_ny);
                mx = nx + other_nx;
                my = ny + other_ny;
                length = sqrtf(mx * mx + my * my);
                d = length > 0.0f ? (mx * nx + my * ny) / length : 0.0f;
                if (d > 1.0f / MITER_LIMIT) {
                    ox = mx / length;
                    oy = my / length;
                    scale = h / d;
                } else if (k == 1) {
                    /* bevelled: the triangle between this segment's end and the next's start,
                       on the corner's outer side (the inner side overlaps) */
                    const float turn = nx * other_ny - ny * other_nx; /* which way the line turns */
                    const float side = turn > 0.0f ? -1.0f : 1.0f;
                    sgl_v2f(px, py);
                    sgl_v2f(px + side * nx * h, py + side * ny * h);
                    sgl_v2f(px + side * other_nx * h, py + side * other_ny * h);
                }
            }
            out[0] = px + ox * scale;
            out[1] = py + oy * scale;
            out[2] = px - ox * scale;
            out[3] = py - oy * scale;
        }
        quad(start[0], start[1], end[0], end[1], end[2], end[3], start[2], start[3]);
    }
    sgl_end();
    return true;
}

void wgf_draw_texture_region(wgf_texture_t texture, float source_x, float source_y, float source_width,
                             float source_height, float x, float y, float width, float height, wgf_color_t tint)
{
    sg_view view;
    sg_sampler sampler;
    int texture_width, texture_height;
    bool placeholder;
    float u0, v0, u1, v1;

    if (!wgf_gfx_priv_is_in_frame() ||
        !wgf_gfx_priv_texture_get_binding(texture, &view, &sampler, &texture_width, &texture_height, &placeholder)) {
        return;
    }
    if (placeholder || source_width <= 0.0f || source_height <= 0.0f) { /* the whole texture */
        source_x = 0.0f;
        source_y = 0.0f;
        if (!placeholder || source_width <= 0.0f || source_height <= 0.0f) {
            source_width = (float)texture_width;
            source_height = (float)texture_height;
        }
    }
    if (width <= 0.0f || height <= 0.0f) {
        width = source_width;
        height = source_height;
    }
    if (placeholder) { /* the checker fills the rectangle */
        u0 = 0.0f;
        v0 = 0.0f;
        u1 = 1.0f;
        v1 = 1.0f;
    } else {
        u0 = source_x / (float)texture_width;
        v0 = source_y / (float)texture_height;
        u1 = (source_x + source_width) / (float)texture_width;
        v1 = (source_y + source_height) / (float)texture_height;
    }
    sgl_enable_texture();
    sgl_texture(view, sampler);
    sgl_begin_quads();
    set_color(tint);
    sgl_v2f_t2f(x, y, u0, v0);
    sgl_v2f_t2f(x + width, y, u1, v0);
    sgl_v2f_t2f(x + width, y + height, u1, v1);
    sgl_v2f_t2f(x, y + height, u0, v1);
    sgl_end();
    sgl_disable_texture();
}

void wgf_draw_texture(wgf_texture_t texture, float x, float y, float width, float height, wgf_color_t tint)
{
    wgf_draw_texture_region(texture, 0.0f, 0.0f, 0.0f, 0.0f, x, y, width, height, tint);
}

void wgf_draw_text(wgf_font_t font, const char *text, float x, float y, float size, wgf_color_t color)
{
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (!wgf_gfx_priv_is_in_frame()) return;
    matrix[12] = x;
    matrix[13] = y;
    wgf_gfx_priv_font_draw_block(font, text, size, color, 0.0f, WGF_TEXT_HALIGN_LEFT, WGF_TEXT_VALIGN_TOP, matrix);
}

void wgf_gfx_priv_draw_shutdown(void)
{
    free(scratch);
    scratch = NULL;
    scratch_capacity = 0;
}
