#ifndef WGF_WINDOW_H
#define WGF_WINDOW_H

#include <stdbool.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The program's window. On the web it is the page's canvas, laid out by the page.
 *
 * Set before wgf_app_run, these say how the window opens; after it opens they
 * change it, where the platform can. Sizes are logical pixels: gfx draws at the
 * display's density inside them (wgf_render_get_dpi_scale). */

/* The title (default "libwgf"). Natively the window's title bar; on the web the
 * page's title. NULL counts as "". */
WGF_API void wgf_window_set_title(const char *title);
WGF_API const char *wgf_window_get_title(void);

/* The size inside the window's frame (default 1024 by 768). Values under 1 are
 * refused (false). On the web, the canvas's size on the page: left to the page
 * unless set. Set, it is the canvas's own style, which wins over the page's: the
 * canvas stays that size, at the page's top left, whatever the browser window does,
 * so a game meant to fill the page doesn't set it on the web. A game written for one
 * size sets a presentation (wgf_presentation.h) instead, which fits it to whatever the
 * window is, and natively opens the window at that size when this isn't set. */
WGF_API bool wgf_window_set_size(int width, int height);
WGF_API int wgf_window_get_width(void);
WGF_API int wgf_window_get_height(void);

/* Ask to fill the screen, or to stop (default off); the answer is
 * wgf_window_is_fullscreen, not the return. False where there is nothing to request
 * (wgf_window_can_fullscreen); true means the request was made, not that it was
 * granted: on the web the browser grants it only during a key press or click, and it
 * arrives a frame or more later, and even on the desktop the switch has not happened
 * yet when this returns. Named request_ for that reason (wgrender's), as every request
 * the platform may refuse is (CONVENTIONS.md, "Naming"). Before the window opens, it
 * opens filling the screen. */
WGF_API bool wgf_window_request_fullscreen(bool fullscreen);
WGF_API bool wgf_window_is_fullscreen(void);

/* Whether the window can fill the screen at all: true on the desktop, and on the web
 * what the browser says (false in a frame that doesn't allow it). Ask before
 * offering a fullscreen key. */
WGF_API bool wgf_window_can_fullscreen(void);

/* Where the window is: its top-left in the desktop's coordinates (0, 0 being the top-
 * left of the main monitor). False, and 0, where programs don't place their windows:
 * the web, and a Wayland desktop, whose compositor places them. Only once the window
 * is open. */
WGF_API bool wgf_window_set_position(int x, int y);
WGF_API int wgf_window_get_x(void);
WGF_API int wgf_window_get_y(void);

/* The monitors, 0 up to the count, and the one the window is on: at least one, the
 * screen. Moving the window to another is false where programs don't place their
 * windows. A monitor's size is in logical pixels, its position in the desktop's
 * coordinates; 0 and "" for one that isn't there. Only once the window is open. */
WGF_API int wgf_window_get_monitor_count(void);
WGF_API bool wgf_window_set_monitor(int monitor);
WGF_API int wgf_window_get_monitor(void);
WGF_API int wgf_window_get_monitor_width(int monitor);
WGF_API int wgf_window_get_monitor_height(int monitor);
WGF_API int wgf_window_get_monitor_x(int monitor);
WGF_API int wgf_window_get_monitor_y(int monitor);
WGF_API const char *wgf_window_get_monitor_name(int monitor);

/* Shown (default) or hidden; the program runs either way. Hidden at open, then
 * shown once loaded, avoids showing an empty window. */
WGF_API void wgf_window_set_visible(bool visible);
WGF_API bool wgf_window_is_visible(void);

/* Whether the user can resize the window (default true), and whether it has a
 * title bar and border (default true). Desktop only: the web ignores them. */
WGF_API void wgf_window_set_resizable(bool resizable);
WGF_API bool wgf_window_is_resizable(void);
WGF_API void wgf_window_set_decorated(bool decorated);
WGF_API bool wgf_window_is_decorated(void);

/* Whether the window has the keyboard. A game pauses itself on losing it, if it
 * wants to (wgf_loop_set_time_scale). */
WGF_API bool wgf_window_is_focused(void);

/* Options fixed when the window opens: refused (false) once it has.
 *   transparent  what is behind the window shows through where gfx clears with
 *                alpha below 1 (default off). Desktop: where the window system
 *                allows it; web: the page under the canvas
 *   high_dpi     draw at the display's full density (default on); off draws one
 *                pixel per logical pixel, scaled up by the display: fewer pixels,
 *                softer edges
 *   vsync        wait for the display between frames (default on); off runs as
 *                fast as it can, or at the target fps. The web is always in step
 *                with the display
 *   msaa         smooth edges by drawing with 4 samples a pixel (default off): a
 *                request, which a platform may give fewer for; gfx draws with
 *                what the window got */
WGF_API bool wgf_window_set_transparent(bool transparent);
WGF_API bool wgf_window_is_transparent(void);
WGF_API bool wgf_window_set_high_dpi(bool high_dpi);
WGF_API bool wgf_window_is_high_dpi(void);
WGF_API bool wgf_window_set_vsync(bool vsync);
WGF_API bool wgf_window_is_vsync(void);
WGF_API bool wgf_window_set_msaa(bool msaa);
WGF_API bool wgf_window_is_msaa(void);

#ifdef __cplusplus
}
#endif

#endif
