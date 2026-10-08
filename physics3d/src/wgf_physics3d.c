#include "wgf_physics.h"
#include "wgf_body.h"
#include "wgf_vehicle.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "mesh/wgf_gfx_mesh_priv.h"
#include "wgf_component.h"
#include "wgf_core_part_priv.h"
#include "wgf_draw.h"
#include "wgf_ecs_priv.h"
#include "wgf_log.h"
#include "wgf_mat4.h"
#include "wgf_model.h"
#include "wgf_physics3d_jolt_priv.h"
#include "wgf_quat.h"

/* physics3d (wgf_physics.h): the body and vehicle components, the ecs's through its hooks
 * for a part's components (wgf_ecs_priv.h), their data the store's; each tick, after the
 * ecs's systems, the world made to match them (a body or vehicle whose settings changed made
 * again, one the program moved put where it is), stepped, and the actors set where it put
 * them; then the sensors' overlaps raised as the ecs's triggers. A part (wgf_core_part_priv.h):
 * installed by the first call that starts physics, so a program that makes no body links
 * none of it, Jolt included. */

#define GRAVITY_DEFAULT -9.81f
#define WHEELS_MAX 8
#define GEARS_MAX 8
#define NAMES_MAX 256

typedef struct body_t {
    uint32_t id; /* Jolt's; WGF_PHYSICS3D_PRIV_NO_BODY until made */
    int type, shape;
    float size[3], mass, friction, bounce, damping[2];
    int layer, mask;
    bool dirty; /* made again at the next tick */
    float velocity[3], spin[3], impulse[3];
    bool moving_set, impulse_set; /* a velocity or a push waiting for the body to be made */
    wgf_vec3_t placed;            /* its actor's position and rotation as physics last left them */
    wgf_quat_t placed_rotation;
    float *edges; /* a convex or mesh body's triangles, for the debug view: 9 floats each, malloc'd */
    int triangles;
} body_t;

typedef struct vehicle_t {
    int index; /* Jolt's; -1 until made */
    wgf_actor_t wheels[WHEELS_MAX];
    int wheel_count;
    char names[NAMES_MAX]; /* a scene's wheels=, found among its children as it is made */
    float radius, width, travel, stiffness, damping, steering, grip, torque, max_rpm;
    float gears[GEARS_MAX];
    int gear_count, drive;
    float throttle, brake, steer;
    bool hand_brake, dirty;
} vehicle_t;

static struct {
    bool started;
    float gravity[3];
    wgf_ecs_priv_id_t body_id, vehicle_id;
    wgf_ecs_priv_query_t *bodies, *vehicles;
    float dt; /* the last tick's, for a kinematic body's moves */
} physics;

static bool start(void);

/* ---- the components' data ------------------------------------------------------------- */

static body_t *body_of(wgf_actor_t actor)
{
    if (!physics.started) return NULL;
    return (body_t *)wgf_ecs_priv_get(actor, physics.body_id);
}

static vehicle_t *vehicle_of(wgf_actor_t actor)
{
    if (!physics.started) return NULL;
    return (vehicle_t *)wgf_ecs_priv_get(actor, physics.vehicle_id);
}

/* A setter's body: physics started, the actor's body, its settings to be made again. */
static body_t *changing(wgf_actor_t actor)
{
    body_t *b;
    if (!start()) return NULL;
    b = body_of(actor);
    if (b != NULL) b->dirty = true;
    return b;
}

static vehicle_t *changing_vehicle(wgf_actor_t actor)
{
    vehicle_t *v;
    if (!start()) return NULL;
    v = vehicle_of(actor);
    if (v != NULL) v->dirty = true;
    return v;
}

/* ---- where an actor is ---------------------------------------------------------------- */

/* The actor's place in its tree's root's space, the simulation's (not where it is drawn). */
static void world_pose(wgf_actor_t actor, float position[3], float rotation[4])
{
    const wgf_mat4_t world = wgf_gfx_priv_actor_get_simulated_world(actor);
    const wgf_vec3_t p = wgf_mat4_get_translation(world);
    const wgf_quat_t q = wgf_mat4_get_rotation(world);
    position[0] = p.x;
    position[1] = p.y;
    position[2] = p.z;
    rotation[0] = q.x;
    rotation[1] = q.y;
    rotation[2] = q.z;
    rotation[3] = q.w;
}

/* The actor put at a pose in its root's space, as its own transform under its parent. */
static void put_actor(wgf_actor_t actor, const float position[3], const float rotation[4])
{
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    wgf_vec3_t p = wgf_vec3_make(position[0], position[1], position[2]);
    wgf_quat_t q = wgf_quat_make(rotation[0], rotation[1], rotation[2], rotation[3]);
    if (actor_ptr == NULL) return;
    if (actor_ptr->parent != 0) {
        const wgf_mat4_t parent = wgf_gfx_priv_actor_get_simulated_world(actor_ptr->parent);
        const wgf_mat4_t local = wgf_mat4_mul(wgf_mat4_invert(parent), wgf_mat4_from_trs(p, q, wgf_vec3_make(1, 1, 1)));
        p = wgf_mat4_get_translation(local);
        q = wgf_mat4_get_rotation(local);
    }
    actor_ptr->position = p;
    actor_ptr->rotation = wgf_quat_normalize(q);
    wgf_gfx_priv_actor_transform_changed(actor);
}

