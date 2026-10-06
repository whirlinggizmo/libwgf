#include <stdio.h>
#include <string.h>

#include <GLES3/gl3.h>
#include <emscripten.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_window.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_render.h"

/* Real frames, in a browser (tools/run_in_browser.py), on the page's 64 by 64
 * canvas through app's runtime: frames cleared to stock colors, then immediate mode
 * shapes, and the pixels read back. Headless Chrome renders WebGL2 on the CPU, so
 * this runs with no GPU. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* A frame of the test's own: the runtime's, under way, is ended first (it drew
 * nothing), and the test reads its pixels as soon as it ends, before the browser
 * shows them. */
static void begin_frame(void)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
}

static int read_center(unsigned char rgba[4])
{
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return glGetError() == GL_NO_ERROR;
}

/* The pixel at (x, y) from the top-left, as gfx's coordinates count; GL counts
 * rows from the bottom. */
static void expect_pixel(int x, int y, wgf_color_t color, const char *what)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, 63 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (rgba[0] != wgf_color_get_red(color) || rgba[1] != wgf_color_get_green(color) ||
        rgba[2] != wgf_color_get_blue(color)) {
        printf("FAIL: %s: at %d, %d read %d %d %d, expected %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color));
        failures++;
    }
}

static void shapes(void)
{
    wgf_render_set_clear_color(WGF_COLOR_BLACK);
    begin_frame();
    wgf_draw_rectangle(4, 4, 20, 20, WGF_COLOR_RED);                /* top left */
    wgf_draw_circle(48, 14, 10, WGF_COLOR_GREEN);                   /* top right */
    wgf_draw_triangle(4, 40, 28, 40, 4, 60, WGF_COLOR_BLUE);        /* bottom left */
    wgf_draw_rectangle_lines(36.5f, 36.5f, 20, 20, 1, WGF_COLOR_YELLOW); /* bottom right, an outline */
    wgf_draw_rectangle(10, 10, 4, 4, WGF_COLOR_WHITE);              /* over the red: call order */
    wgf_gfx_priv_end_frame();
    expect_pixel(18, 18, WGF_COLOR_RED, "rectangle");
    expect_pixel(11, 11, WGF_COLOR_WHITE, "a later draw over an earlier one");
    expect_pixel(48, 14, WGF_COLOR_GREEN, "circle, at its center");
    expect_pixel(48, 26, WGF_COLOR_BLACK, "outside the circle");
    expect_pixel(8, 44, WGF_COLOR_BLUE, "triangle");
    expect_pixel(24, 58, WGF_COLOR_BLACK, "outside the triangle's slope");
    expect_pixel(46, 36, WGF_COLOR_YELLOW, "the outline's top edge");
    expect_pixel(46, 46, WGF_COLOR_BLACK, "inside the outline is clear");
    begin_frame();
    {
        /* a thick line, centered on its path, and a concave polygon filled by ear clipping */
        const float notch[12] = {4, 4, 60, 4, 60, 60, 32, 20, 4, 60, 4, 4};
        wgf_draw_polygon(notch, 10, WGF_COLOR_BLUE);
        wgf_draw_line(0, 50, 64, 50, 6, WGF_COLOR_RED);
    }
    wgf_gfx_priv_end_frame();
    expect_pixel(10, 10, WGF_COLOR_BLUE, "a concave polygon: inside");
    expect_pixel(32, 30, WGF_COLOR_BLACK, "and not in its notch");
    expect_pixel(32, 48, WGF_COLOR_RED, "a 6 pixel line: 2 above its path");
    expect_pixel(32, 52, WGF_COLOR_RED, "and 2 below");
    expect_pixel(32, 55, WGF_COLOR_BLACK, "and nothing 5 below");
    expect_pixel(32, 32, WGF_COLOR_BLACK, "the gap between them is clear");
}

static void frame_cleared_to(wgf_color_t color, const char *what)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    wgf_render_set_clear_color(color);
    begin_frame();
    wgf_gfx_priv_end_frame();
    expect(read_center(rgba), "read the pixels back");
    if (rgba[0] != wgf_color_get_red(color) || rgba[1] != wgf_color_get_green(color) ||
        rgba[2] != wgf_color_get_blue(color)) {
        printf("FAIL: %s: read %d %d %d, expected %d %d %d\n", what, rgba[0], rgba[1], rgba[2],
               wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color));
        failures++;
    }
}

static void run_test(void)
{



    frame_cleared_to(WGF_COLOR_SKYBLUE, "cleared to skyblue");
    frame_cleared_to(WGF_COLOR_MAROON, "then to maroon");
    frame_cleared_to(wgf_color_make(1, 2, 3, 255), "then to an exact made color");
    shapes();

    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void on_frame(void *user)
{
    static int frames;
    (void)user;
    if (++frames == 2) run_test(); /* exits */
}

/* The page's canvas, 64 by 64, through app's runtime: the test runs in a frame. */
int main(void)
{
    wgf_window_set_size(64, 64);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
