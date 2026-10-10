#include "wgf_window.h"

#include <stdio.h>
#include <string.h>

#include "wgf_platform_window_priv.h"
#include "wgf_mouse.h"
#include "wgf_log.h"

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
    desc->size_set = window.size_set;
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
}

bool wgf_platform_priv_window_is_open(void)
{
    return window.open;
}

void wgf_window_set_title(const char *title)
{
    snprintf(window.title, sizeof(window.title), "%s", title != NULL ? title : "");
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
    if (!window.open) return window.width;
    return (int)((float)wgf_platform_priv_get_framebuffer_width() / wgf_platform_priv_get_dpi_scale() + 0.5f);
}

int wgf_window_get_height(void)
{
    if (!window.open) return window.height;
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

/* An option only an unopened window takes. */
static bool set_at_open(bool *option, bool value, const char *name)
{
    if (window.open) {
        wgf_log_warn("wgf_app_window_set_%s: the window is open; set it before wgf_app_run", name);
        return false;
    }
    *option = value;
    return true;
}

bool wgf_window_set_transparent(bool transparent)
{
    return set_at_open(&window.transparent, transparent, "transparent");
}

bool wgf_window_is_transparent(void)
{
    return window.transparent;
}

bool wgf_window_set_high_dpi(bool high_dpi)
{
    return set_at_open(&window.high_dpi, high_dpi, "high_dpi");
}

bool wgf_window_is_high_dpi(void)
{
    return window.high_dpi;
}

bool wgf_window_set_vsync(bool vsync)
{
    return set_at_open(&window.vsync, vsync, "vsync");
}

bool wgf_window_is_vsync(void)
{
    return window.vsync;
}

bool wgf_window_set_msaa(bool msaa)
{
    return set_at_open(&window.msaa, msaa, "msaa");
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
