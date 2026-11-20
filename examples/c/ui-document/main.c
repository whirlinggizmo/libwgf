#include <math.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_document.h"
#include "wgf_draw.h"
#include "wgf_font.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* A game's options menu as a document: RML and RCSS files (examples/assets/ui/), laid out
 * and drawn by libwgf on RmlUi, over a scene drawn behind it. Its tabs, a text field, a
 * choice, a check box, a slider, a scrolling list of levels, and buttons, each drawn from
 * one sprite sheet as nine-patches, a sprite for every state; a title that glows by an
 * animation, buttons whose color eases by a transition, a tooltip shown by `:hover`, and
 * text in the family's bold and italic faces; and what RmlUi draws on layers and through
 * shaders: the window's box shadow, a gradient rule and a round
 * radial-gradient badge, the tooltip's blurred backdrop, the level list clipped to its
 * rounded corners, and locked levels grayed by a filter. The pointer, the wheel, a finger (a list
 * dragged scrolls), Tab and the arrow keys, Enter, and a pad's D-pad and south button all
 * work it, and a text field asks a phone for its keyboard; the program
 * reads what happened as events each frame and answers by changing the document's text.
 * Escape quits where quitting means anything. */

static wgf_document_t menu;
static wgf_element_t status, volume, volume_value, name, sound, difficulty, play, quit;
static bool found;
static float t;

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets");
    wgf_render_set_clear_color(wgf_color_make(10, 12, 20, 255));
    /* the families the style sheet names, set before the document is made */
    wgf_ui_set_document_font("Mono", wgf_font_create("fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"));
    /* the family's bold and italic, faces of their own: `<b>`, `<i>`, `font-weight: bold` */
    wgf_ui_set_document_font_face("Mono", 700, false, wgf_font_create("fonts/JetBrainsMono/JetBrainsMono-Bold.ttf"));
    wgf_ui_set_document_font_face("Mono", 400, true, wgf_font_create("fonts/JetBrainsMono/JetBrainsMono-Italic.ttf"));
    wgf_ui_set_document_font("Komika", wgf_font_create("fonts/Komika/KOMIKAH_.ttf"));
    menu = wgf_document_create("ui/options.rml");
    wgf_document_set_visible(menu, true);
}

static void say(const char *text)
{
    wgf_element_set_text(status, text);
}

/* The elements, once the document is READY. */
static void find(void)
{
    status = wgf_document_find(menu, "status");
    volume = wgf_document_find(menu, "volume");
    volume_value = wgf_document_find(menu, "volume-value");
    name = wgf_document_find(menu, "name");
    sound = wgf_document_find(menu, "sound");
    difficulty = wgf_document_find(menu, "difficulty");
    play = wgf_document_find(menu, "play");
    quit = wgf_document_find(menu, "quit");
    if (!wgf_app_can_quit()) wgf_element_set_property(quit, "display", "none"); /* nothing to quit to on a page */
    wgf_document_set_focus(menu, play);
    found = true;
}

static void answer(int event, wgf_element_t element)
{
    char line[128];
    if (event == WGF_DOCUMENT_EVENT_CLICK && element == play) {
        snprintf(line, sizeof(line), "%s plays on %s, sound %s.", wgf_element_get_value(name),
                 wgf_element_get_value(difficulty), wgf_element_is_checked(sound) ? "on" : "off");
        say(line);
    } else if (event == WGF_DOCUMENT_EVENT_CLICK && element == quit) {
        wgf_app_quit();
    } else if (event == WGF_DOCUMENT_EVENT_CLICK && wgf_element_has_class(element, "level")) {
        snprintf(line, sizeof(line), "Chosen: %s", wgf_element_get_text(element));
        say(line);
    } else if (event == WGF_DOCUMENT_EVENT_CHANGE && element == volume) {
        snprintf(line, sizeof(line), "%d", (int)wgf_element_get_number(volume));
        wgf_element_set_text(volume_value, line);
    } else if (event == WGF_DOCUMENT_EVENT_CHANGE && element == sound) {
        say(wgf_element_is_checked(sound) ? "Sound on." : "Sound off.");
    } else if (event == WGF_DOCUMENT_EVENT_CHANGE && element == difficulty) {
        snprintf(line, sizeof(line), "Difficulty: %s", wgf_element_get_value(difficulty));
        say(line);
    }
}

/* Something to draw the menu over: slow rings about the middle. */
static void scene(void)
{
    const float w = (float)wgf_render_get_width(), h = (float)wgf_render_get_height();
    int i;
    for (i = 0; i < 24; i++) {
        const float a = t * 0.2f + (float)i * 0.2618f, r = 120.0f + 60.0f * sinf(t * 0.5f + (float)i);
        wgf_draw_circle(w * 0.5f + cosf(a) * r * 2.2f, h * 0.5f + sinf(a) * r * 1.4f, 18.0f + (float)(i % 4) * 6.0f,
                        wgf_color_make(40 + i * 4, 60 + i * 3, 120, 140));
    }
}

static void frame(void *user)
{
    int events[64], n, i;
    (void)user;
    t += wgf_loop_get_frame_delta();
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    scene();
    if (!found && wgf_resource_get_status(menu) == WGF_RESOURCE_STATUS_READY) find();
    wgf_ui_draw_documents();
    n = wgf_document_take_events(menu, events, 64);
    for (i = 0; i + 1 < n; i += 2) answer(events[i], (wgf_element_t)events[i + 1]);
}

int main(void)
{
    wgf_window_set_title("libwgf: ui-document");
    wgf_window_set_size(960, 640);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
