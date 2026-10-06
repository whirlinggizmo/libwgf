#include <math.h>
#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_canvas.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_emitter2d.h"
#include "wgf_platform_priv.h"
#include "wgf_random.h"

/* 2D emitters on sokol's dummy backend (a headless build), moved on by the particles
 * part's update as the runtime runs it: the defaults and every setting read back, the
 * refusals and clamps, bursts within the room, a rate over frames, life ending
 * particles, the capacity dropping the oldest, the same seed giving the same
 * particles, and what a canvas draws. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int drawn(wgf_node_t canvas)
{
    int before;
    wgf_platform_priv_set_size(320, 240);
    wgf_platform_priv_headless_set_dpi_scale(1.0f);
    wgf_gfx_priv_begin_frame();
    before = sgl_num_vertices();
    wgf_canvas_draw(canvas);
    before = sgl_num_vertices() - before;
    wgf_gfx_priv_end_frame();
    return before;
}

static void frames(int count, float dt)
{
    int i;
    for (i = 0; i < count; i++) wgf_core_priv_part_update(dt);
}

int main(void)
{
    wgf_node_t canvas, emitter;
    int i;

    wgf_core_priv_init();
    expect(wgf_gfx_priv_start(), "setup");
    canvas = wgf_canvas_create();
    emitter = wgf_emitter2d_create();
    wgf_node_set_parent(emitter, canvas);

    expect(wgf_node_get_type(emitter) == WGF_NODE_TYPE_EMITTER2D, "an emitter");
    expect(wgf_emitter2d_get_rate(emitter) == 0 && wgf_emitter2d_is_emitting(emitter) &&
               wgf_emitter2d_get_capacity(emitter) == 256 && wgf_emitter2d_get_count(emitter) == 0 &&
               wgf_emitter2d_get_life_min(emitter) == 1 && wgf_emitter2d_get_size_start(emitter) == 4 &&
               wgf_emitter2d_get_color_start(emitter) == 0xFFFFFFFFu && wgf_emitter2d_get_stretch(emitter) == 0,
           "defaults");
    frames(10, 1.0f / 60.0f);
    expect(wgf_emitter2d_get_count(emitter) == 0, "rate 0: nothing comes");

    expect(wgf_emitter2d_burst(emitter, 10) && wgf_emitter2d_get_count(emitter) == 10, "a burst");
    expect(drawn(canvas) == 10 * 6, "each particle a quad");
    expect(wgf_emitter2d_burst(emitter, 1000) && wgf_emitter2d_get_count(emitter) == 256, "clamped to the room left");
    expect(!wgf_emitter2d_burst(emitter, 0), "a burst below 1 is refused");
    frames(70, 1.0f / 60.0f);
    expect(wgf_emitter2d_get_count(emitter) == 0, "a second's life: all gone after a second and a bit");

    expect(wgf_emitter2d_set_rate(emitter, 60) && wgf_emitter2d_get_rate(emitter) == 60, "a rate");
    frames(30, 1.0f / 60.0f);
    expect(wgf_emitter2d_get_count(emitter) == 30, "60 a second: 30 in half a second");
    expect(wgf_emitter2d_set_emitting(emitter, false) && !wgf_emitter2d_is_emitting(emitter), "not emitting");
    frames(10, 1.0f / 60.0f);
    expect(wgf_emitter2d_get_count(emitter) == 30, "none born while off");
    expect(wgf_emitter2d_set_rate(emitter, -5) && wgf_emitter2d_get_rate(emitter) == 0, "a rate below 0 is clamped");
    expect(wgf_emitter2d_clear(emitter) && wgf_emitter2d_get_count(emitter) == 0, "cleared");

    expect(wgf_emitter2d_set_capacity(emitter, 8) && wgf_emitter2d_get_capacity(emitter) == 8 &&
               wgf_emitter2d_burst(emitter, 20) && wgf_emitter2d_get_count(emitter) == 8,
           "a smaller capacity");
    expect(wgf_emitter2d_set_capacity(emitter, 4) && wgf_emitter2d_get_count(emitter) == 4, "lowered: the oldest go");
    expect(wgf_emitter2d_set_capacity(emitter, 0) && wgf_emitter2d_get_capacity(emitter) == 1 &&
               wgf_emitter2d_set_capacity(emitter, 1 << 20) && wgf_emitter2d_get_capacity(emitter) == 65536,
           "capacity clamped to 1..65536");
    wgf_emitter2d_set_capacity(emitter, 256);
    wgf_emitter2d_clear(emitter);

    expect(wgf_emitter2d_set_life(emitter, 2, 0.5f) && wgf_emitter2d_get_life_min(emitter) == 0.5f &&
               wgf_emitter2d_get_life_max(emitter) == 2,
           "a life range, swapped");
    expect(!wgf_emitter2d_set_life(emitter, 0, 1) && !wgf_emitter2d_set_life(emitter, 1, -1), "a life of 0 is refused");
    expect(wgf_emitter2d_set_direction(emitter, 1.5f, 0.25f) && wgf_emitter2d_get_direction(emitter) == 1.5f &&
               wgf_emitter2d_get_spread(emitter) == 0.25f && !wgf_emitter2d_set_direction(emitter, 0, -1),
           "a direction and spread");
    expect(wgf_emitter2d_set_speed(emitter, 100, 50) && wgf_emitter2d_get_speed_min(emitter) == 50 &&
               wgf_emitter2d_get_speed_max(emitter) == 100 && !wgf_emitter2d_set_speed(emitter, -1, 2),
           "a speed range");
    expect(wgf_emitter2d_set_radius(emitter, 5) && wgf_emitter2d_get_radius(emitter) == 5 &&
               !wgf_emitter2d_set_radius(emitter, -1),
           "a radius");
    expect(wgf_emitter2d_set_gravity(emitter, 0, 98) && wgf_emitter2d_get_gravity(emitter).y == 98, "gravity");
    expect(wgf_emitter2d_set_drag(emitter, 2) && wgf_emitter2d_get_drag(emitter) == 1 &&
               wgf_emitter2d_set_drag(emitter, 0.5f) && wgf_emitter2d_get_drag(emitter) == 0.5f,
           "drag, clamped to 0..1");
    expect(wgf_emitter2d_set_size(emitter, 6, 0) && wgf_emitter2d_get_size_end(emitter) == 0 &&
               !wgf_emitter2d_set_size(emitter, -1, 1),
           "sizes");
    expect(wgf_emitter2d_set_color(emitter, 0xFF0000FFu, 0x0000FF00u) &&
               wgf_emitter2d_get_color_end(emitter) == 0x0000FF00u,
           "colors");
    expect(wgf_emitter2d_set_stretch(emitter, 0.05f) && wgf_emitter2d_get_stretch(emitter) == 0.05f, "a stretch");

    {
        /* the same seed, the same particles: drawn the same, vertex for vertex */
        int first, second;
        wgf_random_set_seed(99);
        wgf_emitter2d_burst(emitter, 50);
        frames(5, 1.0f / 60.0f);
        first = drawn(canvas);
        wgf_emitter2d_clear(emitter);
        wgf_random_set_seed(99);
        wgf_emitter2d_burst(emitter, 50);
        frames(5, 1.0f / 60.0f);
        second = drawn(canvas);
        expect(first == 50 * 6 && second == first, "a seed replays its particles");
    }
    wgf_node_set_enabled(emitter, false);
    i = wgf_emitter2d_get_count(emitter);
    frames(200, 1.0f / 60.0f);
    expect(wgf_emitter2d_get_count(emitter) == i, "a disabled emitter isn't moved on");
    wgf_node_set_enabled(emitter, true);
    {
        const wgf_node_t holder = wgf_node_create(); /* the emitter under a disabled node */
        const wgf_node_t was = wgf_node_get_parent(emitter);
        wgf_node_set_parent(holder, was);
        wgf_node_set_parent(emitter, holder);
        wgf_node_set_enabled(holder, false);
        i = wgf_emitter2d_get_count(emitter);
        frames(200, 1.0f / 60.0f);
        expect(wgf_emitter2d_get_count(emitter) == i && wgf_node_is_enabled(emitter),
               "an emitter under a disabled node isn't moved on, its own flag on");
        wgf_node_set_enabled(holder, true);
        wgf_node_set_parent(emitter, was);
        wgf_node_destroy(holder, WGF_NODE_DESTROY_CHILDREN);
    }
    wgf_node_set_visible(emitter, false);
    expect(drawn(canvas) == 0, "a hidden one isn't drawn");

    {
        const wgf_node_t plain = wgf_node_create();
        expect(!wgf_emitter2d_burst(plain, 1) && wgf_emitter2d_get_rate(plain) == 0 && !wgf_emitter2d_clear(12345),
               "every call refuses a node that isn't an emitter");
        wgf_node_destroy(plain, WGF_NODE_DESTROY_CHILDREN);
    }
    wgf_node_destroy(emitter, WGF_NODE_DESTROY_CHILDREN);
    frames(1, 1.0f / 60.0f); /* the part with no emitter left */
    expect(wgf_emitter2d_create() != 0, "made again after one was destroyed");
    wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
