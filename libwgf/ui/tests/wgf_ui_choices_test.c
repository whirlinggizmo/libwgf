#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_texture.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* The UI's choosing widgets and its spatial focus, headless, frame by frame through the
 * runtime: tabs chosen by a press and stepped by their arrows (not past either end); a
 * choice; a combo stepped by activation and its arrows, wrapping; a number stepped by its
 * minus and plus and its arrows, on its step's grid and within its range; a grid of tiles
 * laid out in rows, a tile activated; and the arrows moving the focus to the nearest widget
 * that way, falling back to the order, while Tab keeps the order. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void push(sapp_event_type type, sapp_keycode key, uint32_t modifiers)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key_code = key;
    event.modifiers = modifiers;
    wgf_platform_priv_headless_push_event(&event);
}

static void tap(sapp_keycode key)
{
    push(SAPP_EVENTTYPE_KEY_DOWN, key, 0);
    push(SAPP_EVENTTYPE_KEY_UP, key, 0);
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

/* A click `fx` of the way across what `id` was laid out as, at its middle. */
static void click(const char *id, float fx)
{
    const wgf_vec4_t box = wgf_ui_get_bounds(id);
    const float x = box.x + box.z * fx, y = box.y + box.w * 0.5f;
    expect(box.z > 0 && box.w > 0, "laid out");
    mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x, y);
    mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x, y);
    mouse(SAPP_EVENTTYPE_MOUSE_UP, x, y);
}

static bool focused(const char *id)
{
    return strcmp(wgf_ui_get_focus(), id) == 0;
}

static int tab = 0, choice = 0, combo = 0, activated = -1;
static float number = 0.3f;
static wgf_texture_t art;

/* The form: tabs, a choice, a combo, a number, and a grid of seven tiles in rows of 3. */
static void form(void)
{
    int i;
    expect(wgf_ui_begin(), "begun");
    tab = wgf_ui_tabs("tabs", "Game\nVideo\nSound", tab);
    choice = wgf_ui_choice("choice", "Easy\nNormal\nHard", choice);
    combo = wgf_ui_combo("combo", "Red\nGreen\nBlue", combo);
    number = wgf_ui_number("number", number, 0.0f, 1.0f, 0.1f);
    expect(wgf_ui_begin_grid("grid", 3), "a grid");
    for (i = 0; i < 7; i++) {
        char id[16];
        snprintf(id, sizeof(id), "t%d", i);
        if (wgf_ui_tile(id, art, id)) activated = i;
    }
    expect(wgf_ui_end_grid(), "the grid ended");
    expect(wgf_ui_end(), "ended");
}

/* A form: a grid of two columns, each row a name and its button. The names differ in length,
 * and the second row's button is the taller. */
static void rows(void)
{
    expect(wgf_ui_begin(), "begun");
    expect(wgf_ui_begin_grid("rows", 2) && wgf_ui_set_align(WGF_UI_ALIGN_START, WGF_UI_ALIGN_CENTER), "a form");
    wgf_ui_label("A", 16.0f);
    wgf_ui_button("b0", "one");
    wgf_ui_label("A much longer name", 16.0f);
    wgf_ui_begin_box("tall", WGF_UI_DIRECTION_COLUMN);
    wgf_ui_set_width(WGF_UI_SIZING_FIXED, 120.0f);
    wgf_ui_set_height(WGF_UI_SIZING_FIXED, 90.0f);
    wgf_ui_end_box();
    wgf_ui_label("B", 16.0f);
    wgf_ui_button("b2", "three");
    expect(wgf_ui_end_grid() && wgf_ui_end(), "ended");
}

static int frame_number;

static void on_init(void *user)
{
    (void)user;
    art = wgf_texture_create_blank(8, 8);
}

