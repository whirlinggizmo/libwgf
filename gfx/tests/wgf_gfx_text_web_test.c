#include <stdio.h>
#include <string.h>

#include <GLES3/gl3.h>
#include <emscripten.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_window.h"
#include "wgf.h"
#include "wgf_handle.h"
#include "wgf_canvas.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_node.h"
#include "wgf_render.h"
#include "wgf_text.h"

/* Text, in a browser (tools/run_in_browser.py), in the built-in font on a 64 by 64
 * WebGL2 canvas: where glyphs light pixels and where they don't, a centered and a
 * right-aligned block landing on the right sides, wrapping making more lines, and a
 * shape drawn after text keeping its exact color. Glyph shapes aren't compared:
 * only where ink is. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* A frame of the test's own: the runtime's, under way, is ended first (it drew
 * nothing), and the test reads its pixels as soon as it ends, before the browser
 * shows them. */
static void begin_frame(void)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
}

/* Lit pixels in the rectangle from (x0, y0) to (x1, y1), excluded, top-left
 * origin: any channel above 64 on a black frame. */
static int lit(int x0, int y0, int x1, int y1)
{
    static unsigned char pixels[64 * 64 * 4];
    int x, y, count = 0;
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            const unsigned char *p = &pixels[((63 - y) * 64 + x) * 4];
            if (p[0] > 64 || p[1] > 64 || p[2] > 64) count++;
        }
    }
    return count;
}

static void begin(void)
{
    wgf_render_set_clear_color(WGF_COLOR_BLACK);
    begin_frame();
}

static void run_test(void)
{
    wgf_handle_t canvas, centered, right, wrapped;
    int one_line, three_lines;


    /* immediate text at the top-left, and a shape after it */
    begin();
    wgf_draw_text(0, "MMM", 2, 2, 16, WGF_COLOR_WHITE);
    wgf_draw_rectangle(48, 48, 8, 8, wgf_color_make(10, 200, 30, 255));
    wgf_gfx_priv_end_frame();
    expect(lit(2, 2, 40, 22) > 40, "draw_text lights pixels where it is");
    expect(lit(0, 26, 44, 64) == 0, "and none below its line");
    {
        unsigned char rgba[4];
        glReadPixels(52, 63 - 52, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        expect(rgba[0] == 10 && rgba[1] == 200 && rgba[2] == 30, "a shape drawn after text keeps its exact color");
    }

    /* text nodes: centered on x 32, then right-aligned to x 60 */
    canvas = wgf_canvas_create();
    centered = wgf_text_create(0);
    wgf_text_set_string(centered, "MMMM");
    wgf_text_set_align(centered, WGF_TEXT_HALIGN_CENTER, WGF_TEXT_VALIGN_MIDDLE);
    wgf_node_set_position(centered, 32, 32, 0);
    wgf_node_set_parent(centered, canvas);
    begin();
    wgf_canvas_draw(canvas);
    wgf_gfx_priv_end_frame();
    {
        const int left = lit(0, 20, 32, 44), right_half = lit(32, 20, 64, 44);
        expect(left > 20 && right_half > 20, "centered: ink on both sides of its position");
        expect(left * 2 > right_half && right_half * 2 > left, "and about evenly");
        expect(lit(0, 0, 64, 18) == 0 && lit(0, 46, 64, 64) == 0, "middle: about its position, nothing far above or below");
    }
    wgf_node_set_visible(centered, false);
    right = wgf_text_create(0);
    wgf_text_set_string(right, "MM");
    wgf_text_set_align(right, WGF_TEXT_HALIGN_RIGHT, WGF_TEXT_VALIGN_TOP);
    wgf_node_set_position(right, 60, 2, 0);
    wgf_node_set_parent(right, canvas);
    begin();
    wgf_canvas_draw(canvas);
    wgf_gfx_priv_end_frame();
    expect(lit(40, 2, 61, 22) > 20, "right-aligned: ink just left of its position");
    expect(lit(0, 0, 36, 64) == 0 && lit(62, 0, 64, 64) == 0, "and none far left of it, or right of it");

    /* wrapping: the same string, then wrapped narrow, makes more lines */
    wgf_node_set_visible(right, false);
    wrapped = wgf_text_create(0);
    wgf_text_set_string(wrapped, "MM MM MM");
    wgf_node_set_position(wrapped, 2, 2, 0);
    wgf_node_set_parent(wrapped, canvas);
    begin();
    wgf_canvas_draw(canvas);
    wgf_gfx_priv_end_frame();
    one_line = lit(0, 22, 64, 64);
    wgf_text_set_wrap_width(wrapped, 24);
    begin();
    wgf_canvas_draw(canvas);
    wgf_gfx_priv_end_frame();
    three_lines = lit(0, 22, 64, 64);
    expect(one_line == 0, "unwrapped: one line, nothing below it");
    expect(three_lines > 40, "wrapped: more lines below the first");

    wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void on_frame(void *user)
{
    static int frames;
    (void)user;
    if (++frames == 2) run_test(); /* exits */
}

/* The page's canvas, 64 by 64, through app's runtime: the test runs in a frame. */
int main(void)
{
    wgf_window_set_size(64, 64);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
