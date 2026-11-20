#include "wgf_draw.h"

#include <math.h>

#include "draw/wgf_gfx_rounded_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_render.h"

/* Rounded rectangles, raylib's DrawRectangleRounded and DrawRectangleRoundedLinesEx: the
 * rectangle's outline with its corners' arcs, filled as a fan from its middle (it is
 * convex), or outlined as the band between that outline grown and shrunk by half the
 * thickness, centered on the edge as immediate mode's other outlines are. A corner radius
 * in logical pixels, where raylib's is a share of the shorter side. Its own file, so a
 * program drawing none links none of it; a 2D shape's corner radius reads the outline
 * too. */

#define HALF_PI 1.57079632679489661923f
#define PI 3.14159265358979323846f
#define CIRCLE_TOLERANCE 0.25f /* pixels a corner's segments may fall inside its curve */

float wgf_gfx_priv_rounded_radius(float width, float height, float radius)
{
    const float shorter = (width < height ? width : height) * 0.5f;
    if (!(radius > 0.0f) || !(shorter > 0.0f)) return 0.0f; /* NaN too */
    return radius < shorter ? radius : shorter;
}

int wgf_gfx_priv_rounded_segments(float pixels)
{
    int segments = 1;
    if (pixels > CIRCLE_TOLERANCE) {
        /* the angle whose chord sags the tolerance, as wgf_draw_circle's */
        const float step = 2.0f * sqrtf(2.0f * CIRCLE_TOLERANCE / pixels);
        segments = step > 0.0f ? (int)ceilf(HALF_PI / step) : WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS;
    }
    return segments < 1 ? 1 : (segments > WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS ? WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS : segments);
}

int wgf_gfx_priv_rounded_points(float x, float y, float width, float height, float radius, int segments, float *out)
{
    const float r = wgf_gfx_priv_rounded_radius(width, height, radius);
    const float w = width > 0.0f ? width : 0.0f, h = height > 0.0f ? height : 0.0f;
    /* the corners' centers, and the angle each arc starts at (y down: clockwise on the screen) */
    const float cx[4] = {x + r, x + w - r, x + w - r, x + r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
    const float start[4] = {PI, PI + HALF_PI, 0.0f, HALF_PI};
    int corner, i, n = 0;
    if (segments < 1) segments = 1;
    if (segments > WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS) segments = WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS;
    for (corner = 0; corner < 4; corner++) {
        for (i = 0; i <= segments; i++) {
            const float a = start[corner] + HALF_PI * (float)i / (float)segments;
            out[2 * n] = cx[corner] + cosf(a) * r;
            out[2 * n + 1] = cy[corner] + sinf(a) * r;
            n++;
        }
    }
    return n;
}

static void set_color(wgf_color_t color)
{
    sgl_c4b((uint8_t)wgf_color_get_red(color), (uint8_t)wgf_color_get_green(color),
            (uint8_t)wgf_color_get_blue(color), (uint8_t)wgf_color_get_alpha(color));
}

/* A corner's segments: as asked, or (0 or less) enough for its size on the frame. */
static int segments_for(float radius, int segments)
{
    if (segments > 0) return segments > WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS ? WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS : segments;
    return wgf_gfx_priv_rounded_segments(radius * wgf_render_get_dpi_scale());
}

void wgf_draw_rectangle_rounded(float x, float y, float width, float height, float radius, int segments,
                                wgf_color_t color)
{
    float points[8 * (WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS + 1)];
    const float r = wgf_gfx_priv_rounded_radius(width, height, radius);
    const float mx = x + width * 0.5f, my = y + height * 0.5f;
    int n, i;
    if (!wgf_gfx_priv_is_in_frame()) return;
    if (r <= 0.0f) { /* square corners: the plain rectangle */
        wgf_draw_rectangle(x, y, width, height, color);
        return;
    }
    n = wgf_gfx_priv_rounded_points(x, y, width, height, r, segments_for(r, segments), points);
    sgl_begin_triangles();
    set_color(color);
    for (i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        sgl_v2f(mx, my);
        sgl_v2f(points[2 * i], points[2 * i + 1]);
        sgl_v2f(points[2 * j], points[2 * j + 1]);
    }
    sgl_end();
}

void wgf_draw_rectangle_rounded_lines(float x, float y, float width, float height, float radius, int segments,
                                      float thickness, wgf_color_t color)
{
    float outer[8 * (WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS + 1)], inner[8 * (WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS + 1)];
    const float r = wgf_gfx_priv_rounded_radius(width, height, radius);
    const float half = (thickness > 0.0f ? thickness : 1.0f) * 0.5f;
    float ix = x + half, iy = y + half, iw = width - 2.0f * half, ih = height - 2.0f * half;
    int n, i, s;
    if (!wgf_gfx_priv_is_in_frame()) return;
    if (r <= 0.0f) {
        wgf_draw_rectangle_lines(x, y, width, height, thickness, color);
        return;
    }
    if (iw < 0.0f) { /* thicker than the rectangle: the inside a line down its middle */
        ix = x + width * 0.5f;
        iw = 0.0f;
    }
    if (ih < 0.0f) {
        iy = y + height * 0.5f;
        ih = 0.0f;
    }
    s = segments_for(r + half, segments);
    /* the edge grown and shrunk by half the thickness, its arcs about the same centers */
    n = wgf_gfx_priv_rounded_points(x - half, y - half, width + 2.0f * half, height + 2.0f * half, r + half, s, outer);
    wgf_gfx_priv_rounded_points(ix, iy, iw, ih, r - half, s, inner);
    sgl_begin_triangles();
    set_color(color);
    for (i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        sgl_v2f(inner[2 * i], inner[2 * i + 1]);
        sgl_v2f(outer[2 * i], outer[2 * i + 1]);
        sgl_v2f(outer[2 * j], outer[2 * j + 1]);
        sgl_v2f(inner[2 * i], inner[2 * i + 1]);
        sgl_v2f(outer[2 * j], outer[2 * j + 1]);
        sgl_v2f(inner[2 * j], inner[2 * j + 1]);
    }
    sgl_end();
}