static bool same_place(const body_t *b, const wgf_gfx_priv_actor_t *actor_ptr)
{
    return b->placed.x == actor_ptr->position.x && b->placed.y == actor_ptr->position.y &&
           b->placed.z == actor_ptr->position.z && b->placed_rotation.x == actor_ptr->rotation.x &&
           b->placed_rotation.y == actor_ptr->rotation.y && b->placed_rotation.z == actor_ptr->rotation.z &&
           b->placed_rotation.w == actor_ptr->rotation.w;
}

static void note_place(body_t *b, wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    if (actor_ptr == NULL) return;
    b->placed = actor_ptr->position;
    b->placed_rotation = actor_ptr->rotation;
}

/* ---- a shape from models -------------------------------------------------------------- */

typedef struct soup_t {
    float *points;
    uint32_t *indices;
    int point_count, index_count, point_capacity, index_capacity;
} soup_t;

static bool grow(void **array, int *capacity, int need, size_t size)
{
    void *grown;
    int c = *capacity > 0 ? *capacity : 256;
    if (need <= *capacity) return true;
    while (c < need) c *= 2;
    grown = realloc(*array, size * (size_t)c);
    if (grown == NULL) return false;
    *array = grown;
    *capacity = c;
    return true;
}

/* Every model at and under `actor`, its mesh's triangles in `root`'s space. */
static void gather(wgf_actor_t root, wgf_mat4_t root_inverse, wgf_actor_t actor, soup_t *soup, int depth)
{
    int i, n;
    if (depth > 32) return;
    if (wgf_actor_get_kind(actor) == WGF_ACTOR_KIND_MODEL) {
        const wgf_mesh_t mesh = wgf_model_get_mesh(actor);
        int vertex_count = 0, index_count = 0;
        const float *vertices = mesh != 0 ? wgf_gfx_priv_mesh_get_vertices(mesh, &vertex_count) : NULL;
        const uint32_t *indices = mesh != 0 ? wgf_gfx_priv_mesh_get_indices(mesh, &index_count) : NULL;
        if (vertices != NULL && indices != NULL && vertex_count > 0 &&
            grow((void **)&soup->points, &soup->point_capacity, 3 * (soup->point_count + vertex_count), sizeof(float)) &&
            grow((void **)&soup->indices, &soup->index_capacity, soup->index_count + index_count, sizeof(uint32_t))) {
            const wgf_mat4_t to_root =
                wgf_mat4_mul(root_inverse, wgf_gfx_priv_actor_get_simulated_world(actor));
            const uint32_t base = (uint32_t)soup->point_count;
            for (i = 0; i < vertex_count; i++) {
                const float *v = vertices + (size_t)i * WGF_GFX_PRIV_MESH_VERTEX_FLOATS;
                const wgf_vec3_t p = wgf_mat4_transform_point(to_root, wgf_vec3_make(v[0], v[1], v[2]));
                soup->points[3 * soup->point_count] = p.x;
                soup->points[3 * soup->point_count + 1] = p.y;
                soup->points[3 * soup->point_count + 2] = p.z;
                soup->point_count++;
            }
            for (i = 0; i < index_count; i++) soup->indices[soup->index_count++] = base + indices[i];
        }
    }
    (void)root;
    n = wgf_actor_get_child_count(actor);
    for (i = 0; i < n; i++) gather(root, root_inverse, wgf_actor_get_child(actor, i), soup, depth + 1);
}

/* ---- making the world's parts ----------------------------------------------------------- */

static void forget_edges(body_t *b)
{
    free(b->edges);
    b->edges = NULL;
    b->triangles = 0;
}

static void unmake_vehicle(vehicle_t *v)
{
    if (v->index >= 0) wgf_physics3d_priv_jolt_vehicle_destroy(v->index);
    v->index = -1;
}

static void unmake_body(wgf_actor_t actor, body_t *b)
{
    vehicle_t *v = vehicle_of(actor);
    if (v != NULL) { /* its vehicle is made again on the new body */
        unmake_vehicle(v);
        v->dirty = true;
    }
    if (b->id != WGF_PHYSICS3D_PRIV_NO_BODY) wgf_physics3d_priv_jolt_body_destroy(b->id);
    b->id = WGF_PHYSICS3D_PRIV_NO_BODY;
    forget_edges(b);
}

