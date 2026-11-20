#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_platform_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "text/wgf_gfx_font_priv.h"
#include "texture/wgf_gfx_png_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_actor.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_font.h"
#include "wgf_fs.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"
#include "wgf_text.h"
#include "wgf_texture.h"
#include "wgf_time.h"

/* Bitmap fonts on sokol's dummy backend (a headless build): a BMFont in its text and binary
 * forms with its page, and an image of glyphs on magenta, READY with their glyphs' places,
 * measured with their advances, kerning, and spacing at their own size and others; drawn
 * (what is recorded) in 2D, by a text actor, and in 3D; their files refused when they
 * don't read as they say. Pixels: wgf_gfx_font_bitmap_window_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

static void begin_frame(int width, int height, float dpi_scale)
{
    wgf_platform_priv_set_size(width, height);
    wgf_platform_priv_headless_set_dpi_scale(dpi_scale);
    wgf_gfx_priv_begin_frame();
}

static void settle(wgf_handle_t resource)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(resource) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0)
        wgf_core_priv_update();
}

static void write_file(const char *path, const void *data, int size)
{
    const wgf_handle_t task = wgf_fs_write(path, (const unsigned char *)data, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

static void write_text(const char *path, const char *text)
{
    write_file(path, text, (int)strlen(text));
}

/* A 32 by 16 page, each glyph's rectangle opaque white. */
#define PAGE_W 32
#define PAGE_H 16
static void write_page(const char *path)
{
    static const int rects[][4] = {{0, 0, 5, 7}, {6, 0, 5, 7}, {12, 0, 5, 7}, {18, 0, 3, 9}};
    unsigned char rgba[PAGE_W * PAGE_H * 4];
    unsigned char *png;
    int size = 0, r, x, y;
    memset(rgba, 0, sizeof(rgba));
    for (r = 0; r < 4; r++) {
        for (y = rects[r][1]; y < rects[r][1] + rects[r][3]; y++) {
            for (x = rects[r][0]; x < rects[r][0] + rects[r][2]; x++) memset(rgba + (y * PAGE_W + x) * 4, 255, 4);
        }
    }
    png = wgf_gfx_priv_png_encode(rgba, PAGE_W, PAGE_H, &size);
    write_file(path, png, size);
    free(png);
}

/* A line 10 high, its baseline 8 down: A, V, and ? 5 by 7 a pixel below the line's top,
 * advancing 6; a space advancing 3; é (two bytes of UTF-8) 3 by 9 from a pixel above it,
 * advancing 4; A then V kerned 2 closer. */
static const char FNT[] = "info face=\"Test\" size=10 bold=0 italic=0 charset=\"\" unicode=1 padding=0,0,0,0\r\n"
                          "common lineHeight=10 base=8 scaleW=32 scaleH=16 pages=1 packed=0\r\n"
                          "page id=0 file=\"page.png\"\r\n"
                          "chars count=5\n"
                          "char id=65 x=0 y=0 width=5 height=7 xoffset=0 yoffset=1 xadvance=6 page=0 chnl=15\n"
                          "char id=86 x=6 y=0 width=5 height=7 xoffset=0 yoffset=1 xadvance=6 page=0 chnl=15\n"
                          "char id=63 x=12 y=0 width=5 height=7 xoffset=0 yoffset=1 xadvance=6 page=0 chnl=15\n"
                          "char id=32 x=0 y=0 width=0 height=0 xoffset=0 yoffset=0 xadvance=3 page=0 chnl=15\n"
                          "char id=233 x=18 y=0 width=3 height=9 xoffset=0 yoffset=-1 xadvance=4 page=0 chnl=15\n"
                          "kernings count=1\n"
                          "kerning first=65 second=86 amount=-2\n";

