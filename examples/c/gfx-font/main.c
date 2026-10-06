#include <stdbool.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_font.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_window.h"

/* TrueType fonts, loaded as they are created: two fonts from files, a measured and
 * centered title, and several sizes, with the built-in font drawing until they are
 * ready.
 *
 *   D     switch the default font (font 0) between the built-in one and Komika
 *   Esc   quit, where quitting means anything
 *
 * libwgt's gfx-font (wgrender's font) done 1:1, so the two compare in the size table:
 * the same window, fonts, text, and keys. Where it differs, and why:
 *   - "libwgt" reads "libwgf" in the window's title and the drawn title: the library's
 *     name, the same length.
 *   - The frame rate is drawn by the example (draw_fps, as libwgt's wgt_loop_draw_fps
 *     draws it: "%d FPS", 16 pixels, green): libwgf has wgf_loop_get_fps but no call
 *     that draws it. */

#define MONO_PATH "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"
#define KOMIKA_PATH "fonts/Komika/KOMIKAH_.ttf"

static wgf_font_t mono, komika;

static bool ready(wgf_font_t font)
{
    return wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_READY;
}

/* The frame rate at (x, y), as libwgt's wgt_loop_draw_fps draws it. */
static void draw_fps(float x, float y)
{
    char text[32];
    snprintf(text, sizeof(text), "%d FPS", (int)(wgf_loop_get_fps() + 0.5f));
    wgf_draw_text(0, text, x, y, 16.0f, wgf_color_make(0, 255, 0, 255));
}

static void frame(void *user)
{
    const float width = (float)wgf_window_get_width();
    const char *title = "libwgf + fontstash";
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_D) && ready(komika)) {
        wgf_font_set_default(wgf_font_get_default() == 0 ? komika : 0);
    }

    /* A centered title, measured. */
    if (ready(komika)) {
        const wgf_vec2_t size = wgf_font_measure(komika, title, 56);
        wgf_draw_text(komika, title, (width - size.x) * 0.5f, 90, 56, WGF_COLOR_DARKBLUE);
    }
    /* A few sizes. */
    if (ready(mono)) {
        wgf_draw_text(mono, "The quick brown fox jumps over the lazy dog.", 40, 200, 28, WGF_COLOR_BLACK);
        wgf_draw_text(mono, "scalable, anti-aliased TrueType glyphs", 40, 250, 20, WGF_COLOR_DARKGRAY);
        wgf_draw_text(mono, "0123456789  !@#$%^&*()  +-*/=", 40, 290, 24, WGF_COLOR_MAROON);
    } else {
        wgf_draw_text(0,
                      wgf_resource_get_status(mono) == WGF_RESOURCE_STATUS_FAILED ? "a font failed to load"
                                                                                  : "loading fonts...",
                      40, 200, 20, WGF_COLOR_GRAY);
    }
    wgf_draw_text(0,
                  wgf_font_get_default() == 0 ? "[D] default font: built in   {a|b} ~ \\ ^_`"
                                              : "[D] default font: Komika   {a|b} ~ \\ ^_`",
                  40, 360, 16, WGF_COLOR_DARKGREEN);
    draw_fps(12, 12);
}

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(248, 248, 250, 255));
    mono = wgf_font_create(MONO_PATH);
    komika = wgf_font_create(KOMIKA_PATH);
}

static void on_shutdown(void *user)
{
    (void)user;
    wgf_font_set_default(0);
    wgf_resource_release(mono);
    wgf_resource_release(komika);
}

int main(void)
{
    wgf_window_set_title("libwgf font");
    wgf_window_set_size(900, 500);
    wgf_window_set_msaa(true);
    wgf_app_run(init, NULL, frame, on_shutdown, NULL);
    return 0;
}