static void make_body(wgf_actor_t actor, body_t *b)
{
    wgf_physics3d_priv_body_desc_t d;
    soup_t soup;
    memset(&d, 0, sizeof(d));
    memset(&soup, 0, sizeof(soup));
    unmake_body(actor, b);
    d.motion = b->type == WGF_BODY_TYPE_DYNAMIC     ? WGF_PHYSICS3D_PRIV_DYNAMIC
               : b->type == WGF_BODY_TYPE_KINEMATIC ? WGF_PHYSICS3D_PRIV_KINEMATIC
                                                    : WGF_PHYSICS3D_PRIV_STATIC;
    d.sensor = b->type == WGF_BODY_TYPE_SENSOR;
    d.shape = (wgf_physics3d_priv_shape_t)b->shape;
    switch (b->shape) {
        case WGF_BODY_SHAPE_BOX:
            d.size[0] = b->size[0] * 0.5f;
            d.size[1] = b->size[1] * 0.5f;
            d.size[2] = b->size[2] * 0.5f;
            break;
        case WGF_BODY_SHAPE_SPHERE: d.size[0] = b->size[0]; break;
        case WGF_BODY_SHAPE_CAPSULE:
            d.size[0] = b->size[0];
            d.size[1] = b->size[1] * 0.5f - b->size[0]; /* the cylinder between its ends */
            if (d.size[1] < 0.01f) d.size[1] = 0.01f;
            break;
        default: {
            const wgf_mat4_t inverse = wgf_mat4_invert(wgf_gfx_priv_actor_get_simulated_world(actor));
            gather(actor, inverse, actor, &soup, 0);
            d.points = soup.points;
            d.point_count = soup.point_count;
            d.indices = soup.indices;
            d.index_count = soup.index_count;
            if (b->shape == WGF_BODY_SHAPE_MESH && (d.motion != WGF_PHYSICS3D_PRIV_STATIC || d.sensor)) {
                d.shape = WGF_PHYSICS3D_PRIV_CONVEX; /* a mesh moves as its hull */
            }
            break;
        }
    }
    world_pose(actor, d.position, d.rotation);
    d.mass = b->mass;
    d.friction = b->friction;
    d.restitution = b->bounce;
    d.linear_damping = b->damping[0];
    d.angular_damping = b->damping[1];
    d.layer = (uint32_t)b->layer;
    d.mask = (uint32_t)b->mask;
    d.user = (uint64_t)actor;
    b->id = wgf_physics3d_priv_jolt_body_create(&d);
    b->dirty = false;
    if (b->id == WGF_PHYSICS3D_PRIV_NO_BODY) {
        wgf_log_warn("wgf_physics: a body couldn't be made (a convex or mesh shape with no model ready under it, or "
                     "too many bodies); it is made again when its settings change");
    } else {
        if (b->moving_set) wgf_physics3d_priv_jolt_body_set_velocity(b->id, b->velocity, b->spin);
        if (b->impulse_set) wgf_physics3d_priv_jolt_body_add_impulse(b->id, b->impulse);
        if (soup.index_count >= 3) { /* kept for the debug view */
            b->edges = (float *)malloc(sizeof(float) * 9 * (size_t)(soup.index_count / 3));
            if (b->edges != NULL) {
                int t, k;
                for (t = 0; t + 2 < soup.index_count; t += 3) {
                    for (k = 0; k < 3; k++) {
                        const uint32_t i = soup.indices[t + k];
                        memcpy(&b->edges[9 * b->triangles + 3 * k], &soup.points[3 * i], sizeof(float) * 3);
                    }
                    b->triangles++;
                }
            }
        }
    }
    b->moving_set = b->impulse_set = false;
    free(soup.points);
    free(soup.indices);
    note_place(b, actor);
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
            wgf_log_warn("wgf_physics: a vehicle's wheel \"%s\" isn't one of its children", name);
            v->wheel_count = 0;
            break;
        }
        v->wheels[v->wheel_count++] = wheel;
    }
    v->names[0] = '\0';
}

static void make_vehicle(wgf_actor_t actor, vehicle_t *v)
{
    const body_t *b = body_of(actor);
    wgf_physics3d_priv_vehicle_desc_t d;
    int i;
    unmake_vehicle(v);
    find_wheels(actor, v);
    v->dirty = false;
    if (b == NULL || b->id == WGF_PHYSICS3D_PRIV_NO_BODY || b->type != WGF_BODY_TYPE_DYNAMIC || v->wheel_count < 2) {
        if (v->wheel_count >= 2) wgf_log_warn("wgf_physics: a vehicle needs a dynamic body on its actor");
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
    d.engine_torque = v->torque;
    d.max_rpm = v->max_rpm;
    memcpy(d.gears, v->gears, sizeof(d.gears));
    d.gear_count = v->gear_count;
    d.drive = v->drive;
    v->index = wgf_physics3d_priv_jolt_vehicle_create(b->id, &d);
    if (v->index < 0) wgf_log_warn("wgf_physics: a vehicle couldn't be made (more than 64?)");
}

/* ---- the components, through the ecs's hooks ------------------------------------------- */

static void body_add(wgf_actor_t actor)
{
    body_t b;
    wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_make(actor);
    if (record == NULL) return;
    memset(&b, 0, sizeof(b));
    b.id = WGF_PHYSICS3D_PRIV_NO_BODY;
    b.type = WGF_BODY_TYPE_DYNAMIC;
    b.shape = WGF_BODY_SHAPE_BOX;
    b.size[0] = b.size[1] = b.size[2] = 1.0f;
    b.friction = 0.5f;
    b.damping[0] = b.damping[1] = 0.05f;
    b.layer = 1;
    b.mask = 0x7FFF;
    b.dirty = true;
    wgf_ecs_priv_store_set(record->id, physics.body_id, &b);
}

static void body_remove(wgf_actor_t actor)
{
    body_t *b = body_of(actor);
    if (b != NULL) unmake_body(actor, b);
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
    wgf_ecs_priv_store_set(record->id, physics.vehicle_id, &v);
}

static void vehicle_remove(wgf_actor_t actor)
{
    vehicle_t *v = vehicle_of(actor);
    if (v != NULL) unmake_vehicle(v);
}

/* A scene line's value as numbers, a comma-separated list of up to `most`: how many, -1
 * for text that isn't one. */
static int numbers_of(const char *text, double *n, int most)
{
    int count = 0;
    for (;;) {
        char *end;
        if (count == most) return -1;
        n[count++] = strtod(text, &end);
        if (end == text || !isfinite(n[count - 1])) return -1;
        if (*end == '\0') return count;
        if (*end != ',') return -1;
        text = end + 1;
    }
}

static const char *const types[] = {"static", "dynamic", "kinematic", "sensor"};
static const char *const shapes[] = {"box", "sphere", "capsule", "convex", "mesh"};
static const char *const drives[] = {"front", "rear", "all"};

static int word_of(const char *text, const char *const *words, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        if (strcmp(text, words[i]) == 0) return i;
    }
    return -1;
}

