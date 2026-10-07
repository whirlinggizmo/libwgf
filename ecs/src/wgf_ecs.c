#include "wgf_ecs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_bounds.h"
#include "wgf_collider.h"
#include "wgf_presentation.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_ecs_priv.h"
#include "wgf_ecs_scene_priv.h"
#include "wgf_log.h"
#include "wgf_actor.h"
#include "wgf_probe.h"
#include "wgf_voice.h"

/* The ecs (wgf_ecs.h): a flecs world made with the first actor given a component or a
 * behavior, the records of those actors, the systems run each tick over the actors'
 * simulated transforms, and the events. A part (wgf_core_part_priv.h): installed by the
 * first record, so a program that gives no actor a component links none of flecs. */

#define EVENTS_MAX 65536

typedef struct pair_t {
    wgf_actor_t a, b; /* a < b */
} pair_t;

static struct {
    ecs_world_t *world;
    wgf_ecs_priv_ids_t ids;
    ecs_query_t *q_motion, *q_bounds, *q_lifetime, *q_collider;
    wgf_core_priv_handle_pool_t pool;
    wgf_ecs_priv_record_t *records;
    uint64_t next_order;
    int live;
    int *events; /* EVENTS_MAX triples, a ring */
    int event_head, event_count;
    bool events_dropped;
    pair_t *pairs; /* the colliders overlapping as of the last tick, sorted */
    int pair_count, pair_capacity;
    char (*probe_names)[WGF_ECS_PRIV_NAME_MAX]; /* every behavior name a probe was set for */
    int probe_name_count, probe_name_capacity;
    struct {
        char name[WGF_ECS_PRIV_NAME_MAX];
        ecs_entity_t tag;
    } *tags; /* each behavior name's tag, sorted by name */
    int tag_count, tag_capacity;
} ecs;

ecs_world_t *wgf_ecs_priv_world(void)
{
    return ecs.world;
}

const wgf_ecs_priv_ids_t *wgf_ecs_priv_ids(void)
{
    return &ecs.ids;
}

wgf_ecs_priv_record_t *wgf_ecs_priv_record_of(wgf_actor_t actor)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    uint16_t index;
    if (ecs.world == NULL || actor_ptr == NULL || actor_ptr->components == 0 ||
        !wgf_core_priv_handle_pool_resolve(&ecs.pool, actor_ptr->components, &index)) {
        return NULL;
    }
    return &ecs.records[index];
}

void *wgf_ecs_priv_get(wgf_actor_t actor, ecs_entity_t component)
{
    const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    if (record == NULL || !ecs_has_id(ecs.world, record->id, component)) return NULL;
    return ecs_get_mut_id(ecs.world, record->id, component);
}

static int by_order(const void *a, const void *b)
{
    const wgf_ecs_priv_record_t *x = wgf_ecs_priv_record_of(*(const wgf_actor_t *)a);
    const wgf_ecs_priv_record_t *y = wgf_ecs_priv_record_of(*(const wgf_actor_t *)b);
    return x->order < y->order ? -1 : (x->order > y->order ? 1 : 0);
}

wgf_actor_t *wgf_ecs_priv_actors(int *count)
{
    wgf_actor_t *out;
    uint16_t i;
    int n = 0;
    *count = 0;
    if (ecs.world == NULL || ecs.live == 0) return NULL;
    out = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)ecs.live);
    if (out == NULL) return NULL;
    for (i = 1; i < ecs.pool.capacity && n < ecs.live; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&ecs.pool, i) != 0) out[n++] = ecs.records[i].actor;
    }
    qsort(out, (size_t)n, sizeof(wgf_actor_t), by_order);
    *count = n;
    return out;
}

/* ---- events -------------------------------------------------------------------- */

void wgf_ecs_priv_raise(wgf_ecs_event_t event, int a, int b)
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
    ecs.events[3 * at + 1] = a;
    ecs.events[3 * at + 2] = b;
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

void wgf_ecs_priv_forget_pairs(wgf_actor_t actor)
{
    int i, kept = 0;
    for (i = 0; i < ecs.pair_count; i++) {
        if (ecs.pairs[i].a != actor && ecs.pairs[i].b != actor) ecs.pairs[kept++] = ecs.pairs[i];
    }
    ecs.pair_count = kept;
}