static void put16(unsigned char *p, int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

/* The same font in BMFont's binary form; its size, the last block cut by `cut` bytes. */
static int binary_fnt(unsigned char *out, int cut)
{
    static const int chars[][9] = {{65, 0, 0, 5, 7, 0, 1, 6, 0},
                                   {86, 6, 0, 5, 7, 0, 1, 6, 0},
                                   {63, 12, 0, 5, 7, 0, 1, 6, 0},
                                   {32, 0, 0, 0, 0, 0, 0, 3, 0},
                                   {233, 18, 0, 3, 9, 0, -1, 4, 0}};
    unsigned char *p = out;
    int i;
    memcpy(p, "BMF\3", 4);
    p += 4;
    *p++ = 1; /* info: its size, its flags, a name */
    put32(p, 14 + 5);
    p += 4;
    memset(p, 0, 14);
    put16(p, 10);
    memcpy(p + 14, "Test", 5);
    p += 14 + 5;
    *p++ = 2; /* common */
    put32(p, 15);
    p += 4;
    memset(p, 0, 15);
    put16(p, 10);
    put16(p + 2, 8);
    put16(p + 4, 32);
    put16(p + 6, 16);
    put16(p + 8, 1);
    p += 15;
    *p++ = 3; /* pages */
    put32(p, 9);
    p += 4;
    memcpy(p, "page.png", 9);
    p += 9;
    *p++ = 4; /* chars */
    put32(p, 5 * 20);
    p += 4;
    for (i = 0; i < 5; i++) {
        put32(p, (unsigned)chars[i][0]);
        put16(p + 4, chars[i][1]);
        put16(p + 6, chars[i][2]);
        put16(p + 8, chars[i][3]);
        put16(p + 10, chars[i][4]);
        put16(p + 12, chars[i][5] & 0xFFFF);
        put16(p + 14, chars[i][6] & 0xFFFF);
        put16(p + 16, chars[i][7] & 0xFFFF);
        p[18] = (unsigned char)chars[i][8];
        p[19] = 15;
        p += 20;
    }
    *p++ = 5; /* kerning pairs */
    put32(p, 10);
    p += 4;
    put32(p, 65);
    put32(p + 4, 86);
    put16(p + 8, -2 & 0xFFFF);
    p += 10;
    return (int)(p - out) - cut;
}

/* An image of glyphs: a magenta border and gaps a pixel wide around a space 3 wide
 * (transparent) and a ! 2 wide (red), 5 high; `bordered` false for none (all white). */
static void write_image_font(const char *path, int bordered)
{
    enum { W = 8, H = 7 };
    unsigned char rgba[W * H * 4];
    unsigned char *png;
    int size = 0, x, y;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            unsigned char *p = rgba + (y * W + x) * 4;
            const int inside = y >= 1 && y <= 5 && ((x >= 1 && x <= 3) || (x >= 5 && x <= 6));
            p[0] = 255;
            p[1] = bordered && !inside ? 0 : 255;
            p[2] = 255;
            p[3] = 255;
            if (bordered && inside) {
                const int bang = x >= 5;
                p[0] = bang ? 255 : 0;
                p[1] = 0;
                p[2] = 0;
                p[3] = bang ? 255 : 0;
            }
        }
    }
    png = wgf_gfx_priv_png_encode(rgba, W, H, &size);
    write_file(path, png, size);
    free(png);
}

/* What a malformed file comes to: FAILED, and the default font in its place. */
static void expect_refused(const char *path, const char *text, const char *what)
{
    wgf_font_t font;
    char message[200];
    if (text != NULL) write_text(path, text);
    font = wgf_font_create(path);
    settle(font);
    snprintf(message, sizeof(message), "refused, FAILED: %s", what);
    expect(wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_FAILED && wgf_font_get_size(font) == 0, message);
    wgf_resource_release(font);
}

static int listed;
static void count_listed(const char *uri, const char *fallback_uri, bool required, void *context)
{
    (void)fallback_uri;
    (void)context;
    if (strcmp(uri, "page.png") == 0 && required) listed++;
}

