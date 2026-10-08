#include "wgf_behavior.h"
#include "wgf_world.h"

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
#include "wgf_ecs_store_priv.h"
#include "wgf_ecs_scene_priv.h"
#include "wgf_log.h"
#include "wgf_actor.h"
#include "wgf_probe.h"
#include "wgf_voice.h"

/* The ecs (wgf_world.h): the store (wgf_ecs_store_priv.h) made with the first actor given
 * a component or a behavior, the records of those actors, the systems run each tick over
 * the actors' simulated transforms, and the events. A part (wgf_core_part_priv.h):
 * installed by the first record, so a program that gives no actor a component links
 * none of it. */

#define EVENTS_MAX 65536
#define EVENT_INTS 4 /* an event, and its three ints */

typedef struct pair_t {
    wgf_actor_t a, b;          /* a < b */
    int32_t a_layer, b_layer; /* their colliders' layers as they met: what a trigger tells the other */
} pair_t;

static struct {
    bool started; /* the store started, the records' pool made */
    wgf_ecs_priv_ids_t ids;
    wgf_ecs_priv_query_t *q_motion, *q_bounds, *q_lifetime, *q_collider;
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
        wgf_ecs_priv_id_t tag;
    } *tags; /* each behavior name's tag, sorted by name */
    int tag_count, tag_capacity;
} ecs;

bool wgf_ecs_priv_started(void)
{
    return ecs.started;
}

const wgf_ecs_priv_ids_t *wgf_ecs_priv_ids(void)
{
    return &ecs.ids;
}

wgf_ecs_priv_record_t *wgf_ecs_priv_record_of_at(wgf_actor_t actor, const char *caller)
{
    const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of_at(actor, caller);
    uint16_t index;
    if (!ecs.started || actor_ptr == NULL || actor_ptr->components == 0 ||
        !wgf_core_priv_handle_pool_resolve(&ecs.pool, actor_ptr->components, &index)) {
        return NULL;
    }
    return &ecs.records[index];
}

void *wgf_ecs_priv_get_at(wgf_actor_t actor, wgf_ecs_priv_id_t component, const char *caller)
{
    const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of_at(actor, caller);
    return record != NULL ? wgf_ecs_priv_store_get(record->id, component) : NULL;
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
    if (!ecs.started || ecs.live == 0) return NULL;
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

void wgf_ecs_priv_raise(wgf_world_event_t event, int a, int b, int c)
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
    ecs.events[EVENT_INTS * at] = (int)event;
    ecs.events[EVENT_INTS * at + 1] = a;
    ecs.events[EVENT_INTS * at + 2] = b;
    ecs.events[EVENT_INTS * at + 3] = c;
    ecs.event_count++;
}

int wgf_world_get_event_count(void)
{
    return ecs.event_count;
}

