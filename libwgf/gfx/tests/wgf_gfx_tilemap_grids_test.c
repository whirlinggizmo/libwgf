#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_sprite_batch.h"
#include "wgf_stage2d.h"
#include "wgf_tilemap.h"
#include "wgf_tilemap_layer.h"
#include "wgf_time.h"

/* Tiled maps' grids, headless: an orthogonal layer whose tiles reach past their cells drawn in
 * Tiled's order, a batch for each run of a tileset along a row, kept so as cells are set (and
 * a layer of cell-sized tiles turned to that order when a tall one is set), and an orthogonal
 * map's render order; isometric, staggered, and hexagonal maps' cells' middles, the cells at
 * points (on edges and outside the map too), their sizes in pixels, and their drawing order,
 * an animated tile stepping in an ordered layer, and an isometric map's objects placed; an
 * infinite map's chunks read into one grid from its origin, CSV and base64 zlib; and the
 * lister naming image layers' images. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static const unsigned char png_2x2[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00,
    0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81,
    0x34, 0x18, 0x00, 0x00, 0x49, 0xc8, 0x09, 0xf7, 0xf9, 0xab, 0xb6, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

/* Tileset A (gids 1 to 4) is the cells' size; tileset B (5 to 8) twice as tall. */
#define TWO_TILESETS                                                                                                   \
    " \"tilesets\": [{\"firstgid\": 1, \"name\": \"a\", \"image\": \"a.png\", \"tilewidth\": 16, \"tileheight\": 16,"   \
    "  \"tilecount\": 4, \"columns\": 2}, {\"firstgid\": 5, \"name\": \"b\", \"image\": \"b.png\", \"tilewidth\": 16,"   \
    "  \"tileheight\": 32, \"tilecount\": 4, \"columns\": 2}],"

static const char tall[] = "{\"type\": \"map\", \"width\": 3, \"height\": 2, \"tilewidth\": 16, \"tileheight\": 16," TWO_TILESETS
                           " \"layers\": ["
                           "  {\"type\": \"tilelayer\", \"name\": \"ground\", \"width\": 3, \"height\": 2, \"data\": [1, 5, 1, 5, 1, 1]},"
                           "  {\"type\": \"tilelayer\", \"name\": \"flat\", \"width\": 3, \"height\": 2, \"data\": [1, 1, 1, 1, 2, 1]}]}";

static const char left_up[] = "{\"type\": \"map\", \"renderorder\": \"left-up\", \"width\": 2, \"height\": 2, \"tilewidth\": 16,"
                              " \"tileheight\": 16," TWO_TILESETS
                              " \"layers\": [{\"type\": \"tilelayer\", \"name\": \"ground\", \"width\": 2, \"height\": 2,"
                              " \"data\": [5, 6, 7, 8]}]}";

/* 32 by 32 tiles on 32 by 16 diamonds; tile 0 (gid 1) animated to tile 1 and back, a tenth of
 * a second each. */
#define BLOCKS                                                                                                         \
    " \"tilesets\": [{\"firstgid\": 1, \"name\": \"blocks\", \"image\": \"a.png\", \"tilewidth\": 32,"                  \
    "  \"tileheight\": 32, \"tilecount\": 4, \"columns\": 2, \"tiles\": [{\"id\": 0, \"animation\": ["                  \
    "  {\"tileid\": 0, \"duration\": 100}, {\"tileid\": 1, \"duration\": 100}]}]}],"

static const char iso[] = "{\"type\": \"map\", \"orientation\": \"isometric\", \"width\": 3, \"height\": 3, \"tilewidth\": 32,"
                          " \"tileheight\": 16," BLOCKS
                          " \"layers\": ["
                          "  {\"type\": \"tilelayer\", \"name\": \"ground\", \"width\": 3, \"height\": 3,"
                          "   \"data\": [2, 2, 2, 2, 1, 2, 2, 2, 2]},"
                          "  {\"type\": \"objectgroup\", \"name\": \"things\", \"objects\": ["
                          "   {\"id\": 1, \"name\": \"spot\", \"x\": 16, \"y\": 16, \"point\": true},"
                          "   {\"id\": 2, \"name\": \"crate\", \"gid\": 3, \"x\": 16, \"y\": 16, \"width\": 32, \"height\": 32}]}]}";

