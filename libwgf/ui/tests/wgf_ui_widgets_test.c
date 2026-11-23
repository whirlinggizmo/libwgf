#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_app.h"
#include "wgf_image.h"
#include "wgf_input.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_ui.h"
#include "wgf_ui_priv.h"
#include "wgf_window.h"

/* The UI's widgets past buttons, headless, frame by frame through the runtime: a toggle and
 * a checkbox turned by the pointer and by Enter; a slider set by a press, dragged past its
 * end, and moved by its arrows while the others move the focus, then standing up; an image
 * button answering only on its mask; disabled widgets answering nothing and skipped by the
 * focus; an image, a separator, a group, and a custom room laid out; and skins: set, read
 * back, drawn in place of the colors, a fill clipped, a state standing in for another, and
 * every refusal. */

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

/* A point in what `id` was laid out as: `fx`, `fy` of the way across and down. */
static void at(const char *id, float fx, float fy, float *x, float *y)
{
    const wgf_vec4_t box = wgf_ui_get_bounds(id);
    expect(box.z > 0 && box.w > 0, "laid out");
    *x = box.x + box.z * fx;
    *y = box.y + box.w * fy;
}

static void click(const char *id, float fx, float fy)
{
    float x, y;
    at(id, fx, fy, &x, &y);
    mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x, y);
    mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x, y);
    mouse(SAPP_EVENTTYPE_MOUSE_UP, x, y);
}

/* The form: a toggle, a checkbox, a slider, an image button, and a disabled button, which
 * keep their values here. */
static bool sound = false, music = true, with_disabled = true;
static float volume = 5.0f;
static int stars;
static wgf_texture_t star;
static wgf_image_t star_mask;

static void form(void)
{
    expect(wgf_ui_begin(), "begun");
    wgf_ui_begin_panel("form");
    sound = wgf_ui_toggle("sound", "Sound", sound);
    music = wgf_ui_checkbox("music", "Music", music);
    volume = wgf_ui_slider("volume", volume, 0.0f, 10.0f);
    if (wgf_ui_image_button("star", star, 0, star_mask)) stars++;
    if (with_disabled) {
        expect(wgf_ui_set_disabled(true) && wgf_ui_is_disabled(), "disabled from here");
        expect(!wgf_ui_button("save", "Save"), "a disabled button: never activated");
        expect(wgf_ui_set_disabled(false) && !wgf_ui_is_disabled(), "and enabled again");
    }
    wgf_ui_end_panel();
    expect(wgf_ui_end(), "ended");
}

/* The vertices a UI drew, from `build`. */
static int drawn(void (*build)(void))
{
    const int before = sgl_num_vertices();
    expect(wgf_ui_begin(), "begun");
    build();
    expect(wgf_ui_end(), "ended");
    return sgl_num_vertices() - before;
}

static void one_bar(void)
{
    wgf_ui_progress(0.5f);
}

static void one_button(void)
{
    wgf_ui_button("b", "B");
}

static void one_disabled_button(void)
{
    wgf_ui_set_disabled(true);
    wgf_ui_button("b", "B");
}

static int frame_number;
static wgf_texture_t art;

static void on_init(void *user)
{
    int x, y;
    (void)user;
    star = wgf_texture_create_blank(40, 40);
    star_mask = wgf_image_create_color(40, 40, 0x00000000u); /* clear but its right half */
    for (y = 0; y < 40; y++) {
        for (x = 20; x < 40; x++) wgf_image_set_pixel(star_mask, x, y, 0xFFFFFFFFu);
    }
    art = wgf_texture_create_blank(16, 16);
}

