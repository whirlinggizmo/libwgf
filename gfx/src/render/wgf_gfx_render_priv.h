#ifndef WGF_GFX_RENDER_PRIV_H
#define WGF_GFX_RENDER_PRIV_H

#include <stdbool.h>

#include "sokol_gfx.h"

/* The frame, for gfx's own drawing code and app's runtime. */

/* sokol's pools, as wgrender and libwgt size them (sokol's defaults are 128 buffers and
 * images, 256 views): room for every texture a 2D game loads. */
#define WGF_GFX_PRIV_BUFFER_POOL_SIZE 4096
#define WGF_GFX_PRIV_IMAGE_POOL_SIZE 2048
#define WGF_GFX_PRIV_VIEW_POOL_SIZE 4096
#define WGF_GFX_PRIV_PIPELINE_POOL_SIZE 256

/* gfx's start and stop, run by app's runtime once the window exists: sokol_gfx on the
 * window's device (wgf_platform_priv_get_environment). Stopping frees everything gfx
 * holds: its nodes, textures, and fonts. False when sokol_gfx couldn't start. */
bool wgf_gfx_priv_start(void);
void wgf_gfx_priv_stop(void);

/* A frame, at the window's size and DPI scale: recorded between begin and end, and
 * drawn at end into the window's swapchain. in_frame: between the two. */
void wgf_gfx_priv_begin_frame(void);
void wgf_gfx_priv_end_frame(void);
bool wgf_gfx_priv_is_in_frame(void);

/* Between wgf_gfx_priv_start and _stop: the GPU can be used. Drawing is recorded only
 * inside a frame, so a draw outside one can't land in the next. */
bool wgf_gfx_priv_render_is_running(void);

/* The frame's immediate mode recording (sokol_gl), for gfx's drawing code: made
 * current, with the 2D projection -- logical pixels from the top-left, y down,
 * blended, no depth -- and an identity model-view. Call before recording, since
 * other code (fontstash) may have changed sokol_gl's state. */
void wgf_gfx_priv_render_set_2d(void);

/* What immediate mode keeps between frames (a polyline's scratch), freed at gfx's stop. */
void wgf_gfx_priv_draw_shutdown(void);

/* From wgf_presentation_set: how the bars are filled from here on, when a mode has
 * them; and the fill itself, which only that hook reaches. */
void wgf_gfx_priv_render_set_bars(void (*fill)(void));
void wgf_gfx_priv_render_fill_visible(void);

/* What the frame shows, in logical pixels (the presentation's visible area,
 * wgf_presentation.h): where a canvas centers its view and the UI lays out. */
void wgf_gfx_priv_render_get_visible(float *x, float *y, float *width, float *height);

/* The clip drawing is under now (wgf_render_push_clip), in logical pixels; the whole
 * frame when none is pushed, which is when this is false. */
bool wgf_gfx_priv_render_get_clip(float *x, float *y, float *width, float *height);

/* The immediate mode recording's room, for tests: it starts at 65536 vertices and
 * 16384 commands, and a frame that runs out has the draws past it dropped, and
 * doubles the room for the frames after it. */
int wgf_gfx_priv_render_get_vertex_capacity(void);
int wgf_gfx_priv_render_get_command_capacity(void);

/* Frames numbered from 1 as they begin: the frame under way, and the last one that
 * ended (0 before any has). */
unsigned wgf_gfx_priv_render_get_frame(void);
unsigned wgf_gfx_priv_render_get_last_frame(void);

#endif
