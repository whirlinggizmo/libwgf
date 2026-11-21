#ifndef WGF_DEBUG_H
#define WGF_DEBUG_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The frame-rate overlay, for a program under development: "<fps> FPS <cost> ms" (the
 * frames a second and a frame's own cost, wgf_loop.h's), drawn by the runtime over
 * everything the program drew, every frame, at (x, y) in logical pixels, in `font` (0:
 * the built-in), at `size`, in `color`; a size of 0 or less is none, so it isn't
 * shown. Off by default; showing it again moves or restyles it. A program that never
 * shows it links none of it. */
WGF_API void wgf_debug_show_fps(wgf_font_t font, float x, float y, float size, wgf_color_t color);
WGF_API void wgf_debug_hide_fps(void);
WGF_API bool wgf_debug_is_fps_shown(void);

/* The frames' stats, helix's Debug extra's: each frame counted as its drawing ends, its time
 * from the frame before's end (real time, whatever the time scale or an autopilot), and its
 * cost wgf_loop_get_frame_cost's. Times in seconds; a percentile is the least of the slowest
 * share (P95: of the slowest 5%), so a single slow frame in twenty shows. Every stat is 0 for
 * no frames. */
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

/* A stat of the last half second's frames. Frames are counted from the first call of this
 * or wgf_debug_start_frame_recording, so the first returns 0 and the next ones cover the
 * frames since, up to the last half second; a program that never asks counts none. 0 for a
 * stat that isn't one. */
WGF_API float wgf_debug_get_frame_stat(wgf_frame_stat_t stat);

/* Records the frames' stats from the next frame's end until stopped, its latest 65,536
 * frames kept; started again, it drops what it held. Stopped, its stats are kept for reading
 * (wgf_debug_get_recorded_frame_stat) until the next start; stop is false, nothing kept, for
 * a recording never started. Recording doesn't change the last half second's stats. */
WGF_API void wgf_debug_start_frame_recording(void);
WGF_API bool wgf_debug_stop_frame_recording(void);
WGF_API bool wgf_debug_is_frame_recording(void);
/* A stat of the stopped recording's frames; 0 while one runs, or with none. */
WGF_API float wgf_debug_get_recorded_frame_stat(wgf_frame_stat_t stat);
/* The stopped recording's frames' times (`costs` false) or costs, oldest first, into `out`, at
 * most `count`; the number written. */
WGF_API int wgf_debug_get_recorded_frame_times(float *out, int count, bool costs);

#ifdef __cplusplus
}
#endif

#endif