static void on_frame(void *user)
{
    float x, y;
    (void)user;
    switch (frame_number++) {
    case 0:
        expect(!wgf_ui_set_disabled(true) && !wgf_ui_is_disabled(), "disabled outside a UI: refused");
        wgf_log_set_level(WGF_LOG_LEVEL_FATAL); /* each logs, on purpose */
        expect(wgf_ui_toggle("t", "T", true) && !wgf_ui_checkbox("c", "C", false) &&
                   wgf_ui_slider("s", 3.0f, 0.0f, 10.0f) == 3.0f,
               "outside a UI: each value as given");
        wgf_log_set_level(WGF_LOG_LEVEL_INFO);
        form();
        click("sound", 0.5f, 0.5f);
        break;
    case 1:
        form();
        expect(sound, "a toggle clicked: on");
        click("music", 0.9f, 0.5f); /* its text: the checkbox's too */
        break;
    case 2:
        form();
        expect(!music, "a checkbox's text clicked: unchecked");
        expect(strcmp(wgf_ui_get_focus(), "music") == 0, "and focused");
        tap(SAPP_KEYCODE_ENTER);
        break;
    case 3:
        form();
        expect(music, "Enter: checked again");
        at("volume", 0.0f, 0.5f, &x, &y);
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x + 2, y);
        mouse(SAPP_EVENTTYPE_MOUSE_DOWN, x + 2, y);
        break;
    case 4:
        form();
        expect(volume == 0.0f, "a press at the slider's start: its least");
        expect(strcmp(wgf_ui_get_focus(), "volume") == 0, "and focused");
        at("volume", 0.5f, 0.5f, &x, &y);
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x, y);
        break;
    case 5:
        form();
        expect(fabsf(volume - 5.0f) < 0.01f, "dragged to its middle: half");
        at("volume", 1.0f, 0.5f, &x, &y);
        mouse(SAPP_EVENTTYPE_MOUSE_MOVE, x + 300, y + 200);
        break;
    case 6:
        form();
        expect(volume == 10.0f, "dragged past its end, off it: its most");
        expect(wgf_input_is_pointer_captured(), "a drag: the pointer is the UI's");
        mouse(SAPP_EVENTTYPE_MOUSE_UP, 2, 2);
        tap(SAPP_KEYCODE_LEFT);
        break;
    case 7:
        form();
        expect(volume == 9.5f, "Left on a focused slider: a twentieth less");
        expect(strcmp(wgf_ui_get_focus(), "volume") == 0, "Left kept by the slider, not the focus's");
        tap(SAPP_KEYCODE_DOWN);
        break;
    case 8:
        form();
        expect(strcmp(wgf_ui_get_focus(), "star") == 0, "Down: the focus's, to the next");
        tap(SAPP_KEYCODE_DOWN);
        break;
    case 9:
        form();
        expect(strcmp(wgf_ui_get_focus(), "sound") == 0, "past the last, the disabled one skipped: the first");
        click("star", 0.25f, 0.5f);
        break;
    case 10:
        form();
        expect(stars == 0, "the image button's clear half: not pressed");
        click("star", 0.75f, 0.5f);
        break;
    case 11:
        form();
        expect(stars == 1, "its art: pressed");
        click("save", 0.5f, 0.5f);
        break;
    case 12:
        form();
        expect(strcmp(wgf_ui_get_focus(), "save") != 0, "a disabled button clicked: not focused");
        expect(wgf_ui_set_fill(WGF_UI_WIDGET_SLIDER, WGF_UI_FILL_STRETCHED, WGF_UI_FROM_BOTTOM) &&
                   wgf_ui_get_fill_from(WGF_UI_WIDGET_SLIDER) == WGF_UI_FROM_BOTTOM,
               "a slider filling from the bottom");
        wgf_ui_set_focus("volume");
        tap(SAPP_KEYCODE_UP);
        break;
    case 13: {
        wgf_vec4_t box;
        form();
        box = wgf_ui_get_bounds("volume");
        expect(box.w > box.z, "it stands up");
        expect(volume == 10.0f && strcmp(wgf_ui_get_focus(), "volume") == 0, "Up on it: more (at most its most), kept");
        expect(!wgf_ui_set_fill(WGF_UI_WIDGET_BUTTON, WGF_UI_FILL_CLIPPED, WGF_UI_FROM_LEFT) &&
                   !wgf_ui_set_fill(WGF_UI_WIDGET_SLIDER, (wgf_ui_fill_t)2, WGF_UI_FROM_LEFT) &&
                   !wgf_ui_set_fill(WGF_UI_WIDGET_SLIDER, WGF_UI_FILL_CLIPPED, (wgf_ui_from_t)4),
               "a fill for a widget without one, or one that isn't one: refused");
        wgf_ui_reset_style();
        expect(wgf_ui_get_fill_from(WGF_UI_WIDGET_SLIDER) == WGF_UI_FROM_LEFT, "reset: from the left again");

        /* the slider's refusals */
        expect(wgf_ui_begin(), "begun");
        expect(wgf_ui_slider("s", 3.0f, 5.0f, 5.0f) == 3.0f && wgf_ui_slider("s", 3.0f, 0.0f, NAN) == 3.0f &&
                   wgf_ui_slider(NULL, 3.0f, 0.0f, 10.0f) == 3.0f,
               "no range or no id: the value as given");
        expect(wgf_ui_slider("s", 12.0f, 0.0f, 10.0f) == 10.0f && wgf_ui_slider("s2", NAN, 0.0f, 10.0f) == 0.0f,
               "a value outside its range: clamped (NaN: its least)");
        expect(!wgf_ui_toggle("", "T", false) && wgf_ui_checkbox(NULL, "C", true), "no id: as given");
        expect(wgf_ui_end(), "ended");

        /* laid out: an image, a separator, a group, a custom room */
        expect(wgf_ui_begin() && wgf_ui_begin_box("column", WGF_UI_DIRECTION_COLUMN) &&
                   wgf_ui_set_width(WGF_UI_SIZING_FIXED, 300) && wgf_ui_image(art, 0, 0) &&
                   wgf_ui_image(art, 30, 20) && wgf_ui_separator() && wgf_ui_begin_group("group", "Title") &&
                   wgf_ui_custom("room", 50, 40) && wgf_ui_end_group() && wgf_ui_end_box() && wgf_ui_end(),
               "a column of them");
        box = wgf_ui_get_bounds("room");
        expect(box.z == 50 && box.w == 40, "the custom room's size, where it was laid out");
        box = wgf_ui_get_bounds("group");
        expect(box.z > 50 && box.w > 40 + 20, "the group around it, its title above");
        box = wgf_ui_get_bounds("nothing");
        expect(box.x == 0 && box.y == 0 && box.z == 0 && box.w == 0, "an id not laid out: all 0");
        expect(wgf_ui_begin(), "begun");
        expect(!wgf_ui_image(12345, 0, 0) && !wgf_ui_custom("", 1, 1) && !wgf_ui_custom("c", -1, 1) &&
                   !wgf_ui_image_button("i", 0, 0, 0) && !wgf_ui_image_button("i", star, 12345, 0) &&
                   !wgf_ui_image_button("i", star, 0, art) && !wgf_ui_icon_button("i", 12345, "I") &&
                   !wgf_ui_end_group(),
               "a handle that isn't its kind, no id, less than no room, nothing open: refused");
        expect(wgf_ui_icon_button("icon", art, "Icon") == false, "an icon button, not activated");
        expect(wgf_ui_end(), "ended");
        break;
    }
    case 14: {
        int plain, skinned_bar, clipped, disabled;
        /* skins: set and read back */
        expect(wgf_ui_get_skin(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL) == 0 &&
                   wgf_ui_get_skin_tint(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL) == 0xFFFFFFFFu,
               "no skin: none, tinted white");
        expect(wgf_ui_set_skin(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, art, 0, 4, 16, 8) &&
                   wgf_ui_set_skin_borders(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, 1, 2, 3, 4) &&
                   wgf_ui_set_skin_tint(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, 0xFF0000FFu) &&
                   wgf_ui_set_text_offset(WGF_UI_WIDGET_BUTTON, WGF_UI_STATE_PRESSED, 0, 2),
               "a button's skin set");
        {
            const wgf_vec4_t source =
                wgf_ui_get_skin_source(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL);
            const wgf_vec4_t borders =
                wgf_ui_get_skin_borders(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL);
            const wgf_vec2_t offset = wgf_ui_get_text_offset(WGF_UI_WIDGET_BUTTON, WGF_UI_STATE_PRESSED);
            expect(wgf_ui_get_skin(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL) == art &&
                       source.y == 4 && source.z == 16 && borders.x == 1 && borders.w == 4 &&
                       wgf_ui_get_skin_tint(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL) ==
                           0xFF0000FFu &&
                       offset.x == 0 && offset.y == 2,
                   "and read back");
        }
        expect(!wgf_ui_set_skin((wgf_ui_widget_t)16, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, art, 0, 0, 0, 0) &&
                   !wgf_ui_set_skin(WGF_UI_WIDGET_BUTTON, (wgf_ui_part_t)5, WGF_UI_STATE_NORMAL, art, 0, 0, 0, 0) &&
                   !wgf_ui_set_skin(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, (wgf_ui_state_t)4, art, 0, 0, 0, 0) &&
                   !wgf_ui_set_skin(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, 12345, 0, 0, 0, 0) &&
                   !wgf_ui_set_skin(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, art, NAN, 0, 0, 0) &&
                   !wgf_ui_set_skin_borders(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, -1, 0, 0, 0) &&
                   !wgf_ui_set_skin_patch(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL,
                                          (wgf_ui_patch_t)3) &&
                   !wgf_ui_set_skin_patch((wgf_ui_widget_t)16, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL,
                                          WGF_UI_PATCH_TILED) &&
                   !wgf_ui_set_text_offset(WGF_UI_WIDGET_BUTTON, WGF_UI_STATE_NORMAL, INFINITY, 0) &&
                   wgf_ui_get_skin((wgf_ui_widget_t)16, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL) == 0,
               "a widget, part, or state that isn't one, a handle that isn't a texture, a value not finite: refused");

        /* drawn from it: the button's art, the disabled state the normal one's, dimmed */
        plain = drawn(one_bar);
        disabled = drawn(one_disabled_button);
        expect(disabled > 0 && disabled == drawn(one_button), "a disabled button drawn from the normal state's art");
        { /* its patches tiled: more of them than the nine stretched */
            const int stretched = drawn(one_button);
            int tiled, fit;
            expect(wgf_ui_get_skin_patch(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL) ==
                           WGF_UI_PATCH_STRETCHED &&
                       wgf_ui_set_skin_patch(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL,
                                             WGF_UI_PATCH_TILED) &&
                       wgf_ui_get_skin_patch(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL) ==
                           WGF_UI_PATCH_TILED,
                   "a skin's patches tiled, read back");
            tiled = drawn(one_button);
            expect(wgf_ui_set_skin_patch(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL,
                                         WGF_UI_PATCH_TILED_FIT),
                   "tiled to fit");
            fit = drawn(one_button);
            if (getenv("WGF_TEST_SHOW")) printf("patches: stretched %d, tiled %d, fit %d\n", stretched, tiled, fit);
            expect(tiled > stretched && fit > stretched, "tiled: its art repeated");
            expect(wgf_ui_set_skin_patch(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL,
                                         WGF_UI_PATCH_STRETCHED) &&
                       drawn(one_button) == stretched,
                   "stretched again");
        }
        expect(wgf_ui_set_skin(WGF_UI_WIDGET_PROGRESS, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL, art, 0, 0, 0, 0) &&
                   wgf_ui_set_skin(WGF_UI_WIDGET_PROGRESS, WGF_UI_PART_FILL, WGF_UI_STATE_NORMAL, art, 0, 0, 0, 0),
               "a progress bar's skins");
        skinned_bar = drawn(one_bar);
        expect(plain > 0 && skinned_bar > 0 && skinned_bar != plain, "a bar drawn from its art, not its colors");
        expect(wgf_ui_set_fill(WGF_UI_WIDGET_PROGRESS, WGF_UI_FILL_CLIPPED, WGF_UI_FROM_RIGHT) &&
                   wgf_ui_get_fill(WGF_UI_WIDGET_PROGRESS) == WGF_UI_FILL_CLIPPED,
               "its fill clipped, from the right");
        clipped = drawn(one_bar);
        expect(clipped == skinned_bar, "clipped: the whole fill drawn, the clip showing part of it");
        expect(wgf_ui_set_skin(WGF_UI_WIDGET_PROGRESS, WGF_UI_PART_EDGE, WGF_UI_STATE_NORMAL, art, 0, 0, 0, 0) &&
                   drawn(one_bar) > clipped,
               "an edge at its front");

        /* before skins: by a style color */
        expect(wgf_ui_set_style_texture(WGF_UI_COLOR_BUTTON_HOVERED, art, 2, 2, 2, 2) &&
                   wgf_ui_get_skin(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_HOVERED) == art &&
                   wgf_ui_get_style_texture(WGF_UI_COLOR_BAR_FILL) == art,
               "a style color's art: its widget's part's skin");
        expect(wgf_resource_release(art) && wgf_resource_get_status(art) != WGF_RESOURCE_STATUS_NONE,
               "the skins hold their own references");
        break;
    }
    case 15:
        wgf_ui_reset_style();
        expect(wgf_ui_get_skin(WGF_UI_WIDGET_BUTTON, WGF_UI_PART_BASE, WGF_UI_STATE_NORMAL) == 0 &&
                   wgf_ui_get_text_offset(WGF_UI_WIDGET_BUTTON, WGF_UI_STATE_PRESSED).y == 0 &&
                   wgf_resource_get_status(art) == WGF_RESOURCE_STATUS_NONE,
               "reset: every skin and offset let go");
        wgf_app_quit();
        break;
    default: break;
    }
}

int main(void)
{
    wgf_window_set_size(800, 600);
    wgf_app_run(on_init, NULL, on_frame, NULL, NULL);
    expect(frame_number >= 16, "every frame ran");
    return failures == 0 ? 0 : 1;
}
