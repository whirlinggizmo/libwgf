#ifndef WGF_RENDER_H
#define WGF_RENDER_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"

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
 * pixels: the frame is get_width() / get_dpi_scale() of them wide. */
WGF_API int wgf_render_get_width(void);
WGF_API int wgf_render_get_height(void);
WGF_API float wgf_render_get_dpi_scale(void);

/* Clip drawing to a rectangle (logical pixels, top-left origin) until the matching
 * pop. Clips nest: each push intersects with the clip it's pushed inside, so a scroll
 * area inside a panel stays inside the panel. Everything drawn between is clipped:
 * immediate mode, and the canvases drawn there. A width or height of 0 clips
 * everything away. Every push should be popped within the frame: unmatched ones are
 * dropped at its end, with a warning, as is a pop without a push. Up to 32 deep; a
 * push past that clips nothing more, warned once. Outside a frame: nothing, warned. */
WGF_API void wgf_render_push_clip(float x, float y, float width, float height);
WGF_API void wgf_render_pop_clip(void);

#ifdef __cplusplus
}
#endif

#endif
