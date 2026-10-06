#include "wgf_ecs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node/wgf_gfx_node_priv.h"
#include "wgf_bounds.h"
#include "wgf_collider.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_ecs_priv.h"
#include "wgf_ecs_scene_priv.h"
#include "wgf_log.h"
#include "wgf_node.h"
#include "wgf_probe.h"
#include "wgf_voice.h"

/* The ecs (wgf_ecs.h): a flecs world made with the first entity, the records of libwgf's
 * entity handles, the systems run each tick, the frame's interpolation, and the events.
 * A part (wgf_core_part_priv.h): installed by the first entity, so a program that makes
 * none links none of flecs. */

#define EVENTS_MAX 65536

typedef struct pair_t {
    wgf_entity_t a, b; /* a < b */
} pair_t;

static struct {
    ecs_world_t *world;
    wgf_ecs_priv_ids_t ids;
    ecs_query_t *q_transform, *q_motion, *q_bounds, *q_lifetime, *q_collider;
    wgf_core_priv_handle_pool_t pool;
    wgf_ecs_priv_entity_t *records;
    uint64_t next_order;
    int live;
    int *events; /* EVENTS_MAX triples, a ring */
    int event_head, event_count;
    bool events_dropped;
    pair_t *pairs; /* the colliders overlapping as of the last tick, sorted */
    int pair_count, pair_capacity;
    char (*probe_names)[WGF_ECS_PRIV_NAME_MAX]; /* every behavior name a probe was set for */
    int probe_name_count, probe_name_capacity;
} ecs;

ecs_world_t *wgf_ecs_priv_world(void)
{
    return ecs.world;
}

const wgf_ecs_priv_ids_t *wgf_ecs_priv_ids(void)
{
    return &ecs.ids;
}

wgf_ecs_priv_entity_t *wgf_ecs_priv_entity_of(wgf_entity_t entity)
{
    uint16_t index;
    if (ecs.world == NULL || !wgf_core_priv_handle_pool_resolve(&ecs.pool, entity, &index)) return NULL;
    return &ecs.records[index];
}

void *wgf_ecs_priv_get(wgf_entity_t entity, ecs_entity_t component)
{
    const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    if (record == NULL || !ecs_has_id(ecs.world, record->id, component)) return NULL;
    return ecs_get_mut_id(ecs.world, record->id, component);
}

static int by_order(const void *a, const void *b)
{
    const wgf_ecs_priv_entity_t *x = wgf_ecs_priv_entity_of(*(const wgf_entity_t *)a);
    const wgf_ecs_priv_entity_t *y = wgf_ecs_priv_entity_of(*(const wgf_entity_t *)b);
    return x->order < y->order ? -1 : (x->order > y->order ? 1 : 0);
}

wgf_entity_t *wgf_ecs_priv_entities(int *count)
{
    wgf_entity_t *out;
    uint16_t i;
    int n = 0;
    *count = 0;
    if (ecs.world == NULL || ecs.live == 0) return NULL;
    out = (wgf_entity_t *)malloc(sizeof(wgf_entity_t) * (size_t)ecs.live);
    if (out == NULL) return NULL;
    for (i = 1; i < ecs.pool.capacity && n < ecs.live; i++) {
        const wgf_entity_t handle = wgf_core_priv_handle_pool_handle_from_index(&ecs.pool, i);
        if (handle != 0) out[n++] = handle;
    }
    qsort(out, (size_t)n, sizeof(wgf_entity_t), by_order);
    *count = n;
    return out;
}

/* ---- events -------------------------------------------------------------------- */

void wgf_ecs_priv_raise(wgf_ecs_event_t event, wgf_entity_t entity, wgf_entity_t other)
{
    int at;
    if (ecs.events == NULL) return;
    if (ecs.event_count == EVENTS_MAX) { /* the oldest dropped */
        if (!ecs.events_dropped) wgf_log_warn("wgf_ecs: more than %d events waiting; the oldest are dropped", EVENTS_MAX);
        ecs.events_dropped = true;
        ecs.event_head = (ecs.event_head + 1) % EVENTS_MAX;
        ecs.event_count--;
    }
    at = (ecs.event_head + ecs.event_count) % EVENTS_MAX;
    ecs.events[3 * at] = (int)event;
    ecs.events[3 * at + 1] = (int)entity;
    ecs.events[3 * at + 2] = (int)other;
    ecs.event_count++;
}

