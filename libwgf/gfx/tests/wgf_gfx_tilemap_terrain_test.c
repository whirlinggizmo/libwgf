#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_stage2d.h"
#include "wgf_tilemap.h"
#include "wgf_tilemap_layer.h"
#include "wgf_time.h"

/* Terrain (Wang) sets, headless: a corner set and an edge set read from a tileset file, a
 * mixed one in a tileset embedded in the map; their names, kinds, tilesets, terrains, and
 * tiles' places; painted into layers as Tiled's Terrain brush paints (helix-legacy's
 * setTerrain), each cell around checked against the tile expected; terrain read again from
 * the cells after a cell is set; nothing painted where no tile fits, and a cell emptied where
 * nothing of the set is left; and what painting refuses. */

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

/* The ground tileset (gids 1 to 16): tile i's corners grass (2) where its bits say -- 1 its
 * top-left, 2 its top-right, 4 its bottom-right, 8 its bottom-left -- and dirt (1) elsewhere.
 * The roads tileset (gids 17 to 32): tile i's edges road (1) where its bits say -- 1 its top,
 * 2 its right, 4 its bottom, 8 its left -- and none elsewhere (tile 0 takes no part). */
static char tilesets[8192];

static void write_tilesets(void)
{
    int n = 0, i;
    n += snprintf(tilesets + n, sizeof(tilesets) - (size_t)n,
                  "{\"name\": \"ground\", \"image\": \"two.png\", \"tilewidth\": 1, \"tileheight\": 1,"
                  " \"tilecount\": 16, \"columns\": 2, \"objectalignment\": \"center\", \"wangsets\": [{\"name\": \"land\", "
                  "\"type\": \"corner\", \"colors\": [{\"name\": \"dirt\"}, {\"name\": \"grass\"}], \"wangtiles\": [");
    for (i = 0; i < 16; i++) {
        n += snprintf(tilesets + n, sizeof(tilesets) - (size_t)n, "%s{\"tileid\": %d, \"wangid\": [0,%d,0,%d,0,%d,0,%d]}",
                      i > 0 ? ", " : "", i, (i & 2) ? 2 : 1, (i & 4) ? 2 : 1, (i & 8) ? 2 : 1, (i & 1) ? 2 : 1);
    }
    n += snprintf(tilesets + n, sizeof(tilesets) - (size_t)n,
                  "]}, {\"name\": \"bad\", \"type\": \"corner\", \"colors\": [{\"name\": \"x\"}], \"wangtiles\": ["
                  "{\"tileid\": 0, \"wangid\": [0,5,0,1,0,1,0,1]}, {\"tileid\": 99, \"wangid\": [0,1,0,1,0,1,0,1]},"
                  " {\"tileid\": 1, \"wangid\": [0,1,0,1,0,1,0]}, {\"tileid\": 2, \"wangid\": [0,1,0,1,0,1,0,1]}]}]}");
    (void)n;
}

static char roads[8192];

static void write_roads(void)
{
    int n = 0, i;
    n += snprintf(roads + n, sizeof(roads) - (size_t)n,
                  "{\"name\": \"roads\", \"image\": \"two.png\", \"tilewidth\": 1, \"tileheight\": 1,"
                  " \"tilecount\": 16, \"columns\": 2, \"wangsets\": [{\"name\": \"paths\", \"type\": \"edge\","
                  " \"colors\": [{\"name\": \"road\"}], \"wangtiles\": [");
    for (i = 1; i < 16; i++) {
        n += snprintf(roads + n, sizeof(roads) - (size_t)n, "%s{\"tileid\": %d, \"wangid\": [%d,0,%d,0,%d,0,%d,0]}",
                      i > 1 ? ", " : "", i, (i & 1) ? 1 : 0, (i & 2) ? 1 : 0, (i & 4) ? 1 : 0, (i & 8) ? 1 : 0);
    }
    snprintf(roads + n, sizeof(roads) - (size_t)n, "]}]}");
}

