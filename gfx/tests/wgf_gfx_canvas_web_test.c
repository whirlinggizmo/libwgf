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
#include "wgf_camera2d.h"
#include "wgf_canvas.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_emitter2d.h"
#include "wgf_node.h"
#include "wgf_render.h"
#include "wgf_shape2d.h"
#include "wgf_sprite.h"
#include "wgf_texture.h"

/* Canvases and sprites, pixel by pixel, in a browser (tools/run_in_browser.py):
 * a HUD canvas in screen pixels with a sprite and its child, a turned parent carrying
 * its child round, drawing order, a hidden node, and a second canvas whose 2D camera
 * is zoomed and centered on a point; then sprites kept in order around immediate mode
 * drawing and other canvases; 2D shapes: a rectangle, an outlined one, a circle, and a
 * polygon turned by its node, in order with the sprites; and a 2D particle burst, in
 * canvas units under a canvas's camera. A frame loop, since the web store opens between
 * frames. */

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
static wgf_handle_t task, image, hud, world;

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

/* A sprite of one texel of the 2 by 2 image (column, row), `size` square, its pivot
 * at its top-left corner, under `parent`, at (x, y). */
static wgf_handle_t texel_sprite(wgf_handle_t parent, int column, int row, float size, float x, float y)
{
    const wgf_handle_t sprite = wgf_sprite_create(image);
    wgf_sprite_set_source(sprite, (float)column, (float)row, 1, 1);
    wgf_sprite_set_size(sprite, size, size);
    wgf_sprite_set_pivot(sprite, 0, 0);
    wgf_node_set_position(sprite, x, y, 0);
    wgf_node_set_parent(sprite, parent);
    return sprite;
}

static void build(void)
{
    const float quarter = 1.57079632679f;
    wgf_handle_t a, turner, camera;

    hud = wgf_canvas_create();
    /* a: the whole image, 16 by 16 at the top-left; its child, the green texel, 20 to its right */
    a = wgf_sprite_create(image);
    wgf_sprite_set_size(a, 16, 16);
    wgf_sprite_set_pivot(a, 0, 0);
    wgf_node_set_parent(a, hud);
    texel_sprite(a, 1, 0, 8, 20, 0);
    /* a parent at (48, 16) turned a quarter: its child 8 along x lands 8 down y */
    turner = wgf_node_create();
    wgf_node_set_position(turner, 48, 16, 0);
    wgf_node_set_rotation(turner, 0, 0, quarter);
    wgf_node_set_parent(turner, hud);
    {
        const wgf_handle_t carried = texel_sprite(turner, 1, 1, 4, 8, 0); /* white */
        wgf_sprite_set_pivot(carried, 0.5f, 0.5f);
    }
    /* order: red, then blue over it in the same place */
    texel_sprite(hud, 0, 0, 8, 40, 40);
    texel_sprite(hud, 0, 1, 8, 40, 40);
    /* hidden: nothing of it drawn */
    wgf_node_set_visible(texel_sprite(hud, 1, 1, 8, 4, 40), false);

    /* a world canvas: a camera at (100, 100), zoom 2; a white texel there, 4 square, centered */
    world = wgf_canvas_create();
    camera = wgf_camera2d_create();
    wgf_node_set_position(camera, 100, 100, 0);
    wgf_camera2d_set_zoom(camera, 2);
    wgf_canvas_set_camera(world, camera);
    {
        const wgf_handle_t at = texel_sprite(world, 1, 1, 4, 100, 100);
        wgf_sprite_set_pivot(at, 0.5f, 0.5f);
    }
}

static void draw_and_check(void)
{

    wgf_render_set_clear_color(WGF_COLOR_BLACK);
    begin_frame();
    wgf_canvas_draw(world);
    wgf_canvas_draw(hud);
    wgf_gfx_priv_end_frame();

    expect_pixel(4, 4, wgf_color_make(255, 0, 0, 255), "a sprite: its top-left texel");
    expect_pixel(12, 12, wgf_color_make(255, 255, 255, 255), "a sprite: its bottom-right texel");
    expect_pixel(24, 4, wgf_color_make(0, 255, 0, 255), "its child, placed relative to it");
    expect_pixel(48, 24, wgf_color_make(255, 255, 255, 255), "a turned parent carries its child round");
    expect_pixel(56, 16, wgf_color_make(0, 0, 0, 255), "and not where it would be unturned");
    expect_pixel(44, 44, wgf_color_make(0, 0, 255, 255), "a later sibling draws over an earlier one");
    expect_pixel(8, 44, wgf_color_make(0, 0, 0, 255), "a hidden node draws nothing");
    expect_pixel(32, 32, wgf_color_make(255, 255, 255, 255), "a camera shows its point at the center");
    expect_pixel(34, 34, wgf_color_make(255, 255, 255, 255), "zoomed: 4 units drawn 8 pixels across");
    expect_pixel(37, 32, wgf_color_make(0, 0, 0, 255), "and no wider");
}

/* Order: sprites, immediate mode, and canvases drawn as called, each over what came
 * before, though the two canvases' sprites share a texture. */
static void check_order(void)
{
    const wgf_handle_t first = wgf_canvas_create(), second = wgf_canvas_create();
    texel_sprite(first, 0, 0, 16, 40, 40);  /* red */
    texel_sprite(second, 0, 1, 8, 50, 50);  /* blue */
    begin_frame();
    wgf_canvas_draw(first);
    wgf_draw_rectangle(44, 44, 16, 16, WGF_COLOR_GREEN);
    wgf_canvas_draw(second);
    wgf_gfx_priv_end_frame();
    expect_pixel(42, 42, wgf_color_make(255, 0, 0, 255), "a canvas's sprite");
    expect_pixel(46, 46, WGF_COLOR_GREEN, "a rectangle drawn after it, over it");
    expect_pixel(54, 54, wgf_color_make(0, 0, 255, 255), "the next canvas's sprite over that");
    wgf_node_destroy(first, WGF_NODE_DESTROY_CHILDREN);
    wgf_node_destroy(second, WGF_NODE_DESTROY_CHILDREN);
}

