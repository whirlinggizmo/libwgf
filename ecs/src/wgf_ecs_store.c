#include "wgf_ecs_store_priv.h"

#include <stdlib.h>
#include <string.h>

/* The store as plain sparse sets: each component an array of its data, packed, beside the
 * entities they are, and a sparse array from an entity's index to its place there. Set,
 * get, has, and remove are an index or two; a remove moves the last one into the hole. A
 * query walks its smallest set and asks the others about each entity in it. An entity is
 * an index (from 1) and a generation in the upper 32 bits, so a stale one is told apart.
 * Its worst case is a query whose smallest set is large and whose answer is small: it
 * walks the whole set (50,000 for 100 found: 300 ns an entity found, where flecs's tables
 * answered in 5). libwgf's queries are a component and the reference every entity has,
 * which walk the component's own set; a query of two sparse components would want an
 * index of its own first (docs/HISTORY.md, "flecs or sparse sets, measured"). */

typedef struct set_t {
    size_t size, stride;   /* stride: the size rounded up to its alignment; 0 for a tag */
    uint32_t *sparse;      /* by an entity's index: its place in the set, plus 1; 0 for none */
    uint32_t sparse_count; /* how many indices `sparse` covers */
    uint32_t *entities;    /* the set's entities' indices, packed */
    unsigned char *data;   /* and their data, packed alike */
    uint32_t count, capacity;
} set_t;

struct wgf_ecs_priv_query_t {
    uint32_t sets[WGF_ECS_PRIV_STORE_TERMS_MAX];
    int count;
    uint32_t walked; /* the walk's set, the smallest as it began, and where in it */
    uint32_t at;
};

static struct {
    bool ready;
    set_t *sets; /* by a component's id; [0] unused */
    uint32_t set_count, set_capacity;
    uint32_t *generations; /* by an entity's index; odd while it is alive */
    uint32_t entity_count, entity_capacity;
    uint32_t *free_indices;
    uint32_t free_count;
} store;

static uint32_t index_of(wgf_ecs_priv_id_t entity)
{
    return (uint32_t)entity;
}

static bool alive(wgf_ecs_priv_id_t entity)
{
    const uint32_t index = index_of(entity);
    return index != 0 && index < store.entity_count && (store.generations[index] & 1u) != 0 &&
           store.generations[index] == (uint32_t)(entity >> 32);
}

static set_t *set_of(wgf_ecs_priv_id_t component)
{
    return component != 0 && component < store.set_count ? &store.sets[component] : NULL;
}

/* Its place in `set`, or -1. */
static int64_t place(const set_t *set, uint32_t index)
{
    return index < set->sparse_count && set->sparse[index] != 0 ? (int64_t)set->sparse[index] - 1 : -1;
}

bool wgf_ecs_priv_store_start(void)
{
    if (store.ready) return true;
    store.sets = (set_t *)calloc(16, sizeof(set_t));
    store.generations = (uint32_t *)calloc(64, sizeof(uint32_t));
    store.free_indices = (uint32_t *)malloc(64 * sizeof(uint32_t));
    if (store.sets == NULL || store.generations == NULL || store.free_indices == NULL) {
        free(store.sets);
        free(store.generations);
        free(store.free_indices);
        memset(&store, 0, sizeof(store));
        return false;
    }
    store.set_count = 1;
    store.set_capacity = 16;
    store.entity_count = 1;
    store.entity_capacity = 64;
    store.ready = true;
    return true;
}

void wgf_ecs_priv_store_stop(void)
{
    uint32_t i;
    for (i = 1; i < store.set_count; i++) {
        free(store.sets[i].sparse);
        free(store.sets[i].entities);
        free(store.sets[i].data);
    }
    free(store.sets);
    free(store.generations);
    free(store.free_indices);
    memset(&store, 0, sizeof(store));
}

wgf_ecs_priv_id_t wgf_ecs_priv_store_component(size_t size, size_t alignment)
{
    set_t *set;
    if (store.set_count == store.set_capacity) {
        const uint32_t capacity = store.set_capacity * 2;
        set_t *grown = (set_t *)realloc(store.sets, sizeof(set_t) * capacity);
        if (grown == NULL) return 0;
        store.sets = grown;
        store.set_capacity = capacity;
    }
    set = &store.sets[store.set_count];
    memset(set, 0, sizeof(*set));
    set->size = size;
    if (alignment < 1) alignment = 1;
    set->stride = size == 0 ? 0 : (size + alignment - 1) / alignment * alignment;
    return store.set_count++;
}

wgf_ecs_priv_id_t wgf_ecs_priv_store_new(void)
{
    uint32_t index;
    if (store.free_count > 0) {
        index = store.free_indices[--store.free_count];
    } else {
        if (store.entity_count == store.entity_capacity) {
            const uint32_t capacity = store.entity_capacity * 2;
            uint32_t *generations = (uint32_t *)realloc(store.generations, sizeof(uint32_t) * capacity);
            uint32_t *free_indices;
            if (generations == NULL) return 0;
            store.generations = generations;
            free_indices = (uint32_t *)realloc(store.free_indices, sizeof(uint32_t) * capacity);
            if (free_indices == NULL) return 0;
            store.free_indices = free_indices;
            memset(&store.generations[store.entity_count], 0, sizeof(uint32_t) * (capacity - store.entity_count));
            store.entity_capacity = capacity;
        }
        index = store.entity_count++;
    }
    store.generations[index]++; /* odd: alive */
    return ((wgf_ecs_priv_id_t)store.generations[index] << 32) | index;
}

