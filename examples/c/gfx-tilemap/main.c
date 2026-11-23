#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_behavior.h"
#include "wgf_arcade2d.h"
#include "wgf_camera2d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_mouse.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_scene.h"
#include "wgf_stage2d.h"
#include "wgf_tilemap.h"
#include "wgf_tilemap_layer.h"
#include "wgf_window.h"
#include "wgf_world.h"

/* A Tiled map: maps/island.tmj, its tileset maps/tiles.tsj over textures/tiles.png, an
 * embedded one of animated water (textures/water.png), and maps/terrain.tsj, a terrain (Wang)
 * set of a dirt trail; Tiled's JSON written by hand.
 *   - its tile layers drawn as sprite batches (one for each tileset a layer uses), the water
 *     animated as its tileset says, the props layer's rocks flipped and turned by Tiled's
 *     flip bits
 *   - its object layer made actors: each object whose class is a prefab of
 *     scenes/island.scene (trees, a flag, coins, the hero) spawned there, its custom
 *     properties (a coin's value, the hero's speed) its behaviors' parameters; a tile object
 *     (a boulder, mirrored) drawn as its tile
 *   - an image layer (textures/clouds.png) drawn over it all, drifting: a layer is an actor
 *   - the hero a 2D body sliding against the props layer's cells (its solid grid), wearing a
 *     trail: dirt painted into the trail layer at each cell it enters, as Tiled's Terrain
 *     brush paints, the cells around taking the tiles that fit
 *   - a coin taken turns the path under it to sand: a cell set, found from a point
 *
 *   arrows or WASD   walk (left alone, the hero walks the path by itself)
 *   wheel            zoom
 *   Esc              quit, where quitting means anything */

#define MAP_PATH "maps/island.tmj"
#define SCENE_PATH "scenes/island.scene"
#define MAX_COINS 32
#define SAND 2 /* tiles.tsj's second tile */

static struct {
    wgf_actor_t stage, camera, root, ground, trail, things, hero, clouds;
    wgf_tilemap_t map;
    wgf_scene_t scene;
    wgf_actor_t coins[MAX_COINS];
    int coin_count, taken, score, worth, sanded;
    int trail_set, dirt, painted, last_column, last_row; /* the trail's terrain set and terrain, cells painted */
    float speed, walk; /* the hero's speed (its object's property), and its way when left alone */
    float zoom;        /* map pixels seen top to bottom */
    bool played;       /* a key pressed: no longer walking by itself */
    wgf_color_t shade, text, dim;
} g;

static void init(void *user)
{
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(24, 32, 44, 255));
    g.shade = wgf_color_make(18, 20, 28, 190);
    g.text = wgf_color_make(235, 238, 245, 255);
    g.dim = wgf_color_make(140, 146, 158, 255);
    g.stage = wgf_stage2d_create();
    g.camera = wgf_camera2d_create();
    wgf_stage2d_set_camera(g.stage, g.camera);
    g.map = wgf_tilemap_create(MAP_PATH);
    g.scene = wgf_scene_create(SCENE_PATH);
    g.zoom = 200.0f;
    g.walk = 1.0f;
}

static double coin_value(wgf_actor_t coin)
{
    return wgf_behavior_get_param_number(coin, wgf_actor_find_behavior(coin, "Coin"), "value");
}

/* Once the map and the scene are READY: the map placed, its objects spawned, the hero given
 * the props layer as its walls. */
static void place(void)
{
    int i;
    g.root = wgf_tilemap_instantiate(g.map, g.stage);
    g.ground = wgf_actor_find(g.root, "ground");
    g.trail = wgf_actor_find(g.root, "trail");
    g.clouds = wgf_actor_find(g.root, "clouds");
    g.trail_set = wgf_tilemap_find_terrainset(g.map, "trail");
    g.dirt = wgf_tilemap_find_terrain(g.map, g.trail_set, "dirt");
    g.last_column = g.last_row = -1;
    g.things = wgf_actor_find(g.root, "things");
    wgf_scene_spawn_objects(g.scene, g.things);
    g.hero = wgf_actor_find(g.things, "hero");
    g.speed = (float)wgf_behavior_get_param_number(g.hero, wgf_actor_find_behavior(g.hero, "Hero"), "speed");
    if (g.speed <= 0.0f) g.speed = 40.0f;
    /* The bodies under the object layer (the hero) slide against the props layer's rocks. */
    wgf_arcade2d_set_grid_from_layer(g.things, wgf_actor_find(g.root, "props"));
    g.coin_count = wgf_actor_find_with_behavior("Coin", g.coins, MAX_COINS);
    for (i = 0; i < g.coin_count; i++) g.worth += (int)coin_value(g.coins[i]);
    if (wgf_tilemap_get_background_color(g.map) != 0) {
        wgf_render_set_clear_color(wgf_tilemap_get_background_color(g.map));
    }
}

static void walk(void)
{
    float x = 0.0f, y = 0.0f;
    if (wgf_keyboard_is_down(WGF_KEY_LEFT) || wgf_keyboard_is_down(WGF_KEY_A)) x -= 1.0f;
    if (wgf_keyboard_is_down(WGF_KEY_RIGHT) || wgf_keyboard_is_down(WGF_KEY_D)) x += 1.0f;
    if (wgf_keyboard_is_down(WGF_KEY_UP) || wgf_keyboard_is_down(WGF_KEY_W)) y -= 1.0f;
    if (wgf_keyboard_is_down(WGF_KEY_DOWN) || wgf_keyboard_is_down(WGF_KEY_S)) y += 1.0f;
    if (x != 0.0f || y != 0.0f) g.played = true;
    if (!g.played) { /* along the path, turning at the rocks */
        if (wgf_arcade2d_is_blocked_right(g.hero)) g.walk = -1.0f;
        if (wgf_arcade2d_is_blocked_left(g.hero)) g.walk = 1.0f;
        x = g.walk;
    }
    if (x != 0.0f && y != 0.0f) {
        x *= 0.7071f;
        y *= 0.7071f;
    }
    wgf_arcade2d_set_velocity(g.hero, x * g.speed, y * g.speed);
}

