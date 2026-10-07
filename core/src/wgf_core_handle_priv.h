#ifndef WGF_CORE_HANDLE_PRIV_H
#define WGF_CORE_HANDLE_PRIV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wgf_handle.h"

/* 32-bit handle: [ kind: 6 @ 26 ][ generation: 10 @ 16 ][ index: 16 @ 0 ]
 *
 * kind        1..63 (0: none), one per pool
 * generation  1..1023 in a handle (0 never)
 * index       1..65535, the slot (0 never: a zero handle is never valid)
 *
 * A pool stores each slot's generation in 16 bits (`generations` below): the low 10
 * are the generation, bit 15 (WGF_CORE_PRIV_HANDLE_FREE_BIT) is set while the slot is
 * free -- a slot never used included, its generation 0 -- and bits 10..14 are always 0. The free bit is the pool's, never a handle's, so
 * the handle's layout and counts are as they were: 63 kinds, 1023 generations, 65535
 * slots a pool. */
#define WGF_CORE_PRIV_HANDLE_KIND_BITS 6u
#define WGF_CORE_PRIV_HANDLE_GENERATION_BITS 10u
#define WGF_CORE_PRIV_HANDLE_INDEX_BITS 16u

#define WGF_CORE_PRIV_HANDLE_INDEX_MASK ((1u << WGF_CORE_PRIV_HANDLE_INDEX_BITS) - 1u)
#define WGF_CORE_PRIV_HANDLE_GENERATION_MASK ((1u << WGF_CORE_PRIV_HANDLE_GENERATION_BITS) - 1u)
#define WGF_CORE_PRIV_HANDLE_KIND_MASK ((1u << WGF_CORE_PRIV_HANDLE_KIND_BITS) - 1u)

#define WGF_CORE_PRIV_HANDLE_INDEX_SHIFT 0u
#define WGF_CORE_PRIV_HANDLE_GENERATION_SHIFT WGF_CORE_PRIV_HANDLE_INDEX_BITS
#define WGF_CORE_PRIV_HANDLE_KIND_SHIFT (WGF_CORE_PRIV_HANDLE_INDEX_BITS + WGF_CORE_PRIV_HANDLE_GENERATION_BITS)

#define WGF_CORE_PRIV_HANDLE_MAKE(kind, index, generation)                                                     \
    ((wgf_handle_t)((((uint32_t)(kind) & WGF_CORE_PRIV_HANDLE_KIND_MASK) << WGF_CORE_PRIV_HANDLE_KIND_SHIFT) | \
                        (((uint32_t)(generation) & WGF_CORE_PRIV_HANDLE_GENERATION_MASK)                         \
                         << WGF_CORE_PRIV_HANDLE_GENERATION_SHIFT) |                                             \
                        (((uint32_t)(index) & WGF_CORE_PRIV_HANDLE_INDEX_MASK) << WGF_CORE_PRIV_HANDLE_INDEX_SHIFT)))

#define WGF_CORE_PRIV_HANDLE_INDEX(handle) \
    ((uint16_t)(((handle) >> WGF_CORE_PRIV_HANDLE_INDEX_SHIFT) & WGF_CORE_PRIV_HANDLE_INDEX_MASK))
#define WGF_CORE_PRIV_HANDLE_GENERATION(handle) \
    ((uint16_t)(((handle) >> WGF_CORE_PRIV_HANDLE_GENERATION_SHIFT) & WGF_CORE_PRIV_HANDLE_GENERATION_MASK))
#define WGF_CORE_PRIV_HANDLE_KIND(handle) \
    ((uint8_t)(((handle) >> WGF_CORE_PRIV_HANDLE_KIND_SHIFT) & WGF_CORE_PRIV_HANDLE_KIND_MASK))

/* Every kind of handle libwgf has, one list for the whole library, as wgrender's
 * wgr_handle.h did: a kind keeps its number in every program and run. The numbers are
 * private; wgf_handle_get_kind_name gives each one's name, "<layer>.<type>".
 * Kinds fit in 6 bits, and 0 is "none". Each layer has a block of numbers. */
