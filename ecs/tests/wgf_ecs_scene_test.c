#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_behavior.h"
#include "wgf_bounds.h"
#include "wgf_canvas.h"
#include "wgf_collider.h"
#include "wgf_color.h"
#include "wgf_core_priv.h"
#include "wgf_ecs.h"
#include "wgf_entity.h"
#include "wgf_fs.h"
#include "wgf_lifetime.h"
#include "wgf_log.h"
#include "wgf_motion.h"
#include "wgf_node.h"
#include "wgf_resource.h"
#include "wgf_scene.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_stage.h"
#include "wgf_shape2d.h"
#include "wgf_text.h"
#include "wgf_time.h"

/* Scenes, headless: a file loaded as a resource, READY with its entities and prefabs;
 * instantiated, each entity as the file says, a behavior's CREATED raised; a prefab
 * spawned, and one starting `from` another; each refusal FAILED; the same path the same
 * scene; and the dump loaded again making the same world. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static bool near(float a, float b) { return fabsf(a - b) < 1e-4f; }

static void write_file(const char *path, const char *text)
{
    const wgf_fs_task_t task = wgf_fs_write_text(path, text);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

/* The scene at `path`, with `text` written there first, updated until it isn't pending. */
static wgf_scene_t load(const char *path, const char *text)
{
    wgf_scene_t scene;
    double start;
    write_file(path, text);
    scene = wgf_scene_create(path);
    start = wgf_time_get_seconds();
    while (wgf_resource_get_status(scene) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0)
        wgf_core_priv_update();
    return scene;
}

static bool refused(const char *name, const char *text)
{
    char path[64];
    wgf_scene_t scene;
    bool failed;
    snprintf(path, sizeof(path), "scenes/bad_%s.scene", name);
    scene = load(path, text);
    failed = wgf_resource_get_status(scene) == WGF_RESOURCE_STATUS_FAILED && wgf_scene_instantiate(scene, 0) == 0;
    wgf_resource_release(scene);
    return failed;
}

static const char *field_scene =
    "wgf-scene 1\n"
    "# the field\n"
    "prefab rock\n"
    "  motion velocity=0,-20,0 spin=0,0,1\n"
    "  bounds rect=0,0,800,600 mode=wrap margin=30\n"
    "  collider radius=24 layer=2 mask=5\n"
    "  behavior name=Rock size=3 label=\"big \\\"one\\\"\"\n"
    "  shape2d polygon=0,0,10,0,10,10 outline=2 color=#80FF00\n"
    "end\n"
    "prefab small_rock\n"
    "  from rock\n"
    "  collider radius=8\n"
    "  behavior size=1\n"
    "end\n"
    "entity ship\n"
    "  transform position=400,300,0 rotation=0,0,1.5\n"
    "  motion damping=0.5 max_speed=300\n"
    "  bounds mode=clamp\n"
    "  text string=\"P1\" size=12 color=#FFFFFF align=center,middle\n"
    "end\n"
    "entity\n"
    "  from rock\n"
    "  transform position=100,100,0\n"
    "  lifetime seconds=4\n"
    "end\n";

