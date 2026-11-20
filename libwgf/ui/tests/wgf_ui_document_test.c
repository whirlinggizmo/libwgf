#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
#include "wgf_render.h"
#include "wgf_texture.h"
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
 * there FAILED; and every refusal. And what RCSS does over time: a transition, an
 * animation, and a tooltip shown by `:hover` on its parent; a finger dragging a list,
 * which scrolls; a text field's on-screen keyboard; and a family's bold and italic faces.
 * And what RmlUi draws past triangles, counted as it is drawn (the dummy backend draws
 * nothing): gradients, a clip mask for rounded corners, a box shadow made on layers and
 * saved, filters and a backdrop filter composited, a mask image; and inside a program's
 * own render target, where layers can't be, the documents drawn without them. */

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
    "  <div id=\"help\">?<div id=\"tip\">A tooltip</div></div>\n"
    "  <div id=\"fade\"></div>\n"
    "  <div id=\"pulse\"></div>\n"
    "  <div id=\"scroller\"><div id=\"row-1\">1</div><div>2</div><div>3</div><div>4</div><div>5</div><div>6</div></div>\n"
    "  <p><b>bold</b> <i>italic</i></p>\n"
    "  <div id=\"fx\"><div id=\"shadowed\">s</div><div id=\"linear\"></div><div id=\"radial\"></div>"
    "<div id=\"conic\"></div><div id=\"rounded\"><p>clipped</p></div><div id=\"filtered\">f</div>"
    "<div id=\"backdrop\"></div><div id=\"masked\">m</div></div>\n"
    "  <p id=\"pixel\">AV</p>\n"
    "</body>\n"
    "</rml>\n";

static const char rcss[] =
    "body { display: block; width: 100%; height: 100%; font-family: Mono; font-size: 16px; color: #ffffff; }\n"
    "h1, div, p, button { display: block; }\n"
    "b { font-weight: bold; }\n"
    "i { font-style: italic; }\n"
    "button { width: 200px; height: 32px; tab-index: auto; nav: auto; background-color: #334; }\n"
    "input.checkbox { display: block; width: 20px; height: 20px; nav: auto; }\n"
    "input.range { display: block; width: 200px; height: 20px; nav: auto; }\n"
    "input.range slidertrack { height: 20px; }\n"
    "input.range sliderbar { width: 10px; height: 20px; }\n"
    "input.text { display: block; width: 200px; height: 24px; nav: auto; }\n"
    "img { display: block; }\n"
    ".hot { color: #ff0000; }\n"
    "#help { width: 40px; height: 20px; }\n"
    "#tip { display: none; }\n"
    "#help:hover #tip { display: block; width: 100px; height: 20px; }\n"
    "#fade { height: 10px; opacity: 1; transition: opacity 0.5s linear-in; }\n"
    "#fade.out { opacity: 0; }\n"
    "@keyframes grow { from { width: 100px; } to { width: 200px; } }\n"
    "#pulse { height: 10px; animation: 0.6s linear-in infinite alternate grow; }\n"
    "#scroller { height: 60px; overflow-y: auto; }\n"
    "#scroller div { height: 30px; }\n"
    "#fx { display: flex; height: 30px; }\n"
    "#fx div { width: 50px; height: 24px; margin-right: 6px; }\n"
    "#shadowed { background-color: #446; border-radius: 6px; box-shadow: #000c 2px 3px 6px 1px; }\n"
    "#linear { decorator: linear-gradient(90deg, #f00, #00f 60%, #0f0); }\n"
    "#radial { decorator: radial-gradient(circle, #fff, #0000); }\n"
    "#conic { decorator: repeating-conic-gradient(#f00, #0f0 25%, #f00 50%); }\n"
    "#rounded { border-radius: 8px; overflow: hidden; background-color: #888; }\n"
    "#filtered { background-color: #c84; filter: blur(2px) opacity(0.5) drop-shadow(#000 1px 2px 2px) "
    "grayscale(1) hue-rotate(30deg); }\n"
    "#backdrop { backdrop-filter: blur(3px) brightness(1.2); }\n"
    "#masked { background-color: #4c8; mask-image: linear-gradient(#000, #0000); }\n"
    "#pixel { display: inline-block; font-family: Pixel; font-size: 20px; }\n";

