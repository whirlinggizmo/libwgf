#include <stdio.h>
#include <stdlib.h>

#include <GLES3/gl3.h>

#include "wgf_app.h"
#include "wgf_loop.h"
#include "wgf_window.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_render.h"
#include "render/wgf_gfx_render_priv.h"

/* app in a browser (tools/run_in_browser.py): wgf_app_run returns at once and the
 * browser runs the frames, on the page's 64 by 64 canvas. A few frames in, one
 * draws, ends gfx's frame itself so its pixels can be read back before the browser
 * shows them, and quits; shutdown exits with the result. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void expect_pixel(int x, int y, wgf_color_t color, const char *what)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, wgf_render_get_height() - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (rgba[0] != wgf_color_get_red(color) || rgba[1] != wgf_color_get_green(color) ||
        rgba[2] != wgf_color_get_blue(color)) {
        printf("FAIL: %s: at %d, %d read %d %d %d, expected %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color));
        failures++;
    }
}

typedef struct run_t {
    int inits, frames;
    int returned; /* wgf_app_run came back before any frame */
} run_t;

static void on_init(void *user)
{
    run_t *run = user;
    run->inits++;
    wgf_render_set_clear_color(WGF_COLOR_MAROON);
}

static void on_frame(void *user)
{
    run_t *run = user;
    if (++run->frames < 5) return;
    expect(run->returned, "wgf_app_run returned before the first frame");
    expect(run->inits == 1, "init ran once, before the frames");
    expect(wgf_render_get_width() == 64 && wgf_render_get_height() == 64, "frames are the canvas's size");
    expect(wgf_window_get_width() == 64, "the window is the canvas");
    expect(wgf_loop_get_frame_delta() > 0.0f, "frames have a delta");
    wgf_draw_rectangle(8, 8, 16, 16, WGF_COLOR_GOLD);
    wgf_gfx_priv_end_frame();
    expect_pixel(16, 16, WGF_COLOR_GOLD, "the rectangle");
    expect_pixel(48, 48, WGF_COLOR_MAROON, "cleared to the clear color");
    wgf_app_quit();
}

static void on_shutdown(void *user)
{
    run_t *run = user;
    expect(run->frames == 5, "no frame after quitting");
    exit(failures == 0 ? 0 : 1);
}

int main(void)
{
    static run_t run;
    expect(wgf_app_run(on_init, NULL, on_frame, on_shutdown, &run), "runs");
    run.returned = 1;
    return 0;
}