static void on_frame(void *user)
{
    (void)user;
    switch (frame_number++) {
    case 0:
        form();
        click("tabs", 0.5f); /* the middle tab */
        break;
    case 1:
        form();
        expect(tab == 1 && focused("tabs"), "a press on a tab: chosen, the tabs focused");
        tap(SAPP_KEYCODE_RIGHT);
        break;
    case 2:
        form();
        expect(tab == 2 && focused("tabs"), "Right: the next tab, Right kept by the tabs");
        tap(SAPP_KEYCODE_RIGHT);
        break;
    case 3:
        form();
        expect(tab == 2, "Right on the last: still the last");
        click("choice", 0.9f);
        break;
    case 4:
        form();
        expect(choice == 2, "a press on a choice's toggle: chosen");
        click("combo", 0.5f);
        break;
    case 5:
        form();
        expect(combo == 1, "a combo activated: the next");
        tap(SAPP_KEYCODE_LEFT);
        break;
    case 6:
        form();
        expect(combo == 0, "Left: the one before");
        tap(SAPP_KEYCODE_LEFT);
        break;
    case 7:
        form();
        expect(combo == 2, "Left on the first: the last, wrapping");
        click("number", 0.98f); /* its plus */
        break;
    case 8:
        form();
        expect(fabsf(number - 0.4f) < 1e-5f && focused("number"), "its plus: a step more");
        click("number", 0.02f); /* its minus */
        break;
    case 9:
        form();
        expect(fabsf(number - 0.3f) < 1e-5f, "its minus: a step less");
        tap(SAPP_KEYCODE_LEFT);
        break;
    case 10:
        form();
        expect(fabsf(number - 0.2f) < 1e-5f && focused("number"), "Left: a step less, kept by the number");
        {
            const wgf_vec4_t t0 = wgf_ui_get_bounds("t0"), t1 = wgf_ui_get_bounds("t1"), t3 = wgf_ui_get_bounds("t3"),
                             t6 = wgf_ui_get_bounds("t6");
            expect(t0.z == 96 && t1.x > t0.x && t1.y == t0.y && t3.x == t0.x && t3.y > t0.y && t6.y > t3.y,
                   "tiles the style's size wide, in rows of 3");
        }
        expect(wgf_ui_begin(), "begun");
        expect(wgf_ui_number("n", 0.33f, 0.0f, 1.0f, 0.5f) == 0.33f, "untouched: the value as given");
        expect(wgf_ui_number("n2", 3.0f, 0.0f, 1.0f, 0.1f) == 1.0f, "outside its range: clamped");
        expect(wgf_ui_number("n3", 3.0f, 0.0f, 1.0f, 0.0f) == 3.0f &&
                   wgf_ui_number("n4", 3.0f, 1.0f, 1.0f, 0.1f) == 3.0f &&
                   wgf_ui_number(NULL, 3.0f, 0.0f, 9.0f, 1.0f) == 3.0f,
               "no step, no range, or no id: as given");
        expect(wgf_ui_tabs("x", "", 4) == 4 && wgf_ui_tabs("y", NULL, 4) == 4 && wgf_ui_combo(NULL, "A\nB", 1) == 1 &&
                   wgf_ui_choice("z", "A\nB", 9) == 1,
               "no labels or no id: as given; one past the last: the last");
        expect(!wgf_ui_begin_grid("g", 0) && !wgf_ui_tile("t", 12345, "T") && !wgf_ui_end_grid(),
               "a grid of no columns, a picture that isn't a texture, nothing open: refused");
        expect(wgf_ui_end(), "ended");
        wgf_ui_set_focus("t0");
        tap(SAPP_KEYCODE_DOWN);
        break;
    case 11:
        form();
        expect(focused("t3"), "Down from the first tile: the one below it");
        tap(SAPP_KEYCODE_RIGHT);
        break;
    case 12:
        form();
        expect(focused("t4"), "Right: the one beside it");
        tap(SAPP_KEYCODE_DOWN);
        break;
    case 13:
        form();
        expect(focused("t6"), "Down from the middle of a row: the nearest below, the last row's first");
        tap(SAPP_KEYCODE_UP);
        break;
    case 14:
        form();
        expect(focused("t3"), "Up: the one above it");
        wgf_ui_set_focus("t2");
        tap(SAPP_KEYCODE_RIGHT);
        break;
    case 15:
        form();
        expect(focused("t3"), "Right from a row's end, none that way: the next in order");
        tap(SAPP_KEYCODE_ENTER);
        break;
    case 16:
        form();
        expect(activated == 3, "Enter: the focused tile activated");
        wgf_ui_set_focus("t0");
        tap(SAPP_KEYCODE_TAB);
        break;
    case 17:
        form();
        expect(focused("t1"), "Tab: the next in order");
        wgf_ui_set_focus("choice");
        tap(SAPP_KEYCODE_UP);
        break;
    case 18:
        form();
        expect(focused("tabs"), "Up from the choice: the tabs above it");
        click("t5", 0.5f);
        break;
    case 19:
        form();
        expect(activated == 5, "a tile clicked: activated");
        rows();
        break;
    case 20: rows(); break; /* laid out with the columns the first layout measured */
    case 21: {
        const wgf_vec4_t b0 = wgf_ui_get_bounds("b0"), tall = wgf_ui_get_bounds("tall"), b2 = wgf_ui_get_bounds("b2");
        expect(b0.z > 0 && b0.x == tall.x && b0.x == b2.x, "a grid's columns line up: each row's second child");
        expect(tall.y > b0.y + b0.w && b2.y > tall.y + tall.w, "its rows are below each other");
        rows();
        wgf_app_quit();
        break;
    }
    default: break;
    }
}

int main(void)
{
    wgf_window_set_size(800, 800);
    wgf_app_run(on_init, NULL, on_frame, NULL, NULL);
    expect(frame_number >= 22, "every frame ran");
    return failures == 0 ? 0 : 1;
}
