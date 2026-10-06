#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf_platform_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "jetbrains_mono_ascii.h"
#include "text/wgf_gfx_font_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_time.h"
#include "wgf_canvas.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_font.h"
#include "wgf_node.h"
#include "wgf_text.h"

/* Fonts and text on sokol's dummy backend (a headless build): the built-in font,
 * loading, failure falling back, the default, parking, layout, the text node, and
 * what is recorded. Pixels: wgf_gfx_text_web_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* A frame at a size and DPI scale: the headless window's, as the runtime begins one. */
static void begin_frame(int width, int height, float dpi_scale)
{
    wgf_platform_priv_set_size(width, height);
    wgf_platform_priv_headless_set_dpi_scale(dpi_scale);
    wgf_gfx_priv_begin_frame();
}

/* A worker decodes it: wait on time, not on a count of updates. */
static void settle_font(wgf_handle_t font)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0)
        wgf_core_priv_update();
}

static void write_file(const char *path, const unsigned char *data, int size)
{
    const wgf_handle_t task = wgf_fs_write(path, data, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

int main(void)
{
    wgf_handle_t font, broken, again, text;
    wgf_vec2_t one, two, loaded;
    char source[32] = "hello";

    wgf_core_priv_init();
    expect(wgf_font_measure(0, "x", 16).x == 0, "nothing measures before setup");
    expect(wgf_gfx_priv_start(), "setup");

    /* the built-in font, with no file */
    one = wgf_font_measure(0, "Hello", 16);
    two = wgf_font_measure(0, "Hello\nWorld!", 16);
    expect(one.x > 0 && one.y > 0, "the built-in font measures");
    expect(fabsf(two.y - 2 * one.y) < 0.01f && two.x > one.x, "two lines: twice as tall, as wide as the widest");
    expect(wgf_font_measure(0, "Hello", 32).x > one.x * 1.9f, "twice the size, about twice as wide");
    expect(wgf_font_get_default() == 0, "the default is the built-in font");

    /* layout, as wgrender's */
    {
        const wgf_vec2_t word = wgf_font_measure(0, "abcdefghijkl", 16);
        float left, top, width, height;
        expect(wgf_font_measure(0, "", 16).x == 0 && wgf_font_measure(0, "", 16).y == 0, "empty text is 0 by 0");
        expect(wgf_font_measure(0, " ", 16).x > 0, "a line keeps its spaces: a space measures its advance");
        expect(wgf_font_measure(0, "Hello", 0).x == one.x && wgf_font_measure(0, "Hello", -2).x == one.x,
               "a size of 0 or less is the default size, 16");
        expect(wgf_gfx_priv_font_block_bounds(0, "Hello\tWorld", 16, one.x + 1, WGF_TEXT_HALIGN_LEFT,
                                              WGF_TEXT_VALIGN_TOP, &left, &top, &width, &height) &&
                   fabsf(height - 2 * one.y) < 0.01f,
               "tabs wrap as spaces do");
        expect(wgf_gfx_priv_font_block_bounds(0, "Hi", 16, 200, WGF_TEXT_HALIGN_CENTER, WGF_TEXT_VALIGN_MIDDLE, &left,
                                              &top, &width, &height) &&
                   width == 200 && left == -100 && top == -height * 0.5f,
               "a wrapped block is its wrap width wide, aligned by it");
        expect(wgf_gfx_priv_font_block_bounds(0, "abcdefghijkl", 16, 10, WGF_TEXT_HALIGN_RIGHT, WGF_TEXT_VALIGN_TOP,
                                              &left, &top, &width, &height) &&
                   fabsf(width - word.x) < 0.01f && fabsf(left + word.x) < 0.01f,
               "a word wider than the wrap width widens the block");
        expect(!wgf_gfx_priv_font_block_bounds(0, "", 16, 0, WGF_TEXT_HALIGN_LEFT, WGF_TEXT_VALIGN_TOP, &left, &top,
                                               &width, &height),
               "empty text has no block");
        /* a dense display: laid out at twice the pixels, as it is drawn there (fontstash
           rounds advances at the size it rasterizes, so this isn't one's width) */
        {
            const wgf_vec2_t twice = wgf_font_measure(0, "Hello", 32);
            begin_frame(64, 64, 2.0f);
            expect(wgf_font_measure(0, "Hello", 16).x == twice.x / 2 && wgf_font_measure(0, "Hello", 16).y == twice.y / 2,
                   "measured at the density it is drawn, in logical pixels");
        }
        wgf_gfx_priv_end_frame();
        begin_frame(64, 64, 1.0f); /* back to one pixel a pixel */
        wgf_gfx_priv_end_frame();
    }

    /* a font from a file */
    write_file("text_test/mono.ttf", jetbrains_mono_ascii_ttf, (int)sizeof(jetbrains_mono_ascii_ttf));
    write_file("text_test/broken.ttf", (const unsigned char *)"not a font", 10);
    write_file("text_test/cut.ttf", jetbrains_mono_ascii_ttf, 300); /* a real font's start, cut short */
    font = wgf_font_create("text_test/mono.ttf");
    broken = wgf_font_create("text_test/broken.ttf");
    expect(wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_PENDING, "pending at once");
    settle_font(font);
    settle_font(broken);
    expect(wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_READY, "loaded");
    expect(wgf_resource_get_status(broken) == WGF_RESOURCE_STATUS_FAILED, "a file that isn't a font fails");
    expect(strcmp(wgf_resource_get_path(font), "text_test/mono.ttf") == 0 && strcmp(wgf_resource_get_path(0), "") == 0 &&
               strcmp(wgf_resource_get_path(12345), "") == 0,
           "its path; none for the default font, or what isn't a font");
    {
        const wgf_handle_t cut = wgf_font_create("text_test/cut.ttf");
        settle_font(cut);
        expect(wgf_resource_get_status(cut) == WGF_RESOURCE_STATUS_FAILED, "a font cut short fails, without being read past");
        wgf_resource_release(cut);
    }
    loaded = wgf_font_measure(font, "Hello", 16);
    expect(fabsf(loaded.x - one.x) < 0.01f, "the same typeface measures the same");
    expect(fabsf(wgf_font_measure(broken, "Hello", 16).x - one.x) < 0.01f, "a failed font draws as the default");
    expect(wgf_font_create("text_test\\mono.ttf") == font, "one font per file");
    wgf_resource_release(font);

    /* the default holds its reference */
    expect(wgf_font_set_default(font) && wgf_font_get_default() == font, "a default of our own");
    expect(!wgf_font_set_default(12345), "a default that isn't a font is refused");
    wgf_resource_release(font); /* ours */
    expect(wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_READY, "the default keeps it");
    expect(wgf_font_set_default(0) && wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_NONE,
           "back to the built-in font: the last reference gone");
    again = wgf_font_create("text_test/mono.ttf");
    expect(wgf_resource_get_status(again) == WGF_RESOURCE_STATUS_READY, "a font released earlier comes back ready at once");

    /* the text node */
    text = wgf_text_create(again);
    expect(wgf_node_get_type(text) == WGF_NODE_TYPE_TEXT && wgf_text_get_font(text) == again, "a text node");
    expect(wgf_text_create(12345) == 0, "a text node in something that isn't a font is refused");
    expect(strcmp(wgf_text_get_string(text), "") == 0 && wgf_text_get_font_size(text) == 16 &&
               wgf_text_get_color(text) == WGF_COLOR_WHITE && wgf_text_get_wrap_width(text) == 0 &&
               wgf_text_get_halign(text) == WGF_TEXT_HALIGN_LEFT && wgf_text_get_valign(text) == WGF_TEXT_VALIGN_TOP,
           "defaults");
    expect(wgf_text_set_string(text, source), "a string");
    strcpy(source, "changed");
    expect(strcmp(wgf_text_get_string(text), "hello") == 0, "copied, not kept");
    expect(wgf_text_set_font_size(text, 24) && wgf_text_get_font_size(text) == 24, "a font size");
    expect(wgf_text_set_font_size(text, -3) && wgf_text_get_font_size(text) == 16, "0 or less unsets it: 16");
    wgf_text_set_font_size(text, 24);
    expect(wgf_text_set_color(text, WGF_COLOR_RED) && wgf_text_get_color(text) == WGF_COLOR_RED, "a color");
    expect(wgf_text_set_wrap_width(text, 100) && wgf_text_get_wrap_width(text) == 100, "a wrap width");
    expect(!wgf_text_set_wrap_width(text, -1), "a negative wrap width is refused");
    expect(wgf_text_set_align(text, WGF_TEXT_HALIGN_CENTER, WGF_TEXT_VALIGN_BOTTOM) &&
               wgf_text_get_halign(text) == WGF_TEXT_HALIGN_CENTER && wgf_text_get_valign(text) == WGF_TEXT_VALIGN_BOTTOM,
           "an alignment");
    expect(!wgf_text_set_align(text, (wgf_text_halign_t)7, WGF_TEXT_VALIGN_TOP), "an alignment that isn't one");
    expect(!wgf_text_set_font(text, 12345) && wgf_text_set_font(text, 0) && wgf_text_get_font(text) == 0,
           "fonts: refused, then the default");
    expect(wgf_resource_get_status(again) == WGF_RESOURCE_STATUS_READY, "still ours");
    expect(wgf_text_set_font(text, again), "and back");

    /* what is recorded */
    {
        const wgf_handle_t canvas = wgf_canvas_create(), empty = wgf_text_create(0);
        int before, after_text, after_empty;
        wgf_node_set_parent(text, canvas);
        wgf_node_set_parent(empty, canvas);
        wgf_draw_text(0, "outside", 0, 0, 16, WGF_COLOR_WHITE); /* outside a frame */
        begin_frame(64, 64, 1.0f);
        expect(sgl_num_vertices() == 0, "text outside a frame records nothing");
        wgf_draw_text(0, "Hi", 0, 0, 16, WGF_COLOR_WHITE);
        expect(sgl_num_vertices() > 0, "draw_text records glyphs");
        before = sgl_num_vertices();
        wgf_node_set_visible(empty, true);
        wgf_node_set_visible(text, false);
        wgf_canvas_draw(canvas);
        after_empty = sgl_num_vertices();
        wgf_node_set_visible(text, true);
        wgf_canvas_draw(canvas);
        after_text = sgl_num_vertices();
        wgf_gfx_priv_end_frame();
        expect(after_empty == before, "an empty or hidden text node draws nothing");
        expect(after_text > after_empty, "a text node in a canvas draws its string");
        wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
    }
    wgf_resource_release(again);
    expect(wgf_resource_get_status(again) == WGF_RESOURCE_STATUS_NONE, "the text node's reference went with it");
    wgf_resource_release(broken);

    wgf_gfx_priv_stop();
    {
        const wgf_handle_t task = wgf_fs_rmdir("text_test");
        int i;
        for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
        wgf_fs_task_destroy(task);
    }
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
