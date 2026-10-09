#ifndef WGF_CORE_LOAD_PRIV_H
#define WGF_CORE_LOAD_PRIV_H

#include <stdbool.h>
#include <stddef.h>

#include "wgf_handle.h"

/* The load pipeline behind a resource's load on create: gfx's textures and
 * meshes, audio's clips. The layer makes the resource's handle at once, PENDING,
 * and asks for it to be loaded. During wgf_update the pipeline then
 *
 *   1. makes the file local: natively it is there or it isn't; on the web a file
 *      kept only in IndexedDB is read into memory first;
 *   2. prepares it on a worker thread: the loader reads and decodes it, touching
 *      nothing shared. With no threads (the web build without them) one prepare
 *      runs on the main thread per update;
 *   3. finishes it on the main thread, oldest first, a step at a time within a
 *      per-update budget, at least one step per update: the loader fills in the
 *      resource, such as its GPU upload, and a big one can spread over updates.
 *
 * A failure at any step -- a bad path, a missing file, a prepare or finish that
 * fails -- calls the loader's fail on the resource. Nothing is ever called back
 * inside the request: every outcome is in a later update. Cribbed from the
 * worker half of wgrender's wgr_asset. */

typedef enum wgf_core_priv_load_step_t {
    WGF_CORE_PRIV_LOAD_DONE = 0,  /* the resource is ready */
    WGF_CORE_PRIV_LOAD_MORE = 1,  /* call finish again, in this update or a later one */
    WGF_CORE_PRIV_LOAD_FAILED = 2, /* nothing was made (logged why) */
    /* waiting on something outside the frame (the browser decoding audio): call finish
       again in a later update, and finish the others meanwhile */
    WGF_CORE_PRIV_LOAD_WAIT = 3
} wgf_core_priv_load_step_t;

typedef struct wgf_core_priv_loader_t {
    const char *name; /* "texture", for logs */
    /* Any thread: the CPU data for the file at `path` (read with fs's private
     * read), or NULL when it can't be loaded (logged why). */
    void *(*prepare)(const char *path);
    /* Main thread: one step of filling in `resource` from `prepared`. */
    wgf_core_priv_load_step_t (*finish)(void *prepared, wgf_handle_t resource);
    /* Free prepared data, finished or not. */
    void (*discard)(void *prepared);
    /* Main thread: the load failed; mark `resource` FAILED. */
    void (*fail)(wgf_handle_t resource);
    /* Main thread, optional: take a file still arriving (a streamed sound, natively),
       which will be at `path` once whole; `arrival` tells how it goes (the hooks'
       arrival, below) until the loader ends it (arrival_end). The request is over:
       the loader fills in `resource` from here, and fails it if the file does. NULL
       for a loader that reads only whole files, which is every other. */
    void (*arrive)(wgf_handle_t resource, const char *path, wgf_handle_t arrival);
    /* Whether its finish can fill a resource that is READY again, in place
       (wgf_core_priv_resource_reload): what it held freed only once the new is made. */
    bool reloads;
} wgf_core_priv_loader_t;

/* What makes a request's file local, when something other than core does (CONVENTIONS.md,
 * "An optional part is reached through its hook"): the asset part (asset/), installed
 * by the first resource made from a path, sets these and its stop clears them. With
 * none, a file is local or it isn't (the web's IndexedDB read into memory first), and a
 * path that isn't one under the root fails. */
typedef enum wgf_core_priv_locate_t {
    WGF_CORE_PRIV_LOCATE_LOCAL = 0,   /* fs has it, at the path (which may have been changed) */
    WGF_CORE_PRIV_LOCATE_PENDING = 1, /* ask again in a later update */
    WGF_CORE_PRIV_LOCATE_FAILED = 2,  /* it can't be had (logged why) */
    WGF_CORE_PRIV_LOCATE_ARRIVING = 3 /* it is arriving, to be at the path: the loader's arrive */
} wgf_core_priv_locate_t;

/* A file arriving (the loader's arrive): still ARRIVING, WHOLE (all of it at its
 * path), or FAILED (it won't be, logged why). */
typedef enum wgf_core_priv_arrival_t {
    WGF_CORE_PRIV_ARRIVAL_ARRIVING = 0,
    WGF_CORE_PRIV_ARRIVAL_WHOLE = 1,
    WGF_CORE_PRIV_ARRIVAL_FAILED = 2
} wgf_core_priv_arrival_t;

