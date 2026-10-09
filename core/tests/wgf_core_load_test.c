#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_thread_priv.h"
#include "wgf_time.h"

/* The pipeline with a stand-in loader: a "thing" is a file's text, finished in as
 * many steps as its first digit says. Things are handles from a pool of the test's
 * own, as a layer's resources are. Runs with workers where there are threads, and
 * again with none. Files land under load_test_root/. */

typedef struct thing_t {
    int status; /* 0 pending, 1 ready, 2 failed */
    char text[64];
    int finish_calls;
} thing_t;

typedef struct prepared_t {
    char text[64];
    int steps_left;
} prepared_t;

static int failures;
static wgf_core_priv_handle_pool_t thing_pool;
static thing_t *things;
static wgf_core_priv_mutex_t count_lock; /* prepare runs on workers, discard on the main thread */
static int live_prepared;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static thing_t *thing(wgf_handle_t handle)
{
    uint16_t index;
    return wgf_core_priv_handle_pool_resolve(&thing_pool, handle, &index) ? &things[index] : NULL;
}

static void *prepare(const char *path)
{
    unsigned char *data;
    int size;
    prepared_t *prepared;
    if (!wgf_core_priv_fs_read(path, &data, &size)) return NULL;
    if (size == 0 || data[0] < '1' || data[0] > '9' || size >= 64) { /* "bad" files fail here */
        wgf_core_priv_fs_read_free(data);
        return NULL;
    }
    prepared = (prepared_t *)calloc(1, sizeof(prepared_t));
    memcpy(prepared->text, data, (size_t)size);
    prepared->steps_left = data[0] - '0';
    wgf_core_priv_fs_read_free(data);
    wgf_core_priv_mutex_lock(&count_lock);
    live_prepared++;
    wgf_core_priv_mutex_unlock(&count_lock);
    return prepared;
}