static void take_out(set_t *set, uint32_t index)
{
    const int64_t at = place(set, index);
    uint32_t last;
    if (at < 0) return;
    last = --set->count;
    if ((uint32_t)at != last) { /* the last one into the hole */
        set->entities[at] = set->entities[last];
        if (set->stride != 0) memcpy(set->data + set->stride * at, set->data + set->stride * last, set->stride);
        set->sparse[set->entities[at]] = (uint32_t)at + 1;
    }
    set->sparse[index] = 0;
}

void wgf_ecs_priv_store_delete(wgf_ecs_priv_id_t entity)
{
    const uint32_t index = index_of(entity);
    uint32_t i;
    if (!alive(entity)) return;
    for (i = 1; i < store.set_count; i++) take_out(&store.sets[i], index);
    store.generations[index]++; /* even: gone */
    store.free_indices[store.free_count++] = index;
}

void *wgf_ecs_priv_store_set(wgf_ecs_priv_id_t entity, wgf_ecs_priv_id_t component, const void *data)
{
    set_t *set = set_of(component);
    const uint32_t index = index_of(entity);
    int64_t at;
    if (set == NULL || !alive(entity)) return NULL;
    at = place(set, index);
    if (at < 0) {
        if (index >= set->sparse_count) {
            uint32_t count = set->sparse_count > 0 ? set->sparse_count : 64;
            uint32_t *grown;
            while (count <= index) count *= 2;
            grown = (uint32_t *)realloc(set->sparse, sizeof(uint32_t) * count);
            if (grown == NULL) return NULL;
            memset(&grown[set->sparse_count], 0, sizeof(uint32_t) * (count - set->sparse_count));
            set->sparse = grown;
            set->sparse_count = count;
        }
        if (set->count == set->capacity) {
            const uint32_t capacity = set->capacity > 0 ? set->capacity * 2 : 16;
            uint32_t *entities = (uint32_t *)realloc(set->entities, sizeof(uint32_t) * capacity);
            if (entities == NULL) return NULL;
            set->entities = entities;
            if (set->stride != 0) {
                unsigned char *grown = (unsigned char *)realloc(set->data, set->stride * capacity);
                if (grown == NULL) return NULL;
                set->data = grown;
            }
            set->capacity = capacity;
        }
        at = set->count++;
        set->entities[at] = index;
        set->sparse[index] = (uint32_t)at + 1;
    }
    if (set->stride == 0) return NULL;
    if (data != NULL) {
        memcpy(set->data + set->stride * at, data, set->size);
    } else {
        memset(set->data + set->stride * at, 0, set->size);
    }
    return set->data + set->stride * at;
}

bool wgf_ecs_priv_store_has(wgf_ecs_priv_id_t entity, wgf_ecs_priv_id_t component)
{
    const set_t *set = set_of(component);
    return set != NULL && alive(entity) && place(set, index_of(entity)) >= 0;
}

void *wgf_ecs_priv_store_get(wgf_ecs_priv_id_t entity, wgf_ecs_priv_id_t component)
{
    set_t *set = set_of(component);
    int64_t at;
    if (set == NULL || set->stride == 0 || !alive(entity)) return NULL;
    at = place(set, index_of(entity));
    return at >= 0 ? set->data + set->stride * at : NULL;
}

void wgf_ecs_priv_store_remove(wgf_ecs_priv_id_t entity, wgf_ecs_priv_id_t component)
{
    set_t *set = set_of(component);
    if (set != NULL && alive(entity)) take_out(set, index_of(entity));
}

int wgf_ecs_priv_store_count(wgf_ecs_priv_id_t component)
{
    const set_t *set = set_of(component);
    return set != NULL ? (int)set->count : 0;
}

wgf_ecs_priv_query_t *wgf_ecs_priv_store_query(const wgf_ecs_priv_id_t *components, int count)
{
    wgf_ecs_priv_query_t *query;
    int i;
    if (count < 1 || count > WGF_ECS_PRIV_STORE_TERMS_MAX) return NULL;
    query = (wgf_ecs_priv_query_t *)calloc(1, sizeof(*query));
    if (query == NULL) return NULL;
    for (i = 0; i < count; i++) {
        if (set_of(components[i]) == NULL) {
            free(query);
            return NULL;
        }
        query->sets[i] = (uint32_t)components[i];
    }
    query->count = count;
    return query;
}

void wgf_ecs_priv_store_query_free(wgf_ecs_priv_query_t *query)
{
    free(query);
}

void wgf_ecs_priv_store_walk(wgf_ecs_priv_query_t *query)
{
    int i;
    query->walked = query->sets[0];
    for (i = 1; i < query->count; i++) {
        if (store.sets[query->sets[i]].count < store.sets[query->walked].count) query->walked = query->sets[i];
    }
    query->at = 0;
}

bool wgf_ecs_priv_store_next(wgf_ecs_priv_query_t *query, void **fields, wgf_ecs_priv_id_t *entity)
{
    const set_t *walked = &store.sets[query->walked];
    while (query->at < walked->count) {
        const uint32_t index = walked->entities[query->at++];
        int i;
        for (i = 0; i < query->count; i++) {
            const set_t *set = &store.sets[query->sets[i]];
            const int64_t at = place(set, index);
            if (at < 0) break;
            fields[i] = set->stride != 0 ? set->data + set->stride * at : NULL;
        }
        if (i == query->count) {
            *entity = ((wgf_ecs_priv_id_t)store.generations[index] << 32) | index;
            return true;
        }
    }
    return false;
}