int wgf_collider_get_overlaps(wgf_actor_t actor, wgf_actor_t *out, int count)
{
    int i, n = 0;
    if (out == NULL || wgf_ecs_priv_get(actor, ecs.ids.collider) == NULL) return 0;
    for (i = 0; i < ecs.pair_count && n < count; i++) {
        if (ecs.pairs[i].a == actor) out[n++] = ecs.pairs[i].b;
        else if (ecs.pairs[i].b == actor) out[n++] = ecs.pairs[i].a;
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
    wgf_actor_t handle;
    wgf_actor_t parent;
    float x, y, r;
    int32_t layer, mask;
} body_t;

static int by_sweep(const void *a, const void *b)
{
    const body_t *x = (const body_t *)a, *y = (const body_t *)b;
    if (x->parent != y->parent) return x->parent < y->parent ? -1 : 1;
    return x->x - x->r < y->x - y->r ? -1 : (x->x - x->r > y->x - y->r ? 1 : 0);
}

static bool add_pair(pair_t **pairs, int *count, int *capacity, wgf_actor_t a, wgf_actor_t b)
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
        const wgf_ecs_priv_collider_t *c = ecs_field(&it, wgf_ecs_priv_collider_t, 0);
        const wgf_ecs_priv_ref_t *ref = ecs_field(&it, wgf_ecs_priv_ref_t, 1);
        for (i = 0; i < it.count; i++) {
            const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(ref[i].actor);
            float sx, sy;
            if (actor_ptr == NULL || !c[i].enabled) continue; /* switched off: it meets nothing */
            sx = fabsf(actor_ptr->scale.x);
            sy = fabsf(actor_ptr->scale.y);
            if (body_count == body_capacity) {
                const int capacity = body_capacity > 0 ? body_capacity * 2 : 64;
                body_t *grown = (body_t *)realloc(bodies, sizeof(body_t) * (size_t)capacity);
                if (grown == NULL) continue;
                bodies = grown;
                body_capacity = capacity;
            }
            bodies[body_count].handle = ref[i].actor;
            bodies[body_count].parent = actor_ptr->parent;
            bodies[body_count].x = actor_ptr->position.x;
            bodies[body_count].y = actor_ptr->position.y;
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
            wgf_ecs_priv_raise(WGF_ECS_EVENT_TRIGGER_ENTER, (int)found[i].a, (int)found[i].b);
            wgf_ecs_priv_raise(WGF_ECS_EVENT_TRIGGER_ENTER, (int)found[i].b, (int)found[i].a);
            i++;
        } else if (order > 0) {
            wgf_ecs_priv_raise(WGF_ECS_EVENT_TRIGGER_EXIT, (int)ecs.pairs[j].a, (int)ecs.pairs[j].b);
            wgf_ecs_priv_raise(WGF_ECS_EVENT_TRIGGER_EXIT, (int)ecs.pairs[j].b, (int)ecs.pairs[j].a);
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

/* Destroy every actor in `doomed`, with everything under it, after the query that found
 * them has finished (one may have gone with another's tree by then). */
static void destroy_all(wgf_actor_t *doomed, int count)
{
    int i;
    for (i = 0; i < count; i++) wgf_actor_destroy(doomed[i], WGF_ACTOR_DESTROY_CHILDREN);
    free(doomed);
}

static bool doom(wgf_actor_t **doomed, int *count, int *capacity, wgf_actor_t actor)
{
    if (*count == *capacity) {
        const int grown_capacity = *capacity > 0 ? *capacity * 2 : 16;
        wgf_actor_t *grown = (wgf_actor_t *)realloc(*doomed, sizeof(wgf_actor_t) * (size_t)grown_capacity);
        if (grown == NULL) return false;
        *doomed = grown;
        *capacity = grown_capacity;
    }
    (*doomed)[(*count)++] = actor;
    return true;
}

static void count_down(float dt)
{
    wgf_actor_t *doomed = NULL;
    int count = 0, capacity = 0, i;
    ecs_iter_t it = ecs_query_iter(ecs.world, ecs.q_lifetime);
    while (ecs_query_next(&it)) {
        wgf_ecs_priv_lifetime_t *life = ecs_field(&it, wgf_ecs_priv_lifetime_t, 0);
        const wgf_ecs_priv_ref_t *ref = ecs_field(&it, wgf_ecs_priv_ref_t, 1);
        for (i = 0; i < it.count; i++) {
            life[i].seconds -= dt;
            if (life[i].seconds <= 0.0f) {
                life[i].seconds = 0.0f;
                doom(&doomed, &count, &capacity, ref[i].actor);
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
        wgf_ecs_priv_motion_t *m = ecs_field(&it, wgf_ecs_priv_motion_t, 0);
        const wgf_ecs_priv_ref_t *ref = ecs_field(&it, wgf_ecs_priv_ref_t, 1);
        for (i = 0; i < it.count; i++) {
            wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(ref[i].actor);
            wgf_vec3_t angles;
            if (actor_ptr == NULL) continue;
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
            actor_ptr->position = wgf_vec3_add(actor_ptr->position, wgf_vec3_make(m[i].velocity[0] * dt,
                                                                                m[i].velocity[1] * dt,
                                                                                m[i].velocity[2] * dt));
            if (m[i].spin[0] != 0.0f || m[i].spin[1] != 0.0f || m[i].spin[2] != 0.0f) {
                angles = wgf_quat_to_euler(actor_ptr->rotation);
                actor_ptr->rotation = wgf_quat_from_euler(wgf_vec3_make(angles.x + m[i].spin[0] * dt,
                                                                       angles.y + m[i].spin[1] * dt,
                                                                       angles.z + m[i].spin[2] * dt));
            }
            wgf_gfx_priv_actor_transform_changed(ref[i].actor);
        }
    }
}

/* One axis of an actor's position against [lo, hi]: wrapped (its smoothing carried across),
 * clamped (its velocity across stopped), or found out (true: to destroy). */
static bool keep_in(wgf_gfx_priv_actor_t *actor_ptr, wgf_ecs_priv_motion_t *motion, int axis, float lo, float hi,
                    int mode)
{
    float *p = axis == 0 ? &actor_ptr->position.x : &actor_ptr->position.y;
    float unsmoothed, *prev = &unsmoothed; /* an actor with no previous transform (out of memory) */
    if (actor_ptr->previous != NULL) {
        prev = axis == 0 ? &actor_ptr->previous->position.x : &actor_ptr->previous->position.y;
    }
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
            *prev = lo;
            return false;
        }
        while (*p < lo) {
            *p += span;
            *prev += span;
        }
        while (*p > hi) {
            *p -= span;
            *prev -= span;
        }
    }
    return false;
}

static void bound(void)
{
    wgf_actor_t *doomed = NULL;
    int count = 0, capacity = 0, i;
    bool seen = false;
    wgf_vec4_t seen_area = wgf_vec4_make(0, 0, 0, 0); /* the visible area, read once a tick when one asks */
    ecs_iter_t it = ecs_query_iter(ecs.world, ecs.q_bounds);
    while (ecs_query_next(&it)) {
        const wgf_ecs_priv_bounds_t *b = ecs_field(&it, wgf_ecs_priv_bounds_t, 0);
        const wgf_ecs_priv_ref_t *ref = ecs_field(&it, wgf_ecs_priv_ref_t, 1);
        for (i = 0; i < it.count; i++) {
            const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(ref[i].actor);
            wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(ref[i].actor);
            wgf_ecs_priv_motion_t *motion =
                record != NULL && ecs_has_id(ecs.world, record->id, ecs.ids.motion)
                    ? (wgf_ecs_priv_motion_t *)ecs_get_mut_id(ecs.world, record->id, ecs.ids.motion)
                    : NULL;
            const float m = b[i].margin;
            float rect[4];
            bool out_x, out_y;
            if (actor_ptr == NULL) continue;
            if (b[i].visible && !seen) {
                seen_area = wgf_presentation_get_visible();
                seen = true;
            }
            rect[0] = b[i].visible ? seen_area.x : b[i].rect[0];
            rect[1] = b[i].visible ? seen_area.y : b[i].rect[1];
            rect[2] = b[i].visible ? seen_area.z : b[i].rect[2];
            rect[3] = b[i].visible ? seen_area.w : b[i].rect[3];
            out_x = keep_in(actor_ptr, motion, 0, rect[0] - m, rect[0] + rect[2] + m, b[i].mode);
            out_y = keep_in(actor_ptr, motion, 1, rect[1] - m, rect[1] + rect[3] + m, b[i].mode);
            wgf_gfx_priv_actor_transform_changed(ref[i].actor);
            if (out_x || out_y) doom(&doomed, &count, &capacity, ref[i].actor);
        }
    }
    destroy_all(doomed, count);
}

/* A behavior name's probe, the first time it is seen. */
static void note_name(const char *name)
{
    int p;
    for (p = 0; p < ecs.probe_name_count && strcmp(ecs.probe_names[p], name) != 0; p++) {
    }
    if (p == ecs.probe_name_count) {
        if (ecs.probe_name_count == ecs.probe_name_capacity) {
            const int capacity = ecs.probe_name_capacity > 0 ? ecs.probe_name_capacity * 2 : 16;
            char(*grown)[WGF_ECS_PRIV_NAME_MAX] = realloc(ecs.probe_names, sizeof(*grown) * (size_t)capacity);
            if (grown == NULL) return;
            ecs.probe_names = grown;
            ecs.probe_name_capacity = capacity;
        }
        memcpy(ecs.probe_names[ecs.probe_name_count++], name, WGF_ECS_PRIV_NAME_MAX);
    }
}

/* The probes the ecs publishes: the actors with components or behaviors (named
 * `ecs.entities`, as milestone 1's autopilots read it), and each behavior's actors (0 once
 * none is left, so an expectation reads 0 rather than a probe never set). */
static void publish(void)
{
    int i, n, p, b;
    char probe[16 + WGF_ECS_PRIV_NAME_MAX];
    wgf_probe_set_value("ecs.entities", ecs.live);
    for (i = 1; i < ecs.pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&ecs.pool, (uint16_t)i) == 0) continue;
        for (b = 0; b < ecs.records[i].behavior_count; b++) note_name(ecs.records[i].behaviors[b]->name);
    }
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

/* As the tick begins, where each simulated actor is: what the frames after it are drawn
 * from. */
static void tick_begin(void)
{
    uint16_t i;
    for (i = 1; i < ecs.pool.capacity; i++) {
        wgf_gfx_priv_actor_t *actor_ptr;
        if (wgf_core_priv_handle_pool_handle_from_index(&ecs.pool, i) == 0) continue;
        actor_ptr = wgf_gfx_priv_actor_of(ecs.records[i].actor);
        if (actor_ptr == NULL || actor_ptr->previous == NULL) continue;
        actor_ptr->previous->position = actor_ptr->position;
        actor_ptr->previous->rotation = actor_ptr->rotation;
        actor_ptr->previous->scale = actor_ptr->scale;
    }
}

/* Each frame, every simulated actor's matrices made again: it is drawn between its last two
 * ticks at this frame's tick fraction (gfx's actor, drawn_transform). */
static void update(float dt)
{
    uint16_t i;
    (void)dt;
    for (i = 1; i < ecs.pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&ecs.pool, i) == 0) continue;
        wgf_gfx_priv_actor_transform_changed(ecs.records[i].actor);
    }
}