static wgf_core_priv_load_step_t finish(void *data, wgf_handle_t resource)
{
    prepared_t *prepared = (prepared_t *)data;
    thing_t *thing_ptr = thing(resource);
    if (thing_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    thing_ptr->finish_calls++;
    if (strstr(prepared->text, "unfinishable") != NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (strstr(prepared->text, "waits") != NULL && thing_ptr->finish_calls <= 2) return WGF_CORE_PRIV_LOAD_WAIT;
    if (--prepared->steps_left > 0) return WGF_CORE_PRIV_LOAD_MORE;
    snprintf(thing_ptr->text, sizeof(thing_ptr->text), "%s", prepared->text);
    thing_ptr->status = 1;
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void discard(void *data)
{
    wgf_core_priv_mutex_lock(&count_lock);
    live_prepared--;
    wgf_core_priv_mutex_unlock(&count_lock);
    free(data);
}

static void fail(wgf_handle_t resource)
{
    thing_t *thing_ptr = thing(resource);
    if (thing_ptr != NULL) thing_ptr->status = 2;
}

static const wgf_core_priv_loader_t loader = {"thing", prepare, finish, discard, fail, NULL, false};

static wgf_handle_t make(const char *path)
{
    wgf_handle_t handle = wgf_core_priv_handle_pool_alloc(&thing_pool);
    memset(thing(handle), 0, sizeof(thing_t));
    expect(wgf_core_priv_load_request(&loader, path, handle), "a request is held");
    return handle;
}

/* Update until nothing is pending, as a frame loop would; the updates it took. */
static int settle(void)
{
    int updates = 0;
    while (wgf_core_priv_load_get_pending_count() > 0 && updates < 100000) {
        wgf_core_priv_update();
        updates++;
    }
    expect(wgf_core_priv_load_get_pending_count() == 0, "everything settled");
    return updates;
}

static void run(const char *label, int workers)
{
    wgf_handle_t a, b, slow, missing, bad_path, bad, unfinishable, cancelled;
    int updates;

    printf("%s\n", label);
    wgf_core_priv_load_set_worker_count(workers);
    expect(wgf_core_priv_load_get_worker_count() == (workers < 0 ? wgf_core_priv_load_get_worker_count() : workers),
           "the worker count asked for");

    a = make("things/a.txt");
    b = make("things\\b.txt"); /* normalized like any request path */
    slow = make("things/slow.txt");
    missing = make("things/missing.txt");
    bad_path = make("../outside.txt");
    bad = make("things/bad.txt");
    unfinishable = make("things/unfinishable.txt");
    cancelled = make("things/cancelled.txt");
    expect(thing(missing)->status == 0 && thing(bad_path)->status == 0, "nothing fails inside the request");
    expect(wgf_core_priv_load_request(&loader, "things/cancelled.txt", cancelled), "a second request, same resource");
    expect(wgf_core_priv_load_cancel(cancelled), "cancel");
    expect(!wgf_core_priv_load_cancel(cancelled), "cancel again: none left, both requests went");
    expect(wgf_core_priv_load_log_pending() == wgf_core_priv_load_get_pending_count() &&
               wgf_core_priv_load_get_pending_count() == 7,
           "the pending ones named, one line each: the cancelled one isn't");

    wgf_core_priv_load_set_budget(0); /* one finish step per update */
    updates = settle();

    expect(thing(a)->status == 1 && strcmp(thing(a)->text, "1 alpha") == 0, "a loaded");
    expect(thing(b)->status == 1 && strcmp(thing(b)->text, "1 beta") == 0, "b loaded through a normalized path");
    expect(thing(slow)->status == 1 && thing(slow)->finish_calls == 5, "slow finished in 5 steps");
    expect(updates >= 5, "a budget of 0 is one step per update");
    expect(thing(missing)->status == 2, "a missing file fails");
    expect(thing(bad_path)->status == 2, "a path outside the root fails");
    expect(thing(bad)->status == 2, "a failed prepare fails");
    expect(thing(unfinishable)->status == 2, "a failed finish fails");
    expect(thing(cancelled)->status == 0 && thing(cancelled)->finish_calls == 0, "a cancelled request: no calls");
    expect(live_prepared == 0, "every prepared thing was discarded");

    /* cancelled while queued: the slot waits for its job, so new requests never
       outnumber the rings' room (wgrender sizes them to the requests) */
    {
        wgf_handle_t early = make("things/slow.txt"), more[40];
        int i, loaded = 0;
        wgf_core_priv_update(); /* queued, or with no workers already prepared */
        expect(wgf_core_priv_load_cancel(early), "cancel a queued request");
        expect(wgf_core_priv_load_get_pending_count() == 0, "a cancelled request isn't pending");
        for (i = 0; i < 40; i++) more[i] = make(i % 2 == 0 ? "things/a.txt" : "things/slow.txt");
        settle();
        for (i = 0; i < 40; i++) loaded += thing(more[i])->status == 1;
        expect(loaded == 40, "every request after it loaded");
        expect(thing(early)->status == 0, "the cancelled one never loaded");
        expect(live_prepared == 0, "its prepared data discarded when it came back");
        wgf_core_priv_handle_pool_free(&thing_pool, early);
        for (i = 0; i < 40; i++) wgf_core_priv_handle_pool_free(&thing_pool, more[i]);
    }
}

int main(void)
{
    wgf_core_priv_mutex_init(&count_lock);
    wgf_core_priv_init();
    wgf_core_priv_fs_set_root("load_test_root");
    wgf_core_priv_fs_write("things/a.txt", (const unsigned char *)"1 alpha", 7);
    wgf_core_priv_fs_write("things/b.txt", (const unsigned char *)"1 beta", 6);
    wgf_core_priv_fs_write("things/slow.txt", (const unsigned char *)"5 slow", 6);
    wgf_core_priv_fs_write("things/bad.txt", (const unsigned char *)"bad", 3);
    wgf_core_priv_fs_write("things/unfinishable.txt", (const unsigned char *)"1 unfinishable", 14);
    wgf_core_priv_fs_write("things/cancelled.txt", (const unsigned char *)"1 cancelled", 11);
    wgf_core_priv_fs_write("things/waits.txt", (const unsigned char *)"1 waits", 7);
    wgf_core_priv_handle_pool_init(&thing_pool, WGF_CORE_PRIV_HANDLE_KIND_TEST_A, (void **)&things, sizeof(thing_t), 16, 256);

    run("with the default workers", -1);
    run("with no workers: prepares on the main thread", 0);
    expect(wgf_core_priv_load_get_budget() == 0.0f, "budget reads back");

    /* a finish waiting on something outside the frame: asked once an update, however
       big the budget, and the others finish meanwhile */
    {
        wgf_handle_t waits, after;
        int updates = 0;
        wgf_core_priv_load_set_worker_count(0);
        wgf_core_priv_load_set_budget(1000.0f);
        waits = make("things/waits.txt");
        after = make("things/a.txt");
        while (thing(after)->status == 0 && updates < 100) {
            wgf_core_priv_update();
            updates++;
        }
        expect(thing(after)->status == 1 && thing(waits)->status == 0, "one finishes while another waits");
        settle();
        expect(thing(waits)->status == 1 && thing(waits)->finish_calls == 3,
               "a waiting finish is asked once an update, not again in the same one");
        wgf_core_priv_load_set_budget(0.0f);
        wgf_core_priv_handle_pool_free(&thing_pool, waits);
        wgf_core_priv_handle_pool_free(&thing_pool, after);
    }

    /* shutdown with requests in flight discards their data and calls no one */
    make("things/a.txt");
    make("things/slow.txt");
    wgf_core_priv_load_set_worker_count(-1);
    wgf_core_priv_update();
    wgf_core_priv_fs_rmdir("things");
    wgf_core_priv_shutdown();
    expect(live_prepared == 0, "shutdown discards what was prepared");
    expect(!wgf_core_priv_load_request(&loader, "x", 1), "no requests once core has stopped");
    wgf_core_priv_handle_pool_destroy(&thing_pool);
    wgf_core_priv_mutex_destroy(&count_lock);
    return failures == 0 ? 0 : 1;
}
