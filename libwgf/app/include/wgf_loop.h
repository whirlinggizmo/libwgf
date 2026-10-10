#ifndef WGF_LOOP_H
#define WGF_LOOP_H

#include "wgf_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The frame loop's timing: frames as fast as the display shows them, and ticks
 * at a fixed rate, on the runtime's tick clock fed the frames' time. */

/* Seconds since the previous frame, at most 0.1 (a frame after a stall counts as
 * 0.1); the first frame counts as one display frame. Real time: the time scale
 * doesn't change it. */
WGF_API float wgf_loop_get_frame_delta(void);

/* Ticks a second (default 60); 0 or less turns ticks off. Takes effect at the next
 * frame; time not yet ticked is dropped. */
WGF_API void wgf_loop_set_tick_rate(int hz);
WGF_API int wgf_loop_get_tick_rate(void);

/* Seconds one tick steps the simulation: 1 / the tick rate, 0 with ticks off. */
WGF_API float wgf_loop_get_tick_delta(void);

/* How far the loop is into the next tick, 0 up to 1, for drawing ticked state
 * smoothly: lerp(previous tick's value, latest, fraction). 0 with ticks off. It
 * holds still while the time scale is 0. */
WGF_API float wgf_loop_get_tick_fraction(void);

/* How fast ticks run against real time: 1 (the default) is normal, 0.5 slow
 * motion, 0 paused -- no ticks run, while frames still draw and read input.
 * Clamped to 0 or more. */
WGF_API void wgf_loop_set_time_scale(float scale);
WGF_API float wgf_loop_get_time_scale(void);

/* The most frames a second, to save power or heat; 0 or less (the default) is
 * the display's rate. With vsync on, a cap can only lower the rate. On the web a
 * frame the browser offers too early is skipped. */
WGF_API void wgf_loop_set_target_fps(int fps);
WGF_API int wgf_loop_get_target_fps(void);

/* Frames per second, from the frame deltas smoothed (wgrender's moving average):
 * the frames that ran, not the display's refreshes. 0 before the first frame. */
WGF_API float wgf_loop_get_fps(void);

/* The last frame's own cost, in seconds: the real time the runtime spent on it, from
 * its start to its drawing's end (the ticks, the optional parts, the frame callback, and
 * gfx's end). What the GPU and the browser do after isn't in it. Real time under an
 * autopilot too, whose frames are the autopilot's time: so a flown run's cost is
 * measured as it would be played. 0 before the first frame. */
WGF_API float wgf_loop_get_frame_cost(void);

#ifdef __cplusplus
}
#endif

#endif
