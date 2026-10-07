#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_behavior.h"
#include "wgf_bounds.h"
#include "wgf_stage2d.h"
#include "wgf_collider.h"
#include "wgf_color.h"
#include "wgf_core_priv.h"
#include "wgf_world.h"
#include "wgf_component.h"
#include "wgf_fs.h"
#include "wgf_lifetime.h"
#include "wgf_log.h"
#include "wgf_motion.h"
#include "wgf_actor.h"
#include "wgf_resource.h"
#include "wgf_scene.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_stage3d.h"
#include "wgf_shape2d.h"
#include "wgf_text.h"
#include "wgf_time.h"

/* Scenes, headless: a file of actor trees loaded as a resource, READY with its top actors
 * and prefabs; instantiated, each actor of its kind as the file says, with its components,
 * its behaviors (several, each's CREATED raised), and the actors inside it; a prefab
 * spawned, and one starting `from` another, its tree with it; each refusal FAILED; the
 * same path the same scene; and the dump loaded again making the same actors. */

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
    "wgf-scene 2\n"
    "# the field\n"
    "prefab rock\n"
    "  shape2d polygon=0,0,10,0,10,10 outline=2 color=#80FF00\n"
    "  motion velocity=0,-20,0 spin=0,0,1\n"
    "  bounds rect=0,0,800,600 mode=wrap margin=30\n"
    "  collider radius=24 layer=2 mask=5\n"
    "  behavior name=Rock size=3 label=\"big \\\"one\\\"\"\n"
    "  behavior name=Spinner\n"
    "end\n"
    "prefab rock/dust\n"
    "  emitter2d rate=5\n"
    "end\n"
    "prefab small_rock\n"
    "  from rock\n"
    "  collider radius=8\n"
    "  behavior size=1\n"
    "end\n"
    "actor ship\n"
    "  shape2d circle=10\n"
    "  transform position=400,300,0 rotation=0,0,1.5\n"
    "  motion damping=0.5 max_speed=300\n"
    "  bounds mode=clamp\n"
    "end\n"
    "actor ship/label\n"
    "  text string=\"P1\" size=12 color=#FFFFFF align=center,middle\n"
    "  transform position=0,-20,0\n"
    "end\n"
    "actor\n"
    "  from rock\n"
    "  transform position=100,100,0\n"
    "  lifetime seconds=4\n"
    "end\n";