int wgf_ecs_get_event_count(void)
{
    return ecs.event_count;
}

int wgf_ecs_take_events(int *out, int count)
{
    int taken = 0;
    if (out == NULL || count < 3) return 0;
    while (ecs.event_count > 0 && taken + 3 <= count) {
        memcpy(out + taken, &ecs.events[3 * ecs.event_head], sizeof(int) * 3);
        ecs.event_head = (ecs.event_head + 1) % EVENTS_MAX;
        ecs.event_count--;
        taken += 3;
    }
    return taken;
}

/* ---- collider pairs -------------------------------------------------------------- */

void wgf_ecs_priv_forget_pairs(wgf_entity_t entity)
{
    int i, kept = 0;
    for (i = 0; i < ecs.pair_count; i++) {
        if (ecs.pairs[i].a != entity && ecs.pairs[i].b != entity) ecs.pairs[kept++] = ecs.pairs[i];
    }
    ecs.pair_count = kept;
}

int wgf_collider_get_overlaps(wgf_entity_t entity, wgf_entity_t *out, int count)
{
    int i, n = 0;
    if (out == NULL || wgf_ecs_priv_get(entity, ecs.ids.collider) == NULL) return 0;
    for (i = 0; i < ecs.pair_count && n < count; i++) {
        if (ecs.pairs[i].a == entity) out[n++] = ecs.pairs[i].b;
        else if (ecs.pairs[i].b == entity) out[n++] = ecs.pairs[i].a;
    }
    return n;
}

static int by_pair(const void *a, const void *b)
{
    const pair_t *x = (const pair_t *)a, *y = (const pair_t *)b;
    if (x->a != y->a) return x->a < y->a ? -1 : 1;
    return x->b < y->b ? -1 : (x->b > y->b ? 1 : 0);
}

typedef struct body_t {
    wgf_entity_t handle;
    wgf_node_t parent;
    float x, y, r;
    int32_t layer, mask;
} body_t;

static int by_sweep(const void *a, const void *b)
{
    const body_t *x = (const body_t *)a, *y = (const body_t *)b;
    if (x->parent != y->parent) return x->parent < y->parent ? -1 : 1;
    return x->x - x->r < y->x - y->r ? -1 : (x->x - x->r > y->x - y->r ? 1 : 0);
}

static bool add_pair(pair_t **pairs, int *count, int *capacity, wgf_entity_t a, wgf_entity_t b)
{
    if (*count == *capacity) {
        const int grown_capacity = *capacity > 0 ? *capacity * 2 : 64;
        pair_t *grown = (pair_t *)realloc(*pairs, sizeof(pair_t) * (size_t)grown_capacity);
        if (grown == NULL) return false;
        *pairs = grown;
        *capacity = grown_capacity;
    }
    (*pairs)[*count].a = a < b ? a : b;
    (*pairs)[*count].b = a < b ? b : a;
    (*count)++;
    return true;
}

/* Every collider's circle, sorted by parent and left edge, swept for overlaps: the new
 * pairs against the last tick's, raising enter and exit, each told to both. */
