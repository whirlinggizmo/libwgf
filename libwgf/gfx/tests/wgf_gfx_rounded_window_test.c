#include <stdio.h>
#include <stdlib.h>

#include <GL/gl.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* Rounded rectangles in a window (tools/run_in_xvfb.py, OpenGL on a virtual display): a 48
 * by 48 square at (8, 8) with corners of 16, filled, then outlined 4 wide. Its corner's
 * outside is the clear color where the square's corner would be, inside the arc is drawn,
 * and the outline is a band along the edge and around the arc, its middle clear. The pixels
 * are read back from the frame just drawn. */

#define SIZE 64

static int failures;

/* The pixel at (x, y) from the top-left; GL counts rows from the bottom. */
static void expect_pixel(int x, int y, wgf_color_t color, const char *what)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, SIZE - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (rgba[0] != wgf_color_get_red(color) || rgba[1] != wgf_color_get_green(color) ||
        rgba[2] != wgf_color_get_blue(color)) {
        printf("FAIL: %s: at %d, %d read %d %d %d, expected %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color));
        failures++;
    }
}

static int frames;

static void on_frame(void *user)
{
    (void)user;
    if (++frames < 2) return;
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_draw_rectangle_rounded(8, 8, 48, 48, 16, 0, WGF_COLOR_RED);
    wgf_gfx_priv_end_frame();
    expect_pixel(9, 9, WGF_COLOR_DARKGRAY, "filled: outside the top-left corner's arc, nothing");
    expect_pixel(54, 54, WGF_COLOR_DARKGRAY, "and the bottom-right's");
    expect_pixel(14, 14, WGF_COLOR_RED, "inside the arc, drawn");
    expect_pixel(32, 9, WGF_COLOR_RED, "the top edge between the corners");
    expect_pixel(9, 32, WGF_COLOR_RED, "the left edge");
    expect_pixel(32, 32, WGF_COLOR_RED, "the middle");

    wgf_gfx_priv_begin_frame();
    wgf_draw_rectangle_rounded_lines(8, 8, 48, 48, 16, 0, 4, WGF_COLOR_GREEN);
    wgf_gfx_priv_end_frame();
    expect_pixel(32, 8, WGF_COLOR_GREEN, "outlined: the band along the top edge");
    expect_pixel(12, 12, WGF_COLOR_GREEN, "and around the arc");
    expect_pixel(8, 8, WGF_COLOR_DARKGRAY, "the square's corner outside it");
    expect_pixel(32, 32, WGF_COLOR_DARKGRAY, "its middle clear");
    expect_pixel(32, 14, WGF_COLOR_DARKGRAY, "inside the band");
    exit(failures == 0 ? 0 : 1);
}

int main(void)
{
    wgf_window_set_size(SIZE, SIZE);
    wgf_render_set_clear_color(WGF_COLOR_DARKGRAY);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
