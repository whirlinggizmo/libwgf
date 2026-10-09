#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_asset.h"
#include "wgf_body.h"
#include "wgf_core_resource_priv.h"
#include "wgf_component.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_physics.h"
#include "wgf_resource.h"
#include "wgf_scene.h"
#include "wgf_stage3d.h"
#include "wgf_time.h"
#include "wgf_vehicle.h"
#include "wgf_world.h"

/* Physics, headless: refused before it starts; a ball falling onto a floor and coming to
 * rest, its actor set where the world put it; a sensor telling both of an overlap; a body
 * put somewhere new by the program; a kinematic body pushing a dynamic one; a car on four
 * wheels driven, steered, its wheels' actors moved, and reset; a scene's body and vehicle
 * lines; the dump writing them back; and a mesh body of a glTF file's nodes. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void step(int ticks)
{
    while (ticks-- > 0) {
        wgf_core_priv_part_tick_begin();
        wgf_core_priv_part_tick(1.0f / 60.0f);
    }
}

static wgf_actor_t put(wgf_actor_t parent, float x, float y, float z)
{
    const wgf_actor_t actor = wgf_actor_create();
    wgf_actor_set_parent(actor, parent);
    wgf_actor_set_position(actor, x, y, z);
    return actor;
}

/* The events since the last take: whether a trigger enter of `a` by `b` is among them. */
static int events[4 * 256];
static int event_count;

static void take(void)
{
    event_count = wgf_world_take_events(events, 4 * 256) / 4;
}

static bool told(wgf_world_event_t event, wgf_actor_t a, wgf_actor_t b)
{
    int i;
    for (i = 0; i < event_count; i++) {
        if (events[4 * i] == (int)event && events[4 * i + 1] == (int)a && events[4 * i + 2] == (int)b) return true;
    }
    return false;
}