int main(void)
{
    wgf_actor_t stage, ship, rock, small;
    wgf_scene_t field;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the refusals below log, on purpose */
    expect(wgf_gfx_priv_start(), "gfx");
    stage = wgf_stage2d_create();

    /* loaded */
    field = load("scenes/field.scene", field_scene);
    expect(wgf_resource_get_status(field) == WGF_RESOURCE_STATUS_READY, "READY");
    expect(wgf_scene_get_actor_count(field) == 2 && wgf_scene_get_prefab_count(field) == 2, "two top actors, two prefabs");
    expect(strcmp(wgf_scene_get_prefab_name(field, 0), "rock") == 0 &&
               strcmp(wgf_scene_get_prefab_name(field, 1), "small_rock") == 0 &&
               strcmp(wgf_scene_get_prefab_name(field, 2), "") == 0,
           "the prefabs in file order");
    expect(wgf_scene_has_prefab(field, "rock") && !wgf_scene_has_prefab(field, "ship"), "has a prefab, not an actor");
    expect(wgf_scene_create("scenes/field.scene") == field, "the same path: the same scene");
    wgf_resource_release(field);

    /* instantiated */
    wgf_world_take_events(NULL, 0);
    expect(wgf_scene_instantiate(field, 12345) == 0, "a parent that isn't an actor is refused");
    expect(wgf_scene_instantiate(field, stage) == 2 && wgf_actor_get_count() == 2, "both made");
    ship = wgf_actor_find(stage, "ship");
    expect(ship != 0 && wgf_actor_get_parent(ship) == stage && wgf_actor_get_kind(ship) == WGF_ACTOR_KIND_SHAPE2D,
           "the ship, a shape, under the stage");
    expect(near(wgf_actor_get_position(ship).x, 400) && near(wgf_actor_get_rotation(ship).z, 1.5f), "its transform");
    expect(wgf_motion_get_damping(ship) == 0.5f && wgf_motion_get_max_speed(ship) == 300, "its motion");
    expect(wgf_bounds_get_mode(ship) == WGF_BOUNDS_MODE_CLAMP && wgf_bounds_get_rect(ship).z == 800,
           "its bounds: given, and the defaults for what wasn't");
    {
        const wgf_actor_t text = wgf_actor_find(ship, "label");
        expect(wgf_actor_get_parent(text) == ship && wgf_actor_get_kind(text) == WGF_ACTOR_KIND_TEXT &&
                   strcmp(wgf_text_get_string(text), "P1") == 0 && near(wgf_actor_get_position(text).y, -20),
               "its text, an actor inside it");
    }
    {
        wgf_actor_t rocks[4];
        expect(wgf_actor_find_with_behavior("Rock", rocks, 4) == 1 && wgf_actor_count_with_behavior("Rock") == 1, "one rock");
        rock = rocks[0];
    }
    expect(near(wgf_motion_get_velocity(rock).y, -20) && wgf_bounds_get_margin(rock) == 30 &&
               wgf_collider_get_radius(rock) == 24 && wgf_collider_get_layer(rock) == 2 &&
               wgf_collider_get_mask(rock) == 5 && wgf_lifetime_get_seconds(rock) == 4,
           "the rock: from its prefab, and its own lines");
    expect(wgf_actor_get_behavior_count(rock) == 2 &&
               wgf_behavior_get_param_number(rock, wgf_actor_find_behavior(rock, "Rock"), "size") == 3 &&
               strcmp(wgf_behavior_get_param(rock, wgf_actor_find_behavior(rock, "Rock"), "label"), "big \"one\"") == 0,
           "its two behaviors, a quoted parameter unescaped");
    expect(wgf_actor_get_kind(rock) == WGF_ACTOR_KIND_SHAPE2D &&
               wgf_shape2d_get_point_count(rock) == 3 && wgf_shape2d_get_outline(rock) == 2 &&
               wgf_color_get_green(wgf_shape2d_get_color(rock)) == 0xFF,
           "its shape: the kind of its prefab");
    expect(wgf_actor_get_kind(wgf_actor_find(rock, "dust")) == WGF_ACTOR_KIND_EMITTER2D, "the prefab's actor inside it");
    {
        int out[4 * 8];
        expect(wgf_world_take_events(out, 4 * 8) == 8 && out[0] == WGF_WORLD_EVENT_CREATED && (wgf_actor_t)out[1] == rock &&
                   out[6] == 2,
               "CREATED raised for each behavior");
    }

    /* spawned */
    small = wgf_prefab_spawn(wgf_scene_find_prefab(field, "small_rock"), stage);
    expect(small != 0 && wgf_collider_get_radius(small) == 8 && wgf_collider_get_layer(small) == 2 &&
               wgf_behavior_get_param_number(small, wgf_actor_find_behavior(small, "Spinner"), "size") == 1 &&
               wgf_behavior_get_param_number(small, wgf_actor_find_behavior(small, "Rock"), "size") == 3 &&
               wgf_actor_find(small, "dust") != 0,
           "a prefab from another: its lines over the other's, `behavior` without a name the last one's, its tree");
    expect(wgf_scene_find_prefab(field, "nothing") == 0 && wgf_scene_find_prefab(12345, "rock") == 0 &&
               wgf_prefab_spawn(0, stage) == 0 && wgf_prefab_spawn(12345, stage) == 0,
           "a prefab it hasn't, or a scene that isn't one, is refused");
    expect(wgf_actor_count_with_behavior("Rock") == 2, "two rocks");
    {
        const wgf_prefab_t rock_prefab = wgf_scene_find_prefab(field, "rock");
        const wgf_actor_t placed = wgf_prefab_spawn_at(rock_prefab, stage, 30, 40, 0, 1.25f);
        expect(near(wgf_actor_get_position(placed).x, 30) && near(wgf_actor_get_position(placed).y, 40) &&
                   near(wgf_actor_get_rotation(placed).z, 1.25f) && wgf_actor_count_with_behavior("Rock") == 3,
               "spawned at a place, turned about z on a 2D stage");
        expect(wgf_prefab_spawn_at(0, stage, 0, 0, 0, 0) == 0 && wgf_prefab_spawn_at(rock_prefab, 12345, 0, 0, 0, 0) == 0,
               "and refused as a spawn is");
        expect(rock_prefab != 0 && wgf_scene_find_prefab(field, "rock") == rock_prefab &&
                   strcmp(wgf_handle_get_kind_name(rock_prefab), "ecs.prefab") == 0,
               "a prefab is a handle of its own, the same each time it is found");
        wgf_actor_destroy(placed, WGF_ACTOR_DESTROY_CHILDREN);
    }

    /* the dump, loaded again: the same actors */
    {
        char dumped[16384];
        wgf_scene_t again;
        snprintf(dumped, sizeof(dumped), "%s", wgf_world_dump());
        expect(strncmp(dumped, "wgf-scene 2\n", 12) == 0, "the dump is a scene");
        wgf_world_clear();
        again = load("scenes/dumped.scene", dumped);
        expect(wgf_resource_get_status(again) == WGF_RESOURCE_STATUS_READY, "it loads");
        expect(wgf_scene_instantiate(again, stage) == 3, "the three made again");
        expect(strcmp(wgf_world_dump(), dumped) == 0, "dumped again: the same text");
        ship = wgf_actor_find(stage, "ship");
        expect(near(wgf_actor_get_position(ship).x, 400) && wgf_actor_count_with_behavior("Rock") == 2 &&
                   wgf_actor_get_kind(wgf_actor_find(ship, "label")) == WGF_ACTOR_KIND_TEXT,
               "the same actors");
        wgf_resource_release(again);
    }
    {
        const wgf_prefab_t rock_prefab = wgf_scene_find_prefab(field, "rock");
        wgf_resource_release(field);
        expect(rock_prefab != 0 && wgf_prefab_spawn(rock_prefab, stage) == 0,
               "a released scene's prefabs go with it: their handles stale");
    }

    /* references: by a name on the stage, a path from one, from the actor and its parent,
       found once as the scene's actors are all made (a later one too), and again when spawned */
    {
        wgf_scene_t track;
        wgf_actor_t car, gate, wheel, smoke, spawned;
        int b;
        wgf_world_clear();
        track = load("scenes/track.scene", "wgf-scene 2\n"
                                           "prefab marker\n"
                                           "  behavior name=Marker car=@car self=@. up=@..\n"
                                           "end\n"
                                           "actor car\n"
                                           "  behavior name=Car gate=@start_gate smoke=@./wheel_rl/smoke\n"
                                           "  behavior wheel=@car/wheel_rl missing=@nobody plain=text\n"
                                           "end\n"
                                           "actor car/wheel_rl\n"
                                           "  behavior name=Wheel car=@.. gate=@../../start_gate past=@../../../x\n"
                                           "end\n"
                                           "actor car/wheel_rl/smoke\n"
                                           "end\n"
                                           "actor start_gate\n"
                                           "end\n");
        expect(wgf_scene_instantiate(track, stage) == 2, "two top actors");
        car = wgf_stage2d_find(stage, "car");
        gate = wgf_stage2d_find(stage, "start_gate");
        wheel = wgf_actor_find(car, "wheel_rl");
        smoke = wgf_actor_find(car, "wheel_rl/smoke");
        b = wgf_actor_find_behavior(car, "Car");
        expect(gate != 0 && wgf_behavior_get_param_actor(car, b, "gate") == gate,
               "a name on the stage, made after the actor referring to it");
        expect(wgf_behavior_get_param_actor(car, b, "smoke") == smoke &&
                   wgf_behavior_get_param_actor(car, b, "wheel") == wheel,
               "a path from the actor, and one from a name");
        expect(wgf_behavior_get_param_actor(car, b, "missing") == 0 &&
                   wgf_behavior_get_param_actor(car, b, "plain") == 0 &&
                   wgf_behavior_get_param_actor(car, b, "absent") == 0 &&
                   strcmp(wgf_behavior_get_param(car, b, "gate"), "@start_gate") == 0,
               "none found, not a reference, or no such parameter: 0; the text kept");
        b = wgf_actor_find_behavior(wheel, "Wheel");
        expect(wgf_behavior_get_param_actor(wheel, b, "car") == car &&
                   wgf_behavior_get_param_actor(wheel, b, "gate") == gate &&
                   wgf_behavior_get_param_actor(wheel, b, "past") == 0,
               "the parent, a path up and down again, and up past the stage: none");
        spawned = wgf_prefab_spawn(wgf_scene_find_prefab(track, "marker"), smoke);
        b = wgf_actor_find_behavior(spawned, "Marker");
        expect(wgf_behavior_get_param_actor(spawned, b, "car") == car &&
                   wgf_behavior_get_param_actor(spawned, b, "self") == spawned &&
                   wgf_behavior_get_param_actor(spawned, b, "up") == smoke,
               "spawned: found from where it is");
        expect(wgf_behavior_set_param(spawned, b, "car", "@start_gate") &&
                   wgf_behavior_get_param_actor(spawned, b, "car") == gate &&
                   wgf_behavior_set_param(spawned, b, "car", "gate") &&
                   wgf_behavior_get_param_actor(spawned, b, "car") == 0,
               "set: found as it is set, and no longer a reference");
        wgf_actor_destroy(gate, WGF_ACTOR_DESTROY_CHILDREN);
        expect(wgf_behavior_get_param_actor(car, wgf_actor_find_behavior(car, "Car"), "gate") == 0,
               "an actor since destroyed: 0");
        expect(strstr(wgf_world_dump(), "gate=\"@start_gate\"") != NULL, "dumped as written");
        wgf_world_clear();
        wgf_resource_release(track);
    }

    /* models: a generated mesh by its create call's parameters, shared, and its tint, once
       a stage is made; the dump writes them the same */
    {
        wgf_scene_t garage;
        wgf_actor_t car, world3d, cone;
        wgf_mesh_t cube;
        wgf_world_clear();
        world3d = wgf_stage3d_create();
        garage = load("scenes/garage.scene", "wgf-scene 2\n"
                                             "prefab cone\n"
                                             "  transform rotation=0.5,0,0 scale=2,2,2\n"
                                             "end\n"
                                             "actor car\n"
                                             "  model cube=2,1,4 tint=#FF0000FF\n"
                                             "  motion\n"
                                             "end\n");
        expect(wgf_scene_instantiate(garage, stage) == 1, "a model made");
        cone = wgf_prefab_spawn_at(wgf_scene_find_prefab(garage, "cone"), world3d, 1, 2, 3, 0.25f);
        expect(near(wgf_actor_get_position(cone).z, 3) && near(wgf_actor_get_rotation(cone).y, 0.25f) &&
                   near(wgf_actor_get_rotation(cone).x, 0.5f) && near(wgf_actor_get_scale(cone).x, 2),
               "on a 3D stage, turned about y, its other angle and its scale the prefab's");
        car = wgf_actor_find(stage, "car");
        cube = wgf_mesh_create_cube(2, 1, 4);
        expect(wgf_actor_get_kind(car) == WGF_ACTOR_KIND_MODEL && wgf_model_get_mesh(car) == cube &&
                   wgf_model_get_tint(car) == 0xFF0000FFu,
               "a model: its generated mesh (the same, shared) and its tint");
        wgf_resource_release(cube); /* the reference the comparison took */
        expect(strstr(wgf_world_dump(), "model cube=2,1,4 tint=#FF0000FF") != NULL, "dumped as it was written");
        wgf_world_clear();
        wgf_resource_release(garage);
    }

    /* refusals */
    expect(refused("header", "wgf-scene 3\n"), "a version it doesn't know");
    expect(refused("old", "wgf-scene 1\nentity\nend\n"), "version 1's entities: refused, saying why");
    expect(refused("structured", "wgf-scene 2\nactor\n  behavior name=Car wheels[0].radius=0.3\nend\n") &&
               refused("dotted", "wgf-scene 2\nactor\n  behavior name=Car engine.torque=420\nend\n"),
           "a key kept for structured values");
    expect(refused("kinds", "wgf-scene 2\nactor\n  shape2d circle=2\n  sprite\nend\n"), "an actor of two kinds");
    expect(refused("prefabkind", "wgf-scene 2\nprefab p\n  text\nend\nactor\n  from p\n  sprite\nend\n"),
           "a kind other than its prefab's");
    expect(refused("inner", "wgf-scene 2\nactor\n  prefab p\n  end\nend\n"), "a block inside another");
    expect(refused("orphan", "wgf-scene 2\nactor a/b\nend\n"), "a part whose parent's path is no block above");
    expect(refused("paths", "wgf-scene 2\nactor a\nend\nactor a\nend\n"), "two blocks at one path");
    expect(refused("empty", ""), "no header");
    expect(refused("line", "wgf-scene 2\nwobble\n"), "an unknown line");
    expect(refused("component", "wgf-scene 2\nactor\n  physics mass=1\nend\n"), "an unknown component");
    expect(refused("key", "wgf-scene 2\nactor\n  motion speed=1\nend\n"), "an unknown key");
    expect(refused("shape", "wgf-scene 2\nactor\n  motion velocity=1,2\nend\n"), "a value of the wrong shape");
    expect(refused("number", "wgf-scene 2\nactor\n  lifetime seconds=soon\nend\n"), "a word for a number");
    expect(refused("mode", "wgf-scene 2\nactor\n  bounds mode=bounce\nend\n"), "a mode that isn't one");
    expect(refused("color", "wgf-scene 2\nactor\n  shape2d color=#12\nend\n"), "a color that isn't one");
    expect(refused("quote", "wgf-scene 2\nactor\n  text string=\"open\nend\n"), "a quote left open");
    expect(refused("nested", "wgf-scene 2\nactor\nactor\nend\n"), "an actor left open before another");
    expect(refused("open", "wgf-scene 2\nactor\n  motion\n"), "one left without its end");
    expect(refused("stray", "wgf-scene 2\nend\n"), "an end of nothing");
    expect(refused("twice", "wgf-scene 2\nprefab a\nend\nprefab a\nend\n"), "a prefab twice");
    expect(refused("from", "wgf-scene 2\nactor\n  from nowhere\nend\n"), "from a prefab it hasn't");
    expect(refused("late", "wgf-scene 2\nprefab a\nend\nactor\n  motion\n  from a\nend\n"), "from, not first");
    expect(refused("outside", "wgf-scene 2\nmotion velocity=1,0,0\n"), "a component outside an actor");
    {
        static char long_line[5000];
        memcpy(long_line, "wgf-scene 2\n# ", 14);
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
    expect(refused("comment", "wgf-scene 2 # a comment\nactor # here too\n  motion velocity=1,0,0 # and here\nend\n") == false,
           "comments anywhere: not refused");

    wgf_world_clear();
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
