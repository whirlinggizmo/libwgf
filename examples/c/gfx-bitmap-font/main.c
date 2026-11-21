#include <stdbool.h>
#include <stdio.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_font.h"
#include "wgf_keyboard.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage2d.h"
#include "wgf_text.h"
#include "wgf_window.h"

/* Bitmap fonts, raylib's two: Gizmo 5 (tools/gen_bitmap_font.py) as an AngelCode BMFont,
 * its page tinted, at its own size and whole multiples of it, its kerning pairs closing
 * "AV" and "To"; and as an image of glyphs on magenta, drawn in its own colors. A text actor
 * wraps a paragraph in it; letter spacing and the sampling filter can be changed.
 *
 *   F     the filter: NEAREST (sharp texels) or LINEAR (smoothed)
 *   S     letter spacing: none or 2 pixels
 *   Esc   quit, where quitting means anything */

#define FNT_PATH "fonts/gizmo5/gizmo5.fnt"
#define IMAGE_PATH "fonts/gizmo5/gizmo5_image.png"

static wgf_font_t fnt, image;
static wgf_actor_t stage, paragraph;
static bool spaced;

static void frame(void *user)
{
    const float size = wgf_font_get_size(fnt) > 0 ? wgf_font_get_size(fnt) : 10.0f;
    char line[96];
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_F)) {
        wgf_font_set_filter(fnt, wgf_font_get_filter(fnt) == WGF_TEXTURE_FILTER_NEAREST ? WGF_TEXTURE_FILTER_LINEAR
                                                                                        : WGF_TEXTURE_FILTER_NEAREST);
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_S)) {
        spaced = !spaced;
        wgf_text_set_spacing(paragraph, spaced ? 2.0f : 0.0f, 0.0f);
    }
    if (wgf_resource_get_status(fnt) != WGF_RESOURCE_STATUS_READY) {
        wgf_draw_text(0,
                      wgf_resource_get_status(fnt) == WGF_RESOURCE_STATUS_FAILED ? "a font failed to load"
                                                                                 : "loading fonts...",
                      40, 40, 20, WGF_COLOR_GRAY);
        return;
    }

    /* its own size, then whole multiples of it */
    wgf_draw_text(fnt, "Gizmo 5 at its own size: The quick brown fox jumps over the lazy dog.", 40, 30, size,
                  WGF_COLOR_RAYWHITE);
    wgf_draw_text(fnt, "x2: AVAST, To Yonder! 0123456789", 40, 50, size * 2, WGF_COLOR_GOLD);
    wgf_draw_text(fnt, "x3: kerned AV To", 40, 80, size * 3, WGF_COLOR_SKYBLUE);
    wgf_draw_text(fnt, "x4: {pixels}", 40, 120, size * 4, WGF_COLOR_PINK);

    /* measured, and centered */
    {
        const char *title = "measured and centered";
        const wgf_vec2_t measured = wgf_font_measure(fnt, title, size * 2);
        const float width = (float)wgf_window_get_width();
        wgf_draw_rectangle((width - measured.x) * 0.5f - 4, 176, measured.x + 8, measured.y + 8,
                           wgf_color_make(60, 60, 90, 255));
        wgf_draw_text(fnt, title, (width - measured.x) * 0.5f, 180, size * 2, WGF_COLOR_WHITE);
    }

    /* the image of glyphs, in its own colors */
    wgf_draw_text(image, "An image of glyphs, in its colors!", 40, 220, wgf_font_get_size(image) * 3, WGF_COLOR_WHITE);

    /* a text actor wrapping a paragraph */
    wgf_stage2d_draw(stage);

    snprintf(line, sizeof(line), "[F] filter: %s   [S] letter spacing: %s",
             wgf_font_get_filter(fnt) == WGF_TEXTURE_FILTER_NEAREST ? "NEAREST" : "LINEAR", spaced ? "2" : "0");
    wgf_draw_text(fnt, line, 40, 440, size * 2, WGF_COLOR_LIGHTGRAY);
}

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(24, 24, 36, 255));
    fnt = wgf_font_create(FNT_PATH);
    image = wgf_font_create(IMAGE_PATH);
    stage = wgf_stage2d_create();
    paragraph = wgf_text_create(fnt);
    wgf_text_set_string(paragraph, "A text actor in a bitmap font, wrapped at 400 pixels: its lines break between "
                                   "words, and its size is twice the font's own, so every texel is two pixels.");
    wgf_text_set_font_size(paragraph, 20);
    wgf_text_set_wrap_width(paragraph, 400);
    wgf_text_set_color(paragraph, WGF_COLOR_LIME);
    wgf_actor_set_position(paragraph, 40, 290, 0);
    wgf_actor_set_parent(paragraph, stage);
}

static void on_shutdown(void *user)
{
    (void)user;
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_resource_release(fnt);
    wgf_resource_release(image);
}

int main(void)
{
    wgf_font_enable_bitmaps(); /* what reads a bitmap font's file: linked, as this example draws in one */
    wgf_window_set_title("libwgf bitmap font");
    wgf_window_set_size(900, 480);
    wgf_app_run(init, NULL, frame, on_shutdown, NULL);
    return 0;
}
