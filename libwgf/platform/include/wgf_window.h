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
 * granted, and even on the desktop the switch has not happened yet when this returns.
 * On the web the browser grants it only inside a tap, a click, or a key press, which a
 * frame comes after: refused, the request is held and asked again inside the next one, so
 * asked as Play is pressed it is granted by that tap's release or the next, and asked at
 * start it is granted by the player's first tap anywhere. Named request_ for that reason
 * (wgrender's), as every request the platform may refuse is (CONVENTIONS.md, "Naming").
 * Before the window opens, it opens filling the screen (natively; on the web, held for the
 * first tap). */
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

/* Whether the window's size (wgf_window_get_width, _height) is other than it was as the last
 * frame ended: raylib's IsWindowResized, true for that frame. */
WGF_API bool wgf_window_is_resized(void);

/* The window's icon (on the web, the page's), from RGBA pixels `width` by `height` (`size`
 * bytes: width x height x 4, row by row from the top): raylib's SetWindowIcon. False for no
 * pixels, a size of 0 or over 1024 either way, or a byte count that doesn't fit it. */
WGF_API bool wgf_window_set_icon(const unsigned char *rgba, int size, int width, int height);

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

/* A URL opened in the system's browser (on the web, a new tab: in the frame of an input, as
 * browsers allow): a store page, the credits' links. False for NULL, an empty one, or one
 * the system refused. */
WGF_API bool wgf_window_open_url(const char *url);

/* The safe area: how far in from each edge of the window, in logical pixels, the display
 * covers or keeps for itself (a phone's notch, rounded corners, home indicator), where a HUD
 * and buttons shouldn't go. On the web, the page's CSS safe-area insets
 * (env(safe-area-inset-*), which a browser reports only to a page whose viewport meta has
 * viewport-fit=cover, as libwgf's pages' do), less what lies outside the canvas, each at
 * most the window's size; natively 0 (a desktop's window has nothing over it). Read as
 * they are now: they change as the device turns or the window is resized. */
WGF_API int wgf_window_get_safe_inset_top(void);
WGF_API int wgf_window_get_safe_inset_right(void);
WGF_API int wgf_window_get_safe_inset_bottom(void);
WGF_API int wgf_window_get_safe_inset_left(void);

/* Which way up the screen is held. */
typedef enum wgf_window_orientation_t {
    WGF_WINDOW_ORIENTATION_ANY = 0,       /* as the device is held: no lock */
    WGF_WINDOW_ORIENTATION_LANDSCAPE = 1, /* wider than tall */
    WGF_WINDOW_ORIENTATION_PORTRAIT = 2   /* taller than wide */
} wgf_window_orientation_t;

/* Ask for the screen to be held LANDSCAPE or PORTRAIT whichever way the device turns, or
 * let go (ANY); the answer is wgf_window_get_orientation_lock, not the return, a frame or
 * more later. On the web, the browser's screen.orientation.lock, which most browsers grant
 * only to a page in fullscreen on a phone (Android's Chrome): asked outside fullscreen, it is
 * held and asked again as the page goes fullscreen (wgf_window_request_fullscreen); a
 * refusal in fullscreen is warned of, with the browser's reason, at the next frame. False for a value that isn't one, and where
 * nothing can lock: natively, and a browser without screen.orientation.lock (iOS). Where
 * the lock is refused, a game says "turn your device" while wgf_window_get_orientation is
 * the wrong one. */
WGF_API bool wgf_window_request_orientation(wgf_window_orientation_t orientation);
/* The lock granted: LANDSCAPE or PORTRAIT, or ANY with none. The browser lets go of it as
 * the page leaves fullscreen, so outside fullscreen it reads ANY. Changes only at a
 * frame's start. */
WGF_API wgf_window_orientation_t wgf_window_get_orientation_lock(void);
/* Whether a lock can be asked for at all: on the web a browser with screen.orientation.lock
 * on a device with touch (a desktop browser's lock is always refused); natively false. Ask
 * before offering it. */
WGF_API bool wgf_window_can_lock_orientation(void);
/* Which way the window is now, by its shape (wgf_window_get_width against _height):
 * PORTRAIT when taller than wide, else LANDSCAPE; never ANY. */
WGF_API wgf_window_orientation_t wgf_window_get_orientation(void);

/* What the device has: touch (to show on-screen controls), a fine pointer (a mouse or a
 * pad: hover means something), and its system, on the web the one the browser runs on. */
typedef enum wgf_window_os_t {
    WGF_WINDOW_OS_OTHER = 0,
    WGF_WINDOW_OS_LINUX = 1,
    WGF_WINDOW_OS_WINDOWS = 2,
    WGF_WINDOW_OS_MACOS = 3,
    WGF_WINDOW_OS_ANDROID = 5,
    WGF_WINDOW_OS_IOS = 6
} wgf_window_os_t;
WGF_API bool wgf_window_has_touch(void);
WGF_API bool wgf_window_has_fine_pointer(void);
WGF_API wgf_window_os_t wgf_window_get_os(void);
/* Whether it runs in a page. */
WGF_API bool wgf_window_is_web(void);

#ifdef __cplusplus
}
#endif

#endif
