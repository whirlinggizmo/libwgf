#include "wgf_core_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_app.h"

#include <stddef.h>

#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_input_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_loop.h"
#include "wgf.h"
#include "wgf_log.h"
#include "wgf_app_priv.h"
#include "wgf_app_autopilot_priv.h"
#include "wgf_app_tick_clock_priv.h"
#include "wgf_time.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_platform_window_priv.h"

/* The program and its frame loop: the runtime, which starts and drives the layers
 * below it, as libwgt's and wgrender's. */

#define DEFAULT_TICK_RATE 60
#define MAX_FRAME_DELTA 0.1
#define AUTOPILOT_FRAME (1.0 / 60.0) /* an autopilot run's frame, whatever the display does */

static struct {
    bool running;
    bool started; /* core and gfx are up */
    wgf_app_callback_t init, tick, frame, shutdown;
    void *user;
    wgf_app_priv_tick_clock_t clock;
    int tick_rate;
    unsigned tick_rate_changes; /* to notice a change made inside a tick */
    float time_scale;
    int target_fps;
    double next_frame; /* when a capped frame is due; 0 before the first */
    double last_frame; /* when the previous frame ran; 0 before the first */
    float frame_delta;
    double fps_delta; /* frame deltas, smoothed, for the fps readout; 0 before the first */
    float frame_cost;  /* the last frame's own work in real time, seconds; 0 before the first */
    wgf_app_priv_overlay_t overlay; /* drawn over the program's frame (wgf_app_priv.h); NULL for none */
    bool first_frame_done; /* the first frame's mark made */
    bool flown;            /* an autopilot flies it (wgf_app_autopilot_priv.h): its time, not the clock's */
    long frames;           /* frames run since init */
} app = {.tick_rate = DEFAULT_TICK_RATE, .time_scale = 1.0f};

static void call(wgf_app_callback_t callback)
{
    if (callback != NULL) callback(app.user);
}

static void on_init(void)
{
    wgf_platform_priv_mark("wgf:init"); /* startup points: tools/measure_example_startup.py */
    wgf_core_priv_init();
    if (!wgf_gfx_priv_start()) {
        wgf_log_error("wgf_app: gfx couldn't start on the window; quitting");
        wgf_platform_priv_request_quit();
        return;
    }
    wgf_app_priv_tick_clock_set_rate(&app.clock, app.tick_rate);
    app.next_frame = 0.0;
    app.last_frame = 0.0;
    app.frame_delta = 0.0f;
    app.fps_delta = 0.0;
    app.frame_cost = 0.0f;
    app.frames = 0;
    app.flown = wgf_app_priv_autopilot_start(); /* before the program's init: it sets the seed */
    wgf_platform_priv_set_paced(!app.flown);  /* a headless run flown by an autopilot waits for no display */
    app.started = true;
    wgf_platform_priv_input_reset();
    wgf_platform_priv_gamepad_open();
    wgf_platform_priv_window_set_open(true);
    wgf_platform_priv_mark("wgf:started");
    call(app.init);
    wgf_platform_priv_mark("wgf:user-init");
}

static void on_event(const sapp_event *event)
{
    wgf_platform_priv_input_handle_event(event, wgf_platform_priv_get_dpi_scale());
}

/* Pacing for a target fps: false when this frame is skipped (the web). */
static bool pace(void)
{
    double now;
    if (app.target_fps <= 0) return true;
    now = wgf_time_get_seconds();
    if (app.next_frame > 0.0 && !wgf_platform_priv_pace(app.next_frame - now)) return false;
    now = wgf_time_get_seconds();
    app.next_frame = app.next_frame > 0.0 ? app.next_frame + 1.0 / app.target_fps : now + 1.0 / app.target_fps;
    if (app.next_frame <= now) app.next_frame = now + 1.0 / app.target_fps; /* fell a frame behind */
    return true;
}

/* The time since the previous frame: the frame delta, clamped, and the real time,
 * which feeds the tick clock (it drops a stall's backlog itself). */
static double time_frame(void)
{
    const double now = wgf_time_get_seconds();
    if (app.flown) { /* the autopilot's time: a display frame, every frame */
        app.frame_delta = (float)AUTOPILOT_FRAME;
        app.fps_delta = AUTOPILOT_FRAME;
        app.last_frame = now;
        return AUTOPILOT_FRAME;
    }
    const double elapsed = app.last_frame > 0.0 ? now - app.last_frame : 0.0;
    double delta = app.last_frame > 0.0 ? elapsed : wgf_platform_priv_get_frame_duration();
    if (delta <= 0.0 || delta > 1.0) delta = 1.0 / 60.0; /* a first frame with no measure yet */
    app.frame_delta = (float)(delta < 1e-6 ? 1e-6 : (delta > MAX_FRAME_DELTA ? MAX_FRAME_DELTA : delta));
    app.last_frame = now;
    /* wgrender's moving average: frames that ran, not display refreshes */
    app.fps_delta = app.fps_delta > 0.0 ? app.fps_delta + 0.05 * (app.frame_delta - app.fps_delta) : app.frame_delta;
    return elapsed;
}