typedef enum wgf_core_priv_handle_kind_t {
    WGF_CORE_PRIV_HANDLE_KIND_NONE = 0,
    /* core: 1.. */
    WGF_CORE_PRIV_HANDLE_KIND_FS_TASK = 1,      /* "core.fs_task" */
    WGF_CORE_PRIV_HANDLE_KIND_LOAD_REQUEST = 2, /* "core.load_request" */
    /* gfx: 16.. */
    WGF_CORE_PRIV_HANDLE_KIND_NODE = 16,    /* "gfx.node": every node, of every type */
    WGF_CORE_PRIV_HANDLE_KIND_TEXTURE = 17, /* "gfx.texture" */
    WGF_CORE_PRIV_HANDLE_KIND_FONT = 18,    /* "gfx.font" */
    WGF_CORE_PRIV_HANDLE_KIND_MESH = 19,    /* "gfx.mesh" */
    WGF_CORE_PRIV_HANDLE_KIND_MATERIAL = 20, /* "gfx.material" */
    /* ecs: 32.. */
    WGF_CORE_PRIV_HANDLE_KIND_ENTITY = 32, /* "ecs.entity" */
    WGF_CORE_PRIV_HANDLE_KIND_SCENE = 33,  /* "ecs.scene": a scene file, a resource */
    /* audio: 40.. */
    WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND = 40,          /* "audio.sound": decoded whole */
    WGF_CORE_PRIV_HANDLE_KIND_AUDIO_SOUND_STREAMED = 41, /* "audio.sound_streamed": a kind of its own, so a path
                                                           made both ways is two sounds */
    WGF_CORE_PRIV_HANDLE_KIND_AUDIO_VOICE = 42,          /* "audio.voice" */
    /* asset: 48.. */
    WGF_CORE_PRIV_HANDLE_KIND_ASSET_TASK = 48, /* "asset.task": a load's file */
    /* tests: 62 and 63 */
    WGF_CORE_PRIV_HANDLE_KIND_TEST_A = 62, /* "test.a" */
    WGF_CORE_PRIV_HANDLE_KIND_TEST_B = 63  /* "test.b" */
} wgf_core_priv_handle_kind_t;

/* Slots a pool can have at most: indices are 16 bits, and 0 is never a slot. */
#define WGF_CORE_PRIV_HANDLE_POOL_MAX_SLOTS 65535u

/* Handles into a pool of slots, one pool per kind, which starts small and doubles
 * as needed. Index 0 is reserved, so a zero handle is never valid. Freeing a slot
 * bumps its generation, so handles to what it held stop resolving. Freed slots
 * are reused oldest first: churn spreads over all of them, and a stale handle only
 * resolves again after its slot's generation wraps (1023 reuses of that slot). */
typedef struct wgf_core_priv_handle_pool_t {
    uint8_t kind;
    uint16_t capacity; /* slots now, index 0 included */
    uint16_t max;      /* capacity can grow to this */
    uint16_t next_index;

    uint16_t *free_indices; /* ring of `capacity`: every free slot, the oldest first */
    uint16_t free_head;
    uint16_t free_count;

    uint16_t *generations;  /* per slot: its generation (low 10 bits), | FREE_BIT while free (never used: FREE_BIT alone) */
    unsigned char *occupied; /* per slot: in use. Walks read it; resolving doesn't need it */

    /* the owner's item array, grown and zeroed with the slots */
    void **items;
    size_t item_size;
} wgf_core_priv_handle_pool_t;

/* A pool of handles of `kind`. It allocates its bookkeeping and the owner's item
 * array (*items, item_size bytes per slot), starting at `initial` slots and doubling
 * when full, up to `max` (at most WGF_CORE_PRIV_HANDLE_POOL_MAX_SLOTS). New slots are
 * zeroed. Growing moves *items, so a pointer into it must not be held across an
 * alloc. False, and nothing allocated, for a kind that isn't one, or when memory
 * runs out. wgf_core_priv_handle_pool_destroy frees it all. */
bool wgf_core_priv_handle_pool_init(wgf_core_priv_handle_pool_t *pool, wgf_core_priv_handle_kind_t kind, void **items,
                                   size_t item_size, uint16_t initial, uint16_t max);
void wgf_core_priv_handle_pool_destroy(wgf_core_priv_handle_pool_t *pool);
void wgf_core_priv_handle_pool_reset(wgf_core_priv_handle_pool_t *pool);

wgf_handle_t wgf_core_priv_handle_pool_alloc(wgf_core_priv_handle_pool_t *pool);
bool wgf_core_priv_handle_pool_free(wgf_core_priv_handle_pool_t *pool, wgf_handle_t handle);

/* A free slot's generation carries this bit, which a handle's (10 bits) never has, so
 * a generation that matches is a live slot's: resolving reads one array, not two. */
#define WGF_CORE_PRIV_HANDLE_FREE_BIT 0x8000u

/* `handle`'s slot in `pool` when it is live; false for 0, another kind, a slot out of
 * range or free (never used included), or an old generation. `pool` must not be NULL;
 * a pool not set up (or destroyed) has no slots and resolves nothing. Inline: every
 * handle call resolves one, and the library is built without link-time optimization. */
static inline bool wgf_core_priv_handle_pool_resolve(const wgf_core_priv_handle_pool_t *pool, wgf_handle_t handle,
                                                     uint16_t *index_out)
{
    const uint16_t index = WGF_CORE_PRIV_HANDLE_INDEX(handle);
    if (WGF_CORE_PRIV_HANDLE_KIND(handle) != pool->kind || index >= pool->capacity ||
        pool->generations[index] != WGF_CORE_PRIV_HANDLE_GENERATION(handle) || index == 0) {
        return false;
    }
    if (index_out != NULL) *index_out = index;
    return true;
}
wgf_handle_t wgf_core_priv_handle_pool_handle_from_index(const wgf_core_priv_handle_pool_t *pool,
                                                            uint16_t index);

#endif
