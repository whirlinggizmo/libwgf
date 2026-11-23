#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_font.h"
#include "wgf_image.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_presentation.h"
#include "wgf_render.h"
#include "wgf_texture.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* libwgf's UI widgets, skinned: helix-raylib's gui_skins example ported (a settings panel
 * of a star button, a volume slider, a music checkbox, a loading bar, and Load, Save, and
 * Reset buttons), each part drawn from Kenney's UI Pack (CC0, examples/assets/ui/kenney/)
 * as a nine-slice or a bar with caps, in the pack's font. The buttons' four states come
 * from one sheet, their faces on a lip their text sinks onto when pressed; the loading
 * bar's blue rail is clipped, shown up to its value over the grey one, its round far end
 * only when full; the star answers on its round face alone, its clear corners left to the
 * screen, and sets the volume full. Save is disabled. Its tabs show four more pages: a
 * choice, a combo, and numbers; a grid of level tiles in a scroll area, which the arrows
 * move through in two dimensions, scrolled to the one focused; a list of tracks, one played
 * when activated; and a name
 * and a password typed in (a press places the caret, a drag or Shift selects, a double press
 * a word; Ctrl's keys copy, paste, and undo), the name shown refused while it is taken. The
 * wheel, a drag, or a scroll bar scrolls them. The popups: a quality dropdown, whose list
 * opens over the rows below it; tooltips on the main page's buttons, after a rest on one (or
 * the focus on it, by keys or a pad); Reset asks first, in a modal; and the whole UI fades
 * in as it starts. The theme row shows the same panel
 * in libwgf's own style and in a recolored one. The pointer, the arrow keys, Tab, Enter,
 * and a pad work it; Escape quits where quitting means anything. Laid out for a design of
 * 580 by 640, scaled to fit the window or a phone's screen (EXPAND: the blue past it); on a
 * page the canvas fills the page, as the window size is left unset. */

enum { PLAIN, RECOLORED, SKINNED };

static int theme = -1, chosen = SKINNED;
static float volume = 0.6f, loaded = 0.4f, speed = 1.5f;
static bool music = true, loading, asking;
static int page, difficulty = 1, color, lives = 3, level = -1, track, quality = 1;
static float shown; /* seconds since it started: the fade in */
static char greeting[64];
static int playing = -1;
static char tracks[1024];
static wgf_texture_t tex[13];
static wgf_image_t star_mask;
static wgf_font_t font;

enum { PANEL, BUTTON, RAIL, RAIL_SLIDER, RAIL_FILL, HANDLE, CHECK_BOX, CHECK, ICON_LOAD, ICON_SAVE, ICON_RESET, STAR,
       STAR_PRESSED };
static const char *const names[13] = {"panel",    "button",    "rail",       "rail_slider", "rail_fill",
                                      "handle",   "check_box", "check",      "icon_load",   "icon_save",
                                      "icon_reset", "star_button", "star_button_pressed"};

/* A skin of a whole texture, `border` texels all round (or, `caps`, at its ends alone: a bar). */
static void skin(wgf_ui_widget_t widget, wgf_ui_part_t part, int which, float border, bool caps)
{
    wgf_ui_set_skin(widget, part, WGF_UI_STATE_NORMAL, tex[which], 0, 0, 0, 0);
    wgf_ui_set_skin_borders(widget, part, WGF_UI_STATE_NORMAL, border, caps ? 0 : border, border, caps ? 0 : border);
}

/* A skin of the check box's white square, tinted: a scroll bar's or a list's. */
static void tinted(wgf_ui_widget_t widget, wgf_ui_part_t part, wgf_ui_state_t state, wgf_color_t tint)
{
    wgf_ui_set_skin(widget, part, state, tex[CHECK_BOX], 0, 0, 0, 0);
    wgf_ui_set_skin_borders(widget, part, state, 8, 8, 8, 8);
    wgf_ui_set_skin_tint(widget, part, state, tint);
}

/* A face from the button sheet: four cells of 192 by 64 down it (normal, hovered,
 * pressed, disabled), each on a lip 4 texels deep that its text sits above, pressed onto. */
