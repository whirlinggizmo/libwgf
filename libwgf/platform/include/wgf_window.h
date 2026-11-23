#ifndef WGF_WINDOW_H
#define WGF_WINDOW_H

#include <stdbool.h>

#include "wgf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The program's window. On the web it is the page's canvas, laid out by the page.
 *
 * Set before wgf_app_run, these calls say how the window opens. After it opens, they change it
 * where the platform can. Sizes are in logical pixels: gfx draws at the display's density
 * inside them (wgf_render_get_dpi_scale). */

/* Sets or gets the window's title (default: "libwgf").
 *
 * Natively it is the window's title bar. On the web it is the page's title. NULL counts as "". */
WGF_API void wgf_window_set_title(const char *title);
WGF_API const char *wgf_window_get_title(void);

/* Sets or gets the size inside the window's frame, in logical pixels (default: 1024 by 768).
 *
 * Returns false for a width or height under 1.
 *
 * On the web the window is the page's canvas, and the page sizes it:
 *
 * - Setting the size changes nothing there, and returns true.
 * - The getters return the canvas's size on the page, before the run and during it.
 * - A page that wants a fixed size gives the canvas one in its CSS.
 *
 * A game written for one size sets a presentation (wgf_presentation.h). That fits the game to
 * whatever size the window is. Natively, it also opens the window at that size when this size
 * isn't set. */
WGF_API bool wgf_window_set_size(int width, int height);
WGF_API int wgf_window_get_width(void);
WGF_API int wgf_window_get_height(void);

/* Asks for the window to fill the screen, or to stop (default: off).
 *
 * The answer is wgf_window_is_fullscreen, not the return value.
 * - Returns false if there is nothing to request (see wgf_window_can_fullscreen).
 * - Returns true if the request was made. That doesn't mean it was granted. Even on the
 *   desktop, the switch has not happened yet when this returns.
 *
 * On the web the browser grants it only inside a tap, a click, or a key press, and a frame
 * comes after those. So a refused request is held, and asked again inside the next one.
 * - Asked as Play is pressed, it is granted by that tap's release or the next.
 * - Asked at start, it is granted by the player's first tap anywhere.
 * - Once granted, it is no longer held. A player who leaves fullscreen stays out until it is
 *   asked again.
 *
 * Asked before the window opens, the window opens filling the screen. On the web the request
 * is held for the first tap.
 *
 * The call is named request_ because the platform may refuse it, as every such call is. */
WGF_API bool wgf_window_request_fullscreen(bool fullscreen);
WGF_API bool wgf_window_is_fullscreen(void);

/* Returns whether the window can fill the screen at all.
 *
 * It is true on the desktop. On the web it is what the browser says: false in a frame that
 * doesn't allow it. Ask before offering a fullscreen key. */
WGF_API bool wgf_window_can_fullscreen(void);

/* Sets or gets where the window is: its top-left in the desktop's coordinates.
 *
 * 0, 0 is the top-left of the main monitor. These work only once the window is open. Setting
 * returns false, and getting returns 0, where programs don't place their windows: the web, and
 * a Wayland desktop, whose compositor places them. */
WGF_API bool wgf_window_set_position(int x, int y);
WGF_API int wgf_window_get_x(void);
WGF_API int wgf_window_get_y(void);

/* Lists the monitors, numbered 0 up to the count, and sets or gets the one the window is on.
 *
 * - There is at least one monitor: the screen.
 * - Moving the window to another monitor returns false where programs don't place their
 *   windows.
 * - A monitor's size is in logical pixels, and its position in the desktop's coordinates.
 * - A monitor that isn't there gives 0 and "".
 *
 * These work only once the window is open. */
WGF_API int wgf_window_get_monitor_count(void);
WGF_API bool wgf_window_set_monitor(int monitor);
WGF_API int wgf_window_get_monitor(void);
WGF_API int wgf_window_get_monitor_width(int monitor);
WGF_API int wgf_window_get_monitor_height(int monitor);
WGF_API int wgf_window_get_monitor_x(int monitor);
WGF_API int wgf_window_get_monitor_y(int monitor);
WGF_API const char *wgf_window_get_monitor_name(int monitor);

