#include "wgf_window.h"

#include <stdio.h>
#include <string.h>

#include "wgf_platform_window_priv.h"
#include "wgf_mouse.h"
#include "wgf_log.h"
#include "wgf_core_format_priv.h"

#define TITLE_SIZE 256

static struct {
    bool open;
    char title[TITLE_SIZE];
    int width, height; /* logical; the window's own once it is open */
    bool fullscreen;
    bool visible;
    bool resizable;
    bool decorated;
    bool transparent;
    bool high_dpi;
    bool vsync;
    bool mouse_locked; /* asked for before the window opened */
    bool size_set;     /* by the program, not the default */
    bool msaa;
} window = {false, "libwgf", 1024, 768, false, true, true, true, false, true, true, false, false, false};

static wgf_platform_priv_fitting_t fitting; /* wgf_presentation_set's; NULL for NONE */

void wgf_platform_priv_set_fitting(wgf_platform_priv_fitting_t set, int design_width, int design_height)
{
    fitting = set;
    if (set != NULL && !window.size_set && !window.open) { /* the window opens at the design size */
        window.width = design_width;
        window.height = design_height;
    }
}

bool wgf_platform_priv_is_presented(void)
{
    return fitting != NULL;
}

static void presentation_none(float dpi_scale, wgf_platform_priv_presentation_t *out)
{
    const float scale = dpi_scale >= 1.0f ? dpi_scale : 1.0f;
    out->scale_x = out->scale_y = scale;
    out->offset_x = out->offset_y = 0.0f;
    out->visible_x = out->visible_y = 0.0f;
    out->visible_width = (float)wgf_platform_priv_get_framebuffer_width() / scale;
    out->visible_height = (float)wgf_platform_priv_get_framebuffer_height() / scale;
    out->bars = false;
}

void wgf_platform_priv_get_presentation(wgf_platform_priv_presentation_t *out)
{
    if (fitting != NULL) {
        fitting(wgf_platform_priv_get_framebuffer_width(), wgf_platform_priv_get_framebuffer_height(),
                wgf_platform_priv_get_dpi_scale(), out);
    } else {
        presentation_none(wgf_platform_priv_get_dpi_scale(), out);
    }
}

void wgf_platform_priv_window_describe(wgf_platform_priv_desc_t *desc)
{
    desc->width = window.width;
    desc->height = window.height;
    desc->title = window.title;
    desc->fullscreen = window.fullscreen;
    desc->high_dpi = window.high_dpi;
    desc->vsync = window.vsync;
    desc->transparent = window.transparent;
    desc->resizable = window.resizable;
    desc->decorated = window.decorated;
    desc->visible = window.visible;
    desc->msaa = window.msaa;
}

void wgf_platform_priv_window_set_open(bool open)
{
    window.open = open;
    if (open && window.mouse_locked) wgf_platform_priv_lock_mouse(true);
    if (open && !wgf_mouse_is_cursor_visible()) wgf_platform_priv_show_mouse(false);
#ifdef __EMSCRIPTEN__
    /* the web opens no window filling the screen: asked before, it is held for the first tap */
    if (open && window.fullscreen && wgf_platform_priv_has_fullscreen()) wgf_platform_priv_set_fullscreen(true);
#endif
}

bool wgf_platform_priv_window_is_open(void)
{
    return window.open;
}

/* The size the last frame ended at, for is_resized; -1 before the first. */
static int seen_width = -1, seen_height = -1;

void wgf_platform_priv_window_end_frame(void)
{
    seen_width = wgf_window_get_width();
    seen_height = wgf_window_get_height();
}

bool wgf_window_is_resized(void)
{
    return seen_width >= 0 && (wgf_window_get_width() != seen_width || wgf_window_get_height() != seen_height);
}

bool wgf_window_set_icon(const unsigned char *rgba, int size, int width, int height)
{
    if (rgba == NULL || width <= 0 || height <= 0 || width > 1024 || height > 1024 || size != width * height * 4) {
        return false;
    }
    return wgf_platform_priv_set_icon(rgba, size, width, height);
}

void wgf_window_set_title(const char *title)
{
    wgf_core_priv_format(window.title, sizeof(window.title), "%s", title != NULL ? title : "");
    if (window.open) wgf_platform_priv_set_title(window.title);
}

const char *wgf_window_get_title(void)
{
    return window.title;
}