int main(void)
{
    wgf_font_t font, binary, image;
    wgf_vec2_t m;
    unsigned char bytes[512];

    wgf_core_priv_init();
    write_page("bitmap_test/page.png");
    write_text("bitmap_test/test.fnt", FNT);
    write_file("bitmap_test/binary.fnt", bytes, binary_fnt(bytes, 0));
    write_image_font("bitmap_test/glyphs.png", 1);
    expect(wgf_gfx_priv_start(), "setup");
    begin_frame(64, 64, 1.0f);
    wgf_gfx_priv_end_frame();

    /* the .fnt's lister names its page, for the web */
    {
        const wgf_core_priv_load_lister_fn list = wgf_core_priv_load_lister("bitmap_test/test.FNT");
        expect(list != NULL, "a .fnt has a lister");
        if (list != NULL) list((const unsigned char *)FNT, (int)strlen(FNT), count_listed, NULL);
        expect(listed == 1, "it names the page, required");
    }

    /* the text form */
    font = wgf_font_create("bitmap_test/test.fnt");
    expect(wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_PENDING, "pending at once");
    expect(wgf_font_get_filter(font) == WGF_TEXTURE_FILTER_NEAREST, "nearest unset, while pending too");
    {
        const wgf_vec2_t pending = wgf_font_measure(font, "AV", 10), builtin = wgf_font_measure(0, "AV", 10);
        expect(near(pending.x, builtin.x), "pending, it measures as the default font");
    }
    settle(font);
    expect(wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_READY, "a BMFont loads");
    expect(wgf_font_get_size(font) == 10, "its size: its line's height");
    expect(wgf_font_get_size(0) == 0 && wgf_font_get_size(12345) == 0, "the built-in font, or what isn't a font: 0");

    /* its glyphs where the file puts them */
    {
        float quads[8 * 4], ascender = 0, descender = 0, line = 0;
        const char *text = "AV";
        int n;
        expect(wgf_gfx_priv_font_vertical_metrics(font, 10, &ascender, &descender, &line) && ascender == 8 &&
                   descender == 2 && line == 10,
               "its metrics: the baseline 8 down a line 10 high");
        expect(wgf_gfx_priv_font_page_count(font) == 1 && wgf_gfx_priv_font_page(font, 0) != 0 &&
                   wgf_texture_get_width(wgf_gfx_priv_font_page(font, 0)) == PAGE_W &&
                   wgf_texture_get_filter(wgf_gfx_priv_font_page(font, 0)) == WGF_TEXTURE_FILTER_NEAREST,
               "one page, a texture of its own, sampled nearest");
        expect(wgf_gfx_priv_font_page_count(0) == 1 && wgf_gfx_priv_font_page(0, 0) == 0,
               "a TrueType font's glyphs are on the atlas");
        n = wgf_gfx_priv_font_quads(font, text, text + 2, 10, 100, 50, 0, quads, 4);
        expect(n == 2, "a quad a glyph");
        expect(near(quads[0], 100) && near(quads[1], 43) && near(quads[2], 105) && near(quads[3], 50),
               "A: at the pen, a pixel below the line's top (the baseline 8 below it)");
        expect(near(quads[4], 0) && near(quads[5], 0) && near(quads[6], 5.0f / PAGE_W) && near(quads[7], 7.0f / PAGE_H),
               "A: its rectangle on the page");
        expect(near(quads[8], 104) && near(quads[10], 109) && near(quads[12], 6.0f / PAGE_W),
               "V: after A's advance, kerned 2 closer");
        n = wgf_gfx_priv_font_quads(font, text, text + 2, 20, 0, 0, 0, quads, 4);
        expect(n == 2 && near(quads[0], 0) && near(quads[1], -14) && near(quads[2], 10) && near(quads[8], 8),
               "at twice its size, twice as far");
        n = wgf_gfx_priv_font_quads(font, text, text + 2, 10, 0, 0, 1, quads, 4);
        expect(n == 0, "nothing on a page it hasn't");
        {
            const char *accented = "\xC3\xA9 ";
            n = wgf_gfx_priv_font_quads(font, accented, accented + 3, 10, 0, 8, -1, quads, 4);
        }
        expect(n == 1 && near(quads[1], -1) && near(quads[3], 8) && near(quads[2], 3),
               "UTF-8: é above the line; a space has no quad");
    }

    /* measured: advances, kerning, spacing */
    m = wgf_font_measure(font, "A", 10);
    expect(near(m.x, 6) && near(m.y, 10), "A: its advance, a line");
    expect(near(wgf_font_measure(font, "AV", 10).x, 10), "A then V: kerned, 6 - 2 + 6");
    expect(near(wgf_font_measure(font, "VA", 10).x, 12), "V then A: no pair");
    expect(near(wgf_font_measure(font, "A A", 10).x, 15), "a space's advance");
    expect(near(wgf_font_measure(font, "Z", 10).x, 6), "a character it lacks: ?");
    expect(near(wgf_font_measure(font, "\xC3\xA9", 10).x, 4), "a character past ASCII");
    expect(near(wgf_font_measure(font, "AV", 30).x, 30) && near(wgf_font_measure(font, "AV", 30).y, 30),
           "three times its size: three times as wide and high");
    expect(near(wgf_font_measure(font, "AV", 0).x, 16), "a size of 0 or less: 16");
    m = wgf_font_measure(font, "AV\nAVA", 10);
    expect(near(m.x, 16) && near(m.y, 20), "two lines: the widest, two lines high");
    wgf_draw_set_text_spacing(1, 3);
    m = wgf_font_measure(font, "AVA\nA", 10);
    expect(near(m.x, 6 - 2 + 1 + 6 + 1 + 6) && near(m.y, 26), "letter spacing between glyphs, line spacing a line");
    wgf_draw_set_text_spacing(0, 0);
    begin_frame(64, 64, 2.0f);
    expect(near(wgf_font_measure(font, "AV", 10).x, 10), "on a dense display: the same logical pixels");
    wgf_gfx_priv_end_frame();
    begin_frame(64, 64, 1.0f);
    wgf_gfx_priv_end_frame();
    {
        float left, top, width, height;
        expect(wgf_gfx_priv_font_block_bounds(font, "AV AV", 10, 12, WGF_TEXT_HALIGN_CENTER, WGF_TEXT_VALIGN_TOP,
                                              &left, &top, &width, &height) &&
                   near(width, 12) && near(height, 20) && near(left, -6),
               "wrapped at a width: two lines");
    }

    /* the filter */
    expect(wgf_font_set_filter(font, WGF_TEXTURE_FILTER_LINEAR) &&
               wgf_font_get_filter(font) == WGF_TEXTURE_FILTER_LINEAR &&
               wgf_texture_get_filter(wgf_gfx_priv_font_page(font, 0)) == WGF_TEXTURE_FILTER_LINEAR,
           "linear, its page with it");
    expect(!wgf_font_set_filter(font, (wgf_texture_filter_t)5) && !wgf_font_set_filter(0, WGF_TEXTURE_FILTER_LINEAR) &&
               !wgf_font_set_filter(12345, WGF_TEXTURE_FILTER_LINEAR),
           "a filter that isn't one, font 0, or what isn't a font: refused");
    wgf_font_set_filter(font, WGF_TEXTURE_FILTER_NEAREST);

    /* the binary form reads the same */
    binary = wgf_font_create("bitmap_test/binary.fnt");
    settle(binary);
    expect(wgf_resource_get_status(binary) == WGF_RESOURCE_STATUS_READY && wgf_font_get_size(binary) == 10,
           "a binary BMFont loads");
    expect(near(wgf_font_measure(binary, "AV", 10).x, 10) && near(wgf_font_measure(binary, "\xC3\xA9", 10).x, 4),
           "its advances and kerning");

    /* an image of glyphs */
    image = wgf_font_create("bitmap_test/glyphs.png");
    settle(image);
    expect(wgf_resource_get_status(image) == WGF_RESOURCE_STATUS_READY && wgf_font_get_size(image) == 5,
           "an image of glyphs loads: its size its glyphs' height");
    expect(near(wgf_font_measure(image, " !", 5).x, 5) && near(wgf_font_measure(image, "!!", 10).x, 8),
           "each glyph as wide as its cell, from the space");
    expect(near(wgf_font_measure(image, "!?", 5).x, 2), "what it lacks, with no ?, takes no room");

    /* drawn: what is recorded */
    {
        const wgf_actor_t stage = wgf_stage2d_create(), label = wgf_text_create(font);
        int drawn, actor;
        wgf_text_set_string(label, "AV A");
        wgf_actor_set_parent(label, stage);
        begin_frame(64, 64, 1.0f);
        wgf_draw_text(font, "AV A", 0, 0, 10, WGF_COLOR_WHITE);
        drawn = sgl_num_vertices();
        wgf_stage2d_draw(stage);
        actor = sgl_num_vertices() - drawn;
        wgf_draw_text_aligned(image, "!", 32, 32, 10, WGF_COLOR_WHITE, WGF_TEXT_HALIGN_CENTER, WGF_TEXT_VALIGN_MIDDLE);
        expect(sgl_num_vertices() - drawn - actor == 6, "an image font's glyph: a quad");
        wgf_gfx_priv_end_frame();
        expect(drawn == 3 * 6, "draw_text: a quad a glyph, none for a space");
        expect(actor == 3 * 6, "a text actor in a bitmap font draws its glyphs");
        wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    }
    {
        const wgf_actor_t world = wgf_stage3d_create(), camera = wgf_camera3d_create(), sign = wgf_text_create(font);
        int drawn, direct;
        wgf_actor_set_position(camera, 0, 0, 20);
        wgf_actor_set_parent(camera, world);
        wgf_stage3d_set_camera(world, camera);
        wgf_actor_set_parent(sign, world);
        wgf_text_set_string(sign, "AVA");
        begin_frame(320, 240, 1.0f);
        wgf_stage3d_draw(world);
        drawn = sgl_num_vertices();
        wgf_draw_begin_3d(camera);
        wgf_draw_text_3d(font, "AV", 0, 0, 0, 1, WGF_COLOR_WHITE);
        wgf_draw_end_3d();
        direct = sgl_num_vertices() - drawn;
        wgf_gfx_priv_end_frame();
        expect(drawn == 3 * 6, "a text actor on a 3D stage in a bitmap font: a quad a glyph");
        expect(direct == 2 * 6, "draw_text_3d in a bitmap font");
        wgf_actor_destroy(world, WGF_ACTOR_DESTROY_CHILDREN);
    }

    /* let go of: its page with it */
    {
        const wgf_texture_t page = wgf_gfx_priv_font_page(font, 0);
        wgf_resource_release(font);
        expect(wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_NONE &&
                   wgf_resource_get_status(page) == WGF_RESOURCE_STATUS_NONE,
               "released: the font and its page");
    }

    /* malformed files, refused */
    expect_refused("bitmap_test/xml.fnt", "<?xml version=\"1.0\"?>\n<font><common lineHeight=\"10\"/></font>\n",
                   "BMFont's XML form");
    expect_refused("bitmap_test/packed.fnt",
                   "common lineHeight=10 base=8 pages=1 packed=1\npage id=0 file=\"page.png\"\n"
                   "char id=65 x=0 y=0 width=5 height=7 xadvance=6 page=0\n",
                   "glyphs packed into channels");
    expect_refused("bitmap_test/no_common.fnt", "page id=0 file=\"page.png\"\nchar id=65 width=5 height=7\n",
                   "no common line");
    expect_refused("bitmap_test/height.fnt",
                   "common lineHeight=0 base=0 pages=1\npage id=0 file=\"page.png\"\nchar id=65 xadvance=6\n",
                   "a line 0 high");
    expect_refused("bitmap_test/page_id.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=1 file=\"page.png\"\nchar id=65 xadvance=6\n",
                   "a page past its count");
    expect_refused("bitmap_test/unnamed.fnt", "common lineHeight=10 base=8 pages=2\npage id=0 file=\"page.png\"\n"
                                              "char id=65 xadvance=6\n",
                   "a page not named");
    expect_refused("bitmap_test/char_page.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=0 file=\"page.png\"\nchar id=65 xadvance=6 page=3\n",
                   "a character on a page it hasn't");
    expect_refused("bitmap_test/twice.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=0 file=\"page.png\"\nchar id=65 xadvance=6\n"
                   "char id=65 xadvance=7\n",
                   "a character twice");
    expect_refused("bitmap_test/kern_twice.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=0 file=\"page.png\"\nchar id=65 xadvance=6\n"
                   "kerning first=65 second=65 amount=-1\nkerning first=65 second=65 amount=1\n",
                   "a kerning pair twice");
    expect_refused("bitmap_test/none.fnt", "common lineHeight=10 base=8 pages=1\npage id=0 file=\"page.png\"\n",
                   "no glyphs");
    expect_refused("bitmap_test/number.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=0 file=\"page.png\"\nchar id=65 xadvance=six\n",
                   "a number that isn't");
    expect_refused("bitmap_test/huge.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=0 file=\"page.png\"\nchar id=65 x=99999999999\n",
                   "a number too big");
    expect_refused("bitmap_test/codepoint.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=0 file=\"page.png\"\nchar id=1114112 xadvance=6\n",
                   "a character past Unicode");
    expect_refused("bitmap_test/quote.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=0 file=\"page.png\nchar id=65 xadvance=6\n",
                   "a quote never closed");
    expect_refused("bitmap_test/missing.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=0 file=\"gone.png\"\nchar id=65 xadvance=6\n",
                   "its page missing");
    expect_refused("bitmap_test/climb.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=0 file=\"../../../page.png\"\nchar id=65 xadvance=6\n",
                   "a page above the files' root");
    expect_refused("bitmap_test/absolute.fnt",
                   "common lineHeight=10 base=8 pages=1\npage id=0 file=\"/etc/page.png\"\nchar id=65 xadvance=6\n",
                   "a page by an absolute path");
    expect_refused("bitmap_test/empty.fnt", "", "an empty file");
    write_file("bitmap_test/cut.fnt", bytes, binary_fnt(bytes, 3));
    expect_refused("bitmap_test/cut.fnt", NULL, "a binary BMFont cut short");
    {
        const int size = binary_fnt(bytes, 0);
        bytes[3] = 2;
        write_file("bitmap_test/v2.fnt", bytes, size);
    }
    expect_refused("bitmap_test/v2.fnt", NULL, "a binary BMFont of another version");
    write_image_font("bitmap_test/plain.png", 0);
    expect_refused("bitmap_test/plain.png", NULL, "an image with no key border");

    wgf_resource_release(binary);
    wgf_resource_release(image);
    wgf_gfx_priv_stop();
    {
        const wgf_handle_t task = wgf_fs_rmdir("bitmap_test");
        int i;
        for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
        wgf_fs_task_destroy(task);
    }
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_gfx_font_bitmap_test: ok\n");
    return failures == 0 ? 0 : 1;
}
