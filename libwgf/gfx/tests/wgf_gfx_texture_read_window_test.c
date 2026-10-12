#include <stdio.h>
#include <stdlib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define finish emscripten_force_exit
#else
#define finish exit
#endif

#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Render targets read back (wgf_texture_read) over the runtime's own frames, in a window
 * (tools/run_in_xvfb.py, OpenGL on a virtual display) with MSAA, so what is read is the
 * image the samples resolve into; wgf_gfx_texture_read_web_test is this file on WebGL2,
 * without MSAA, so a target drawn into directly. A 32 by 24 target, dark gray, with a yellow square at its top-left and a green
 * one at its bottom-right: a read is PENDING in its frame and DONE in the next, its rows
 * the top first; a read of another target released in its frame still reads it, the read
 * holding it; and one destroyed while PENDING is gone. */

#define WIDTH 32
#define HEIGHT 24

static int failures;
static int frames;
static wgf_texture_t target, blue;
static wgf_texture_read_t read, blue_read;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The pixel at (x, y) from the top-left of a read, exactly the color's. */
static void expect_pixel(wgf_texture_read_t r, int x, int y, wgf_color_t color, const char *what)
{
    const unsigned char *rgba = wgf_texture_read_get_data(r) + ((size_t)y * (size_t)wgf_texture_read_get_width(r) + (size_t)x) * 4u;
    if (rgba[0] != wgf_color_get_red(color) || rgba[1] != wgf_color_get_green(color) ||
        rgba[2] != wgf_color_get_blue(color) || rgba[3] != wgf_color_get_alpha(color)) {
        printf("FAIL: %s: at %d, %d read %d %d %d %d, expected %d %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               rgba[3], wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color),
               wgf_color_get_alpha(color));
        failures++;
    }
}

static void on_frame(void *user)
{
    wgf_texture_read_t dropped;
    (void)user;
    frames++;
    if (frames == 2) { /* the first frame's drawing may wait on the window */
        wgf_render_begin_target(target);
        wgf_draw_rectangle(0, 0, 8, 8, WGF_COLOR_YELLOW);
        wgf_draw_rectangle(WIDTH - 8, HEIGHT - 8, 8, 8, WGF_COLOR_GREEN);
        wgf_render_end_target();
        wgf_render_begin_target(blue);
        wgf_render_end_target(); /* cleared to its color */
        read = wgf_texture_read(target);
        expect(wgf_texture_read_get_status(read) == WGF_TEXTURE_READ_STATUS_PENDING, "PENDING in its frame");
        expect(wgf_texture_read_get_data(read) == NULL && wgf_texture_read_get_width(read) == 0,
               "no pixels while PENDING");
        blue_read = wgf_texture_read(blue);
        wgf_resource_release(blue); /* the read holds it */
        dropped = wgf_texture_read(target);
        expect(wgf_texture_read_destroy(dropped), "one destroyed PENDING");
    } else if (frames == 3) {
        expect(wgf_texture_read_get_status(read) == WGF_TEXTURE_READ_STATUS_DONE, "DONE in the next frame");
        expect(wgf_texture_read_get_width(read) == WIDTH && wgf_texture_read_get_height(read) == HEIGHT &&
                   wgf_texture_read_get_size(read) == WIDTH * HEIGHT * 4,
               "its size, and 4 bytes a pixel");
        if (wgf_texture_read_get_data(read) != NULL) {
            expect_pixel(read, 2, 2, WGF_COLOR_YELLOW, "the square at the top-left, in the first rows");
            expect_pixel(read, WIDTH - 2, HEIGHT - 2, WGF_COLOR_GREEN, "the square at the bottom-right, in the last");
            expect_pixel(read, WIDTH / 2, HEIGHT / 2, WGF_COLOR_DARKGRAY, "the clear color between");
        }
        expect(wgf_texture_read_get_status(blue_read) == WGF_TEXTURE_READ_STATUS_DONE,
               "a target released in its read's frame still read");
        if (wgf_texture_read_get_data(blue_read) != NULL) {
            expect_pixel(blue_read, 4, 4, WGF_COLOR_BLUE, "what it was cleared to");
        }
        expect(!wgf_handle_is_alive(blue), "and let go of once read");
        expect(wgf_texture_read_destroy(read) && wgf_texture_read_destroy(blue_read), "destroyed");
        finish(failures == 0 ? 0 : 1);
    }
}

static void init(void *user)
{
    (void)user;
    target = wgf_texture_create_target(WIDTH, HEIGHT);
    wgf_render_set_target_clear_color(target, WGF_COLOR_DARKGRAY);
    blue = wgf_texture_create_target(8, 8);
    wgf_render_set_target_clear_color(blue, WGF_COLOR_BLUE);
}

int main(void)
{
    wgf_window_set_size(64, 32);
#ifndef __EMSCRIPTEN__
    wgf_window_set_msaa(true);
#endif
    wgf_app_run(init, NULL, on_frame, NULL, NULL);
    return 0;
}