bool wgf_window_set_size(int width, int height)
{
    if (width < 1 || height < 1) return false;
    if (!window.open) {
        window.width = width;
        window.height = height;
        window.size_set = true;
        return true;
    }
    return wgf_platform_priv_set_size(width, height);
}

int wgf_window_get_width(void)
{
    int width = 0, height = 0;
    if (!window.open) return wgf_platform_priv_get_unopened_size(&width, &height) ? width : window.width;
    return (int)((float)wgf_platform_priv_get_framebuffer_width() / wgf_platform_priv_get_dpi_scale() + 0.5f);
}

int wgf_window_get_height(void)
{
    int width = 0, height = 0;
    if (!window.open) return wgf_platform_priv_get_unopened_size(&width, &height) ? height : window.height;
    return (int)((float)wgf_platform_priv_get_framebuffer_height() / wgf_platform_priv_get_dpi_scale() +
                 0.5f);
}

bool wgf_window_request_fullscreen(bool fullscreen)
{
    if (!window.open) {
        window.fullscreen = fullscreen;
        return true;
    }
    if (!wgf_platform_priv_has_fullscreen()) return false;
    wgf_platform_priv_set_fullscreen(fullscreen);
    return true;
}

bool wgf_window_is_fullscreen(void)
{
    return window.open ? wgf_platform_priv_is_fullscreen() : window.fullscreen;
}

bool wgf_window_can_fullscreen(void)
{
    return window.open && wgf_platform_priv_has_fullscreen();
}

bool wgf_window_set_position(int x, int y)
{
    return window.open && wgf_platform_priv_set_position(x, y);
}

int wgf_window_get_x(void)
{
    int x = 0, y = 0;
    if (window.open) wgf_platform_priv_get_position(&x, &y);
    return x;
}

int wgf_window_get_y(void)
{
    int x = 0, y = 0;
    if (window.open) wgf_platform_priv_get_position(&x, &y);
    return y;
}

int wgf_window_get_monitor_count(void)
{
    return window.open ? wgf_platform_priv_get_monitor_count() : 1;
}

bool wgf_window_set_monitor(int monitor)
{
    return window.open && wgf_platform_priv_set_monitor(monitor);
}

int wgf_window_get_monitor(void)
{
    return window.open ? wgf_platform_priv_get_monitor() : 0;
}

/* One of a monitor's x, y, width, height: 0 for one that isn't there. */
static int monitor_field(int monitor, int field)
{
    int rect[4] = {0, 0, 0, 0};
    if (!window.open || !wgf_platform_priv_get_monitor_rect(monitor, &rect[0], &rect[1], &rect[2], &rect[3])) {
        return 0;
    }
    return rect[field];
}

int wgf_window_get_monitor_x(int monitor)
{
    return monitor_field(monitor, 0);
}

int wgf_window_get_monitor_y(int monitor)
{
    return monitor_field(monitor, 1);
}

int wgf_window_get_monitor_width(int monitor)
{
    return monitor_field(monitor, 2);
}

int wgf_window_get_monitor_height(int monitor)
{
    return monitor_field(monitor, 3);
}

const char *wgf_window_get_monitor_name(int monitor)
{
    return window.open ? wgf_platform_priv_get_monitor_name(monitor) : "";
}

void wgf_window_set_visible(bool visible)
{
    window.visible = visible;
    if (window.open) wgf_platform_priv_set_visible(visible);
}

bool wgf_window_is_visible(void)
{
    return window.open ? wgf_platform_priv_is_visible() : window.visible;
}

void wgf_window_set_resizable(bool resizable)
{
    window.resizable = resizable;
    if (window.open) wgf_platform_priv_set_style(window.resizable, window.decorated);
}

bool wgf_window_is_resizable(void)
{
    return window.resizable;
}

void wgf_window_set_decorated(bool decorated)
{
    window.decorated = decorated;
    if (window.open) wgf_platform_priv_set_style(window.resizable, window.decorated);
}

bool wgf_window_is_decorated(void)
{
    return window.decorated;
}

bool wgf_window_is_focused(void)
{
    return window.open && wgf_platform_priv_is_focused();
}

