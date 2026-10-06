#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera2d.h"
#include "wgf_canvas.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_mouse.h"
#include "wgf_node.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* A scrolling 2D tile map: sprites in a canvas, seen through a 2D camera.
 *   - one sprite sheet (textures/tiles.png), one sprite a cell, cut out with
 *     wgf_sprite_set_source
 *   - trees and flags are 16x32 in the sheet, drawn 1x2 world units, and stand on
 *     their cell because their pivot is the bottom edge (pivot 0.5, 1)
 *   - drag or use the arrow keys to scroll, the wheel to zoom (the world units seen
 *     top to bottom)
 *   - coins react to the pointer: hovering lights them up, clicking collects them
 *
 *   Esc   quit, where quitting means anything
 *
 * libwgt's gfx-tilemap (wgrender's tilemap) done 1:1, so the two compare in the size
 * table: the same window, map, controls, and text. Where it differs, and why:
 *   - "libwgt" reads "libwgf" in the window's title and the heading: the library's name.
 *   - A canvas and a 2D camera, where libwgt has 3D sprites (WGT_SPRITE_FACING_FREE) in
 *     a scene under an orthographic camera: libwgf is 2D only. The world is the same,
 *     y up, in world units: a cell at (x, y) is at (x, -y) in the canvas, and the
 *     camera's zoom is the window's height over the units seen (a 2D camera is
 *     orthographic), so the picture is the same.
 *   - Ground and props are drawn in their order, the ground first, where libwgt orders
 *     them by depth and draws them OPAQUE and MASK: libwgf's sprites have no alpha
 *     modes, and a canvas needs none, drawing in order.
 *   - The coins' hover and click are the example's own test of the pointer against each
 *     coin's disc, where libwgt's pointer picking (wgt_pointer.h) finds them by their
 *     texels: libwgf has no picking. A click is a press and a release on the same coin,
 *     as wgt_pointer_is_clicked's is. */

#define TILES_PATH "textures/tiles.png"

enum { WORLD_W = 24, WORLD_H = 16, MAX_PROPS = 64, COIN_COUNT = 8 };

/* Cells of the sheet, in texture pixels: x, y, width, height (each cell has a gutter
 * around it that repeats its edge, so sampling never reaches a neighbour). */
static const float GRASS[4] = {2, 2, 16, 16};
static const float SAND[4] = {22, 2, 16, 16};
static const float WATER[4] = {42, 2, 16, 16};
static const float STONE[4] = {62, 2, 16, 16};
static const float TREE[4] = {2, 22, 16, 32};
static const float FLAG[4] = {22, 22, 16, 32};
static const float COIN[4] = {42, 22, 16, 16};
static const float ROCK[4] = {62, 22, 16, 16};

static struct {
    wgf_node_t canvas, camera;
    wgf_texture_t texture;
    wgf_color_t shade, text, dim, highlight;
    wgf_node_t props[MAX_PROPS];
    bool is_coin[MAX_PROPS];
    float prop_x[MAX_PROPS], prop_y[MAX_PROPS], prop_size[MAX_PROPS]; /* a coin's bottom middle, and size */
    int prop_count;
    int collected;
    int pressed; /* the coin the left button went down on, or -1 */
    float center_x, center_y; /* what the camera looks at, in world units */
    float zoom;               /* world units seen top to bottom */
} g;

static void place_camera(void)
{
    const float height = (float)wgf_window_get_height();
    wgf_node_set_position(g.camera, g.center_x, -g.center_y, 0.0f); /* the canvas's y is down */
    if (height > 0.0f) wgf_camera2d_set_zoom(g.camera, height / g.zoom);
}

/* One cell of the sheet in the world, at (x, y) with y up; the ones added later draw
 * over the ones before. */
static wgf_node_t add_sprite(const float cell[4], float x, float y, float width, float height, float pivot_y)
{
    const wgf_node_t sprite = wgf_sprite_create(g.texture);
    wgf_sprite_set_source(sprite, cell[0], cell[1], cell[2], cell[3]);
    wgf_sprite_set_size(sprite, width, height);
    wgf_sprite_set_pivot(sprite, 0.5f, pivot_y);
    wgf_node_set_position(sprite, x, -y, 0.0f);
    wgf_node_set_parent(sprite, g.canvas);
    return sprite;
}

static void add_prop(const float cell[4], float x, float y, float width, float height, bool coin)
{
    if (g.prop_count >= MAX_PROPS) return;
    /* The pivot is the bottom edge, so a prop stands on its cell whatever its height. */
    g.props[g.prop_count] = add_sprite(cell, x, y - 0.5f, width, height, 1.0f);
    g.is_coin[g.prop_count] = coin;
    g.prop_x[g.prop_count] = x;
    g.prop_y[g.prop_count] = y - 0.5f;
    g.prop_size[g.prop_count] = width;
    g.prop_count++;
}

/* A small hand-made map: water along the bottom, a sand shore, stone paths. */
static const float *tile_at(int x, int y)
{
    if (y < 2) return WATER;
    if (y < 3) return SAND;
    if (x == 8 || y == 9) return STONE;
    return GRASS;
}

