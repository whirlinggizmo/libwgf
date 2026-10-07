#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_behavior.h"
#include "wgf_component.h"
#include "wgf_ecs.h"
#include "wgf_loop.h"
#include "wgf_motion.h"
#include "wgf_stage2d.h"
#include "wgf_time.h"

/* The actor benchmark (SPEC.md, "An actor benchmark"; tools/bench/measure_actors.py runs
 * it): what an actor costs, in bytes and in time, at 10k and 50k, static and moving, in
 * flat and deep trees, with and without behaviors, with churn, and found by name, path,
 * and component. One row a run, named by its argument (`--list` prints them), so each
 * starts from a fresh heap:
 *
 *   bytes   the heap's growth (glibc's mallinfo2, mapped chunks too) from making the
 *           actors, over their count: everything an actor brings, its pools' slack
 *           included, past the fixed cost the first one's component pays (the world)
 *   time    the frames' own cost (wgf_loop_get_frame_cost: the tick, its systems, the
 *           stage walked and drawn) over the actors, in nanoseconds an actor a frame,
 *           the lookup row's finds taken out; its finds' rows, nanoseconds a find (by
 *           component, an actor found)
 *
 * Flown by an autopilot (measure_actors.py writes one), so each frame is one tick and
 * none waits for a display. Each line it prints is "bench_actors: <row> key=value ...".
 * Linux alone: mallinfo2 is glibc's. */

#define DEEP 32         /* a deep tree: chains of 32, each actor its predecessor's child */
#define WARM 60         /* frames before measuring: the churn's first second */
#define MEASURED 240    /* frames measured */
#define CHURN 1000      /* actors spawned, and as many destroyed, a second */
#define CARS 1000       /* the lookup row's cars, each with a wheel_rl/smoke */

typedef struct row_t {
    const char *name;
    int count;
    bool moving, deep, behaviors, churn, lookup;
} row_t;

static const row_t rows[] = {
    {"10k-static-flat", 10000, false, false, false, false, false},
    {"10k-moving-flat", 10000, true, false, false, false, false},
    {"10k-moving-deep", 10000, true, true, false, false, false},
    {"10k-moving-behaviors", 10000, true, false, true, false, false},
    {"10k-churn", 10000, true, false, true, true, false},
    {"10k-lookup", 10000, true, false, false, false, true},
    {"50k-static-flat", 50000, false, false, false, false, false},
    {"50k-moving-flat", 50000, true, false, false, false, false},
    {"50k-moving-deep", 50000, true, true, false, false, false},
    {"50k-moving-behaviors", 50000, true, false, true, false, false},
};

static struct {
    const row_t *row;
    wgf_actor_t stage;
    wgf_actor_t *actors, *cars;
    char (*names)[16]; /* the lookup row's names, made before the finding is timed */
    wgf_actor_t churned[CHURN];
    int churn_head, churn_count, spawned;
    size_t heap_made;
    int frame;
    double cost;
    double find_name, find_path, find_component;
    double finding; /* the finds' time inside the frames measured, taken out of their cost */
    int events[3 * 4096];
} bench;

/* The heap in use: its chunks and the ones mapped on their own (a big pool's). */
static size_t heap(void)
{
    const struct mallinfo2 info = mallinfo2();
    return info.uordblks + info.hblkhd;
}

/* ---- the actor calls the rows make: all of the API the benchmark leans on ---------- */

static wgf_actor_t make(wgf_actor_t parent, bool moving, bool behavior, const char *name)
{
    const wgf_actor_t actor = wgf_actor_create();
    wgf_actor_set_parent(actor, parent);
    wgf_actor_set_position(actor, (float)(rand() % 800), (float)(rand() % 600), 0);
    if (name != NULL) wgf_actor_set_name(actor, name);
    if (moving) {
        wgf_actor_add_component(actor, WGF_COMPONENT_MOTION);
        wgf_actor_add_component(actor, WGF_COMPONENT_BOUNDS); /* wrapping in 800 by 600 */
        wgf_motion_set_velocity(actor, (float)(rand() % 200 - 100), (float)(rand() % 200 - 100), 0);
    }
    if (behavior) wgf_actor_add_behavior(actor, "Bench");
    return actor;
}

static void destroy(wgf_actor_t actor)
{
    wgf_actor_destroy(actor, WGF_ACTOR_DESTROY_CHILDREN);
}

static wgf_actor_t find_name(const char *name)
{
    return wgf_stage2d_find(bench.stage, name);
}

static wgf_actor_t find_path(wgf_actor_t from, const char *path)
{
    return wgf_actor_find(from, path);
}

static int find_component(wgf_actor_t *out, int count)
{
    return wgf_ecs_find_component(WGF_COMPONENT_MOTION, out, count);
}

/* ---- the rows --------------------------------------------------------------------- */

