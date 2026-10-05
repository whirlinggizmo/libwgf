#include <stdio.h>
#include <string.h>

#include "wgf_core_handle_priv.h"
#include "wgf_handle.h"

/* The pool is private (the layers above core use it), so this tests it through its
 * private header, and the kind name through the public one. */

typedef struct item_t {
    int value;
} item_t;

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    wgf_core_priv_handle_pool_t textures, models, again;
    item_t *texture_items = NULL, *model_items = NULL, *again_items = NULL;
    wgf_handle_t a, b, c, model;
    uint16_t index = 0;
    int i;

    expect(strcmp(wgf_handle_get_kind_name(0), "") == 0, "0 has no kind");

    expect(wgf_core_priv_handle_pool_init(&textures, WGF_CORE_PRIV_HANDLE_KIND_TEST_A, (void **)&texture_items,
                                          sizeof(item_t), 2, 8),
           "texture pool made");
    expect(wgf_core_priv_handle_pool_init(&models, WGF_CORE_PRIV_HANDLE_KIND_TEST_B, (void **)&model_items,
                                          sizeof(item_t), 2, 8),
           "model pool made");

    a = wgf_core_priv_handle_pool_alloc(&textures);
    b = wgf_core_priv_handle_pool_alloc(&textures);
    model = wgf_core_priv_handle_pool_alloc(&models);
    expect(a != 0 && b != 0 && a != b, "allocs give distinct non-zero handles");
    expect(strcmp(wgf_handle_get_kind_name(a), "test.a") == 0, "a handle names its kind");
    expect(strcmp(wgf_handle_get_kind_name(model), "test.b") == 0, "and another kind its own");
    expect(strcmp(wgf_handle_get_kind_name(WGF_CORE_PRIV_HANDLE_MAKE(WGF_CORE_PRIV_HANDLE_KIND_TEXTURE, 1, 1)),
                  "gfx.texture") == 0,
           "every kind libwgf has is named, whether or not its pool exists yet");
    expect(strcmp(wgf_handle_get_kind_name(WGF_CORE_PRIV_HANDLE_MAKE(39, 1, 1)), "") == 0,
           "a number no kind has has no name");

    expect(wgf_core_priv_handle_pool_resolve(&textures, a, &index) && index > 0, "a resolves");
    texture_items[index].value = 42;
    expect(!wgf_core_priv_handle_pool_resolve(&models, a, NULL), "a texture doesn't resolve as a model");
    expect(!wgf_core_priv_handle_pool_resolve(&textures, model, NULL), "a model doesn't resolve as a texture");

    expect(wgf_core_priv_handle_pool_free(&textures, a), "a freed");
    expect(!wgf_core_priv_handle_pool_resolve(&textures, a, NULL), "a stale after free");
    expect(!wgf_core_priv_handle_pool_free(&textures, a), "a can't be freed twice");
    {
        /* while a's slot is free, even the handle its next item will get doesn't resolve:
           the stored generation carries the free bit, which no handle has */
        const uint16_t slot = WGF_CORE_PRIV_HANDLE_INDEX(a);
        const wgf_handle_t next = WGF_CORE_PRIV_HANDLE_MAKE(WGF_CORE_PRIV_HANDLE_KIND(a), slot,
                                                            WGF_CORE_PRIV_HANDLE_GENERATION(a) + 1u);
        expect((textures.generations[slot] & WGF_CORE_PRIV_HANDLE_FREE_BIT) != 0 &&
                   (textures.generations[slot] & WGF_CORE_PRIV_HANDLE_GENERATION_MASK) ==
                       WGF_CORE_PRIV_HANDLE_GENERATION(next),
               "a free slot: its next generation, and the free bit");
        expect(!wgf_core_priv_handle_pool_resolve(&textures, next, NULL), "a free slot resolves no handle");
    }
    expect(strcmp(wgf_handle_get_kind_name(a), "test.a") == 0, "a stale handle still names its kind");

    /* grows past its initial 2 slots, up to max 8 (7 usable, slot 0 reserved) */
    for (i = 0; i < 6; i++) {
        c = wgf_core_priv_handle_pool_alloc(&textures);
        expect(c != 0, "alloc while growing");
    }
    expect(textures.capacity == 8, "grown to max");
    expect(wgf_core_priv_handle_pool_alloc(&textures) == 0, "full at max");
    expect(wgf_core_priv_handle_pool_resolve(&textures, b, &index), "b survives growth");
    expect(!wgf_core_priv_handle_pool_resolve(&textures, a, NULL), "a stays stale after its slot is reused");

    /* a kind keeps its number across a destroy and a new pool */
    wgf_core_priv_handle_pool_destroy(&textures);
    expect(texture_items == NULL, "destroy frees the items");
    expect(wgf_core_priv_handle_pool_init(&again, WGF_CORE_PRIV_HANDLE_KIND_TEST_A, (void **)&again_items,
                                          sizeof(item_t), 2, 8),
           "texture pool made again");
    expect(!wgf_core_priv_handle_pool_resolve(&again, b, NULL), "an old handle doesn't resolve in a new pool");
    for (uint16_t generation = 0; generation < 3; generation++) {
        /* a slot never used resolves no handle, generation 0 included: a binding or a JS
           guest can hand back any integer */
        expect(!wgf_core_priv_handle_pool_resolve(
                   &again, WGF_CORE_PRIV_HANDLE_MAKE(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, 1, generation), NULL),
               "a never-used slot resolves no handle, whatever its generation");
    }
    {
        /* a reset: no handle from before it matches one after it */
        const wgf_handle_t before = wgf_core_priv_handle_pool_alloc(&again);
        wgf_handle_t after;
        wgf_core_priv_handle_pool_reset(&again);
        expect(!wgf_core_priv_handle_pool_resolve(&again, before, NULL), "a handle from before a reset: stale");
        after = wgf_core_priv_handle_pool_alloc(&again);
        expect(after != 0 && after != before && !wgf_core_priv_handle_pool_resolve(&again, before, NULL),
               "the slot again, under another generation");
    }
    expect(!wgf_core_priv_handle_pool_init(&textures, (wgf_core_priv_handle_kind_t)39, (void **)&texture_items,
                                           sizeof(item_t), 2, 8),
           "a kind that isn't one is refused");

    wgf_core_priv_handle_pool_destroy(&models);
    wgf_core_priv_handle_pool_destroy(&again);
    return failures == 0 ? 0 : 1;
}
