#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_color.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_texture.h"

/* Render targets on sokol's dummy backend (a headless build): what makes one and what
 * refuses one, a target's own size and scale while it is drawn into and the screen's
 * after, the clips of each pass, a target's clear color, the passes a frame allows, and a
 * read of one (wgf_texture_read): PENDING until the update after its frame is drawn,
 * FAILED then, and holding its target till then. No GPU, so no pixels: those are
 * wgf_gfx_target_window_test's, and wgf_gfx_texture_read_window_test's. */

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
    wgf_texture_t target, other;
    wgf_texture_read_t read, pending;
    float x, y, w, h;
    int i;

    wgf_core_priv_init();
    expect(wgf_texture_create_target(16, 16) == 0, "no target before gfx runs");
    expect(wgf_gfx_priv_start(), "start");
    expect(wgf_texture_create_target(0, 16) == 0 && wgf_texture_create_target(16, -1) == 0 &&
               wgf_texture_create_target(16385, 16) == 0,
           "no target of a size of 0 or less, or over 16384");
    target = wgf_texture_create_target(64, 32);
    expect(target != 0 && wgf_resource_get_status(target) == WGF_RESOURCE_STATUS_READY, "a target, READY at once");
    expect(wgf_texture_get_width(target) == 64 && wgf_texture_get_height(target) == 32, "its size");
    expect(wgf_render_get_target_clear_color(target) == 0x00000000u, "cleared to transparent black unset");
    expect(wgf_render_set_target_clear_color(target, WGF_COLOR_SKYBLUE) &&
               wgf_render_get_target_clear_color(target) == WGF_COLOR_SKYBLUE,
           "its clear color reads back");
    other = wgf_texture_create("textures/none.png"); /* a texture, but not a target */
    expect(!wgf_render_set_target_clear_color(other, WGF_COLOR_RED) &&
               wgf_render_get_target_clear_color(other) == 0x00000000u,
           "a texture that isn't a target has no clear color");
    expect(!wgf_render_begin_target(target), "no target outside a frame");

    wgf_platform_priv_set_size(400, 300);
    wgf_platform_priv_headless_set_dpi_scale(2.0f);
    wgf_gfx_priv_begin_frame();
    expect(!wgf_render_begin_target(other), "a texture that isn't a target isn't begun");
    expect(!wgf_render_begin_target(0), "nor is 0");
    wgf_render_push_clip(10, 10, 50, 50);
    expect(wgf_render_begin_target(target), "begun in a frame");
    expect(wgf_gfx_priv_render_get_pass() == 1 && wgf_gfx_priv_render_get_target() == target, "its pass");
    expect(wgf_render_get_width() == 64 && wgf_render_get_height() == 32 && wgf_render_get_dpi_scale() == 1.0f,
           "inside it, its size, and a scale of 1");
    wgf_gfx_priv_render_get_visible(&x, &y, &w, &h);
    expect(x == 0.0f && y == 0.0f && w == 64.0f && h == 32.0f, "what is visible: the whole target");
    expect(!wgf_gfx_priv_render_get_clip(&x, &y, &w, &h) && w == 64.0f, "unclipped by the screen's clip");
    expect(!wgf_render_begin_target(target), "not nested");
    wgf_render_push_clip(0, 0, 8, 8);
    wgf_render_end_target(); /* its clip left pushed: dropped, warned */
    expect(wgf_gfx_priv_render_get_pass() == 0 && wgf_gfx_priv_render_get_target() == 0, "the screen again");
    expect(wgf_render_get_width() == 400 && wgf_render_get_dpi_scale() == 2.0f, "the screen's size and scale again");
    expect(wgf_gfx_priv_render_get_clip(&x, &y, &w, &h) && x == 10.0f && w == 50.0f, "the screen's clip again");
    wgf_render_pop_clip();
    wgf_render_end_target(); /* not in one: warned */
    expect(wgf_render_begin_target(target), "begun again in the same frame");
    wgf_gfx_priv_end_frame(); /* inside it: ended, warned */
    expect(!wgf_gfx_priv_is_in_frame() && wgf_gfx_priv_render_get_pass() == 0, "a frame's end ends a target");

    wgf_gfx_priv_begin_frame();
    expect(wgf_render_get_width() == 400 && wgf_gfx_priv_render_get_pass() == 0, "the next frame starts on the screen");
    for (i = 0; i < 16; i++) {
        expect(wgf_render_begin_target(target), "16 target passes a frame");
        wgf_render_end_target();
    }
    expect(!wgf_render_begin_target(target), "not a 17th");
    wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    expect(wgf_render_begin_target(target), "16 again the next frame");
    wgf_render_end_target();
    wgf_gfx_priv_end_frame();

    expect(wgf_texture_read(other) == 0 && wgf_texture_read(0) == 0, "no read of a texture that isn't a target");
    read = wgf_texture_read(target);
    expect(read != 0 && wgf_texture_read_get_status(read) == WGF_TEXTURE_READ_STATUS_PENDING, "a read, PENDING");
    pending = wgf_texture_read(target);
    expect(wgf_texture_read_destroy(pending) && wgf_texture_read_get_status(pending) == WGF_TEXTURE_READ_STATUS_NONE,
           "one destroyed PENDING: gone");
    wgf_core_priv_part_update(0.0f);
    expect(wgf_texture_read_get_status(read) == WGF_TEXTURE_READ_STATUS_PENDING,
           "PENDING in an update before its frame is drawn");
    wgf_resource_release(target);
    expect(wgf_resource_get_status(target) == WGF_RESOURCE_STATUS_READY, "its target held while it is PENDING");
    wgf_gfx_priv_begin_frame();
    wgf_gfx_priv_end_frame();
    wgf_core_priv_part_update(0.0f);
    expect(wgf_texture_read_get_status(read) == WGF_TEXTURE_READ_STATUS_FAILED, "FAILED next: no GPU to read");
    expect(wgf_texture_read_get_data(read) == NULL && wgf_texture_read_get_size(read) == 0 &&
               wgf_texture_read_get_width(read) == 0 && wgf_texture_read_get_height(read) == 0,
           "no pixels, and no size");
    expect(!wgf_handle_is_alive(target), "its target let go of once read");
    expect(wgf_texture_read_destroy(read) && !wgf_texture_read_destroy(read) &&
               wgf_texture_read_get_status(read) == WGF_TEXTURE_READ_STATUS_NONE,
           "destroyed once");

    wgf_gfx_priv_begin_frame();
    expect(!wgf_render_begin_target(target), "a target released isn't begun");
    wgf_gfx_priv_end_frame();
    wgf_resource_release(other);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
