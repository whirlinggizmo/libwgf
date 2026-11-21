#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* The UI's scroll areas and lists, headless, frame by frame through the runtime: an area
 * scrolled by the wheel (the innermost under the pointer), by a drag that lets go of the
 * button it began on (but not of a slider), with a fling after, by its thumb dragged and its
 * track pressed, by Page Down with the focus in it, and to the widget the focus moves to;
 * set and read back, held to its content; and a list chosen by a press and release, stepped
 * by Down and Page Down with the chosen item scrolled into view, a drag choosing none, laid
 * out only where in view. */

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

static void wheel(float x, float y, float notches)
{
    sapp_event event;
    mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x, y);
    memset(&event, 0, sizeof(event));
    event.type = SAPP_EVENTTYPE_MOUSE_SCROLL;
    event.mouse_x = x;
    event.mouse_y = y;
    event.scroll_y = notches;
    wgf_platform_priv_headless_push_event(&event);
}

static wgf_vec4_t bounds(const char *id)
{
    const wgf_vec4_t box = wgf_ui_get_bounds(id);
    expect(box.z > 0 && box.w > 0, "laid out");
    return box;
}

static float scrolled(const char *id)
{
    return wgf_ui_get_scroll(id).y;
}

static int activated = -1, chosen = -1;
static float level = 0.5f;
static char items[1000 * 8];

/* An area 300 by 200: an inner area of six buttons, a slider, and twelve buttons; a list of
 * 1000 items, 4 tall. */
static void form(void)
{
    int i;
    char id[16];
    expect(wgf_ui_begin(), "begun");
    expect(wgf_ui_begin_scroll("area", 300, 200), "an area");
    expect(!wgf_ui_set_width(WGF_UI_SIZING_FIXED, 10) && !wgf_ui_set_height(WGF_UI_SIZING_GROW, 0),
           "an area's size: the call's");
    expect(wgf_ui_begin_scroll("inner", 200, 100), "an inner area");
    for (i = 0; i < 6; i++) {
        snprintf(id, sizeof(id), "i%d", i);
        wgf_ui_button(id, id);
    }
    expect(wgf_ui_end_scroll(), "the inner area ended");
    level = wgf_ui_slider("s", level, 0, 1);
    for (i = 0; i < 12; i++) {
        snprintf(id, sizeof(id), "b%d", i);
        if (wgf_ui_button(id, id)) activated = i;
    }
    expect(wgf_ui_end_scroll(), "the area ended");
    chosen = wgf_ui_list("list", items, chosen, 4);
    expect(wgf_ui_end(), "ended");
}

static const float row = 32.0f; /* the style's text size and half its button padding */
static int frame_number;
static float x0, y0, before;

