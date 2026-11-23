#include "wgf_vehicle.h"
#include "wgf_physics3d.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_body3d.h"
#include "wgf_component.h"
#include "wgf_core_load_priv.h" /* WGF_CORE_PRIV_ON_LINK */
#include "wgf_ecs_priv.h"
#include "wgf_log.h"
#include "wgf_physics3d_jolt_priv.h"
#include "wgf_physics3d_priv.h"
#include "wgf_quat.h"

/* Vehicles (wgf_vehicle.h): the vehicle component, a file of its own linked on purpose
 * (wgf_physics3d_enable_vehicles, or a game's "vehicles" feature), so a program with physics and
 * no car carries none of Jolt's VehicleConstraint. physics3d (wgf_physics3d.c) reaches it
 * through the hooks this file sets as it is linked: its component made as physics starts, each
 * vehicle made where it changed and given its driver's input before each step, its wheels set
 * after it. */

#define WHEELS_MAX 8
#define GEARS_MAX 8
#define NAMES_MAX 256

typedef struct vehicle_t {
    int index; /* Jolt's; -1 until made */
    wgf_actor_t wheels[WHEELS_MAX];
    int wheel_count;
    char names[NAMES_MAX]; /* a scene's wheels=, found among its children as it is made */
    float radius, width, travel, stiffness, damping, steering, grip, torque, max_rpm;
    float gears[GEARS_MAX];
    int gear_count, drive;
    float anti_roll;
    float throttle, brake, steer;
    bool hand_brake, dirty;
} vehicle_t;

static struct {
    wgf_ecs_priv_id_t id; /* the component's, as physics started; 0 before */
    wgf_ecs_priv_query_t *query;
} state;


static const char *const drives_words[] = {"front", "rear", "all"};

static const wgf_physics3d_priv_line_key_t vehicle_keys[] = {{"wheels", 0},    {"drive", 0},         {"radius", 1},  {"width", 1},
                                          {"suspension", 1}, {"stiffness", 1},    {"damping", 1}, {"steering", 1},
                                          {"grip", 1},       {"engine_torque", 1}, {"max_rpm", 1}, {"gears", -1},
                                          {"anti_roll", 1}};

static float at_least(float value, float least)
{
    return value > least ? value : least;
}

static int word_of(const char *text, const char *const *words, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        if (strcmp(text, words[i]) == 0) return i;
    }
    return -1;
}

static void put(char *out, size_t size, const char *format, ...)
{
    va_list args;
    const size_t used = strlen(out);
    if (used >= size) return;
    va_start(args, format);
    vsnprintf(out + used, size - used, format, args);
    va_end(args);
}


static vehicle_t *vehicle_of(wgf_actor_t actor)
{
    if (!wgf_physics3d_priv_is_started() || state.id == 0) return NULL;
    return (vehicle_t *)wgf_ecs_priv_get(actor, state.id);
}

static vehicle_t *changing_vehicle(wgf_actor_t actor)
{
    vehicle_t *v;
    if (!wgf_physics3d_priv_start()) return NULL;
    v = vehicle_of(actor);
    if (v != NULL) v->dirty = true;
    return v;
}

static void unmake_vehicle(vehicle_t *v)
{
    if (v->index >= 0) wgf_physics3d_priv_jolt_vehicle_destroy(v->index);
    v->index = -1;
}

/* A scene's wheels=, comma-separated names, found among the actor's children. */
static void find_wheels(wgf_actor_t actor, vehicle_t *v)
{
    char names[NAMES_MAX];
    char *name, *rest;
    if (v->names[0] == '\0') return;
    memcpy(names, v->names, sizeof(names));
    v->wheel_count = 0;
    for (name = names; name != NULL && v->wheel_count < WHEELS_MAX; name = rest) {
        wgf_actor_t wheel;
        rest = strchr(name, ',');
        if (rest != NULL) *rest++ = '\0';
        wheel = wgf_actor_find(actor, name);
        if (wheel == 0 || wgf_actor_get_parent(wheel) != actor) {
            wgf_log_warn("wgf_physics3d: a vehicle's wheel \"%s\" isn't one of its children", name);
            v->wheel_count = 0;
            break;
        }
        v->wheels[v->wheel_count++] = wheel;
    }
    v->names[0] = '\0';
}

