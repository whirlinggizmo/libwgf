#include <stdio.h>
#include <string.h>

#include "wgf_platform_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_color.h"
#include "wgf_draw.h"

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

    wgf_gfx_priv_stop();
    return failures == 0 ? 0 : 1;
}