/* 5 by 4: ground all dirt (gid 1), roads empty; the mixed set's tileset embedded first. */
static const char map[] =
    "{\"type\": \"map\", \"width\": 5, \"height\": 4, \"tilewidth\": 1, \"tileheight\": 1,"
    " \"tilesets\": [{\"firstgid\": 1, \"source\": \"ground.tsj\"}, {\"firstgid\": 17, \"source\": \"roads.tsj\"},"
    "  {\"firstgid\": 33, \"name\": \"odd\", \"image\": \"two.png\", \"tilewidth\": 1, \"tileheight\": 1,"
    "   \"tilecount\": 1, \"columns\": 1, \"wangsets\": [{\"name\": \"both\", \"type\": \"mixed\","
    "   \"colors\": [{\"name\": \"a\"}], \"wangtiles\": [{\"tileid\": 0, \"wangid\": [1,1,1,1,1,1,1,1]}]}]}],"
    " \"layers\": ["
    "  {\"type\": \"tilelayer\", \"name\": \"ground\", \"width\": 5, \"height\": 4,"
    "   \"data\": [1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1]},"
    "  {\"type\": \"tilelayer\", \"name\": \"roads\", \"width\": 5, \"height\": 4,"
    "   \"data\": [0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0]}]}";

static const char staggered[] =
    "{\"type\": \"map\", \"orientation\": \"staggered\", \"staggeraxis\": \"y\", \"staggerindex\": \"odd\","
    " \"width\": 2, \"height\": 2, \"tilewidth\": 2, \"tileheight\": 2,"
    " \"tilesets\": [{\"firstgid\": 1, \"source\": \"ground.tsj\"}],"
    " \"layers\": [{\"type\": \"tilelayer\", \"name\": \"ground\", \"width\": 2, \"height\": 2, \"data\": [1, 1, 1, 1]}]}";

static void write_file(const char *path, const void *data, int size)
{
    const wgf_fs_task_t write = wgf_fs_write(path, (const unsigned char *)data, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(write);
}

static void wait_for(wgf_handle_t resource)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(resource) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
}

/* Whether the layer's cells are `expected`, row by row; the first that isn't printed. */
static int cells_are(wgf_actor_t layer, const int *expected, int count)
{
    int cells[64], i;
    if (wgf_tilemap_layer_get_cells(layer, cells, 64) != count) return 0;
    for (i = 0; i < count; i++) {
        if (cells[i] != expected[i]) {
            printf("  cell %d is %d, not %d\n", i, cells[i], expected[i]);
            return 0;
        }
    }
    return 1;
}

/* The ground tile (gid) whose corners are grass where `bits` say. */
#define G(bits) (1 + (bits))
/* The road tile (gid) whose edges are road where `bits` say. */
#define R(bits) (17 + (bits))

static void test_read(wgf_tilemap_t m)
{
    expect(wgf_tilemap_get_terrainset_count(m) == 4 && strcmp(wgf_tilemap_get_terrainset_name(m, 0), "land") == 0 &&
               strcmp(wgf_tilemap_get_terrainset_name(m, 1), "bad") == 0 &&
               strcmp(wgf_tilemap_get_terrainset_name(m, 2), "paths") == 0 &&
               strcmp(wgf_tilemap_get_terrainset_name(m, 3), "both") == 0 &&
               strcmp(wgf_tilemap_get_terrainset_name(m, 4), "") == 0,
           "its terrain sets, by tileset and then in the file's order");
    expect(wgf_tilemap_get_terrainset_kind(m, 0) == WGF_TILEMAP_TERRAIN_KIND_CORNER &&
               wgf_tilemap_get_terrainset_kind(m, 2) == WGF_TILEMAP_TERRAIN_KIND_EDGE &&
               wgf_tilemap_get_terrainset_kind(m, 3) == WGF_TILEMAP_TERRAIN_KIND_MIXED &&
               wgf_tilemap_get_terrainset_kind(m, 9) == WGF_TILEMAP_TERRAIN_KIND_NONE,
           "their kinds");
    expect(wgf_tilemap_get_terrainset_tileset(m, 0) == 0 && wgf_tilemap_get_terrainset_tileset(m, 2) == 1 &&
               wgf_tilemap_get_terrainset_tileset(m, 3) == 2 && wgf_tilemap_get_terrainset_tileset(m, -1) == -1 &&
               wgf_tilemap_find_terrainset(m, "paths") == 2 && wgf_tilemap_find_terrainset(m, "none") == -1,
           "their tilesets, and one found by name");
    expect(wgf_tilemap_get_terrain_count(m, 0) == 2 && strcmp(wgf_tilemap_get_terrain_name(m, 0, 2), "grass") == 0 &&
               strcmp(wgf_tilemap_get_terrain_name(m, 0, 0), "") == 0 &&
               strcmp(wgf_tilemap_get_terrain_name(m, 0, 3), "") == 0 && wgf_tilemap_find_terrain(m, 0, "dirt") == 1 &&
               wgf_tilemap_find_terrain(m, 0, "lava") == 0 && wgf_tilemap_get_terrain_count(m, 2) == 1,
           "their terrains, numbered from 1");
    expect(wgf_tilemap_get_tile_terrain(m, 0, G(2), 1) == 2 && wgf_tilemap_get_tile_terrain(m, 0, G(2), 7) == 1 &&
               wgf_tilemap_get_tile_terrain(m, 0, G(2), 0) == 0 && wgf_tilemap_get_tile_terrain(m, 2, R(1), 0) == 1 &&
               wgf_tilemap_get_tile_terrain(m, 2, R(1), 2) == 0 && wgf_tilemap_get_tile_terrain(m, 2, R(0), 0) == 0 &&
               wgf_tilemap_get_tile_terrain(m, 0, R(1), 0) == 0 && wgf_tilemap_get_tile_terrain(m, 0, G(2), 8) == 0,
           "a tile's terrain at each place; 0 for a tile the set doesn't hold, or a place past 7");
    expect(wgf_tilemap_get_tile_terrain(m, 1, 1, 1) == 0 && wgf_tilemap_get_tile_terrain(m, 1, 2, 1) == 0 &&
               wgf_tilemap_get_tile_terrain(m, 1, 3, 1) == 1,
           "a tile naming a terrain the set hasn't, one it hasn't, or with too few places: left out");
}