static void make_vehicle(wgf_actor_t actor, vehicle_t *v)
{
    uint32_t body = WGF_PHYSICS3D_PRIV_NO_BODY;
    bool dirty = false;
    int type = -1;
    const bool has_body = wgf_physics3d_priv_body_info(actor, &body, &dirty, &type);
    wgf_physics3d_priv_vehicle_desc_t d;
    int i;
    if (has_body && dirty) return; /* its body waits for its models' files: so does it */
    unmake_vehicle(v);
    find_wheels(actor, v);
    v->dirty = false;
    if (!has_body || body == WGF_PHYSICS3D_PRIV_NO_BODY || type != WGF_BODY3D_TYPE_DYNAMIC || v->wheel_count < 2) {
        if (v->wheel_count >= 2) wgf_log_warn("wgf_physics3d: a vehicle needs a dynamic body on its actor");
        return;
    }
    memset(&d, 0, sizeof(d));
    d.wheel_count = v->wheel_count;
    for (i = 0; i < v->wheel_count; i++) {
        const wgf_vec3_t at = wgf_actor_get_position(v->wheels[i]);
        d.wheels[i][0] = at.x;
        d.wheels[i][1] = at.y;
        d.wheels[i][2] = at.z;
        wgf_ecs_priv_record_make(v->wheels[i]); /* simulated: drawn between ticks, as the body is */
    }
    d.radius = v->radius;
    d.width = v->width;
    d.suspension = v->travel;
    d.frequency = v->stiffness;
    d.damping = v->damping;
    d.max_steer = v->steering;
    d.grip = v->grip;
    d.anti_roll = v->anti_roll;
    d.engine_torque = v->torque;
    d.max_rpm = v->max_rpm;
    memcpy(d.gears, v->gears, sizeof(d.gears));
    d.gear_count = v->gear_count;
    d.drive = v->drive;
    v->index = wgf_physics3d_priv_jolt_vehicle_create(body, &d);
    if (v->index < 0) wgf_log_warn("wgf_physics3d: a vehicle couldn't be made (more than 64?)");
}

static void vehicle_add(wgf_actor_t actor)
{
    vehicle_t v;
    static const float gears[] = {2.66f, 1.78f, 1.3f, 1.0f, 0.74f}; /* Jolt's */
    wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_make(actor);
    if (record == NULL) return;
    memset(&v, 0, sizeof(v));
    v.index = -1;
    v.radius = 0.3f;
    v.width = 0.2f;
    v.travel = 0.3f;
    v.stiffness = 1.5f;
    v.damping = 0.5f;
    v.steering = 0.5f;
    v.grip = 1.0f;
    v.torque = 500.0f;
    v.max_rpm = 6000.0f;
    memcpy(v.gears, gears, sizeof(gears));
    v.gear_count = 5;
    v.drive = WGF_VEHICLE_DRIVE_REAR;
    v.dirty = true;
    wgf_ecs_priv_store_set(record->id, state.id, &v);
}

static void vehicle_remove(wgf_actor_t actor)
{
    vehicle_t *v = vehicle_of(actor);
    if (v != NULL) unmake_vehicle(v);
}

static void vehicle_set(wgf_actor_t actor, const char *key, const char *text)
{
    vehicle_t *v = vehicle_of(actor);
    double n[8];
    const int count = wgf_physics3d_priv_read_value("vehicle", vehicle_keys, (int)(sizeof(vehicle_keys) / sizeof(vehicle_keys[0])), key, text, n);
    const float x = count > 0 ? (float)n[0] : 0.0f;
    int i;
    if (v == NULL || count < 0) return;
    v->dirty = true;
    if (strcmp(key, "wheels") == 0) {
        snprintf(v->names, sizeof(v->names), "%s", text);
    } else if (strcmp(key, "radius") == 0) v->radius = x > 0.01f ? x : 0.01f;
    else if (strcmp(key, "width") == 0) v->width = x > 0.01f ? x : 0.01f;
    else if (strcmp(key, "suspension") == 0) v->travel = x > 0.0f ? x : 0.0f;
    else if (strcmp(key, "stiffness") == 0) v->stiffness = x > 0.1f ? x : 0.1f;
    else if (strcmp(key, "damping") == 0) v->damping = x > 0.0f ? x : 0.0f;
    else if (strcmp(key, "steering") == 0) v->steering = x > 0.0f ? x : 0.0f;
    else if (strcmp(key, "grip") == 0) v->grip = x > 0.0f ? x : 0.0f;
    else if (strcmp(key, "anti_roll") == 0) v->anti_roll = x > 0.0f ? x : 0.0f;
    else if (strcmp(key, "engine_torque") == 0) v->torque = x > 0.0f ? x : 0.0f;
    else if (strcmp(key, "max_rpm") == 0) v->max_rpm = x > 100.0f ? x : 100.0f;
    else if (strcmp(key, "gears") == 0 && count > 0) {
        v->gear_count = count < GEARS_MAX ? count : GEARS_MAX;
        for (i = 0; i < v->gear_count; i++) v->gears[i] = (float)n[i];
    } else if (strcmp(key, "drive") == 0) {
        const int w = word_of(text, drives_words, 3);
        if (w >= 0) v->drive = w;
        else wgf_log_warn("wgf_physics3d: a vehicle's drive is front, rear, or all (\"%s\")", text);
    }
}

