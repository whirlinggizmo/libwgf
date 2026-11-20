#include <math.h>
#include <stdio.h>
#include <string.h>

#include "jetbrains_mono_ascii.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_app.h"
#include "wgf_core_load_priv.h"
#include "wgf_document.h"
#include "wgf_font.h"
#include "wgf_fs.h"
#include "wgf_gamepad.h"
#include "wgf_handle.h"
#include "wgf_input.h"
#include "wgf_log.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_time.h"
#include "wgf_ui_priv.h"
#include "wgf_window.h"

/* Documents headless, frame by frame through the runtime, from files the test writes: the
 * files a document names, found for the asset part; a document loaded once its font is,
 * with its style sheet and image; its elements found, read, and changed (text, RML,
 * attributes, classes, properties, a form control's value, a checkbox, a slider, disabled);
 * the pointer clicking a button and a checkbox, typed text and keys in a text field, Tab
 * and a pad's D-pad moving the focus, each seen as the events taken; the captures; an
 * element gone with its RML; a document released, its elements with it; one that isn't
 * there FAILED; and every refusal. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        fflush(stdout);
        failures++;
    }
}

static const char rml[] =
    "<rml>\n"
    "<head>\n"
    "  <title>Options</title>\n"
    "  <link type=\"text/rcss\" href=\"test.rcss\"/>\n"
    "</head>\n"
    "<body>\n"
    "  <h1 id=\"title\">Options</h1>\n"
    "  <button id=\"play\"><span>Play</span></button>\n"
    "  <input id=\"sound\" type=\"checkbox\" value=\"on\"/>\n"
    "  <input id=\"volume\" type=\"range\" min=\"0\" max=\"10\" step=\"1\" value=\"5\"/>\n"
    "  <input id=\"name\" type=\"text\" value=\"Rob\"/>\n"
    "  <button id=\"quit\">Quit</button>\n"
    "  <div id=\"list\"><p id=\"first\">one</p></div>\n"
    "  <img id=\"logo\" src=\"dot.png\"/>\n"
    "  <a href=\"https://example.com/elsewhere.png\">away</a>\n"
    "</body>\n"
    "</rml>\n";

static const char rcss[] =
    "body { display: block; width: 100%; height: 100%; font-family: Mono; font-size: 16px; color: #ffffff; }\n"
    "h1, div, p, button { display: block; }\n"
    "button { width: 200px; height: 32px; tab-index: auto; nav: auto; background-color: #334; }\n"
    "input.checkbox { display: block; width: 20px; height: 20px; nav: auto; }\n"
    "input.range { display: block; width: 200px; height: 20px; nav: auto; }\n"
    "input.range slidertrack { height: 20px; }\n"
    "input.range sliderbar { width: 10px; height: 20px; }\n"
    "input.text { display: block; width: 200px; height: 24px; nav: auto; }\n"
    "img { display: block; }\n"
    ".hot { color: #ff0000; }\n";

/* a 2 by 2 PNG, white, opaque */
static const unsigned char png[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xFD, 0xD4, 0x9A, 0x73, 0x00, 0x00, 0x00,
    0x0E, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0x0F, 0x06, 0x0C, 0x10, 0x0A, 0x00, 0x53, 0xBA, 0x0B,
    0xF5, 0x98, 0xAC, 0x20, 0x67, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

static wgf_handle_t tasks[4];
static wgf_font_t font;
static wgf_document_t document, missing;
static wgf_element_t play, sound, volume, name, quit, first, logo;
static int frame_number, ready_frame = -1;
static double started;
static wgf_platform_priv_gamepad_t pad;

static void push(sapp_event event)
{
    wgf_platform_priv_headless_push_event(&event);
}

static void mouse(sapp_event_type type, float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.mouse_button = SAPP_MOUSEBUTTON_LEFT;
    event.mouse_x = x;
    event.mouse_y = y;
    push(event);
}

static void key(sapp_event_type type, sapp_keycode code)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key_code = code;
    push(event);
}