/* Sets or gets whether the window is shown or hidden (default: shown).
 *
 * The program runs either way. Hiding the window at open, then showing it once loaded, avoids
 * showing an empty window. */
WGF_API void wgf_window_set_visible(bool visible);
WGF_API bool wgf_window_is_visible(void);

/* Sets or gets whether the user can resize the window (default: true), and whether it has a
 * title bar and border (default: true).
 *
 * These are for the desktop only. The web ignores them. */
WGF_API void wgf_window_set_resizable(bool resizable);
WGF_API bool wgf_window_is_resizable(void);
WGF_API void wgf_window_set_decorated(bool decorated);
WGF_API bool wgf_window_is_decorated(void);

/* Returns whether the window has the keyboard. A game that wants to pause on losing it does so
 * itself (wgf_loop_set_time_scale). */
WGF_API bool wgf_window_is_focused(void);

/* Returns whether the window's size (wgf_window_get_width, _height) differs from its size as
 * the last frame ended. It is true for that one frame. */
WGF_API bool wgf_window_is_resized(void);

/* Sets the window's icon (on the web, the page's) from RGBA pixels, `width` by `height`.
 *
 * `rgba` holds `size` bytes: width x height x 4, row by row from the top.
 *
 * Returns false for no pixels, a width or height of 0 or over 1024, or a byte count that
 * doesn't fit them. */
WGF_API bool wgf_window_set_icon(const unsigned char *rgba, int size, int width, int height);

/* The options fixed when the window opens. Add them together with |. */
typedef enum wgf_window_flag_t {
    WGF_WINDOW_FLAG_NONE = 0,
    WGF_WINDOW_FLAG_TRANSPARENT = 1, /* what is behind the window shows through where gfx clears with alpha below 1 */
    WGF_WINDOW_FLAG_MSAA = 2,        /* smooth edges by drawing with 4 samples a pixel */
    WGF_WINDOW_FLAG_VSYNC_OFF = 4,   /* don't wait for the display between frames */
    WGF_WINDOW_FLAG_LOW_DPI = 8      /* draw one pixel per logical pixel, scaled up by the display */
} wgf_window_flag_t;

/* Sets or gets the options fixed when the window opens (default: WGF_WINDOW_FLAG_NONE).
 *
 * Call it before wgf_app_run, with every flag wanted: it replaces the flags set before.
 *
 *   TRANSPARENT  Desktop: where the window system allows it. Web: the page under the canvas
 *                shows.
 *   MSAA         A request. A platform may give fewer samples, and gfx draws with what the
 *                window got (wgf_window_get_msaa_samples).
 *   VSYNC_OFF    Frames run as fast as they can, or at the target fps. The web is always in
 *                step with the display.
 *   LOW_DPI      Fewer pixels and softer edges. Without it, drawing is at the display's full
 *                density.
 *
 * Returns false, changing nothing, once the window has opened (logged), or for a bit that
 * isn't a flag. */
WGF_API bool wgf_window_set_flags(int flags);
WGF_API int wgf_window_get_flags(void);

/* Returns each open flag's answer: transparent, drawing at full density, in step with the
 * display, and MSAA asked for. */
WGF_API bool wgf_window_is_transparent(void);
WGF_API bool wgf_window_is_high_dpi(void);
WGF_API bool wgf_window_is_vsync(void);
WGF_API bool wgf_window_is_msaa(void);

/* Returns how many samples a pixel the window draws with: 1 without MSAA.
 *
 * Before the window opens, it is what was asked for (4 with WGF_WINDOW_FLAG_MSAA). Once open,
 * it is what the window got, which may be fewer. */
WGF_API int wgf_window_get_msaa_samples(void);