static void init(void *user)
{
    int x, y, i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(24, 28, 38, 255));
    g.shade = wgf_color_make(18, 20, 28, 190);
    g.text = wgf_color_make(235, 238, 245, 255);
    g.dim = wgf_color_make(140, 146, 158, 255);
    g.highlight = wgf_color_make(255, 230, 140, 255);
    g.pressed = -1;

    g.canvas = wgf_canvas_create();
    g.camera = wgf_camera2d_create();
    wgf_canvas_set_camera(g.canvas, g.camera);
    g.center_x = WORLD_W * 0.5f;
    g.center_y = WORLD_H * 0.5f;
    g.zoom = 12.0f;
    place_camera();

    g.texture = wgf_texture_create(TILES_PATH);
    /* Pixel art: keep the texels crisp when zoomed in. */
    wgf_texture_set_sampling(g.texture, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_NEAREST);
    for (y = 0; y < WORLD_H; y++) {
        for (x = 0; x < WORLD_W; x++) {
            /* A hair over one unit: neighbouring quads are drawn separately, so edges
             * that land exactly on a pixel boundary would let the background through
             * as a hairline seam. */
            add_sprite(tile_at(x, y), (float)x + 0.5f, (float)y + 0.5f, 1.01f, 1.01f, 0.5f);
        }
    }
    for (i = 0; i < 7; i++) { /* trees and flags: 16x32 cells drawn 1x2 */
        add_prop(TREE, 2.5f + (float)i * 3.0f, 12.5f - (float)(i % 3), 1.0f, 2.0f, false);
    }
    add_prop(FLAG, 8.5f, 9.5f, 1.0f, 2.0f, false);
    for (i = 0; i < 6; i++) add_prop(ROCK, 4.5f + (float)i * 3.5f, 4.5f + (float)(i % 2) * 2.0f, 1.0f, 1.0f, false);
    for (i = 0; i < COIN_COUNT; i++) {
        add_prop(COIN, 3.5f + (float)i * 2.5f, 7.5f + (float)(i % 3), 0.8f, 0.8f, true);
    }
    wgf_resource_release(g.texture); /* the sprites hold their own references */
}

/* The coin under the pointer at world (x, y), the topmost (the last added) first; -1
 * for none. A coin is hit inside its disc, as its see-through corners let a click
 * through. */
static int coin_at(float x, float y)
{
    int i;
    for (i = g.prop_count - 1; i >= 0; i--) {
        const float r = g.prop_size[i] * 0.5f;
        const float dx = x - g.prop_x[i], dy = y - (g.prop_y[i] + r);
        if (g.is_coin[i] && wgf_node_is_visible(g.props[i]) && dx * dx + dy * dy <= r * r) return i;
    }
    return -1;
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    const float width = (float)wgf_window_get_width();
    const float height = (float)wgf_window_get_height();
    const float units_per_pixel = height > 0.0f ? g.zoom / height : 0.0f;
    char line[160];
    int hovered, i;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    /* Scroll: drag, or the arrow keys. */
    if (wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT)) {
        const wgf_vec2_t delta = wgf_mouse_get_delta();
        g.center_x -= delta.x * units_per_pixel;
        g.center_y += delta.y * units_per_pixel; /* screen y is down, world y is up */
    }
    {
        const float speed = g.zoom * 0.6f * dt;
        if (wgf_keyboard_is_down(WGF_KEY_LEFT)) g.center_x -= speed;
        if (wgf_keyboard_is_down(WGF_KEY_RIGHT)) g.center_x += speed;
        if (wgf_keyboard_is_down(WGF_KEY_DOWN)) g.center_y -= speed;
        if (wgf_keyboard_is_down(WGF_KEY_UP)) g.center_y += speed;
    }
    if (wgf_mouse_get_wheel().y != 0.0f) { /* zoom: fewer world units seen = closer */
        g.zoom -= wgf_mouse_get_wheel().y * 1.5f;
        g.zoom = g.zoom < 4.0f ? 4.0f : g.zoom > 32.0f ? 32.0f : g.zoom;
    }
    g.center_x = g.center_x < 0.0f ? 0.0f : g.center_x > (float)WORLD_W ? (float)WORLD_W : g.center_x;
    g.center_y = g.center_y < 0.0f ? 0.0f : g.center_y > (float)WORLD_H ? (float)WORLD_H : g.center_y;
    place_camera();

    /* Coins: hover lights them up, a click collects them. */
    {
        const wgf_vec2_t mouse = wgf_mouse_get_position();
        const float zoom_now = height > 0.0f ? g.zoom / height : 0.0f;
        hovered = coin_at(g.center_x + (mouse.x - width * 0.5f) * zoom_now,
                          g.center_y - (mouse.y - height * 0.5f) * zoom_now);
    }
    if (wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT)) g.pressed = hovered;
    if (wgf_mouse_is_released(WGF_MOUSE_BUTTON_LEFT)) {
        if (g.pressed >= 0 && g.pressed == hovered) {
            wgf_node_set_visible(g.props[hovered], false);
            g.collected++;
            hovered = -1;
        }
        g.pressed = -1;
    }
    for (i = 0; i < g.prop_count; i++) {
        if (g.is_coin[i]) wgf_sprite_set_tint(g.props[i], i == hovered ? g.highlight : WGF_COLOR_WHITE);
    }

    wgf_canvas_draw(g.canvas);
    wgf_draw_rectangle(0, 0, width, 88, g.shade);
    wgf_draw_text(0, "libwgf tilemap: an orthographic camera over sprite tiles", 20, 20, 20, g.text);
    snprintf(line, sizeof(line), "coins: %d of 8   zoom: %.1f units   center: %.1f, %.1f%s", g.collected,
             (double)g.zoom, (double)g.center_x, (double)g.center_y,
             wgf_resource_get_status(g.texture) == WGF_RESOURCE_STATUS_READY ? "" : "   (loading)");
    wgf_draw_text(0, line, 20, 46, 15, g.dim);
    wgf_draw_text(0, "drag or arrows to scroll, wheel to zoom, click the coins", 20, 68, 15, g.dim);
}

int main(void)
{
    /* No MSAA: tiles are quads that meet edge to edge, and multisampled edges let the
     * background through as a hairline seam between them. */
    wgf_window_set_title("libwgf tilemap");
    wgf_window_set_size(960, 600);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
