#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_font.h"
#include "wgf_input.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_text_buffer.h"
#include "wgf_time.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* The UI's text fields, headless, frame by frame through the runtime: a press focuses one and
 * shows the on-screen keyboard; typing, word moves, undo and redo of a run, Shift's
 * selection, copy and paste (newlines stripped), a word deleted, the maximum length; a double
 * press selecting a word, a triple the whole, a drag, and a Shift press; Tab into one
 * selecting the whole; a password never copied; Enter's answer; a press off it letting the
 * focus and the keyboard go; a disabled one answering nothing; a text set on its buffer shown,
 * a longer one cut in the buffer too, a buffer released showing ""; and the refusals. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void push(sapp_event_type type, sapp_keycode key)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key_code = key;
    wgf_platform_priv_headless_push_event(&event);
}

static void tap(sapp_keycode key)
{
    push(SAPP_EVENTTYPE_KEY_DOWN, key);
    push(SAPP_EVENTTYPE_KEY_UP, key);
}

/* `key` tapped with `modifier` held. */
static void chord(sapp_keycode modifier, sapp_keycode key)
{
    push(SAPP_EVENTTYPE_KEY_DOWN, modifier);
    tap(key);
    push(SAPP_EVENTTYPE_KEY_UP, modifier);
}

static void type(const char *text)
{
    for (; *text != '\0'; text++) {
        sapp_event event;
        memset(&event, 0, sizeof(event));
        event.type = SAPP_EVENTTYPE_CHAR;
        event.char_code = (uint32_t)(unsigned char)*text;
        wgf_platform_priv_headless_push_event(&event);
    }
}

static void mouse(sapp_event_type type_of, float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type_of;
    event.mouse_button = SAPP_MOUSEBUTTON_LEFT;
    event.mouse_x = x;
    event.mouse_y = y;
    wgf_platform_priv_headless_push_event(&event);
}

static void click(float x, float y)
{
    mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x, y);
    mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x, y);
    mouse(SAPP_EVENTTYPE_MOUSE_UP, x, y);
}

/* The fields' text buffers, made at the first frame. */
static wgf_text_buffer_t name_text, pass_text, off_text;

static wgf_text_buffer_t buffer_of(const char *id)
{
    return strcmp(id, "name") == 0 ? name_text : strcmp(id, "pass") == 0 ? pass_text : off_text;
}

static const char *text_of(const char *id)
{
    return wgf_text_buffer_get_text(buffer_of(id));
}

/* Where in the field `id` the text's first `count` characters end: its inside starts half
 * the button padding (12) in, its text the style's size (20), unscrolled. */
static float x_after(const char *id, int count)
{
    char prefix[64];
    const wgf_vec4_t box = wgf_ui_get_bounds(id);
    const char *text = text_of(id);
    snprintf(prefix, sizeof(prefix), "%.*s", count, text);
    return box.x + 12.0f + (count > 0 ? wgf_font_measure(0, prefix, 20.0f).x : 0.0f);
}

static float middle(const char *id)
{
    const wgf_vec4_t box = wgf_ui_get_bounds(id);
    return box.y + box.w * 0.5f;
}

static bool is(const char *id, const char *text)
{
    return strcmp(text_of(id), text) == 0;
}

static int submits;
static bool disabled;

static void form(void)
{
    if (name_text == 0) {
        name_text = wgf_text_buffer_create_text(NULL);
        pass_text = wgf_text_buffer_create_text(NULL);
        off_text = wgf_text_buffer_create_text(NULL);
    }
    expect(wgf_ui_begin(), "begun");
    wgf_ui_begin_panel("panel");
    wgf_ui_set_width(WGF_UI_SIZING_FIXED, 400);
    if (wgf_ui_text_field("name", name_text, "Name", 16)) submits++;
    expect(wgf_ui_set_invalid(true) && wgf_ui_is_invalid(), "invalid");
    wgf_ui_password_field("pass", pass_text, NULL, 0);
    expect(wgf_ui_set_invalid(false) && !wgf_ui_is_invalid(), "valid again");
    wgf_ui_set_disabled(disabled);
    wgf_ui_text_field("off", off_text, NULL, 0);
    wgf_ui_set_disabled(false);
    wgf_ui_button("after", "After");
    wgf_ui_end_panel();
    expect(wgf_ui_end(), "ended");
}

static int frame_number;
static double since;