static void collide(void)
{
    body_t *bodies = NULL;
    pair_t *found = NULL;
    int body_count = 0, body_capacity = 0, found_count = 0, found_capacity = 0, i, j;
    ecs_iter_t it = ecs_query_iter(ecs.world, ecs.q_collider);
    while (ecs_query_next(&it)) {
        const wgf_ecs_priv_transform_t *t = ecs_field(&it, wgf_ecs_priv_transform_t, 0);
        const wgf_ecs_priv_collider_t *c = ecs_field(&it, wgf_ecs_priv_collider_t, 1);
        const wgf_ecs_priv_ref_t *ref = ecs_field(&it, wgf_ecs_priv_ref_t, 2);
        for (i = 0; i < it.count; i++) {
            const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(ref[i].handle);
            const float sx = fabsf(t[i].scale[0]), sy = fabsf(t[i].scale[1]);
            if (record == NULL) continue;
            if (body_count == body_capacity) {
                const int capacity = body_capacity > 0 ? body_capacity * 2 : 64;
                body_t *grown = (body_t *)realloc(bodies, sizeof(body_t) * (size_t)capacity);
                if (grown == NULL) continue;
                bodies = grown;
                body_capacity = capacity;
            }
            bodies[body_count].handle = ref[i].handle;
            bodies[body_count].parent = wgf_node_get_parent(record->node);
            bodies[body_count].x = t[i].position[0];
            bodies[body_count].y = t[i].position[1];
            bodies[body_count].r = c[i].radius * (sx > sy ? sx : sy);
            bodies[body_count].layer = c[i].layer;
            bodies[body_count].mask = c[i].mask;
            body_count++;
        }
    }
    if (body_count > 1) qsort(bodies, (size_t)body_count, sizeof(body_t), by_sweep);
    for (i = 0; i < body_count; i++) {
        const body_t *a = &bodies[i];
        for (j = i + 1; j < body_count && bodies[j].parent == a->parent && bodies[j].x - bodies[j].r <= a->x + a->r;
             j++) {
            const body_t *b = &bodies[j];
            const float dx = a->x - b->x, dy = a->y - b->y, reach = a->r + b->r;
            if (((a->layer & b->mask) == 0 && (b->layer & a->mask) == 0) || dx * dx + dy * dy > reach * reach) continue;
            if (!add_pair(&found, &found_count, &found_capacity, a->handle, b->handle)) {
                wgf_log_error("wgf_ecs: out of memory finding overlaps");
                break;
            }
        }
    }
    free(bodies);
    if (found_count > 1) qsort(found, (size_t)found_count, sizeof(pair_t), by_pair);
    /* the two sorted lists walked together: in the new alone, entered; in the old alone, exited */
    i = j = 0;
    while (i < found_count || j < ecs.pair_count) {
        const int order = i == found_count ? 1 : (j == ecs.pair_count ? -1 : by_pair(&found[i], &ecs.pairs[j]));
        if (order < 0) {
            wgf_ecs_priv_raise(WGF_ECS_EVENT_TRIGGER_ENTER, found[i].a, found[i].b);
            wgf_ecs_priv_raise(WGF_ECS_EVENT_TRIGGER_ENTER, found[i].b, found[i].a);
            i++;
        } else if (order > 0) {
            wgf_ecs_priv_raise(WGF_ECS_EVENT_TRIGGER_EXIT, ecs.pairs[j].a, ecs.pairs[j].b);
            wgf_ecs_priv_raise(WGF_ECS_EVENT_TRIGGER_EXIT, ecs.pairs[j].b, ecs.pairs[j].a);
            j++;
        } else {
            i++;
            j++;
        }
    }
    free(ecs.pairs);
    ecs.pairs = found;
    ecs.pair_count = found_count;
    ecs.pair_capacity = found_capacity;
}

/* ---- the systems -------------------------------------------------------------- */

/* Destroy every entity in `doomed`, after the query that found them has finished. */
static void destroy_all(wgf_entity_t *doomed, int count)
{
    int i;
    for (i = 0; i < count; i++) wgf_entity_destroy(doomed[i]);
    free(doomed);
}

static bool doom(wgf_entity_t **doomed, int *count, int *capacity, wgf_entity_t entity)
{
    if (*count == *capacity) {
        const int grown_capacity = *capacity > 0 ? *capacity * 2 : 16;
        wgf_entity_t *grown = (wgf_entity_t *)realloc(*doomed, sizeof(wgf_entity_t) * (size_t)grown_capacity);
        if (grown == NULL) return false;
        *doomed = grown;
        *capacity = grown_capacity;
    }
    (*doomed)[(*count)++] = entity;
    return true;
}