/* What a scene's line said, as the keys it can have (the words' keys first). */
typedef struct line_key_t {
    const char *key;
    int count; /* numbers it takes; 0 a word or names; -1 a list of 1 to 8 */
} line_key_t;

static const line_key_t body_keys[] = {{"type", 0},     {"shape", 0},    {"size", 3},    {"radius", 1}, {"height", 1},
                                       {"mass", 1},     {"friction", 1}, {"bounce", 1},  {"damping", 2}, {"layer", 1},
                                       {"mask", 1},     {"velocity", 3}, {"spin", 3}};
static const line_key_t vehicle_keys[] = {{"wheels", 0},    {"drive", 0},         {"radius", 1},  {"width", 1},
                                          {"suspension", 1}, {"stiffness", 1},    {"damping", 1}, {"steering", 1},
                                          {"grip", 1},       {"engine_torque", 1}, {"max_rpm", 1}, {"gears", -1}};

/* `value`'s numbers as `key` takes them, into n: how many; -1, warned, for a key the line
 * hasn't or a value it can't take. */
static int read_value(const char *line, const line_key_t *keys, int key_count, const char *key, const char *value,
                      double *n)
{
    int i, count;
    for (i = 0; i < key_count && strcmp(keys[i].key, key) != 0; i++) {
    }
    if (i == key_count) {
        wgf_log_warn("wgf_physics: a %s line has no key \"%s\"", line, key);
        return -1;
    }
    if (keys[i].count == 0) return 0;
    count = numbers_of(value, n, 8);
    if (count < 1 || (keys[i].count > 0 && count != keys[i].count)) {
        wgf_log_warn("wgf_physics: a %s line's %s takes %d number(s) (\"%s\")", line, key, keys[i].count > 0 ? keys[i].count : 8,
                     value);
        return -1;
    }
    return count;
}

/* A scene's body line's setting. */
static void body_set(wgf_actor_t actor, const char *key, const char *text)
{
    body_t *b = body_of(actor);
    double n[8];
    const int count = read_value("body", body_keys, (int)(sizeof(body_keys) / sizeof(body_keys[0])), key, text, n);
    const float x = count > 0 ? (float)n[0] : 0.0f;
    int w;
    if (b == NULL || count < 0) return;
    if (strcmp(key, "type") == 0) {
        if ((w = word_of(text, types, 4)) >= 0) wgf_body_set_type(actor, (wgf_body_type_t)w);
        else wgf_log_warn("wgf_physics: a body's type is static, dynamic, kinematic, or sensor (\"%s\")", text);
    } else if (strcmp(key, "shape") == 0) {
        if ((w = word_of(text, shapes, 5)) >= 0) {
            b->shape = w;
            b->dirty = true;
        } else {
            wgf_log_warn("wgf_physics: a body's shape is box, sphere, capsule, convex, or mesh (\"%s\")", text);
        }
    } else if (strcmp(key, "size") == 0 && count == 3) {
        b->size[0] = (float)n[0];
        b->size[1] = (float)n[1];
        b->size[2] = (float)n[2];
        b->dirty = true;
    } else if (strcmp(key, "radius") == 0) {
        b->size[0] = x;
        b->dirty = true;
    } else if (strcmp(key, "height") == 0) {
        b->size[1] = x;
        b->dirty = true;
    } else if (strcmp(key, "mass") == 0) wgf_body_set_mass(actor, x);
    else if (strcmp(key, "friction") == 0) wgf_body_set_friction(actor, x);
    else if (strcmp(key, "bounce") == 0) wgf_body_set_bounce(actor, x);
    else if (strcmp(key, "damping") == 0 && count == 2) wgf_body_set_damping(actor, x, (float)n[1]);
    else if (strcmp(key, "layer") == 0) wgf_body_set_layer(actor, (int)x);
    else if (strcmp(key, "mask") == 0) wgf_body_set_mask(actor, (int)x);
    else if (strcmp(key, "velocity") == 0 && count == 3) wgf_body_set_velocity(actor, x, (float)n[1], (float)n[2]);
    else if (strcmp(key, "spin") == 0 && count == 3) wgf_body_set_spin(actor, x, (float)n[1], (float)n[2]);
}

