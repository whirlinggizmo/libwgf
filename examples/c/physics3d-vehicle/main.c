#include <math.h>
#include <stddef.h>

#include "wgf_action.h"
#include "wgf_app.h"
#include "wgf_body.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_component.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_actor.h"
#include "wgf_physics.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_vehicle.h"
#include "wgf_window.h"
#include "wgf_world.h"

/* physics3d: a car on four wheels, driven, on a floor with a ramp, beside a stack of boxes
 * it can knock over, through a sensor gate that lights when the car is in it. The car is a
 * dynamic body with a vehicle; its wheels are models under it, which the vehicle moves.
 * The camera follows where the car is drawn (wgf_actor_get_drawn_position), so it is as
 * smooth as the car at any frame rate. Keys: arrows (or WASD) drive, Space the hand brake,
 * R puts the car back, B shows the bodies (the debug view), Escape quits where quitting
 * means anything. */

enum { BOXES = 10 };

static struct {
    wgf_actor_t stage, camera, car, gate, gate_model;
    bool show_bodies, in_gate;
} g;

/* A model of a generated box, `w` by `h` by `d`, on the stage at (x, y, z), tinted, with a
 * body of the same size. */
static wgf_actor_t block(float w, float h, float d, float x, float y, float z, wgf_color_t tint, wgf_body_type_t type)
{
    const wgf_mesh_t mesh = wgf_mesh_create_cube(w, h, d);
    const wgf_actor_t model = wgf_model_create(mesh);
    wgf_resource_release(mesh); /* the model holds its own */
    wgf_model_set_tint(model, tint);
    wgf_actor_set_position(model, x, y, z);
    wgf_actor_set_parent(model, g.stage);
    wgf_actor_add_component(model, WGF_COMPONENT_BODY);
    wgf_body_set_type(model, type);
    wgf_body_set_shape(model, WGF_BODY_SHAPE_BOX, w, h, d);
    return model;
}

static void place_car(void)
{
    wgf_actor_set_transform(g.car, 0, 1.2f, -8, 0, 0, 0, 1, 1, 1);
    wgf_vehicle_reset(g.car);
}

static void init(void *user)
{
    const wgf_mesh_t wheel_mesh = wgf_mesh_create_cylinder(0.34f, 0.22f, 20);
    wgf_actor_t wheels[4];
    wgf_actor_t sun;
    int i;
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(150, 180, 210, 255));
    wgf_physics_set_gravity(0, -9.81f, 0); /* physics starts here: the bodies below need it */
    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, wgf_color_make(150, 170, 200, 255), 0.35f);
    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(sun, -0.4f, -1.0f, -0.3f, 0, 1, 0);
    wgf_light_set_intensity(sun, 1.2f);
    wgf_actor_set_parent(sun, g.stage);

    block(60, 1, 60, 0, -0.5f, 0, wgf_color_make(90, 120, 80, 255), WGF_BODY_TYPE_STATIC); /* the floor */
    {
        const wgf_actor_t ramp = block(6, 0.4f, 8, -8, 0.6f, 6, wgf_color_make(170, 150, 120, 255), WGF_BODY_TYPE_STATIC);
        wgf_actor_set_rotation(ramp, -0.25f, 0, 0);
    }
    for (i = 0; i < BOXES; i++) { /* a pyramid of crates */
        const int row = i < 4 ? 0 : (i < 7 ? 1 : (i < 9 ? 2 : 3));
        const int first = row == 0 ? 0 : (row == 1 ? 4 : (row == 2 ? 7 : 9));
        const float x = 8.0f + (float)(i - first) * 1.05f + (float)row * 0.525f;
        const wgf_actor_t box = block(1, 1, 1, x, 0.5f + (float)row * 1.0f, 8, wgf_color_make(200, 140, 60, 255),
                                      WGF_BODY_TYPE_DYNAMIC);
        wgf_body_set_mass(box, 40);
    }
    /* a gate: a sensor across the way, its frame a model with no body */
    g.gate = wgf_actor_create();
    wgf_actor_set_position(g.gate, 0, 1.5f, 10);
    wgf_actor_set_parent(g.gate, g.stage);
    wgf_actor_add_component(g.gate, WGF_COMPONENT_BODY);
    wgf_body_set_type(g.gate, WGF_BODY_TYPE_SENSOR);
    wgf_body_set_shape(g.gate, WGF_BODY_SHAPE_BOX, 8, 3, 1);
    {
        const wgf_mesh_t bar = wgf_mesh_create_cube(8.4f, 0.3f, 0.3f);
        g.gate_model = wgf_model_create(bar);
        wgf_resource_release(bar);
        wgf_actor_set_position(g.gate_model, 0, 1.6f, 0);
        wgf_actor_set_parent(g.gate_model, g.gate);
    }

    /* the car: a body, a vehicle, and its wheels' models under it, front pair first */
    g.car = block(1.8f, 0.6f, 4.0f, 0, 1.2f, -8, wgf_color_make(200, 40, 40, 255), WGF_BODY_TYPE_DYNAMIC);
    wgf_body_set_mass(g.car, 1200);
    for (i = 0; i < 4; i++) {
        const wgf_actor_t axle = wgf_actor_create(); /* the vehicle turns this: the cylinder lies across it */
        const wgf_actor_t tire = wgf_model_create(wheel_mesh);
        wgf_actor_set_position(axle, i % 2 ? 0.85f : -0.85f, -0.25f, i < 2 ? 1.35f : -1.35f);
        wgf_actor_set_parent(axle, g.car);
        wgf_actor_set_rotation(tire, 0, 0, 1.5707964f);
        wgf_model_set_tint(tire, wgf_color_make(30, 30, 34, 255));
        wgf_actor_set_parent(tire, axle);
        wheels[i] = axle;
    }
    wgf_resource_release(wheel_mesh);
    wgf_actor_add_component(g.car, WGF_COMPONENT_VEHICLE);
    wgf_vehicle_set_wheels(g.car, wheels, 4);
    wgf_vehicle_set_wheel_size(g.car, 0.34f, 0.22f);
    wgf_vehicle_set_engine(g.car, 420, 7000);
    wgf_vehicle_set_grip(g.car, 1.2f);

    wgf_action_bind_keys("steer", WGF_KEY_LEFT, WGF_KEY_RIGHT);
    wgf_action_bind_keys("steer", WGF_KEY_A, WGF_KEY_D);
    wgf_action_bind_key("throttle", WGF_KEY_UP);
    wgf_action_bind_key("throttle", WGF_KEY_W);
    wgf_action_bind_key("brake", WGF_KEY_DOWN);
    wgf_action_bind_key("brake", WGF_KEY_S);
    wgf_action_bind_key("hand_brake", WGF_KEY_SPACE);
}

