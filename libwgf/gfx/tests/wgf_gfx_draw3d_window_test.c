#include <stdio.h>

#include <GL/gl.h>
#include <stdlib.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_presentation.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* Immediate mode in 3D, in a window (tools/run_in_xvfb.py, OpenGL on a virtual display),
 * its pixels read back: a red cube in front of a camera, a larger green one behind it drawn
 * after it and hidden by it where they overlap (the depth test), and 2D drawn after the
 * 3D over both. Then under a presentation (FIT, 32 by 32 on 64 by 32): the 3D fills the
 * design area, centered between the bars. */

static int failures;
static wgf_actor_t camera;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The pixel at (x, y) from the top-left of the 32-pixel-high canvas. */
static void expect_pixel(int x, int y, wgf_color_t color, const char *what)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, 31 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (rgba[0] != wgf_color_get_red(color) || rgba[1] != wgf_color_get_green(color) ||
        rgba[2] != wgf_color_get_blue(color)) {
        printf("FAIL: %s: at %d, %d read %d %d %d, expected %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color));
        failures++;
    }
}

static void frame_3d(void)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    expect(wgf_draw_begin_3d(camera), "begin_3d");
    wgf_draw_cube(0, 0, 0, 1, 1, 1, WGF_COLOR_RED);
    wgf_draw_cube(0, 0, -3, 6, 6, 1, WGF_COLOR_GREEN); /* behind, after: hidden behind the red */
    wgf_draw_end_3d();
    wgf_draw_rectangle(0, 0, 2, 2, WGF_COLOR_WHITE); /* 2D after it, over it */
    wgf_gfx_priv_end_frame();
}

static int frames;

static void on_frame(void *user)
{
    (void)user;
    if (++frames != 2) return;
    /* 64 by 32, no presentation: the camera's view is the canvas */
    frame_3d();
    expect_pixel(32, 16, WGF_COLOR_RED, "the near cube, in the middle");
    expect_pixel(40, 6, WGF_COLOR_GREEN, "the far one around it");
    expect_pixel(1, 1, WGF_COLOR_WHITE, "2D drawn after, over the 3D");
    expect_pixel(62, 16, WGF_COLOR_DARKGRAY, "past the far cube: the clear color");
    /* FIT 32 by 32 on 64 by 32: the view is the design area, 16 pixels of bar either side */
    wgf_presentation_set(WGF_PRESENTATION_MODE_FIT, 32, 32);
    frame_3d();
    expect_pixel(32, 16, WGF_COLOR_RED, "fit: the near cube in the design's middle");
    expect_pixel(4, 16, WGF_COLOR_BLUE, "fit: the bar, nothing 3D in it");
    expect_pixel(17, 1, WGF_COLOR_WHITE, "fit: 2D at the design's corner");
    exit(failures == 0 ? 0 : 1);
}

int main(void)
{
    wgf_window_set_size(64, 32);
    wgf_render_set_clear_color(WGF_COLOR_DARKGRAY);
    wgf_presentation_set_bar_color(WGF_COLOR_BLUE);
    camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(camera, 0.9f);
    wgf_actor_set_position(camera, 0.0f, 0.0f, 5.0f);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
