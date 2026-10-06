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

#ifdef __cplusplus
}
#endif

#endif