static void count_down(float dt)
{
    wgf_entity_t *doomed = NULL;
    int count = 0, capacity = 0, i;
    ecs_iter_t it = ecs_query_iter(ecs.world, ecs.q_lifetime);
    while (ecs_query_next(&it)) {
        wgf_ecs_priv_lifetime_t *life = ecs_field(&it, wgf_ecs_priv_lifetime_t, 0);
        const wgf_ecs_priv_ref_t *ref = ecs_field(&it, wgf_ecs_priv_ref_t, 1);
        for (i = 0; i < it.count; i++) {
            life[i].seconds -= dt;
            if (life[i].seconds <= 0.0f) {
                life[i].seconds = 0.0f;
                doom(&doomed, &count, &capacity, ref[i].handle);
            }
        }
    }
    destroy_all(doomed, count);
}

static void move(float dt)
{
    int i, k;
    ecs_iter_t it = ecs_query_iter(ecs.world, ecs.q_motion);
    while (ecs_query_next(&it)) {
        wgf_ecs_priv_transform_t *t = ecs_field(&it, wgf_ecs_priv_transform_t, 0);
        wgf_ecs_priv_motion_t *m = ecs_field(&it, wgf_ecs_priv_motion_t, 1);
        for (i = 0; i < it.count; i++) {
            if (m[i].damping > 0.0f) {
                const float keep = powf(1.0f - m[i].damping, dt);
                for (k = 0; k < 3; k++) m[i].velocity[k] *= keep;
            }
            if (m[i].max_speed > 0.0f) {
                const float speed = sqrtf(m[i].velocity[0] * m[i].velocity[0] + m[i].velocity[1] * m[i].velocity[1] +
                                          m[i].velocity[2] * m[i].velocity[2]);
                if (speed > m[i].max_speed) {
                    for (k = 0; k < 3; k++) m[i].velocity[k] *= m[i].max_speed / speed;
                }
            }
            for (k = 0; k < 3; k++) {
                t[i].position[k] += m[i].velocity[k] * dt;
                t[i].rotation[k] += m[i].spin[k] * dt;
            }
        }
    }
}

/* One axis of `t` against [lo, hi]: wrapped (its smoothing carried across), clamped (its
 * velocity across stopped), or found out (true: to destroy). */
static bool keep_in(wgf_ecs_priv_transform_t *t, wgf_ecs_priv_motion_t *motion, int axis, float lo, float hi, int mode)
{
    float *p = &t->position[axis];
    if (*p >= lo && *p <= hi) return false;
    if (mode == WGF_BOUNDS_MODE_DESTROY) return true;
    if (mode == WGF_BOUNDS_MODE_CLAMP) {
        *p = *p < lo ? lo : hi;
        if (motion != NULL) motion->velocity[axis] = 0.0f;
        return false;
    }
    {
        const float span = hi - lo;
        if (span <= 0.0f) {
            *p = lo;
            t->prev_position[axis] = lo;
            return false;
        }
        while (*p < lo) {
            *p += span;
            t->prev_position[axis] += span;
        }
        while (*p > hi) {
            *p -= span;
            t->prev_position[axis] -= span;
        }
    }
    return false;
}

static void bound(void)
{
    wgf_entity_t *doomed = NULL;
    int count = 0, capacity = 0, i;
    ecs_iter_t it = ecs_query_iter(ecs.world, ecs.q_bounds);
    while (ecs_query_next(&it)) {
        wgf_ecs_priv_transform_t *t = ecs_field(&it, wgf_ecs_priv_transform_t, 0);
        const wgf_ecs_priv_bounds_t *b = ecs_field(&it, wgf_ecs_priv_bounds_t, 1);
        const wgf_ecs_priv_ref_t *ref = ecs_field(&it, wgf_ecs_priv_ref_t, 2);
        for (i = 0; i < it.count; i++) {
            const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(ref[i].handle);
            wgf_ecs_priv_motion_t *motion =
                record != NULL && ecs_has_id(ecs.world, record->id, ecs.ids.motion)
                    ? (wgf_ecs_priv_motion_t *)ecs_get_mut_id(ecs.world, record->id, ecs.ids.motion)
                    : NULL;
            const float m = b[i].margin;
            const bool out_x = keep_in(&t[i], motion, 0, b[i].rect[0] - m, b[i].rect[0] + b[i].rect[2] + m, b[i].mode);
            const bool out_y = keep_in(&t[i], motion, 1, b[i].rect[1] - m, b[i].rect[1] + b[i].rect[3] + m, b[i].mode);
            if (out_x || out_y) doom(&doomed, &count, &capacity, ref[i].handle);
        }
    }
    destroy_all(doomed, count);
}