/* A coin under the hero taken: its value scored, the cell under it sanded. */
static void take_coins(void)
{
    const wgf_vec3_t at = wgf_actor_get_position(g.hero);
    int i;
    for (i = 0; i < g.coin_count; i++) {
        const wgf_vec3_t coin = wgf_actor_get_position(g.coins[i]);
        const float dx = coin.x - at.x, dy = coin.y - at.y;
        int column, row;
        if (!wgf_actor_is_visible(g.coins[i]) || dx * dx + dy * dy > 10.0f * 10.0f) continue;
        wgf_actor_set_visible(g.coins[i], false);
        g.taken++;
        g.score += (int)coin_value(g.coins[i]);
        /* The coin is under the object layer and the ground under the map's root, both at
         * the map's pixels, so the coin's point is the ground's too. */
        column = wgf_tilemap_layer_get_column_at(g.ground, coin.x, coin.y);
        row = wgf_tilemap_layer_get_row_at(g.ground, coin.x, coin.y);
        if (wgf_tilemap_layer_get_cell(g.ground, column, row) != SAND &&
            wgf_tilemap_layer_set_cell(g.ground, column, row, SAND, WGF_TILEMAP_FLIP_NONE)) {
            g.sanded++;
        }
    }
}

/* Dirt painted where the hero is, once a cell: a trail, its edges fitted to the grass. */
static void wear_trail(void)
{
    const wgf_vec3_t at = wgf_actor_get_position(g.hero);
    const int column = wgf_tilemap_layer_get_column_at(g.trail, at.x, at.y);
    const int row = wgf_tilemap_layer_get_row_at(g.trail, at.x, at.y);
    if (column == g.last_column && row == g.last_row) return;
    g.last_column = column, g.last_row = row;
    if (wgf_tilemap_layer_paint_terrain(g.trail, column, row, g.trail_set, g.dirt)) g.painted++;
}

/* The camera on the hero (the map's middle before), kept over the map. */
static void place_camera(void)
{
    const float height = (float)wgf_window_get_height(), width = (float)wgf_window_get_width();
    const float map_w = (float)(wgf_tilemap_get_columns(g.map) * wgf_tilemap_get_tile_width(g.map));
    const float map_h = (float)(wgf_tilemap_get_rows(g.map) * wgf_tilemap_get_tile_height(g.map));
    const float seen_h = g.zoom, seen_w = height > 0.0f ? g.zoom * width / height : g.zoom;
    wgf_vec3_t at = g.hero != 0 ? wgf_actor_get_drawn_position(g.hero) : wgf_vec3_make(map_w * 0.5f, map_h * 0.5f, 0);
    at.x = map_w > seen_w ? fminf(fmaxf(at.x, seen_w * 0.5f), map_w - seen_w * 0.5f) : map_w * 0.5f;
    at.y = map_h > seen_h ? fminf(fmaxf(at.y, seen_h * 0.5f), map_h - seen_h * 0.5f) : map_h * 0.5f;
    wgf_actor_set_position(g.camera, at.x, at.y, 0.0f);
    if (height > 0.0f) wgf_camera2d_set_zoom(g.camera, height / g.zoom);
}

static void frame(void *user)
{
    const float width = (float)wgf_window_get_width();
    int events[4 * 64];
    char line[160];
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    while (wgf_world_take_events(events, 4 * 64) > 0) {
    }
    if (g.root == 0 && wgf_resource_get_status(g.map) == WGF_RESOURCE_STATUS_READY &&
        wgf_resource_get_status(g.scene) == WGF_RESOURCE_STATUS_READY) {
        place();
    }
    if (g.hero != 0) {
        walk();
        take_coins();
        wear_trail();
    }
    if (g.clouds != 0) { /* the image layer drifting across, round again past the map's right */
        const wgf_vec3_t at = wgf_actor_get_position(g.clouds);
        const float x = at.x + 0.25f;
        wgf_actor_set_position(g.clouds, x > (float)wgf_tilemap_get_pixel_width(g.map) ? -192.0f : x, at.y, 0.0f);
    }
    if (wgf_mouse_get_wheel().y != 0.0f) { /* fewer pixels seen = closer */
        g.zoom -= wgf_mouse_get_wheel().y * 24.0f;
        g.zoom = g.zoom < 96.0f ? 96.0f : g.zoom > 320.0f ? 320.0f : g.zoom;
    }
    place_camera();

    wgf_stage2d_draw(g.stage);
    wgf_draw_rectangle(0, 0, width, 88, g.shade);
    snprintf(line, sizeof(line), "libwgf tilemap: %s, a Tiled map on sprite batches",
             g.root != 0 ? wgf_tilemap_get_property(g.map, -1, -1, "title") : "loading");
    wgf_draw_text(0, line, 20, 20, 20, g.text);
    snprintf(line, sizeof(line),
             "coins: %d of %d   worth: %d of %d   cells set: %d   trail painted: %d   layers: %d   tilesets: %d",
             g.taken, g.coin_count, g.score, g.worth, g.sanded, g.painted, wgf_tilemap_get_layer_count(g.map),
             wgf_tilemap_get_tileset_count(g.map));
    wgf_draw_text(0, line, 20, 46, 15, g.dim);
    wgf_draw_text(0, "arrows or WASD to walk (it walks by itself when left alone), wheel to zoom", 20, 68, 15, g.dim);
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
