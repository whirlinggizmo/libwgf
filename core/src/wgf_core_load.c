#include "wgf_core_load_priv.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"
#include "wgf_core_thread_priv.h"
#include "wgf_time.h"

/* Workers never touch the request table: they see only two rings, jobs in and
 * results out, under one lock, and a job carries copies of what a prepare needs.
 * The main thread owns everything else, so the table can grow under them.
 *
 * As wgrender's asset tasks, a request has at most one job, and keeps its slot
 * until that job is back, even when cancelled; the rings grow with the table, so
 * they always have room for every job and a push never fails. */

#define MAX_WORKERS 4
#define DEFAULT_BUDGET_MS 4.0f

typedef enum state_t {
    STATE_BAD_PATH,  /* fails in the next update, never inside the request */
    STATE_LOCATING,  /* making the file local */
    STATE_CACHE_READ, /* web: reading it from IndexedDB into memory */
    STATE_QUEUED,    /* waiting for a worker, or the main thread without workers */
    STATE_FINISHING, /* prepared: finishing on the main thread */
    STATE_CANCELLED  /* cancelled while queued: its slot waits for its job to come back */
} state_t;

typedef struct request_t {
    const wgf_core_priv_loader_t *loader;
    wgf_handle_t resource;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
    state_t state;
    int cache_read;          /* web: the cache read in flight */
    void *prepared;          /* STATE_FINISHING */
    unsigned long order;     /* prepared order: the oldest finishes first */
    bool finish_started;     /* a finish in progress goes on before any other */
    unsigned long waiting;   /* the update its finish said to WAIT in (updates counts from 1); 0 none */
    bool asked;              /* the locate hook was asked about it: told when it ends */
    bool refetched;          /* its file failed once and was located again */
} request_t;

wgf_core_priv_load_hooks_t wgf_core_priv_load_hooks;

static unsigned long updates; /* updates run: a request WAITs until the next */

typedef struct job_t {
    wgf_handle_t request;
    const wgf_core_priv_loader_t *loader;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
    void *prepared; /* a result: what prepare returned */
} job_t;

typedef struct ring_t {
    job_t *jobs;
    int capacity;
    int head;
    int count;
} ring_t;

static bool running;
static wgf_core_priv_handle_pool_t request_pool;
static request_t *requests;
static unsigned long next_order;
static float budget_ms = DEFAULT_BUDGET_MS;
static int worker_request = -1; /* -1: the default count */

static struct {
    wgf_core_priv_mutex_t lock;
    wgf_core_priv_cond_t wake;
    ring_t queue; /* jobs for the workers */
    ring_t done;  /* their results */
    bool stop;
    wgf_core_priv_thread_t threads[MAX_WORKERS];
    int worker_count;
} jobs;

/* --- rings, under jobs.lock ---------------------------------------------- */

static void ring_push(ring_t *ring, const job_t *job)
{
    ring->jobs[(ring->head + ring->count) % ring->capacity] = *job; /* never full: one entry per request */
    ring->count++;
}

/* Grow a ring to `capacity` jobs, keeping their order. Ported from wgrender's
 * wgr_asset. */
static bool ring_grow(ring_t *ring, int capacity)
{
    job_t *grown;
    int i;
    if (ring->capacity >= capacity) return true;
    grown = (job_t *)malloc(sizeof(job_t) * (size_t)capacity);
    if (grown == NULL) return false;
    for (i = 0; i < ring->count; i++) grown[i] = ring->jobs[(ring->head + i) % ring->capacity];
    free(ring->jobs);
    ring->jobs = grown;
    ring->capacity = capacity;
    ring->head = 0;
    return true;
}

static bool ring_pop(ring_t *ring, job_t *job)
{
    if (ring->count == 0) return false;
    *job = ring->jobs[ring->head];
    ring->head = (ring->head + 1) % ring->capacity;
    ring->count--;
    return true;
}

static void ring_free(ring_t *ring)
{
    free(ring->jobs);
    memset(ring, 0, sizeof(*ring));
}

/* --- workers -------------------------------------------------------------- */

static void worker_main(void *arg)
{
    job_t job;
    (void)arg;
    wgf_core_priv_mutex_lock(&jobs.lock);
    for (;;) {
        while (!jobs.stop && jobs.queue.count == 0) wgf_core_priv_cond_wait(&jobs.wake, &jobs.lock);
        if (jobs.stop) break;
        ring_pop(&jobs.queue, &job);
        wgf_core_priv_mutex_unlock(&jobs.lock);
        job.prepared = job.loader->prepare(job.path);
        wgf_core_priv_mutex_lock(&jobs.lock);
        ring_push(&jobs.done, &job);
    }
    wgf_core_priv_mutex_unlock(&jobs.lock);
}