static void free_behaviors(wgf_ecs_priv_record_t *record)
{
    int b;
    for (b = 0; b < record->behavior_count; b++) wgf_ecs_priv_behavior_free(record->behaviors[b]);
    free(record->behaviors);
    record->behaviors = NULL;
    record->behavior_count = record->behavior_capacity = 0;
}

/* Gone with gfx's stop, after its actors (whose going let go of their records). */
static void stop(void)
{
    uint16_t i;
    if (ecs.world == NULL) return;
    for (i = 1; i < ecs.pool.capacity; i++) {
        wgf_ecs_priv_record_t *record =
            wgf_core_priv_handle_pool_handle_from_index(&ecs.pool, i) != 0 ? &ecs.records[i] : NULL;
        if (record == NULL) continue;
        if (record->voice != 0) wgf_voice_destroy(record->voice);
        free_behaviors(record);
    }
    wgf_gfx_priv_actor_set_components_hook(NULL);
    wgf_ecs_priv_scene_shutdown();
    wgf_ecs_priv_dump_shutdown();
    ecs_query_fini(ecs.q_motion);
    ecs_query_fini(ecs.q_bounds);
    ecs_query_fini(ecs.q_lifetime);
    ecs_query_fini(ecs.q_collider);
    ecs_fini(ecs.world);
    wgf_core_priv_handle_pool_destroy(&ecs.pool);
    free(ecs.events);
    free(ecs.pairs);
    free(ecs.probe_names);
    free(ecs.tags);
    memset(&ecs, 0, sizeof(ecs));
}

