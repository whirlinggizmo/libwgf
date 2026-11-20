#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_behavior.h"
#include "wgf_body2d.h"
#include "wgf_component.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_scene.h"
#include "wgf_stage2d.h"
#include "wgf_tilemap.h"
#include "wgf_tilemap_layer.h"
#include "wgf_time.h"

/* A placed Tiled map in the ecs, headless: an object layer's objects spawned as a scene's
 * prefabs by class, at their middles, turned, named, hidden, and their properties set on
 * their behaviors (a tile object's sprite replaced by its prefab); and a tile layer made a 2D
 * body's solid grid, the body standing on it. */

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
    return fabsf(a - b) < 1e-3f;
}

/* 4 by 3 of 16-pixel tiles, its bottom row walls; four objects, three of class coin. */
static const char map_text[] =
    "{\"width\": 4, \"height\": 3, \"tilewidth\": 16, \"tileheight\": 16,"
    " \"tilesets\": [{\"firstgid\": 1, \"name\": \"t\", \"image\": \"four.png\", \"tilewidth\": 16, \"tileheight\": 16,"
    "  \"tilecount\": 4, \"columns\": 2}],"
    " \"layers\": ["
    "  {\"type\": \"tilelayer\", \"name\": \"walls\", \"width\": 4, \"height\": 3,"
    "   \"data\": [0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 2]},"
    "  {\"type\": \"objectgroup\", \"name\": \"things\", \"objects\": ["
    "   {\"id\": 1, \"name\": \"gold\", \"type\": \"coin\", \"x\": 16, \"y\": 16, \"width\": 16, \"height\": 16,"
    "    \"properties\": [{\"name\": \"value\", \"type\": \"int\", \"value\": 5},"
    "     {\"name\": \"label\", \"type\": \"string\", \"value\": \"big\"}]},"
    "   {\"id\": 2, \"class\": \"coin\", \"gid\": 1, \"x\": 32, \"y\": 32, \"width\": 16, \"height\": 16, \"visible\": false},"
    "   {\"id\": 3, \"name\": \"start\", \"type\": \"spawn\", \"point\": true, \"x\": 8, \"y\": 8},"
    "   {\"id\": 4, \"type\": \"coin\", \"x\": 0, \"y\": 0, \"width\": 16, \"height\": 16, \"rotation\": 90}]}]}";

static const char scene_text[] = "wgf-scene 2\n"
                                 "prefab coin\n"
                                 "  behavior name=Coin\n"
                                 "  behavior name=Spin\n"
                                 "end\n";

static void write_file(const char *path, const char *text)
{
    const wgf_fs_task_t task = wgf_fs_write_text(path, text);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

static void wait_for(wgf_handle_t resource)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(resource) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
}

static void step(float dt)
{
    wgf_core_priv_part_tick_begin();
    wgf_core_priv_part_tick(dt);
}