static int default_worker_count(void)
{
    const int count = wgf_core_priv_thread_get_cpu_count() - 1;
    if (!wgf_core_priv_thread_is_available()) return 0;
    return count < 1 ? 1 : (count > MAX_WORKERS ? MAX_WORKERS : count);
}

static void start_workers(int count)
{
    int i;
    jobs.stop = false;
    jobs.worker_count = 0;
    for (i = 0; i < count && i < MAX_WORKERS; i++) {
        if (!wgf_core_priv_thread_create(&jobs.threads[i], worker_main, NULL)) {
            wgf_log_warn("wgf_core_load: started %d of %d workers; the rest of loading runs on the main thread", i,
                        count);
            break;
        }
        jobs.worker_count++;
    }
}

/* Running prepares finish first; their results stay in the done ring. */
static void stop_workers(void)
{
    int i;
    wgf_core_priv_mutex_lock(&jobs.lock);
    jobs.stop = true;
    wgf_core_priv_cond_broadcast(&jobs.wake);
    wgf_core_priv_mutex_unlock(&jobs.lock);
    for (i = 0; i < jobs.worker_count; i++) wgf_core_priv_thread_join(&jobs.threads[i]);
    jobs.worker_count = 0;
}

/* --- requests, on the main thread ----------------------------------------- */

static request_t *request_of(wgf_handle_t handle)
{
    uint16_t index;
    return wgf_core_priv_handle_pool_resolve(&request_pool, handle, &index) ? &requests[index] : NULL;
}

static void end_request(wgf_handle_t handle, request_t *request_ptr, bool failed)
{
    if (request_ptr->prepared != NULL) request_ptr->loader->discard(request_ptr->prepared);
    request_ptr->prepared = NULL;
    if (failed && request_ptr->asked && !request_ptr->refetched && wgf_core_priv_load_hooks.refetch != NULL &&
        wgf_core_priv_load_hooks.refetch(request_ptr->path)) {
        /* a cached copy that won't load: located again, once */
        request_ptr->refetched = true;
        request_ptr->state = STATE_LOCATING;
        request_ptr->finish_started = false;
        request_ptr->waiting = 0;
        return;
    }
    if (request_ptr->asked && wgf_core_priv_load_hooks.forget != NULL) wgf_core_priv_load_hooks.forget(handle);
    if (failed) request_ptr->loader->fail(request_ptr->resource);
    wgf_core_priv_handle_pool_free(&request_pool, handle);
}

static void queue_prepare(wgf_handle_t handle, request_t *request_ptr)
{
    job_t job;
    job.request = handle;
    job.loader = request_ptr->loader;
    memcpy(job.path, request_ptr->path, sizeof(job.path));
    job.prepared = NULL;
    request_ptr->state = STATE_QUEUED;
    wgf_core_priv_mutex_lock(&jobs.lock);
    ring_push(&jobs.queue, &job);
    wgf_core_priv_cond_broadcast(&jobs.wake);
    wgf_core_priv_mutex_unlock(&jobs.lock);
}

bool wgf_core_priv_load_request(const wgf_core_priv_loader_t *loader, const char *path, wgf_handle_t resource)
{
    wgf_handle_t handle;
    uint16_t index;
    request_t *request_ptr;
    bool grown;

    if (!running || loader == NULL) return false;
    handle = wgf_core_priv_handle_pool_alloc(&request_pool);
    if (handle == 0 || !wgf_core_priv_handle_pool_resolve(&request_pool, handle, &index)) return false;
    /* room on both rings for every request's job, before this one can have one */
    wgf_core_priv_mutex_lock(&jobs.lock);
    grown = ring_grow(&jobs.queue, request_pool.capacity) && ring_grow(&jobs.done, request_pool.capacity);
    wgf_core_priv_mutex_unlock(&jobs.lock);
    if (!grown) {
        wgf_core_priv_handle_pool_free(&request_pool, handle);
        wgf_log_error("wgf_core_load: out of memory");
        return false;
    }
    request_ptr = &requests[index];
    memset(request_ptr, 0, sizeof(*request_ptr));
    request_ptr->loader = loader;
    request_ptr->resource = resource;
    request_ptr->state = wgf_core_priv_fs_normalize_path(path, request_ptr->path, sizeof(request_ptr->path))
                             ? STATE_LOCATING
                             : STATE_BAD_PATH;
    if (request_ptr->state == STATE_BAD_PATH && wgf_core_priv_load_hooks.url_key != NULL && path != NULL &&
        strlen(path) < sizeof(request_ptr->path)) {
        memcpy(request_ptr->path, path, strlen(path) + 1); /* a URL: the hook knows it */
        request_ptr->state = strstr(path, "://") != NULL ? STATE_LOCATING : STATE_BAD_PATH;
    }
    return true;
}