static void test_corners(wgf_actor_t ground)
{
    /* Grass at 2, 1: its corners grass, and so its neighbors' shared corners. */
    const int one[20] = {G(0), G(4), G(12), G(8), G(0), G(0), G(6), G(15), G(9), G(0),
                         G(0), G(2), G(3),  G(1), G(0), G(0), G(0), G(0),  G(0), G(0)};
    /* Then at 3, 1 beside it: the two run into one. */
    const int two[20] = {G(0), G(4), G(12), G(12), G(8), G(0), G(6), G(15), G(15), G(9),
                         G(0), G(2), G(3),  G(3),  G(1), G(0), G(0), G(0),  G(0),  G(0)};
    expect(wgf_tilemap_layer_paint_terrain(ground, 2, 1, 0, 2) && cells_are(ground, one, 20),
           "corners painted: the cell all grass, each neighbor the tile of the corners it shares");
    expect(wgf_tilemap_layer_paint_terrain(ground, 3, 1, 0, 2) && cells_are(ground, two, 20),
           "painted beside: neighbors fit what is around them both");
    expect(wgf_tilemap_layer_paint_terrain(ground, 4, 3, 0, 2) && wgf_tilemap_layer_get_cell(ground, 4, 3) == G(15) &&
               wgf_tilemap_layer_get_cell(ground, 3, 2) == G(7) && wgf_tilemap_layer_get_cell(ground, 4, 2) == G(13),
           "painted at the edge: the neighbors outside left alone");
    expect(wgf_tilemap_layer_paint_terrain(ground, 2, 1, 0, 1) && wgf_tilemap_layer_get_cell(ground, 2, 1) == G(0) &&
               wgf_tilemap_layer_get_cell(ground, 3, 1) == G(6) && wgf_tilemap_layer_get_cell(ground, 1, 1) == G(0),
           "dirt painted back: grass where it is left");

    /* A cell set another way: the terrains read again from the cells. */
    expect(wgf_tilemap_layer_set_cell(ground, 0, 3, G(15), 0), "a cell set");
    expect(wgf_tilemap_layer_paint_terrain(ground, 1, 3, 0, 2) && wgf_tilemap_layer_get_cell(ground, 1, 3) == G(15) &&
               wgf_tilemap_layer_get_cell(ground, 0, 3) == G(15) && wgf_tilemap_layer_get_cell(ground, 0, 2) == G(12) &&
               wgf_tilemap_layer_get_cell(ground, 2, 3) == G(9),
           "painted after: the set cell's grass read from it, kept");

    /* None painted: where all of a cell's corners are none it is emptied; where some are,
     * no tile fits and it is left. */
    expect(wgf_tilemap_layer_paint_terrain(ground, 0, 0, 0, 0) && wgf_tilemap_layer_get_cell(ground, 0, 0) == 0 &&
               wgf_tilemap_layer_get_cell(ground, 1, 0) == G(0) && wgf_tilemap_layer_get_cell(ground, 0, 1) == G(0),
           "none painted: the cell emptied, its neighbors left as no tile fits");
}

