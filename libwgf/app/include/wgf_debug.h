#ifndef WGF_DEBUG_H
#define WGF_DEBUG_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shows, hides, or reports the frame-rate overlay, for a program under development.
 *
 * The overlay reads "<fps> FPS <cost> ms": the frames a second, and a frame's own cost
 * (see wgf_loop.h). The runtime draws it every frame, over everything the program drew.
 *
 * - (x, y) is its position, in logical pixels.
 * - `font` is the font to draw it in. 0 is the built-in font.
 * - `size` is its size. A size of 0 or less draws nothing, so it isn't shown.
 * - `color` is its color.
 *
 * It is off by default. Showing it again moves or restyles it. A program that never shows
 * it links none of it. */
WGF_API void wgf_debug_show_fps(wgf_font_t font, float x, float y, float size, wgf_color_t color);
WGF_API void wgf_debug_hide_fps(void);
WGF_API bool wgf_debug_is_fps_shown(void);

/* The frame stats you can read.
 *
 * - Each frame is counted as its drawing ends.
 * - A frame's time runs from the previous frame's end. It is real time, whatever the time
 *   scale, and under an autopilot too.
 * - A frame's cost is wgf_loop_get_frame_cost's.
 * - Times and costs are in seconds.
 * - A percentile is the least of the slowest share: P95 is the least of the slowest 5%. So
 *   a single slow frame in twenty shows.
 * - Every stat is 0 when there are no frames. */
typedef enum wgf_frame_stat_t {
    WGF_FRAME_STAT_FRAMES = 0,       /* the frames counted */
    WGF_FRAME_STAT_FPS = 1,          /* the frames over their total time */
    WGF_FRAME_STAT_TIME_MEAN = 2,
    WGF_FRAME_STAT_TIME_MEDIAN = 3,
    WGF_FRAME_STAT_TIME_P95 = 4,
    WGF_FRAME_STAT_TIME_P99 = 5,
    WGF_FRAME_STAT_TIME_SLOWEST = 6,
    WGF_FRAME_STAT_COST_MEAN = 7,
    WGF_FRAME_STAT_COST_P95 = 8,
    WGF_FRAME_STAT_COST_SLOWEST = 9
} wgf_frame_stat_t;

/* Returns a stat of the frames in the last half second.
 *
 * Frames are counted from the first call of this or wgf_debug_start_frame_recording. So
 * the first call returns 0, and later ones cover the frames since then, up to the last half
 * second. A program that never asks counts no frames.
 *
 * Returns 0 for a value of `stat` that isn't a stat. */
WGF_API float wgf_debug_get_frame_stat(wgf_frame_stat_t stat);

/* Starts, stops, or reports a recording of frame stats.
 *
 * - A recording runs from the next frame's end until it is stopped. It keeps its latest
 *   65,536 frames.
 * - Starting again drops what the recording held.
 * - Once stopped, its stats are kept for reading (wgf_debug_get_recorded_frame_stat) until
 *   the next start.
 * - Stop returns false, and keeps nothing, if no recording was started.
 * - Recording doesn't change the stats of the last half second. */
WGF_API void wgf_debug_start_frame_recording(void);
WGF_API bool wgf_debug_stop_frame_recording(void);
WGF_API bool wgf_debug_is_frame_recording(void);
/* Returns a stat of the stopped recording's frames. Returns 0 while a recording runs, or if
 * there is none. */
WGF_API float wgf_debug_get_recorded_frame_stat(wgf_frame_stat_t stat);
/* Copies the stopped recording's frame times, or their costs if `costs` is true, into `out`,
 * oldest first. Writes at most `count`, and returns the number written. */
WGF_API int wgf_debug_get_recorded_frame_times(float *out, int count, bool costs);

#ifdef __cplusplus
}
#endif

#endif
