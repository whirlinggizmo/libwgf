#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_time.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* The UI's popup layer, headless, frame by frame through the runtime: a dropdown opened by a
 * press and release, its list over the widget under it (which a press on the list doesn't
 * reach), chosen by a press and release on an item, or by Down and Enter, closed by Escape
 * choosing none, by a press off it that does nothing else, and by not being given; a modal
 * that nothing under answers, the focus put on its first widget, kept among its own, and
 * given back as it closes; a tooltip shown once the pointer rests on its widget, below it;
 * and the whole UI's alpha. */

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

static void mouse(sapp_event_type type, float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
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

static wgf_vec4_t bounds(const char *id)
{
    const wgf_vec4_t box = wgf_ui_get_bounds(id);
    expect(box.z > 0 && box.w > 0, "laid out");
    return box;
}

static void click_on(const char *id)
{
    const wgf_vec4_t box = bounds(id);
    click(box.x + box.z * 0.5f, box.y + box.w * 0.5f);
}

static const float row = 32.0f; /* the style's text size and half its button padding */

/* Where the open list's item `i` is: below the dropdown, by the focus width (3), inset by
 * half the corner radius (4). */
static void click_item(int i)
{
    const wgf_vec4_t box = bounds("dd");
    click(box.x + 20, box.y + box.w + 3 + 4 + row * ((float)i + 0.5f));
}

static int quality = 0, opened = 0, unders = 0, others = 0;
static bool modal, with_dropdown = true;
static int cancels;
static float tip_waited;

/* A panel: a button with a tooltip that opens the modal, the dropdown, and two buttons below
 * it, the first under its open list; the modal: two buttons. */
static void form(void)
{
    expect(wgf_ui_begin(), "begun");
    expect(!wgf_ui_tooltip("nothing before it"), "a tooltip with no widget before it: refused");
    wgf_ui_begin_panel("panel");
    if (wgf_ui_button("open", "Open")) {
        opened++;
        modal = true;
    }
    expect(wgf_ui_tooltip("Opens a modal"), "a tooltip");
    if (with_dropdown) quality = wgf_ui_dropdown("dd", "Low\nMedium\nHigh", quality);
    if (wgf_ui_button("under", "Under")) unders++;
    if (wgf_ui_button("other", "Other")) others++;
    wgf_ui_end_panel();
    if (modal) {
        expect(wgf_ui_begin_modal("modal"), "a modal");
        expect(!wgf_ui_begin_modal("second"), "a second modal: refused");
        wgf_ui_label("Quit?", 0);
        wgf_ui_button("yes", "Yes");
        if (wgf_ui_button("no", "No")) modal = false;
        if (wgf_ui_is_modal_cancelled()) {
            cancels++;
            modal = false;
        }
        expect(wgf_ui_end_modal(), "the modal ended");
    }
    expect(wgf_ui_end(), "ended");
}

static int frame_number;
static double since;

static void on_frame(void *user)
{
    wgf_vec4_t box;
    (void)user;
    switch (frame_number++) {
    case 0: form(); break;
    case 1:
        form();
        expect(wgf_ui_get_bounds("under").y < bounds("dd").y + bounds("dd").w + row * 2, "under where the list opens");
        click_on("dd");
        break;
    case 2: form(); click_item(2); break; /* over "under", under the list */
    case 3:
        form();
        expect(quality == 2 && unders == 0, "a press and release on an item: chosen, the widget under the list not");
        click_item(1); /* closed: "under" */
        break;
    case 4:
        form();
        expect(quality == 2 && unders == 1, "chosen, the list closed");
        click_on("dd");
        break;
    case 5:
        form();
        click_on("other"); /* off the open list */
        break;
    case 6:
        form();
        expect(others == 0, "a press off the list closes it and does nothing else");
        click_on("other");
        break;
    case 7:
        form();
        expect(others == 1, "the press after is the widget's");
        expect(wgf_ui_set_focus("dd"), "the dropdown focused");
        tap(SAPP_KEYCODE_ENTER);
        break;
    case 8: form(); tap(SAPP_KEYCODE_UP); tap(SAPP_KEYCODE_RIGHT); break;
    case 9: form(); tap(SAPP_KEYCODE_UP); break;
    case 10:
        form();
        expect(strcmp(wgf_ui_get_focus(), "dd") == 0 && quality == 2, "open, it keeps the arrows");
        tap(SAPP_KEYCODE_ENTER);
        break;
    case 11:
        form();
        expect(quality == 0, "Up twice and Enter: the first");
        tap(SAPP_KEYCODE_ENTER);
        break;
    case 12: form(); tap(SAPP_KEYCODE_DOWN); break;
    case 13: form(); tap(SAPP_KEYCODE_ESCAPE); break;
    case 14:
        form();
        expect(quality == 0, "Escape: closed, none chosen");
        tap(SAPP_KEYCODE_ENTER); /* open again */
        break;
    case 15:
        with_dropdown = false; /* not given: closed */
        form();
        break;
    case 16:
        with_dropdown = true;
        form();
        click_item(1); /* "under", as it is closed */
        break;
    case 17:
        form();
        expect(unders == 2 && quality == 0, "a dropdown not given a frame: closed");
        click_on("open");
        break;
    case 18:
        form();
        expect(modal && opened == 1, "the modal open");
        expect(strcmp(wgf_ui_get_focus(), "yes") == 0, "the focus on the modal's first widget");
        tap(SAPP_KEYCODE_TAB);
        break;
    case 19:
        form();
        expect(strcmp(wgf_ui_get_focus(), "no") == 0, "Tab: its next");
        tap(SAPP_KEYCODE_TAB);
        break;
    case 20:
        form();
        expect(strcmp(wgf_ui_get_focus(), "yes") == 0, "Tab wraps among its own");
        tap(SAPP_KEYCODE_UP);
        break;
    case 21:
        form();
        expect(strcmp(wgf_ui_get_focus(), "no") == 0, "Up, with none that way: its last");
        click_on("open"); /* under the dim */
        break;
    case 22:
        form();
        expect(opened == 1, "nothing under a modal answers the pointer");
        box = bounds("modal");
        expect(fabsf(box.x + box.z * 0.5f - 400) < 1 && fabsf(box.y + box.w * 0.5f - 400) < 1,
               "at the screen's center");
        click_on("no");
        break;
    case 23:
        form();
        expect(!modal, "closed by its own button");
        break;
    case 24:
        form();
        expect(strcmp(wgf_ui_get_focus(), "open") == 0, "the focus given back as it closed");
        expect(wgf_ui_get_bounds("modal").z == 0, "the modal gone");
        box = bounds("open");
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, box.x + 10, box.y + 10);
        since = wgf_time_get_seconds();
        break;
    case 25: /* the pointer rests on "open": its tooltip, once half a second has gone */
        form();
        box = wgf_ui_get_bounds("wgf-ui-tooltip");
        if (box.z > 0) {
            tip_waited = (float)(wgf_time_get_seconds() - since);
            expect(tip_waited >= 0.5f, "the tooltip after half a second");
            expect(box.y >= bounds("open").y + bounds("open").w, "below its widget");
            mouse(SAPP_EVENTTYPE_MOUSE_DOWN, bounds("open").x + 10, bounds("open").y + 10);
        } else if (wgf_time_get_seconds() - since < 3.0) {
            frame_number--; /* again */
        } else {
            expect(false, "a tooltip shown");
        }
        break;
    case 26:
        form();
        expect(wgf_ui_get_bounds("wgf-ui-tooltip").z == 0, "a press hides it");
        mouse(SAPP_EVENTTYPE_MOUSE_UP, bounds("open").x + 10, bounds("open").y + 10);
        break;
    case 27:
        form();
        expect(wgf_ui_get_bounds("wgf-ui-tooltip").z == 0, "let go: waiting again");
        modal = false;
        break;
    case 28:
        form();
        modal = false;
        expect(wgf_ui_get_alpha() == 1.0f && wgf_ui_set_alpha(0.25f) && wgf_ui_get_alpha() == 0.25f, "the alpha");
        expect(!wgf_ui_set_alpha(1.5f) && !wgf_ui_set_alpha(-0.1f) && !wgf_ui_set_alpha(NAN) &&
                   wgf_ui_get_alpha() == 0.25f,
               "an alpha outside 0 to 1: refused");
        expect(wgf_ui_set_alpha(0.0f), "faded out");
        break;
    case 29:
        form(); /* answered, drawn not at all */
        expect(wgf_ui_set_alpha(1.0f), "back");
        expect(wgf_ui_begin(), "begun");
        expect(wgf_ui_dropdown(NULL, "a", 3) == 3 && wgf_ui_dropdown("x", "", 3) == 3 &&
                   wgf_ui_dropdown("x", NULL, 3) == 3 && wgf_ui_dropdown("y", "a\nb", 7) == 1,
               "no id, no labels: as given; else clamped");
        expect(!wgf_ui_begin_modal(NULL) && !wgf_ui_end_modal() && !wgf_ui_tooltip(NULL) && !wgf_ui_tooltip(""),
               "no id, nothing open, no text: refused");
        expect(!wgf_ui_is_modal_cancelled(), "no modal: none cancelled");
        expect(wgf_ui_end(), "ended");
        modal = true;
        break;
    case 30:
        form();
        expect(cancels == 0 && modal, "a modal, not cancelled");
        tap(SAPP_KEYCODE_ESCAPE);
        break;
    case 31:
        form();
        expect(cancels == 1 && !modal, "Escape: the modal cancelled, once");
        break;
    case 32:
        form();
        expect(cancels == 1 && wgf_ui_get_bounds("modal").z == 0, "and gone");
        wgf_app_quit();
        break;
    default: break;
    }
}

int main(void)
{
    wgf_window_set_size(800, 800);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    expect(frame_number >= 33, "every frame ran");
    expect(!wgf_ui_tooltip("x") && wgf_ui_dropdown("d", "a", 0) == 0 && !wgf_ui_begin_modal("m") &&
               !wgf_ui_is_modal_cancelled(),
           "outside a begun UI: refused");
    return failures == 0 ? 0 : 1;
}