static void test_edges(wgf_actor_t roads)
{
    const int one[20] = {0, R(4), 0,     0, 0, R(2), R(15), R(8), 0, 0,
                         0, R(1), 0,     0, 0, 0,    0,     0,    0, 0};
    const int two[20] = {0, R(4), R(4), 0, 0, R(2), R(15), R(15), R(8), 0,
                         0, R(1), R(1), 0, 0, 0,    0,     0,     0,    0};
    expect(wgf_tilemap_layer_paint_terrain(roads, 1, 1, 2, 1) && cells_are(roads, one, 20),
           "edges painted: the cell's four, each neighbor the edge it shares");
    expect(wgf_tilemap_layer_paint_terrain(roads, 2, 1, 2, 1) && cells_are(roads, two, 20),
           "painted beside: the road runs on");
    expect(wgf_tilemap_layer_paint_terrain(roads, 1, 1, 2, 0) && wgf_tilemap_layer_get_cell(roads, 1, 1) == 0 &&
               wgf_tilemap_layer_get_cell(roads, 0, 1) == 0 && wgf_tilemap_layer_get_cell(roads, 2, 1) == R(7) &&
               wgf_tilemap_layer_get_cell(roads, 1, 0) == 0,
           "none painted: the cell and the neighbors left with no road emptied");
}

int main(void)
{
    wgf_tilemap_t m, s;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(wgf_gfx_priv_start(), "setup");
    write_tilesets();
    write_roads();
    write_file("terrain_test/two.png", png_2x2, (int)sizeof(png_2x2));
    write_file("terrain_test/ground.tsj", tilesets, (int)strlen(tilesets));
    write_file("terrain_test/roads.tsj", roads, (int)strlen(roads));
    write_file("terrain_test/map.tmj", map, (int)sizeof(map) - 1);
    write_file("terrain_test/staggered.tmj", staggered, (int)sizeof(staggered) - 1);

    m = wgf_tilemap_create("terrain_test/map.tmj");
    s = wgf_tilemap_create("terrain_test/staggered.tmj");
    wait_for(m);
    wait_for(s);
    expect(wgf_resource_get_status(m) == WGF_RESOURCE_STATUS_READY && wgf_resource_get_status(s) == WGF_RESOURCE_STATUS_READY,
           "READY");
    expect(wgf_tilemap_get_terrainset_count(0) == 0 && wgf_tilemap_find_terrainset(0, "land") == -1,
           "no map: no terrain sets");
    test_read(m);
    {
        const wgf_actor_t stage = wgf_stage2d_create();
        const wgf_actor_t root = wgf_tilemap_instantiate(m, stage);
        const wgf_actor_t ground = wgf_actor_find(root, "ground"), roads = wgf_actor_find(root, "roads");
        const wgf_actor_t other = wgf_actor_find(wgf_tilemap_instantiate(s, stage), "ground");
        test_corners(ground);
        test_edges(roads);
        expect(!wgf_tilemap_layer_paint_terrain(ground, 5, 0, 0, 2) && !wgf_tilemap_layer_paint_terrain(ground, 0, -1, 0, 2) &&
                   !wgf_tilemap_layer_paint_terrain(ground, 0, 0, 0, 3) &&
                   !wgf_tilemap_layer_paint_terrain(ground, 0, 0, 0, -1) &&
                   !wgf_tilemap_layer_paint_terrain(ground, 0, 0, 4, 1) &&
                   !wgf_tilemap_layer_paint_terrain(ground, 0, 0, 3, 1) &&
                   !wgf_tilemap_layer_paint_terrain(root, 0, 0, 0, 1) &&
                   !wgf_tilemap_layer_paint_terrain(other, 0, 0, 0, 1),
               "refused: outside, a terrain or set the map hasn't, a mixed set, not a layer, a staggered map");
        wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    }
    wgf_resource_release(m);
    wgf_resource_release(s);

    {
        const wgf_fs_task_t remove = wgf_fs_rmdir("terrain_test");
        int i;
        for (i = 0; i < 1000 && wgf_fs_task_get_status(remove) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
        wgf_fs_task_destroy(remove);
    }
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
