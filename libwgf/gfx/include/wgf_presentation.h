#ifndef WGF_PRESENTATION_H
#define WGF_PRESENTATION_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How a game's design resolution is fitted to the window or the screen.
 *
 * A game is written for a design resolution. Its logical coordinates stay the design's at any
 * window size and in fullscreen. That holds for drawing, 2D stages, the UI, the pointer and
 * touches (mapped back into those coordinates), and an autopilot's mouse. It works the same
 * on the desktop, in a resized browser window, and in fullscreen.
 *
 *   NONE     the default: logical pixels are the window's (a framebuffer pixel divided by
 *            the DPI scale), and the visible area is the window
 *   STRETCH  the design scaled to the window on each axis separately, losing its aspect
 *   FIT      scaled alike on both axes, as large as fits whole, and centered; the rest
 *            is bars (wgf_presentation_set_bar_color), with nothing drawn in them
 *   FILL     scaled alike, as small as covers the window, and centered; whatever is past
 *            the window's edges is cropped
 *   EXPAND   scaled as FIT is, but the visible area grows past the design to the
 *            window's edges, centered on it; the design is the least the game shows,
 *            and a UI anchored to the visible area's edges stays at them
 *   INTEGER  as FIT, at a whole number of framebuffer pixels to a logical one (at
 *            least 1), for pixel art; with bars around it
 *
 * wgf_presentation_set sets the mode and the design size.
 * - Set it any time. It takes effect at the next frame.
 * - A program that never sets a mode links none of the fitting code.
 * - Natively, a window whose size the program didn't set (wgf_window_set_size) opens at the
 *   design size.
 * - It returns false for a size under 1 (except with NONE, which takes no size), or a mode
 *   that isn't one. */
typedef enum wgf_presentation_mode_t {
    WGF_PRESENTATION_MODE_NONE = 0,
    WGF_PRESENTATION_MODE_STRETCH = 1,
    WGF_PRESENTATION_MODE_FIT = 2,
    WGF_PRESENTATION_MODE_FILL = 3,
    WGF_PRESENTATION_MODE_EXPAND = 4,
    WGF_PRESENTATION_MODE_INTEGER = 5
} wgf_presentation_mode_t;

WGF_API bool wgf_presentation_set(wgf_presentation_mode_t mode, int width, int height);
WGF_API wgf_presentation_mode_t wgf_presentation_get_mode(void);

/* Sets or gets the color of the bars (default: black).
 *
 * The bars, under FIT and INTEGER, are the framebuffer outside the visible area. Nothing is
 * drawn in them. */
WGF_API void wgf_presentation_set_bar_color(wgf_color_t color);
WGF_API wgf_color_t wgf_presentation_get_bar_color(void);
/* Returns the design resolution, or the window's logical size under NONE. */
WGF_API int wgf_presentation_get_width(void);
WGF_API int wgf_presentation_get_height(void);

/* Returns what is visible now, in logical coordinates, as x, y, width, height.
 *
 * - Under STRETCH, FIT, and INTEGER, it is the design area: 0, 0, its width, its height.
 * - Under FILL, it is less of the design area.
 * - Under EXPAND, it is more, with x and y below 0 when it grows.
 *
 * A HUD anchored to a corner places itself from this. */
WGF_API wgf_vec4_t wgf_presentation_get_visible(void);

/* Returns how many framebuffer pixels make a logical pixel, across.
 *
 * Under STRETCH each axis has its own scale, and this returns the horizontal one. */
WGF_API float wgf_presentation_get_scale(void);

#ifdef __cplusplus
}
#endif

#endif
