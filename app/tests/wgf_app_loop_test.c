#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_loop.h"
#include "wgf_window.h"
#include "wgf_time.h"
#include "wgf_render.h"
#include "render/wgf_gfx_render_priv.h"
#include "util/sokol_gl.h"

/* The loop with no window (a headless build): the callbacks in order, core and
 * gfx started and stopped around them, frames of the window's size, ticks at
 * their rate and paused by the time scale, a capped frame rate (below the 60 a
 * second the headless display shows), and the window's settings before and after
 * it opens. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

typedef struct run_t {
    int inits, ticks, frames, shutdowns;
    int ticks_in_order; /* every tick came after init and before shutdown */
    int frames_in_frame; /* gfx was inside a frame during each frame callback */
    int frame_sizes_ok;
    int paused_ticks;   /* ticks while the time scale was 0 */
    float paused_fraction_first, paused_fraction_last;
    double started, ended;
    int nested_run_refused;
    int open_options_refused;
    int samples;
    int placed, monitors_ok;
    float fps;          /* at the last frame */
    int fps_drawn;      /* the readout recorded glyphs */
} run_t;

static void on_init(void *user)
{
    run_t *run = user;
    run->inits++;
    run->started = wgf_time_get_seconds();
    run->nested_run_refused = !wgf_app_run(NULL, NULL, NULL, NULL, NULL);
    run->open_options_refused = !wgf_window_set_vsync(false) && wgf_window_is_vsync() &&
                                !wgf_window_set_msaa(false) && wgf_window_is_msaa() &&
                                !wgf_window_set_high_dpi(false) && !wgf_window_set_transparent(true);
    wgf_loop_set_tick_rate(50);
    wgf_loop_set_target_fps(30);
    run->samples = sg_query_desc().environment.defaults.sample_count;
    run->placed = wgf_window_set_position(30, 40) && wgf_window_get_x() == 30 && wgf_window_get_y() == 40;
    run->monitors_ok = wgf_window_get_monitor_count() == 1 && wgf_window_get_monitor() == 0 &&
                       wgf_window_get_monitor_width(0) == 320 && wgf_window_get_monitor_height(0) == 200 &&
                       strcmp(wgf_window_get_monitor_name(0), "headless") == 0 &&
                       wgf_window_get_monitor_width(5) == 0 && !wgf_window_set_monitor(5) &&
                       !wgf_window_can_fullscreen();
}

static void on_tick(void *user)
{
    run_t *run = user;
    run->ticks++;
    run->ticks_in_order &= run->inits == 1 && run->shutdowns == 0 && !wgf_gfx_priv_is_in_frame();
    if (wgf_loop_get_time_scale() == 0.0f) run->paused_ticks++;
}

static void on_frame(void *user)
{
    run_t *run = user;
    run->frames++;
    run->frames_in_frame &= wgf_gfx_priv_is_in_frame();
    run->frame_sizes_ok &= wgf_render_get_width() == 320 && wgf_render_get_height() == 200;
    if (run->frames == 30) {
        wgf_loop_set_time_scale(0.0f); /* paused from the next frame */
    } else if (run->frames == 31) {
        run->paused_fraction_first = wgf_loop_get_tick_fraction();
    } else if (run->frames == 45) {
        run->paused_fraction_last = wgf_loop_get_tick_fraction();
        run->fps = wgf_loop_get_fps();
        wgf_app_quit();
    }
}

static void on_shutdown(void *user)
{
    run_t *run = user;
    run->shutdowns++;
    run->ended = wgf_time_get_seconds();
}

int main(void)
{
    run_t run;
    memset(&run, 0, sizeof(run));
    run.ticks_in_order = run.frames_in_frame = run.frame_sizes_ok = 1;

    /* before the window opens: its settings, and the loop's defaults */
    expect(!wgf_app_is_running(), "not running before wgf_app_run");
    expect(wgf_window_get_width() == 1024 && wgf_window_get_height() == 768, "1024 by 768 by default");
    expect(strcmp(wgf_window_get_title(), "libwgf") == 0, "titled libwgf by default");
    expect(wgf_window_set_size(320, 200) && wgf_window_get_width() == 320, "a size of our own");
    expect(!wgf_window_set_size(0, 200) && wgf_window_get_width() == 320, "a size under 1 is refused");
    wgf_window_set_title("loop test");
    expect(strcmp(wgf_window_get_title(), "loop test") == 0, "the title reads back");
    wgf_window_set_title(NULL);
    expect(strcmp(wgf_window_get_title(), "") == 0, "a NULL title is empty");
    expect(wgf_window_set_transparent(true) && wgf_window_is_transparent(), "transparent, before it opens");
    wgf_window_set_transparent(false);
    expect(wgf_window_is_vsync() && wgf_window_is_high_dpi(), "vsync and high DPI by default");
    wgf_window_set_resizable(false);
    expect(!wgf_window_is_resizable() && wgf_window_is_decorated(), "resizable and decorated read back");
    expect(wgf_loop_get_tick_rate() == 60 && fabsf(wgf_loop_get_tick_delta() - 1.0f / 60.0f) < 1e-6f,
           "60 ticks a second by default");
    wgf_loop_set_time_scale(-2.0f);
    expect(wgf_loop_get_time_scale() == 0.0f, "a negative time scale is 0");
    wgf_loop_set_time_scale(1.0f);
    expect(wgf_loop_get_target_fps() == 0, "no frame cap by default");
    expect(wgf_app_can_quit(), "a headless program can quit");
    expect(!wgf_window_is_msaa() && wgf_window_set_msaa(true) && wgf_window_is_msaa(), "MSAA asked for");
    expect(!wgf_window_set_position(1, 2) && wgf_window_get_x() == 0, "no position before the window opens");

    expect(wgf_app_run(on_init, on_tick, on_frame, on_shutdown, &run), "runs");

    expect(run.inits == 1 && run.shutdowns == 1, "init and shutdown once each");
    expect(run.frames == 45, "frames until it quit");
    expect(run.ticks_in_order, "ticks between init and shutdown, outside frames");
    expect(run.frames_in_frame, "the frame callback runs inside gfx's frame");
    expect(run.frame_sizes_ok, "frames of the window's size");
    expect(run.nested_run_refused, "wgf_app_run while running is refused");
    expect(run.open_options_refused, "options fixed at open are refused once it is open");
    expect(run.samples == 4, "gfx draws with the samples the window got");
    expect(run.placed, "the window placed");
    expect(run.monitors_ok, "one monitor, the size of the headless window");
    /* 30 frames at 30 a second: about 1 s, so about 50 ticks at 50 a second */
    expect(run.ticks >= 40 && run.ticks <= 60, "ticks at their rate");
    expect(run.paused_ticks == 0, "no ticks while the time scale is 0");
    expect(run.paused_fraction_first == run.paused_fraction_last, "the tick fraction holds while paused");
    expect(run.ended - run.started >= 1.3, "the frame cap paces frames");
    expect(run.fps > 25.0f && run.fps < 35.0f, "the readout's fps: the frames that ran, capped at 30");
    expect(!wgf_app_is_running(), "not running after it quit");
    expect(wgf_time_get_seconds() == 0.0, "core stopped after shutdown");
    expect(wgf_loop_get_tick_rate() == 50 && wgf_loop_get_target_fps() == 30, "loop settings kept");
    printf("%d frames, %d ticks, %.2f s\n", run.frames, run.ticks, run.ended - run.started);
    return failures == 0 ? 0 : 1;
}