bool wgf_core_priv_load_cancel(wgf_handle_t resource)
{
    uint16_t i;
    bool any = false;
    if (!running) return false;
    for (i = 1; i < request_pool.capacity; i++) {
        const wgf_handle_t handle = wgf_core_priv_handle_pool_handle_from_index(&request_pool, i);
        if (handle == 0 || requests[i].state == STATE_CANCELLED || requests[i].resource != resource) continue;
        any = true;
        if (requests[i].state == STATE_QUEUED) {
            /* its prepare is queued or running: the slot waits for it, and its data is
               discarded when it comes back */
            requests[i].state = STATE_CANCELLED;
            requests[i].resource = 0;
            continue;
        }
        end_request(handle, &requests[i], false);
    }
    return any;
}

void wgf_core_priv_load_set_budget(float milliseconds)
{
    budget_ms = milliseconds > 0.0f ? milliseconds : 0.0f;
}

float wgf_core_priv_load_get_budget(void)
{
    return budget_ms;
}

void wgf_core_priv_load_set_worker_count(int count)
{
    worker_request = count;
    if (!running) return;
    stop_workers();
    start_workers(count >= 0 ? count : default_worker_count());
}

int wgf_core_priv_load_get_worker_count(void)
{
    return jobs.worker_count;
}

int wgf_core_priv_load_get_pending_count(void)
{
    uint16_t i;
    int count = 0;
    if (!running) return 0;
    for (i = 1; i < request_pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&request_pool, i) != 0 && requests[i].state != STATE_CANCELLED) {
            count++;
        }
    }
    return count;
}

int wgf_core_priv_load_log_pending(void)
{
    static const char *const stages[] = {"a bad path", "locating its file", "reading the cache", "waiting to be prepared",
                                         "finishing", "cancelled"};
    uint16_t i;
    int count = 0;
    for (i = 1; running && i < request_pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&request_pool, i) == 0 || requests[i].state == STATE_CANCELLED) {
            continue;
        }
        wgf_log_warn("wgf_core_load: pending: %s (%s)", requests[i].path, stages[requests[i].state]);
        count++;
    }
    return count;
}