static void typed(uint32_t c)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = SAPP_EVENTTYPE_CHAR;
    event.char_code = c;
    push(event);
}

static void click_at(wgf_element_t element, sapp_event_type type)
{
    float x = 0, y = 0, w = 0, h = 0;
    expect(wgf_ui_priv_document_element_bounds(element, &x, &y, &w, &h) && w > 0 && h > 0, "laid out, with room");
    mouse(type, x + w * 0.5f, y + h * 0.5f);
}

/* The events waiting, as kinds; whether `kind` for `element` is among them. */
static int events[64], event_ints;

static void take(void)
{
    event_ints = wgf_document_take_events(document, events, 64);
}

static bool happened(wgf_document_event_t kind, wgf_element_t element)
{
    int i;
    for (i = 0; i + 1 < event_ints; i += 2) {
        if (events[i] == (int)kind && (wgf_element_t)events[i + 1] == element) return true;
    }
    return false;
}

/* What a document names, as its lister finds it. */
static char named[8][64];
static int named_count;

static void add_named(const char *uri, const char *fallback, bool required, void *context)
{
    (void)fallback;
    (void)context;
    expect(!required, "nothing a document names is required");
    if (named_count < 8) snprintf(named[named_count++], sizeof(named[0]), "%s", uri);
}

static bool was_named(const char *uri)
{
    int i;
    for (i = 0; i < named_count; i++) {
        if (strcmp(named[i], uri) == 0) return true;
    }
    return false;
}

static void on_init(void *user)
{
    (void)user;
    tasks[0] = wgf_fs_write("doc_test/menu.rml", (const unsigned char *)rml, (int)strlen(rml));
    tasks[1] = wgf_fs_write("doc_test/test.rcss", (const unsigned char *)rcss, (int)strlen(rcss));
    tasks[2] = wgf_fs_write("doc_test/dot.png", png, (int)sizeof(png));
    tasks[3] = wgf_fs_write("doc_test/mono.ttf", jetbrains_mono_ascii_ttf, (int)sizeof(jetbrains_mono_ascii_ttf));
    memset(&pad, 0, sizeof(pad));
    pad.connected = true;
    snprintf(pad.name, sizeof(pad.name), "%s", "test pad");
    wgf_platform_priv_gamepad_set_test_pad(0, &pad);
    started = wgf_time_get_seconds();
}

static void press_pad(wgf_gamepad_button_t button, bool down)
{
    pad.buttons[button] = down;
    wgf_platform_priv_gamepad_set_test_pad(0, &pad);
}

static void before_ready(void)
{
    int i;
    if (font != 0) return;
    for (i = 0; i < 4; i++) {
        if (tasks[i] == 0 || wgf_fs_task_get_status(tasks[i]) == WGF_FS_TASK_STATUS_PENDING) return;
    }
    if (font == 0) {
        for (i = 0; i < 4; i++) {
            expect(wgf_fs_task_get_status(tasks[i]) == WGF_FS_TASK_STATUS_DONE, "a file written");
            wgf_fs_task_destroy(tasks[i]);
        }
        /* the lister: what the asset part makes local with a document */
        {
            const wgf_core_priv_load_lister_fn list = wgf_core_priv_load_lister("doc_test/menu.rml");
            expect(list != NULL && wgf_core_priv_load_lister("a/b.RCSS") == list, "documents and style sheets listed");
            if (list != NULL) list((const unsigned char *)rml, (int)strlen(rml), add_named, NULL);
            expect(named_count == 2 && was_named("test.rcss") && was_named("dot.png"),
                   "a document names its style sheet and its image, and no URL");
        }
        font = wgf_font_create("doc_test/mono.ttf");
        expect(wgf_ui_set_document_font("  'Mono' ", font) && wgf_ui_get_document_font("mono") == font,
               "a family's font, matched without case or quotes");
        expect(!wgf_ui_set_document_font("", font) && !wgf_ui_set_document_font(NULL, font) &&
                   !wgf_ui_set_document_font("x", 12345) && wgf_ui_get_document_font("serif") == 0,
               "no family, or no font: refused; a family unset is none");
        expect(!wgf_ui_draw_documents(), "nothing to draw before a document");
        document = wgf_document_create("doc_test/menu.rml");
        missing = wgf_document_create("doc_test/missing.rml");
        expect(document != 0 && wgf_resource_get_status(document) == WGF_RESOURCE_STATUS_PENDING, "pending at once");
        expect(strcmp(wgf_handle_get_kind_name(document), "ui.document") == 0, "a document's kind");
        expect(wgf_document_create("doc_test/menu.rml") == document && wgf_resource_release(document),
               "one document a path, counted");
        expect(wgf_document_set_visible(document, true) && wgf_document_is_visible(document),
               "asked to show while loading");
        expect(wgf_document_find(document, "play") == 0, "nothing found while loading");
    }
}