int main(void)
{
    wgf_node_t canvas;
    wgf_scene_t field;
    wgf_entity_t ship, rock, small;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the refusals below log, on purpose */
    expect(wgf_gfx_priv_start(), "gfx");
    canvas = wgf_canvas_create();

    /* loaded */
    field = load("scenes/field.scene", field_scene);
    expect(wgf_resource_get_status(field) == WGF_RESOURCE_STATUS_READY, "READY");
    expect(wgf_scene_get_entity_count(field) == 2 && wgf_scene_get_prefab_count(field) == 2, "two entities, two prefabs");
    expect(strcmp(wgf_scene_get_prefab_name(field, 0), "rock") == 0 &&
               strcmp(wgf_scene_get_prefab_name(field, 1), "small_rock") == 0 &&
               strcmp(wgf_scene_get_prefab_name(field, 2), "") == 0,
           "the prefabs in file order");
    expect(wgf_scene_has_prefab(field, "rock") && !wgf_scene_has_prefab(field, "ship"), "has a prefab, not an entity");
    expect(wgf_scene_create("scenes/field.scene") == field, "the same path: the same scene");
    wgf_resource_release(field);

    /* instantiated */
    wgf_ecs_take_events(NULL, 0);
    expect(wgf_scene_instantiate(field, 12345) == 0, "a parent that isn't a node is refused");
    expect(wgf_scene_instantiate(field, canvas) == 2 && wgf_entity_get_count() == 2, "both made");
    ship = wgf_entity_find("ship");
    expect(ship != 0 && wgf_node_get_parent(wgf_entity_get_node(ship)) == canvas, "the ship, under the canvas");
    expect(near(wgf_entity_get_position(ship).x, 400) && near(wgf_entity_get_rotation(ship).z, 1.5f), "its transform");
    expect(wgf_motion_get_damping(ship) == 0.5f && wgf_motion_get_max_speed(ship) == 300, "its motion");
    expect(wgf_bounds_get_mode(ship) == WGF_BOUNDS_MODE_CLAMP && wgf_bounds_get_rect(ship).z == 800,
           "its bounds: given, and the defaults for what wasn't");
    {
        const wgf_node_t text = wgf_entity_get_component_node(ship, WGF_COMPONENT_TEXT);
        expect(strcmp(wgf_text_get_string(text), "P1") == 0 && wgf_text_get_font_size(text) == 12, "its text");
    }
    {
        wgf_entity_t rocks[4];
        expect(wgf_ecs_find_behavior("Rock", rocks, 4) == 1 && wgf_ecs_count_behavior("Rock") == 1, "one rock");
        rock = rocks[0];
    }
    expect(near(wgf_motion_get_velocity(rock).y, -20) && wgf_bounds_get_margin(rock) == 30 &&
               wgf_collider_get_radius(rock) == 24 && wgf_collider_get_layer(rock) == 2 &&
               wgf_collider_get_mask(rock) == 5 && wgf_lifetime_get_seconds(rock) == 4,
           "the rock: from its prefab, and its own lines");
    expect(wgf_behavior_get_param_number(rock, "size") == 3 && strcmp(wgf_behavior_get_param(rock, "label"), "big \"one\"") == 0,
           "its parameters, a quoted one unescaped");
    {
        const wgf_node_t shape = wgf_entity_get_component_node(rock, WGF_COMPONENT_SHAPE2D);
        expect(wgf_shape2d_get_point_count(shape) == 3 && wgf_shape2d_get_outline(shape) == 2 &&
                   wgf_color_get_red(wgf_shape2d_get_color(shape)) == 0x80 &&
                   wgf_color_get_green(wgf_shape2d_get_color(shape)) == 0xFF,
               "its shape");
    }
    {
        int out[3 * 8];
        expect(wgf_ecs_take_events(out, 3 * 8) == 3 && out[0] == WGF_ECS_EVENT_CREATED && (wgf_entity_t)out[1] == rock,
               "CREATED raised for the one with a behavior");
    }

    /* spawned */
    small = wgf_scene_spawn(field, "small_rock", canvas);
    expect(small != 0 && wgf_collider_get_radius(small) == 8 && wgf_collider_get_layer(small) == 2 &&
               wgf_behavior_get_param_number(small, "size") == 1 && strcmp(wgf_behavior_get_name(small), "Rock") == 0,
           "a prefab from another: its lines over the other's");
    expect(wgf_scene_spawn(field, "nothing", canvas) == 0 && wgf_scene_spawn(12345, "rock", canvas) == 0,
           "a prefab it hasn't, or a scene that isn't one, is refused");
    expect(wgf_ecs_count_behavior("Rock") == 2, "two rocks");

    /* the dump, loaded again: the same world */
    {
        char dumped[8192];
        wgf_scene_t again;
        snprintf(dumped, sizeof(dumped), "%s", wgf_ecs_dump());
        expect(strncmp(dumped, "wgf-scene 1\n", 12) == 0, "the dump is a scene");
        wgf_ecs_clear();
        again = load("scenes/dumped.scene", dumped);
        expect(wgf_resource_get_status(again) == WGF_RESOURCE_STATUS_READY, "it loads");
        expect(wgf_scene_instantiate(again, canvas) == 3, "the three made again");
        expect(strcmp(wgf_ecs_dump(), dumped) == 0, "dumped again: the same text");
        ship = wgf_entity_find("ship");
        expect(near(wgf_entity_get_position(ship).x, 400) && wgf_ecs_count_behavior("Rock") == 2, "the same world");
        wgf_resource_release(again);
    }
    wgf_resource_release(field);

    /* models: a generated mesh by its create call's parameters, shared, and its tint; the
       dump writes them the same */
    {
        const wgf_scene_t garage = load("scenes/garage.scene", "wgf-scene 1\n"
                                                              "entity car\n"
                                                              "  model cube=2,1,4 tint=#FF0000FF\n"
                                                              "end\n"
                                                              "entity wheel\n"
                                                              "  model torus=0.5,0.25,12,8\n"
                                                              "end\n");
        wgf_node_t car, wheel;
        wgf_mesh_t cube;
        wgf_ecs_clear();
        wgf_stage_create(); /* models are made once there is a stage to draw them */
        expect(wgf_scene_instantiate(garage, canvas) == 2, "two models made");
        car = wgf_entity_get_component_node(wgf_entity_find("car"), WGF_COMPONENT_MODEL);
        wheel = wgf_entity_get_component_node(wgf_entity_find("wheel"), WGF_COMPONENT_MODEL);
        cube = wgf_mesh_create_cube(2, 1, 4);
        expect(wgf_node_get_type(car) == WGF_NODE_TYPE_MODEL && wgf_model_get_mesh(car) == cube &&
                   wgf_model_get_tint(car) == 0xFF0000FFu,
               "a model: its generated mesh (the same, shared) and its tint");
        wgf_resource_release(cube); /* the reference the comparison took */
        expect(strstr(wgf_ecs_dump(), "model cube=2,1,4 tint=#FF0000FF") != NULL &&
                   strstr(wgf_ecs_dump(), "model torus=0.5,0.25,12,8 tint=#FFFFFFFF") != NULL,
               "dumped as they were written");
        (void)wheel;
        wgf_ecs_clear();
        wgf_resource_release(garage);
    }

    /* refusals */
    expect(refused("header", "wgf-scene 2\n"), "a version it doesn't know");
    expect(refused("empty", ""), "no header");
    expect(refused("line", "wgf-scene 1\nwobble\n"), "an unknown line");
    expect(refused("component", "wgf-scene 1\nentity\n  physics mass=1\nend\n"), "an unknown component");
    expect(refused("key", "wgf-scene 1\nentity\n  motion speed=1\nend\n"), "an unknown key");
    expect(refused("shape", "wgf-scene 1\nentity\n  motion velocity=1,2\nend\n"), "a value of the wrong shape");
    expect(refused("number", "wgf-scene 1\nentity\n  lifetime seconds=soon\nend\n"), "a word for a number");
    expect(refused("mode", "wgf-scene 1\nentity\n  bounds mode=bounce\nend\n"), "a mode that isn't one");
    expect(refused("color", "wgf-scene 1\nentity\n  shape2d color=#12\nend\n"), "a color that isn't one");
    expect(refused("quote", "wgf-scene 1\nentity\n  text string=\"open\nend\n"), "a quote left open");
    expect(refused("nested", "wgf-scene 1\nentity\nentity\nend\nend\n"), "an entity in another");
    expect(refused("open", "wgf-scene 1\nentity\n  motion\n"), "one left without its end");
    expect(refused("stray", "wgf-scene 1\nend\n"), "an end of nothing");
    expect(refused("twice", "wgf-scene 1\nprefab a\nend\nprefab a\nend\n"), "a prefab twice");
    expect(refused("from", "wgf-scene 1\nentity\n  from nowhere\nend\n"), "from a prefab it hasn't");
    expect(refused("late", "wgf-scene 1\nprefab a\nend\nentity\n  motion\n  from a\nend\n"), "from, not first");
    expect(refused("outside", "wgf-scene 1\nmotion velocity=1,0,0\n"), "a component outside an entity");
    {
        static char long_line[5000];
        memcpy(long_line, "wgf-scene 1\n# ", 14);
        memset(long_line + 14, 'x', sizeof(long_line) - 16);
        long_line[sizeof(long_line) - 2] = '\n';
        expect(refused("long", long_line), "a line of more than 4096 bytes");
    }
    {
        const wgf_scene_t missing = wgf_scene_create("scenes/none.scene");
        double start = wgf_time_get_seconds();
        while (wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0)
            wgf_core_priv_update();
        expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED, "a missing file: FAILED");
        wgf_resource_release(missing);
    }
    expect(refused("comment", "wgf-scene 1 # a comment\nentity # here too\n  motion velocity=1,0,0 # and here\nend\n") == false,
           "comments anywhere: not refused");

    wgf_ecs_clear();
    wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
