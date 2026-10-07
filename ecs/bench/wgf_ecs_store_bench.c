#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wgf_ecs_store_priv.h"

/* The ecs's store measured on its own (ROADMAP, step 3b): each storage's worst case and
 * the ordinary ones, the same program built on each (WGF_ECS_STORE). Nanoseconds, the
 * best of 5 rounds:
 *
 *   intersect   a query of A and B where 50,000 have A, 50,000 have B, and 100 both: a
 *               walk, per entity found (sparse sets' worst: they walk a set of 50,000)
 *   toggle      10,000 entities each given a component and losing it again, as many
 *               actors adding and removing one each tick would: per add and remove
 *               (archetype tables' worst: each moves the entity between tables twice)
 *   walk        a query of 2 over 50,000 entities that all have both, per entity
 *   walk-mixed  the same over 50,000 of which half have a third component the query
 *               doesn't name (two tables for flecs), per entity
 *   spawn       an entity made with 3 components, and deleted: per entity
 *   get         a component read by entity, 50,000 in a shuffled order, per read */

#define N 50000
#define ROUNDS 5

typedef struct data_t {
    float a[4];
} data_t;

static double now(void)
{
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static volatile float sink;

static void report(const char *name, double best, double per)
{
    printf("store_bench: %s ns=%.2f\n", name, best / per * 1e9);
}

static void intersect(void)
{
    wgf_ecs_priv_id_t a = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    wgf_ecs_priv_id_t b = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    const wgf_ecs_priv_id_t terms[2] = {a, b};
    wgf_ecs_priv_query_t *query;
    wgf_ecs_priv_id_t entity, *all = (wgf_ecs_priv_id_t *)malloc(sizeof(wgf_ecs_priv_id_t) * 2 * N);
    double best = 1e9;
    int i, r, found = 0;
    for (i = 0; i < 2 * N - 100; i++) {
        all[i] = wgf_ecs_priv_store_new();
        wgf_ecs_priv_store_set(all[i], i < N ? a : b, NULL);
    }
    for (; i < 2 * N; i++) { /* 100 with both */
        all[i] = wgf_ecs_priv_store_new();
        wgf_ecs_priv_store_set(all[i], a, NULL);
        wgf_ecs_priv_store_set(all[i], b, NULL);
    }
    query = wgf_ecs_priv_store_query(terms, 2);
    for (r = 0; r < ROUNDS; r++) {
        void *fields[2];
        const double start = now();
        found = 0;
        wgf_ecs_priv_store_walk(query);
        while (wgf_ecs_priv_store_next(query, fields, &entity)) {
            sink += ((data_t *)fields[0])->a[0];
            found++;
        }
        if (now() - start < best) best = now() - start;
    }
    if (found != 100) printf("store_bench: intersect found %d, not 100\n", found);
    report("intersect", best, found);
    wgf_ecs_priv_store_query_free(query);
    for (i = 0; i < 2 * N; i++) wgf_ecs_priv_store_delete(all[i]);
    free(all);
}

static void toggle(void)
{
    const int count = 10000;
    wgf_ecs_priv_id_t ref = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    wgf_ecs_priv_id_t motion = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    wgf_ecs_priv_id_t extra = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    wgf_ecs_priv_id_t *all = (wgf_ecs_priv_id_t *)malloc(sizeof(wgf_ecs_priv_id_t) * count);
    double best = 1e9;
    int i, r;
    for (i = 0; i < count; i++) {
        all[i] = wgf_ecs_priv_store_new();
        wgf_ecs_priv_store_set(all[i], ref, NULL);
        wgf_ecs_priv_store_set(all[i], motion, NULL);
    }
    for (r = 0; r < ROUNDS; r++) {
        const double start = now();
        for (i = 0; i < count; i++) wgf_ecs_priv_store_set(all[i], extra, NULL);
        for (i = 0; i < count; i++) wgf_ecs_priv_store_remove(all[i], extra);
        if (now() - start < best) best = now() - start;
    }
    report("toggle", best, count);
    for (i = 0; i < count; i++) wgf_ecs_priv_store_delete(all[i]);
    free(all);
}

static void walk(const char *name, bool mixed)
{
    wgf_ecs_priv_id_t a = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    wgf_ecs_priv_id_t b = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    wgf_ecs_priv_id_t c = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    const wgf_ecs_priv_id_t terms[2] = {a, b};
    wgf_ecs_priv_query_t *query;
    wgf_ecs_priv_id_t entity, *all = (wgf_ecs_priv_id_t *)malloc(sizeof(wgf_ecs_priv_id_t) * N);
    double best = 1e9;
    int i, r;
    for (i = 0; i < N; i++) {
        all[i] = wgf_ecs_priv_store_new();
        wgf_ecs_priv_store_set(all[i], a, NULL);
        wgf_ecs_priv_store_set(all[i], b, NULL);
        if (mixed && i % 2 == 0) wgf_ecs_priv_store_set(all[i], c, NULL);
    }
    query = wgf_ecs_priv_store_query(terms, 2);
    for (r = 0; r < ROUNDS; r++) {
        void *fields[2];
        const double start = now();
        wgf_ecs_priv_store_walk(query);
        while (wgf_ecs_priv_store_next(query, fields, &entity)) {
            data_t *x = (data_t *)fields[0];
            const data_t *y = (const data_t *)fields[1];
            x->a[0] += y->a[1] * 0.5f;
        }
        if (now() - start < best) best = now() - start;
    }
    report(name, best, N);
    wgf_ecs_priv_store_query_free(query);
    for (i = 0; i < N; i++) wgf_ecs_priv_store_delete(all[i]);
    free(all);
}

static void spawn_and_get(void)
{
    wgf_ecs_priv_id_t a = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    wgf_ecs_priv_id_t b = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    wgf_ecs_priv_id_t c = wgf_ecs_priv_store_component(sizeof(data_t), 4);
    wgf_ecs_priv_id_t *all = (wgf_ecs_priv_id_t *)malloc(sizeof(wgf_ecs_priv_id_t) * N);
    int *order = (int *)malloc(sizeof(int) * N);
    double best = 1e9, best_get = 1e9;
    int i, r;
    for (i = 0; i < N; i++) order[i] = i;
    srand(1);
    for (i = N - 1; i > 0; i--) {
        const int j = rand() % (i + 1), t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (r = 0; r < ROUNDS; r++) {
        double start = now();
        for (i = 0; i < N; i++) {
            all[i] = wgf_ecs_priv_store_new();
            wgf_ecs_priv_store_set(all[i], a, NULL);
            wgf_ecs_priv_store_set(all[i], b, NULL);
            wgf_ecs_priv_store_set(all[i], c, NULL);
        }
        for (i = 0; i < N; i++) wgf_ecs_priv_store_delete(all[i]);
        if (now() - start < best) best = now() - start;
    }
    for (i = 0; i < N; i++) {
        all[i] = wgf_ecs_priv_store_new();
        wgf_ecs_priv_store_set(all[i], a, NULL);
        wgf_ecs_priv_store_set(all[i], b, NULL);
    }
    for (r = 0; r < ROUNDS; r++) {
        const double start = now();
        for (i = 0; i < N; i++) sink += ((data_t *)wgf_ecs_priv_store_get(all[order[i]], b))->a[0];
        if (now() - start < best_get) best_get = now() - start;
    }
    report("spawn", best, N);
    report("get", best_get, N);
    for (i = 0; i < N; i++) wgf_ecs_priv_store_delete(all[i]);
    free(all);
    free(order);
}

int main(void)
{
    if (!wgf_ecs_priv_store_start()) return 1;
    intersect();
    toggle();
    walk("walk", false);
    walk("walk-mixed", true);
    spawn_and_get();
    wgf_ecs_priv_store_stop();
    return 0;
}