static void vehicle_set(wgf_actor_t actor, const char *key, const char *text)
{
    vehicle_t *v = vehicle_of(actor);
    double n[8];
    const int count = read_value("vehicle", vehicle_keys, (int)(sizeof(vehicle_keys) / sizeof(vehicle_keys[0])), key, text, n);
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
    else if (strcmp(key, "engine_torque") == 0) v->torque = x > 0.0f ? x : 0.0f;
    else if (strcmp(key, "max_rpm") == 0) v->max_rpm = x > 100.0f ? x : 100.0f;
    else if (strcmp(key, "gears") == 0 && count > 0) {
        v->gear_count = count < GEARS_MAX ? count : GEARS_MAX;
        for (i = 0; i < v->gear_count; i++) v->gears[i] = (float)n[i];
    } else if (strcmp(key, "drive") == 0) {
        const int w = word_of(text, drives, 3);
        if (w >= 0) v->drive = w;
        else wgf_log_warn("wgf_physics: a vehicle's drive is front, rear, or all (\"%s\")", text);
    }
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

static void body_describe(wgf_actor_t actor, char *out, size_t size)
{
    const body_t *b = body_of(actor);
    if (b == NULL) return;
    put(out, size, " type=%s shape=%s", types[b->type], shapes[b->shape]);
    if (b->shape == WGF_BODY_SHAPE_BOX) put(out, size, " size=%.9g,%.9g,%.9g", b->size[0], b->size[1], b->size[2]);
    if (b->shape == WGF_BODY_SHAPE_SPHERE || b->shape == WGF_BODY_SHAPE_CAPSULE) put(out, size, " radius=%.9g", b->size[0]);
    if (b->shape == WGF_BODY_SHAPE_CAPSULE) put(out, size, " height=%.9g", b->size[1]);
    put(out, size, " mass=%.9g friction=%.9g bounce=%.9g damping=%.9g,%.9g layer=%d mask=%d", b->mass, b->friction,
        b->bounce, b->damping[0], b->damping[1], b->layer, b->mask);
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
    put(out, size, " drive=%s", drives[v->drive]);
}

/* the components' hooks, their store ids filled as physics starts */
static wgf_ecs_priv_part_component_t body_hooks = {0, body_add, body_remove, body_set, body_describe};
static wgf_ecs_priv_part_component_t vehicle_hooks = {0, vehicle_add, vehicle_remove, vehicle_set, vehicle_describe};

/* ---- the tick ---------------------------------------------------------------------------- */

static int32_t layer_of(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->layer : 0;
}

static void tick(float dt)
{
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    wgf_physics3d_priv_overlap_t overlaps[64];
    int n, i;
    physics.dt = dt;
    /* the world made to match: a body made again, one the program moved put there, a
       kinematic one (or a sensor) taken where its actor is */
    wgf_ecs_priv_store_walk(physics.bodies);
    while (wgf_ecs_priv_store_next(physics.bodies, fields, &entity)) {
        body_t *b = (body_t *)fields[0];
        const wgf_actor_t actor = ((const wgf_ecs_priv_ref_t *)fields[1])->actor;
        const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
        if (actor_ptr == NULL) continue;
        if (b->dirty || b->id == WGF_PHYSICS3D_PRIV_NO_BODY) {
            if (b->dirty) make_body(actor, b);
            continue;
        }
        if (b->type == WGF_BODY_TYPE_KINEMATIC || b->type == WGF_BODY_TYPE_SENSOR) {
            float p[3], q[4];
            world_pose(actor, p, q);
            wgf_physics3d_priv_jolt_body_move(b->id, p, q, dt);
            note_place(b, actor);
        } else if (!same_place(b, actor_ptr)) {
            float p[3], q[4];
            world_pose(actor, p, q);
            wgf_physics3d_priv_jolt_body_set_pose(b->id, p, q);
            note_place(b, actor);
        }
    }
    wgf_ecs_priv_store_walk(physics.vehicles);
    while (wgf_ecs_priv_store_next(physics.vehicles, fields, &entity)) {
        vehicle_t *v = (vehicle_t *)fields[0];
        const wgf_actor_t actor = ((const wgf_ecs_priv_ref_t *)fields[1])->actor;
        if (v->dirty) make_vehicle(actor, v);
        if (v->index >= 0) {
            wgf_physics3d_priv_jolt_vehicle_set_input(v->index, v->throttle, v->steer, v->brake, v->hand_brake ? 1.0f : 0.0f);
        }
    }

    wgf_physics3d_priv_jolt_step(dt);

    /* the actors set where the world put them */
    wgf_ecs_priv_store_walk(physics.bodies);
    while (wgf_ecs_priv_store_next(physics.bodies, fields, &entity)) {
        body_t *b = (body_t *)fields[0];
        const wgf_actor_t actor = ((const wgf_ecs_priv_ref_t *)fields[1])->actor;
        float p[3], q[4];
        if (b->type != WGF_BODY_TYPE_DYNAMIC || b->id == WGF_PHYSICS3D_PRIV_NO_BODY) continue;
        wgf_physics3d_priv_jolt_body_get_pose(b->id, p, q);
        put_actor(actor, p, q);
        note_place(b, actor);
    }
    wgf_ecs_priv_store_walk(physics.vehicles);
    while (wgf_ecs_priv_store_next(physics.vehicles, fields, &entity)) {
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

    /* the sensors' overlaps, told to both as the ecs's triggers */
    while ((n = wgf_physics3d_priv_jolt_take_overlaps(overlaps, 64)) > 0) {
        for (i = 0; i < n; i++) {
            const wgf_actor_t sensor = (wgf_actor_t)overlaps[i].sensor, other = (wgf_actor_t)overlaps[i].other;
            const wgf_world_event_t event = overlaps[i].entered ? WGF_WORLD_EVENT_TRIGGER_ENTER : WGF_WORLD_EVENT_TRIGGER_EXIT;
            wgf_ecs_priv_raise(event, (int)sensor, (int)other, layer_of(other));
            wgf_ecs_priv_raise(event, (int)other, (int)sensor, layer_of(sensor));
        }
    }
}

static void stop(void)
{
    if (!physics.started) return;
    wgf_ecs_priv_set_part_component(WGF_COMPONENT_BODY, NULL);
    wgf_ecs_priv_set_part_component(WGF_COMPONENT_VEHICLE, NULL);
    wgf_ecs_priv_store_query_free(physics.bodies);
    wgf_ecs_priv_store_query_free(physics.vehicles);
    wgf_physics3d_priv_jolt_stop();
    memset(&physics, 0, sizeof(physics));
}

static wgf_core_priv_part_t part = {.name = "physics3d",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_PHYSICS3D,
                                    .tick = tick,
                                    .stop = stop};

/* Physics started: the ecs's world, Jolt's, the components' hooks, the part. */
static bool start(void)
{
    wgf_ecs_priv_id_t terms[2];
    if (physics.started) return true;
    if (!wgf_ecs_priv_start()) return false;
    if (!wgf_physics3d_priv_jolt_start(0.0f, GRAVITY_DEFAULT, 0.0f)) {
        wgf_log_error("wgf_physics: couldn't start");
        return false;
    }
    physics.started = true;
    physics.gravity[1] = GRAVITY_DEFAULT;
    physics.body_id = wgf_ecs_priv_store_component(sizeof(body_t), _Alignof(body_t));
    physics.vehicle_id = wgf_ecs_priv_store_component(sizeof(vehicle_t), _Alignof(vehicle_t));
    terms[0] = physics.body_id;
    terms[1] = wgf_ecs_priv_ids()->ref;
    physics.bodies = wgf_ecs_priv_store_query(terms, 2);
    terms[0] = physics.vehicle_id;
    physics.vehicles = wgf_ecs_priv_store_query(terms, 2);
    body_hooks.id = physics.body_id;
    vehicle_hooks.id = physics.vehicle_id;
    wgf_ecs_priv_set_part_component(WGF_COMPONENT_BODY, &body_hooks);
    wgf_ecs_priv_set_part_component(WGF_COMPONENT_VEHICLE, &vehicle_hooks);
    wgf_core_priv_part_install(&part);
    return true;
}

/* ---- the calls ---------------------------------------------------------------------------- */

bool wgf_physics_set_gravity(float x, float y, float z)
{
    if (!start()) return false;
    physics.gravity[0] = x;
    physics.gravity[1] = y;
    physics.gravity[2] = z;
    wgf_physics3d_priv_jolt_set_gravity(x, y, z);
    return true;
}

wgf_vec3_t wgf_physics_get_gravity(void)
{
    return physics.started ? wgf_vec3_make(physics.gravity[0], physics.gravity[1], physics.gravity[2])
                           : wgf_vec3_make(0.0f, GRAVITY_DEFAULT, 0.0f);
}

bool wgf_body_set_type(wgf_actor_t actor, wgf_body_type_t type)
{
    body_t *b;
    if ((int)type < WGF_BODY_TYPE_STATIC || type > WGF_BODY_TYPE_SENSOR) return false;
    if ((b = changing(actor)) == NULL) return false;
    b->type = type;
    return true;
}

wgf_body_type_t wgf_body_get_type(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? (wgf_body_type_t)b->type : WGF_BODY_TYPE_STATIC;
}

static float at_least(float value, float least)
{
    return value > least ? value : least;
}

bool wgf_body_set_shape(wgf_actor_t actor, wgf_body_shape_t shape, float x, float y, float z)
{
    body_t *b;
    if ((int)shape < WGF_BODY_SHAPE_BOX || shape > WGF_BODY_SHAPE_MESH) return false;
    if ((b = changing(actor)) == NULL) return false;
    b->shape = shape;
    b->size[0] = at_least(x, 0.01f);
    b->size[1] = at_least(y, 0.01f);
    b->size[2] = at_least(z, 0.01f);
    return true;
}

wgf_body_shape_t wgf_body_get_shape(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? (wgf_body_shape_t)b->shape : WGF_BODY_SHAPE_BOX;
}

wgf_vec3_t wgf_body_get_size(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? wgf_vec3_make(b->size[0], b->size[1], b->size[2]) : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_body_set_mass(wgf_actor_t actor, float kilograms)
{
    body_t *b = changing(actor);
    if (b == NULL) return false;
    b->mass = at_least(kilograms, 0.0f);
    return true;
}

float wgf_body_get_mass(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->mass : 0.0f;
}

bool wgf_body_set_friction(wgf_actor_t actor, float friction)
{
    body_t *b = changing(actor);
    if (b == NULL) return false;
    b->friction = at_least(friction, 0.0f);
    return true;
}

float wgf_body_get_friction(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->friction : 0.0f;
}

bool wgf_body_set_bounce(wgf_actor_t actor, float bounce)
{
    body_t *b = changing(actor);
    if (b == NULL) return false;
    b->bounce = bounce < 0.0f ? 0.0f : (bounce > 1.0f ? 1.0f : bounce);
    return true;
}

float wgf_body_get_bounce(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->bounce : 0.0f;
}

static float unit(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

bool wgf_body_set_damping(wgf_actor_t actor, float linear, float angular)
{
    body_t *b = changing(actor);
    if (b == NULL) return false;
    b->damping[0] = unit(linear);
    b->damping[1] = unit(angular);
    return true;
}

wgf_vec2_t wgf_body_get_damping(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? wgf_vec2_make(b->damping[0], b->damping[1]) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_body_set_layer(wgf_actor_t actor, int layer)
{
    body_t *b;
    if (layer < 1 || layer > 0x7FFF) return false;
    if ((b = changing(actor)) == NULL) return false;
    b->layer = layer;
    return true;
}

int wgf_body_get_layer(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->layer : 0;
}

bool wgf_body_set_mask(wgf_actor_t actor, int mask)
{
    body_t *b;
    if (mask < 0 || mask > 0x7FFF) return false;
    if ((b = changing(actor)) == NULL) return false;
    b->mask = mask;
    return true;
}

int wgf_body_get_mask(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->mask : 0;
}

/* A velocity or spin: the world's body's when it is made, else kept for when it is. */
static bool set_moving(wgf_actor_t actor, const float *linear, const float *angular)
{
    body_t *b;
    if (!start() || (b = body_of(actor)) == NULL) return false;
    if (b->id != WGF_PHYSICS3D_PRIV_NO_BODY && !b->dirty) {
        float l[3], a[3];
        wgf_physics3d_priv_jolt_body_get_velocity(b->id, l, a);
        wgf_physics3d_priv_jolt_body_set_velocity(b->id, linear != NULL ? linear : l, angular != NULL ? angular : a);
        return true;
    }
    if (linear != NULL) memcpy(b->velocity, linear, sizeof(b->velocity));
    if (angular != NULL) memcpy(b->spin, angular, sizeof(b->spin));
    b->moving_set = true;
    return true;
}

bool wgf_body_set_velocity(wgf_actor_t actor, float x, float y, float z)
{
    const float v[3] = {x, y, z};
    return set_moving(actor, v, NULL);
}

bool wgf_body_set_spin(wgf_actor_t actor, float x, float y, float z)
{
    const float v[3] = {x, y, z};
    return set_moving(actor, NULL, v);
}

static wgf_vec3_t moving_of(wgf_actor_t actor, bool angular)
{
    const body_t *b = body_of(actor);
    float l[3] = {0, 0, 0}, a[3] = {0, 0, 0};
    if (b == NULL) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    if (b->id != WGF_PHYSICS3D_PRIV_NO_BODY) {
        wgf_physics3d_priv_jolt_body_get_velocity(b->id, l, a);
    } else if (b->moving_set) {
        memcpy(l, b->velocity, sizeof(l));
        memcpy(a, b->spin, sizeof(a));
    }
    return angular ? wgf_vec3_make(a[0], a[1], a[2]) : wgf_vec3_make(l[0], l[1], l[2]);
}

wgf_vec3_t wgf_body_get_velocity(wgf_actor_t actor)
{
    return moving_of(actor, false);
}

wgf_vec3_t wgf_body_get_spin(wgf_actor_t actor)
{
    return moving_of(actor, true);
}

bool wgf_body_add_impulse(wgf_actor_t actor, float x, float y, float z)
{
    body_t *b;
    if (!start() || (b = body_of(actor)) == NULL) return false;
    if (b->id != WGF_PHYSICS3D_PRIV_NO_BODY && !b->dirty) {
        const float impulse[3] = {x, y, z};
        wgf_physics3d_priv_jolt_body_add_impulse(b->id, impulse);
        return true;
    }
    b->impulse[0] += x;
    b->impulse[1] += y;
    b->impulse[2] += z;
    b->impulse_set = true;
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
    const body_t *b = body_of(actor);
    float l[3], a[3];
    wgf_vec3_t forward;
    if (vehicle_of(actor) == NULL || b == NULL || b->id == WGF_PHYSICS3D_PRIV_NO_BODY) return 0.0f;
    wgf_physics3d_priv_jolt_body_get_velocity(b->id, l, a);
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
    const body_t *b = body_of(actor);
    static const float none[3] = {0.0f, 0.0f, 0.0f};
    if (v == NULL) return false;
    v->throttle = v->brake = v->steer = 0.0f;
    v->hand_brake = false;
    if (v->index >= 0) wgf_physics3d_priv_jolt_vehicle_reset(v->index);
    if (b != NULL && b->id != WGF_PHYSICS3D_PRIV_NO_BODY) wgf_physics3d_priv_jolt_body_set_velocity(b->id, none, none);
    return true;
}

/* ---- the debug view ---------------------------------------------------------------------- */

static void line(wgf_mat4_t m, float ax, float ay, float az, float bx, float by, float bz, wgf_color_t color)
{
    const wgf_vec3_t a = wgf_mat4_transform_point(m, wgf_vec3_make(ax, ay, az));
    const wgf_vec3_t b = wgf_mat4_transform_point(m, wgf_vec3_make(bx, by, bz));
    wgf_draw_line_3d(a.x, a.y, a.z, b.x, b.y, b.z, color);
}

/* A circle of `radius` about the axis `axis` (0 x, 1 y, 2 z), centered `offset` along y. */
static void ring(wgf_mat4_t m, float radius, int axis, float offset, wgf_color_t color)
{
    int i;
    for (i = 0; i < 24; i++) {
        const float a0 = 6.2831853f * (float)i / 24.0f, a1 = 6.2831853f * (float)(i + 1) / 24.0f;
        const float c0 = radius * cosf(a0), s0 = radius * sinf(a0), c1 = radius * cosf(a1), s1 = radius * sinf(a1);
        if (axis == 0) line(m, 0, c0 + offset, s0, 0, c1 + offset, s1, color);
        else if (axis == 1) line(m, c0, offset, s0, c1, offset, s1, color);
        else line(m, c0, s0 + offset, 0, c1, s1 + offset, 0, color);
    }
}

void wgf_physics_draw_bodies(wgf_color_t color)
{
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    int i;
    if (!physics.started) return;
    wgf_ecs_priv_store_walk(physics.bodies);
    while (wgf_ecs_priv_store_next(physics.bodies, fields, &entity)) {
        const body_t *b = (const body_t *)fields[0];
        const wgf_actor_t actor = ((const wgf_ecs_priv_ref_t *)fields[1])->actor;
        float p[3], q[4];
        wgf_mat4_t m;
        const wgf_color_t c = b->type == WGF_BODY_TYPE_SENSOR ? (color & 0xFFFFFF00u) | ((color & 0xFFu) / 2u) : color;
        if (b->id == WGF_PHYSICS3D_PRIV_NO_BODY) continue;
        wgf_physics3d_priv_jolt_body_get_pose(b->id, p, q);
        (void)actor;
        m = wgf_mat4_from_trs(wgf_vec3_make(p[0], p[1], p[2]), wgf_quat_make(q[0], q[1], q[2], q[3]), wgf_vec3_make(1, 1, 1));
        if (b->shape == WGF_BODY_SHAPE_BOX) {
            const float x = b->size[0] * 0.5f, y = b->size[1] * 0.5f, z = b->size[2] * 0.5f;
            for (i = 0; i < 4; i++) {
                const float sx = i & 1 ? x : -x, sz = i & 2 ? z : -z;
                line(m, sx, -y, sz, sx, y, sz, c); /* the uprights, then the top and bottom */
                line(m, -x, i & 1 ? y : -y, i & 2 ? z : -z, x, i & 1 ? y : -y, i & 2 ? z : -z, c);
                line(m, i & 1 ? x : -x, i & 2 ? y : -y, -z, i & 1 ? x : -x, i & 2 ? y : -y, z, c);
            }
        } else if (b->shape == WGF_BODY_SHAPE_SPHERE) {
            for (i = 0; i < 3; i++) ring(m, b->size[0], i, 0.0f, c);
        } else if (b->shape == WGF_BODY_SHAPE_CAPSULE) {
            const float r = b->size[0], half = at_least(b->size[1] * 0.5f - r, 0.0f);
            ring(m, r, 1, half, c);
            ring(m, r, 1, -half, c);
            ring(m, r, 0, 0.0f, c); /* its outline across, ends and sides */
            ring(m, r, 2, 0.0f, c);
            line(m, r, -half, 0, r, half, 0, c);
            line(m, -r, -half, 0, -r, half, 0, c);
            line(m, 0, -half, r, 0, half, r, c);
            line(m, 0, -half, -r, 0, half, -r, c);
        } else {
            for (i = 0; i < b->triangles; i++) {
                const float *t = &b->edges[9 * i];
                line(m, t[0], t[1], t[2], t[3], t[4], t[5], c);
                line(m, t[3], t[4], t[5], t[6], t[7], t[8], c);
                line(m, t[6], t[7], t[8], t[0], t[1], t[2], c);
            }
        }
    }
    wgf_ecs_priv_store_walk(physics.vehicles);
    while (wgf_ecs_priv_store_next(physics.vehicles, fields, &entity)) {
        const vehicle_t *v = (const vehicle_t *)fields[0];
        for (i = 0; v->index >= 0 && i < v->wheel_count; i++) {
            const wgf_mat4_t wheel = wgf_gfx_priv_actor_get_simulated_world(v->wheels[i]);
            ring(wheel, v->radius, 0, 0.0f, color); /* a wheel turns about its x */
        }
    }
}