/* Step 1: make each located request's file local, then queue its prepare. */
static void locate(void)
{
    uint16_t i;
    if (!wgf_core_priv_fs_is_ready()) return; /* the web store's list isn't read yet */
    for (i = 1; i < request_pool.capacity; i++) {
        const wgf_handle_t handle = wgf_core_priv_handle_pool_handle_from_index(&request_pool, i);
        request_t *request_ptr = &requests[i];
        if (handle == 0) continue;
        if (request_ptr->state == STATE_BAD_PATH) {
            wgf_log_warn("wgf_core_load: %s: not a path under the root", request_ptr->loader->name);
            end_request(handle, request_ptr, true);
        } else if (request_ptr->state == STATE_LOCATING && wgf_core_priv_load_hooks.locate != NULL) {
            char path[WGF_CORE_PRIV_FS_PATH_MAX];
            wgf_core_priv_locate_t located;
            wgf_handle_t arrival = 0;
            memcpy(path, request_ptr->path, sizeof(path));
            request_ptr->asked = true;
            located = wgf_core_priv_load_hooks.locate(handle, path, sizeof(path),
                                                      request_ptr->loader->arrive != NULL ? &arrival : NULL);
            request_ptr = &requests[i]; /* it may have requested loads, and grown the table */
            if (located == WGF_CORE_PRIV_LOCATE_PENDING) continue;
            if (located == WGF_CORE_PRIV_LOCATE_ARRIVING) { /* the loader's from here */
                const wgf_core_priv_loader_t *loader = request_ptr->loader;
                const wgf_handle_t resource = request_ptr->resource;
                if (strcmp(path, request_ptr->path) != 0) wgf_core_priv_resource_set_found(resource, path);
                request_ptr->refetched = true;
                end_request(handle, request_ptr, false);
                loader->arrive(resource, path, arrival);
                continue;
            }
            if (located == WGF_CORE_PRIV_LOCATE_FAILED) {
                request_ptr->refetched = true; /* it never was local: nothing to fetch again */
                end_request(handle, request_ptr, true);
                continue;
            }
            if (strcmp(path, request_ptr->path) != 0) {
                memcpy(request_ptr->path, path, sizeof(path));
                wgf_core_priv_resource_set_found(request_ptr->resource, path); /* where it was found */
            }
            if (wgf_core_priv_fs_exists(request_ptr->path)) {
                queue_prepare(handle, request_ptr);
            } else if ((request_ptr->cache_read = wgf_core_priv_fs_cache_read_begin(request_ptr->path)) != 0) {
                request_ptr->state = STATE_CACHE_READ;
            } else {
                wgf_log_warn("wgf_core_load: %s %s: located, then gone", request_ptr->loader->name, request_ptr->path);
                end_request(handle, request_ptr, true);
            }
        } else if (request_ptr->state == STATE_LOCATING) {
            if (wgf_core_priv_fs_exists(request_ptr->path)) {
                queue_prepare(handle, request_ptr);
            } else if ((request_ptr->cache_read = wgf_core_priv_fs_cache_read_begin(request_ptr->path)) != 0) {
                request_ptr->state = STATE_CACHE_READ;
            } else {
                wgf_log_warn("wgf_core_load: %s %s: no such file", request_ptr->loader->name, request_ptr->path);
                end_request(handle, request_ptr, true);
            }
        } else if (request_ptr->state == STATE_CACHE_READ) {
            const int state = wgf_core_priv_fs_cache_read_poll(request_ptr->cache_read);
            if (state == 0) continue;
            request_ptr->cache_read = 0;
            if (state > 0) {
                queue_prepare(handle, request_ptr);
            } else {
                wgf_log_warn("wgf_core_load: %s %s: couldn't read it from the cache", request_ptr->loader->name,
                            request_ptr->path);
                end_request(handle, request_ptr, true);
            }
        }
    }
}

/* Step 2's results: what the workers prepared, or, with none, one prepare here. */
static void collect(void)
{
    job_t job;
    bool have;

    if (jobs.worker_count == 0) {
        wgf_core_priv_mutex_lock(&jobs.lock);
        have = ring_pop(&jobs.queue, &job);
        wgf_core_priv_mutex_unlock(&jobs.lock);
        if (have) {
            const request_t *request_ptr = request_of(job.request);
            job.prepared = request_ptr->state != STATE_CANCELLED ? job.loader->prepare(job.path) : NULL;
            wgf_core_priv_mutex_lock(&jobs.lock);
            ring_push(&jobs.done, &job);
            wgf_core_priv_mutex_unlock(&jobs.lock);
        }
    }
    for (;;) {
        request_t *request_ptr;
        wgf_core_priv_mutex_lock(&jobs.lock);
        have = ring_pop(&jobs.done, &job);
        wgf_core_priv_mutex_unlock(&jobs.lock);
        if (!have) break;
        request_ptr = request_of(job.request); /* its slot waited for it */
        if (request_ptr->state == STATE_CANCELLED) {
            if (job.prepared != NULL) job.loader->discard(job.prepared);
            end_request(job.request, request_ptr, false);
            continue;
        }
        if (job.prepared == NULL) {
            wgf_log_warn("wgf_core_load: %s %s: couldn't be prepared", request_ptr->loader->name, request_ptr->path);
            end_request(job.request, request_ptr, true);
            continue;
        }
        request_ptr->prepared = job.prepared;
        request_ptr->state = STATE_FINISHING;
        request_ptr->order = next_order++;
    }
}

/* The finishing request to step next: one already started, else the oldest. */
static uint16_t next_finishing(void)
{
    uint16_t i, next = 0;
    for (i = 1; i < request_pool.capacity; i++) {
        const request_t *request_ptr = &requests[i];
        if (wgf_core_priv_handle_pool_handle_from_index(&request_pool, i) == 0 ||
            request_ptr->state != STATE_FINISHING) {
            continue;
        }
        if (request_ptr->waiting == updates) continue; /* asked again next update */
        if (request_ptr->finish_started) return i;
        if (next == 0 || request_ptr->order < requests[next].order) next = i;
    }
    return next;
}

