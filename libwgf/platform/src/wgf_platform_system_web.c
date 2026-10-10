#include "wgf_platform_priv.h"

#include <emscripten.h>
#include <emscripten/eventloop.h>

#include "sokol_app.h"

/* The window on the web, where it is the page's canvas: what differs from the
 * desktop. */

/* The canvas's size on the page, in CSS pixels (logical); sokol_app reads it again
 * on a resize event, so one is sent. Whether it took: the page's CSS can override
 * it. */
EM_JS(int, canvas_set_size, (int width, int height), {
    const canvas = Module.canvas;
    if (!canvas) return 0;
    canvas.style.width = width + "px";
    canvas.style.height = height + "px";
    window.dispatchEvent(new Event("resize"));
    const rect = canvas.getBoundingClientRect();
    return Math.round(rect.width) === width && Math.round(rect.height) === height ? 1 : 0;
})
EM_JS(int, document_has_focus, (void), { return document.hasFocus() ? 1 : 0; })
EM_JS(int, screen_width, (void), { return window.screen.width | 0; })
EM_JS(int, screen_height, (void), { return window.screen.height | 0; })
/* False in an iframe without allowfullscreen, or under a permissions policy. */
EM_JS(int, document_fullscreen_enabled, (void), { return document.fullscreenEnabled ? 1 : 0; })

void wgf_platform_priv_sokol_hold(bool hold)
{
    if (hold) {
        emscripten_runtime_keepalive_push();
    } else {
        emscripten_runtime_keepalive_pop();
    }
}

void wgf_platform_priv_sokol_open(int width, int height, bool size_set)
{
    if (size_set) canvas_set_size(width, height);
}

bool wgf_platform_priv_set_size(int width, int height)
{
    return canvas_set_size(width, height) != 0;
}

bool wgf_platform_priv_can_quit(void)
{
    return false; /* a page has nothing to quit to */
}

/* The canvas is the window: it has no position, and there is one monitor, the
 * screen. */
bool wgf_platform_priv_set_position(int x, int y)
{
    (void)x, (void)y;
    return false;
}

bool wgf_platform_priv_get_position(int *x, int *y)
{
    *x = *y = 0;
    return false;
}

int wgf_platform_priv_get_monitor_count(void)
{
    return 1;
}

int wgf_platform_priv_get_monitor(void)
{
    return 0;
}

bool wgf_platform_priv_set_monitor(int monitor)
{
    return monitor == 0;
}

bool wgf_platform_priv_get_monitor_rect(int monitor, int *x, int *y, int *width, int *height)
{
    *x = *y = *width = *height = 0;
    if (monitor != 0) return false;
    *width = screen_width();
    *height = screen_height();
    return true;
}

const char *wgf_platform_priv_get_monitor_name(int monitor)
{
    return monitor == 0 ? "screen" : "";
}

bool wgf_platform_priv_has_fullscreen(void)
{
    return document_fullscreen_enabled() != 0;
}

bool wgf_platform_priv_is_focused(void)
{
    return document_has_focus() != 0;
}

/* The browser offers a frame each time the display shows one: run a frame due
 * within half of one, so 30 fps on a 60 Hz display runs every other frame instead
 * of drifting. */
bool wgf_platform_priv_pace(double wait)
{
    return wait <= 0.5 * wgf_platform_priv_get_frame_duration();
}

EM_JS(void, wgf_platform_js_mark, (const char *name), { performance.mark(UTF8ToString(name)); })

void wgf_platform_priv_mark(const char *name)
{
    wgf_platform_js_mark(name);
}