static void ready(void)
{
    wgf_element_t title, list;
    play = wgf_document_find(document, "play");
    sound = wgf_document_find(document, "sound");
    volume = wgf_document_find(document, "volume");
    name = wgf_document_find(document, "name");
    quit = wgf_document_find(document, "quit");
    first = wgf_document_find(document, "first");
    title = wgf_document_find(document, "title");
    list = wgf_document_find(document, "list");
    logo = wgf_document_find(document, "logo");
    expect(play && sound && volume && name && quit && first && title && list && logo, "every element found");
    expect(wgf_document_find(document, "play") == play, "the same element, the same handle");
    expect(strcmp(wgf_handle_get_kind_name(play), "ui.element") == 0, "an element's kind");
    expect(wgf_document_find(document, "nothing") == 0 && wgf_document_find(document, "") == 0 &&
               wgf_document_find(document, NULL) == 0 && wgf_document_find(0, "play") == 0,
           "no such element, or no document: none");
    expect(wgf_element_get_document(play) == document && strcmp(wgf_element_get_id(play), "play") == 0,
           "its document and id");

    /* text, RML, attributes, classes, properties */
    expect(strcmp(wgf_element_get_text(title), "Options") == 0 && strcmp(wgf_element_get_text(play), "Play") == 0,
           "its text, the text inside it");
    expect(wgf_element_set_text(title, "A <b>&</b>") && strcmp(wgf_element_get_text(title), "A <b>&</b>") == 0,
           "text set is shown as it is");
    expect(wgf_element_set_rml(title, "Sound <b>on</b>") && strcmp(wgf_element_get_text(title), "Sound on") == 0 &&
               strstr(wgf_element_get_rml(title), "<b>on</b>") != NULL,
           "RML set is parsed");
    expect(wgf_element_set_attribute(title, "data-x", "7") && strcmp(wgf_element_get_attribute(title, "data-x"), "7") == 0 &&
               wgf_element_has_attribute(title, "data-x") && wgf_element_remove_attribute(title, "data-x") &&
               !wgf_element_has_attribute(title, "data-x") && !wgf_element_remove_attribute(title, "data-x") &&
               strcmp(wgf_element_get_attribute(title, "data-x"), "") == 0,
           "an attribute set, read, and removed");
    expect(wgf_element_set_class(title, "hot", true) && wgf_element_has_class(title, "hot") &&
               wgf_element_set_class(title, "hot", false) && !wgf_element_has_class(title, "hot"),
           "a class on and off");
    expect(wgf_element_set_property(title, "width", "120px") &&
               strcmp(wgf_element_get_property(title, "width"), "120px") == 0,
           "a property set and read");
    wgf_log_set_level(WGF_LOG_LEVEL_FATAL); /* RmlUi says why it refuses these, on purpose */
    expect(!wgf_element_set_property(title, "nonsense", "1") && !wgf_element_set_property(title, "width", "wide") &&
               strcmp(wgf_element_get_property(title, "nonsense"), "") == 0,
           "no such property, or no such value: refused");
    wgf_log_set_level(WGF_LOG_LEVEL_INFO);
    expect(wgf_element_set_property(title, "width", "") && strcmp(wgf_element_get_property(title, "width"), "auto") == 0,
           "the element's own taken off");

    /* form controls */
    expect(strcmp(wgf_element_get_value(name), "Rob") == 0 && wgf_element_set_value(name, "Ann") &&
               strcmp(wgf_element_get_value(name), "Ann") == 0,
           "a text field's text");
    expect(wgf_element_get_number(volume) == 5 && wgf_element_set_number(volume, 7) &&
               wgf_element_get_number(volume) == 7 && wgf_element_set_number(volume, 40) &&
               wgf_element_get_number(volume) == 10 && wgf_element_set_number(volume, 7),
           "a slider's number, clamped to its range");
    expect(!wgf_element_is_checked(sound) && wgf_element_set_checked(sound, true) && wgf_element_is_checked(sound) &&
               wgf_element_set_checked(sound, false),
           "a checkbox checked");
    expect(wgf_element_set_disabled(quit, true) == false && wgf_element_set_disabled(name, true) &&
               wgf_element_is_disabled(name) && wgf_element_set_disabled(name, false) && !wgf_element_is_disabled(name),
           "a form control disabled; a button isn't one");
    expect(!wgf_element_set_value(play, "x") && strcmp(wgf_element_get_value(play), "") == 0 &&
               !wgf_element_set_checked(volume, true) && !wgf_element_is_checked(play) &&
               wgf_element_get_number(play) == 0 && !wgf_element_set_number(volume, NAN),
           "what isn't a form control, or a checkbox, or a number: refused");
    expect(!wgf_element_set_text(0, "x") && !wgf_element_set_text(title, NULL) && strcmp(wgf_element_get_text(0), "") == 0 &&
               !wgf_element_set_class(title, "", true) && !wgf_element_set_attribute(title, NULL, "x") &&
               wgf_element_get_document(0) == 0 && !wgf_element_set_attribute(document, "a", "b"),
           "no element, no text, no name, or a handle of another kind: refused");

    /* an element gone with the RML that held it (refused, warned: quieted) */
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(wgf_element_set_rml(list, "<p>two</p>") && !wgf_handle_is_alive(first) && !wgf_element_set_text(first, "x") &&
               strcmp(wgf_element_get_id(first), "") == 0,
           "an element replaced: its handle refused");
    wgf_log_set_level(WGF_LOG_LEVEL_INFO);

    /* the missing document */
    expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED && !wgf_document_set_visible(missing, true) &&
               wgf_document_find(missing, "play") == 0,
           "a document that isn't there FAILED, and refuses");
    expect(wgf_document_take_events(document, events, 1) == 0 && wgf_document_take_events(document, NULL, 8) == 0 &&
               wgf_document_get_event_count(0) == 0,
           "no room for an event, or no document: none taken");
    take(); /* what loading raised */
}

