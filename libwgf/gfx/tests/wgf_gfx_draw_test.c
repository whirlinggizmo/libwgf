#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_platform_priv.h"
#include "draw/wgf_gfx_rounded_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_material.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_render.h"

/* Immediate mode on sokol's dummy backend (a headless build): what can be checked
 * without pixels. A draw outside a frame records nothing, and a frame that runs out
 * of room grows it for the next. Pixels: wgf_gfx_frame_web_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-3f;
}

/* A frame at a size and DPI scale: the headless window's, as the runtime begins one. */
static void begin_frame(int width, int height, float dpi_scale)
{
    wgf_platform_priv_set_size(width, height);
    wgf_platform_priv_headless_set_dpi_scale(dpi_scale);
    wgf_gfx_priv_begin_frame();
}

int main(void)
{
    int i, vertices;

    expect(wgf_gfx_priv_start(), "setup");

    wgf_draw_rectangle(0, 0, 10, 10, WGF_COLOR_RED); /* outside a frame */
    begin_frame(640, 480, 1.0f);
    expect(sgl_num_vertices() == 0, "a draw outside a frame records nothing");
    wgf_draw_rectangle(0, 0, 10, 10, WGF_COLOR_RED);
    wgf_draw_rectangle_lines(0, 0, 10, 10, 2, WGF_COLOR_RED);
    wgf_draw_line(0, 0, 10, 10, 0, WGF_COLOR_RED);
    wgf_draw_circle(5, 5, 5, WGF_COLOR_RED);
    wgf_draw_circle_lines(5, 5, 5, 1, WGF_COLOR_RED);
    wgf_draw_triangle(0, 0, 10, 0, 0, 10, WGF_COLOR_RED);
    vertices = sgl_num_vertices();
    expect(vertices > 0, "draws in a frame are recorded");
    {
        int before = sgl_num_vertices();
        wgf_draw_rectangle_lines(0, 0, 10, 10, 2, WGF_COLOR_RED);
        expect(sgl_num_vertices() - before == 4 * 6, "an outline: four bands of two triangles, no overlap");
        before = sgl_num_vertices();
        wgf_draw_line(0, 0, 10, 10, 3, WGF_COLOR_RED);
        expect(sgl_num_vertices() - before == 6, "a line: one quad");
        before = sgl_num_vertices();
        wgf_draw_line(4, 4, 4, 4, 3, WGF_COLOR_RED);
        expect(sgl_num_vertices() == before, "a line of no length: nothing");
    }
    {
        /* polylines and polygons, from the caller's arrays */
        const float square[8] = {0, 0, 10, 0, 10, 10, 0, 10};
        const float spike[6] = {0, 0, 100, 1, 0, 2}; /* a corner too sharp to mitre: bevelled */
        const float star[20] = {50, 0, 61, 35, 98, 35, 68, 57, 79, 91, 50, 70, 21, 91, 32, 57, 2, 35, 39, 35};
        const float same[6] = {5, 5, 5, 5, 5, 5};
        int before = sgl_num_vertices();
        expect(wgf_draw_polyline(square, 8, true, 2, WGF_COLOR_RED) && sgl_num_vertices() - before == 4 * 6,
               "a closed square: four segments of two triangles");
        before = sgl_num_vertices();
        expect(wgf_draw_polyline(square, 8, false, 2, WGF_COLOR_RED) && sgl_num_vertices() - before == 3 * 6,
               "open: three");
        before = sgl_num_vertices();
        expect(wgf_draw_polyline(spike, 6, true, 2, WGF_COLOR_RED) && sgl_num_vertices() - before > 3 * 6,
               "a sharp corner bevelled: a triangle more at it");
        before = sgl_num_vertices();
        expect(wgf_draw_polygon(star, 20, WGF_COLOR_RED) && sgl_num_vertices() - before == 8 * 3,
               "a concave polygon of ten points, ear-clipped into eight triangles");
        before = sgl_num_vertices();
        expect(wgf_draw_polyline(same, 6, false, 1, WGF_COLOR_RED) && sgl_num_vertices() == before,
               "every point the same: nothing to see");
        expect(!wgf_draw_polyline(square, 7, true, 1, WGF_COLOR_RED) && !wgf_draw_polyline(square, 2, true, 1, WGF_COLOR_RED) &&
                   !wgf_draw_polyline(NULL, 8, true, 1, WGF_COLOR_RED),
               "a polyline refuses an odd count, one point, and NULL");
        expect(!wgf_draw_polygon(square, 4, WGF_COLOR_RED) && !wgf_draw_polygon(square, 7, WGF_COLOR_RED) &&
                   !wgf_draw_polygon(NULL, 8, WGF_COLOR_RED),
               "a polygon refuses fewer than three points, an odd count, and NULL");
    }
    {
        /* rounded rectangles: their outline's points, and what is recorded */
        float points[8 * (WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS + 1)];
        float lo_x = 1e9f, lo_y = 1e9f, hi_x = -1e9f, hi_y = -1e9f;
        int n, before;
        n = wgf_gfx_priv_rounded_points(10, 20, 100, 40, 1000, 4, points);
        for (i = 0; i < n; i++) {
            lo_x = points[2 * i] < lo_x ? points[2 * i] : lo_x;
            hi_x = points[2 * i] > hi_x ? points[2 * i] : hi_x;
            lo_y = points[2 * i + 1] < lo_y ? points[2 * i + 1] : lo_y;
            hi_y = points[2 * i + 1] > hi_y ? points[2 * i + 1] : hi_y;
        }
        expect(n == 4 * 5, "four corners of five points each, at four segments a corner");
        expect(near(lo_x, 10) && near(hi_x, 110) && near(lo_y, 20) && near(hi_y, 60), "within its rectangle, touching each side");
        expect(near(wgf_gfx_priv_rounded_radius(100, 40, 1000), 20) && near(points[0], 10) && near(points[1], 40) &&
                   near(points[8], 30) && near(points[9], 20) && near(points[10], 90) && near(points[11], 20),
               "a radius of 1000 clamped to half the shorter side: the left edge's middle to the top's, 20 in");
        expect(near(wgf_gfx_priv_rounded_radius(100, 40, -5), 0) && near(wgf_gfx_priv_rounded_radius(10, 40, 3), 3) &&
                   wgf_gfx_priv_rounded_segments(0.1f) == 1 &&
                   wgf_gfx_priv_rounded_segments(1e6f) == WGF_GFX_PRIV_ROUNDED_MAX_SEGMENTS &&
                   wgf_gfx_priv_rounded_segments(20) < wgf_gfx_priv_rounded_segments(200),
               "a radius below 0 is none, one that fits kept; more segments for a larger corner, within 1..64");
        before = sgl_num_vertices();
        wgf_draw_rectangle_rounded(10, 20, 100, 40, 10, 4, WGF_COLOR_RED);
        expect(sgl_num_vertices() - before == 3 * 4 * 5, "filled: a fan of a triangle a point");
        before = sgl_num_vertices();
        wgf_draw_rectangle_rounded_lines(10, 20, 100, 40, 10, 4, 2, WGF_COLOR_RED);
        expect(sgl_num_vertices() - before == 6 * 4 * 5, "outlined: a quad a point, between the outline grown and shrunk");
        before = sgl_num_vertices();
        wgf_draw_rectangle_rounded(10, 20, 100, 40, 0, 4, WGF_COLOR_RED);
        expect(sgl_num_vertices() - before == 6, "no radius: the plain rectangle");
        before = sgl_num_vertices();
        wgf_draw_rectangle_rounded_lines(10, 20, 100, 40, 0, 4, 2, WGF_COLOR_RED);
        expect(sgl_num_vertices() - before == 4 * 6, "no radius, outlined: the plain outline");
        before = sgl_num_vertices();
        wgf_draw_rectangle_rounded(10, 20, 100, 40, 10, 0, WGF_COLOR_RED);
        expect(sgl_num_vertices() - before > 3 * 4 * 2 && (sgl_num_vertices() - before) % (3 * 4) == 0,
               "segments of 0: enough for its size");
    }
    wgf_gfx_priv_end_frame();

    expect(wgf_gfx_priv_render_get_vertex_capacity() == 65536, "starts with room for 65536 vertices");
    begin_frame(640, 480, 1.0f);
    for (i = 0; i < 20000; i++) wgf_draw_rectangle((float)(i % 600), (float)(i % 400), 4, 4, WGF_COLOR_BLUE);
    wgf_gfx_priv_end_frame();
    expect(wgf_gfx_priv_render_get_vertex_capacity() == 131072, "a frame that ran out doubles the room");
    begin_frame(640, 480, 1.0f);
    for (i = 0; i < 20000; i++) wgf_draw_rectangle((float)(i % 600), (float)(i % 400), 4, 4, WGF_COLOR_BLUE);
    expect(sgl_num_vertices() >= 20000 * 6, "so the next frame holds every draw");
    wgf_gfx_priv_end_frame();
    expect(wgf_gfx_priv_render_get_vertex_capacity() == 131072, "and it doesn't grow again");
    expect(wgf_render_get_draw_calls() >= 1 && wgf_render_get_triangle_count() >= 20000 * 2,
           "the frame's draw calls and triangles counted");

    { /* blending, a textured polygon, a tiled draw; the GPU's bytes */
        const double before = wgf_render_get_gpu_bytes();
        const wgf_texture_t blank = wgf_texture_create_blank(64, 64);
        const float points[6] = {0, 0, 10, 0, 0, 10}, uvs[6] = {0, 0, 1, 0, 0, 1};
        expect(wgf_render_get_gpu_bytes() >= before + 64 * 64 * 4, "a texture made: the GPU's bytes up by its size");
        begin_frame(640, 480, 1.0f);
        expect(wgf_draw_get_blend_mode() == WGF_ALPHA_MODE_BLEND && wgf_draw_set_blend_mode(WGF_ALPHA_MODE_MULTIPLY) &&
                   wgf_draw_get_blend_mode() == WGF_ALPHA_MODE_MULTIPLY && wgf_draw_set_blend_mode(WGF_ALPHA_MODE_MASK) &&
                   wgf_draw_get_blend_mode() == WGF_ALPHA_MODE_BLEND && !wgf_draw_set_blend_mode((wgf_alpha_mode_t)9),
               "immediate mode's blend mode (MASK drawn as BLEND); one that isn't refused");
        vertices = sgl_num_vertices();
        expect(wgf_draw_texture_polygon(blank, points, 6, uvs, 6, WGF_COLOR_WHITE) && sgl_num_vertices() == vertices + 3,
               "a textured triangle");
        expect(!wgf_draw_texture_polygon(blank, points, 4, uvs, 4, WGF_COLOR_WHITE) &&
                   !wgf_draw_texture_polygon(blank, points, 6, uvs, 4, WGF_COLOR_WHITE),
               "too few points, or coordinates not one a point: refused");
        vertices = sgl_num_vertices();
        wgf_draw_texture_tiled(blank, 0, 0, 64, 64, 0, 0, 100, 70, 1.0f, WGF_COLOR_WHITE);
        expect(sgl_num_vertices() >= vertices + 4 * 6, "tiled: two across and two down, the last cut");
        wgf_draw_set_blend_mode(WGF_ALPHA_MODE_BLEND);
        wgf_gfx_priv_end_frame();
        wgf_resource_release(blank);
    }

    wgf_gfx_priv_stop();
    return failures == 0 ? 0 : 1;
}