static wgf_core_priv_part_t part = {.name = "ecs",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_ECS,
                                    .update = update,
                                    .tick_begin = tick_begin,
                                    .tick = tick,
                                    .stop = stop,
                                    .dump = wgf_ecs_dump};

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

static ecs_query_t *query(ecs_entity_t a, ecs_entity_t b)
{
    ecs_query_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.terms[0].id = a;
    desc.terms[1].id = b;
    return ecs_query_init(ecs.world, &desc);
}

static void components_gone(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr)
{
    (void)actor_ptr;
    wgf_ecs_priv_record_free(actor);
}

bool wgf_ecs_priv_start(void)
{
    if (ecs.world != NULL) return true;
    ecs.events = (int *)malloc(sizeof(int) * 3 * EVENTS_MAX);
    if (ecs.events == NULL ||
        !wgf_core_priv_handle_pool_init(&ecs.pool, WGF_CORE_PRIV_HANDLE_KIND_COMPONENTS, (void **)&ecs.records,
                                        sizeof(wgf_ecs_priv_record_t), 64, 65535)) {
        free(ecs.events);
        ecs.events = NULL;
        wgf_log_error("wgf_ecs: out of memory starting");
        return false;
    }
    ecs.world = ecs_mini();
    ecs.ids.ref = component("wgf_ref", sizeof(wgf_ecs_priv_ref_t), _Alignof(wgf_ecs_priv_ref_t));
    ecs.ids.motion = component("wgf_motion", sizeof(wgf_ecs_priv_motion_t), _Alignof(wgf_ecs_priv_motion_t));
    ecs.ids.bounds = component("wgf_bounds", sizeof(wgf_ecs_priv_bounds_t), _Alignof(wgf_ecs_priv_bounds_t));
    ecs.ids.lifetime = component("wgf_lifetime", sizeof(wgf_ecs_priv_lifetime_t), _Alignof(wgf_ecs_priv_lifetime_t));
    ecs.ids.collider = component("wgf_collider", sizeof(wgf_ecs_priv_collider_t), _Alignof(wgf_ecs_priv_collider_t));
    ecs.ids.voice = ecs_new(ecs.world);
    ecs.q_motion = query(ecs.ids.motion, ecs.ids.ref);
    ecs.q_bounds = query(ecs.ids.bounds, ecs.ids.ref);
    ecs.q_lifetime = query(ecs.ids.lifetime, ecs.ids.ref);
    ecs.q_collider = query(ecs.ids.collider, ecs.ids.ref);
    wgf_gfx_priv_actor_set_components_hook(components_gone);
    wgf_core_priv_part_install(&part);
    return true;
}