static void on_frame(void *user)
{
    float x, y, w, h;
    (void)user;
    if (ready_frame < 0) {
        before_ready();
        if (document != 0 && wgf_resource_get_status(document) == WGF_RESOURCE_STATUS_READY &&
            wgf_resource_get_status(missing) != WGF_RESOURCE_STATUS_PENDING) {
            ready_frame = frame_number;
            ready();
        } else {
            if (wgf_time_get_seconds() - started > 30.0) {
                expect(false, "the document loaded in 30 s");
                wgf_app_quit();
            }
            frame_number++;
            return;
        }
    }
    expect(wgf_ui_draw_documents(), "drawn");
    switch (frame_number++ - ready_frame) {
    case 0: {
        const int before = sgl_num_vertices();
        expect(wgf_ui_draw_documents(), "drawn twice");
        expect(sgl_num_vertices() > before, "its boxes and text recorded");
        expect(wgf_ui_priv_document_element_bounds(play, &x, &y, &w, &h) && w == 200 && h == 32, "laid out by RCSS");
        expect(wgf_ui_priv_document_element_bounds(logo, &x, &y, &w, &h) && w == 2 && h == 2,
               "an image the document names, its size its file's");
        click_at(play, SAPP_EVENTTYPE_MOUSE_MOVE);
        break;
    }
    case 1:
        expect(wgf_input_is_pointer_captured(), "the pointer over a button: the document's");
        click_at(play, SAPP_EVENTTYPE_MOUSE_DOWN);
        break;
    case 2:
        click_at(play, SAPP_EVENTTYPE_MOUSE_UP);
        break;
    case 3:
        take();
        expect(happened(WGF_DOCUMENT_EVENT_CLICK, play), "a click on the button's label is the button's");
        expect(happened(WGF_DOCUMENT_EVENT_FOCUS, play) && wgf_document_get_focus(document) == play,
               "pressed, it took the focus");
        expect(wgf_input_is_keyboard_captured(), "an element focused: the keyboard the document's");
        click_at(sound, SAPP_EVENTTYPE_MOUSE_MOVE);
        click_at(sound, SAPP_EVENTTYPE_MOUSE_DOWN);
        break;
    case 4:
        click_at(sound, SAPP_EVENTTYPE_MOUSE_UP);
        break;
    case 5:
        take();
        expect(happened(WGF_DOCUMENT_EVENT_CHANGE, sound) && wgf_element_is_checked(sound), "a checkbox clicked: checked");
        expect(happened(WGF_DOCUMENT_EVENT_BLUR, play), "the button lost the focus");
        expect(wgf_document_set_focus(document, name) && wgf_document_get_focus(document) == name, "focus given");
        typed('x');
        break;
    case 6:
        take();
        expect(strcmp(wgf_element_get_value(name), "xAnn") == 0 && happened(WGF_DOCUMENT_EVENT_CHANGE, name),
               "typed into a text field, at its start");
        key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_BACKSPACE);
        break;
    case 7:
        key(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_BACKSPACE);
        expect(strcmp(wgf_element_get_value(name), "Ann") == 0, "a key in a text field");
        key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_TAB);
        break;
    case 8:
        key(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_TAB);
        expect(wgf_document_get_focus(document) == quit, "Tab: the next that takes the focus");
        press_pad(WGF_GAMEPAD_BUTTON_DPAD_UP, true);
        break;
    case 9:
        press_pad(WGF_GAMEPAD_BUTTON_DPAD_UP, false);
        expect(wgf_document_get_focus(document) == name, "the D-pad up: the element above, by nav: auto");
        expect(wgf_document_set_focus(document, play), "focus given");
        press_pad(WGF_GAMEPAD_BUTTON_SOUTH, true);
        break;
    case 10:
        press_pad(WGF_GAMEPAD_BUTTON_SOUTH, false);
        take();
        expect(happened(WGF_DOCUMENT_EVENT_CLICK, play), "the pad's south button clicks what has the focus");
        expect(wgf_document_set_focus(document, 0) && wgf_document_get_focus(document) == 0 &&
                   !wgf_document_set_focus(document, document) && !wgf_document_set_focus(0, play),
               "the focus taken away; another kind, or no document: refused");
        expect(wgf_document_set_visible(document, false) && !wgf_document_is_visible(document), "hidden");
        break;
    case 11:
        expect(!wgf_input_is_pointer_captured() && !wgf_input_is_keyboard_captured(),
               "hidden: neither the pointer nor the keyboard the document's");
        /* released: the document and its elements gone */
        expect(wgf_resource_release(document), "released");
        wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* refused, warned: quieted */
        expect(!wgf_handle_is_alive(document) && !wgf_handle_is_alive(play) && wgf_document_find(document, "play") == 0 &&
                   strcmp(wgf_element_get_text(play), "") == 0,
               "released: the document and its elements refused");
        wgf_log_set_level(WGF_LOG_LEVEL_INFO);
        wgf_resource_release(missing);
        wgf_resource_release(font);
        wgf_app_quit();
        break;
    default: break;
    }
}

int main(void)
{
    wgf_window_set_size(800, 600);
    wgf_app_run(on_init, NULL, on_frame, NULL, NULL);
    expect(ready_frame >= 0 && frame_number - ready_frame >= 12, "every frame ran");
    return failures == 0 ? 0 : 1;
}