static const char staggered[] =
    "{\"type\": \"map\", \"orientation\": \"staggered\", \"staggeraxis\": \"y\", \"staggerindex\": \"odd\", \"width\": 3,"
    " \"height\": 4, \"tilewidth\": 32, \"tileheight\": 16," BLOCKS
    " \"layers\": [{\"type\": \"tilelayer\", \"name\": \"ground\", \"width\": 3, \"height\": 4,"
    "  \"data\": [2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2]}]}";

static const char hexagonal[] =
    "{\"type\": \"map\", \"orientation\": \"hexagonal\", \"staggeraxis\": \"x\", \"staggerindex\": \"even\","
    " \"hexsidelength\": 16, \"width\": 3, \"height\": 2, \"tilewidth\": 32, \"tileheight\": 28,"
    " \"tilesets\": [{\"firstgid\": 1, \"name\": \"hexes\", \"image\": \"a.png\", \"tilewidth\": 32, \"tileheight\": 28,"
    "  \"tilecount\": 2, \"columns\": 2}],"
    " \"layers\": [{\"type\": \"tilelayer\", \"name\": \"ground\", \"width\": 3, \"height\": 2, \"data\": [1, 1, 1, 1, 1, 1]}]}";

static const char infinite[] =
    "{\"type\": \"map\", \"infinite\": true, \"width\": 10, \"height\": 10, \"tilewidth\": 16, \"tileheight\": 16," TWO_TILESETS
    " \"layers\": ["
    "  {\"type\": \"tilelayer\", \"name\": \"ground\", \"startx\": -4, \"starty\": -2, \"width\": 8, \"height\": 4,"
    "   \"chunks\": [{\"x\": -4, \"y\": -2, \"width\": 4, \"height\": 2, \"data\": [1, 2, 3, 4, 4, 3, 2, 1]},"
    "    {\"x\": 0, \"y\": 0, \"width\": 4, \"height\": 2, \"data\": [2, 2, 2, 2, 3, 3, 3, 3]}]},"
    "  {\"type\": \"group\", \"name\": \"g\", \"layers\": ["
    "   {\"type\": \"tilelayer\", \"name\": \"deco\", \"encoding\": \"base64\", \"compression\": \"zlib\","
    "    \"chunks\": [{\"x\": 1, \"y\": 0, \"width\": 3, \"height\": 2,"
    "     \"data\": \"eJxjYGBgYGaAAEYGBgUQzQLEAAGQACk=\"}]}]}]}";

