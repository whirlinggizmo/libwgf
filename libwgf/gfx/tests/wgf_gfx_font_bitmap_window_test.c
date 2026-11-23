#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_font.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_window.h"

/* Bitmap fonts on a real GPU, in a window (tools/run_in_xvfb.py, OpenGL on a virtual
 * display), their pixels read back: the examples' Gizmo 5 (tools/gen_bitmap_font.py) as a
 * BMFont drawn texel for texel at its own size, tinted; at twice its size sampled nearest,
 * then smoothed once LINEAR; its kerning closing A and V; and as an image of glyphs, drawn
 * in its own colors. */

#define W 64
#define H 32

static int failures;
static wgf_font_t fnt, image;
static unsigned char rgba[W * H * 4];

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* `text` drawn at the top-left on black, the window's pixels read back. */
static void draw(wgf_font_t font, const char *text, float size, wgf_color_t color)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_render_set_clear_color(WGF_COLOR_BLACK);
    wgf_gfx_priv_begin_frame();
    wgf_draw_text(font, text, 0, 0, size, color);
    wgf_gfx_priv_end_frame();
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

/* A pixel from the top-left (GL reads rows from the bottom). */
static const unsigned char *at(int x, int y)
{
    return rgba + ((H - 1 - y) * W + x) * 4;
}

static int is(int x, int y, int r, int g, int b)
{
    const unsigned char *p = at(x, y);
    return abs(p[0] - r) <= 2 && abs(p[1] - g) <= 2 && abs(p[2] - b) <= 2;
}

static void check(void)
{
    int x, y, partial = 0;
    expect(wgf_resource_get_status(fnt) == WGF_RESOURCE_STATUS_READY && wgf_font_get_size(fnt) == 10,
           "the BMFont is READY, 10 pixels a line");
    expect(wgf_resource_get_status(image) == WGF_RESOURCE_STATUS_READY && wgf_font_get_size(image) == 9,
           "the image of glyphs is READY, its glyphs 9 high");

    /* I: "###" then ".#." from a pixel below the line's top */
    draw(fnt, "I", 10, wgf_color_make(255, 0, 0, 255));
    expect(is(0, 0, 0, 0, 0) && is(0, 1, 255, 0, 0) && is(1, 1, 255, 0, 0) && is(2, 1, 255, 0, 0) &&
               is(3, 1, 0, 0, 0),
           "its own size: the top bar texel for texel, tinted red");
    expect(is(0, 2, 0, 0, 0) && is(1, 2, 255, 0, 0) && is(2, 2, 0, 0, 0) && is(1, 7, 255, 0, 0) &&
               is(1, 8, 0, 0, 0),
           "the stem, ending on the baseline");

    draw(fnt, "I", 20, WGF_COLOR_WHITE);
    expect(is(0, 1, 0, 0, 0) && is(0, 2, 255, 255, 255) && is(5, 3, 255, 255, 255) && is(6, 2, 0, 0, 0),
           "twice its size: the bar two pixels a texel");
    expect(is(1, 4, 0, 0, 0) && is(2, 4, 255, 255, 255) && is(3, 5, 255, 255, 255) && is(4, 4, 0, 0, 0),
           "sampled nearest: sharp edges");
    expect(wgf_font_set_filter(fnt, WGF_TEXTURE_FILTER_LINEAR), "linear");
    draw(fnt, "I", 20, WGF_COLOR_WHITE);
    for (y = 0; y < 22; y++) {
        for (x = 0; x < 8; x++) {
            if (at(x, y)[0] > 8 && at(x, y)[0] < 247) partial++;
        }
    }
    printf("smoothed: %d pixels between black and white\n", partial);
    expect(partial >= 4, "LINEAR smooths its edges");
    wgf_font_set_filter(fnt, WGF_TEXTURE_FILTER_NEAREST);

    /* A, then V kerned a pixel closer: V's left side at 5, its right at 9 */
    draw(fnt, "AV", 10, WGF_COLOR_WHITE);
    expect(is(0, 2, 255, 255, 255) && is(4, 2, 255, 255, 255), "A's sides");
    expect(is(5, 1, 255, 255, 255) && is(6, 1, 0, 0, 0) && is(9, 1, 255, 255, 255) && is(10, 1, 0, 0, 0),
           "V kerned a pixel closer than A's advance");
    draw(fnt, "AA", 10, WGF_COLOR_WHITE);
    expect(is(6, 2, 255, 255, 255) && is(5, 2, 0, 0, 0), "A then A: no pair, at A's advance");

    /* the image of glyphs: !, gold at its top, a transparent column after it, no magenta */
    draw(image, "!", 9, WGF_COLOR_WHITE);
    expect(is(0, 0, 255, 236, 120) && is(1, 0, 0, 0, 0), "an image font's glyph in its own colors");
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            if (is(x, y, 255, 0, 255)) {
                expect(0, "no key color drawn");
                return;
            }
        }
    }
}

static int frames;

static void on_frame(void *user)
{
    (void)user;
    if (++frames == 1) {
        wgf_asset_set_host(WGF_TEST_ASSETS);
        fnt = wgf_font_create("fonts/gizmo5/gizmo5.fnt");
        image = wgf_font_create("fonts/gizmo5/gizmo5_image.png");
        return;
    }
    if ((wgf_resource_get_status(fnt) == WGF_RESOURCE_STATUS_PENDING ||
         wgf_resource_get_status(image) == WGF_RESOURCE_STATUS_PENDING) &&
        frames < 600)
        return;
    check();
    exit(failures == 0 ? 0 : 1);
}

int main(void)
{
    wgf_font_enable_bitmaps(); /* what reads a bitmap font's file, linked */
    wgf_window_set_size(W, H);
    wgf_window_set_flags(WGF_WINDOW_FLAG_LOW_DPI); /* one pixel a logical pixel, whatever the display */
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
