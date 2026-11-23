#include "wgf_physics3d.h"
#include "wgf_body3d.h"

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
#include "wgf_physics3d_priv.h"
#include "wgf_quat.h"
#include "wgf_resource.h"
#include "wgf_math_draw_trig_priv.h"

/* physics3d (wgf_physics3d.h): the body and vehicle components, the ecs's through its hooks
 * for a part's components (wgf_ecs_priv.h), their data the store's; each tick, after the
 * ecs's systems, the world made to match them (a body or vehicle whose settings changed made
 * again, one the program moved put where it is), stepped, and the actors set where it put
 * them; then the sensors' overlaps raised as the ecs's triggers. A part (wgf_core_part_priv.h):
 * installed by the first call that starts physics, so a program that makes no body links
 * none of it, Jolt included. */

#define GRAVITY_DEFAULT -9.81f

typedef struct body_t {
    uint32_t id; /* Jolt's; WGF_PHYSICS3D_PRIV_NO_BODY until made */
    int type, shape;
    float size[3], offset[3], mass_offset[3], mass, friction, bounce, damping[2];
    int layer, mask;
    bool dirty; /* made again at the next tick */
    float velocity[3], spin[3], impulse[3];
    bool moving_set, impulse_set; /* a velocity or a push waiting for the body to be made */
    wgf_vec3_t placed;            /* its actor's position and rotation as physics last left them */
    wgf_quat_t placed_rotation;
    float *edges; /* a convex or mesh body's triangles, for the debug view: 9 floats each, malloc'd */
    int triangles;
    int character;         /* a character's: Jolt's index + 1; 0 none */
    float step, max_slope; /* a character's: the steps it climbs, the steepest ground it walks */
    bool grounded;         /* a character's, as its last tick left it */
    float ground[3];
} body_t;

static struct {
    bool started;
    float gravity[3];
    wgf_ecs_priv_id_t body_id;
    wgf_ecs_priv_query_t *bodies;
    float dt; /* the last tick's, for a kinematic body's moves */
    const wgf_physics3d_priv_characters_t *characters; /* NULL until linked (wgf_physics3d_enable_characters) */
} physics;

/* Vehicles' hooks, set as their file is linked (wgf_physics3d_enable_vehicles): kept apart from
 * `physics`, which a stop clears, since they outlive a world. */
static const wgf_physics3d_priv_vehicles_t *vehicles;

void wgf_physics3d_priv_set_vehicles(const wgf_physics3d_priv_vehicles_t *set)
{
    const bool fresh = vehicles == NULL;
    vehicles = set;
    if (fresh && set != NULL && physics.started) set->start(); /* linked after physics started */
}

bool wgf_physics3d_is_vehicles_enabled(void)
{
    return vehicles != NULL;
}

void wgf_physics3d_priv_set_characters(const wgf_physics3d_priv_characters_t *characters)
{
    physics.characters = characters;
}

bool wgf_physics3d_is_characters_enabled(void)
{
    return physics.characters != NULL;
}

static bool start(void);

/* ---- the components' data ------------------------------------------------------------- */