static void vehicle_describe(wgf_actor_t actor, char *out, size_t size)
{
    const vehicle_t *v = vehicle_of(actor);
    int i;
    if (v == NULL) return;
    if (v->wheel_count > 0) {
        put(out, size, " wheels=");
        for (i = 0; i < v->wheel_count; i++) { /* an unnamed one as the dump names it: _<its index> */
            const char *name = wgf_actor_get_name(v->wheels[i]);
            if (i > 0) put(out, size, ",");
            if (name[0] != '\0') put(out, size, "%s", name);
            else put(out, size, "_%d", wgf_actor_get_index(v->wheels[i]));
        }
    } else if (v->names[0] != '\0') {
        put(out, size, " wheels=%s", v->names);
    }
    put(out, size, " radius=%.9g width=%.9g suspension=%.9g stiffness=%.9g damping=%.9g steering=%.9g grip=%.9g",
        v->radius, v->width, v->travel, v->stiffness, v->damping, v->steering, v->grip);
    put(out, size, " engine_torque=%.9g max_rpm=%.9g gears=", v->torque, v->max_rpm);
    for (i = 0; i < v->gear_count; i++) put(out, size, i == 0 ? "%.9g" : ",%.9g", v->gears[i]);
    put(out, size, " drive=%s", drives_words[v->drive]);
    if (v->anti_roll > 0.0f) put(out, size, " anti_roll=%.9g", v->anti_roll);
}



static wgf_ecs_priv_part_component_t vehicle_hooks = {0, vehicle_add, vehicle_remove, vehicle_set, vehicle_describe};

/* ---- physics3d's hooks ------------------------------------------------------------------- */

static void start(void)
{
    wgf_ecs_priv_id_t terms[2];
    state.id = wgf_ecs_priv_store_component(sizeof(vehicle_t), _Alignof(vehicle_t));
    terms[0] = state.id;
    terms[1] = wgf_ecs_priv_ids()->ref;
    state.query = wgf_ecs_priv_store_query(terms, 2);
    vehicle_hooks.id = state.id;
    wgf_ecs_priv_set_part_component(WGF_COMPONENT_VEHICLE, &vehicle_hooks);
}

static void stop(void)
{
    wgf_ecs_priv_set_part_component(WGF_COMPONENT_VEHICLE, NULL);
    if (state.query != NULL) wgf_ecs_priv_store_query_free(state.query);
    memset(&state, 0, sizeof(state));
}

static void before_step(void)
{
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    if (state.query == NULL) return;
    wgf_ecs_priv_store_walk(state.query);
    while (wgf_ecs_priv_store_next(state.query, fields, &entity)) {
        vehicle_t *v = (vehicle_t *)fields[0];
        const wgf_actor_t actor = ((const wgf_ecs_priv_ref_t *)fields[1])->actor;
        if (v->dirty) make_vehicle(actor, v);
        if (v->index >= 0) {
            wgf_physics3d_priv_jolt_vehicle_set_input(v->index, v->throttle, v->steer, v->brake, v->hand_brake ? 1.0f : 0.0f);
        }
    }
}

static void after_step(void)
{
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    int i;
    if (state.query == NULL) return;
    wgf_ecs_priv_store_walk(state.query);
    while (wgf_ecs_priv_store_next(state.query, fields, &entity)) {
        const vehicle_t *v = (const vehicle_t *)fields[0];
        if (v->index < 0) continue;
        for (i = 0; i < v->wheel_count; i++) {
            wgf_gfx_priv_actor_t *wheel_ptr = wgf_gfx_priv_actor_of(v->wheels[i]);
            float p[3], q[4];
            if (wheel_ptr == NULL) continue;
            wgf_physics3d_priv_jolt_vehicle_get_wheel(v->index, i, p, q);
            wheel_ptr->position = wgf_vec3_make(p[0], p[1], p[2]);
            wheel_ptr->rotation = wgf_quat_normalize(wgf_quat_make(q[0], q[1], q[2], q[3]));
            wgf_gfx_priv_actor_transform_changed(v->wheels[i]);
        }
    }
}