int main(void)
{
    wgf_tilemap_t map;
    wgf_scene_t scene;
    wgf_actor_t stage, root, walls, things, gold, hidden, turned, hero;
    int i;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(wgf_gfx_priv_start(), "gfx");
    write_file("tilemap_ecs/level.tmj", map_text);
    write_file("tilemap_ecs/coins.scene", scene_text);
    map = wgf_tilemap_create("tilemap_ecs/level.tmj");
    scene = wgf_scene_create("tilemap_ecs/coins.scene");
    stage = wgf_stage2d_create();
    wait_for(scene);
    wait_for(map);
    expect(wgf_resource_get_status(map) == WGF_RESOURCE_STATUS_READY &&
               wgf_resource_get_status(scene) == WGF_RESOURCE_STATUS_READY,
           "the map and the scene READY");
    root = wgf_tilemap_instantiate(map, stage);
    walls = wgf_actor_find(root, "walls");
    things = wgf_actor_find(root, "things");

    expect(wgf_tilemap_layer_get_object_actor(things, 1) != 0 && wgf_actor_get_child_count(things) == 1,
           "placed: the tile object drawn as its tile");
    expect(wgf_scene_spawn_objects(scene, things) == 3 && wgf_actor_get_child_count(things) == 3 &&
               wgf_tilemap_layer_get_object_actor(things, 1) == 0,
           "the objects of a class the scene has a prefab of spawned, under their layer; the prefab in place of the "
           "tile object's sprite");
    gold = wgf_actor_find(things, "gold");
    hidden = wgf_actor_get_child(things, 1);
    turned = wgf_actor_get_child(things, 2);
    expect(gold != 0 && near(wgf_actor_get_position(gold).x, 24) && near(wgf_actor_get_position(gold).y, 24) &&
               wgf_actor_is_visible(gold),
           "named for its object, at its middle");
    expect(wgf_actor_get_behavior_count(gold) == 2 &&
               strcmp(wgf_behavior_get_param(gold, wgf_actor_find_behavior(gold, "Coin"), "label"), "big") == 0 &&
               wgf_behavior_get_param_number(gold, wgf_actor_find_behavior(gold, "Spin"), "value") == 5.0,
           "its properties its behaviors' parameters");
    expect(strcmp(wgf_actor_get_name(hidden), "") == 0 && !wgf_actor_is_visible(hidden) &&
               near(wgf_actor_get_position(hidden).x, 40) && near(wgf_actor_get_position(hidden).y, 24),
           "a tile object at its middle, up from its bottom-left; hidden as in Tiled");
    expect(near(wgf_actor_get_position(turned).x, -8) && near(wgf_actor_get_position(turned).y, 8) &&
               near(wgf_actor_get_rotation(turned).z, 1.5707963f),
           "a turned object at its turned middle, turned");
    expect(wgf_scene_spawn_objects(scene, walls) == 0 && wgf_scene_spawn_objects(scene, root) == 0 &&
               wgf_scene_spawn_objects(0, things) == 0,
           "a layer of tiles, an actor that isn't a layer, no scene: none");

    hero = wgf_actor_create();
    wgf_actor_set_parent(hero, root);
    wgf_actor_set_position(hero, 20, 0, 0);
    wgf_actor_add_component(hero, WGF_COMPONENT_BODY2D);
    wgf_body2d_set_size(hero, 10, 10);
    wgf_body2d_set_gravity(hero, 1000);
    expect(wgf_body2d_set_grid_from_layer(root, walls) && wgf_body2d_get_grid_cell(root, 0, 2) == 1 &&
               wgf_body2d_get_grid_cell(root, 3, 2) == 1 && wgf_body2d_get_grid_cell(root, 0, 1) == 0,
           "a tile layer's filled cells the solid grid");
    for (i = 0; i < 60; i++) step(1.0f / 60.0f);
    expect(near(wgf_actor_get_position(hero).y, 27) && wgf_body2d_is_grounded(hero), "a body standing on the layer's cells");
    expect(!wgf_body2d_set_grid_from_layer(root, things) && !wgf_body2d_set_grid_from_layer(root, root) &&
               !wgf_body2d_set_grid_from_layer(0, walls),
           "a layer without cells, an actor that isn't a layer, no parent: refused");

    wgf_actor_set_position(walls, 0, 16, 0); /* the layer moved down a row */
    expect(wgf_body2d_set_grid_from_layer(root, walls), "set again");
    for (i = 0; i < 60; i++) step(1.0f / 60.0f);
    expect(near(wgf_actor_get_position(hero).y, 43), "the grid where the layer is in its parent");

    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_resource_release(map);
    wgf_resource_release(scene);
    {
        const wgf_fs_task_t remove = wgf_fs_rmdir("tilemap_ecs");
        for (i = 0; i < 1000 && wgf_fs_task_get_status(remove) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
        wgf_fs_task_destroy(remove);
    }
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
