#ifndef WGF_RENDER_H
#define WGF_RENDER_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_texture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The frame gfx is drawing into: the window. A frame is recorded while the program's
 * frame callback runs, and drawn when it returns. */

/* What each frame starts cleared to. Default: black. */
WGF_API void wgf_render_set_clear_color(wgf_color_t color);
WGF_API wgf_color_t wgf_render_get_clear_color(void);


/* The frame's size in pixels, and how many pixels make one logical pixel (2 on most
 * high-density displays); 0, 0, and 1 while there is no surface. Drawing is in logical
 * pixels: the frame is get_width() / get_dpi_scale() of them wide. Inside a render target
 * (wgf_render_begin_target), the target's size, and 1: drawing there is in its own
 * pixels, with no presentation (wgf_presentation.h), and a stage's camera sees it at its
 * aspect ratio. */
WGF_API int wgf_render_get_width(void);
WGF_API int wgf_render_get_height(void);
WGF_API float wgf_render_get_dpi_scale(void);

/* Draw into a render target (wgf_texture_create_target) instead of the screen, until
 * wgf_render_end_target, as libwgt's. Inside a frame, and everything draws there:
 * stages, sprites, particles, text, the UI, immediate mode. Targets are drawn before the
 * screen, in the order begun, so the screen shows what they drew this frame; a target
 * begun again in the same frame keeps what it drew before. Not nested. A target isn't
 * read while it is drawn into: used inside its own pass, it draws nothing there (warned
 * once). False (logged) outside a frame, inside another target, for a texture that isn't
 * a target, or past 16 target passes a frame. A frame that ends inside a target ends it
 * (warned). */
WGF_API bool wgf_render_begin_target(wgf_texture_t target);
WGF_API void wgf_render_end_target(void);

/* What a pass into `target` starts cleared to, kept with it until changed. Default:
 * transparent black. False, and transparent black read back, for a texture that isn't a
 * render target. */
WGF_API bool wgf_render_set_target_clear_color(wgf_texture_t target, wgf_color_t color);
WGF_API wgf_color_t wgf_render_get_target_clear_color(wgf_texture_t target);

/* Clip drawing to a rectangle (logical pixels, top-left origin; a render target's own
 * pixels inside one) until the matching pop. Clips nest: each push intersects with the
 * clip it's pushed inside, so a scroll area inside a panel stays inside the panel.
 * Everything drawn between is clipped: immediate mode, and the 2D stages drawn there. A
 * width or height of 0 clips everything away. A render target's pass starts unclipped,
 * and the screen's clips apply again when it ends. Every push should be popped within
 * the frame (and a target's within its pass): unmatched ones are dropped at its end,
 * with a warning, as is a pop without a push. Up to 32 deep; a push past that clips
 * nothing more, warned once. Outside a frame: nothing, warned. */
WGF_API void wgf_render_push_clip(float x, float y, float width, float height);
WGF_API void wgf_render_pop_clip(void);

#ifdef __cplusplus
}
#endif

#endif