bool wgf_window_set_flags(int flags)
{
    const int all = WGF_WINDOW_FLAG_TRANSPARENT | WGF_WINDOW_FLAG_MSAA | WGF_WINDOW_FLAG_VSYNC_OFF |
                    WGF_WINDOW_FLAG_LOW_DPI;
    if (window.open) {
        wgf_log_warn("wgf_window_set_flags: the window is open; set them before wgf_app_run");
        return false;
    }
    if ((flags & ~all) != 0) return false;
    window.transparent = (flags & WGF_WINDOW_FLAG_TRANSPARENT) != 0;
    window.msaa = (flags & WGF_WINDOW_FLAG_MSAA) != 0;
    window.vsync = (flags & WGF_WINDOW_FLAG_VSYNC_OFF) == 0;
    window.high_dpi = (flags & WGF_WINDOW_FLAG_LOW_DPI) == 0;
    return true;
}

int wgf_window_get_flags(void)
{
    return (window.transparent ? WGF_WINDOW_FLAG_TRANSPARENT : 0) | (window.msaa ? WGF_WINDOW_FLAG_MSAA : 0) |
           (window.vsync ? 0 : WGF_WINDOW_FLAG_VSYNC_OFF) | (window.high_dpi ? 0 : WGF_WINDOW_FLAG_LOW_DPI);
}

bool wgf_window_is_transparent(void)
{
    return window.transparent;
}

bool wgf_window_is_high_dpi(void)
{
    return window.high_dpi;
}

bool wgf_window_is_vsync(void)
{
    return window.vsync;
}

int wgf_window_get_msaa_samples(void)
{
    if (!window.open) return window.msaa ? 4 : 1;
    return wgf_platform_priv_get_sample_count();
}

bool wgf_window_is_msaa(void)
{
    return window.msaa;
}

void wgf_mouse_set_locked(bool locked)
{
    window.mouse_locked = locked;
    if (window.open) wgf_platform_priv_lock_mouse(locked);
}

bool wgf_mouse_is_locked(void)
{
    return window.open ? wgf_platform_priv_is_mouse_locked() : window.mouse_locked;
}

/* ---------------------------------------------------------------- the browser, the device ---- */

bool wgf_window_open_url(const char *url)
{
    return url != NULL && url[0] != '\0' && wgf_platform_priv_open_url(url);
}

bool wgf_window_has_touch(void)
{
    return wgf_platform_priv_has_touch();
}

bool wgf_window_has_fine_pointer(void)
{
    return wgf_platform_priv_has_fine_pointer();
}

wgf_window_os_t wgf_window_get_os(void)
{
    return (wgf_window_os_t)wgf_platform_priv_get_os();
}

/* ---------------------------------------------------------------- the safe area, orientation ---- */

/* One inset, held to the window's size along its axis. */
static int safe_inset(int side)
{
    int insets[4] = {0, 0, 0, 0};
    int limit, value;
    wgf_platform_priv_get_safe_insets(insets);
    limit = side == 0 || side == 2 ? wgf_window_get_height() : wgf_window_get_width();
    value = insets[side];
    return value < 0 ? 0 : (value > limit ? limit : value);
}

int wgf_window_get_safe_inset_top(void)
{
    return safe_inset(0);
}

int wgf_window_get_safe_inset_right(void)
{
    return safe_inset(1);
}

int wgf_window_get_safe_inset_bottom(void)
{
    return safe_inset(2);
}

int wgf_window_get_safe_inset_left(void)
{
    return safe_inset(3);
}

bool wgf_window_request_orientation(wgf_window_orientation_t orientation)
{
    if (orientation != WGF_WINDOW_ORIENTATION_ANY && orientation != WGF_WINDOW_ORIENTATION_LANDSCAPE &&
        orientation != WGF_WINDOW_ORIENTATION_PORTRAIT) {
        return false;
    }
    return wgf_platform_priv_request_orientation((int)orientation);
}

wgf_window_orientation_t wgf_window_get_orientation_lock(void)
{
    return (wgf_window_orientation_t)wgf_platform_priv_get_orientation_lock();
}

bool wgf_window_can_lock_orientation(void)
{
    return wgf_platform_priv_can_lock_orientation();
}

wgf_window_orientation_t wgf_window_get_orientation(void)
{
    return wgf_window_get_height() > wgf_window_get_width() ? WGF_WINDOW_ORIENTATION_PORTRAIT
                                                            : WGF_WINDOW_ORIENTATION_LANDSCAPE;
}

bool wgf_window_is_web(void)
{
#if defined(__EMSCRIPTEN__)
    return true;
#else
    return false;
#endif
}
