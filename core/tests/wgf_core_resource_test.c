#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_resource.h"

/* The resource core with a stand-in kind: a "note" is a file's text, read on a worker
 * and kept on the main thread, as wgrender's resource core does its textures. A note's
 * ".alt" path is mapped to ".txt", as a texture's .ktx is to its variant. Files land
 * under resource_test_root/. */

typedef struct note_t {
    wgf_core_priv_resource_t resource;
    char *text;
    int defaults; /* set by the kind's init */
} note_t;

static int failures, freed;
static wgf_core_priv_handle_pool_t note_pool;
static note_t *notes;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static note_t *note(wgf_handle_t handle)
{
    return (note_t *)wgf_core_priv_resource_get(handle);
}

static void *prepare(const char *path)
{
    unsigned char *data;
    int size;
    char *text;
    if (!wgf_core_priv_fs_read(path, &data, &size)) return NULL;
    text = (char *)malloc((size_t)size + 1);
    memcpy(text, data, (size_t)size);
    text[size] = '\0';
    wgf_core_priv_fs_read_free(data);
    return text;
}

static wgf_core_priv_load_step_t finish(void *data, wgf_handle_t resource)
{
    note_t *note_ptr = note(resource);
    if (note_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (strcmp((const char *)data, "bad") == 0) return WGF_CORE_PRIV_LOAD_FAILED;
    note_ptr->text = (char *)data;
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void discard(void *data)
{
    note_t *owner = NULL;
    for (uint16_t i = 1; i < note_pool.capacity && owner == NULL; i++) {
        if (note_pool.occupied[i] && notes[i].text == data) owner = &notes[i];
    }
    if (owner == NULL) free(data); /* finished: the note's now */
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"note", prepare, finish, discard, fail, NULL};

static void map(const char *path, char *out, size_t size)
{
    const size_t n = strlen(path);
    if (n > 4 && strcmp(path + n - 4, ".alt") == 0) snprintf(out, size, "%.*s.txt", (int)(n - 4), path);
    else snprintf(out, size, "%s", path);
}

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void init(void *record)
{
    ((note_t *)record)->defaults = 7;
}

/* The kind's lock, as audio's mixer lock: taken around a record's making and freeing,
 * never twice at once. */
static int locks, held, freed_unlocked, nested;

static void lock_notes(void)
{
    nested += held;
    held = 1;
    locks++;
}

static void unlock_notes(void)
{
    held = 0;
}

static void free_note(wgf_handle_t resource, void *record)
{
    (void)resource;
    free(((note_t *)record)->text);
    freed++;
    freed_unlocked += !held;
}

static const wgf_core_priv_resource_kind_t kind = {.create = "note_create",
                                                   .map = map,
                                                   .loader = loader_of,
                                                   .init = init,
                                                   .free = free_note,
                                                   .lock = lock_notes,
                                                   .unlock = unlock_notes};

static void settle(void)
{
    for (int i = 0; i < 100000 && wgf_core_priv_load_get_pending_count() > 0; i++) wgf_core_priv_update();
}

int main(void)
{
    wgf_handle_t a, again, mapped, missing, bad, made, kept;

    wgf_core_priv_init();
    wgf_core_priv_fs_set_root("resource_test_root");
    expect(wgf_resource_get_load_budget() == 4.0f, "the load budget: 4 ms by default");
    expect(wgf_resource_set_load_budget(10.0f) && wgf_resource_get_load_budget() == 10.0f, "set");
    expect(!wgf_resource_set_load_budget(-1.0f) && wgf_resource_get_load_budget() == 10.0f,
           "less than 0: refused, nothing changed");
    expect(!wgf_resource_set_load_budget(NAN) && wgf_resource_get_load_budget() == 10.0f, "not finite: refused");
    expect(wgf_resource_set_load_budget(0.0f) && wgf_resource_get_load_budget() == 0.0f, "0: one step a frame");
    wgf_resource_set_load_budget(4.0f);
    wgf_core_priv_fs_write("notes/a.txt", (const unsigned char *)"alpha", 5);
    wgf_core_priv_fs_write("notes/bad.txt", (const unsigned char *)"bad", 3);
    wgf_core_priv_handle_pool_init(&note_pool, WGF_CORE_PRIV_HANDLE_KIND_TEST_A, (void **)&notes, sizeof(note_t), 4, 256);
    expect(wgf_core_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/a.txt") == 0,
           "a kind not registered: none");
    wgf_core_priv_resource_register(&note_pool, &kind);

    a = wgf_core_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/a.txt");
    expect(wgf_resource_get_status(a) == WGF_RESOURCE_STATUS_PENDING && note(a)->defaults == 7,
           "PENDING at once, with the kind's defaults");
    again = wgf_core_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/./a.txt");
    expect(again == a && note(a)->resource.refs == 2, "the same path, however written: the same resource");
    expect(strcmp(wgf_resource_get_path(a), "") == 0, "no path until READY");
    mapped = wgf_core_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/a.alt");
    expect(mapped != a, "found by the path it was created from, not the file it reads");
    missing = wgf_core_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/missing.txt");
    bad = wgf_core_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/bad.txt");
    settle();
    expect(wgf_resource_get_status(a) == WGF_RESOURCE_STATUS_READY && strcmp(note(a)->text, "alpha") == 0, "READY");
    expect(strcmp(wgf_resource_get_path(a), "notes/a.txt") == 0, "its path once READY");
    expect(wgf_resource_get_status(mapped) == WGF_RESOURCE_STATUS_READY &&
               strcmp(wgf_resource_get_path(mapped), "notes/a.txt") == 0,
           "a mapped path: the file it read");
    expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED &&
               wgf_resource_get_status(bad) == WGF_RESOURCE_STATUS_FAILED,
           "a missing file, or one its loader refuses: FAILED");

    locks = 0;
    made = wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_TEST_A);
    expect(locks == 1 && !held, "making a record takes the kind's lock, and lets it go");
    expect(wgf_resource_get_status(made) == WGF_RESOURCE_STATUS_READY && strcmp(wgf_resource_get_path(made), "") == 0,
           "made from numbers: READY, no path");
    kept = wgf_core_priv_resource_add(WGF_CORE_PRIV_HANDLE_KIND_TEST_A);
    wgf_core_priv_resource_set_path(kept, "notes/kept.txt");
    expect(wgf_core_priv_resource_find(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/kept.txt") == kept &&
               strcmp(wgf_resource_get_path(kept), "notes/kept.txt") == 0,
           "one given a path is found by it (one more reference)");
    wgf_resource_release(kept);
    wgf_resource_release(kept);
    expect(wgf_resource_get_status(kept) == WGF_RESOURCE_STATUS_NONE, "both let go of: freed");
    expect(freed_unlocked == 0 && nested == 0 && !held, "a record is freed under the kind's lock, never taken twice");

    freed = 0;
    expect(wgf_resource_release(a) && wgf_resource_get_status(a) == WGF_RESOURCE_STATUS_READY && freed == 0,
           "a reference let go of: still held");
    expect(wgf_resource_release(a) && wgf_resource_get_status(a) == WGF_RESOURCE_STATUS_NONE && freed == 1,
           "the last: freed");
    expect(!wgf_resource_release(a) && !wgf_resource_release(0) && !wgf_resource_release(12345),
           "released already, 0, or not a resource: false");
    note(made)->resource.permanent = true;
    expect(wgf_resource_release(made) && wgf_resource_release(made) &&
               wgf_resource_get_status(made) == WGF_RESOURCE_STATUS_READY,
           "a built-in: never freed");

    {
        /* released while it loads: the load stops, and nothing is called back */
        const wgf_handle_t gone = wgf_core_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/a.txt");
        expect(wgf_resource_release(gone) && wgf_core_priv_load_get_pending_count() == 0, "its load cancelled");
    }

    freed = 0;
    wgf_core_priv_resource_unregister(&note_pool);
    expect(freed == 4 && wgf_resource_get_status(made) == WGF_RESOURCE_STATUS_NONE,
           "unregistered: every one freed, built-ins too");
    wgf_core_priv_handle_pool_destroy(&note_pool);
    wgf_core_priv_fs_rmdir("notes");
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
