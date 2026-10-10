#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <GL/gl.h>
#include <stdlib.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_platform_priv.h"
#include "wgf_presentation.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* The presentation in a window (tools/run_in_xvfb.py, OpenGL on a virtual display): a 32
 * by 32 design fitted to a 64 by 32 window (bars left and right), then the window resized
 * to 32 by 64 (bars above and below); the bars their color, the design its clear color,
 * and a square drawn at the design's corner landing at the fitted corner. Then the same
 * design expanded, and fullscreen: the design centered in the screen with bars. The
 * pixels are read back from the frame just drawn. Fullscreen needs a window manager to
 * honor it, which Xvfb has none of: when the window doesn't take the screen's size, that
 * part is said to be skipped, not passed. */

static int failures;
static int height = 32; /* the canvas's, for reading rows from the top */

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The pixel at (x, y) from the top-left; GL counts rows from the bottom. */
static void expect_pixel(int x, int y, wgf_color_t color, const char *what)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (rgba[0] != wgf_color_get_red(color) || rgba[1] != wgf_color_get_green(color) ||
        rgba[2] != wgf_color_get_blue(color)) {
        printf("FAIL: %s: at %d, %d read %d %d %d, expected %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color));
        failures++;
    }
}

/* A frame of the test's own, read back as soon as it ends. */
static void frame_with_square(void)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_draw_rectangle(0, 0, 8, 8, WGF_COLOR_RED); /* the design's top-left corner */
    wgf_draw_rectangle(-20, 12, 10, 8, WGF_COLOR_GREEN); /* left of the design: in a bar, not drawn */
    wgf_gfx_priv_end_frame();
}

static int frames;
static bool fullscreen_next;
static int waited;

static void on_frame(void *user)
{
    (void)user;
    frames++;
    if (fullscreen_next) {
        if (waited == 0) wgf_window_request_fullscreen(true);
        if (++waited < 30 && wgf_platform_priv_get_framebuffer_width() == 32) return;
        if (wgf_platform_priv_get_framebuffer_width() == 32) {
            printf("SKIPPING fullscreen: the window kept its size (no window manager on Xvfb to honor it)\n");
        } else { /* the design centered in the screen, bars around it */
            const int w = wgf_platform_priv_get_framebuffer_width();
            height = wgf_platform_priv_get_framebuffer_height();
            wgf_presentation_set(WGF_PRESENTATION_MODE_FIT, 32, 32);
            frame_with_square();
            expect_pixel(w / 2, height / 2, WGF_COLOR_DARKGRAY, "fullscreen: the design in the screen's middle");
            expect_pixel(2, height / 2, w > height ? WGF_COLOR_BLUE : WGF_COLOR_DARKGRAY,
                         "fullscreen: a bar at the side of a wide screen");
        }
        exit(failures == 0 ? 0 : 1);
    }
    if (frames == 2) { /* 64 by 32: the design at 1:1, 16 pixels of bar either side */
        frame_with_square();
        expect_pixel(4, 16, WGF_COLOR_BLUE, "fit, wide: the left bar, its color");
        expect_pixel(60, 16, WGF_COLOR_BLUE, "and the right bar");
        expect_pixel(30, 20, WGF_COLOR_DARKGRAY, "the design: the clear color");
        expect_pixel(18, 2, WGF_COLOR_RED, "the design's corner, drawn at the fitted corner");
        expect_pixel(12, 14, WGF_COLOR_BLUE, "drawing outside the design isn't in the bar");
        wgf_window_set_size(32, 64); /* a resized window: bars above and below from here */
    } else if (frames >= 10 && wgf_platform_priv_get_framebuffer_width() == 32 && !fullscreen_next) {

        height = 64;
        frame_with_square();
        expect(wgf_platform_priv_get_framebuffer_width() == 32 && wgf_platform_priv_get_framebuffer_height() == 64,
               "the canvas resized");
        expect_pixel(16, 4, WGF_COLOR_BLUE, "fit, tall: the bar above");
        expect_pixel(16, 60, WGF_COLOR_BLUE, "and below");
        expect_pixel(2, 18, WGF_COLOR_RED, "the design's corner, 16 pixels down");
        wgf_presentation_set(WGF_PRESENTATION_MODE_EXPAND, 32, 32);
        frame_with_square();
        expect_pixel(16, 4, WGF_COLOR_DARKGRAY, "expand: no bars, the clear color to the edge");
        expect_pixel(2, 18, WGF_COLOR_RED, "the design where fit had it");
        fullscreen_next = true;
    }
}

int main(void)
{
    wgf_window_set_size(64, 32);
    expect(wgf_presentation_set(WGF_PRESENTATION_MODE_FIT, 32, 32), "fit, 32 by 32");
    wgf_presentation_set_bar_color(WGF_COLOR_BLUE);
    wgf_render_set_clear_color(WGF_COLOR_DARKGRAY);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