int wgf_world_take_events(int *out, int count)
{
    int taken = 0;
    if (out == NULL || count < EVENT_INTS) return 0;
    while (ecs.event_count > 0 && taken + EVENT_INTS <= count) {
        memcpy(out + taken, &ecs.events[EVENT_INTS * ecs.event_head], sizeof(int) * EVENT_INTS);
        ecs.event_head = (ecs.event_head + 1) % EVENTS_MAX;
        ecs.event_count--;
        taken += EVENT_INTS;
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
    float x, y, z, r;
    int32_t layer, mask;
} body_t;

static int by_sweep(const void *a, const void *b)
{
    const body_t *x = (const body_t *)a, *y = (const body_t *)b;
    if (x->parent != y->parent) return x->parent < y->parent ? -1 : 1;
    return x->x - x->r < y->x - y->r ? -1 : (x->x - x->r > y->x - y->r ? 1 : 0);
}

static bool add_pair(pair_t **pairs, int *count, int *capacity, const body_t *a, const body_t *b)
{
    if (*count == *capacity) {
        const int grown_capacity = *capacity > 0 ? *capacity * 2 : 64;
        pair_t *grown = (pair_t *)realloc(*pairs, sizeof(pair_t) * (size_t)grown_capacity);
        if (grown == NULL) return false;
        *pairs = grown;
        *capacity = grown_capacity;
    }
    if (b->handle < a->handle) {
        const body_t *swap = a;
        a = b;
        b = swap;
    }
    (*pairs)[*count].a = a->handle;
    (*pairs)[*count].b = b->handle;
    (*pairs)[*count].a_layer = a->layer;
    (*pairs)[*count].b_layer = b->layer;
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
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    wgf_ecs_priv_store_walk(ecs.q_collider);
    while (wgf_ecs_priv_store_next(ecs.q_collider, fields, &entity)) {
        const wgf_ecs_priv_collider_t *c = (const wgf_ecs_priv_collider_t *)fields[0];
        const wgf_ecs_priv_ref_t *ref = (const wgf_ecs_priv_ref_t *)fields[1];
        const wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(ref->actor);
        float sx, sy;
        if (actor_ptr == NULL || !c->enabled) continue; /* switched off: it meets nothing */
        sx = fabsf(actor_ptr->scale.x);
        sy = fabsf(actor_ptr->scale.y);
        if (body_count == body_capacity) {
            const int capacity = body_capacity > 0 ? body_capacity * 2 : 64;
            body_t *grown = (body_t *)realloc(bodies, sizeof(body_t) * (size_t)capacity);
            if (grown == NULL) continue;
            bodies = grown;
            body_capacity = capacity;
        }
        bodies[body_count].handle = ref->actor;
        bodies[body_count].parent = actor_ptr->parent;
        bodies[body_count].x = actor_ptr->position.x;
        bodies[body_count].y = actor_ptr->position.y;
        bodies[body_count].z = actor_ptr->position.z;
        bodies[body_count].r = c->radius * (sx > sy ? sx : sy);
        bodies[body_count].layer = c->layer;
        bodies[body_count].mask = c->mask;
        body_count++;
    }
    if (body_count > 1) qsort(bodies, (size_t)body_count, sizeof(body_t), by_sweep);
    for (i = 0; i < body_count; i++) {
        const body_t *a = &bodies[i];
        for (j = i + 1; j < body_count && bodies[j].parent == a->parent && bodies[j].x - bodies[j].r <= a->x + a->r;
             j++) {
            const body_t *b = &bodies[j];
            const float dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z, reach = a->r + b->r;
            if (((a->layer & b->mask) == 0 && (b->layer & a->mask) == 0) || dx * dx + dy * dy + dz * dz > reach * reach) {
                continue;
            }
            if (!add_pair(&found, &found_count, &found_capacity, a, b)) {
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
            wgf_ecs_priv_raise(WGF_WORLD_EVENT_TRIGGER_ENTER, (int)found[i].a, (int)found[i].b, found[i].b_layer);
            wgf_ecs_priv_raise(WGF_WORLD_EVENT_TRIGGER_ENTER, (int)found[i].b, (int)found[i].a, found[i].a_layer);
            i++;
        } else if (order > 0) {
            wgf_ecs_priv_raise(WGF_WORLD_EVENT_TRIGGER_EXIT, (int)ecs.pairs[j].a, (int)ecs.pairs[j].b, ecs.pairs[j].b_layer);
            wgf_ecs_priv_raise(WGF_WORLD_EVENT_TRIGGER_EXIT, (int)ecs.pairs[j].b, (int)ecs.pairs[j].a, ecs.pairs[j].a_layer);
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
    int count = 0, capacity = 0;
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    wgf_ecs_priv_store_walk(ecs.q_lifetime);
    while (wgf_ecs_priv_store_next(ecs.q_lifetime, fields, &entity)) {
        wgf_ecs_priv_lifetime_t *life = (wgf_ecs_priv_lifetime_t *)fields[0];
        const wgf_ecs_priv_ref_t *ref = (const wgf_ecs_priv_ref_t *)fields[1];
        life->seconds -= dt;
        if (life->seconds <= 0.0f) {
            life->seconds = 0.0f;
            doom(&doomed, &count, &capacity, ref->actor);
        }
    }
    destroy_all(doomed, count);
}

static void move(float dt)
{
    int k;
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    wgf_ecs_priv_store_walk(ecs.q_motion);
    while (wgf_ecs_priv_store_next(ecs.q_motion, fields, &entity)) {
        wgf_ecs_priv_motion_t *m = (wgf_ecs_priv_motion_t *)fields[0];
        const wgf_ecs_priv_ref_t *ref = (const wgf_ecs_priv_ref_t *)fields[1];
        wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(ref->actor);
        if (actor_ptr == NULL) continue;
        if (m->damping > 0.0f) {
            const float keep = powf(1.0f - m->damping, dt);
            for (k = 0; k < 3; k++) m->velocity[k] *= keep;
        }
        if (m->max_speed > 0.0f) {
            const float speed = sqrtf(m->velocity[0] * m->velocity[0] + m->velocity[1] * m->velocity[1] +
                                      m->velocity[2] * m->velocity[2]);
            if (speed > m->max_speed) {
                for (k = 0; k < 3; k++) m->velocity[k] *= m->max_speed / speed;
            }
        }
        actor_ptr->position = wgf_vec3_add(actor_ptr->position, wgf_vec3_make(m->velocity[0] * dt,
                                                                            m->velocity[1] * dt,
                                                                            m->velocity[2] * dt));
        if (m->spin[0] != 0.0f || m->spin[1] != 0.0f || m->spin[2] != 0.0f) {
            /* turned by the tick's share of the spin, about the parent's axes: never through the
             * angles read back, which past a quarter turn about y aren't the ones that went in */
            const wgf_quat_t turn =
                wgf_quat_from_euler(wgf_vec3_make(m->spin[0] * dt, m->spin[1] * dt, m->spin[2] * dt));
            actor_ptr->rotation = wgf_quat_normalize(wgf_quat_mul(turn, actor_ptr->rotation));
        }
        wgf_gfx_priv_actor_transform_changed(ref->actor);
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
    int count = 0, capacity = 0;
    bool seen = false;
    wgf_vec4_t seen_area = wgf_vec4_make(0, 0, 0, 0); /* the visible area, read once a tick when one asks */
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    wgf_ecs_priv_store_walk(ecs.q_bounds);
    while (wgf_ecs_priv_store_next(ecs.q_bounds, fields, &entity)) {
        const wgf_ecs_priv_bounds_t *b = (const wgf_ecs_priv_bounds_t *)fields[0];
        const wgf_ecs_priv_ref_t *ref = (const wgf_ecs_priv_ref_t *)fields[1];
        const wgf_ecs_priv_record_t *record = wgf_ecs_priv_record_of(ref->actor);
        wgf_gfx_priv_actor_t *actor_ptr = wgf_gfx_priv_actor_of(ref->actor);
        wgf_ecs_priv_motion_t *motion =
            record != NULL ? (wgf_ecs_priv_motion_t *)wgf_ecs_priv_store_get(record->id, ecs.ids.motion) : NULL;
        const float m = b->margin;
        float rect[4];
        bool out_x, out_y;
        if (actor_ptr == NULL) continue;
        if (b->visible && !seen) {
            seen_area = wgf_presentation_get_visible();
            seen = true;
        }
        rect[0] = b->visible ? seen_area.x : b->rect[0];
        rect[1] = b->visible ? seen_area.y : b->rect[1];
        rect[2] = b->visible ? seen_area.z : b->rect[2];
        rect[3] = b->visible ? seen_area.w : b->rect[3];
        out_x = keep_in(actor_ptr, motion, 0, rect[0] - m, rect[0] + rect[2] + m, b->mode);
        out_y = keep_in(actor_ptr, motion, 1, rect[1] - m, rect[1] + rect[3] + m, b->mode);
        wgf_gfx_priv_actor_transform_changed(ref->actor);
        if (out_x || out_y) doom(&doomed, &count, &capacity, ref->actor);
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
        n = wgf_actor_count_with_behavior(ecs.probe_names[p]);
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
    if (!ecs.started) return;
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
    wgf_ecs_priv_store_query_free(ecs.q_motion);
    wgf_ecs_priv_store_query_free(ecs.q_bounds);
    wgf_ecs_priv_store_query_free(ecs.q_lifetime);
    wgf_ecs_priv_store_query_free(ecs.q_collider);
    wgf_ecs_priv_store_stop();
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
                                    .dump = wgf_world_dump};

/* ---- the world -------------------------------------------------------------------- */

static wgf_ecs_priv_query_t *query_of(wgf_ecs_priv_id_t a, wgf_ecs_priv_id_t b)
{
    const wgf_ecs_priv_id_t terms[2] = {a, b};
    return wgf_ecs_priv_store_query(terms, 2);
}

static void components_gone(wgf_actor_t actor, wgf_gfx_priv_actor_t *actor_ptr)
{
    (void)actor_ptr;
    wgf_ecs_priv_record_free(actor);
}

bool wgf_ecs_priv_start(void)
{
    if (ecs.started) return true;
    ecs.events = (int *)malloc(sizeof(int) * EVENT_INTS * EVENTS_MAX);
    if (ecs.events == NULL ||
        !wgf_core_priv_handle_pool_init(&ecs.pool, WGF_CORE_PRIV_HANDLE_KIND_COMPONENTS, (void **)&ecs.records,
                                        sizeof(wgf_ecs_priv_record_t), 64, 65535)) {
        free(ecs.events);
        ecs.events = NULL;
        wgf_log_error("wgf_ecs: out of memory starting");
        return false;
    }
    if (!wgf_ecs_priv_store_start()) {
        free(ecs.events);
        ecs.events = NULL;
        wgf_core_priv_handle_pool_destroy(&ecs.pool);
        wgf_log_error("wgf_ecs: out of memory starting");
        return false;
    }
    ecs.started = true;
    ecs.ids.ref = wgf_ecs_priv_store_component(sizeof(wgf_ecs_priv_ref_t), _Alignof(wgf_ecs_priv_ref_t));
    ecs.ids.motion = wgf_ecs_priv_store_component(sizeof(wgf_ecs_priv_motion_t), _Alignof(wgf_ecs_priv_motion_t));
    ecs.ids.bounds = wgf_ecs_priv_store_component(sizeof(wgf_ecs_priv_bounds_t), _Alignof(wgf_ecs_priv_bounds_t));
    ecs.ids.lifetime =
        wgf_ecs_priv_store_component(sizeof(wgf_ecs_priv_lifetime_t), _Alignof(wgf_ecs_priv_lifetime_t));
    ecs.ids.collider =
        wgf_ecs_priv_store_component(sizeof(wgf_ecs_priv_collider_t), _Alignof(wgf_ecs_priv_collider_t));
    ecs.ids.voice = wgf_ecs_priv_store_component(0, 1);
    ecs.q_motion = query_of(ecs.ids.motion, ecs.ids.ref);
    ecs.q_bounds = query_of(ecs.ids.bounds, ecs.ids.ref);
    ecs.q_lifetime = query_of(ecs.ids.lifetime, ecs.ids.ref);
    ecs.q_collider = query_of(ecs.ids.collider, ecs.ids.ref);
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
    record->id = wgf_ecs_priv_store_new();
    record->order = ecs.next_order++;
    record->actor = actor;
    record->next_behavior = 1;
    ref.actor = actor;
    wgf_ecs_priv_store_set(record->id, ecs.ids.ref, &ref);
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
        wgf_ecs_priv_raise(WGF_WORLD_EVENT_DESTROYED, (int)actor, record->behaviors[b]->id, 0);
    }
    wgf_ecs_priv_forget_pairs(actor);
    if (record->voice != 0) wgf_voice_destroy(record->voice);
    for (b = WGF_COMPONENT_BODY; b <= WGF_COMPONENT_VEHICLE; b++) { /* a part's components let go of by the part */
        const wgf_ecs_priv_part_component_t *hooks = wgf_ecs_priv_get_part_component((wgf_component_t)b);
        if (hooks != NULL && wgf_actor_has_component(actor, (wgf_component_t)b)) hooks->remove(actor);
    }
    record = wgf_ecs_priv_record_of(actor);
    wgf_ecs_priv_store_delete(record->id);
    free_behaviors(record);
    wgf_core_priv_handle_pool_free(&ecs.pool, actor_ptr->components);
    actor_ptr->components = 0;
    wgf_gfx_priv_actor_set_simulated(actor_ptr, false);
    ecs.live--;
}

int wgf_actor_get_count(void)
{
    return ecs.live;
}

/* ---- finding ---------------------------------------------------------------------- */

wgf_ecs_priv_id_t wgf_ecs_priv_behavior_tag(const char *name, bool make)
{
    int low = 0, high = ecs.tag_count;
    if (!ecs.started || name == NULL) return 0;
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
    ecs.tags[low].tag = wgf_ecs_priv_store_component(0, 1);
    return ecs.tags[low].tag;
}

/* The actors whose entities have `id`, oldest first, into `out` as many as fit in
 * `count`: the store's set of the id gives them, never a look at every record. */
static int find_id(wgf_ecs_priv_id_t id, wgf_actor_t *out, int count)
{
    wgf_actor_t *all;
    int n = 0, total;
    wgf_ecs_priv_query_t *query;
    void *fields[2];
    wgf_ecs_priv_id_t entity;
    if (!ecs.started || id == 0 || out == NULL || count <= 0) return 0;
    total = wgf_ecs_priv_store_count(id);
    if (total == 0) return 0;
    all = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)total);
    if (all == NULL) {
        wgf_log_error("wgf_ecs: out of memory finding actors");
        return 0;
    }
    query = query_of(id, ecs.ids.ref); /* the store's index of the id answers it */
    if (query == NULL) {
        free(all);
        return 0;
    }
    wgf_ecs_priv_store_walk(query);
    while (n < total && wgf_ecs_priv_store_next(query, fields, &entity)) {
        all[n++] = ((const wgf_ecs_priv_ref_t *)fields[1])->actor;
    }
    wgf_ecs_priv_store_query_free(query);
    qsort(all, (size_t)n, sizeof(wgf_actor_t), by_order);
    if (n > count) n = count;
    memcpy(out, all, sizeof(wgf_actor_t) * (size_t)n);
    free(all);
    return n;
}

static wgf_ecs_priv_id_t component_id(wgf_component_t component)
{
    switch (component) {
        case WGF_COMPONENT_MOTION: return ecs.ids.motion;
        case WGF_COMPONENT_BOUNDS: return ecs.ids.bounds;
        case WGF_COMPONENT_LIFETIME: return ecs.ids.lifetime;
        case WGF_COMPONENT_COLLIDER: return ecs.ids.collider;
        case WGF_COMPONENT_VOICE: return ecs.ids.voice;
        case WGF_COMPONENT_BODY:
        case WGF_COMPONENT_VEHICLE: {
            const wgf_ecs_priv_part_component_t *hooks = wgf_ecs_priv_get_part_component(component);
            return hooks != NULL ? hooks->id : 0;
        }
        default: return 0;
    }
}

int wgf_actor_find_with_behavior(const char *name, wgf_actor_t *out, int count)
{
    return find_id(wgf_ecs_priv_behavior_tag(name, false), out, count);
}

int wgf_actor_count_with_behavior(const char *name)
{
    return wgf_ecs_priv_store_count(wgf_ecs_priv_behavior_tag(name, false));
}

int wgf_actor_find_with_component(wgf_component_t component, wgf_actor_t *out, int count)
{
    return find_id(component_id(component), out, count);
}

int wgf_actor_count_with_component(wgf_component_t component)
{
    return ecs.started ? wgf_ecs_priv_store_count(component_id(component)) : 0;
}

void wgf_world_clear(void)
{
    int count = 0, i;
    wgf_actor_t *all = wgf_ecs_priv_actors(&count);
    for (i = 0; i < count; i++) wgf_actor_destroy(all[i], WGF_ACTOR_DESTROY_CHILDREN); /* one may be gone with another */
    free(all);
}