/* The probes the ecs publishes: entities, and each behavior's count (0 once none is
 * left, so an expectation reads 0 rather than a probe never set). */
static void publish(void)
{
    int count = 0, i, n, p;
    wgf_entity_t *all = wgf_ecs_priv_entities(&count);
    char probe[16 + WGF_ECS_PRIV_NAME_MAX];
    wgf_probe_set_value("ecs.entities", ecs.live);
    for (i = 0; i < count; i++) { /* each name a probe the first time it is seen */
        const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(all[i]);
        if (record->behavior == NULL || record->behavior->name[0] == '\0') continue;
        for (p = 0; p < ecs.probe_name_count && strcmp(ecs.probe_names[p], record->behavior->name) != 0; p++) {
        }
        if (p == ecs.probe_name_count) {
            if (ecs.probe_name_count == ecs.probe_name_capacity) {
                const int capacity = ecs.probe_name_capacity > 0 ? ecs.probe_name_capacity * 2 : 16;
                char(*grown)[WGF_ECS_PRIV_NAME_MAX] = realloc(ecs.probe_names, sizeof(*grown) * (size_t)capacity);
                if (grown == NULL) continue;
                ecs.probe_names = grown;
                ecs.probe_name_capacity = capacity;
            }
            memcpy(ecs.probe_names[ecs.probe_name_count++], record->behavior->name, WGF_ECS_PRIV_NAME_MAX);
        }
    }
    free(all);
    for (p = 0; p < ecs.probe_name_count; p++) {
        n = wgf_ecs_count_behavior(ecs.probe_names[p]);
        snprintf(probe, sizeof(probe), "ecs.behavior.%s", ecs.probe_names[p]);
        wgf_probe_set_value(probe, n); /* a name too long for a probe is simply not published */
    }
}

static void tick(float dt)
{
    count_down(dt);
    move(dt);
    bound();
    collide();
    publish();
}

/* As the tick begins, where each entity is: what the frames after it are drawn from. */
static void tick_begin(void)
{
    int i;
    ecs_iter_t it = ecs_query_iter(ecs.world, ecs.q_transform);
    while (ecs_query_next(&it)) {
        wgf_ecs_priv_transform_t *t = ecs_field(&it, wgf_ecs_priv_transform_t, 0);
        for (i = 0; i < it.count; i++) {
            memcpy(t[i].prev_position, t[i].position, sizeof(t[i].position));
            memcpy(t[i].prev_scale, t[i].scale, sizeof(t[i].scale));
            t[i].prev_rotation = wgf_quat_from_euler(wgf_vec3_make(t[i].rotation[0], t[i].rotation[1], t[i].rotation[2]));
        }
    }
}

/* Each frame, every entity's node given its transform between the last two ticks: the
 * rotation the shortest way round, so a turn across a full one draws no spin. */
