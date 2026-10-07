#include <stdio.h>

#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_window.h"

/* The window: its size, place, fullscreen, and monitors.
 *
 *   arrows   move the window 50 pixels
 *   = / -    grow / shrink it by 10%
 *   F        fullscreen, where the window can be
 *   M        move it to the next monitor
 *   H        hide it for two seconds (it keeps running)
 *   Esc      quit, where quitting means anything
 *
 * Where programs don't place their windows -- the web, a Wayland desktop -- moving
 * and changing monitor say "not supported here".
 *
 * libwgt's app-window (wgrender's window) done 1:1 for the size table. The one
 * difference: the title and the help line say libwgf where libwgt's say libwgt. */

static struct {
    char status[96];
    float hidden_for; /* seconds left hidden */
} g;

static void report(const char *what, bool ok)
{
    snprintf(g.status, sizeof(g.status), "%s: %s", what, ok ? "done" : "not supported here");
}

static void frame(void *user)
{
    const int width = wgf_window_get_width(), height = wgf_window_get_height();
    const int x = wgf_window_get_x(), y = wgf_window_get_y();
    char line[160];
    float row = 12;
    int m;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_LEFT)) report("move", wgf_window_set_position(x - 50, y));
    if (wgf_keyboard_is_pressed(WGF_KEY_RIGHT)) report("move", wgf_window_set_position(x + 50, y));
    if (wgf_keyboard_is_pressed(WGF_KEY_UP)) report("move", wgf_window_set_position(x, y - 50));
    if (wgf_keyboard_is_pressed(WGF_KEY_DOWN)) report("move", wgf_window_set_position(x, y + 50));
    if (wgf_keyboard_is_pressed(WGF_KEY_EQUAL)) {
        report("grow", wgf_window_set_size((int)((float)width * 1.1f), (int)((float)height * 1.1f)));
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_MINUS)) {
        report("shrink", wgf_window_set_size((int)((float)width / 1.1f), (int)((float)height / 1.1f)));
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_F)) { /* a request: is_fullscreen answers on a later frame */
        const bool asked = wgf_window_request_fullscreen(!wgf_window_is_fullscreen());
        snprintf(g.status, sizeof(g.status), "fullscreen: %s", asked ? "asked for" : "not supported here");
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_H)) {
        wgf_window_set_visible(false);
        g.hidden_for = 2.0f;
        report("hide for 2 s", true);
    }
    if (g.hidden_for > 0.0f && (g.hidden_for -= wgf_loop_get_frame_delta()) <= 0.0f) {
        wgf_window_set_visible(true);
        report("show", true);
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_M)) {
        report("monitor", wgf_window_set_monitor((wgf_window_get_monitor() + 1) % wgf_window_get_monitor_count()));
    }

    wgf_draw_text(0, wgf_window_can_fullscreen()
                         ? "libwgf window   arrows: move   =/-: size   F: fullscreen   M: next monitor   H: hide"
                         : "libwgf window   arrows: move   =/-: size   (no fullscreen here)   M: next monitor"
                           "   H: hide",
                  12, row, 16, WGF_COLOR_RAYWHITE);
    row += 32;
    snprintf(line, sizeof(line), "window: %d x %d at (%d, %d)   fullscreen: %s   focused: %s", width, height, x, y,
             wgf_window_is_fullscreen() ? "yes" : "no", wgf_window_is_focused() ? "yes" : "no");
    wgf_draw_text(0, line, 12, row, 16, WGF_COLOR_LIGHTGRAY);
    row += 24;
    wgf_draw_text(0, g.status, 12, row, 16, WGF_COLOR_GOLD);
    row += 32;
    for (m = 0; m < wgf_window_get_monitor_count(); m++) {
        snprintf(line, sizeof(line), "%s monitor %d \"%s\": %d x %d at (%d, %d)",
                 m == wgf_window_get_monitor() ? ">" : " ", m, wgf_window_get_monitor_name(m),
                 wgf_window_get_monitor_width(m), wgf_window_get_monitor_height(m), wgf_window_get_monitor_x(m),
                 wgf_window_get_monitor_y(m));
        wgf_draw_text(0, line, 12, row, 16, WGF_COLOR_LIGHTGRAY);
        row += 22;
    }
}

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(24, 28, 38, 255));
    snprintf(g.status, sizeof(g.status), "press a key");
}

int main(void)
{
    wgf_window_set_title("libwgf window");
    wgf_window_set_size(900, 400);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
