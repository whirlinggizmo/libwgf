#include "wgf_core_handle_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_log.h"

/* Each kind's name, by number; "" for a number no kind has. */
static const char *const kind_names[WGF_CORE_PRIV_HANDLE_KIND_MASK + 1] = {
    [WGF_CORE_PRIV_HANDLE_KIND_FS_TASK] = "core.fs_task",
    [WGF_CORE_PRIV_HANDLE_KIND_LOAD_REQUEST] = "core.load_request",
    [WGF_CORE_PRIV_HANDLE_KIND_NODE] = "gfx.node",
    [WGF_CORE_PRIV_HANDLE_KIND_TEXTURE] = "gfx.texture",
    [WGF_CORE_PRIV_HANDLE_KIND_FONT] = "gfx.font",
    [WGF_CORE_PRIV_HANDLE_KIND_MESH] = "gfx.mesh",
    [WGF_CORE_PRIV_HANDLE_KIND_MATERIAL] = "gfx.material",
    [WGF_CORE_PRIV_HANDLE_KIND_ENTITY] = "ecs.entity",
    [WGF_CORE_PRIV_HANDLE_KIND_SCENE] = "ecs.scene",
    [WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND] = "audio.sound",
    [WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND_STREAMED] = "audio.sound_streamed",
    [WGF_CORE_PRIV_HANDLE_KIND_AUDIO_VOICE] = "audio.voice",
    [WGF_CORE_PRIV_HANDLE_KIND_ASSET_TASK] = "asset.task",
    [WGF_CORE_PRIV_HANDLE_KIND_TEST_A] = "test.a",
    [WGF_CORE_PRIV_HANDLE_KIND_TEST_B] = "test.b",
};

const char *wgf_handle_get_kind_name(wgf_handle_t handle)
{
    const char *name = handle != 0 ? kind_names[WGF_CORE_PRIV_HANDLE_KIND(handle)] : NULL;
    return name != NULL ? name : "";
}

/* The generation after `generation` (its free bit, if any, dropped), 1..1023, wrapping
 * past 0, which a handle never has. */
static uint16_t bump_slot_generation(uint16_t generation)
{
    uint16_t next_generation =
        (uint16_t)(((generation & WGF_CORE_PRIV_HANDLE_GENERATION_MASK) + 1u) & WGF_CORE_PRIV_HANDLE_GENERATION_MASK);
    if (next_generation == 0) next_generation = 1;
    return next_generation;
}

/* Grow the pool's slots, bookkeeping and items to `capacity`, zeroing the new
 * slots. Only called with no free slot left, so the free ring is empty. */
static bool grow(wgf_core_priv_handle_pool_t *pool, uint16_t capacity)
{
    const size_t old_capacity = pool->capacity;
    uint16_t *generations = realloc(pool->generations, sizeof(uint16_t) * capacity);
    unsigned char *occupied;
    uint16_t *free_indices;
    unsigned char *items;

    if (generations == NULL) return false;
    pool->generations = generations;
    occupied = realloc(pool->occupied, capacity);
    if (occupied == NULL) return false;
    pool->occupied = occupied;
    free_indices = realloc(pool->free_indices, sizeof(uint16_t) * capacity);
    if (free_indices == NULL) return false;
    pool->free_indices = free_indices;
    items = realloc(*pool->items, pool->item_size * capacity);
    if (items == NULL) return false;
    *pool->items = items;

    for (size_t i = old_capacity; i < capacity; i++) generations[i] = WGF_CORE_PRIV_HANDLE_FREE_BIT; /* free, never used */
    memset(occupied + old_capacity, 0, capacity - old_capacity);
    memset(items + pool->item_size * old_capacity, 0, pool->item_size * (capacity - old_capacity));
    pool->free_head = 0;
    pool->capacity = capacity;
    return true;
}

bool wgf_core_priv_handle_pool_init(wgf_core_priv_handle_pool_t *pool, wgf_core_priv_handle_kind_t kind, void **items,
                                   size_t item_size, uint16_t initial, uint16_t max)
{
    if (pool == NULL || items == NULL || item_size == 0) return false;
    if ((unsigned)kind > WGF_CORE_PRIV_HANDLE_KIND_MASK || kind_names[kind] == NULL) {
        wgf_log_error("wgf_core_handle: %d isn't a kind of handle", (int)kind);
        return false;
    }
    if (max < 2) max = 2;
    if (initial < 2) initial = 2; /* slot 0 plus one */
    if (initial > max) initial = max;
    *items = NULL;
    *pool = (wgf_core_priv_handle_pool_t){
        .kind = (uint8_t)kind,
        .max = max,
        .next_index = 1,
        .items = items,
        .item_size = item_size,
    };
    if (!grow(pool, initial)) {
        wgf_core_priv_handle_pool_destroy(pool);
        return false;
    }
    return true;
}