static void body_unmade(wgf_actor_t actor)
{
    vehicle_t *v = vehicle_of(actor);
    if (v == NULL) return;
    unmake_vehicle(v);
    v->dirty = true;
}

static void draw(wgf_color_t color, void (*ring)(wgf_mat4_t m, float radius, int axis, float offset, wgf_color_t c))
{
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    int i;
    if (state.query == NULL) return;
    wgf_ecs_priv_store_walk(state.query);
    while (wgf_ecs_priv_store_next(state.query, fields, &entity)) {
        const vehicle_t *v = (const vehicle_t *)fields[0];
        for (i = 0; v->index >= 0 && i < v->wheel_count; i++) {
            const wgf_mat4_t wheel = wgf_gfx_priv_actor_get_simulated_world(v->wheels[i]);
            ring(wheel, v->radius, 0, 0.0f, color); /* a wheel turns about its x */
        }
    }
}

static const wgf_physics3d_priv_vehicles_t hooks = {start, stop, before_step, after_step, body_unmade, draw};

WGF_CORE_PRIV_ON_LINK(link_vehicles)
{
    wgf_physics3d_priv_set_vehicles(&hooks);
}

bool wgf_physics3d_enable_vehicles(void)
{
    wgf_physics3d_priv_set_vehicles(&hooks);
    return true;
}

/* ---- vehicles ---------------------------------------------------------------------------- */

bool wgf_vehicle_set_wheels(wgf_actor_t actor, const wgf_actor_t *wheels, int count)
{
    vehicle_t *v;
    int i;
    if (wheels == NULL || count < 2 || count > WHEELS_MAX || count % 2 != 0) return false;
    for (i = 0; i < count; i++) {
        if (wgf_actor_get_parent(wheels[i]) != actor || actor == 0) return false;
    }
    if ((v = changing_vehicle(actor)) == NULL) return false;
    memcpy(v->wheels, wheels, sizeof(wgf_actor_t) * (size_t)count);
    v->wheel_count = count;
    v->names[0] = '\0';
    return true;
}

int wgf_vehicle_get_wheel_count(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL ? v->wheel_count : 0;
}

wgf_actor_t wgf_vehicle_get_wheel(wgf_actor_t actor, int index)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL && index >= 0 && index < v->wheel_count ? v->wheels[index] : 0;
}

bool wgf_vehicle_set_wheel_size(wgf_actor_t actor, float radius, float width)
{
    vehicle_t *v = changing_vehicle(actor);
    if (v == NULL) return false;
    v->radius = at_least(radius, 0.01f);
    v->width = at_least(width, 0.01f);
    return true;
}

bool wgf_vehicle_set_suspension(wgf_actor_t actor, float travel, float stiffness, float damping)
{
    vehicle_t *v = changing_vehicle(actor);
    if (v == NULL) return false;
    v->travel = at_least(travel, 0.0f);
    v->stiffness = at_least(stiffness, 0.1f);
    v->damping = at_least(damping, 0.0f);
    return true;
}

bool wgf_vehicle_set_steering(wgf_actor_t actor, float radians)
{
    vehicle_t *v = changing_vehicle(actor);
    if (v == NULL) return false;
    v->steering = at_least(radians, 0.0f);
    return true;
}

bool wgf_vehicle_set_grip(wgf_actor_t actor, float grip)
{
    vehicle_t *v = changing_vehicle(actor);
    if (v == NULL) return false;
    v->grip = at_least(grip, 0.0f);
    return true;
}

bool wgf_vehicle_set_engine(wgf_actor_t actor, float torque, float max_rpm)
{
    vehicle_t *v = changing_vehicle(actor);
    if (v == NULL) return false;
    v->torque = at_least(torque, 0.0f);
    v->max_rpm = at_least(max_rpm, 100.0f);
    return true;
}

bool wgf_vehicle_set_gears(wgf_actor_t actor, const float *ratios, int count)
{
    vehicle_t *v;
    int i;
    if (ratios == NULL || count < 1 || count > GEARS_MAX) return false;
    for (i = 0; i < count; i++) {
        if (!(ratios[i] > 0.0f)) return false;
    }
    if ((v = changing_vehicle(actor)) == NULL) return false;
    memcpy(v->gears, ratios, sizeof(float) * (size_t)count);
    v->gear_count = count;
    return true;
}

bool wgf_vehicle_set_drive(wgf_actor_t actor, wgf_vehicle_drive_t drive)
{
    vehicle_t *v;
    if ((int)drive < WGF_VEHICLE_DRIVE_FRONT || drive > WGF_VEHICLE_DRIVE_ALL) return false;
    if ((v = changing_vehicle(actor)) == NULL) return false;
    v->drive = drive;
    return true;
}

