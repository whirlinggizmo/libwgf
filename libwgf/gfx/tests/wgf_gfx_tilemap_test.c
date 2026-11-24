#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_asset.h"
#include "wgf_core_resource_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_handle.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_sprite_batch.h"
#include "wgf_stage2d.h"
#include "wgf_tilemap.h"
#include "wgf_tilemap_layer.h"
#include "wgf_time.h"

/* Tiled maps, headless: a map with a tileset in a file of its own and one embedded, a group
 * flattened, tile layers as CSV, base64 zlib, and base64 gzip, objects of each shape, and
 * custom properties of the map, a layer, an object, and a tile, read; placed on a stage, its
 * cells drawn as sprite batches' quads, flipped and faded as the map says, animated tiles
 * stepping and held under a paused actor; cells read and set, singly and in bulk; columns
 * and rows from points; an image layer drawn as a sprite of its image, and tile objects as
 * sprites of their tiles, placed, sized, flipped, and hidden as the map says; the map loaded
 * again in place, from its own file and from a tileset file it names, its handles and the
 * layers it still has kept; and the maps it refuses. */

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
    return fabsf(a - b) < 1e-4f;
}

static const unsigned char png_2x2[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00,
    0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81,
    0x34, 0x18, 0x00, 0x00, 0x49, 0xc8, 0x09, 0xf7, 0xf9, 0xab, 0xb6, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

/* Four 1 by 1 tiles: the first animated (itself, then the fourth, a tenth of a second each),
 * the second of class "water". */
static const char tileset[] =
    "{\"name\": \"tiles\", \"image\": \"../art/four.png\", \"tilewidth\": 1, \"tileheight\": 1, \"tilecount\": 4,"
    " \"columns\": 2, \"imagewidth\": 2, \"imageheight\": 2, \"margin\": 0, \"spacing\": 0,"
    " \"tiles\": [{\"id\": 0, \"animation\": [{\"tileid\": 0, \"duration\": 100}, {\"tileid\": 3, \"duration\": 100}]},"
    " {\"id\": 1, \"type\": \"water\", \"properties\": [{\"name\": \"speed\", \"type\": \"float\", \"value\": 1.5}]}]}";

/* The tileset saved again: its third tile of class "lava". */
static const char tileset_saved[] =
    "{\"name\": \"tiles\", \"image\": \"../art/four.png\", \"tilewidth\": 1, \"tileheight\": 1, \"tilecount\": 4,"
    " \"columns\": 2, \"tiles\": [{\"id\": 2, \"type\": \"lava\"}]}";

/* 3 by 2. ground (in a group 10 across, half faded): 1 2 2|flip-x / 5 0 6; walls (zlib) and
 * deco (gzip, hidden): 0 3 0 / 1|diagonal 0 4. */
static const char map[] =
    "{\"type\": \"map\", \"orientation\": \"orthogonal\", \"infinite\": false, \"width\": 3, \"height\": 2,"
    " \"tilewidth\": 1, \"tileheight\": 1, \"backgroundcolor\": \"#80102030\","
    " \"properties\": [{\"name\": \"music\", \"type\": \"string\", \"value\": \"song.ogg\"},"
    "  {\"name\": \"gravity\", \"type\": \"float\", \"value\": 9.5}],"
    " \"tilesets\": [{\"firstgid\": 5, \"name\": \"small\", \"image\": \"../art/four.png\", \"tilewidth\": 1,"
    "  \"tileheight\": 1, \"tilecount\": 4, \"columns\": 2}, {\"firstgid\": 1, \"source\": \"tiles.tsj\"}],"
    " \"layers\": ["
    "  {\"type\": \"group\", \"name\": \"g\", \"offsetx\": 10, \"opacity\": 0.5, \"layers\": ["
    "   {\"type\": \"tilelayer\", \"name\": \"ground\", \"width\": 3, \"height\": 2,"
    "    \"data\": [1, 2, 2147483650, 5, 0, 6]}]},"
    "  {\"type\": \"tilelayer\", \"name\": \"walls\", \"width\": 3, \"height\": 2, \"encoding\": \"base64\","
    "   \"compression\": \"zlib\", \"data\": \"eJxjYGBgYGaAAEYGBgUQzQLEAAGQACk=\","
    "   \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},"
    "  {\"type\": \"tilelayer\", \"name\": \"deco\", \"width\": 3, \"height\": 2, \"encoding\": \"base64\","
    "   \"compression\": \"gzip\", \"visible\": false,"
    "   \"data\": \"H4sIAAAAAAACA2NgYGBgZoAARgYGBRDNAsQAcd5emhgAAAA=\"},"
    "  {\"type\": \"objectgroup\", \"name\": \"things\", \"offsety\": 2, \"objects\": ["
    "   {\"id\": 1, \"name\": \"coin1\", \"type\": \"coin\", \"x\": 3, \"y\": 4, \"width\": 2, \"height\": 2,"
    "    \"rotation\": 90, \"visible\": true, \"properties\": ["
    "     {\"name\": \"value\", \"type\": \"int\", \"value\": 5},"
    " {\"name\": \"label\", \"type\": \"string\", \"value\": \"gold\"},"
    "     {\"name\": \"heavy\", \"type\": \"bool\", \"value\": true},"
    "     {\"name\": \"tint\", \"type\": \"color\", \"value\": \"#ff00ff00\"},"
    "     {\"name\": \"shape\", \"type\": \"class\", \"propertytype\": \"S\", \"value\": {\"r\": 2}}]},"
    "   {\"id\": 2, \"name\": \"start\", \"class\": \"spawn\", \"x\": 1, \"y\": 1, \"point\": true},"
    "   {\"id\": 3, \"x\": 0, \"y\": 0,"
    " \"polygon\": [{\"x\": 0, \"y\": 0}, {\"x\": 2, \"y\": 0}, {\"x\": 1, \"y\": 2}]},"
    "   {\"id\": 4, \"gid\": 2147483654, \"x\": 1, \"y\": 2, \"width\": 1, \"height\": 1, \"visible\": false}]},"
    "  {\"type\": \"imagelayer\", \"name\": \"sky\", \"image\": \"sky.png\", \"opacity\": 0.5}]}";

/* The map saved again: ground's first cell 3, walls gone, a new layer "fresh", and things
 * without its tile object. */
static const char map_saved[] =
    "{\"type\": \"map\", \"width\": 3, \"height\": 2, \"tilewidth\": 1, \"tileheight\": 1,"
    " \"tilesets\": [{\"firstgid\": 1, \"source\": \"tiles.tsj\"}],"
    " \"layers\": ["
    "  {\"type\": \"tilelayer\", \"name\": \"ground\", \"width\": 3, \"height\": 2, \"data\": [3, 2, 2, 1, 0, 1]},"
    "  {\"type\": \"tilelayer\", \"name\": \"fresh\", \"width\": 3, \"height\": 2, \"data\": [0, 0, 0, 0, 0, 4]},"
    "  {\"type\": \"objectgroup\", \"name\": \"things\", \"objects\": []}]}";

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

static void frames(int n, float dt)
{
    while (n-- > 0) wgf_core_priv_part_update(dt);
}

/* A map whose file is `text`: its status once its load settles. */
static wgf_resource_status_t load_status(const char *path, const char *text)
{
    wgf_tilemap_t refused;
    wgf_resource_status_t status;
    write_file(path, text, (int)strlen(text));
    refused = wgf_tilemap_create(path);
    wait_for(refused);
    status = wgf_resource_get_status(refused);
    wgf_resource_release(refused);
    return status;
}

/* Update until `map` has loaded again (wgf_asset_reload). */
static void settle_reload(wgf_tilemap_t map)
{
    const double start = wgf_time_get_seconds();
    while (wgf_core_priv_resource_is_reloading(map) && wgf_time_get_seconds() - start < 30.0) wgf_core_priv_update();
}

/* Loaded again in place: from its file, then from the tileset file it names. */
static void test_reload(wgf_tilemap_t m)
{
    const wgf_actor_t stage = wgf_stage2d_create();
    const wgf_actor_t root = wgf_tilemap_instantiate(m, stage);
    const wgf_actor_t ground = wgf_actor_find(root, "ground"), walls = wgf_actor_find(root, "walls");
    const wgf_actor_t things = wgf_actor_find(root, "things");
    const wgf_actor_t mine = wgf_actor_create(); /* the game's, under a layer */
    const wgf_texture_t texture = wgf_tilemap_get_tileset_texture(m, 0);
    wgf_actor_t fresh;
    wgf_actor_set_parent(mine, things);
    wgf_tilemap_layer_set_cell(ground, 1, 1, 3, 0);
    write_file("tilemap_test/maps/map.tmj", map_saved, (int)sizeof(map_saved) - 1);
    expect(wgf_asset_reload("tilemap_test/maps/map.tmj") == 1 &&
               wgf_resource_get_status(m) == WGF_RESOURCE_STATUS_READY &&
               wgf_tilemap_get_layer_count(m) == 5,
           "loading again: READY all the while, its old contents read until the new are in");
    settle_reload(m);
    fresh = wgf_actor_find(root, "fresh");
    expect(wgf_resource_get_status(m) == WGF_RESOURCE_STATUS_READY && wgf_tilemap_get_layer_count(m) == 3 &&
               wgf_tilemap_get_tileset_texture(m, 0) == texture,
           "loaded again: the new file read, the same texture");
    expect(wgf_handle_is_alive(root) && wgf_handle_is_alive(ground) && wgf_actor_find(root, "ground") == ground &&
               wgf_handle_is_alive(things) && wgf_actor_get_parent(mine) == things && !wgf_handle_is_alive(walls) &&
               fresh != 0 && wgf_actor_get_child_count(root) == 3 && wgf_actor_get_child(root, 1) == fresh,
           "placed again: the root and the layers the file still has kept, the game's actor under one, a layer gone, "
           "a new one in its place");
    expect(wgf_tilemap_layer_get_cell(ground, 0, 0) == 3 && wgf_tilemap_layer_get_cell(ground, 1, 1) == 0 &&
               wgf_tilemap_layer_get_index(fresh) == 1 && wgf_tilemap_layer_get_index(things) == 2 &&
               wgf_tilemap_layer_get_cell(fresh, 2, 1) == 4 && wgf_actor_get_child_count(things) == 1 &&
               wgf_actor_get_child_count(ground) == 1 && wgf_actor_get_position(ground).x == 0.0f,
           "a kept layer shows the new file's cells and place (a cell set at run time gone), its tile objects anew");
    /* The tileset file saved: tile 3 now of class "lava", read through the map that names it. */
    write_file("tilemap_test/maps/tiles.tsj", tileset_saved, (int)sizeof(tileset_saved) - 1);
    expect(wgf_asset_reload("tilemap_test/maps/tiles.tsj") == 1,
           "a tileset file saved: the map naming it, loading again");
    settle_reload(m);
    expect(strcmp(wgf_tilemap_get_tile_class(m, 3), "lava") == 0 && wgf_actor_find(root, "ground") == ground,
           "its new tileset read, the layers kept");
    write_file("tilemap_test/maps/map.tmj", "{\"width\": 0}", 12);
    expect(wgf_asset_reload("tilemap_test/maps/map.tmj") == 1, "a broken file, loading again");
    settle_reload(m);
    expect(wgf_resource_get_status(m) == WGF_RESOURCE_STATUS_READY && wgf_tilemap_get_layer_count(m) == 3 &&
               wgf_actor_find(root, "ground") == ground,
           "a file that doesn't load: what was loaded kept");
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
}

static void test_read(wgf_tilemap_t m)
{
    float points[8];
    expect(wgf_tilemap_get_columns(m) == 3 && wgf_tilemap_get_rows(m) == 2 && wgf_tilemap_get_tile_width(m) == 1 &&
               wgf_tilemap_get_tile_height(m) == 1,
           "its size and tile size");
    expect(wgf_tilemap_get_background_color(m) == 0x10203080u, "its background color, #AARRGGBB as RGBA");
    expect(wgf_tilemap_get_layer_count(m) == 5 && strcmp(wgf_tilemap_get_layer_name(m, 0), "ground") == 0 &&
               strcmp(wgf_tilemap_get_layer_name(m, 3), "things") == 0 && wgf_tilemap_find_layer(m, "deco") == 2 &&
               wgf_tilemap_find_layer(m, "g") == -1 && wgf_tilemap_find_layer(m, "nope") == -1,
           "its layers, the group flattened");
    expect(wgf_tilemap_get_layer_kind(m, 0) == WGF_TILEMAP_LAYER_KIND_TILES &&
               wgf_tilemap_get_layer_kind(m, 3) == WGF_TILEMAP_LAYER_KIND_OBJECTS &&
               wgf_tilemap_get_layer_kind(m, 4) == WGF_TILEMAP_LAYER_KIND_IMAGE &&
               wgf_tilemap_get_layer_kind(m, 5) == WGF_TILEMAP_LAYER_KIND_NONE,
           "the layers' kinds");
    expect(wgf_tilemap_get_tileset_count(m) == 2 && strcmp(wgf_tilemap_get_tileset_name(m, 0), "tiles") == 0 &&
               wgf_tilemap_get_tileset_first_gid(m, 1) == 5 && wgf_tilemap_get_tileset_tile_count(m, 0) == 4 &&
               wgf_tilemap_get_tileset_texture(m, 0) != 0 &&
               wgf_tilemap_get_tileset_texture(m, 0) == wgf_tilemap_get_tileset_texture(m, 1),
           "its tilesets by first gid, one image shared");
    expect(strcmp(wgf_tilemap_get_tile_class(m, 2), "water") == 0 &&
               strcmp(wgf_tilemap_get_tile_class(m, 1), "") == 0 &&
               wgf_tilemap_get_tile_property_count(m, 2) == 1 &&
               strcmp(wgf_tilemap_get_tile_property_name(m, 2, 0), "speed") == 0 &&
               wgf_tilemap_get_tile_property_type(m, 2, "speed") == WGF_TILEMAP_PROPERTY_TYPE_FLOAT &&
               wgf_tilemap_get_tile_property_number(m, 2, "speed") == 1.5 &&
               strcmp(wgf_tilemap_get_tile_property(m, 2, "speed"), "1.5") == 0 &&
               wgf_tilemap_get_tile_property_count(m, 0) == 0,
           "a tile's class and properties");
    expect(wgf_tilemap_get_property_count(m, -1, -1) == 2 &&
               strcmp(wgf_tilemap_get_property_name(m, -1, -1, 1), "gravity") == 0 &&
               strcmp(wgf_tilemap_get_property(m, -1, -1, "music"), "song.ogg") == 0 &&
               wgf_tilemap_get_property_number(m, -1, -1, "gravity") == 9.5 &&
               wgf_tilemap_get_property_type(m, -1, -1, "nope") == WGF_TILEMAP_PROPERTY_TYPE_NONE &&
               strcmp(wgf_tilemap_get_property(m, -1, -1, "nope"), "") == 0,
           "the map's properties");
    expect(wgf_tilemap_get_property_type(m, 1, -1, "solid") == WGF_TILEMAP_PROPERTY_TYPE_BOOL &&
               wgf_tilemap_get_property_number(m, 1, -1, "solid") == 1.0 &&
               strcmp(wgf_tilemap_get_property(m, 1, -1, "solid"), "true") == 0 &&
               wgf_tilemap_get_property_count(m, 0, -1) == 0 && wgf_tilemap_get_property_count(m, -1, 0) == 0,
           "a layer's properties");
    expect(wgf_tilemap_get_object_count(m, 3) == 4 && wgf_tilemap_get_object_count(m, 0) == 0 &&
               wgf_tilemap_get_object_id(m, 3, 0) == 1 && strcmp(wgf_tilemap_get_object_name(m, 3, 0), "coin1") == 0 &&
               strcmp(wgf_tilemap_get_object_class(m, 3, 0), "coin") == 0 &&
               strcmp(wgf_tilemap_get_object_class(m, 3, 1), "spawn") == 0,
           "the objects, their ids, names, and classes (type or class)");
    expect(wgf_tilemap_get_object_shape(m, 3, 0) == WGF_TILEMAP_SHAPE_RECTANGLE &&
               wgf_tilemap_get_object_shape(m, 3, 1) == WGF_TILEMAP_SHAPE_POINT &&
               wgf_tilemap_get_object_shape(m, 3, 2) == WGF_TILEMAP_SHAPE_POLYGON &&
               wgf_tilemap_get_object_shape(m, 3, 3) == WGF_TILEMAP_SHAPE_TILE &&
               wgf_tilemap_get_object_shape(m, 3, 4) == WGF_TILEMAP_SHAPE_NONE,
           "the objects' shapes");
    expect(wgf_tilemap_get_object_position(m, 3, 0).x == 3.0f && wgf_tilemap_get_object_position(m, 3, 0).y == 4.0f &&
               wgf_tilemap_get_object_size(m, 3, 0).x == 2.0f &&
               near(wgf_tilemap_get_object_rotation(m, 3, 0), 1.5707963f) &&
               wgf_tilemap_is_object_visible(m, 3, 0) && !wgf_tilemap_is_object_visible(m, 3, 3) &&
               wgf_tilemap_get_object_tile(m, 3, 3) == 6 && wgf_tilemap_get_object_tile(m, 3, 0) == 0,
           "an object's place, size, turn in radians, visibility, and a tile object's gid, its flips apart");
    expect(wgf_tilemap_get_object_point_count(m, 3, 2) == 3 && wgf_tilemap_get_object_points(m, 3, 2, points, 8) == 6 &&
               points[2] == 2.0f && points[5] == 2.0f && wgf_tilemap_get_object_points(m, 3, 2, points, 3) == 3 &&
               wgf_tilemap_get_object_points(m, 3, 0, points, 8) == 0,
           "a polygon's points, as many as fit");
    expect(wgf_tilemap_get_property_number(m, 3, 0, "value") == 5.0 &&
               strcmp(wgf_tilemap_get_property(m, 3, 0, "value"), "5") == 0 &&
               strcmp(wgf_tilemap_get_property(m, 3, 0, "label"), "gold") == 0 &&
               wgf_tilemap_get_property_number(m, 3, 0, "label") == 0.0 &&
               wgf_tilemap_get_property_number(m, 3, 0, "heavy") == 1.0 &&
               strcmp(wgf_tilemap_get_property(m, 3, 0, "tint"), "#ff00ff00") == 0 &&
               wgf_tilemap_get_property_type(m, 3, 0, "tint") == WGF_TILEMAP_PROPERTY_TYPE_COLOR &&
               strcmp(wgf_tilemap_get_property(m, 3, 0, "shape"), "{\"r\":2}") == 0 &&
               wgf_tilemap_get_property_count(m, 3, 9) == 0,
           "an object's properties: numbers, text, bools, colors, and a class's members as JSON");
}

/* The quad's values in a batch: its position, size, turn, region, and tint. */
static void quad(wgf_actor_t batch, int index, float out[10], int *tint)
{
    wgf_sprite_batch_get_positions(batch, index, out, 2);
    wgf_sprite_batch_get_sizes(batch, index, out + 2, 2);
    wgf_sprite_batch_get_rotations(batch, index, out + 4, 1);
    wgf_sprite_batch_get_regions(batch, index, out + 5, 4);
    wgf_sprite_batch_get_tints(batch, index, tint, 1);
}

static void test_placed(wgf_tilemap_t m)
{
    const wgf_actor_t stage = wgf_stage2d_create();
    const wgf_actor_t root = wgf_tilemap_instantiate(m, stage);
    wgf_actor_t ground, walls, things, tiles, small;
    float q[10];
    int tint = 0, cells[6];
    const int six[6] = {2, 0, 2 + (1 << 28), 0, 7 + (4 << 28), 0};
    expect(root != 0 && strcmp(wgf_actor_get_name(root), "map") == 0 && wgf_actor_get_parent(root) == stage &&
               wgf_actor_get_child_count(root) == 5,
           "placed: a root named for its file, a layer actor each");
    ground = wgf_actor_find(root, "ground");
    walls = wgf_actor_find(root, "walls");
    things = wgf_actor_find(root, "things");
    expect(ground != 0 && wgf_actor_get_kind(ground) == WGF_ACTOR_KIND_TILEMAP_LAYER &&
               wgf_tilemap_layer_get_map(ground) == m && wgf_tilemap_layer_get_index(walls) == 1 &&
               wgf_actor_get_kind(things) == WGF_ACTOR_KIND_TILEMAP_LAYER && wgf_tilemap_layer_get_index(things) == 3,
           "its layers, by name, each its map's and its index");
    expect(wgf_actor_get_position(ground).x == 10.0f && wgf_actor_get_position(things).y == 2.0f &&
               !wgf_actor_is_visible(wgf_actor_find(root, "deco")) && wgf_actor_is_visible(walls),
           "offset (a group's too) and hidden as in Tiled");
    expect(wgf_actor_get_child_count(ground) == 2 && wgf_actor_get_child_count(walls) == 1 &&
               wgf_actor_get_child_count(things) == 1,
           "a sprite batch for each tileset a tile layer uses; a sprite for the tile object");
    tiles = wgf_actor_get_child(ground, 0);
    small = wgf_actor_get_child(ground, 1);
    expect(wgf_actor_get_kind(tiles) == WGF_ACTOR_KIND_SPRITE_BATCH && wgf_sprite_batch_get_count(tiles) == 3 &&
               wgf_sprite_batch_get_count(small) == 2 &&
               wgf_sprite_batch_get_texture(tiles) == wgf_tilemap_get_tileset_texture(m, 0),
           "a quad for each of its cells, in its tileset's batch");
    quad(tiles, 0, q, &tint);
    expect(q[0] == 0.5f && q[1] == 0.5f && q[2] == 1.0f && q[3] == 1.0f && q[5] == 0.0f && q[6] == 0.0f &&
               (unsigned)tint == 0xffffff80u,
           "a cell's quad: its middle, its tile's size and region, faded by its group");
    quad(tiles, 2, q, &tint);
    expect(q[0] == 2.5f && q[2] == -1.0f && q[3] == 1.0f && near(q[4], 0.0f) && q[5] == 1.0f && q[6] == 0.0f,
           "flipped across: mirrored, unturned");
    quad(small, 1, q, &tint);
    expect(q[0] == 2.5f && q[1] == 1.5f && q[5] == 1.0f && q[6] == 0.0f,
           "an embedded tileset's tile, by its first gid");
    quad(wgf_actor_get_child(walls, 0), 1, q, &tint);
    expect(q[0] == 0.5f && q[1] == 1.5f && q[2] == -1.0f && near(q[4], -1.5707963f) && (unsigned)tint == 0xffffffffu,
           "flipped across its diagonal: mirrored and turned a quarter");

    /* Animation: tile 1 shows itself, then tile 4 (region 1, 1), a tenth of a second each. */
    quad(tiles, 0, q, &tint);
    expect(q[5] == 0.0f && q[6] == 0.0f, "an animated tile at its first frame");
    frames(3, 0.04f);
    quad(tiles, 0, q, &tint);
    expect(q[5] == 1.0f && q[6] == 1.0f, "a tenth of a second on: its second frame");
    wgf_actor_set_paused(root, true);
    frames(5, 0.04f);
    quad(tiles, 0, q, &tint);
    expect(q[5] == 1.0f && q[6] == 1.0f, "under a paused actor: held");
    wgf_actor_set_paused(root, false);
    frames(3, 0.04f);
    quad(tiles, 0, q, &tint);
    expect(q[5] == 0.0f && q[6] == 0.0f, "unpaused: on round to its first");

    /* Cells read and set. */
    expect(wgf_tilemap_layer_get_cell(ground, 2, 0) == 2 &&
               wgf_tilemap_layer_get_cell_flips(ground, 2, 0) == WGF_TILEMAP_FLIP_X &&
               wgf_tilemap_layer_get_cell(walls, 0, 1) == 1 &&
               wgf_tilemap_layer_get_cell_flips(walls, 0, 1) == WGF_TILEMAP_FLIP_DIAGONAL &&
               wgf_tilemap_layer_get_cell(ground, 1, 1) == 0 && wgf_tilemap_layer_get_cell(ground, 3, 0) == 0 &&
               wgf_tilemap_layer_get_cell(things, 0, 0) == 0,
           "a cell's tile and flips; 0 empty, outside, or in a layer of objects");
    expect(wgf_tilemap_layer_set_cell(ground, 1, 1, 7, WGF_TILEMAP_FLIP_Y) &&
               wgf_tilemap_layer_get_cell(ground, 1, 1) == 7 &&
               wgf_sprite_batch_get_count(small) == 3,
           "an empty cell set: a quad more in its tileset's batch");
    expect(wgf_tilemap_layer_set_cell(ground, 0, 0, 6, 0) && wgf_sprite_batch_get_count(tiles) == 2 &&
               wgf_sprite_batch_get_count(small) == 4,
           "a cell set to another tileset's tile: moved to its batch");
    quad(tiles, 0, q, &tint);
    expect(q[0] == 2.5f && q[2] == -1.0f, "the batch's last quad moved into the hole");
    expect(wgf_tilemap_layer_set_cell(ground, 1, 0, 0, 0) && wgf_sprite_batch_get_count(tiles) == 1 &&
               wgf_tilemap_layer_get_cell(ground, 1, 0) == 0,
           "a cell emptied");
    expect(!wgf_tilemap_layer_set_cell(ground, 3, 0, 1, 0) && !wgf_tilemap_layer_set_cell(ground, 0, 0, 9, 0) &&
               !wgf_tilemap_layer_set_cell(ground, 0, 0, 1, 8) && !wgf_tilemap_layer_set_cell(things, 0, 0, 1, 0) &&
               !wgf_tilemap_layer_set_cell(root, 0, 0, 1, 0),
           "set outside, a gid no tileset holds, flips past three bits, no cells, not a layer: refused");
    expect(wgf_tilemap_layer_get_cells(ground, cells, 6) == 6 && cells[0] == 6 && cells[2] == 2 + (1 << 28) &&
               cells[4] == 7 + (2 << 28) && wgf_tilemap_layer_get_cells(ground, cells, 2) == 2,
           "every cell in bulk, a cell's flips times 0x10000000");
    expect(wgf_tilemap_layer_set_cells(ground, six, 6) && wgf_tilemap_layer_get_cell(ground, 1, 1) == 7 &&
               wgf_tilemap_layer_get_cell_flips(ground, 1, 1) == WGF_TILEMAP_FLIP_DIAGONAL &&
               wgf_sprite_batch_get_count(tiles) == 2 && wgf_sprite_batch_get_count(small) == 1,
           "every cell set at once");
    expect(!wgf_tilemap_layer_set_cells(ground, six, 5) && !wgf_tilemap_layer_set_cells(things, six, 6),
           "a count other than its cells', or no cells: refused");

    /* Image layers and tile objects. */
    {
        const wgf_actor_t sky = wgf_actor_find(root, "sky");
        const wgf_actor_t image = wgf_actor_get_child(sky, 0);
        const wgf_actor_t tile = wgf_tilemap_layer_get_object_actor(things, 3);
        expect(wgf_actor_get_child_count(sky) == 1 && wgf_actor_get_kind(image) == WGF_ACTOR_KIND_SPRITE &&
                   wgf_sprite_get_texture(image) == wgf_tilemap_get_layer_texture(m, 4) &&
                   wgf_tilemap_get_layer_texture(m, 4) != 0 && wgf_tilemap_get_layer_texture(m, 0) == 0 &&
                   wgf_sprite_get_pivot(image).x == 0.0f && wgf_sprite_get_pivot(image).y == 0.0f &&
                   wgf_sprite_get_tint(image) == 0xffffff80u,
               "an image layer: a sprite of its image, its top-left on the layer's, faded");
        expect(tile != 0 && wgf_actor_get_parent(tile) == things && wgf_actor_get_kind(tile) == WGF_ACTOR_KIND_SPRITE &&
                   wgf_sprite_get_texture(tile) == wgf_tilemap_get_tileset_texture(m, 0) &&
                   wgf_tilemap_layer_get_object_actor(things, 0) == 0 &&
                   wgf_tilemap_layer_get_object_actor(things, 9) == 0 &&
                   wgf_tilemap_layer_get_object_actor(ground, 0) == 0,
               "a tile object: a sprite of its tileset under the layer; none for other shapes");
        expect(wgf_sprite_get_source(tile).x == 1.0f && wgf_sprite_get_source(tile).y == 0.0f &&
                   wgf_sprite_get_size(tile).x == 1.0f && wgf_actor_get_position(tile).x == 1.5f &&
                   wgf_actor_get_position(tile).y == 1.5f && wgf_actor_get_scale(tile).x == -1.0f &&
                   !wgf_actor_is_visible(tile) && wgf_tilemap_get_object_tile_flips(m, 3, 3) == WGF_TILEMAP_FLIP_X &&
                   wgf_tilemap_get_object_middle(m, 3, 3).x == 1.5f && wgf_tilemap_get_object_middle(m, 3, 3).y == 1.5f,
               "its tile's region, its size, its middle up from its bottom-left place, mirrored, hidden");
        expect(wgf_tilemap_get_object_middle(m, 3, 1).x == 1.0f &&
                   near(wgf_tilemap_get_object_middle(m, 3, 0).x, 2.0f) &&
                   near(wgf_tilemap_get_object_middle(m, 3, 0).y, 5.0f),
               "other objects' middles: a point's point, a turned rectangle's");
        wgf_actor_destroy(tile, WGF_ACTOR_DESTROY_CHILDREN);
        expect(wgf_tilemap_layer_get_object_actor(things, 3) == 0, "a tile object's sprite destroyed: none");
    }

    /* Columns and rows from points in the map's units, through the layer's offset. */
    expect(wgf_tilemap_layer_get_column_at(ground, 11.5f, 0.2f) == 1 &&
               wgf_tilemap_layer_get_row_at(ground, 11.5f, 1.2f) == 1 &&
               wgf_tilemap_layer_get_column_at(ground, 9.5f, 0.0f) == -1 &&
               wgf_tilemap_layer_get_column_at(walls, 1.5f, 0.0f) == 1 &&
               wgf_tilemap_layer_get_column_at(root, 1.0f, 0.0f) == 0,
           "a column and a row from a point, outside too");
    expect(wgf_tilemap_layer_get_cell_middle(ground, 1, 1).x == 11.5f &&
               wgf_tilemap_layer_get_cell_middle(ground, 1, 1).y == 1.5f &&
               wgf_tilemap_get_cell_middle(m, 1, 1).x == 1.5f && wgf_tilemap_get_column_at(m, 2.5f, 1.5f) == 2 &&
               wgf_tilemap_get_row_at(m, 2.5f, 1.5f) == 1 && wgf_tilemap_layer_get_cell_middle(root, 0, 0).x == 0.0f,
           "a cell's middle, through the layer's offset and in the map's pixels");
    expect(wgf_tilemap_get_orientation(m) == WGF_TILEMAP_ORIENTATION_ORTHOGONAL && !wgf_tilemap_is_infinite(m) &&
               wgf_tilemap_get_origin_column(m) == 0 && wgf_tilemap_get_pixel_width(m) == 3 &&
               wgf_tilemap_get_pixel_height(m) == 2,
           "orthogonal, finite, its size in pixels");

    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
}

int main(void)
{
    wgf_tilemap_t m;

    wgf_core_priv_init();
    wgf_asset_enable_reload(); /* what wgf serve does, to load its files again */
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(wgf_gfx_priv_start(), "setup");
    write_file("tilemap_test/art/four.png", png_2x2, (int)sizeof(png_2x2));
    write_file("tilemap_test/maps/tiles.tsj", tileset, (int)sizeof(tileset) - 1);
    write_file("tilemap_test/maps/map.tmj", map, (int)sizeof(map) - 1);
    write_file("tilemap_test/maps/sky.png", png_2x2, (int)sizeof(png_2x2));

    m = wgf_tilemap_create("tilemap_test/maps/map.tmj");
    expect(m != 0 && wgf_resource_get_status(m) == WGF_RESOURCE_STATUS_PENDING && wgf_tilemap_get_columns(m) == 0 &&
               wgf_tilemap_instantiate(m, 0) == 0,
           "a map, PENDING at once: nothing read, nothing placed");
    wait_for(m);
    expect(wgf_resource_get_status(m) == WGF_RESOURCE_STATUS_READY, "READY");
    test_read(m);
    test_placed(m);

    {
        const wgf_actor_t root = wgf_tilemap_instantiate(m, 0);
        const wgf_actor_t ground = wgf_actor_get_child(root, 0);
        wgf_resource_release(m);
        expect(wgf_handle_is_alive(m) && wgf_tilemap_layer_get_map(ground) == m && wgf_tilemap_get_columns(m) == 3,
               "a placed layer holds a reference to its map");
        test_reload(m);
        wgf_actor_destroy(root, WGF_ACTOR_DESTROY_CHILDREN);
        expect(!wgf_handle_is_alive(m), "its last layer gone, the map with it");
    }

    expect(load_status("tilemap_test/iso.tmj", "{\"orientation\": \"octagonal\", \"width\": 1, \"height\": 1,"
                                               " \"tilewidth\": 1, \"tileheight\": 1}") == WGF_RESOURCE_STATUS_FAILED,
           "an orientation not Tiled's: FAILED");
    expect(load_status("tilemap_test/inf.tmj", "{\"infinite\": true, \"width\": 1, \"height\": 1, \"tilewidth\": 1,"
                                               " \"tileheight\": 1,"
                                               " \"layers\": [{\"type\": \"tilelayer\", \"name\": \"a\","
                                               " \"chunks\": [{\"x\": 0, \"y\": 0, \"width\": 0, \"height\": 1,"
                                               " \"data\": []}]}]}") == WGF_RESOURCE_STATUS_FAILED,
           "an infinite map's chunk out of range: FAILED");
    expect(load_status("tilemap_test/chunks.tmj", "{\"width\": 1, \"height\": 1, \"tilewidth\": 1, \"tileheight\": 1,"
                                                  " \"layers\": [{\"type\": \"tilelayer\","
                                                  " \"name\": \"a\", \"width\": 1,"
                                                  " \"height\": 1, \"chunks\": []}]}") == WGF_RESOURCE_STATUS_FAILED,
           "chunks in a finite map: FAILED");
    expect(load_status("tilemap_test/tsx.tmj", "{\"width\": 1, \"height\": 1, \"tilewidth\": 1, \"tileheight\": 1,"
                                               " \"tilesets\": [{\"firstgid\": 1, \"source\": \"a.tsx\"}]}") ==
               WGF_RESOURCE_STATUS_FAILED,
           "a tileset as XML: FAILED");
    expect(load_status("tilemap_test/short.tmj", "{\"width\": 2, \"height\": 1, \"tilewidth\": 1, \"tileheight\": 1,"
                                                 " \"layers\": [{\"type\": \"tilelayer\","
                                                 " \"name\": \"a\", \"width\": 2,"
                                                 " \"height\": 1, \"data\": [1]}]}") == WGF_RESOURCE_STATUS_FAILED,
           "a layer with too few cells: FAILED");
    expect(load_status("tilemap_test/empty.tmj", "{\"width\": 2, \"height\": 1,"
                                                 " \"tilewidth\": 4, \"tileheight\": 4}") ==
               WGF_RESOURCE_STATUS_READY,
           "a map with nothing in it: READY");

    {
        const wgf_fs_task_t remove = wgf_fs_rmdir("tilemap_test");
        int i;
        for (i = 0; i < 1000 && wgf_fs_task_get_status(remove) == WGF_FS_TASK_STATUS_PENDING; i++)
            wgf_core_priv_update();
        wgf_fs_task_destroy(remove);
    }
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