static void faces(wgf_ui_widget_t widget)
{
    static const wgf_ui_state_t states[4] = {WGF_UI_STATE_NORMAL, WGF_UI_STATE_HOVERED, WGF_UI_STATE_PRESSED,
                                             WGF_UI_STATE_DISABLED};
    int i;
    for (i = 0; i < 4; i++) {
        wgf_ui_set_skin(widget, WGF_UI_PART_BASE, states[i], tex[BUTTON], 0, 64.0f * (float)i, 192, 64);
        wgf_ui_set_skin_borders(widget, WGF_UI_PART_BASE, states[i], 16, 16, 16, 20);
        wgf_ui_set_text_offset(widget, states[i], 0, states[i] == WGF_UI_STATE_PRESSED ? 2.0f : -2.0f);
    }
}

static void set_theme(int which)
{
    if (which == theme) return;
    theme = which;
    wgf_ui_reset_style();
    wgf_ui_set_style_value(WGF_UI_VALUE_BAR_WIDTH, 200);
    if (theme == RECOLORED) {
        wgf_ui_set_style_color(WGF_UI_COLOR_PANEL, wgf_color_make(48, 28, 40, 240));
        wgf_ui_set_style_color(WGF_UI_COLOR_BUTTON, wgf_color_make(120, 52, 70, 255));
        wgf_ui_set_style_color(WGF_UI_COLOR_BUTTON_HOVERED, wgf_color_make(150, 70, 90, 255));
        wgf_ui_set_style_color(WGF_UI_COLOR_BUTTON_PRESSED, wgf_color_make(90, 36, 52, 255));
        wgf_ui_set_style_color(WGF_UI_COLOR_BAR, wgf_color_make(80, 44, 58, 255));
        wgf_ui_set_style_color(WGF_UI_COLOR_BAR_FILL, wgf_color_make(255, 170, 60, 255));
        wgf_ui_set_style_color(WGF_UI_COLOR_HANDLE, wgf_color_make(255, 220, 160, 255));
        wgf_ui_set_style_color(WGF_UI_COLOR_FOCUS, wgf_color_make(255, 220, 160, 255));
        wgf_ui_set_style_value(WGF_UI_VALUE_CORNER_RADIUS, 14);
    } else if (theme == SKINNED) {
        wgf_ui_set_style_font(font);
        wgf_ui_set_style_color(WGF_UI_COLOR_TEXT, wgf_color_make(70, 70, 80, 255));
        wgf_ui_set_style_color(WGF_UI_COLOR_SEPARATOR, wgf_color_make(190, 194, 205, 255));
        wgf_ui_set_style_value(WGF_UI_VALUE_BUTTON_PADDING, 32);
        wgf_ui_set_style_value(WGF_UI_VALUE_BUTTON_WIDTH, 160);
        wgf_ui_set_style_value(WGF_UI_VALUE_BAR_HEIGHT, 16);
        wgf_ui_set_style_value(WGF_UI_VALUE_SLIDER_HEIGHT, 32);
        wgf_ui_set_style_value(WGF_UI_VALUE_HANDLE_SIZE, 24);
        wgf_ui_set_style_value(WGF_UI_VALUE_CHECK_SIZE, 32);
        skin(WGF_UI_WIDGET_PANEL, WGF_UI_PART_BASE, PANEL, 16, false);
        skin(WGF_UI_WIDGET_GROUP, WGF_UI_PART_BASE, PANEL, 16, false);
        faces(WGF_UI_WIDGET_BUTTON);
        faces(WGF_UI_WIDGET_TOGGLE);
        faces(WGF_UI_WIDGET_TAB);
        faces(WGF_UI_WIDGET_COMBO);
        faces(WGF_UI_WIDGET_TILE);
        skin(WGF_UI_WIDGET_NUMBER, WGF_UI_PART_BASE, CHECK_BOX, 8, false);
        wgf_ui_set_style_value(WGF_UI_VALUE_TILE_SIZE, 120);
        skin(WGF_UI_WIDGET_SLIDER, WGF_UI_PART_BASE, RAIL_SLIDER, 16, true);
        skin(WGF_UI_WIDGET_SLIDER, WGF_UI_PART_HANDLE, HANDLE, 0, false);
        skin(WGF_UI_WIDGET_PROGRESS, WGF_UI_PART_BASE, RAIL, 16, true);
        skin(WGF_UI_WIDGET_PROGRESS, WGF_UI_PART_FILL, RAIL_FILL, 16, true);
        wgf_ui_set_fill(WGF_UI_WIDGET_PROGRESS, WGF_UI_FILL_CLIPPED, WGF_UI_FROM_LEFT);
        skin(WGF_UI_WIDGET_CHECKBOX, WGF_UI_PART_BASE, CHECK_BOX, 8, false);
        skin(WGF_UI_WIDGET_CHECKBOX, WGF_UI_PART_FILL, CHECK, 0, false);
        wgf_ui_set_style_value(WGF_UI_VALUE_SCROLL_BAR, 16);
        tinted(WGF_UI_WIDGET_SCROLL, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, wgf_color_make(214, 218, 226, 255));
        tinted(WGF_UI_WIDGET_SCROLL, WGF_UI_PART_HANDLE, WGF_UI_STATE_NORMAL, wgf_color_make(150, 158, 176, 255));
        tinted(WGF_UI_WIDGET_SCROLL, WGF_UI_PART_HANDLE, WGF_UI_STATE_HOVERED, wgf_color_make(110, 120, 142, 255));
        tinted(WGF_UI_WIDGET_SCROLL, WGF_UI_PART_HANDLE, WGF_UI_STATE_PRESSED, wgf_color_make(52, 152, 219, 255));
        tinted(WGF_UI_WIDGET_LIST, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, WGF_COLOR_WHITE);
        tinted(WGF_UI_WIDGET_LIST, WGF_UI_PART_ITEM, WGF_UI_STATE_HOVERED, wgf_color_make(226, 232, 242, 255));
        tinted(WGF_UI_WIDGET_LIST, WGF_UI_PART_ITEM, WGF_UI_STATE_PRESSED, wgf_color_make(160, 208, 240, 255));
        faces(WGF_UI_WIDGET_DROPDOWN);
        tinted(WGF_UI_WIDGET_DROPDOWN, WGF_UI_PART_ITEM, WGF_UI_STATE_HOVERED, wgf_color_make(226, 232, 242, 255));
        tinted(WGF_UI_WIDGET_DROPDOWN, WGF_UI_PART_ITEM, WGF_UI_STATE_PRESSED, wgf_color_make(160, 208, 240, 255));
        skin(WGF_UI_WIDGET_POPUP, WGF_UI_PART_BASE, PANEL, 16, false);
        tinted(WGF_UI_WIDGET_TEXT, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, WGF_COLOR_WHITE);
        tinted(WGF_UI_WIDGET_TEXT, WGF_UI_PART_BASE, WGF_UI_STATE_PRESSED, wgf_color_make(232, 244, 252, 255));
    }
}

