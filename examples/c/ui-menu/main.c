#include <stdio.h>

#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_render.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* A game's menus with libwgf's UI: a title screen of a panel and three buttons, an
 * options screen with a row of buttons and a back button, styled as the game's own.
 * The pointer, the arrow keys, Tab, Enter, and a pad's D-pad and south button all work
 * it; a status bar along the bottom says what was last activated and who has the
 * pointer. Escape quits where quitting means anything. */

enum { TITLE, OPTIONS };

static int screen = TITLE, volume = 5, plays;
static char status[64] = "nothing yet";

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(8, 10, 16, 255));
    wgf_ui_set_style_color(WGF_UI_COLOR_PANEL, wgf_color_make(20, 24, 36, 235));
    wgf_ui_set_style_color(WGF_UI_COLOR_FOCUS, wgf_color_make(120, 220, 255, 255));
    wgf_ui_set_style_value(WGF_UI_VALUE_CORNER_RADIUS, 10);
}

static void title(void)
{
    wgf_ui_begin_panel("title");
    wgf_ui_set_align(WGF_UI_ALIGN_CENTER, WGF_UI_ALIGN_START);
    wgf_ui_label("libwgf", 56);
    wgf_ui_label("ui-menu", 18);
    wgf_ui_spacer(12);
    if (wgf_ui_button("play", "Play")) snprintf(status, sizeof(status), "played %d time(s)", ++plays);
    if (wgf_ui_button("options", "Options")) {
        screen = OPTIONS;
        wgf_ui_set_focus("back"); /* the new screen's first stop, for keys and pads */
    }
    if (wgf_app_can_quit() && wgf_ui_button("quit", "Quit")) wgf_app_quit();
    wgf_ui_end_panel();
}

static void options(void)
{
    char label[32];
    wgf_ui_begin_panel("options");
    wgf_ui_set_align(WGF_UI_ALIGN_CENTER, WGF_UI_ALIGN_START);
    wgf_ui_label("Options", 36);
    snprintf(label, sizeof(label), "volume %d", volume);
    wgf_ui_begin_box("volume", WGF_UI_DIRECTION_ROW);
    wgf_ui_set_gap(12);
    wgf_ui_set_align(WGF_UI_ALIGN_CENTER, WGF_UI_ALIGN_CENTER);
    wgf_ui_set_style_value(WGF_UI_VALUE_BUTTON_WIDTH, 60);
    if (wgf_ui_button("quieter", "-") && volume > 0) volume--;
    wgf_ui_label(label, 0);
    if (wgf_ui_button("louder", "+") && volume < 10) volume++;
    wgf_ui_set_style_value(WGF_UI_VALUE_BUTTON_WIDTH, 200);
    wgf_ui_end_box();
    if (wgf_ui_button("back", "Back")) {
        screen = TITLE;
        wgf_ui_set_focus("options");
    }
    wgf_ui_end_panel();
}

static void frame(void *user)
{
    char line[160];
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_ui_begin()) {
        if (screen == TITLE)
            title();
        else
            options();
        wgf_ui_end();
    }
    snprintf(line, sizeof(line), "last: %s   focus: %s   pointer: %s", status,
             wgf_ui_get_focus()[0] != '\0' ? wgf_ui_get_focus() : "none",
             wgf_input_is_pointer_captured() ? "the UI's" : "the game's");
    wgf_draw_text(0, line, 12, (float)wgf_render_get_height() / wgf_render_get_dpi_scale() - 28, 14,
                  WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("ui-menu");
    wgf_window_set_size(800, 450);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
