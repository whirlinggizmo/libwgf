#include <stdio.h>

#include "wgf_app.h"
#include "wgf_debug.h"
#include "wgf_time.h"
#include "wgf_window.h"

/* The frames' stats, through the loop with no window (a headless build): counted from the
 * first ask, the last half second's, and a recording of 40 frames, every tenth made slow by
 * waiting in it, whose percentiles find the slow ones the mean hides. */

#define RECORDED 40
#define SLOW 0.05 /* seconds a slow frame waits: well past a headless frame's sixtieth */

static int failures;
static int frame;
static float times[RECORDED + 8], costs[RECORDED + 8];

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void spin(double seconds)
{
    const double until = wgf_time_get_seconds() + seconds;
    while (wgf_time_get_seconds() < until) {
    }
}

static void on_frame(void *user)
{
    (void)user;
    frame++;
    if (frame == 1) {
        expect(wgf_debug_get_frame_stat(WGF_FRAME_STAT_FRAMES) == 0.0f, "nothing counted before the first ask");
        expect(!wgf_debug_stop_frame_recording() && !wgf_debug_is_frame_recording(), "no recording to stop");
        expect(wgf_debug_get_recorded_frame_times(times, RECORDED, false) == 0, "no frames recorded");
    } else if (frame == 10) {
        const float n = wgf_debug_get_frame_stat(WGF_FRAME_STAT_FRAMES);
        expect(n >= 1.0f && n <= 9.0f, "the frames since the first ask");
        expect(wgf_debug_get_frame_stat(WGF_FRAME_STAT_FPS) > 0.0f && wgf_debug_get_frame_stat(WGF_FRAME_STAT_TIME_MEAN) > 0.0f,
               "a rate and a mean");
        expect(wgf_debug_get_frame_stat((wgf_frame_stat_t)99) == 0.0f, "a stat that isn't one is 0");
        wgf_debug_start_frame_recording();
        expect(wgf_debug_is_frame_recording(), "recording");
    } else if (frame > 10 && frame <= 10 + RECORDED) {
        if (frame % 10 == 0) spin(SLOW); /* frames 20, 30, 40, 50: 4 of the 40 recorded */
        expect(wgf_debug_get_recorded_frame_stat(WGF_FRAME_STAT_FRAMES) == 0.0f, "nothing read while it runs");
    } else if (frame == 11 + RECORDED) {
        int n, slow = 0, i;
        expect(wgf_debug_stop_frame_recording() && !wgf_debug_is_frame_recording(), "stopped");
        n = wgf_debug_get_recorded_frame_times(times, RECORDED + 8, false);
        expect(n == RECORDED && wgf_debug_get_recorded_frame_stat(WGF_FRAME_STAT_FRAMES) == (float)RECORDED,
               "the frames from the next frame's end to the stop");
        expect(wgf_debug_get_recorded_frame_times(costs, 3, true) == 3, "at most the count asked");
        for (i = 0; i < n; i++) slow += times[i] >= SLOW;
        expect(slow == 4, "the slow frames, among the times");
        expect(wgf_debug_get_recorded_frame_stat(WGF_FRAME_STAT_TIME_P95) >= (float)SLOW &&
                   wgf_debug_get_recorded_frame_stat(WGF_FRAME_STAT_TIME_SLOWEST) >= (float)SLOW,
               "the 95th percentile finds them");
        expect(wgf_debug_get_recorded_frame_stat(WGF_FRAME_STAT_TIME_MEDIAN) < (float)SLOW, "the median doesn't");
        expect(wgf_debug_get_recorded_frame_stat(WGF_FRAME_STAT_COST_P95) >= (float)SLOW,
               "their cost: the wait is the frame's own work");
        expect(wgf_debug_get_recorded_frame_stat(WGF_FRAME_STAT_TIME_MEAN) <
                   wgf_debug_get_recorded_frame_stat(WGF_FRAME_STAT_TIME_P95),
               "the mean hides them");
        expect(wgf_debug_get_frame_stat(WGF_FRAME_STAT_TIME_SLOWEST) >= (float)SLOW,
               "the last half second's stats count the recorded frames too");
        wgf_debug_start_frame_recording();
        expect(wgf_debug_stop_frame_recording() && wgf_debug_get_recorded_frame_stat(WGF_FRAME_STAT_FRAMES) == 0.0f,
               "started again, what it held is dropped");
        wgf_app_quit();
    }
}

int main(void)
{
    wgf_window_set_size(64, 64);
    expect(wgf_app_run(NULL, NULL, on_frame, NULL, NULL), "runs");
    expect(frame == 11 + RECORDED, "every frame ran");
    if (failures == 0) printf("wgf_app_frame_stats_test: passed\n");
    return failures == 0 ? 0 : 1;
}