static void init(void *user)
{
    static const char *const names_of[10] = {"Sunrise", "Harbor", "Neon", "Dunes", "Glacier",
                                             "Canyon",  "Forest", "Metro", "Comet", "Lagoon"};
    size_t used = 0;
    int i;
    (void)user;
    for (i = 0; i < 30; i++) /* the list's items, one a line */
        used += (size_t)snprintf(tracks + used, sizeof(tracks) - used, "%d. %s %s\n", i + 1, names_of[i % 10],
                                 i < 10 ? "Run" : i < 20 ? "Night" : "Storm");
    wgf_asset_set_host("../assets");
    wgf_render_set_clear_color(wgf_color_make(52, 152, 219, 255));
    for (i = 0; i < 13; i++) {
        char path[64];
        snprintf(path, sizeof(path), "ui/kenney/%s.png", names[i]);
        tex[i] = wgf_texture_create(path);
    }
    /* the button sheet's cells drawn smaller than they are: from the full image, so no
       mipmap blends a cell's edge with the next's */
    wgf_texture_set_sampling(tex[BUTTON], WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP,
                             WGF_TEXTURE_FILTER_LINEAR_NO_MIPMAPS);
    star_mask = wgf_image_create("ui/kenney/star_button.png");
    font = wgf_font_create("ui/kenney/font.ttf");
    set_theme(SKINNED);
}

/* A row: `label` in a column `width` wide, then what follows. */
static void row(const char *id, const char *label, float width)
{
    wgf_ui_begin_box(id, WGF_UI_DIRECTION_ROW);
    wgf_ui_set_width(WGF_UI_SIZING_GROW, 0);
    wgf_ui_set_gap(16);
    wgf_ui_set_align(WGF_UI_ALIGN_START, WGF_UI_ALIGN_CENTER);
    wgf_ui_begin_box(NULL, WGF_UI_DIRECTION_COLUMN);
    wgf_ui_set_width(WGF_UI_SIZING_FIXED, width);
    wgf_ui_label(label, 0);
    wgf_ui_end_box();
}