static void tick(void *user)
{
    /* reverse when stopped and braking */
    const float speed = wgf_vehicle_get_speed(g.car), brake = wgf_action_get_value("brake");
    const float throttle = speed < 0.5f && brake > 0.0f ? -brake : wgf_action_get_value("throttle");
    int events[4 * 64], i, n;
    (void)user;
    wgf_vehicle_set_input(g.car, throttle, speed < 0.5f ? 0.0f : brake, wgf_action_get_axis("steer"),
                          wgf_action_is_down("hand_brake"));
    if (wgf_keyboard_is_pressed(WGF_KEY_R)) place_car();
    n = wgf_world_take_events(events, 4 * 64) / 4;
    for (i = 0; i < n; i++) {
        if (events[4 * i + 1] != (int)g.gate || events[4 * i + 2] != (int)g.car) continue;
        g.in_gate = events[4 * i] == WGF_WORLD_EVENT_TRIGGER_ENTER;
        wgf_model_set_tint(g.gate_model, g.in_gate ? wgf_color_make(60, 220, 90, 255) : wgf_color_make(240, 240, 240, 255));
    }
}

static void frame(void *user)
{
    (void)user;
    if (wgf_keyboard_is_pressed(WGF_KEY_B)) g.show_bodies = !g.show_bodies;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    { /* behind and above the car where it is drawn this frame, looking ahead of it */
        const wgf_vec3_t at = wgf_actor_get_drawn_position(g.car);
        const wgf_vec3_t ahead = wgf_actor_get_drawn_direction(g.car, 0, 0, 1);
        const float len = sqrtf(ahead.x * ahead.x + ahead.z * ahead.z);
        const float fx = len > 0.01f ? ahead.x / len : 0.0f, fz = len > 0.01f ? ahead.z / len : 1.0f;
        wgf_actor_set_position(g.camera, at.x - fx * 9.0f, at.y + 4.0f, at.z - fz * 9.0f);
        wgf_actor_look_at(g.camera, at.x + fx * 4.0f, at.y + 0.8f, at.z + fz * 4.0f, 0, 1, 0);
    }
    wgf_stage3d_draw(g.stage);
    if (g.show_bodies && wgf_draw_begin_3d(g.camera)) {
        wgf_physics_draw_bodies(wgf_color_make(255, 255, 0, 255));
        wgf_draw_end_3d();
    }
    wgf_draw_text(0, "libwgf physics3d: arrows drive, space hand brake, R back, B bodies", 12, 12, 20, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, g.in_gate ? "in the gate" : "", 12, 40, 18, WGF_COLOR_LIME);
}

int main(void)
{
    wgf_window_set_title("libwgf physics3d");
    wgf_window_set_size(1000, 600);
    wgf_window_set_msaa(true);
    wgf_window_set_resizable(true);
    wgf_debug_show_fps(0, 12, 64, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, tick, frame, NULL, NULL);
    return 0;
}