static void on_frame(void *user)
{
    (void)user;
    switch (frame_number++) {
    case 0: form(); break;
    case 1:
        form();
        expect(wgf_ui_get_bounds("name").z >= 300, "as wide as the panel's room");
        click(x_after("name", 0), middle("name"));
        break;
    case 2:
        form();
        expect(strcmp(wgf_ui_get_focus(), "name") == 0, "a press: focused");
        expect(wgf_platform_priv_is_keyboard_shown(), "the on-screen keyboard shown");
        type("hello world");
        break;
    case 3:
        form();
        expect(is("name", "hello world"), "typed");
        chord(SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_LEFT); /* before "world" */
        break;
    case 4: form(); type("big "); break; /* a frame Ctrl was down in types nothing */
    case 5:
        form();
        expect(is("name", "hello big world"), "Ctrl+Left: a word back");
        chord(SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_Z);
        break;
    case 6:
        form();
        expect(is("name", "hello world"), "undo: the run of typing at once");
        push(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_LEFT_SHIFT);
        chord(SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_Z);
        push(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_LEFT_SHIFT);
        break;
    case 7:
        form();
        expect(is("name", "hello big world"), "redo");
        chord(SAPP_KEYCODE_LEFT_SHIFT, SAPP_KEYCODE_HOME);
        break;
    case 8: form(); type("X"); break;
    case 9:
        form();
        expect(is("name", "Xworld"), "Shift+Home selects; typing replaces it");
        chord(SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_A);
        chord(SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_C);
        break;
    case 10:
        form();
        expect(strcmp(wgf_input_get_clipboard(), "Xworld") == 0, "Ctrl+A, Ctrl+C: copied");
        tap(SAPP_KEYCODE_END);
        break;
    case 11: form(); tap(SAPP_KEYCODE_BACKSPACE); break;
    case 12:
        form();
        expect(is("name", "Xworl"), "End, Backspace");
        chord(SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_BACKSPACE);
        break;
    case 13:
        form();
        expect(is("name", ""), "Ctrl+Backspace: the word");
        wgf_input_set_clipboard("a\nb\r\nc");
        chord(SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_V);
        break;
    case 14:
        form();
        expect(is("name", "abc"), "pasted, its newlines stripped");
        type("0123456789uvwxyz");
        break;
    case 15:
        form();
        expect(is("name", "abc0123456789uvw"), "at most its 16 characters");
        expect(wgf_text_buffer_set_text(name_text, "one two three"), "set on its buffer");
        break;
    case 16: form(); click(x_after("name", 5), middle("name")); break; /* in "two" */
    case 17: form(); click(x_after("name", 5), middle("name")); break;
    case 18: form(); type("2"); break;
    case 19:
        form();
        expect(is("name", "one 2 three"), "a double press: the word");
        click(x_after("name", 2), middle("name"));
        break;
    case 20: form(); click(x_after("name", 2), middle("name")); break;
    case 21: form(); click(x_after("name", 2), middle("name")); break;
    case 22: form(); type("Z"); break;
    case 23:
        form();
        expect(is("name", "Z"), "a triple press: the whole");
        wgf_text_buffer_set_text(name_text, "abcdef");
        since = wgf_time_get_seconds();
        break;
    case 24: /* past a double press's time: a drag from after "a" to after "e" */
        form();
        if (wgf_time_get_seconds() - since < 0.5) {
            frame_number--;
            break;
        }
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x_after("name", 1), middle("name"));
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x_after("name", 1), middle("name"));
        break;
    case 25: form(); mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x_after("name", 5), middle("name")); break;
    case 26:
        form();
        mouse(SAPP_EVENTTYPE_MOUSE_UP, x_after("name", 5), middle("name"));
        type("-");
        break;
    case 27:
        form();
        expect(is("name", "a-f"), "a drag selects");
        tap(SAPP_KEYCODE_LEFT); /* the caret after "a" */
        break;
    case 28:
        form();
        push(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_LEFT_SHIFT);
        click(x_after("name", 3), middle("name"));
        push(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_LEFT_SHIFT);
        break;
    case 29: form(); type("+"); break;
    case 30:
        form();
        expect(is("name", "a+"), "a Shift press extends the selection");
        tap(SAPP_KEYCODE_ENTER);
        break;
    case 31:
        form();
        expect(submits == 1, "Enter: true");
        expect(is("name", "a+"), "Enter types nothing");
        tap(SAPP_KEYCODE_TAB);
        break;
    case 32:
        form();
        expect(strcmp(wgf_ui_get_focus(), "pass") == 0, "Tab: the password");
        type("secret");
        break;
    case 33:
        form();
        expect(is("pass", "secret"), "a password typed into");
        chord(SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_A);
        chord(SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_C);
        chord(SAPP_KEYCODE_LEFT_CONTROL, SAPP_KEYCODE_X);
        break;
    case 34:
        form();
        expect(strcmp(wgf_input_get_clipboard(), "a\nb\r\nc") == 0 && is("pass", "secret"),
               "a password: never copied or cut");
        wgf_ui_set_focus("name");
        break;
    case 35: form(); type("q"); break;
    case 36:
        form();
        expect(is("name", "q"), "focus by the program: the whole selected");
        click(10, 10); /* off it */
        break;
    case 37:
        form();
        expect(wgf_ui_get_focus()[0] == '\0', "a press off it: the focus let go");
        expect(!wgf_platform_priv_is_keyboard_shown(), "the keyboard hidden");
        disabled = true;
        break;
    case 38: form(); click(x_after("off", 0), middle("off")); break;
    case 39: form(); type("x"); break;
    case 40:
        form();
        expect(is("off", "") && wgf_ui_get_focus()[0] == '\0', "a disabled field answers nothing");
        expect(wgf_ui_begin(), "begun");
        expect(!wgf_ui_text_field(NULL, name_text, "x", 0) && !wgf_ui_password_field("", pass_text, NULL, 0) &&
                   !wgf_ui_text_field("none", 0, NULL, 0),
               "no id, no buffer: refused");
        expect(wgf_ui_end(), "ended");
        disabled = false;
        /* longer than the field's 16: cut, in the buffer too */
        wgf_text_buffer_set_text(name_text, "abcdefghijklmnopqrstuvwxyz");
        break;
    case 41:
        form();
        expect(is("name", "abcdefghijklmnop"), "a longer text: cut in the buffer too");
        wgf_resource_release(off_text); /* gone: "" shown, nothing taken */
        break;
    case 42: form(); click(x_after("off", 0), middle("off")); break;
    case 43: form(); type("x"); break;
    case 44:
        form();
        expect(is("off", "") && wgf_text_buffer_get_version(off_text) == 0, "a buffer released: \"\", nothing taken");
        wgf_app_quit();
        break;
    default: break;
    }
}

int main(void)
{
    expect(!wgf_ui_set_invalid(true) && !wgf_ui_is_invalid(), "outside a begun UI: refused");
    wgf_window_set_size(800, 800);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    expect(frame_number >= 45, "every frame ran");
    return failures == 0 ? 0 : 1;
}