static void write_file(const char *path, const void *data, int size)
{
    const wgf_fs_task_t write = wgf_fs_write(path, (const unsigned char *)data, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(write);
}

static wgf_tilemap_t load(const char *path, const char *text)
{
    wgf_tilemap_t m;
    double start;
    write_file(path, text, (int)strlen(text));
    m = wgf_tilemap_create(path);
    start = wgf_time_get_seconds();
    while (wgf_resource_get_status(m) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
    expect(wgf_resource_get_status(m) == WGF_RESOURCE_STATUS_READY, path);
    return m;
}

/* Quad `index` of a batch: its position and region's x. */
static void quad(wgf_actor_t batch, int index, float *x, float *y, float *region_x)
{
    float position[2] = {0.0f, 0.0f}, region[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    wgf_sprite_batch_get_positions(batch, index, position, 2);
    wgf_sprite_batch_get_regions(batch, index, region, 4);
    *x = position[0], *y = position[1], *region_x = region[0];
}

/* Whether the layer's batches are, in order, of tilesets `tilesets` (-1 ends) with `counts`
 * quads. */
static int batches_are(wgf_tilemap_t m, wgf_actor_t layer, const int *tilesets, const int *counts)
{
    int i;
    for (i = 0; tilesets[i] >= 0; i++) {
        const wgf_actor_t b = wgf_actor_get_child(layer, i);
        if (wgf_sprite_batch_get_texture(b) != wgf_tilemap_get_tileset_texture(m, tilesets[i]) ||
            wgf_sprite_batch_get_count(b) != counts[i]) {
            printf("  batch %d: %d quads\n", i, wgf_sprite_batch_get_count(b));
            return 0;
        }
    }
    return wgf_actor_get_child_count(layer) == i;
}

static void test_overlap(wgf_actor_t stage)
{
    const wgf_tilemap_t m = load("grids_test/tall.tmj", tall);
    const wgf_actor_t root = wgf_tilemap_instantiate(m, stage);
    const wgf_actor_t ground = wgf_actor_find(root, "ground"), flat = wgf_actor_find(root, "flat");
    float x, y, rx;
    {
        const int sets[] = {0, 1, 0, 1, 0, -1}, counts[] = {1, 1, 1, 1, 2};
        expect(batches_are(m, ground, sets, counts), "tall tiles: a batch a run of a tileset, row by row, in order");
    }
    quad(wgf_actor_get_child(ground, 1), 0, &x, &y, &rx);
    expect(x == 24.0f && y == 0.0f, "a tall tile reaching up from its cell's bottom-left");
    {
        const int sets[] = {0, -1}, counts[] = {6};
        expect(batches_are(m, flat, sets, counts), "a layer of tiles its cells' size: a batch a tileset");
    }
    expect(wgf_tilemap_layer_set_cell(ground, 2, 0, 5, 0), "a cell set");
    {
        const int sets[] = {0, 1, 1, 0, -1}, counts[] = {1, 2, 1, 2};
        expect(batches_are(m, ground, sets, counts), "set: its row's runs built again, the rest kept in order");
    }
    expect(wgf_tilemap_layer_set_cell(ground, 2, 1, 6, 0), "a cell set at a row's end");
    {
        const int sets[] = {0, 1, 1, 0, 1, -1}, counts[] = {1, 2, 1, 1, 1};
        expect(batches_are(m, ground, sets, counts), "a run more at the end");
    }
    expect(wgf_tilemap_layer_set_cell(ground, 0, 0, 5, 0) && wgf_tilemap_layer_set_cell(ground, 1, 0, 1, 0),
           "cells set at a row's start");
    {
        const int sets[] = {1, 0, 1, 1, 0, 1, -1}, counts[] = {1, 1, 1, 1, 1, 1};
        expect(batches_are(m, ground, sets, counts), "a run more in the middle, placed after the one before it");
    }
    quad(wgf_actor_get_child(ground, 3), 0, &x, &y, &rx);
    expect(x == 8.0f && y == 16.0f, "the next row's first after it");
    expect(wgf_tilemap_layer_set_cell(flat, 0, 0, 5, 0), "a tall tile set in a layer of small ones");
    {
        const int sets[] = {1, 0, 0, -1}, counts[] = {1, 2, 3};
        expect(batches_are(m, flat, sets, counts), "then drawn in Tiled's order");
    }
    {
        const int cells[6] = {5, 5, 5, 1, 1, 1};
        const int sets[] = {1, 0, -1}, counts[] = {3, 3};
        expect(wgf_tilemap_layer_set_cells(flat, cells, 6) && batches_are(m, flat, sets, counts), "every cell set at once");
    }
    wgf_resource_release(m);
    {
        const wgf_tilemap_t lu = load("grids_test/leftup.tmj", left_up);
        const wgf_actor_t layer = wgf_actor_find(wgf_tilemap_instantiate(lu, stage), "ground");
        float rx2;
        quad(wgf_actor_get_child(layer, 0), 0, &x, &y, &rx);
        quad(wgf_actor_get_child(layer, 0), 1, &x, &y, &rx2);
        expect(wgf_actor_get_child_count(layer) == 2 && rx == 16.0f && rx2 == 0.0f && x == 8.0f && y == 16.0f,
               "left-up: the bottom row first, from the right");
        wgf_resource_release(lu);
    }
}

static void test_isometric(wgf_actor_t stage)
{
    const wgf_tilemap_t m = load("grids_test/iso.tmj", iso);
    const wgf_actor_t root = wgf_tilemap_instantiate(m, stage);
    const wgf_actor_t ground = wgf_actor_find(root, "ground"), things = wgf_actor_find(root, "things");
    const wgf_vec2_t middle = wgf_tilemap_get_cell_middle(m, 1, 0);
    const wgf_actor_t crate = wgf_tilemap_layer_get_object_actor(things, 1);
    float x, y, rx;
    int i;
    expect(wgf_tilemap_get_orientation(m) == WGF_TILEMAP_ORIENTATION_ISOMETRIC && wgf_tilemap_get_pixel_width(m) == 96 &&
               wgf_tilemap_get_pixel_height(m) == 48,
           "isometric: its diamond's box");
    expect(middle.x == 64.0f && middle.y == 16.0f && wgf_tilemap_get_cell_middle(m, 0, 0).x == 48.0f &&
               wgf_tilemap_get_cell_middle(m, 0, 0).y == 8.0f && wgf_tilemap_get_cell_middle(m, 0, 1).x == 32.0f,
           "its cells' middles down its axes");
    expect(wgf_tilemap_get_column_at(m, 48.0f, 24.0f) == 1 && wgf_tilemap_get_row_at(m, 48.0f, 24.0f) == 1 &&
               wgf_tilemap_get_column_at(m, 44.0f, 6.0f) == 0 && wgf_tilemap_get_row_at(m, 44.0f, 6.0f) == 0 &&
               wgf_tilemap_get_column_at(m, 20.0f, 8.0f) == -1 && wgf_tilemap_get_row_at(m, 20.0f, 8.0f) == 1,
           "the cell whose diamond holds a point, outside the map too");
    expect(wgf_tilemap_layer_get_column_at(ground, 64.0f, 16.0f) == 1 &&
               wgf_tilemap_layer_get_cell_middle(ground, 1, 1).y == 24.0f,
           "a layer's cells, through it");
    expect(wgf_actor_get_child_count(ground) == 5, "drawn by diagonals: a batch each");
    quad(wgf_actor_get_child(ground, 1), 0, &x, &y, &rx);
    expect(x == 32.0f && y == 8.0f, "a diagonal from its left: a tall tile's middle, its bottom-left on its box's");
    quad(wgf_actor_get_child(ground, 1), 1, &x, &y, &rx);
    expect(x == 64.0f && y == 8.0f, "then right along it");
    quad(wgf_actor_get_child(ground, 2), 1, &x, &y, &rx);
    expect(x == 48.0f && rx == 0.0f, "the middle cell animated, at its first frame");
    for (i = 0; i < 3; i++) wgf_core_priv_part_update(0.04f);
    quad(wgf_actor_get_child(ground, 2), 1, &x, &y, &rx);
    expect(rx == 32.0f, "a tenth of a second on, in an ordered layer: its second");
    expect(wgf_tilemap_get_object_middle(m, 1, 0).x == 48.0f && wgf_tilemap_get_object_middle(m, 1, 0).y == 16.0f,
           "an object's place down the map's axes taken to its pixels");
    expect(crate != 0 && wgf_actor_get_position(crate).x == 48.0f && wgf_actor_get_position(crate).y == 0.0f &&
               wgf_tilemap_get_object_middle(m, 1, 1).y == 0.0f,
           "a tile object at its bottom's middle, as isometric maps align them");
    wgf_resource_release(m);
}

static void test_staggered(wgf_actor_t stage)
{
    const wgf_tilemap_t m = load("grids_test/stag.tmj", staggered);
    const wgf_actor_t ground = wgf_actor_find(wgf_tilemap_instantiate(m, stage), "ground");
    float x, y, rx;
    expect(wgf_tilemap_get_orientation(m) == WGF_TILEMAP_ORIENTATION_STAGGERED && wgf_tilemap_get_pixel_width(m) == 112 &&
               wgf_tilemap_get_pixel_height(m) == 40,
           "staggered: its size, its odd rows moved half a tile");
    expect(wgf_tilemap_get_cell_middle(m, 0, 0).x == 16.0f && wgf_tilemap_get_cell_middle(m, 0, 0).y == 8.0f &&
               wgf_tilemap_get_cell_middle(m, 0, 1).x == 32.0f && wgf_tilemap_get_cell_middle(m, 0, 1).y == 16.0f &&
               wgf_tilemap_get_cell_middle(m, 2, 3).x == 96.0f && wgf_tilemap_get_cell_middle(m, 2, 3).y == 32.0f,
           "its cells' middles");
    expect(wgf_tilemap_get_column_at(m, 32.0f, 16.0f) == 0 && wgf_tilemap_get_row_at(m, 32.0f, 16.0f) == 1 &&
               wgf_tilemap_get_column_at(m, 31.0f, 9.0f) == 0 && wgf_tilemap_get_row_at(m, 31.0f, 9.0f) == 1 &&
               wgf_tilemap_get_row_at(m, 16.0f, 8.0f) == 0 && wgf_tilemap_get_column_at(m, 1.0f, 1.0f) == -1 &&
               wgf_tilemap_get_row_at(m, 1.0f, 1.0f) == -1,
           "the cell whose diamond holds a point, between rows and outside the map");
    expect(wgf_actor_get_child_count(ground) == 4, "drawn row by row");
    quad(wgf_actor_get_child(ground, 1), 2, &x, &y, &rx);
    expect(x == 96.0f && y == 8.0f, "a moved row's cell, its tall tile up from its box");
    wgf_resource_release(m);
}

static void test_hexagonal(wgf_actor_t stage)
{
    const wgf_tilemap_t m = load("grids_test/hex.tmj", hexagonal);
    const wgf_actor_t ground = wgf_actor_find(wgf_tilemap_instantiate(m, stage), "ground");
    float x, y, rx;
    expect(wgf_tilemap_get_orientation(m) == WGF_TILEMAP_ORIENTATION_HEXAGONAL && wgf_tilemap_get_pixel_width(m) == 80 &&
               wgf_tilemap_get_pixel_height(m) == 70,
           "hexagonal: its size");
    expect(wgf_tilemap_get_cell_middle(m, 0, 0).x == 16.0f && wgf_tilemap_get_cell_middle(m, 0, 0).y == 28.0f &&
               wgf_tilemap_get_cell_middle(m, 1, 0).x == 40.0f && wgf_tilemap_get_cell_middle(m, 1, 0).y == 14.0f &&
               wgf_tilemap_get_cell_middle(m, 0, 1).y == 56.0f,
           "its cells' middles, its even columns moved down");
    expect(wgf_tilemap_get_column_at(m, 16.0f, 28.0f) == 0 && wgf_tilemap_get_column_at(m, 40.0f, 14.0f) == 1 &&
               wgf_tilemap_get_row_at(m, 40.0f, 14.0f) == 0 && wgf_tilemap_get_column_at(m, 2.0f, 28.0f) == 0 &&
               wgf_tilemap_get_column_at(m, 2.0f, 15.0f) == -1 && wgf_tilemap_get_row_at(m, 2.0f, 15.0f) == 0,
           "the cell whose hexagon holds a point, by its slanted sides too");
    expect(wgf_actor_get_child_count(ground) == 4 && wgf_sprite_batch_get_count(wgf_actor_get_child(ground, 0)) == 1 &&
               wgf_sprite_batch_get_count(wgf_actor_get_child(ground, 1)) == 2,
           "drawn a row's raised columns, then its lowered ones");
    quad(wgf_actor_get_child(ground, 0), 0, &x, &y, &rx);
    expect(x == 40.0f && y == 14.0f, "the raised column first");
    wgf_resource_release(m);
}

static void test_infinite(wgf_actor_t stage)
{
    const wgf_tilemap_t m = load("grids_test/inf.tmj", infinite);
    const wgf_actor_t root = wgf_tilemap_instantiate(m, stage);
    const wgf_actor_t ground = wgf_actor_find(root, "ground"), deco = wgf_actor_find(root, "deco");
    float x, y, rx;
    expect(wgf_tilemap_is_infinite(m) && wgf_tilemap_get_columns(m) == 8 && wgf_tilemap_get_rows(m) == 4 &&
               wgf_tilemap_get_origin_column(m) == -4 && wgf_tilemap_get_origin_row(m) == -2,
           "infinite: the box its chunks fill, from its origin");
    expect(wgf_tilemap_layer_get_cell(ground, 0, 0) == 1 && wgf_tilemap_layer_get_cell(ground, 3, 1) == 1 &&
               wgf_tilemap_layer_get_cell(ground, 4, 2) == 2 && wgf_tilemap_layer_get_cell(ground, 7, 3) == 3 &&
               wgf_tilemap_layer_get_cell(ground, 0, 2) == 0,
           "each chunk's cells where it is");
    expect(wgf_tilemap_layer_get_cell(deco, 6, 2) == 3 && wgf_tilemap_layer_get_cell(deco, 5, 3) == 1 &&
               wgf_tilemap_layer_get_cell_flips(deco, 5, 3) == WGF_TILEMAP_FLIP_DIAGONAL &&
               wgf_tilemap_layer_get_cell(deco, 7, 3) == 4,
           "a chunk in base64 zlib, in a group");
    expect(wgf_tilemap_get_cell_middle(m, 0, 0).x == -56.0f && wgf_tilemap_get_cell_middle(m, 0, 0).y == -24.0f &&
               wgf_tilemap_get_column_at(m, -56.0f, -24.0f) == 0 && wgf_tilemap_get_column_at(m, 8.0f, 8.0f) == 4 &&
               wgf_tilemap_get_row_at(m, 8.0f, 8.0f) == 2,
           "its cells where Tiled has them");
    quad(wgf_actor_get_child(ground, 0), 0, &x, &y, &rx);
    expect(x == -56.0f && y == -24.0f, "drawn there");
    wgf_resource_release(m);
}

static int listed;
static char named[4][64];

static void add(const char *uri, const char *fallback, bool required, void *context)
{
    (void)fallback, (void)required, (void)context;
    if (listed < 4) snprintf(named[listed], sizeof(named[listed]), "%s", uri);
    listed++;
}

int main(void)
{
    wgf_actor_t stage;
    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(wgf_gfx_priv_start(), "setup");
    write_file("grids_test/a.png", png_2x2, (int)sizeof(png_2x2));
    write_file("grids_test/b.png", png_2x2, (int)sizeof(png_2x2));
    stage = wgf_stage2d_create();
    test_overlap(stage);
    test_isometric(stage);
    test_staggered(stage);
    test_hexagonal(stage);
    test_infinite(stage);
    expect(wgf_tilemap_get_cell_middle(0, 0, 0).x == 0.0f && wgf_tilemap_get_column_at(0, 5.0f, 5.0f) == 0 &&
               wgf_tilemap_get_orientation(0) == WGF_TILEMAP_ORIENTATION_NONE && wgf_tilemap_get_pixel_width(0) == 0,
           "no map: none");
    {
        static const char text[] = "{\"layers\": [{\"type\": \"group\", \"layers\": [{\"type\": \"imagelayer\","
                                   " \"image\": \"sky.png\"}]}, {\"type\": \"imagelayer\", \"image\": \"\"}]}";
        const wgf_core_priv_load_lister_fn list = wgf_core_priv_load_lister("a/b.tmj");
        if (list != NULL) list((const unsigned char *)text, (int)sizeof(text) - 1, add, NULL);
        expect(list != NULL && listed == 1 && strcmp(named[0], "sky.png") == 0,
               "a map's lister names its image layers' images, in groups too");
    }
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    {
        const wgf_fs_task_t remove = wgf_fs_rmdir("grids_test");
        int i;
        for (i = 0; i < 1000 && wgf_fs_task_get_status(remove) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
        wgf_fs_task_destroy(remove);
    }
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