/* 2D shapes, drawn in tree order with the canvas's sprites, and a polygon turned by its node. */
static void check_shapes(void)
{
    const wgf_handle_t canvas = wgf_canvas_create(), box = wgf_shape2d_create(), ring = wgf_shape2d_create();
    const wgf_handle_t dot = wgf_shape2d_create(), wedge = wgf_shape2d_create();
    const float triangle[6] = {0, 0, 10, 0, 0, 10}; /* a right angle at its origin */
    wgf_shape2d_set_rectangle(box, 20, 20); /* from its top-left corner */
    wgf_shape2d_set_color(box, WGF_COLOR_RED);
    wgf_node_set_position(box, 4, 4, 0);
    wgf_node_set_parent(box, canvas);
    texel_sprite(canvas, 0, 1, 8, 8, 8); /* blue, drawn after the box: over it */
    wgf_shape2d_set_rectangle(ring, 20, 20);
    wgf_shape2d_set_outline(ring, 3);
    wgf_shape2d_set_color(ring, WGF_COLOR_GREEN);
    wgf_node_set_position(ring, 36, 4, 0);
    wgf_node_set_parent(ring, canvas);
    wgf_shape2d_set_circle(dot, 8);
    wgf_shape2d_set_color(dot, WGF_COLOR_YELLOW);
    wgf_node_set_position(dot, 16, 48, 0);
    wgf_node_set_parent(dot, canvas);
    wgf_shape2d_set_polygon(wedge, triangle, 6);
    wgf_shape2d_set_color(wedge, WGF_COLOR_RED);
    wgf_node_set_transform(wedge, 50, 50, 0, 0, 0, 3.14159265f, 1, 1, 1); /* a half turn: up and to the left */
    wgf_node_set_parent(wedge, canvas);
    begin_frame();
    wgf_canvas_draw(canvas);
    wgf_gfx_priv_end_frame();
    expect_pixel(6, 20, WGF_COLOR_RED, "a rectangle, filled");
    expect_pixel(10, 10, wgf_color_make(0, 0, 255, 255), "a sprite after it in the tree: over it");
    expect_pixel(36, 14, WGF_COLOR_GREEN, "an outlined rectangle: its band, on its edge");
    expect_pixel(46, 14, WGF_COLOR_BLACK, "and nothing inside it");
    expect_pixel(16, 48, WGF_COLOR_YELLOW, "a circle, about its center");
    expect_pixel(9, 41, WGF_COLOR_BLACK, "and not its box's corner");
    expect_pixel(47, 47, WGF_COLOR_RED, "a polygon turned a half turn by its node");
    expect_pixel(53, 53, WGF_COLOR_BLACK, "and not where it was unturned");
    wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
}

/* A 2D burst under the world canvas's camera (zoomed 2 at (100, 100)): a particle born
 * there shows at the middle of the frame, its 4 units 8 pixels across. */
static void check_particles(void)
{
    const wgf_handle_t burst = wgf_emitter2d_create();
    wgf_emitter2d_set_size(burst, 4, 4);
    wgf_emitter2d_set_color(burst, WGF_COLOR_RED, WGF_COLOR_RED);
    wgf_node_set_position(burst, 104, 96, 0); /* 4 right of and up from the camera's point */
    wgf_node_set_parent(burst, world);
    wgf_emitter2d_burst(burst, 1);
    begin_frame();
    wgf_canvas_draw(world);
    wgf_gfx_priv_end_frame();
    expect_pixel(40, 24, WGF_COLOR_RED, "a particle where its canvas's camera puts it");
    expect_pixel(52, 24, WGF_COLOR_BLACK, "and only there");
    wgf_node_destroy(burst, WGF_NODE_DESTROY_CHILDREN);
}

static void finish_test(void)
{
    wgf_node_destroy(hud, WGF_NODE_DESTROY_CHILDREN);
    wgf_node_destroy(world, WGF_NODE_DESTROY_CHILDREN);
    expect(wgf_resource_get_status(image) == WGF_RESOURCE_STATUS_READY, "ours still holds the texture");
    wgf_resource_release(image);
    expect(wgf_resource_get_status(image) == WGF_RESOURCE_STATUS_NONE, "freed with the last reference");
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void on_frame(void *user)
{
    (void)user;
    if (++frames > 600) {
        printf("FAIL: still waiting at step %d\n", step);
        failures++;
        finish_test();
        return;
    }
    if (step == 0) {
        task = wgf_fs_write("images/rgbw.png", png_2x2, (int)sizeof(png_2x2));
        step++;
    } else if (step == 1 && wgf_fs_task_get_status(task) != WGF_FS_TASK_STATUS_PENDING) {
        wgf_fs_task_destroy(task);
        image = wgf_texture_create("images/rgbw.png");
        wgf_texture_set_sampling(image, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_NEAREST);
        build();
        step++;
    } else if (step == 2 && wgf_resource_get_status(image) != WGF_RESOURCE_STATUS_PENDING) {
        expect(wgf_resource_get_status(image) == WGF_RESOURCE_STATUS_READY, "the PNG loaded");
        draw_and_check();
        check_order();
        check_shapes();
        check_particles();
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