static void on_frame(void *user)
{
    wgf_vec4_t box;
    (void)user;
    switch (frame_number++) {
    case 0: form(); break;
    case 1:
        form();
        box = bounds("area");
        expect(box.z == 300 && box.w == 200 && scrolled("area") == 0, "an area as sized, unscrolled");
        expect(bounds("b0").x + bounds("b0").z < box.x + 300 - 9, "its bar beside its content");
        wheel(box.x + 250, box.y + 40, -1); /* a notch down, over the area, not the inner one */
        break;
    case 2:
        form();
        expect(scrolled("area") == row && scrolled("inner") == 0, "the wheel: a row down, the area under it alone");
        box = bounds("inner");
        wheel(box.x + 20, box.y + 60, -1); /* below its top, scrolled out of the area */
        break;
    case 3:
        form();
        expect(scrolled("area") == row && scrolled("inner") == row, "the wheel over the inner area: it alone");
        expect(wgf_ui_set_scroll("area", 0, 1e6f) && scrolled("area") > 200 &&
                   wgf_ui_set_scroll("area", 0, -5) && scrolled("area") == 0,
               "set: held to its content");
        expect(!wgf_ui_set_scroll("nope", 0, 0) && !wgf_ui_set_scroll("area", NAN, 0) &&
                   !wgf_ui_set_scroll(NULL, 0, 0) && wgf_ui_get_scroll("nope").y == 0,
               "an id that isn't an area, a value not finite: refused");
        break;
    case 4: /* a press on b0 */
        form();
        box = bounds("b0");
        x0 = box.x + 10;
        y0 = box.y + box.w * 0.5f;
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x0, y0);
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x0, y0);
        break;
    case 5:
        form();
        expect(scrolled("area") == 0, "a press: not a drag yet");
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x0, y0 - 3);
        break;
    case 6:
        form();
        expect(scrolled("area") == 0, "within the slop: still not");
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x0, y0 - 40);
        break;
    case 7:
        form();
        expect(scrolled("area") == 40, "past it: the content follows the pointer");
        mouse(SAPP_EVENTTYPE_MOUSE_UP, x0, y0 - 40);
        break;
    case 8:
        form();
        expect(activated == -1, "the button the drag began on: not activated");
        expect(scrolled("area") > 40, "let go moving: a fling");
        break;
    case 9:
        form();
        wgf_ui_set_scroll("area", 0, 0);
        break;
    case 10: /* a press on the slider, then a drag */
        form();
        box = bounds("s");
        x0 = box.x + box.z * 0.5f;
        y0 = box.y + box.w * 0.5f;
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x0, y0);
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x0, y0);
        break;
    case 11:
        form();
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x0, y0 - 40);
        break;
    case 12:
        form();
        expect(scrolled("area") == 0, "a drag from a slider: the slider's");
        mouse(SAPP_EVENTTYPE_MOUSE_UP, x0, y0 - 40);
        break;
    case 13: /* the thumb, at the bar's top */
        form();
        box = bounds("area");
        x0 = box.x + box.z - 5;
        y0 = box.y + 10;
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x0, y0);
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x0, y0);
        break;
    case 14:
        form();
        expect(scrolled("area") == 0, "a press on the thumb: no page");
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x0, y0 + 30);
        break;
    case 15:
        form();
        before = scrolled("area");
        expect(before > 30, "the thumb dragged: the content further than the thumb");
        mouse(SAPP_EVENTTYPE_MOUSE_UP, x0, y0 + 30);
        break;
    case 16: /* the track, below the thumb */
        form();
        box = bounds("area");
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, box.x + box.z - 5, box.y + box.w - 5);
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, box.x + box.z - 5, box.y + box.w - 5);
        mouse(SAPP_EVENTTYPE_MOUSE_UP, box.x + box.z - 5, box.y + box.w - 5);
        break;
    case 17:
        form();
        expect(scrolled("area") == before + 200, "the track pressed: a page");
        wgf_ui_set_scroll("area", 0, 0);
        wgf_ui_set_focus("b11");
        break;
    case 18: form(); break;
    case 19:
        form();
        box = bounds("area");
        expect(scrolled("area") > 0 && bounds("b11").y + bounds("b11").w <= box.y + box.w,
               "the focus given to a widget out of view: scrolled to");
        wgf_ui_set_focus("b0");
        break;
    case 20: form(); break;
    case 21:
        form();
        box = bounds("area");
        before = scrolled("area");
        expect(bounds("b0").y >= box.y, "and back to the first");
        tap(SAPP_KEYCODE_PAGE_DOWN);
        break;
    case 22: form(); break;
    case 23:
        form();
        expect(scrolled("area") == before + 200, "Page Down with the focus in it: a page");
        box = bounds("list");
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, box.x + 20, box.y + 4 + row * 2.5f);
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, box.x + 20, box.y + 4 + row * 2.5f);
        mouse(SAPP_EVENTTYPE_MOUSE_UP, box.x + 20, box.y + 4 + row * 2.5f);
        break;
    case 24:
        form();
        expect(chosen == 2 && strcmp(wgf_ui_get_focus(), "list") == 0, "a press and release on an item: chosen");
        tap(SAPP_KEYCODE_DOWN);
        break;
    case 25:
        form();
        expect(chosen == 3 && scrolled("list") == 0, "Down: the next, in view already");
        tap(SAPP_KEYCODE_PAGE_DOWN);
        break;
    case 26:
        form();
        expect(chosen == 7 && scrolled("list") == row * 4, "Page Down: a page on, scrolled to");
        expect(wgf_ui_list("x", "A", 0, 0) == 0, "no rows: as given");
        box = bounds("list");
        x0 = box.x + 20;
        y0 = box.y + 4 + row * 0.5f;
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x0, y0);
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x0, y0);
        break;
    case 27:
        form();
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x0, y0 + 20);
        break;
    case 28:
        form();
        expect(scrolled("list") == row * 4 - 20, "a drag on a list: it scrolls");
        mouse(SAPP_EVENTTYPE_MOUSE_UP, x0, y0 + 20);
        break;
    case 29:
        form();
        expect(chosen == 7, "a drag: none chosen");
        expect(wgf_ui_set_scroll("list", 0, 1e9f) && scrolled("list") == row * (1000 - 4), "a list to its end");
        break;
    case 30: form(); break;
    case 31:
        form();
        box = bounds("list");
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, box.x + 20, box.y + 4 + row * 3.5f);
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, box.x + 20, box.y + 4 + row * 3.5f);
        mouse(SAPP_EVENTTYPE_MOUSE_UP, box.x + 20, box.y + 4 + row * 3.5f);
        break;
    case 32:
        form();
        expect(chosen == 999, "the last item, laid out where scrolled to");
        expect(wgf_ui_begin(), "begun");
        expect(!wgf_ui_begin_scroll(NULL, 10, 10) && !wgf_ui_begin_scroll("y", -1, 10) &&
                   !wgf_ui_begin_scroll("y", 10, INFINITY) && !wgf_ui_end_scroll(),
               "no id, a size below 0 or not finite, nothing open: refused");
        expect(wgf_ui_list("empty", "", 3, 2) == -1, "an empty list: none chosen");
        expect(wgf_ui_end(), "ended");
        wgf_app_quit();
        break;
    default: break;
    }
}

static void on_init(void *user)
{
    int i;
    size_t used = 0;
    (void)user;
    for (i = 0; i < 1000; i++) used += (size_t)snprintf(items + used, sizeof(items) - used, "%d\n", i);
}

int main(void)
{
    wgf_window_set_size(800, 800);
    wgf_app_run(on_init, NULL, on_frame, NULL, NULL);
    expect(frame_number >= 33, "every frame ran");
    return failures == 0 ? 0 : 1;
}
