#ifndef WGF_LOOP_H
#define WGF_LOOP_H

#include "wgf_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The frame loop's timing.
 *
 * Frames run as fast as the display shows them. Ticks run at a fixed rate, on the
 * runtime's tick clock, which is fed the frames' time. */

/* Returns the seconds since the previous frame, at most 0.1.
 *
 * - A frame after a stall counts as 0.1 seconds.
 * - The first frame counts as one display frame.
 * - It is real time: the time scale doesn't change it. */
WGF_API float wgf_loop_get_frame_delta(void);

/* Sets or gets the tick rate, in ticks a second (default: 60). 0 or less turns ticks off.
 * A new rate takes effect at the next frame, and time not yet ticked is dropped. */
WGF_API void wgf_loop_set_tick_rate(int hz);
WGF_API int wgf_loop_get_tick_rate(void);

/* Returns the seconds one tick steps the simulation: 1 / the tick rate. Returns 0 with
 * ticks off. */
WGF_API float wgf_loop_get_tick_delta(void);

/* Returns how far the loop is into the next tick, from 0 up to 1.
 *
 * Use it to draw ticked state smoothly: lerp(previous tick's value, latest, fraction). It
 * holds still while the time scale is 0. Returns 0 with ticks off. */
WGF_API float wgf_loop_get_tick_fraction(void);

/* Sets or gets how fast ticks run against real time (default: 1). A scale below 0 is
 * clamped to 0.
 *
 * - 1 is normal speed.
 * - 0.5 is slow motion.
 * - 0 is paused: no ticks run, while frames still draw and read input. */
WGF_API void wgf_loop_set_time_scale(float scale);
WGF_API float wgf_loop_get_time_scale(void);

/* Sets or gets whether ticks stop while the window, or the page, isn't focused (default:
 * off). The game pauses when the player looks away, and frames still draw. A run that an
 * autopilot flies or records isn't held. */
WGF_API void wgf_loop_set_pause_unfocused(bool pause);
WGF_API bool wgf_loop_is_pause_unfocused(void);

/* Sets or gets the most frames a second, to save power or heat. 0 or less is the
 * display's rate (default: 0).
 *
 * With vsync on, a cap can only lower the rate. On the web, a frame the browser offers too
 * early is skipped. */
WGF_API void wgf_loop_set_target_fps(int fps);
WGF_API int wgf_loop_get_target_fps(void);

/* Returns the frames per second, from a moving average of the frame deltas. It counts the
 * frames that ran, not the display's refreshes. Returns 0 before the first frame. */
WGF_API float wgf_loop_get_fps(void);

/* Returns the last frame's own cost, in seconds: the real time the runtime spent on it.
 *
 * The cost runs from the frame's start to its drawing's end. It covers the ticks, the
 * optional parts, the frame callback, and gfx's end. What the GPU and the browser do after
 * that isn't in it.
 *
 * It is real time under an autopilot too, though the autopilot sets the frames' time. So a
 * flown run's cost is measured as it would be played. Returns 0 before the first frame. */
WGF_API float wgf_loop_get_frame_cost(void);

#ifdef __cplusplus
}
#endif

#endif