static void settings(void)
{
    char percent[16];
    row("heading", "Settings", 100);
    wgf_ui_spacer(0);
    if (wgf_ui_image_button("star", tex[STAR], tex[STAR_PRESSED], star_mask)) volume = 1.0f; /* full */
    wgf_ui_end_box();

    row("volume-row", "Volume", 100);
    volume = wgf_ui_slider("volume", volume, 0.0f, 1.0f);
    wgf_ui_end_box();

    music = wgf_ui_checkbox("music", "Music", music);

    if (loading)
        loaded = loaded + wgf_loop_get_frame_delta() / 4.0f < 1.0f ? loaded + wgf_loop_get_frame_delta() / 4.0f : 1.0f;
    snprintf(percent, sizeof(percent), "%d%%", (int)(loaded * 100.0f + 0.5f));
    row("loading-row", percent, 100);
    wgf_ui_progress(loaded);
    wgf_ui_end_box();

    wgf_ui_begin_box("buttons", WGF_UI_DIRECTION_ROW);
    wgf_ui_set_gap(20);
    if (wgf_ui_icon_button("load", tex[ICON_LOAD], "Load")) {
        loaded = 0.0f;
        loading = true;
    }
    wgf_ui_tooltip("Fills the bar in four seconds");
    wgf_ui_set_disabled(true);
    wgf_ui_icon_button("save", tex[ICON_SAVE], "Save");
    wgf_ui_tooltip("Nothing to save yet");
    wgf_ui_set_disabled(false);
    wgf_ui_end_box();
    wgf_ui_set_style_value(WGF_UI_VALUE_BUTTON_WIDTH, theme == SKINNED ? 340 : 200);
    if (wgf_ui_icon_button("reset", tex[ICON_RESET], "Reset")) asking = true;
    wgf_ui_tooltip("Empties the bar, once asked");
    wgf_ui_set_style_value(WGF_UI_VALUE_BUTTON_WIDTH, theme == SKINNED ? 160 : 200);
}

/* Reset's question, over the rest. */
static void ask(void)
{
    wgf_ui_begin_modal("ask");
    wgf_ui_set_align(WGF_UI_ALIGN_CENTER, WGF_UI_ALIGN_START);
    wgf_ui_label("Empty the loading bar?", 0);
    wgf_ui_begin_box("answers", WGF_UI_DIRECTION_ROW);
    wgf_ui_set_gap(20);
    if (wgf_ui_button("no", "No")) asking = false;
    if (wgf_ui_button("yes", "Yes")) {
        loaded = 0.0f;
        loading = asking = false;
    }
    wgf_ui_end_box();
    wgf_ui_end_modal();
}

/* The choices page: a segmented choice, a combo, a dropdown, and two numbers. */
static void choices(void)
{
    row("difficulty-row", "Level", 100);
    difficulty = wgf_ui_choice("difficulty", "Easy\nMid\nHard", difficulty);
    wgf_ui_end_box();
    row("color-row", "Color", 100);
    color = wgf_ui_combo("color", "Red\nGreen\nBlue\nGold", color);
    wgf_ui_end_box();
    row("quality-row", "Quality", 100);
    quality = wgf_ui_dropdown("quality", "Low\nMedium\nHigh\nUltra", quality);
    wgf_ui_end_box();
    row("lives-row", "Lives", 100);
    lives = (int)wgf_ui_number("lives", (float)lives, 1.0f, 9.0f, 1.0f);
    wgf_ui_end_box();
    row("speed-row", "Speed", 100);
    speed = wgf_ui_number("speed", speed, 0.5f, 3.0f, 0.25f);
    wgf_ui_end_box();
}

