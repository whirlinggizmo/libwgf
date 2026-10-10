#ifndef WGF_PRESENTATION_H
#define WGF_PRESENTATION_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_color.h"
#include "wgf_vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How a game's design is fitted to the window or the screen (Godot's stretch modes
 * and aspects are the prior art). A game is written for a design resolution, and its
 * logical coordinates stay the design's at any window size and in fullscreen: drawing,
 * 2D stages, the UI, the pointer and touches (mapped back into them), and an autopilot's
 * mouse. The same on the desktop, in a resized browser window, and in fullscreen.
 *
 *   NONE     the default: logical pixels are the window's (a framebuffer pixel over the
 *            DPI scale), and the visible area is the window
 *   STRETCH  the design scaled to the window on each axis apart, its aspect lost
 *   FIT      scaled alike on both axes, as large as it fits whole, centered; the rest
 *            are bars (wgf_presentation_set_bar_color), nothing drawn in them
 *   FILL     scaled alike, as small as covers the window, centered; what is past the
 *            window's edges is cropped
 *   EXPAND   scaled as FIT is, but the visible area grows past the design to the
 *            window's edges, centered on it: the design is the least the game shows,
 *            and a UI anchored to the visible area's edges stays at them
 *   INTEGER  as FIT, at a whole number of framebuffer pixels to a logical one (at
 *            least 1), for pixel art; bars around it
 *
 * Set any time; it takes effect at the next frame. A program that never sets a mode
 * links none of its fitting. Natively, a window whose size the program didn't set
 * (wgf_window_set_size) opens at the design size. False for a size under 1 (but for
 * NONE, which takes none) or a mode that isn't one. */
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

/* The bars (FIT and INTEGER): the framebuffer outside the visible area, which nothing is
 * drawn in. Default: black. */
WGF_API void wgf_presentation_set_bar_color(wgf_color_t color);
WGF_API wgf_color_t wgf_presentation_get_bar_color(void);
/* The design resolution; the window's logical size under NONE. */
WGF_API int wgf_presentation_get_width(void);
WGF_API int wgf_presentation_get_height(void);

/* What is visible now, in logical coordinates: x, y, width, height. The design area
 * (0, 0, its width, its height) under STRETCH, FIT, and INTEGER; less of it under FILL;
 * more under EXPAND, x and y below 0 when it grows. A HUD anchored to a corner places
 * itself from this. */
WGF_API wgf_vec4_t wgf_presentation_get_visible(void);

/* Framebuffer pixels to a logical pixel, across (under STRETCH, each axis has its own:
 * this is the horizontal one). */
WGF_API float wgf_presentation_get_scale(void);

#ifdef __cplusplus
}
#endif

#endif