static void init(void *user)
{
    const row_t *row = bench.row;
    char name[32];
    wgf_actor_t first;
    int i;
    (void)user;
    srand(1);
    bench.stage = wgf_stage2d_create();
    first = make(bench.stage, true, true, NULL); /* the ecs's world made: a fixed cost, not an actor's */
    destroy(first);
    bench.actors = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * (size_t)row->count);
    bench.cars = (wgf_actor_t *)malloc(sizeof(wgf_actor_t) * CARS);
    if (bench.actors == NULL || bench.cars == NULL) {
        fprintf(stderr, "bench_actors: out of memory\n");
        wgf_app_quit();
        return;
    }
    bench.heap_made = heap();
    for (i = 0; i < row->count; i++) {
        const wgf_actor_t parent = row->deep && i % DEEP != 0 ? bench.actors[i - 1] : bench.stage;
        if (row->lookup) snprintf(name, sizeof(name), "a%d", i);
        bench.actors[i] = make(parent, row->moving, row->behaviors, row->lookup ? name : NULL);
    }
    bench.heap_made = heap() - bench.heap_made;
    bench.names = row->lookup ? (char (*)[16])malloc(16 * 1000) : NULL;
    for (i = 0; bench.names != NULL && i < 1000; i++) snprintf(bench.names[i], 16, "a%d", (i * 7919) % row->count);
    for (i = 0; row->lookup && i < CARS; i++) {
        wgf_actor_t wheel;
        snprintf(name, sizeof(name), "car%d", i);
        bench.cars[i] = make(bench.stage, false, false, name);
        wheel = make(bench.cars[i], false, false, "wheel_rl");
        make(wheel, false, false, "smoke");
    }
}

static void churn(void)
{
    const int due = (bench.frame + 1) * CHURN / 60; /* spawned by the end of this frame */
    while (bench.spawned < due) {
        if (bench.churn_count == CHURN) { /* the oldest, destroyed */
            destroy(bench.churned[bench.churn_head]);
            bench.churn_head = (bench.churn_head + 1) % CHURN;
            bench.churn_count--;
        }
        bench.churned[(bench.churn_head + bench.churn_count) % CHURN] = make(bench.stage, true, true, NULL);
        bench.churn_count++;
        bench.spawned++;
    }
}

static void lookups(void)
{
    const double began = wgf_time_get_seconds();
    double start;
    volatile wgf_actor_t found = 0;
    int i;
    start = wgf_time_get_seconds();
    for (i = 0; i < 1000; i++) found = find_name(bench.names[i]);
    bench.find_name += wgf_time_get_seconds() - start;
    start = wgf_time_get_seconds();
    for (i = 0; i < 1000; i++) found = find_path(bench.cars[(i * 7919) % CARS], "wheel_rl/smoke");
    bench.find_path += wgf_time_get_seconds() - start;
    start = wgf_time_get_seconds();
    found = (wgf_actor_t)find_component(bench.actors, bench.row->count);
    bench.find_component += wgf_time_get_seconds() - start;
    if (bench.frame < WARM + MEASURED) bench.finding += wgf_time_get_seconds() - began; /* in a cost counted */
    (void)found;
}

static void frame(void *user)
{
    const row_t *row = bench.row;
    (void)user;
    while (wgf_ecs_take_events(bench.events, (int)(sizeof(bench.events) / sizeof(bench.events[0]))) > 0) {
        /* a binding takes them each frame: so does this */
    }
    if (bench.frame > WARM) bench.cost += wgf_loop_get_frame_cost(); /* the frame before this one's */
    if (row->churn) churn();
    if (row->lookup && bench.frame >= WARM) lookups();
    wgf_stage2d_draw(bench.stage);
    if (++bench.frame == WARM + MEASURED + 1) {
        const double frames = MEASURED;
        printf("bench_actors: %s actors=%d bytes=%.1f ns=%.2f\n", row->name, row->count,
               (double)bench.heap_made / row->count, (bench.cost - bench.finding) / frames / row->count * 1e9);
        if (row->lookup) {
            printf("bench_actors: %s-name finds=1000 ns=%.1f\n", row->name, bench.find_name / (frames + 1) / 1000 * 1e9);
            printf("bench_actors: %s-path finds=1000 ns=%.1f\n", row->name, bench.find_path / (frames + 1) / 1000 * 1e9);
            printf("bench_actors: %s-component found=%d ns=%.2f\n", row->name, row->count,
                   bench.find_component / (frames + 1) / row->count * 1e9);
        }
        fflush(stdout);
        wgf_app_quit();
    }
}

int main(int argc, char **argv)
{
    size_t i;
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) printf("%s\n", rows[i].name);
        return 0;
    }
    for (i = 0; argc == 2 && i < sizeof(rows) / sizeof(rows[0]); i++) {
        if (strcmp(argv[1], rows[i].name) == 0) bench.row = &rows[i];
    }
    if (bench.row == NULL) {
        fprintf(stderr, "usage: bench_actors --list | <row>\n");
        return 2;
    }
    wgf_loop_set_tick_rate(60);
    return wgf_app_run(init, NULL, frame, NULL, NULL) ? 0 : 1;
}
