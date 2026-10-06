#include <stdio.h>
#include <string.h>

#include <GLES3/gl3.h>
#include <emscripten.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_app.h"
#include "wgf_window.h"
#include "wgf.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_color.h"
#include "wgf_canvas.h"
#include "wgf_draw.h"
#include "wgf_node.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_render.h"
#include "wgf_texture.h"

/* Textures, pixel by pixel, in a browser (tools/run_in_browser.py): a PNG loaded on
 * create through core's pipeline, drawn into a WebGL2 canvas scaled up with nearest
 * filtering, a region cut from it, and a texture that failed showing the
 * placeholder checker, a sprite of it turned in a canvas, and a pushed clip. A frame
 * loop, since the
 * web store opens between frames. */

/* A 2 by 2 PNG: red, green; blue, white. */
static const unsigned char png_2x2[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00,
    0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81,
    0x34, 0x18, 0x00, 0x00, 0x49, 0xc8, 0x09, 0xf7, 0xf9, 0xab, 0xb6, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

static int failures;
static int step;
static int frames;
static wgf_fs_task_t task;
static wgf_texture_t image, missing;

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

static void expect_pixel(int x, int y, wgf_color_t color, const char *what)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, 63 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (rgba[0] != wgf_color_get_red(color) || rgba[1] != wgf_color_get_green(color) ||
        rgba[2] != wgf_color_get_blue(color)) {
        printf("FAIL: %s: at %d, %d read %d %d %d, expected %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color));
        failures++;
    }
}

static void finish_test(void)
{
    wgf_resource_release(image);
    wgf_resource_release(missing);
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void draw_and_check(void)
{

    wgf_render_set_clear_color(WGF_COLOR_BLACK);
    begin_frame();
    wgf_draw_texture(image, 0, 0, 32, 32, WGF_COLOR_WHITE);                 /* each texel a 16 by 16 square */
    wgf_draw_texture_region(image, 1, 0, 1, 1, 40, 0, 16, 16, WGF_COLOR_WHITE); /* the green texel alone */
    wgf_draw_texture(image, 40, 20, 8, 8, wgf_color_make(255, 0, 0, 255)); /* tinted red: only red survives */
    wgf_draw_texture(missing, 32, 32, 32, 32, WGF_COLOR_WHITE);             /* the placeholder */
    wgf_gfx_priv_end_frame();

    expect_pixel(8, 8, wgf_color_make(255, 0, 0, 255), "top-left texel: red");
    expect_pixel(24, 8, wgf_color_make(0, 255, 0, 255), "top-right texel: green");
    expect_pixel(8, 24, wgf_color_make(0, 0, 255, 255), "bottom-left texel: blue");
    expect_pixel(24, 24, wgf_color_make(255, 255, 255, 255), "bottom-right texel: white");
    expect_pixel(48, 8, wgf_color_make(0, 255, 0, 255), "a region: the green texel");
    expect_pixel(41, 21, wgf_color_make(255, 0, 0, 255), "a tint multiplies: red over red stays red");
    expect_pixel(46, 26, wgf_color_make(255, 0, 0, 255), "a tint multiplies: red over white is red");
    expect_pixel(34, 34, wgf_color_make(255, 0, 255, 255), "the placeholder's first square: magenta");
    expect_pixel(38, 34, wgf_color_make(20, 0, 20, 255), "and the next: near black");

    {
        /* a sprite of it in a canvas, turned a half turn about its center: the texels the
           other way round; and a pushed clip around a rectangle (its bottom-right quarter) */
        const wgf_node_t canvas = wgf_canvas_create(), sprite = wgf_sprite_create(image);
        wgf_node_set_parent(sprite, canvas);
        wgf_sprite_set_size(sprite, 32, 32);
        wgf_node_set_transform(sprite, 16, 16, 0, 0, 0, 3.14159265f, 1, 1, 1);
        begin_frame();
        wgf_render_push_clip(32, 32, 64, 64);
        wgf_draw_rectangle(0, 0, 64, 64, wgf_color_make(255, 0, 0, 255));
        wgf_render_pop_clip();
        wgf_canvas_draw(canvas);
        wgf_gfx_priv_end_frame();
        expect_pixel(8, 8, wgf_color_make(255, 255, 255, 255), "a sprite turned a half turn: white top-left");
        expect_pixel(24, 24, wgf_color_make(255, 0, 0, 255), "and red bottom-right");
        expect_pixel(48, 48, wgf_color_make(255, 0, 0, 255), "a pushed clip: red inside it");
        expect_pixel(48, 16, wgf_color_make(0, 0, 0, 255), "a pushed clip: nothing outside it");
        wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
    }
}

static void on_frame(void *user)
{
    (void)user;
    frames++;
    if (frames > 600) {
        printf("FAIL: still waiting at step %d\n", step);
        failures++;
        finish_test();
        return;
    }
    if (step == 0) {
        task = wgf_fs_write("images/rgbw.png", png_2x2, (int)sizeof(png_2x2));
        step++;
    } else if (step == 1 && wgf_fs_task_get_status(task) != WGF_FS_TASK_STATUS_PENDING) {
        expect(wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_DONE, "the PNG written");
        wgf_fs_task_destroy(task);
        image = wgf_texture_create("images/rgbw.png");
        missing = wgf_texture_create("images/missing.png");
        expect(wgf_texture_set_sampling(image, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_NEAREST),
               "nearest sampling");
        step++;
    } else if (step == 2 && wgf_resource_get_status(image) != WGF_RESOURCE_STATUS_PENDING &&
               wgf_resource_get_status(missing) != WGF_RESOURCE_STATUS_PENDING) {
        expect(wgf_resource_get_status(image) == WGF_RESOURCE_STATUS_READY, "the PNG loaded");
        expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED, "the missing file failed");
        draw_and_check();
        finish_test();
    }
}

/* The page's canvas, 64 by 64, through app's runtime, which updates core each
 * frame before calling it. */
int main(void)
{
    wgf_window_set_size(64, 64);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