/* Opens `url` in the system's browser, such as a store page or the credits' links.
 *
 * On the web it opens in a new tab, and only in the frame of an input, as browsers allow.
 *
 * Returns false for NULL, an empty URL, or one the system refused. */
WGF_API bool wgf_window_open_url(const char *url);

/* Returns the safe area's insets: how far in from each edge of the window the display covers
 * or keeps for itself, in logical pixels.
 *
 * That covered area is a phone's notch, rounded corners, and home indicator, where a HUD and
 * buttons shouldn't go.
 * - On the web, they are the page's CSS safe-area insets (env(safe-area-inset-*)), less what
 *   lies outside the canvas, each at most the window's size. A browser reports them only to a
 *   page whose viewport meta has viewport-fit=cover, as libwgf's pages do.
 * - Natively they are 0, since nothing covers a desktop's window.
 *
 * They are read as they are now, and change as the device turns or the window is resized. */
WGF_API int wgf_window_get_safe_inset_top(void);
WGF_API int wgf_window_get_safe_inset_right(void);
WGF_API int wgf_window_get_safe_inset_bottom(void);
WGF_API int wgf_window_get_safe_inset_left(void);

/* Which way up the screen is held. */
typedef enum wgf_window_orientation_t {
    WGF_WINDOW_ORIENTATION_ANY = 0,       /* as the device is held, with no lock */
    WGF_WINDOW_ORIENTATION_LANDSCAPE = 1, /* wider than tall */
    WGF_WINDOW_ORIENTATION_PORTRAIT = 2   /* taller than wide */
} wgf_window_orientation_t;

/* Asks for the screen to be held LANDSCAPE or PORTRAIT whichever way the device turns, or to
 * let go (ANY).
 *
 * The answer is wgf_window_get_orientation_lock, a frame or more later, not the return value.
 *
 * On the web this is the browser's screen.orientation.lock. Most browsers grant it only to a
 * page in fullscreen on a phone (Android's Chrome).
 * - Asked outside fullscreen, the request is held, and asked again as the page goes fullscreen
 *   (wgf_window_request_fullscreen).
 * - A refusal in fullscreen is warned about at the next frame, with the browser's reason.
 *
 * Returns false for a value that isn't an orientation, and where nothing can lock: natively,
 * and in a browser without screen.orientation.lock (iOS).
 *
 * Where the lock is refused, a game says "turn your device" while wgf_window_get_orientation
 * is the wrong one. */
WGF_API bool wgf_window_request_orientation(wgf_window_orientation_t orientation);
/* Returns the lock granted: LANDSCAPE or PORTRAIT, or ANY with no lock.
 *
 * The browser lets go of the lock as the page leaves fullscreen, so outside fullscreen it reads
 * ANY. It changes only at a frame's start. */
WGF_API wgf_window_orientation_t wgf_window_get_orientation_lock(void);
/* Returns whether an orientation lock can be asked for at all.
 *
 * On the web it is true in a browser with screen.orientation.lock on a device with touch. A
 * desktop browser's lock is always refused. Natively it is false. Ask before offering a lock. */
WGF_API bool wgf_window_can_lock_orientation(void);
/* Returns which way the window is now, by its shape (wgf_window_get_width against _height).
 *
 * Returns PORTRAIT when it is taller than wide, and LANDSCAPE otherwise. It never returns ANY. */
WGF_API wgf_window_orientation_t wgf_window_get_orientation(void);

/* The device's operating system, and what the device has.
 *
 * - wgf_window_has_touch: whether it has touch, to decide on showing on-screen controls.
 * - wgf_window_has_fine_pointer: whether it has a fine pointer (a mouse or a pad), so hover
 *   matters.
 * - wgf_window_get_os: its system. On the web, it is the system the browser runs on. */
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
/* Returns whether the program runs in a web page. */
WGF_API bool wgf_window_is_web(void);

#ifdef __cplusplus
}
#endif

#endif