static body_t *body_of(wgf_actor_t actor)
{
    if (!physics.started) return NULL;
    return (body_t *)wgf_ecs_priv_get(actor, physics.body_id);
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
    bool pending; /* a model's mesh still loading (a glTF file): the body waits for it */
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

/* Primitive `primitive` of `mesh`, placed by `to_root`, added to `soup`. */
static void add_primitive(wgf_mesh_t mesh, int primitive, wgf_mat4_t to_root, soup_t *soup)
{
    const float *vertices = NULL;
    const uint32_t *indices = NULL;
    int vertex_count = 0, index_count = 0, i;
    uint32_t base;
    if (!wgf_gfx_priv_mesh_get_triangles(mesh, primitive, &vertices, &vertex_count, &indices, &index_count) ||
        vertex_count == 0 ||
        !grow((void **)&soup->points, &soup->point_capacity, 3 * (soup->point_count + vertex_count), sizeof(float)) ||
        !grow((void **)&soup->indices, &soup->index_capacity, soup->index_count + index_count, sizeof(uint32_t))) {
        return;
    }
    base = (uint32_t)soup->point_count;
    for (i = 0; i < vertex_count; i++) {
        const float *v = vertices + (size_t)i * WGF_GFX_PRIV_MESH_VERTEX_FLOATS;
        const wgf_vec3_t point = wgf_mat4_transform_point(to_root, wgf_vec3_make(v[0], v[1], v[2]));
        soup->points[3 * soup->point_count] = point.x;
        soup->points[3 * soup->point_count + 1] = point.y;
        soup->points[3 * soup->point_count + 2] = point.z;
        soup->point_count++;
    }
    for (i = 0; i < index_count; i++) soup->indices[soup->index_count++] = base + indices[i];
}

/* Every model at and under `actor`, its mesh's triangles in `root`'s space (a glTF file's
 * root none: its nodes, under it, carry the file's meshes); and whether a model's mesh is
 * still loading. */
static void gather(wgf_actor_t root, wgf_mat4_t root_inverse, wgf_actor_t actor, soup_t *soup, int depth)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    int count, i, n;
    if (depth > 32 || actor_ptr == NULL) return;
    if (actor_ptr->type == WGF_ACTOR_KIND_MODEL && actor_ptr->as.model.mesh != 0 &&
        wgf_resource_get_status(actor_ptr->as.model.mesh) == WGF_RESOURCE_STATUS_PENDING) {
        soup->pending = true;
    }
    count = actor_ptr->type == WGF_ACTOR_KIND_MODEL && !actor_ptr->as.model.file_root
                ? wgf_gfx_priv_mesh_get_primitive_count(actor_ptr->as.model.mesh)
                : 0;
    if (count > 0) {
        const wgf_mat4_t to_root = wgf_mat4_mul(root_inverse, wgf_gfx_priv_actor_get_simulated_world(actor));
        for (i = 0; i < count; i++) add_primitive(actor_ptr->as.model.mesh, i, to_root, soup);
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

static void unmake_body(wgf_actor_t actor, body_t *b)
{
    if (vehicles != NULL) vehicles->body_unmade(actor); /* its vehicle is made again on the new body */
    if (b->id != WGF_PHYSICS3D_PRIV_NO_BODY) wgf_physics3d_priv_jolt_body_destroy(b->id);
    b->id = WGF_PHYSICS3D_PRIV_NO_BODY;
    if (b->character > 0 && physics.characters != NULL) physics.characters->destroy(b->character - 1);
    b->character = 0;
    b->grounded = false;
    forget_edges(b);
}

static void make_body(wgf_actor_t actor, body_t *b)
{
    wgf_physics3d_priv_body_desc_t d;
    soup_t soup;
    memset(&d, 0, sizeof(d));
    memset(&soup, 0, sizeof(soup));
    unmake_body(actor, b);
    d.motion = b->type == WGF_BODY3D_TYPE_DYNAMIC                                             ? WGF_PHYSICS3D_PRIV_DYNAMIC
               : b->type == WGF_BODY3D_TYPE_KINEMATIC || b->type == WGF_BODY3D_TYPE_CHARACTER ? WGF_PHYSICS3D_PRIV_KINEMATIC
                                                    : WGF_PHYSICS3D_PRIV_STATIC;
    d.sensor = b->type == WGF_BODY3D_TYPE_SENSOR;
    d.shape = (wgf_physics3d_priv_shape_t)b->shape;
    switch (b->shape) {
        case WGF_BODY3D_SHAPE_BOX:
            d.size[0] = b->size[0] * 0.5f;
            d.size[1] = b->size[1] * 0.5f;
            d.size[2] = b->size[2] * 0.5f;
            break;
        case WGF_BODY3D_SHAPE_SPHERE: d.size[0] = b->size[0]; break;
        case WGF_BODY3D_SHAPE_CAPSULE:
            d.size[0] = b->size[0];
            d.size[1] = b->size[1] * 0.5f - b->size[0]; /* the cylinder between its ends */
            if (d.size[1] < 0.01f) d.size[1] = 0.01f;
            break;
        default: {
            /* the triangles in the body's space: its place and turn, which the body takes,
               but not its scale, which they keep (a kit's piece scaled up is its size) */
            const wgf_mat4_t world = wgf_gfx_priv_actor_get_simulated_world(actor);
            const wgf_mat4_t inverse = wgf_mat4_invert(wgf_mat4_from_trs(
                wgf_mat4_get_translation(world), wgf_mat4_get_rotation(world), wgf_vec3_make(1, 1, 1)));
            gather(actor, inverse, actor, &soup, 0);
            if (soup.pending) { /* made at a tick after its models' files have loaded */
                free(soup.points);
                free(soup.indices);
                b->dirty = true;
                return;
            }
            d.points = soup.points;
            d.point_count = soup.point_count;
            d.indices = soup.indices;
            d.index_count = soup.index_count;
            if (b->shape == WGF_BODY3D_SHAPE_MESH && (d.motion != WGF_PHYSICS3D_PRIV_STATIC || d.sensor)) {
                d.shape = WGF_PHYSICS3D_PRIV_CONVEX; /* a mesh moves as its hull */
            }
            break;
        }
    }
    world_pose(actor, d.position, d.rotation);
    memcpy(d.offset, b->offset, sizeof(d.offset));
    memcpy(d.mass_offset, b->mass_offset, sizeof(d.mass_offset));
    d.mass = b->mass;
    d.friction = b->friction;
    d.restitution = b->bounce;
    d.linear_damping = b->damping[0];
    d.angular_damping = b->damping[1];
    d.layer = (uint32_t)b->layer;
    d.mask = (uint32_t)b->mask;
    d.user = (uint64_t)actor;
    if (b->type == WGF_BODY3D_TYPE_CHARACTER) {
        b->character = physics.characters != NULL ? physics.characters->create(&d, b->max_slope) + 1 : 0;
        b->dirty = false;
        if (b->character == 0) wgf_log_warn("wgf_physics3d: a character couldn't be made (more than 256, or no shape)");
        free(soup.points);
        free(soup.indices);
        note_place(b, actor);
        return;
    }
    b->id = wgf_physics3d_priv_jolt_body_create(&d);
    b->dirty = false;
    if (b->id == WGF_PHYSICS3D_PRIV_NO_BODY) {
        wgf_log_warn("wgf_physics3d: a body couldn't be made (a convex or mesh shape with no model ready under it, or "
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

/* ---- the components, through the ecs's hooks ------------------------------------------- */

static void body_add(wgf_actor_t actor)
{
    body_t b;
    wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_make(actor);
    if (record == NULL) return;
    memset(&b, 0, sizeof(b));
    b.id = WGF_PHYSICS3D_PRIV_NO_BODY;
    b.type = WGF_BODY3D_TYPE_DYNAMIC;
    b.shape = WGF_BODY3D_SHAPE_BOX;
    b.size[0] = b.size[1] = b.size[2] = 1.0f;
    b.friction = 0.5f;
    b.damping[0] = b.damping[1] = 0.05f;
    b.layer = 1;
    b.mask = 0x7FFF;
    b.step = 0.3f;
    b.max_slope = 0.8726646f; /* 50 degrees, Jolt's */
    b.dirty = true;
    wgf_ecs_priv_store_set(record->id, physics.body_id, &b);
}

static void body_remove(wgf_actor_t actor)
{
    body_t *b = body_of(actor);
    if (b != NULL) unmake_body(actor, b);
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

static const char *const types[] = {"static", "dynamic", "kinematic", "sensor", "character"};
static const char *const shapes[] = {"box", "sphere", "capsule", "convex", "mesh"};
static int word_of(const char *text, const char *const *words, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        if (strcmp(text, words[i]) == 0) return i;
    }
    return -1;
}

/* What a scene's line said, as the keys it can have (the words' keys first). */
typedef wgf_physics3d_priv_line_key_t line_key_t; /* wgf_physics3d_priv.h's, the vehicles' lines' too */

static const line_key_t body_keys[] = {{"type", 0},     {"shape", 0},    {"size", 3},    {"radius", 1}, {"height", 1},
                                       {"mass", 1},     {"friction", 1}, {"bounce", 1},  {"damping", 2}, {"layer", 1},
                                       {"mask", 1},     {"velocity", 3}, {"spin", 3},
                                       {"offset", 3},   {"mass_offset", 3}};
/* `value`'s numbers as `key` takes them, into n: how many; -1, warned, for a key the line
 * hasn't or a value it can't take. */
static int read_value(const char *line, const line_key_t *keys, int key_count, const char *key, const char *value,
                      double *n)
{
    int i, count;
    for (i = 0; i < key_count && strcmp(keys[i].key, key) != 0; i++) {
    }
    if (i == key_count) {
        wgf_log_warn("wgf_physics3d: a %s line has no key \"%s\"", line, key);
        return -1;
    }
    if (keys[i].count == 0) return 0;
    count = numbers_of(value, n, 8);
    if (count < 1 || (keys[i].count > 0 && count != keys[i].count)) {
        wgf_log_warn("wgf_physics3d: a %s line's %s takes %d number(s) (\"%s\")", line, key, keys[i].count > 0 ? keys[i].count : 8,
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
    const int count = read_value("body3d", body_keys, (int)(sizeof(body_keys) / sizeof(body_keys[0])), key, text, n);
    const float x = count > 0 ? (float)n[0] : 0.0f;
    int w;
    if (b == NULL || count < 0) return;
    if (strcmp(key, "type") == 0) {
        if ((w = word_of(text, types, 5)) >= 0) wgf_body3d_set_type(actor, (wgf_body3d_type_t)w);
        else wgf_log_warn("wgf_physics3d: a body's type is static, dynamic, kinematic, sensor, or character (\"%s\")", text);
    } else if (strcmp(key, "shape") == 0) {
        if ((w = word_of(text, shapes, 5)) >= 0) {
            b->shape = w;
            b->dirty = true;
        } else {
            wgf_log_warn("wgf_physics3d: a body's shape is box, sphere, capsule, convex, or mesh (\"%s\")", text);
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
    } else if (strcmp(key, "mass") == 0) wgf_body3d_set_mass(actor, x);
    else if (strcmp(key, "friction") == 0) wgf_body3d_set_friction(actor, x);
    else if (strcmp(key, "bounce") == 0) wgf_body3d_set_bounce(actor, x);
    else if (strcmp(key, "damping") == 0 && count == 2) wgf_body3d_set_damping(actor, x, (float)n[1]);
    else if (strcmp(key, "layer") == 0) wgf_body3d_set_layer(actor, (int)x);
    else if (strcmp(key, "mask") == 0) wgf_body3d_set_mask(actor, (int)x);
    else if (strcmp(key, "velocity") == 0 && count == 3) wgf_body3d_set_velocity(actor, x, (float)n[1], (float)n[2]);
    else if (strcmp(key, "spin") == 0 && count == 3) wgf_body3d_set_spin(actor, x, (float)n[1], (float)n[2]);
    else if (strcmp(key, "offset") == 0) wgf_body3d_set_offset(actor, x, (float)n[1], (float)n[2]);
    else if (strcmp(key, "mass_offset") == 0) wgf_body3d_set_mass_offset(actor, x, (float)n[1], (float)n[2]);
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
    if (b->shape == WGF_BODY3D_SHAPE_BOX) put(out, size, " size=%.9g,%.9g,%.9g", b->size[0], b->size[1], b->size[2]);
    if (b->shape == WGF_BODY3D_SHAPE_SPHERE || b->shape == WGF_BODY3D_SHAPE_CAPSULE) put(out, size, " radius=%.9g", b->size[0]);
    if (b->shape == WGF_BODY3D_SHAPE_CAPSULE) put(out, size, " height=%.9g", b->size[1]);
    put(out, size, " mass=%.9g friction=%.9g bounce=%.9g damping=%.9g,%.9g layer=%d mask=%d", b->mass, b->friction,
        b->bounce, b->damping[0], b->damping[1], b->layer, b->mask);
    if (b->offset[0] != 0.0f || b->offset[1] != 0.0f || b->offset[2] != 0.0f) {
        put(out, size, " offset=%.9g,%.9g,%.9g", b->offset[0], b->offset[1], b->offset[2]);
    }
    if (b->mass_offset[0] != 0.0f || b->mass_offset[1] != 0.0f || b->mass_offset[2] != 0.0f) {
        put(out, size, " mass_offset=%.9g,%.9g,%.9g", b->mass_offset[0], b->mass_offset[1], b->mass_offset[2]);
    }
}

/* the components' hooks, their store ids filled as physics starts */
static wgf_ecs_priv_part_component_t body_hooks = {0, body_add, body_remove, body_set, body_describe};
/* ---- the tick ---------------------------------------------------------------------------- */

static int32_t layer_of(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->layer : 0;
}

/* Whether `a` is `b` or above it. */
static bool at_or_above(wgf_actor_t a, wgf_actor_t b)
{
    int depth;
    for (depth = 0; b != 0 && depth < 64; depth++) {
        if (a == b) return true;
        b = wgf_actor_get_parent(b);
    }
    return false;
}

/* A glTF file's tree made, or made again (the file loaded again: wgf_asset_reload): each
 * convex or mesh body over it -- on its root, above it, or under it -- made again at the next
 * tick, from the triangles the tree has now. */
static void tree_built(wgf_actor_t root)
{
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    if (!physics.started) return;
    wgf_ecs_priv_store_walk(physics.bodies);
    while (wgf_ecs_priv_store_next(physics.bodies, fields, &entity)) {
        body_t *b = (body_t *)fields[0];
        const wgf_actor_t actor = ((const wgf_ecs_priv_ref_t *)fields[1])->actor;
        if ((b->shape == WGF_BODY3D_SHAPE_CONVEX || b->shape == WGF_BODY3D_SHAPE_MESH) &&
            (at_or_above(actor, root) || at_or_above(root, actor))) {
            b->dirty = true;
        }
    }
}

/* A character's step, before the world's: put where its actor was moved to, if it was;
 * moved by its velocity (falling while unsupported, the up part taken from where it went:
 * stopped on a ceiling, 0 on the ground); its actor set where it went, its turn kept. */
static void step_character(wgf_actor_t actor, body_t *b, float dt)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    float p[3], q[4], v[3], out[3];
    const int c = b->character - 1;
    if (actor_ptr == NULL || c < 0 || physics.characters == NULL) return;
    if (!same_place(b, actor_ptr)) {
        world_pose(actor, p, q);
        physics.characters->set_pose(c, p, q);
    }
    memcpy(v, b->velocity, sizeof(v));
    if (b->grounded && v[1] < 0.0f) v[1] = 0.0f;
    if (!b->grounded) v[1] += physics.gravity[1] * dt;
    physics.characters->update(c, v, b->step, dt, p, out);
    b->velocity[1] = out[1];
    b->grounded = physics.characters->ground(c, b->ground);
    if (b->grounded && b->velocity[1] < 0.0f) b->velocity[1] = 0.0f;
    world_pose(actor, v, q);
    put_actor(actor, p, q);
    note_place(b, actor);
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
        if (b->type == WGF_BODY3D_TYPE_CHARACTER && !b->dirty) {
            step_character(actor, b, dt);
            continue;
        }
        if (b->dirty || b->id == WGF_PHYSICS3D_PRIV_NO_BODY) {
            if (b->dirty) make_body(actor, b);
            continue;
        }
        if (b->type == WGF_BODY3D_TYPE_KINEMATIC || b->type == WGF_BODY3D_TYPE_SENSOR) {
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
    if (vehicles != NULL) vehicles->before_step(); /* each made where it changed, and its driver's input given */

    wgf_physics3d_priv_jolt_step(dt);

    /* the actors set where the world put them */
    wgf_ecs_priv_store_walk(physics.bodies);
    while (wgf_ecs_priv_store_next(physics.bodies, fields, &entity)) {
        body_t *b = (body_t *)fields[0];
        const wgf_actor_t actor = ((const wgf_ecs_priv_ref_t *)fields[1])->actor;
        float p[3], q[4];
        if (b->type != WGF_BODY3D_TYPE_DYNAMIC || b->id == WGF_PHYSICS3D_PRIV_NO_BODY) continue;
        wgf_physics3d_priv_jolt_body_get_pose(b->id, p, q);
        put_actor(actor, p, q);
        note_place(b, actor);
    }
    if (vehicles != NULL) vehicles->after_step(); /* their wheels set where the world turned them */

    /* the sensors' overlaps, told to both as the ecs's triggers */
    while ((n = wgf_physics3d_priv_jolt_take_overlaps(overlaps, 64)) > 0) {
        for (i = 0; i < n; i++) {
            const wgf_actor_t sensor = (wgf_actor_t)overlaps[i].sensor, other = (wgf_actor_t)overlaps[i].other;
            const wgf_world_event_t event = overlaps[i].contact
                                                ? (overlaps[i].entered ? WGF_WORLD_EVENT_CONTACT_BEGIN : WGF_WORLD_EVENT_CONTACT_END)
                                                : (overlaps[i].entered ? WGF_WORLD_EVENT_TRIGGER_ENTER : WGF_WORLD_EVENT_TRIGGER_EXIT);
            wgf_ecs_priv_raise(event, (int)sensor, (int)other, layer_of(other));
            wgf_ecs_priv_raise(event, (int)other, (int)sensor, layer_of(sensor));
        }
    }
}

static void stop(void)
{
    if (!physics.started) return;
    wgf_ecs_priv_set_part_component(WGF_COMPONENT_BODY3D, NULL);
    wgf_ecs_priv_store_query_free(physics.bodies);
    if (vehicles != NULL) vehicles->stop();
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
        wgf_log_error("wgf_physics3d: couldn't start");
        return false;
    }
    physics.started = true;
    physics.gravity[1] = GRAVITY_DEFAULT;
    physics.body_id = wgf_ecs_priv_store_component(sizeof(body_t), _Alignof(body_t));
    terms[0] = physics.body_id;
    terms[1] = wgf_ecs_priv_ids()->ref;
    physics.bodies = wgf_ecs_priv_store_query(terms, 2);
    body_hooks.id = physics.body_id;
    wgf_ecs_priv_set_part_component(WGF_COMPONENT_BODY3D, &body_hooks);
    if (vehicles != NULL) vehicles->start(); /* its component, if vehicles are linked */
    wgf_core_priv_part_install(&part);
    wgf_gfx_priv_add_model_built_hook(tree_built); /* room for it: the ecs's and physics' are all */
    return true;
}

/* ---- the calls ---------------------------------------------------------------------------- */

bool wgf_physics3d_set_gravity(float x, float y, float z)
{
    if (!start()) return false;
    physics.gravity[0] = x;
    physics.gravity[1] = y;
    physics.gravity[2] = z;
    wgf_physics3d_priv_jolt_set_gravity(x, y, z);
    return true;
}

wgf_vec3_t wgf_physics3d_get_gravity(void)
{
    return physics.started ? wgf_vec3_make(physics.gravity[0], physics.gravity[1], physics.gravity[2])
                           : wgf_vec3_make(0.0f, GRAVITY_DEFAULT, 0.0f);
}

bool wgf_body3d_set_type(wgf_actor_t actor, wgf_body3d_type_t type)
{
    body_t *b;
    if ((int)type < WGF_BODY3D_TYPE_STATIC || type > WGF_BODY3D_TYPE_CHARACTER) return false;
    if (type == WGF_BODY3D_TYPE_CHARACTER && physics.characters == NULL) {
        wgf_log_error("wgf_body3d_set_type: characters aren't linked in this program (a game: \"characters\" in wgf.json's "
                      "features; C: wgf_physics3d_enable_characters)");
        return false;
    }
    if ((b = changing(actor)) == NULL) return false;
    b->type = type;
    return true;
}

wgf_body3d_type_t wgf_body3d_get_type(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? (wgf_body3d_type_t)b->type : WGF_BODY3D_TYPE_STATIC;
}

static float at_least(float value, float least)
{
    return value > least ? value : least;
}

bool wgf_body3d_set_shape(wgf_actor_t actor, wgf_body3d_shape_t shape, float x, float y, float z)
{
    body_t *b;
    if ((int)shape < WGF_BODY3D_SHAPE_BOX || shape > WGF_BODY3D_SHAPE_MESH) return false;
    if ((b = changing(actor)) == NULL) return false;
    b->shape = shape;
    b->size[0] = at_least(x, 0.01f);
    b->size[1] = at_least(y, 0.01f);
    b->size[2] = at_least(z, 0.01f);
    return true;
}

wgf_body3d_shape_t wgf_body3d_get_shape(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? (wgf_body3d_shape_t)b->shape : WGF_BODY3D_SHAPE_BOX;
}

wgf_vec3_t wgf_body3d_get_size(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? wgf_vec3_make(b->size[0], b->size[1], b->size[2]) : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_body3d_set_mass(wgf_actor_t actor, float kilograms)
{
    body_t *b = changing(actor);
    if (b == NULL) return false;
    b->mass = at_least(kilograms, 0.0f);
    return true;
}

float wgf_body3d_get_mass(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->mass : 0.0f;
}

/* A setting a made body takes in place, its motion kept; one not yet made takes it when it is. */
static body_t *tuning(wgf_actor_t actor)
{
    if (!start()) return NULL;
    return body_of(actor);
}

static void retune(body_t *b)
{
    if (b->id != WGF_PHYSICS3D_PRIV_NO_BODY && !b->dirty) {
        wgf_physics3d_priv_jolt_body_set_material(b->id, b->friction, b->bounce, b->damping[0], b->damping[1]);
    }
}

bool wgf_body3d_set_friction(wgf_actor_t actor, float friction)
{
    body_t *b = tuning(actor);
    if (b == NULL) return false;
    b->friction = at_least(friction, 0.0f);
    retune(b);
    return true;
}

float wgf_body3d_get_friction(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->friction : 0.0f;
}

bool wgf_body3d_set_bounce(wgf_actor_t actor, float bounce)
{
    body_t *b = tuning(actor);
    if (b == NULL) return false;
    b->bounce = bounce < 0.0f ? 0.0f : (bounce > 1.0f ? 1.0f : bounce);
    retune(b);
    return true;
}

float wgf_body3d_get_bounce(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->bounce : 0.0f;
}

static float unit(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

bool wgf_body3d_set_damping(wgf_actor_t actor, float linear, float angular)
{
    body_t *b = tuning(actor);
    if (b == NULL) return false;
    b->damping[0] = unit(linear);
    b->damping[1] = unit(angular);
    retune(b);
    return true;
}

bool wgf_body3d_set_offset(wgf_actor_t actor, float x, float y, float z)
{
    body_t *b = changing(actor);
    if (b == NULL) return false;
    b->offset[0] = x;
    b->offset[1] = y;
    b->offset[2] = z;
    return true;
}

wgf_vec3_t wgf_body3d_get_offset(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? wgf_vec3_make(b->offset[0], b->offset[1], b->offset[2]) : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

bool wgf_body3d_set_mass_offset(wgf_actor_t actor, float x, float y, float z)
{
    body_t *b = changing(actor);
    if (b == NULL) return false;
    b->mass_offset[0] = x;
    b->mass_offset[1] = y;
    b->mass_offset[2] = z;
    return true;
}

wgf_vec3_t wgf_body3d_get_mass_offset(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? wgf_vec3_make(b->mass_offset[0], b->mass_offset[1], b->mass_offset[2])
                     : wgf_vec3_make(0.0f, 0.0f, 0.0f);
}

wgf_vec2_t wgf_body3d_get_damping(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? wgf_vec2_make(b->damping[0], b->damping[1]) : wgf_vec2_make(0.0f, 0.0f);
}

bool wgf_body3d_set_layer(wgf_actor_t actor, int layer)
{
    body_t *b;
    if (layer < 1 || layer > 0x7FFF) return false;
    if ((b = changing(actor)) == NULL) return false;
    b->layer = layer;
    return true;
}

int wgf_body3d_get_layer(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->layer : 0;
}

bool wgf_body3d_set_mask(wgf_actor_t actor, int mask)
{
    body_t *b;
    if (mask < 0 || mask > 0x7FFF) return false;
    if ((b = changing(actor)) == NULL) return false;
    b->mask = mask;
    return true;
}

int wgf_body3d_get_mask(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->mask : 0;
}

/* A velocity or spin: the world's body's when it is made, else kept for when it is. */
static bool set_moving(wgf_actor_t actor, const float *linear, const float *angular)
{
    body_t *b;
    if (!start() || (b = body_of(actor)) == NULL) return false;
    if (b->type == WGF_BODY3D_TYPE_CHARACTER) { /* its own, which its steps move it by */
        if (linear != NULL) memcpy(b->velocity, linear, sizeof(b->velocity));
        return true;
    }
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

bool wgf_body3d_set_velocity(wgf_actor_t actor, float x, float y, float z)
{
    const float v[3] = {x, y, z};
    return set_moving(actor, v, NULL);
}

bool wgf_body3d_set_spin(wgf_actor_t actor, float x, float y, float z)
{
    const float v[3] = {x, y, z};
    return set_moving(actor, NULL, v);
}

static wgf_vec3_t moving_of(wgf_actor_t actor, bool angular)
{
    const body_t *b = body_of(actor);
    float l[3] = {0, 0, 0}, a[3] = {0, 0, 0};
    if (b == NULL) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    if (b->type == WGF_BODY3D_TYPE_CHARACTER) {
        return angular ? wgf_vec3_make(0.0f, 0.0f, 0.0f) : wgf_vec3_make(b->velocity[0], b->velocity[1], b->velocity[2]);
    }
    if (b->id != WGF_PHYSICS3D_PRIV_NO_BODY) {
        wgf_physics3d_priv_jolt_body_get_velocity(b->id, l, a);
    } else if (b->moving_set) {
        memcpy(l, b->velocity, sizeof(l));
        memcpy(a, b->spin, sizeof(a));
    }
    return angular ? wgf_vec3_make(a[0], a[1], a[2]) : wgf_vec3_make(l[0], l[1], l[2]);
}

wgf_vec3_t wgf_body3d_get_velocity(wgf_actor_t actor)
{
    return moving_of(actor, false);
}

wgf_vec3_t wgf_body3d_get_spin(wgf_actor_t actor)
{
    return moving_of(actor, true);
}

bool wgf_body3d_add_impulse(wgf_actor_t actor, float x, float y, float z)
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

/* ---- what the vehicles take (wgf_physics3d_priv.h) ------------------------------------------ */

bool wgf_physics3d_priv_start(void)
{
    return start();
}

bool wgf_physics3d_priv_is_started(void)
{
    return physics.started;
}

bool wgf_physics3d_priv_body_info(wgf_actor_t actor, uint32_t *id, bool *dirty, int *type)
{
    const body_t *b = body_of(actor);
    if (b == NULL) return false;
    *id = b->id;
    *dirty = b->dirty;
    *type = b->type;
    return true;
}

int wgf_physics3d_priv_read_value(const char *line, const wgf_physics3d_priv_line_key_t *keys, int key_count,
                                  const char *key, const char *value, double *n)
{
    return read_value(line, keys, key_count, key, value, n);
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
        const float c0 = radius * draw_cos(a0), s0 = radius * draw_sin(a0), c1 = radius * draw_cos(a1), s1 = radius * draw_sin(a1);
        if (axis == 0) line(m, 0, c0 + offset, s0, 0, c1 + offset, s1, color);
        else if (axis == 1) line(m, c0, offset, s0, c1, offset, s1, color);
        else line(m, c0, s0 + offset, 0, c1, s1 + offset, 0, color);
    }
}

void wgf_physics3d_draw_bodies(wgf_color_t color)
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
        const wgf_color_t c = b->type == WGF_BODY3D_TYPE_SENSOR ? (color & 0xFFFFFF00u) | ((color & 0xFFu) / 2u) : color;
        if (b->id == WGF_PHYSICS3D_PRIV_NO_BODY) continue;
        wgf_physics3d_priv_jolt_body_get_pose(b->id, p, q);
        (void)actor;
        m = wgf_mat4_mul(wgf_mat4_from_trs(wgf_vec3_make(p[0], p[1], p[2]), wgf_quat_make(q[0], q[1], q[2], q[3]),
                                           wgf_vec3_make(1, 1, 1)),
                         wgf_mat4_from_trs(wgf_vec3_make(b->offset[0], b->offset[1], b->offset[2]), wgf_quat_identity(),
                                           wgf_vec3_make(1, 1, 1))); /* its shape where the offset puts it */
        if (b->shape == WGF_BODY3D_SHAPE_BOX) {
            const float x = b->size[0] * 0.5f, y = b->size[1] * 0.5f, z = b->size[2] * 0.5f;
            for (i = 0; i < 4; i++) {
                const float sx = i & 1 ? x : -x, sz = i & 2 ? z : -z;
                line(m, sx, -y, sz, sx, y, sz, c); /* the uprights, then the top and bottom */
                line(m, -x, i & 1 ? y : -y, i & 2 ? z : -z, x, i & 1 ? y : -y, i & 2 ? z : -z, c);
                line(m, i & 1 ? x : -x, i & 2 ? y : -y, -z, i & 1 ? x : -x, i & 2 ? y : -y, z, c);
            }
        } else if (b->shape == WGF_BODY3D_SHAPE_SPHERE) {
            for (i = 0; i < 3; i++) ring(m, b->size[0], i, 0.0f, c);
        } else if (b->shape == WGF_BODY3D_SHAPE_CAPSULE) {
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
    if (vehicles != NULL) vehicles->draw(color, ring); /* each wheel a ring about its axle */
}

/* ------------------------------------------------------------------ queries ---- */

#define QUERY_HITS 256

/* Hits into the caller's arrays: each hit's actor, and its point, normal, and distance (7
 * floats). How many. */
static int put_hits(const wgf_physics3d_priv_hit_t *hits, int n, wgf_actor_t *actors, int count, float *out,
                    int out_count)
{
    int i;
    if (n > count) n = count;
    for (i = 0; i < n; i++) {
        actors[i] = (wgf_actor_t)hits[i].user;
        if (out != NULL && (i + 1) * 7 <= out_count) {
            float *h = &out[i * 7];
            h[0] = hits[i].point[0], h[1] = hits[i].point[1], h[2] = hits[i].point[2];
            h[3] = hits[i].normal[0], h[4] = hits[i].normal[1], h[5] = hits[i].normal[2];
            h[6] = hits[i].distance;
        }
    }
    return n;
}

int wgf_physics3d_cast_ray(float x, float y, float z, float dx, float dy, float dz, float distance, int mask,
                         wgf_actor_t *actors, int count, float *hits, int hits_count)
{
    static wgf_physics3d_priv_hit_t found[QUERY_HITS];
    const float origin[3] = {x, y, z}, direction[3] = {dx, dy, dz};
    int n;
    if (actors == NULL || count <= 0 || !start()) return 0;
    n = wgf_physics3d_priv_jolt_cast_ray(origin, direction, distance, (uint32_t)mask, found,
                                         count < QUERY_HITS ? count : QUERY_HITS);
    return put_hits(found, n, actors, count, hits, hits_count);
}

int wgf_physics3d_cast_sphere(float x, float y, float z, float radius, float dx, float dy, float dz, float distance,
                            int mask, wgf_actor_t *actors, int count, float *hits, int hits_count)
{
    static wgf_physics3d_priv_hit_t found[QUERY_HITS];
    const float origin[3] = {x, y, z}, direction[3] = {dx, dy, dz}, size[3] = {radius, radius, radius};
    int n;
    if (actors == NULL || count <= 0 || !(radius > 0.0f) || !start()) return 0;
    n = wgf_physics3d_priv_jolt_cast_shape(WGF_PHYSICS3D_PRIV_SPHERE, size, origin, direction, distance, (uint32_t)mask,
                                           found, count < QUERY_HITS ? count : QUERY_HITS);
    return put_hits(found, n, actors, count, hits, hits_count);
}

static int put_actors(const uint64_t *found, int n, wgf_actor_t *actors)
{
    int i;
    for (i = 0; i < n; i++) actors[i] = (wgf_actor_t)found[i];
    return n;
}

int wgf_physics3d_overlap_sphere(float x, float y, float z, float radius, int mask, wgf_actor_t *actors, int count)
{
    static uint64_t found[QUERY_HITS];
    const float center[3] = {x, y, z}, size[3] = {radius, radius, radius};
    if (actors == NULL || count <= 0 || !(radius > 0.0f) || !start()) return 0;
    return put_actors(found,
                      wgf_physics3d_priv_jolt_overlap(WGF_PHYSICS3D_PRIV_SPHERE, size, center, (uint32_t)mask, found,
                                                      count < QUERY_HITS ? count : QUERY_HITS),
                      actors);
}

int wgf_physics3d_overlap_box(float x, float y, float z, float width, float height, float depth, int mask,
                            wgf_actor_t *actors, int count)
{
    static uint64_t found[QUERY_HITS];
    const float center[3] = {x, y, z}, size[3] = {width * 0.5f, height * 0.5f, depth * 0.5f};
    if (actors == NULL || count <= 0 || !(width > 0.0f && height > 0.0f && depth > 0.0f) || !start()) return 0;
    return put_actors(found,
                      wgf_physics3d_priv_jolt_overlap(WGF_PHYSICS3D_PRIV_BOX, size, center, (uint32_t)mask, found,
                                                      count < QUERY_HITS ? count : QUERY_HITS),
                      actors);
}

/* ---------------------------------------------------------------- characters ---- */

bool wgf_body3d_is_grounded(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL && b->type == WGF_BODY3D_TYPE_CHARACTER && b->grounded;
}

wgf_vec3_t wgf_body3d_get_ground_normal(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    if (b == NULL || b->type != WGF_BODY3D_TYPE_CHARACTER || !b->grounded) return wgf_vec3_make(0.0f, 0.0f, 0.0f);
    return wgf_vec3_make(b->ground[0], b->ground[1], b->ground[2]);
}

bool wgf_body3d_set_step_height(wgf_actor_t actor, float height)
{
    body_t *b;
    if (!isfinite(height) || height < 0.0f || !start() || (b = body_of(actor)) == NULL) return false;
    b->step = height;
    return true;
}

float wgf_body3d_get_step_height(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->step : 0.0f;
}

bool wgf_body3d_set_max_slope(wgf_actor_t actor, float radians)
{
    body_t *b;
    if (!isfinite(radians) || radians < 0.0f || radians > 1.5707964f) return false;
    if ((b = changing(actor)) == NULL) return false;
    b->max_slope = radians;
    return true;
}

float wgf_body3d_get_max_slope(wgf_actor_t actor)
{
    const body_t *b = body_of(actor);
    return b != NULL ? b->max_slope : 0.0f;
}