/* ---- the records -------------------------------------------------------------------- */

wgf_ecs_priv_record_t *wgf_ecs_priv_record_make(wgf_actor_t actor)
{
    wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    wgf_gfx_priv_actor_t *actor_ptr;
    wgf_ecs_priv_ref_t ref;
    wgf_handle_t handle;
    if (record != NULL) return record;
    if (wgf_gfx_priv_actor_of(actor) == NULL || !wgf_ecs_priv_start()) return NULL;
    handle = wgf_core_priv_handle_pool_alloc(&ecs.pool);
    if (handle == 0) {
        wgf_log_error("wgf_ecs: no room for another actor's components");
        return NULL;
    }
    actor_ptr = wgf_gfx_priv_actor_of(actor);
    actor_ptr->components = handle;
    wgf_gfx_priv_actor_set_simulated(actor_ptr, true);
    record = wgf_ecs_priv_record_of(actor);
    memset(record, 0, sizeof(*record));
    record->id = ecs_new(ecs.world);
    record->order = ecs.next_order++;
    record->actor = actor;
    record->next_behavior = 1;
    ref.actor = actor;
    ecs_set_id(ecs.world, record->id, ecs.ids.ref, sizeof(ref), &ref);
    ecs.live++;
    return record;
}

void wgf_ecs_priv_record_free(wgf_actor_t actor)
{
    wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(actor);
    wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(actor);
    int b;
    if (record == NULL) return;
    for (b = 0; b < record->behavior_count; b++) {
        wgf_ecs_priv_raise(WGF_ECS_EVENT_DESTROYED, (int)actor, record->behaviors[b]->id);
    }
    wgf_ecs_priv_forget_pairs(actor);
    if (record->voice != 0) wgf_voice_destroy(record->voice);
    record = wgf_ecs_priv_record_of(actor);
    ecs_delete(ecs.world, record->id);
    free_behaviors(record);
    wgf_core_priv_handle_pool_free(&ecs.pool, actor_ptr->components);
    actor_ptr->components = 0;
    wgf_gfx_priv_actor_set_simulated(actor_ptr, false);
    ecs.live--;
}