/* a 2 by 2 PNG, white, opaque */
static const unsigned char png[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xFD, 0xD4, 0x9A, 0x73, 0x00, 0x00, 0x00,
    0x0E, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0x0F, 0x06, 0x0C, 0x10, 0x0A, 0x00, 0x53, 0xBA, 0x0B,
    0xF5, 0x98, 0xAC, 0x20, 0x67, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

/* a bitmap font: A and V, 2 by 2 on the white PNG, advancing 6 in a line 10 high, kerned 2 closer */
static const char fnt[] = "common lineHeight=10 base=8 pages=1\npage id=0 file=\"dot.png\"\n"
                          "char id=65 x=0 y=0 width=2 height=2 xoffset=0 yoffset=6 xadvance=6 page=0\n"
                          "char id=86 x=0 y=0 width=2 height=2 xoffset=0 yoffset=6 xadvance=6 page=0\n"
                          "kerning first=65 second=86 amount=-2\n";

#define TASKS 6
static wgf_handle_t tasks[TASKS];
static wgf_font_t font, bold, pixel;
static wgf_document_t document, missing;
static wgf_element_t play, sound, volume, name, quit, first, logo, help, tip, fade, pulse, row;
static double hold_until; /* the steps wait until then: what RCSS does over time */
static double faded_at;
static float pulse_width, row_x, row_y;
static wgf_texture_t own_target; /* a program's own render target, which documents are drawn into */
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

static void finger(sapp_event_type type, float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.num_touches = 1;
    event.touches[0].identifier = 7;
    event.touches[0].pos_x = x;
    event.touches[0].pos_y = y;
    event.touches[0].changed = true;
    push(event);
}

static float number_of(const char *text)
{
    return (float)atof(text);
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
    tasks[4] = wgf_fs_write("doc_test/mono-bold.ttf", jetbrains_mono_ascii_ttf, (int)sizeof(jetbrains_mono_ascii_ttf));
    tasks[5] = wgf_fs_write("doc_test/pixel.fnt", (const unsigned char *)fnt, (int)strlen(fnt));
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
    for (i = 0; i < TASKS; i++) {
        if (tasks[i] == 0 || wgf_fs_task_get_status(tasks[i]) == WGF_FS_TASK_STATUS_PENDING) return;
    }
    if (font == 0) {
        for (i = 0; i < TASKS; i++) {
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
        /* a family's faces: its bold, and its italic, each a font of its own */
        bold = wgf_font_create("doc_test/mono-bold.ttf");
        expect(wgf_ui_set_document_font_face("Mono", 700, false, bold) &&
                   wgf_ui_get_document_font_face("mono", 700, false) == bold &&
                   wgf_ui_get_document_font_face("mono", 400, false) == font &&
                   wgf_ui_get_document_font_face("mono", 600, false) == 0,
               "a family's bold face, set and read; the family's font its face at 400");
        expect(wgf_ui_priv_document_face_font("Mono", 700, false) == bold &&
                   wgf_ui_priv_document_face_font("Mono", 550, false) == bold &&
                   wgf_ui_priv_document_face_font("Mono", 450, false) == font &&
                   wgf_ui_priv_document_face_font("Mono", 900, true) == bold &&
                   wgf_ui_priv_document_face_font("Mono", 300, true) == font &&
                   wgf_ui_priv_document_face_font("Serif", 400, false) == 0,
               "a face drawn in the nearest the family has; another style's when it has none of its own");
        expect(wgf_ui_set_document_font_face("Mono", 400, true, bold) &&
                   wgf_ui_priv_document_face_font("Mono", 700, true) == bold &&
                   wgf_ui_priv_document_face_font("Mono", 700, false) == bold &&
                   wgf_ui_priv_document_face_font("Mono", 400, false) == font,
               "an italic face: italic text draws in it, upright text never");
        expect(!wgf_ui_set_document_font_face("Mono", 0, false, bold) &&
                   !wgf_ui_set_document_font_face("Mono", 1001, false, bold) &&
                   !wgf_ui_set_document_font_face(NULL, 700, false, bold) &&
                   !wgf_ui_set_document_font_face("Mono", 700, false, 12345),
               "a weight past 1 to 1000, no family, or no font: refused");
        expect(!wgf_ui_draw_documents(), "nothing to draw before a document");
        pixel = wgf_font_create("doc_test/pixel.fnt");
        expect(wgf_ui_set_document_font("Pixel", pixel), "a bitmap font's family");
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
    help = wgf_document_find(document, "help");
    tip = wgf_document_find(document, "tip");
    fade = wgf_document_find(document, "fade");
    pulse = wgf_document_find(document, "pulse");
    row = wgf_document_find(document, "row-1");
    expect(play && sound && volume && name && quit && first && title && list && logo, "every element found");
    expect(help && tip && fade && pulse && row, "and those for what RCSS does over time");
    own_target = wgf_texture_create_target(200, 100);
    expect(own_target != 0, "a render target of the program's own");
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
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
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
    if (wgf_time_get_seconds() < hold_until) return; /* a step waiting on time: the steps hold still */
    switch (frame_number++ - ready_frame) {
    case 0: {
        const int before = sgl_num_vertices();
        int counts[6];
        wgf_ui_priv_document_draw_counts(counts);
        expect(counts[0] >= 3, "gradients drawn: linear, radial, conic");
        expect(counts[1] >= 2, "clip masks drawn: rounded corners, and the box shadow's");
        expect(counts[2] >= 4 && counts[3] >= 4, "layers pushed and composited: filters, a backdrop, a mask, a shadow");
        expect(counts[4] >= 2, "layers saved: the box shadow as a texture, the mask image as a mask");
        expect(counts[5] == 0, "no layer refused on the screen");
        expect(wgf_ui_draw_documents(), "drawn twice");
        expect(sgl_num_vertices() > before, "its boxes and text recorded");
        wgf_ui_priv_document_draw_counts(counts);
        expect(counts[4] >= 2 && counts[2] >= 8, "drawn again: its filters again, its box shadow kept");
        expect(wgf_render_begin_target(own_target), "into the program's own target");
        expect(wgf_ui_draw_documents(), "documents drawn inside it");
        wgf_render_end_target();
        wgf_ui_priv_document_draw_counts(counts);
        expect(counts[5] > 0, "inside a program's target: layers refused, drawn into it instead");
        expect(wgf_ui_priv_document_element_bounds(play, &x, &y, &w, &h) && w == 200 && h == 32, "laid out by RCSS");
        expect(wgf_ui_priv_document_element_bounds(logo, &x, &y, &w, &h) && w == 2 && h == 2,
               "an image the document names, its size its file's");
        /* an image named at run time that isn't local: the change held until its load settles
           (here it fails: there is no such file), and a later change to the same name drops it */
        expect(wgf_element_set_attribute(logo, "src", "late.png") && strcmp(wgf_element_get_attribute(logo, "src"), "dot.png") == 0,
               "an image not local: the change held, the element as it was");
        expect(wgf_element_set_attribute(logo, "data-a", "late2.png") && wgf_element_set_attribute(logo, "data-a", "x") &&
                   strcmp(wgf_element_get_attribute(logo, "data-a"), "x") == 0,
               "a change to the same name: made at once, the held one dropped");
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
        expect(strcmp(wgf_element_get_attribute(logo, "src"), "late.png") == 0 &&
                   strcmp(wgf_element_get_attribute(logo, "data-a"), "x") == 0,
               "the held change made once its image settled; the dropped one never");
        expect(happened(WGF_DOCUMENT_EVENT_BLUR, play), "the button lost the focus");
        expect(wgf_document_set_focus(document, name) && wgf_document_get_focus(document) == name, "focus given");
        typed('x');
        break;
    case 6:
        take();
        expect(strcmp(wgf_element_get_value(name), "xAnn") == 0 && happened(WGF_DOCUMENT_EVENT_CHANGE, name),
               "typed into a text field, at its start");
        expect(wgf_platform_priv_is_keyboard_shown(), "a text field typed into: an on-screen keyboard asked for");
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
        expect(!wgf_platform_priv_is_keyboard_shown(), "the text field left: the keyboard let go");
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
        /* a tooltip: shown by `:hover` on its parent */
        expect(strcmp(wgf_element_get_property(tip, "display"), "none") == 0, "a tooltip hidden");
        click_at(help, SAPP_EVENTTYPE_MOUSE_MOVE);
        break;
    case 11:
        expect(strcmp(wgf_element_get_property(tip, "display"), "block") == 0 &&
                   wgf_ui_priv_document_element_bounds(tip, &x, &y, &w, &h) && w == 100 && h == 20,
               "the pointer over its parent: the tooltip shown, laid out");
        click_at(play, SAPP_EVENTTYPE_MOUSE_MOVE);
        /* a transition: a class taken on fades it over half a second */
        expect(wgf_element_set_class(fade, "out", true), "a class on");
        faded_at = wgf_time_get_seconds();
        pulse_width = number_of(wgf_element_get_property(pulse, "width"));
        expect(pulse_width >= 100.0f && pulse_width <= 200.0f, "an animation under way, within its keyframes");
        hold_until = faded_at + 0.2;
        break;
    case 12: {
        const float opacity = number_of(wgf_element_get_property(fade, "opacity"));
        const float width = number_of(wgf_element_get_property(pulse, "width"));
        expect(strcmp(wgf_element_get_property(tip, "display"), "none") == 0, "the pointer gone: the tooltip hidden");
        expect(opacity > 0.05f && opacity < 0.95f, "a transition part of its way");
        expect(width >= 100.0f && width <= 200.0f && width != pulse_width, "the animation moved on");
        hold_until = faded_at + 0.7;
        break;
    }
    case 13:
        expect(number_of(wgf_element_get_property(fade, "opacity")) == 0.0f, "a transition at its end");
        /* a finger dragging a list scrolls it */
        expect(wgf_ui_priv_document_element_bounds(row, &x, &y, &w, &h), "the list's first row laid out");
        row_x = x + 10.0f; /* where it was */
        row_y = y;
        finger(SAPP_EVENTTYPE_TOUCHES_BEGAN, row_x, y + 45.0f);
        break;
    case 14:
        finger(SAPP_EVENTTYPE_TOUCHES_MOVED, row_x, row_y + 25.0f);
        break;
    case 15:
        finger(SAPP_EVENTTYPE_TOUCHES_MOVED, row_x, row_y + 5.0f);
        break;
    case 16:
        expect(wgf_ui_priv_document_element_bounds(row, &x, &y, &w, &h) && y <= row_y - 39.0f,
               "a finger dragged up: the list scrolled with it");
        finger(SAPP_EVENTTYPE_TOUCHES_ENDED, row_x, row_y + 5.0f);
        break;
    case 17: {
        const wgf_element_t pixel_text = wgf_document_find(document, "pixel");
        expect(wgf_resource_get_status(pixel) == WGF_RESOURCE_STATUS_READY, "the bitmap font loaded");
        expect(wgf_ui_priv_document_element_bounds(pixel_text, &x, &y, &w, &h) && w == 20 && h > 0,
               "text in a bitmap font: its advances and kerning at twice its size");
        expect(wgf_document_set_visible(document, false) && !wgf_document_is_visible(document), "hidden");
        break;
    }
    case 18:
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
        wgf_resource_release(bold);
        wgf_resource_release(pixel);
        wgf_resource_release(own_target);
        wgf_app_quit();
        break;
    default: break;
    }
}

int main(void)
{
    wgf_window_set_size(800, 600);
    wgf_app_run(on_init, NULL, on_frame, NULL, NULL);
    expect(ready_frame >= 0 && frame_number - ready_frame >= 19, "every frame ran");
    return failures == 0 ? 0 : 1;
}