static void update(float dt)
{
    const float f = wgf_core_priv_part_get_fraction();
    int i, k;
    ecs_iter_t it = ecs_query_iter(ecs.world, ecs.q_transform);
    (void)dt;
    while (ecs_query_next(&it)) {
        const wgf_ecs_priv_transform_t *t = ecs_field(&it, wgf_ecs_priv_transform_t, 0);
        const wgf_ecs_priv_ref_t *ref = ecs_field(&it, wgf_ecs_priv_ref_t, 1);
        for (i = 0; i < it.count; i++) {
            const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(ref[i].handle);
            wgf_gfx_priv_node_t *node_ptr = record != NULL ? wgf_gfx_priv_node_of(record->node) : NULL;
            const wgf_quat_t now = wgf_quat_from_euler(wgf_vec3_make(t[i].rotation[0], t[i].rotation[1], t[i].rotation[2]));
            float position[3], scale[3];
            if (node_ptr == NULL) continue;
            for (k = 0; k < 3; k++) {
                position[k] = t[i].prev_position[k] + (t[i].position[k] - t[i].prev_position[k]) * f;
                scale[k] = t[i].prev_scale[k] + (t[i].scale[k] - t[i].prev_scale[k]) * f;
            }
            node_ptr->position = wgf_vec3_make(position[0], position[1], position[2]);
            node_ptr->scale = wgf_vec3_make(scale[0], scale[1], scale[2]);
            node_ptr->rotation = wgf_quat_slerp(t[i].prev_rotation, now, f);
            wgf_gfx_priv_node_transform_changed(record->node);
        }
    }
}

/* Gone with gfx's stop (its nodes are gone by then: there is nothing of theirs to let go). */
static void stop(void)
{
    uint16_t i;
    if (ecs.world == NULL) return;
    for (i = 1; i < ecs.pool.capacity; i++) {
        const wgf_entity_t handle = wgf_core_priv_handle_pool_handle_from_index(&ecs.pool, i);
        wgf_ecs_priv_entity_t *record = handle != 0 ? &ecs.records[i] : NULL;
        if (record == NULL) continue;
        if (record->voice != 0) wgf_voice_destroy(record->voice);
        free(record->behavior);
    }
    wgf_ecs_priv_scene_shutdown();
    wgf_ecs_priv_dump_shutdown();
    ecs_query_fini(ecs.q_transform);
    ecs_query_fini(ecs.q_motion);
    ecs_query_fini(ecs.q_bounds);
    ecs_query_fini(ecs.q_lifetime);
    ecs_query_fini(ecs.q_collider);
    ecs_fini(ecs.world);
    wgf_core_priv_handle_pool_destroy(&ecs.pool);
    free(ecs.events);
    free(ecs.pairs);
    free(ecs.probe_names);
    memset(&ecs, 0, sizeof(ecs));
}

static wgf_core_priv_part_t part = {.name = "ecs",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_ECS,
                                    .update = update,
                                    .tick_begin = tick_begin,
                                    .tick = tick,
                                    .stop = stop};

/* ---- the world -------------------------------------------------------------------- */

static ecs_entity_t component(const char *name, size_t size, size_t alignment)
{
    ecs_entity_desc_t entity_desc;
    ecs_component_desc_t desc;
    memset(&entity_desc, 0, sizeof(entity_desc));
    entity_desc.name = name;
    memset(&desc, 0, sizeof(desc));
    desc.entity = ecs_entity_init(ecs.world, &entity_desc);
    desc.type.size = (ecs_size_t)size;
    desc.type.alignment = (ecs_size_t)alignment;
    return ecs_component_init(ecs.world, &desc);
}

static ecs_query_t *query(ecs_entity_t a, ecs_entity_t b, ecs_entity_t c)
{
    ecs_query_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.terms[0].id = a;
    desc.terms[1].id = b;
    desc.terms[2].id = c;
    return ecs_query_init(ecs.world, &desc);
}