static void run_ticks(double elapsed)
{
    const unsigned changes = app.tick_rate_changes;
    const int count = wgf_app_priv_tick_clock_advance(&app.clock, elapsed * (double)app.time_scale,
                                                      WGF_APP_PRIV_MAX_TICKS_PER_FRAME);
    int i;
    if (app.tick == NULL) {
        for (i = 0; i < count; i++) {
            wgf_core_priv_part_tick_begin();
            wgf_core_priv_part_tick(wgf_loop_get_tick_delta());
        }
        wgf_platform_priv_input_end_tick(); /* nothing reads tick edges: don't let them pile up */
        wgf_platform_priv_gamepad_end_tick();
        return;
    }
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_TICK);
    for (i = 0; i < count && app.tick_rate_changes == changes && app.running; i++) {
        wgf_core_priv_part_tick_begin(); /* the parts' state as the tick begins (ecs) */
        call(app.tick);
        wgf_core_priv_part_tick(wgf_loop_get_tick_delta()); /* the parts' systems, after the program's (ecs) */
        wgf_platform_priv_input_end_tick();
        wgf_platform_priv_gamepad_end_tick();
    }
    wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_FRAME);
}

static void on_frame(void)
{
    double started;
    if (!app.started) return;
    wgf_core_priv_update();
    if (!pace()) return;
    started = wgf_time_get_seconds(); /* the frame's own work, from here to its drawing's end */
    wgf_app_priv_autopilot_begin_frame(app.frames); /* the frame's autopilot inputs, before its ticks */
    wgf_platform_priv_gamepad_begin_frame();     /* before the ticks: they read the pads too */
    run_ticks(time_frame());
    wgf_core_priv_part_set_fraction(wgf_loop_get_tick_fraction()); /* for those drawing ticked state */
    wgf_core_priv_part_update(app.frame_delta); /* the parts' (the ecs's nodes, particles): after the ticks, before the frame */
    wgf_gfx_priv_begin_frame();
    call(app.frame);
    if (app.overlay != NULL) app.overlay();
    wgf_gfx_priv_end_frame();
    app.frame_cost = (float)(wgf_time_get_seconds() - started);
    if (!app.first_frame_done) {
        app.first_frame_done = true;
        wgf_platform_priv_mark("wgf:first-frame");
    }
    wgf_platform_priv_input_end_frame();
    wgf_platform_priv_gamepad_end_frame();
    wgf_app_priv_autopilot_end_frame(app.frames); /* its expectations, after it */
    app.frames++;
}

static void on_shutdown(void)
{
    if (app.started) {
        call(app.shutdown);
        app.started = false;
    }
    wgf_app_priv_autopilot_stop(app.frames);
    wgf_platform_priv_window_set_open(false);
    wgf_platform_priv_gamepad_close();
    wgf_platform_priv_input_reset();
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    app.running = false;
}

bool wgf_app_run(wgf_app_callback_t init, wgf_app_callback_t tick, wgf_app_callback_t frame, wgf_app_callback_t shutdown,
                void *user)
{
    wgf_platform_priv_desc_t desc = {0};
    if (app.running) return false;
    app.running = true;
    app.init = init;
    app.tick = tick;
    app.frame = frame;
    app.shutdown = shutdown;
    app.user = user;
    desc.init = on_init;
    desc.frame = on_frame;
    desc.event = on_event;
    desc.shutdown = on_shutdown;
    wgf_platform_priv_window_describe(&desc);
    wgf_platform_priv_run(&desc);
    return true;
}

void wgf_app_quit(void)
{
    if (app.running) wgf_platform_priv_request_quit();
}

bool wgf_app_can_quit(void)
{
    return wgf_platform_priv_can_quit();
}

bool wgf_app_is_running(void)
{
    return app.running;
}

/* ---------------------------------------------------------------- loop ---- */

float wgf_loop_get_frame_delta(void)
{
    return app.frame_delta;
}

void wgf_loop_set_tick_rate(int hz)
{
    app.tick_rate = hz > 0 ? hz : 0;
    app.tick_rate_changes++;
    if (app.started) wgf_app_priv_tick_clock_set_rate(&app.clock, app.tick_rate);
}

int wgf_loop_get_tick_rate(void)
{
    return app.tick_rate;
}

float wgf_loop_get_tick_delta(void)
{
    return app.tick_rate > 0 ? 1.0f / (float)app.tick_rate : 0.0f;
}

float wgf_loop_get_tick_fraction(void)
{
    return app.started ? wgf_app_priv_tick_clock_fraction(&app.clock) : 0.0f;
}

void wgf_loop_set_time_scale(float scale)
{
    app.time_scale = scale > 0.0f ? scale : 0.0f;
}

float wgf_loop_get_time_scale(void)
{
    return app.time_scale;
}

void wgf_loop_set_target_fps(int fps)
{
    app.target_fps = fps > 0 ? fps : 0;
    app.next_frame = 0.0;
}

int wgf_loop_get_target_fps(void)
{
    return app.target_fps;
}

float wgf_loop_get_fps(void)
{
    return app.fps_delta > 0.0 ? (float)(1.0 / app.fps_delta) : 0.0f;
}

void wgf_app_priv_set_overlay(wgf_app_priv_overlay_t overlay)
{
    app.overlay = overlay;
}

float wgf_loop_get_frame_cost(void)
{
    return app.frame_cost;
}