/* The levels page: a grid of tiles in a scroll area, the one chosen named below it. */
static void levels(void)
{
    char text[32];
    int i;
    wgf_ui_begin_scroll("levels-area", 0, 300);
    wgf_ui_set_gap(8);
    wgf_ui_begin_grid("levels", 3);
    for (i = 0; i < 12; i++) {
        char id[24], name[16];
        snprintf(id, sizeof(id), "level%d", i + 1);
        snprintf(name, sizeof(name), "%d", i + 1);
        if (wgf_ui_tile(id, tex[STAR], name)) level = i + 1;
    }
    wgf_ui_end_grid();
    wgf_ui_end_scroll();
    if (level > 0) snprintf(text, sizeof(text), "Level %d chosen", level);
    else snprintf(text, sizeof(text), "Choose a level");
    wgf_ui_label(text, 0);
}

/* The list page: thirty tracks, the one chosen named below them, playing once it is activated
 * (Enter, or a press on it again). */
static void list(void)
{
    char text[48];
    const char *at = tracks;
    int i;
    track = wgf_ui_list("tracks", tracks, track, 7);
    if (wgf_ui_is_activated()) playing = track;
    for (i = 0; i < track && at != NULL; i++) {
        at = strchr(at, '\n');
        if (at != NULL) at++;
    }
    if (at != NULL && track >= 0) {
        const char *end = strchr(at, '\n');
        snprintf(text, sizeof(text), "%s %.*s", playing == track ? "Playing" : "Chosen",
                 end != NULL ? (int)(end - at) : (int)strlen(at), at);
    } else {
        snprintf(text, sizeof(text), "Nothing playing");
    }
    wgf_ui_label(text, 0);
}

/* The name page: a name, refused while it is taken, and a password; Enter in the name greets.
 * Their texts are text buffers, made once: the fields write what is typed into them. */
static void name(void)
{
    static wgf_text_buffer_t name_text, password_text;
    bool taken;
    if (name_text == 0) {
        name_text = wgf_text_buffer_create_text(NULL);
        password_text = wgf_text_buffer_create_text(NULL);
    }
    taken = strcmp(wgf_text_buffer_get_text(name_text), "admin") == 0;
    row("name-row", "Name", 130);
    wgf_ui_set_invalid(taken);
    if (wgf_ui_text_field("name", name_text, "Your name", 16) && !taken && wgf_text_buffer_get_length(name_text) > 0)
        snprintf(greeting, sizeof(greeting), "Hello, %s", wgf_text_buffer_get_text(name_text));
    wgf_ui_set_invalid(false);
    wgf_ui_end_box();
    row("password-row", "Password", 130);
    wgf_ui_password_field("password", password_text, "At least 8", 32);
    wgf_ui_end_box();
    wgf_ui_label(taken ? "That name is taken" : greeting[0] != '\0' ? greeting : "Enter your name", 0);
}

static void panel(void)
{
    wgf_ui_begin_panel("panel");
    wgf_ui_set_width(WGF_UI_SIZING_FIXED, theme == SKINNED ? 560 : 460); /* the skin's tabs are wider */
    wgf_ui_set_padding(30, 20);
    wgf_ui_set_gap(14);
    page = wgf_ui_tabs("pages", "Main\nMore\nLevels\nList\nName", page);
    if (page == 0) settings();
    else if (page == 1) choices();
    else if (page == 2) levels();
    else if (page == 3) list();
    else name();
    wgf_ui_end_panel();
}

/* The theme row: three toggles, one on. */
static void themes(void)
{
    static const char *const labels[3] = {"Plain", "Recolored", "Skinned"};
    static const char *const ids[3] = {"plain", "recolored", "skinned"};
    int i;
    wgf_ui_begin_box("themes", WGF_UI_DIRECTION_ROW);
    wgf_ui_set_gap(8);
    for (i = 0; i < 3; i++) {
        if (wgf_ui_toggle(ids[i], labels[i], theme == i) != (theme == i)) chosen = i;
    }
    wgf_ui_end_box();
}

static void frame(void *user)
{
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    shown += wgf_loop_get_frame_delta();
    wgf_ui_set_alpha(shown < 0.5f ? shown / 0.5f : 1.0f);
    if (wgf_ui_begin()) {
        panel();
        themes();
        if (asking) ask();
        wgf_ui_end();
    }
    set_theme(chosen); /* after the frame's UI is drawn: the style changes for the next */
}

int main(void)
{
    wgf_window_set_title("ui-widgets");
    wgf_presentation_set(WGF_PRESENTATION_MODE_EXPAND, 580, 640); /* natively, the window opens at it */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
