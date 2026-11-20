#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera2d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_mouse.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape2d.h"
#include "wgf_stage2d.h"
#include "wgf_tilemap.h"
#include "wgf_tilemap_layer.h"
#include "wgf_window.h"

/* Tiled maps of every grid, side by side on one 2D stage (written by tools/gen_tilemaps.py):
 *   - maps/grids_iso.tmj: isometric, its blocks and trees taller than their diamonds drawn in
 *     Tiled's order (by diagonals), a lamp a tile object, its pond's water animated
 *   - maps/grids_staggered.tmj: staggered, every other row moved half a diamond
 *   - maps/grids_hex.tmj: hexagonal, pointed tops, every other row moved half a hexagon
 *   - maps/grids_infinite.tmj: an infinite orthogonal map in four chunks round Tiled's first
 *     cell, read as one grid from its chunks' top-left (its origin)
 * A ring marks a cell of each: the one under the pointer (its column and row from the point,
 * its middle back from the cell), or, left alone, one walking each map's cells in turn.
 *
 *   mouse   point at a cell
 *   Esc     quit, where quitting means anything */

#define MAPS 4
#define GAP 48.0f /* map pixels between the maps */

static const char *const paths[MAPS] = {"maps/grids_iso.tmj", "maps/grids_staggered.tmj", "maps/grids_hex.tmj",
                                        "maps/grids_infinite.tmj"};
static const char *const names[MAPS] = {"isometric", "staggered", "hexagonal", "infinite"};

static struct {
    wgf_actor_t stage, camera;
    wgf_tilemap_t maps[MAPS];
    wgf_actor_t roots[MAPS], grounds[MAPS], rings[MAPS];
    float left[MAPS], top[MAPS]; /* each map's box's top-left on the stage */
    float width, height;         /* all four's */
    int columns[MAPS], rows[MAPS];
    bool placed;
    int frame;
    wgf_color_t text, dim;
} g;

static void init(void *user)
{
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(26, 30, 40, 255));
    g.text = wgf_color_make(235, 238, 245, 255);
    g.dim = wgf_color_make(150, 156, 170, 255);
    g.stage = wgf_stage2d_create();
    g.camera = wgf_camera2d_create();
    wgf_stage2d_set_camera(g.stage, g.camera);
    for (i = 0; i < MAPS; i++) g.maps[i] = wgf_tilemap_create(paths[i]);
}

/* Once every map is READY: each placed in its quarter, a ring over each. */
static void place(void)
{
    float widths[MAPS], heights[MAPS], column_w, row_h;
    int i;
    for (i = 0; i < MAPS; i++) {
        widths[i] = (float)wgf_tilemap_get_pixel_width(g.maps[i]);
        heights[i] = (float)wgf_tilemap_get_pixel_height(g.maps[i]);
        g.columns[i] = wgf_tilemap_get_columns(g.maps[i]);
        g.rows[i] = wgf_tilemap_get_rows(g.maps[i]);
    }
    column_w = fmaxf(widths[0], widths[2]);
    row_h = fmaxf(heights[0], heights[1]);
    g.width = column_w + GAP + fmaxf(widths[1], widths[3]);
    g.height = row_h + GAP + fmaxf(heights[2], heights[3]);
    for (i = 0; i < MAPS; i++) {
        /* An infinite map's cells start at its origin, which may be left of and above 0, 0. */
        const float ox = (float)(wgf_tilemap_get_origin_column(g.maps[i]) * wgf_tilemap_get_tile_width(g.maps[i]));
        const float oy = (float)(wgf_tilemap_get_origin_row(g.maps[i]) * wgf_tilemap_get_tile_height(g.maps[i]));
        g.left[i] = (i % 2 == 0) ? column_w - widths[i] : column_w + GAP;
        g.top[i] = (i < 2) ? row_h - heights[i] : row_h + GAP;
        g.roots[i] = wgf_tilemap_instantiate(g.maps[i], g.stage);
        wgf_actor_set_position(g.roots[i], g.left[i] - ox, g.top[i] - oy, 0.0f);
        g.grounds[i] = wgf_actor_find(g.roots[i], "ground");
        g.rings[i] = wgf_shape2d_create();
        wgf_shape2d_set_circle(g.rings[i], 6.0f);
        wgf_shape2d_set_outline(g.rings[i], 2.0f);
        wgf_shape2d_set_color(g.rings[i], wgf_color_make(255, 220, 90, 255));
        wgf_actor_set_parent(g.rings[i], g.roots[i]); /* in the map's pixels, as its layers are */
    }
    g.placed = true;
}

/* Each ring on the cell under the pointer, or on its map's next cell in turn. */
static void mark(int *columns, int *rows)
{
    const wgf_vec2_t mouse = wgf_mouse_get_position();
    const wgf_vec2_t world = wgf_stage2d_screen_to_world(g.stage, mouse.x, mouse.y);
    const int step = g.frame / 20;
    int i;
    for (i = 0; i < MAPS; i++) {
        const wgf_vec3_t root = wgf_actor_get_position(g.roots[i]);
        int column = wgf_tilemap_layer_get_column_at(g.grounds[i], world.x - root.x, world.y - root.y);
        int row = wgf_tilemap_layer_get_row_at(g.grounds[i], world.x - root.x, world.y - root.y);
        wgf_vec2_t middle;
        if (column < 0 || row < 0 || column >= g.columns[i] || row >= g.rows[i]) { /* not over it: walking */
            column = step % g.columns[i];
            row = (step / g.columns[i]) % g.rows[i];
        }
        middle = wgf_tilemap_layer_get_cell_middle(g.grounds[i], column, row);
        wgf_actor_set_position(g.rings[i], middle.x, middle.y, 0.0f);
        columns[i] = column, rows[i] = row;
    }
}

static void frame(void *user)
{
    const float width = (float)wgf_window_get_width(), height = (float)wgf_window_get_height();
    int columns[MAPS] = {0, 0, 0, 0}, rows[MAPS] = {0, 0, 0, 0};
    char line[160];
    int i;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (!g.placed) {
        for (i = 0; i < MAPS && wgf_resource_get_status(g.maps[i]) == WGF_RESOURCE_STATUS_READY; i++) {
        }
        if (i == MAPS) place();
    }
    g.frame++;
    if (g.placed) {
        const float zoom = fminf((width - 40.0f) / g.width, (height - 120.0f) / g.height);
        wgf_actor_set_position(g.camera, g.width * 0.5f, g.height * 0.5f - 30.0f / (zoom > 0.0f ? zoom : 1.0f), 0.0f);
        if (zoom > 0.0f) wgf_camera2d_set_zoom(g.camera, zoom);
        mark(columns, rows);
    }

    wgf_stage2d_draw(g.stage);
    wgf_draw_text(0, "libwgf tilemap grids: Tiled's isometric, staggered, hexagonal, and infinite maps", 20, 16, 20,
                  g.text);
    wgf_draw_text(0, "point at a cell (left alone, a ring walks each map's cells)", 20, 42, 15, g.dim);
    for (i = 0; g.placed && i < MAPS; i++) {
        const wgf_vec2_t at = wgf_stage2d_world_to_screen(g.stage, g.left[i], g.top[i]);
        snprintf(line, sizeof(line), "%s %d by %d: cell %d, %d", names[i], g.columns[i], g.rows[i], columns[i], rows[i]);
        wgf_draw_text(0, line, at.x, at.y - 18.0f, 14, g.text);
    }
}

int main(void)
{
    wgf_window_set_title("libwgf tilemap grids");
    wgf_window_set_size(960, 640);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
