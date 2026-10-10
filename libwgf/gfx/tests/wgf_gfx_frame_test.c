#include <stdio.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_platform_priv.h"
#include "wgf.h"
#include "wgf_color.h"
#include "wgf_render.h"

/* gfx's start and frames on sokol's dummy backend (a headless build): a frame takes
 * the window's size and DPI scale, as the platform reports them; gfx stops, ending
 * a frame under way, and starts again. No GPU, so no pixels: those are checked in a
 * browser. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    int i;

    wgf_core_priv_init();
    expect(wgf_render_get_width() == 0 && wgf_render_get_dpi_scale() == 1.0f, "before a frame: 0 by 0, scale 1");
    expect(wgf_render_get_clear_color() == WGF_COLOR_BLACK, "clears to black by default");
    wgf_render_set_clear_color(WGF_COLOR_SKYBLUE);
    expect(wgf_render_get_clear_color() == WGF_COLOR_SKYBLUE, "the clear color reads back");

    wgf_gfx_priv_begin_frame();
    expect(!wgf_gfx_priv_is_in_frame(), "no frame before gfx starts");
    expect(wgf_gfx_priv_start(), "start");
    expect(wgf_gfx_priv_start(), "start again while running does nothing, and says it runs");
    for (i = 0; i < 10; i++) {
        wgf_platform_priv_set_size(320 + i, 200);
        wgf_platform_priv_headless_set_dpi_scale(2.0f);
        wgf_gfx_priv_begin_frame();
        expect(wgf_gfx_priv_is_in_frame(), "in a frame");
        expect(wgf_render_get_width() == 320 + i && wgf_render_get_height() == 200, "the window's size");
        expect(wgf_render_get_dpi_scale() == 2.0f, "the window's scale");
        wgf_platform_priv_set_size(10, 10);
        wgf_gfx_priv_begin_frame();
        expect(wgf_render_get_width() == 320 + i, "begin inside a frame is ignored");
        wgf_gfx_priv_end_frame();
        expect(!wgf_gfx_priv_is_in_frame(), "out of the frame");
    }

    /* the clip stack: nested pushes intersect, a pop goes back, the frame's end drops
       what is left */
    wgf_platform_priv_set_size(400, 300);
    wgf_platform_priv_headless_set_dpi_scale(1.0f);
    {
        float x, y, w, h;
        wgf_gfx_priv_begin_frame();
        expect(!wgf_gfx_priv_render_get_clip(&x, &y, &w, &h) && w == 400.0f && h == 300.0f,
               "no clip: the whole frame");
        wgf_render_push_clip(10, 20, 100, 100);
        wgf_render_push_clip(50, 0, 200, 50);
        expect(wgf_gfx_priv_render_get_clip(&x, &y, &w, &h) && x == 50.0f && y == 20.0f && w == 60.0f && h == 30.0f,
               "a clip inside another is their intersection");
        wgf_render_push_clip(500, 500, 10, 10);
        expect(wgf_gfx_priv_render_get_clip(&x, &y, &w, &h) && w == 0.0f && h == 0.0f,
               "one outside its parent clips everything away");
        wgf_render_pop_clip();
        wgf_render_pop_clip();
        expect(wgf_gfx_priv_render_get_clip(&x, &y, &w, &h) && x == 10.0f && w == 100.0f, "popped back");
        wgf_gfx_priv_end_frame(); /* one left pushed: dropped, warned */
        wgf_gfx_priv_begin_frame();
        expect(!wgf_gfx_priv_render_get_clip(&x, &y, &w, &h), "the next frame starts unclipped");
        wgf_render_pop_clip(); /* without a push: warned, nothing changes */
        for (int n = 0; n < 40; n++) wgf_render_push_clip(0, 0, 100, 100);
        for (int n = 0; n < 40; n++) wgf_render_pop_clip();
        expect(!wgf_gfx_priv_render_get_clip(&x, &y, &w, &h), "pushes past 32 deep pop back to none");
        wgf_gfx_priv_end_frame();
    }

    wgf_gfx_priv_begin_frame();
    wgf_gfx_priv_stop();
    expect(!wgf_gfx_priv_is_in_frame() && wgf_render_get_width() == 0, "stopping ends the frame");
    wgf_gfx_priv_stop();
    expect(wgf_gfx_priv_start(), "and start again");
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