int wgf_ecs_get_count(void)
{
    return ecs.live;
}

/* ---- finding ---------------------------------------------------------------------- */

ecs_entity_t wgf_ecs_priv_behavior_tag(const char *name, bool make)
{
    int low = 0, high = ecs.tag_count;
    if (ecs.world == NULL || name == NULL) return 0;
    while (low < high) { /* the first not before `name` */
        const int mid = (low + high) / 2;
        if (strcmp(ecs.tags[mid].name, name) < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low < ecs.tag_count && strcmp(ecs.tags[low].name, name) == 0) return ecs.tags[low].tag;
    if (!make || strlen(name) >= WGF_ECS_PRIV_NAME_MAX) return 0;
    if (ecs.tag_count == ecs.tag_capacity) {
        const int capacity = ecs.tag_capacity > 0 ? ecs.tag_capacity * 2 : 16;
        void *grown = realloc(ecs.tags, sizeof(ecs.tags[0]) * (size_t)capacity);
        if (grown == NULL) return 0;
        ecs.tags = grown;
        ecs.tag_capacity = capacity;
    }
    memmove(&ecs.tags[low + 1], &ecs.tags[low], sizeof(ecs.tags[0]) * (size_t)(ecs.tag_count - low));
    ecs.tag_count++;
    memcpy(ecs.tags[low].name, name, strlen(name) + 1);
    ecs.tags[low].tag = ecs_new(ecs.world);
    return ecs.tags[low].tag;
}

/* The actors whose flecs entities have `id`, oldest first, into `out` as many as fit in
 * `count`: flecs's index of the id gives them, never a look at every record. */
static int find_id(ecs_id_t id, wgf_actor_t *out, int count)
{
    wgf_actor_t *all;
    int n = 0, total;
    ecs_iter_t it;
    if (ecs.world == NULL || id == 0 || out == NULL || count <= 0) return 0;
    total = ecs_count_id(ecs.world, id);
    if (total == 0) return 0;
    all = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)total);
    if (all == NULL) {
        wgf_log_error("wgf_ecs: out of memory finding actors");
        return 0;
    }
    it = ecs_each_id(ecs.world, id);
    while (ecs_each_next(&it)) {
        int i;
        for (i = 0; i < it.count && n < total; i++) {
            const wgf_ecs_priv_ref_t *ref =
                (const wgf_ecs_priv_ref_t *)ecs_get_id(ecs.world, it.entities[i], ecs.ids.ref);
            if (ref != NULL) all[n++] = ref->actor;
        }
    }
    qsort(all, (size_t)n, sizeof(wgf_actor_t), by_order);
    if (n > count) n = count;
    memcpy(out, all, sizeof(wgf_actor_t) * (size_t)n);
    free(all);
    return n;
}

static ecs_id_t component_id(wgf_component_t component)
{
    switch (component) {
        case WGF_COMPONENT_MOTION: return ecs.ids.motion;
        case WGF_COMPONENT_BOUNDS: return ecs.ids.bounds;
        case WGF_COMPONENT_LIFETIME: return ecs.ids.lifetime;
        case WGF_COMPONENT_COLLIDER: return ecs.ids.collider;
        case WGF_COMPONENT_VOICE: return ecs.ids.voice;
        default: return 0;
    }
}

int wgf_ecs_find_behavior(const char *name, wgf_actor_t *out, int count)
{
    return find_id(wgf_ecs_priv_behavior_tag(name, false), out, count);
}

int wgf_ecs_count_behavior(const char *name)
{
    const ecs_entity_t tag = wgf_ecs_priv_behavior_tag(name, false);
    return tag != 0 ? ecs_count_id(ecs.world, tag) : 0;
}

int wgf_ecs_find_component(wgf_component_t component, wgf_actor_t *out, int count)
{
    return find_id(component_id(component), out, count);
}

int wgf_ecs_count_component(wgf_component_t component)
{
    const ecs_id_t id = component_id(component);
    return ecs.world != NULL && id != 0 ? ecs_count_id(ecs.world, id) : 0;
}

void wgf_ecs_clear(void)
{
    int count = 0, i;
    wgf_actor_t *all = wgf_ecs_priv_actors(&count);
    for (i = 0; i < count; i++) wgf_actor_destroy(all[i], WGF_ACTOR_DESTROY_CHILDREN); /* one may be gone with another */
    free(all);
}