bool wgf_ecs_priv_start(void)
{
    if (ecs.world != NULL) return true;
    ecs.events = (int *)malloc(sizeof(int) * 3 * EVENTS_MAX);
    if (ecs.events == NULL ||
        !wgf_core_priv_handle_pool_init(&ecs.pool, WGF_CORE_PRIV_HANDLE_KIND_ENTITY, (void **)&ecs.records,
                                        sizeof(wgf_ecs_priv_entity_t), 64, 65535)) {
        free(ecs.events);
        ecs.events = NULL;
        wgf_log_error("wgf_ecs: out of memory starting");
        return false;
    }
    ecs.world = ecs_mini();
    ecs.ids.ref = component("wgf_ref", sizeof(wgf_ecs_priv_ref_t), _Alignof(wgf_ecs_priv_ref_t));
    ecs.ids.transform = component("wgf_transform", sizeof(wgf_ecs_priv_transform_t), _Alignof(wgf_ecs_priv_transform_t));
    ecs.ids.motion = component("wgf_motion", sizeof(wgf_ecs_priv_motion_t), _Alignof(wgf_ecs_priv_motion_t));
    ecs.ids.bounds = component("wgf_bounds", sizeof(wgf_ecs_priv_bounds_t), _Alignof(wgf_ecs_priv_bounds_t));
    ecs.ids.lifetime = component("wgf_lifetime", sizeof(wgf_ecs_priv_lifetime_t), _Alignof(wgf_ecs_priv_lifetime_t));
    ecs.ids.collider = component("wgf_collider", sizeof(wgf_ecs_priv_collider_t), _Alignof(wgf_ecs_priv_collider_t));
    ecs.q_transform = query(ecs.ids.transform, ecs.ids.ref, 0);
    ecs.q_motion = query(ecs.ids.transform, ecs.ids.motion, 0);
    ecs.q_bounds = query(ecs.ids.transform, ecs.ids.bounds, ecs.ids.ref);
    ecs.q_lifetime = query(ecs.ids.lifetime, ecs.ids.ref, 0);
    ecs.q_collider = query(ecs.ids.transform, ecs.ids.collider, ecs.ids.ref);
    wgf_core_priv_part_install(&part);
    return true;
}

/* ---- entities' records ------------------------------------------------------------- */

wgf_entity_t wgf_ecs_priv_new_record(wgf_node_t node)
{
    const wgf_entity_t handle = wgf_core_priv_handle_pool_alloc(&ecs.pool);
    wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(handle);
    wgf_ecs_priv_ref_t ref;
    wgf_ecs_priv_transform_t t;
    if (record == NULL) return 0;
    memset(record, 0, sizeof(*record));
    record->id = ecs_new(ecs.world);
    record->order = ecs.next_order++;
    record->node = node;
    ref.handle = handle;
    ecs_set_id(ecs.world, record->id, ecs.ids.ref, sizeof(ref), &ref);
    memset(&t, 0, sizeof(t));
    t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
    t.prev_scale[0] = t.prev_scale[1] = t.prev_scale[2] = 1.0f;
    t.prev_rotation = wgf_quat_identity();
    ecs_set_id(ecs.world, record->id, ecs.ids.transform, sizeof(t), &t);
    ecs.live++;
    return handle;
}

void wgf_ecs_priv_free_record(wgf_entity_t entity)
{
    wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(entity);
    if (record == NULL) return;
    ecs_delete(ecs.world, record->id);
    free(record->behavior);
    record->behavior = NULL;
    wgf_core_priv_handle_pool_free(&ecs.pool, entity);
    ecs.live--;
}

int wgf_entity_get_count(void)
{
    return ecs.live;
}

/* ---- finding ---------------------------------------------------------------------- */

int wgf_ecs_find_behavior(const char *name, wgf_entity_t *out, int count)
{
    int all_count = 0, i, n = 0;
    wgf_entity_t *all;
    if (name == NULL || out == NULL || count <= 0) return 0;
    all = wgf_ecs_priv_entities(&all_count);
    for (i = 0; i < all_count && n < count; i++) {
        const wgf_ecs_priv_entity_t *record = wgf_ecs_priv_entity_of(all[i]);
        if (record->behavior != NULL && strcmp(record->behavior->name, name) == 0) out[n++] = all[i];
    }
    free(all);
    return n;
}

int wgf_ecs_count_behavior(const char *name)
{
    uint16_t i;
    int n = 0;
    if (name == NULL || ecs.world == NULL) return 0;
    for (i = 1; i < ecs.pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&ecs.pool, i) == 0) continue;
        if (ecs.records[i].behavior != NULL && strcmp(ecs.records[i].behavior->name, name) == 0) n++;
    }
    return n;
}

void wgf_ecs_clear(void)
{
    int count = 0, i;
    wgf_entity_t *all = wgf_ecs_priv_entities(&count);
    for (i = 0; i < count; i++) wgf_entity_destroy(all[i]); /* an earlier one may have taken it with its node */
    free(all);
}