void wgf_core_priv_handle_pool_destroy(wgf_core_priv_handle_pool_t *pool)
{
    if (pool == NULL || pool->items == NULL) return;
    free(pool->generations);
    free(pool->occupied);
    free(pool->free_indices);
    free(*pool->items);
    *pool->items = NULL;
    *pool = (wgf_core_priv_handle_pool_t){0};
}

void wgf_core_priv_handle_pool_reset(wgf_core_priv_handle_pool_t *pool)
{
    if (pool == NULL) return;
    pool->next_index = 1; /* 0 is always reserved as invalid */
    pool->free_head = 0;
    pool->free_count = 0;
    /* every slot free; a used one moves to its next generation, so no handle from before
       the reset matches one after it */
    for (uint16_t i = 0; pool->generations != NULL && i < pool->capacity; i++) {
        const bool used = pool->occupied != NULL && pool->occupied[i];
        pool->generations[i] = (uint16_t)((used ? bump_slot_generation(pool->generations[i]) : pool->generations[i]) |
                                          WGF_CORE_PRIV_HANDLE_FREE_BIT);
    }
    if (pool->occupied != NULL) memset(pool->occupied, 0, pool->capacity);
    if (pool->items != NULL && *pool->items != NULL) memset(*pool->items, 0, pool->item_size * pool->capacity);
}

static uint16_t find_free_slot_index(wgf_core_priv_handle_pool_t *pool)
{
    uint16_t i;
    if (pool->free_count > 0) {
        const uint16_t index = pool->free_indices[pool->free_head];
        pool->free_head = (uint16_t)((pool->free_head + 1u) % pool->capacity);
        pool->free_count--;
        return index;
    }

    /* then slots never used */
    for (i = pool->next_index; i < pool->capacity; i++) {
        if (!pool->occupied[i]) {
            pool->next_index = (uint16_t)(i + 1u);
            return i;
        }
    }

    /* every slot in use: double the pool */
    if (pool->capacity < pool->max) {
        const uint16_t old_capacity = pool->capacity;
        const uint32_t doubled = (uint32_t)old_capacity * 2u;
        const uint16_t capacity = (uint16_t)(doubled < pool->max ? doubled : pool->max);
        if (!grow(pool, capacity)) {
            wgf_log_error("%s: out of memory growing to %u slots", kind_names[pool->kind], (unsigned)capacity);
            return 0;
        }
        wgf_log_debug("%s: grown to %u slots", kind_names[pool->kind], (unsigned)capacity);
        pool->next_index = (uint16_t)(old_capacity + 1u);
        return old_capacity;
    }
    return 0;
}


wgf_handle_t wgf_core_priv_handle_pool_alloc(wgf_core_priv_handle_pool_t *pool)
{
    uint16_t index;
    uint16_t generation;

    if (pool == NULL || pool->kind == 0) return 0;

    index = find_free_slot_index(pool);
    if (index == 0 || index >= pool->capacity) return 0;

    generation = (uint16_t)(pool->generations[index] & ~WGF_CORE_PRIV_HANDLE_FREE_BIT); /* live again */
    if (generation == 0) generation = 1;
    pool->generations[index] = generation;
    pool->occupied[index] = 1;
    return WGF_CORE_PRIV_HANDLE_MAKE(pool->kind, index, generation);
}

bool wgf_core_priv_handle_pool_free(wgf_core_priv_handle_pool_t *pool, wgf_handle_t handle)
{
    uint16_t index = 0;
    if (pool == NULL || !wgf_core_priv_handle_pool_resolve(pool, handle, &index)) return false;

    pool->occupied[index] = 0;
    /* the next handle's generation, and free until then: no handle matches it */
    pool->generations[index] = (uint16_t)(bump_slot_generation(pool->generations[index]) | WGF_CORE_PRIV_HANDLE_FREE_BIT);
    pool->free_indices[(pool->free_head + pool->free_count) % pool->capacity] = index;
    pool->free_count++;
    return true;
}

wgf_handle_t wgf_core_priv_handle_pool_handle_from_index(const wgf_core_priv_handle_pool_t *pool,
                                                            uint16_t index)
{
    if (pool == NULL || pool->kind == 0) return 0;
    if (index == 0 || index >= pool->capacity) return 0;
    if (!pool->occupied[index]) return 0;
    return WGF_CORE_PRIV_HANDLE_MAKE(pool->kind, index, pool->generations[index]);
}