static void write_file(const char *path, const char *text)
{
    const wgf_fs_task_t task = wgf_fs_write_text(path, text);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

static const char *garage =
    "wgf-scene 2\n"
    "actor car\n"
    "  transform position=0,1,0\n"
    "  body type=dynamic shape=box size=1.8,0.6,4 mass=1200 friction=0.9 damping=0.1,0.2 layer=2 mask=3\n"
    "  vehicle wheels=wheel_fl,wheel_fr,wheel_rl,wheel_rr radius=0.34 width=0.22 suspension=0.3 stiffness=1.6 "
    "damping=0.5 steering=0.5 grip=1.2 engine_torque=420 max_rpm=7000 gears=3.2,2.1,1.5 drive=rear\n"
    "end\n"
    "actor car/wheel_fl\n  transform position=-0.8,-0.2,1.4\nend\n"
    "actor car/wheel_fr\n  transform position=0.8,-0.2,1.4\nend\n"
    "actor car/wheel_rl\n  transform position=-0.8,-0.2,-1.4\nend\n"
    "actor car/wheel_rr\n  transform position=0.8,-0.2,-1.4\nend\n";

int main(void)
{
    wgf_actor_t stage, floor_actor, ball, gate, pusher, car, wheels[4];
    wgf_vec3_t at;
    int i;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the refusals log, on purpose */
    expect(wgf_gfx_priv_start(), "gfx");
    stage = wgf_stage3d_create();

    /* before physics starts */
    ball = put(stage, 0, 5, 0);
    expect(!wgf_actor_add_component(ball, WGF_COMPONENT_BODY) && !wgf_actor_has_component(ball, WGF_COMPONENT_BODY),
           "a body before physics starts: refused");
    expect(wgf_physics_get_gravity().y == -9.81f, "its gravity, as it will start");

    /* started; a ball onto a floor */
    expect(wgf_physics_set_gravity(0, -9.81f, 0) && wgf_physics_get_gravity().y == -9.81f, "started, by its gravity");
    floor_actor = put(stage, 0, -0.5f, 0);
    expect(wgf_actor_add_component(floor_actor, WGF_COMPONENT_BODY) &&
               wgf_body_set_type(floor_actor, WGF_BODY_TYPE_STATIC) &&
               wgf_body_set_shape(floor_actor, WGF_BODY_SHAPE_BOX, 200, 1, 200),
           "a static floor");
    expect(wgf_actor_add_component(ball, WGF_COMPONENT_BODY) && wgf_body_set_shape(ball, WGF_BODY_SHAPE_SPHERE, 0.5f, 0, 0),
           "a dynamic ball");
    expect(wgf_body_get_type(ball) == WGF_BODY_TYPE_DYNAMIC && wgf_body_get_shape(ball) == WGF_BODY_SHAPE_SPHERE &&
               wgf_body_get_size(ball).x == 0.5f && wgf_body_get_friction(ball) == 0.5f && wgf_body_get_mask(ball) == 0x7FFF,
           "its settings, its defaults");
    expect(!wgf_body_set_layer(ball, 0) && !wgf_body_set_mask(ball, 0x8000) && !wgf_body_set_type(ball, (wgf_body_type_t)9),
           "a layer, a mask, a type that isn't one: refused");
    step(1);
    expect(wgf_actor_get_position(ball).y < 5.0f, "falling at the first tick");
    step(180);
    at = wgf_actor_get_position(ball);
    expect(fabsf(at.y - 0.5f) < 0.05f && fabsf(at.x) < 0.01f, "at rest on the floor");
    expect(fabsf(wgf_body_get_velocity(ball).y) < 0.05f, "still");

    /* put somewhere new by the program, and pushed */
    wgf_actor_set_position(ball, 3, 2, 0);
    step(1);
    expect(fabsf(wgf_actor_get_position(ball).x - 3.0f) < 0.01f && wgf_actor_get_position(ball).y < 2.0f,
           "set by the program: the body there, falling");
    wgf_body_add_impulse(ball, 0, 0, 500);
    step(1);
    expect(wgf_body_get_velocity(ball).z > 0.5f, "an impulse pushed it");
    wgf_body_set_velocity(ball, 0, 0, 0);
    expect(fabsf(wgf_body_get_velocity(ball).z) < 1e-4f, "its velocity set");

    /* a sensor, telling both */
    gate = put(stage, -6, 2, 0);
    wgf_actor_add_component(gate, WGF_COMPONENT_BODY);
    wgf_body_set_type(gate, WGF_BODY_TYPE_SENSOR);
    wgf_body_set_shape(gate, WGF_BODY_SHAPE_BOX, 2, 1, 2);
    wgf_body_set_layer(gate, 8);
    wgf_world_take_events(NULL, 0);
    step(1);
    take();
    wgf_actor_set_position(ball, -6, 4, 0);
    wgf_body_set_layer(ball, 2);
    for (i = 0; i < 90; i++) {
        step(1);
        take();
        if (told(WGF_WORLD_EVENT_TRIGGER_ENTER, gate, ball)) break;
    }
    expect(told(WGF_WORLD_EVENT_TRIGGER_ENTER, gate, ball) && told(WGF_WORLD_EVENT_TRIGGER_ENTER, ball, gate),
           "a ball falling into a sensor: both told");
    for (i = 0; i < 4 * event_count; i += 4) {
        if (events[i + 1] == (int)gate && events[i] == WGF_WORLD_EVENT_TRIGGER_ENTER) expect(events[i + 3] == 2, "the gate told the ball's layer");
    }
    for (i = 0; i < 120; i++) {
        step(1);
        take();
        if (told(WGF_WORLD_EVENT_TRIGGER_EXIT, gate, ball)) break;
    }
    expect(told(WGF_WORLD_EVENT_TRIGGER_EXIT, gate, ball), "and left");

    /* a shape off its actor, a center of mass off its shape, a setting taken at once */
    {
        const wgf_actor_t tipper = put(stage, -20, 0.5f, 0), slider = put(stage, -30, 0.5f, 0);
        wgf_quat_t before;
        wgf_actor_add_component(tipper, WGF_COMPONENT_BODY);
        expect(wgf_body_set_mass_offset(tipper, 0.8f, 0, 0) && wgf_body_get_mass_offset(tipper).x == 0.8f &&
                   wgf_body_set_offset(tipper, 0, 0, 0) && wgf_body_get_offset(tipper).y == 0.0f,
               "its offsets, set and read back");
        step(1);
        before = wgf_gfx_priv_actor_of(tipper)->rotation;
        step(120);
        expect(fabsf(wgf_gfx_priv_actor_of(tipper)->rotation.z - before.z) > 0.2f,
               "a box with its mass past its edge tipped over");
        wgf_actor_add_component(slider, WGF_COMPONENT_BODY);
        wgf_body_set_friction(slider, 0.0f);
        step(2);
        wgf_body_set_velocity(slider, 5, 0, 0);
        step(10);
        wgf_body_set_friction(slider, 0.3f);
        step(1);
        expect(wgf_body_get_velocity(slider).x > 3.0f && wgf_body_get_friction(slider) == 0.3f,
               "friction set as it slides: taken at once, its motion kept");
    }

    /* a kinematic body moved by the program, pushing */
    pusher = put(stage, 10, 0.5f, 0);
    wgf_actor_add_component(pusher, WGF_COMPONENT_BODY);
    wgf_body_set_type(pusher, WGF_BODY_TYPE_KINEMATIC);
    wgf_actor_set_position(ball, 11.2f, 0.5f, 0);
    step(10);
    for (i = 0; i < 30; i++) {
        wgf_actor_set_position(pusher, 10.0f + 0.05f * (float)(i + 1), 0.5f, 0);
        step(1);
    }
    expect(wgf_actor_get_position(ball).x > 11.6f, "a kinematic box pushed the ball along");

    /* a car */
    car = put(stage, 0, 1, 20);
    wgf_actor_add_component(car, WGF_COMPONENT_BODY);
    wgf_body_set_shape(car, WGF_BODY_SHAPE_BOX, 1.8f, 0.6f, 4);
    wgf_body_set_mass(car, 1200);
    for (i = 0; i < 4; i++) wheels[i] = put(car, i % 2 ? 0.8f : -0.8f, -0.2f, i < 2 ? 1.4f : -1.4f);
    expect(!wgf_vehicle_set_input(car, 1, 0, 0, false), "no vehicle yet: refused");
    expect(wgf_actor_add_component(car, WGF_COMPONENT_VEHICLE), "a vehicle on it");
    expect(!wgf_vehicle_set_wheels(car, wheels, 3) && !wgf_vehicle_set_wheels(car, &ball, 2) &&
               wgf_vehicle_set_wheels(car, wheels, 4) && wgf_vehicle_get_wheel_count(car) == 4 &&
               wgf_vehicle_get_wheel(car, 2) == wheels[2] && wgf_vehicle_get_wheel(car, 4) == 0,
           "its wheels: pairs of its children");
    {
        const float gears[] = {3.2f, 2.1f, 1.5f, 1.15f, 0.92f};
        expect(wgf_vehicle_set_engine(car, 420, 7000) && wgf_vehicle_set_gears(car, gears, 5) &&
                   !wgf_vehicle_set_gears(car, gears, 0) && wgf_vehicle_set_drive(car, WGF_VEHICLE_DRIVE_REAR) &&
                   wgf_vehicle_set_wheel_size(car, 0.34f, 0.22f) && wgf_vehicle_set_suspension(car, 0.3f, 1.6f, 0.5f) &&
                   wgf_vehicle_set_steering(car, 0.5f) && wgf_vehicle_set_grip(car, 1.2f),
               "its settings");
        expect(wgf_vehicle_get_engine(car).y == 7000.0f && wgf_vehicle_get_gear_count(car) == 5 &&
                   wgf_vehicle_get_gear_ratio(car, 1) == 2.1f && wgf_vehicle_get_gear_ratio(car, 5) == 0.0f &&
                   wgf_vehicle_get_drive(car) == WGF_VEHICLE_DRIVE_REAR && wgf_vehicle_get_wheel_size(car).x == 0.34f &&
                   wgf_vehicle_get_suspension(car).y == 1.6f && wgf_vehicle_get_steering(car) == 0.5f &&
                   wgf_vehicle_get_grip(car) == 1.2f,
               "read back as set");
    }
    step(60); /* settled on its springs */
    expect(fabsf(wgf_vehicle_get_speed(car)) < 0.2f, "at rest");
    for (i = 0; i < 180; i++) {
        wgf_vehicle_set_input(car, 1, 0, 0, false);
        step(1);
    }
    expect(wgf_vehicle_get_speed(car) > 8.0f && wgf_actor_get_position(car).z > 25.0f, "the throttle took it forward");
    expect(wgf_vehicle_get_gear(car) >= 1 && wgf_vehicle_get_rpm(car) > 500.0f, "in gear, its engine turning");
    expect(fabsf(wgf_actor_get_position(wheels[0]).x + 0.8f) < 0.05f && wgf_actor_get_position(wheels[0]).y < -0.2f,
           "its wheel set where the wheel is, on its spring");
    {
        const float x = wgf_actor_get_position(car).x;
        for (i = 0; i < 60; i++) {
            wgf_vehicle_set_input(car, 0.5f, 0, 1, false);
            step(1);
        }
        expect(wgf_actor_get_position(car).x < x - 0.5f, "steered right: toward its -x, facing +z");
    }
    expect(wgf_vehicle_get_wheel_slip(car, 0) >= 0.0f && wgf_vehicle_get_wheel_slip(car, 7) == 0.0f, "a wheel's slip");
    expect(wgf_vehicle_set_anti_roll(car, 5000) && wgf_vehicle_get_anti_roll(car) == 5000.0f, "anti-roll, taken at once");
    {
        float front = 0.0f, rear = 0.0f;
        wgf_actor_set_transform(car, 30, 1, 60, 0, 0, 0, 1, 1, 1); /* back on its wheels, somewhere clear */
        wgf_vehicle_reset(car);
        step(60);
        for (i = 0; i < 60; i++) {
            wgf_vehicle_set_input(car, 1, 0, 0, false);
            step(1);
            front = wgf_vehicle_get_wheel_slip(car, 0) > front ? wgf_vehicle_get_wheel_slip(car, 0) : front;
            rear = wgf_vehicle_get_wheel_slip(car, 2) > rear ? wgf_vehicle_get_wheel_slip(car, 2) : rear;
        }
        if (getenv("WGF_TEST_SHOW")) printf("slip at launch: front %g, rear %g\n", front, rear);
        expect(front < 2.0f && rear > 5.0f, "at full throttle from rest the rolling front tire is near its grip, the driven rear spins");
    }
    expect(wgf_vehicle_reset(car) && fabsf(wgf_vehicle_get_speed(car)) < 1e-3f, "reset: at rest");

    /* a scene's lines, and the dump */
    {
        wgf_scene_t scene;
        wgf_actor_t made;
        double start;
        const char *dump;
        write_file("scenes/garage.scene", garage);
        scene = wgf_scene_create("scenes/garage.scene");
        start = wgf_time_get_seconds();
        while (wgf_resource_get_status(scene) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
            wgf_core_priv_update();
        }
        expect(wgf_scene_instantiate(scene, stage) == 1, "a scene's car");
        made = wgf_stage3d_find(stage, "car");
        expect(wgf_actor_has_component(made, WGF_COMPONENT_BODY) && wgf_body_get_mass(made) == 1200.0f &&
                   wgf_body_get_layer(made) == 2 && wgf_body_get_damping(made).y == 0.2f,
               "its body as the line says");
        step(2);
        expect(wgf_vehicle_get_wheel_count(made) == 4 && wgf_vehicle_get_wheel(made, 3) == wgf_actor_find(made, "wheel_rr"),
               "its vehicle's wheels found by name as it was made");
        dump = wgf_world_dump();
        if (getenv("WGF_TEST_SHOW")) printf("%s", dump);
        expect(strstr(dump, "body type=dynamic shape=box size=1.79999995,0.600000024,4 mass=1200 friction=0.899999976") != NULL &&
                   strstr(dump, "vehicle wheels=_0,_1,_2,_3 ") != NULL &&
                   strstr(dump, "vehicle wheels=wheel_fl,wheel_fr,wheel_rl,wheel_rr radius=0.340000004") != NULL &&
                   strstr(dump, "gears=3.20000005,2.0999999,1.5 drive=rear") != NULL,
               "dumped as scene lines");
        wgf_resource_release(scene);
    }

    /* gone */
    wgf_actor_destroy(car, WGF_ACTOR_DESTROY_CHILDREN);
    step(1);
    expect(wgf_actor_count_with_component(WGF_COMPONENT_VEHICLE) == 1 && wgf_actor_count_with_component(WGF_COMPONENT_BODY) == 7,
           "a car destroyed: its body and vehicle with it");

    /* a mesh body of a glTF file's nodes, made while the file loads: it waits for the file,
       then takes each node's own triangles where the node is, none of the file's root's */
    {
        wgf_mesh_t file;
        wgf_actor_t deck, on_deck, beside;
        double start;
        write_file("models/deck.gltf",
                   "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
                   "\"nodes\":[{\"name\":\"deck\",\"mesh\":0,\"translation\":[20,0,0]}],"
                   "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                   "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,"
                   "AACgwAAAAAAAAKDAAACgwAAAAAAAAKBAAACgQAAAAAAAAKBAAACgQAAAAAAAAKDAAAABAAIAAAACAAMA\","
                   "\"byteLength\":60}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":48},"
                   "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":12}],"
                   "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\","
                   "\"min\":[-5,0,-5],\"max\":[5,0,5]},"
                   "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]}");
        file = wgf_mesh_create("models/deck.gltf");
        deck = wgf_model_create(file);
        wgf_resource_release(file);
        wgf_actor_set_parent(deck, stage);
        wgf_actor_set_position(deck, -60, 3, -60); /* clear of the rest */
        expect(wgf_actor_add_component(deck, WGF_COMPONENT_BODY) && wgf_body_set_type(deck, WGF_BODY_TYPE_STATIC) &&
                   wgf_body_set_shape(deck, WGF_BODY_SHAPE_MESH, 0, 0, 0),
               "a mesh body on a file's root, the file loading");
        step(1);
        start = wgf_time_get_seconds();
        while (wgf_resource_get_status(file) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0)
            wgf_core_priv_update();
        expect(wgf_resource_get_status(file) == WGF_RESOURCE_STATUS_READY, "the file loaded");
        on_deck = put(stage, -40, 6, -60);
        beside = put(stage, -60, 6, -60); /* where the file's mesh would be, unplaced */
        wgf_actor_add_component(on_deck, WGF_COMPONENT_BODY);
        wgf_body_set_shape(on_deck, WGF_BODY_SHAPE_SPHERE, 0.5f, 0, 0);
        wgf_actor_add_component(beside, WGF_COMPONENT_BODY);
        wgf_body_set_shape(beside, WGF_BODY_SHAPE_SPHERE, 0.5f, 0, 0);
        step(180);
        expect(fabsf(wgf_actor_get_position(on_deck).y - 3.5f) < 0.05f, "a ball rests on the file's node, where it is");
        if (getenv("WGF_TEST_SHOW")) printf("on the deck %g, beside %g\n", wgf_actor_get_position(on_deck).y, wgf_actor_get_position(beside).y);
        expect(fabsf(wgf_actor_get_position(beside).y - 0.5f) < 0.05f, "and none of it is anywhere else: one beside falls past");
        /* the file saved again, its node 2 lower, and loaded again: the body made again from it */
        write_file("models/deck.gltf",
                   "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
                   "\"nodes\":[{\"name\":\"deck\",\"mesh\":0,\"translation\":[20,-2,0]}],"
                   "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                   "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,"
                   "AACgwAAAAAAAAKDAAACgwAAAAAAAAKBAAACgQAAAAAAAAKBAAACgQAAAAAAAAKDAAAABAAIAAAACAAMA\","
                   "\"byteLength\":60}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":48},"
                   "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":12}],"
                   "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\","
                   "\"min\":[-5,0,-5],\"max\":[5,0,5]},"
                   "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]}");
        expect(wgf_asset_reload("models/deck.gltf") == 1, "the file loading again");
        start = wgf_time_get_seconds();
        while (wgf_core_priv_resource_is_reloading(file) && wgf_time_get_seconds() - start < 30.0) wgf_core_priv_update();
        step(180);
        if (getenv("WGF_TEST_SHOW")) printf("on the lowered deck %g\n", wgf_actor_get_position(on_deck).y);
        expect(fabsf(wgf_actor_get_position(on_deck).y - 1.5f) < 0.05f,
               "loaded again, the body is made again from its new place, and the ball asleep on it wakes and drops");
        wgf_actor_destroy(deck, WGF_ACTOR_DESTROY_CHILDREN);
        wgf_actor_destroy(on_deck, WGF_ACTOR_DESTROY_CHILDREN);
        wgf_actor_destroy(beside, WGF_ACTOR_DESTROY_CHILDREN);
    }
    /* the same to the bit natively and in a browser: a pile of boxes, each made turned (the
       rotations' trigonometry, wgf_trig.h, the same on every target), dropped onto a floor
       and settled, their poses hashed and held to one number on every target (the
       experiments' 1,000-box scene came apart across targets before it) */
    {
        enum { SIDE = 5, BOXES = SIDE * SIDE * SIDE };
        wgf_actor_t pile[BOXES], ground;
        uint32_t seed = 12345u, hash = 2166136261u;
        int b, k;
        ground = put(stage, 300, -0.5f, 0);
        wgf_actor_add_component(ground, WGF_COMPONENT_BODY);
        wgf_body_set_type(ground, WGF_BODY_TYPE_STATIC);
        wgf_body_set_shape(ground, WGF_BODY_SHAPE_BOX, 40, 1, 40);
        for (b = 0; b < BOXES; b++) {
            float yaw;
            seed = seed * 1664525u + 1013904223u;
            yaw = ((float)(seed >> 8) / 16777216.0f - 0.5f) * 0.4f;
            pile[b] = put(stage, 300 + (float)(b % SIDE) * 0.65f, 3 + (float)(b / (SIDE * SIDE)) * 0.65f,
                          (float)((b / SIDE) % SIDE) * 0.65f);
            wgf_actor_set_rotation(pile[b], 0, yaw, 0);
            wgf_actor_add_component(pile[b], WGF_COMPONENT_BODY);
            wgf_body_set_shape(pile[b], WGF_BODY_SHAPE_BOX, 0.44f, 0.44f, 0.44f);
            wgf_body_set_friction(pile[b], 0.5f);
            wgf_body_set_damping(pile[b], 0.05f, 0.05f);
        }
        step(360);
        for (b = 0; b < BOXES; b++) {
            const wgf_gfx_priv_actor_t *box_ptr = wgf_gfx_priv_actor_of(pile[b]);
            const float values[7] = {box_ptr->position.x, box_ptr->position.y, box_ptr->position.z, box_ptr->rotation.x,
                                     box_ptr->rotation.y, box_ptr->rotation.z, box_ptr->rotation.w};
            for (k = 0; k < 7; k++) {
                uint32_t u;
                memcpy(&u, &values[k], sizeof(u));
                hash = (hash ^ u) * 16777619u;
            }
        }
        if (getenv("WGF_TEST_SHOW")) printf("pile hash %08x\n", (unsigned)hash);
        expect(hash == 0x305B395Fu, "a settled pile of turned boxes: the same to the bit on every target");
        for (b = 0; b < BOXES; b++) wgf_actor_destroy(pile[b], WGF_ACTOR_DESTROY_CHILDREN);
        wgf_actor_destroy(ground, WGF_ACTOR_DESTROY_CHILDREN);
    }
    wgf_gfx_priv_stop();
    expect(!wgf_actor_has_component(ball, WGF_COMPONENT_BODY), "stopped with gfx");
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