/* Step 3: finish within the budget, at least one step. */
static void finish(void)
{
    const double start = wgf_time_get_seconds();
    uint16_t i;
    while ((i = next_finishing()) != 0) {
        const wgf_handle_t handle = wgf_core_priv_handle_pool_handle_from_index(&request_pool, i);
        request_t *request_ptr = &requests[i];
        const wgf_core_priv_load_step_t step = request_ptr->loader->finish(request_ptr->prepared, request_ptr->resource);
        request_ptr = &requests[i]; /* finish may have requested a load, and grown the table */
        request_ptr->finish_started = true;
        request_ptr->waiting = step == WGF_CORE_PRIV_LOAD_WAIT ? updates : 0;
        if (step != WGF_CORE_PRIV_LOAD_MORE && step != WGF_CORE_PRIV_LOAD_WAIT) {
            if (step == WGF_CORE_PRIV_LOAD_FAILED) {
                wgf_log_warn("wgf_core_load: %s %s: couldn't be finished", request_ptr->loader->name,
                            request_ptr->path);
            }
            end_request(handle, request_ptr, step == WGF_CORE_PRIV_LOAD_FAILED);
        }
        if ((wgf_time_get_seconds() - start) * 1000.0 >= (double)budget_ms) break;
    }
}

void wgf_core_priv_load_update(void)
{
    if (!running) return;
    updates++;
    if (wgf_core_priv_load_hooks.update != NULL) wgf_core_priv_load_hooks.update();
    locate();
    wgf_core_priv_thread_service(); /* the workers' proxied reads, on the web with threads */
    collect();
    finish();
}

void wgf_core_priv_load_init(void)
{
    if (running) return;
    if (!wgf_core_priv_handle_pool_init(&request_pool, WGF_CORE_PRIV_HANDLE_KIND_LOAD_REQUEST, (void **)&requests, sizeof(request_t), 16,
                                       4096)) {
        wgf_log_error("wgf_core_load: no request pool; resources won't load");
        return;
    }
    wgf_core_priv_mutex_init(&jobs.lock);
    wgf_core_priv_cond_init(&jobs.wake);
    start_workers(worker_request >= 0 ? worker_request : default_worker_count());
    next_order = 0;
    running = true;
}

void wgf_core_priv_load_deinit(void)
{
    job_t job;
    uint16_t i;
    if (!running) return;
    stop_workers();
    while (ring_pop(&jobs.done, &job)) {
        if (job.prepared != NULL) job.loader->discard(job.prepared);
    }
    ring_free(&jobs.queue);
    ring_free(&jobs.done);
    for (i = 1; i < request_pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&request_pool, i) != 0 && requests[i].prepared != NULL) {
            requests[i].loader->discard(requests[i].prepared);
        }
    }
    wgf_core_priv_handle_pool_destroy(&request_pool);
    wgf_core_priv_cond_destroy(&jobs.wake);
    wgf_core_priv_mutex_destroy(&jobs.lock);
    running = false;
}

/* --- listers -------------------------------------------------------------- */

#define MAX_LISTERS 8
static struct {
    char extension[16];
    wgf_core_priv_load_lister_fn list;
} listers[MAX_LISTERS];
static int lister_count;

bool wgf_core_priv_load_set_lister(const char *extension, wgf_core_priv_load_lister_fn list)
{
    int i;
    if (extension == NULL || strlen(extension) >= sizeof(listers[0].extension)) return false;
    for (i = 0; i < lister_count && strcmp(listers[i].extension, extension) != 0; i++) {
    }
    if (i == MAX_LISTERS) return false;
    snprintf(listers[i].extension, sizeof(listers[i].extension), "%s", extension);
    listers[i].list = list;
    if (i == lister_count) lister_count++;
    return true;
}

wgf_core_priv_load_lister_fn wgf_core_priv_load_lister(const char *path)
{
    const size_t path_len = path != NULL ? strlen(path) : 0;
    for (int f = 0; f < lister_count; f++) {
        const size_t ext_len = strlen(listers[f].extension);
        bool match = path_len >= ext_len;
        for (size_t k = 0; match && k < ext_len; k++) {
            match = tolower((unsigned char)path[path_len - ext_len + k]) ==
                    tolower((unsigned char)listers[f].extension[k]);
        }
        if (match) return listers[f].list;
    }
    return NULL;
}