bool wgf_vehicle_set_anti_roll(wgf_actor_t actor, float stiffness)
{
    vehicle_t *v;
    if (!wgf_physics3d_priv_start() || (v = vehicle_of(actor)) == NULL) return false;
    v->anti_roll = at_least(stiffness, 0.0f);
    if (v->index >= 0 && !v->dirty) wgf_physics3d_priv_jolt_vehicle_set_anti_roll(v->index, v->anti_roll);
    return true;
}

float wgf_vehicle_get_anti_roll(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL ? v->anti_roll : 0.0f;
}

wgf_vec2_t wgf_vehicle_get_wheel_size(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL ? wgf_vec2_make(v->radius, v->width) : wgf_vec2_make(0.0f, 0.0f);
}

wgf_vec3_t wgf_vehicle_get_suspension(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL ? wgf_vec3_make(v->travel, v->stiffness, v->damping) : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

float wgf_vehicle_get_steering(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL ? v->steering : 0.0f;
}

float wgf_vehicle_get_grip(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL ? v->grip : 0.0f;
}

wgf_vec2_t wgf_vehicle_get_engine(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL ? wgf_vec2_make(v->torque, v->max_rpm) : wgf_vec2_make(0.0f, 0.0f);
}

int wgf_vehicle_get_gear_count(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL ? v->gear_count : 0;
}

float wgf_vehicle_get_gear_ratio(wgf_actor_t actor, int index)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL && index >= 0 && index < v->gear_count ? v->gears[index] : 0.0f;
}

wgf_vehicle_drive_t wgf_vehicle_get_drive(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL ? (wgf_vehicle_drive_t)v->drive : WGF_VEHICLE_DRIVE_REAR;
}

static float clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

bool wgf_vehicle_set_input(wgf_actor_t actor, float throttle, float brake, float steer, bool hand_brake)
{
    vehicle_t *v = vehicle_of(actor);
    if (v == NULL) return false;
    v->throttle = clamp(throttle, -1.0f, 1.0f);
    v->brake = clamp(brake, 0.0f, 1.0f);
    v->steer = clamp(steer, -1.0f, 1.0f);
    v->hand_brake = hand_brake;
    return true;
}

float wgf_vehicle_get_speed(wgf_actor_t actor)
{
    uint32_t body = WGF_PHYSICS3D_PRIV_NO_BODY;
    bool dirty;
    int type;
    float l[3], a[3];
    wgf_vec3_t forward;
    if (vehicle_of(actor) == NULL || !wgf_physics3d_priv_body_info(actor, &body, &dirty, &type) ||
        body == WGF_PHYSICS3D_PRIV_NO_BODY) {
        return 0.0f;
    }
    wgf_physics3d_priv_jolt_body_get_velocity(body, l, a);
    forward = wgf_actor_get_world_direction(actor, 0.0f, 0.0f, 1.0f);
    return l[0] * forward.x + l[1] * forward.y + l[2] * forward.z;
}

float wgf_vehicle_get_rpm(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL && v->index >= 0 ? wgf_physics3d_priv_jolt_vehicle_get_rpm(v->index) : 0.0f;
}

int wgf_vehicle_get_gear(wgf_actor_t actor)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL && v->index >= 0 ? wgf_physics3d_priv_jolt_vehicle_get_gear(v->index) : 0;
}

float wgf_vehicle_get_wheel_slip(wgf_actor_t actor, int index)
{
    const vehicle_t *v = vehicle_of(actor);
    return v != NULL && v->index >= 0 ? wgf_physics3d_priv_jolt_vehicle_get_slip(v->index, index) : 0.0f;
}

bool wgf_vehicle_reset(wgf_actor_t actor)
{
    vehicle_t *v = vehicle_of(actor);
    uint32_t body = WGF_PHYSICS3D_PRIV_NO_BODY;
    bool dirty;
    int type;
    static const float none[3] = {0.0f, 0.0f, 0.0f};
    if (v == NULL) return false;
    v->throttle = v->brake = v->steer = 0.0f;
    v->hand_brake = false;
    if (v->index >= 0) wgf_physics3d_priv_jolt_vehicle_reset(v->index);
    if (wgf_physics3d_priv_body_info(actor, &body, &dirty, &type) && body != WGF_PHYSICS3D_PRIV_NO_BODY) {
        wgf_physics3d_priv_jolt_body_set_velocity(body, none, none);
    }
    return true;
}
