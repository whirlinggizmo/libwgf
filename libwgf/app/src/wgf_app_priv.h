#ifndef WGF_APP_PRIV_H
#define WGF_APP_PRIV_H

/* What the runtime draws over a program's frame, after its frame callback and before
 * gfx's end: the debug overlay's drawing (wgf_app_debug.c), NULL for none. Set only by
 * a call that shows one, so a program that never does links none of its drawing. */
typedef void (*wgf_app_priv_overlay_t)(void);
void wgf_app_priv_set_overlay(wgf_app_priv_overlay_t overlay);

/* Called at each frame's end, after its drawing, with the real time since the frame before's
 * end (0 for the first) and the frame's cost: the frame stats' (wgf_app_frame_stats.c), set
 * by their first call, so a program that never asks for them links none. */
typedef void (*wgf_app_priv_frame_counted_t)(double interval, float cost);
void wgf_app_priv_set_frame_counted(wgf_app_priv_frame_counted_t counted);

/* For tests: the options as a page's query would give them ("a=1&b"), in place of the
 * program's own (wgf_app_options.c). NULL: the program's again. */
void wgf_app_priv_set_options(const char *query);

#endif