typedef struct wgf_core_priv_load_hooks_t {
    /* Each update, before the requests are located: fetches answered, tasks moved on. */
    void (*update)(void);
    /* Make request `request`'s file local: its `path` on entry, and where fs has it on
       LOCAL (a redirect's, a URL's key), the request then reading that. Called each
       update until it isn't PENDING. `arrival` is NULL unless the request's loader
       takes a file still arriving: then a file being downloaded may answer ARRIVING,
       `path` where it will be and `*arrival` set. */
    wgf_core_priv_locate_t (*locate)(wgf_handle_t request, char *path, size_t path_size, wgf_handle_t *arrival);
    /* The request ended (done, failed, or cancelled) while asked about. */
    void (*forget)(wgf_handle_t request);
    /* Its file was local but failed to load: true when a cached copy was dropped and
       the request should locate again, once (a bad copy rather than a bad file). */
    bool (*refetch)(const char *path);
    /* A path that is a URL: the key it is kept and found under, true; false for one it
       can't take. Asked when a path isn't one under the root. */
    bool (*url_key)(const char *url, char *key, size_t key_size);
    /* A file arriving (locate's ARRIVING): how it goes. Its bytes so far are read by
       fs's rule for a file read while it arrives (wgf_core_fs_priv.h): from its
       partial path while that is there, then from its path. */
    wgf_core_priv_arrival_t (*arrival)(wgf_handle_t arrival);
    /* The loader is done with it: told nothing more, and let go of. */
    void (*arrival_end)(wgf_handle_t arrival);
    /* The `index`th URL it may come from into `url`, false past the last: its own, then
       the redirects' still to try. On the web the loader fetches a file arriving itself
       (a streamed sound's <audio> element), trying the next when one fails, and arrival
       says ARRIVING until it is ended. */
    bool (*arrival_source)(wgf_handle_t arrival, int index, char *url, size_t url_size);
} wgf_core_priv_load_hooks_t;
extern wgf_core_priv_load_hooks_t wgf_core_priv_load_hooks;

/* Files that name other files (a .gltf its buffers and images): each format's lister,
 * which the layer that reads the format registers and the asset part asks, so a file is
 * made local with what it names (wgf_asset_ensure) without asset knowing the format. `add`
 * is called for each URI as the file writes it: `fallback_uri` (or NULL) a file to look
 * for when `uri` is missing, `required` whether the file is no good without it. */
typedef void (*wgf_core_priv_load_add_fn)(const char *uri, const char *fallback_uri, bool required, void *context);
typedef void (*wgf_core_priv_load_lister_fn)(const unsigned char *data, int size, wgf_core_priv_load_add_fn add,
                                             void *context);
/* `extension` with its dot (".gltf"), matched without case; up to 8, kept across runs.
 * False when full. */
bool wgf_core_priv_load_set_lister(const char *extension, wgf_core_priv_load_lister_fn list);
/* The lister for the file at `path`, by its extension, or NULL. */
wgf_core_priv_load_lister_fn wgf_core_priv_load_lister(const char *path);

/* `static void name(void)`, defined after it, run before main whenever its file is
 * linked: how a format's file registers its listers, so a file ensured before the
 * program's first create of that format is made local with what it names. The one
 * code that runs before main (CONVENTIONS.md, "An optional part is reached through its
 * hook"): it may only fill a static table, such as the listers', never touch state a
 * run sets up. It runs only if something else pulls its file from the archive. */
#if defined(_MSC_VER)
#pragma section(".CRT$XCU", read)
#define WGF_CORE_PRIV_ON_LINK(name)                                                                                    \
    static void name(void);                                                                                            \
    __declspec(allocate(".CRT$XCU")) static void (*const name##_on_link)(void) = name;                               \
    static void name(void)
#else
#define WGF_CORE_PRIV_ON_LINK(name)                                                                                    \
    __attribute__((constructor)) static void name(void);                                                               \
    static void name(void)
#endif

/* Load the file at `path` into `resource` with `loader`. The path is a request's,
 * normalized and jailed as fs's public paths are. False only when the request
 * can't be held (memory); everything else, a bad path included, is told through
 * the loader in a later update. `loader` must outlive the request. */
bool wgf_core_priv_load_request(const wgf_core_priv_loader_t *loader, const char *path, wgf_handle_t resource);

/* The same, loading a resource's file again (wgf_core_priv_resource_reload): its file
 * fetched anew rather than taken from what this run has (the locate hook asks
 * is_reload). */
bool wgf_core_priv_load_request_reload(const wgf_core_priv_loader_t *loader, const char *path, wgf_handle_t resource);
bool wgf_core_priv_load_is_reload(wgf_handle_t request);

/* Forget every request for `resource`, released while it loads -- a resource can
 * have more than one, such as a texture's image and its alpha: their prepared data
 * is discarded whenever it arrives, and neither finish nor fail is called. False
 * when there is no request for it. */
bool wgf_core_priv_load_cancel(wgf_handle_t resource);

/* Milliseconds of finishing per update (default 4); 0 or less still finishes one
 * step per update. */
void wgf_core_priv_load_set_budget(float milliseconds);
float wgf_core_priv_load_get_budget(void);

/* Worker threads: by default one fewer than the CPUs, 1 to 4, and none without
 * threads. 0 prepares on the main thread, one per update. Takes effect at once,
 * waiting for running prepares. */
void wgf_core_priv_load_set_worker_count(int count);
int wgf_core_priv_load_get_worker_count(void);

/* Requests not yet done, failed, or cancelled. */
int wgf_core_priv_load_get_pending_count(void);

/* Warn, one line each, which requests are pending and where each is (locating its
 * file, reading the web's cache, waiting for a worker, finishing): for a check that
 * ran out of time, as wgrender's wgri_asset_pending_log. The number warned of. */
int wgf_core_priv_load_log_pending(void);

/* Started and stopped with core; run in wgf_update, after fs. Stopping
 * discards every request's prepared data without calling fail: the layers that made
 * the requests have stopped first. */
void wgf_core_priv_load_init(void);
void wgf_core_priv_load_deinit(void);
void wgf_core_priv_load_update(void);

#endif
