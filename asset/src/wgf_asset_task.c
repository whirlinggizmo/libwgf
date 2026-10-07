#include "wgf_asset_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_core_load_priv.h"
#include "wgf_log.h"

/* Tasks (wgf_asset.h): a file made local, a group of them, or a ping, ported from
 * wgrender's wgr_asset.c. A load's file is a task too, not kept, which core's locate
 * hook asks about each update (wgf_asset.c); its preparing and finishing are core's
 * (wgf_core_load.c), where wgrender's asset tasks did them. Every outcome is in an
 * update, never in the call that asked. */

#define TASKS_INITIAL 64

static bool ready;
static wgf_core_priv_handle_pool_t pool;
static wgf_asset_priv_task_t *tasks; /* moves when the pool grows: no pointer held across a new task */

static bool ensure_pool(void)
{
    if (!ready) {
        ready = wgf_core_priv_handle_pool_init(&pool, WGF_CORE_PRIV_HANDLE_KIND_ASSET_TASK, (void **)&tasks,
                                               sizeof(wgf_asset_priv_task_t), TASKS_INITIAL,
                                               WGF_CORE_PRIV_HANDLE_POOL_MAX_SLOTS);
        if (!ready) wgf_log_error("wgf_asset: out of memory for tasks");
    }
    return ready;
}

wgf_asset_priv_task_t *wgf_asset_priv_task_at(uint16_t slot)
{
    return ready && slot > 0 && slot < pool.capacity && pool.occupied[slot] ? &tasks[slot] : NULL;
}

static uint16_t slot_of_at(wgf_handle_t handle, const char *caller)
{
    uint16_t slot = 0;
    return ready && wgf_core_priv_handle_pool_resolve_at(&pool, handle, &slot, caller) ? slot : 0;
}
#define slot_of(...) slot_of_at(__VA_ARGS__, WGF_CORE_PRIV_CALLER)

/* A task the program holds (an ensure, a group, a ping), or NULL. */
static wgf_asset_priv_task_t *kept_task(wgf_handle_t handle)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot_of(handle));
    return task_ptr != NULL && task_ptr->kept && !task_ptr->dropped && !task_ptr->arrival ? task_ptr : NULL;
}

static uint16_t new_slot(void)
{
    wgf_handle_t handle;
    uint16_t slot = 0;
    if (!ensure_pool()) return 0;
    handle = wgf_core_priv_handle_pool_alloc(&pool);
    if (handle == 0 || !wgf_core_priv_handle_pool_resolve(&pool, handle, &slot)) {
        wgf_log_warn("wgf_asset: no room for another task");
        return 0;
    }
    memset(&tasks[slot], 0, sizeof(tasks[slot]));
    return slot;
}

void wgf_asset_priv_task_free(uint16_t slot)
{
    if (wgf_asset_priv_task_at(slot) == NULL) return;
    for (uint16_t i = 1; i < pool.capacity; i++) { /* its dependencies finish on their own, telling no one */
        if (pool.occupied[i] && tasks[i].parent == slot) tasks[i].parent = 0;
    }
    wgf_asset_priv_platform_task_freed(&tasks[slot], wgf_core_priv_handle_pool_handle_from_index(&pool, slot));
    free(tasks[slot].candidates);
    memset(&tasks[slot], 0, sizeof(tasks[slot]));
    wgf_core_priv_handle_pool_free(&pool, wgf_core_priv_handle_pool_handle_from_index(&pool, slot));
}

uint16_t wgf_asset_priv_task_new(const char *path, const char *fetch_url, bool local, unsigned int flags)
{
    const uint16_t slot = new_slot();
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr == NULL) return 0;
    snprintf(task_ptr->origin, sizeof(task_ptr->origin), "%s", path);
    task_ptr->flags = flags;
    if (fetch_url != NULL) { /* the caller chose the file: no redirects */
        snprintf(task_ptr->path, sizeof(task_ptr->path), "%s", local ? fetch_url : path);
        snprintf(task_ptr->fetch_url, sizeof(task_ptr->fetch_url), "%s", local ? "" : fetch_url);
        task_ptr->caller_url = true;
        return slot;
    }
    {
        int count;
        wgf_asset_priv_candidate_t *list = wgf_asset_priv_plan(path, &count);
        if (list == NULL || count == 0) { /* out of memory: the path as it is */
            free(list);
            snprintf(task_ptr->path, sizeof(task_ptr->path), "%s", path);
            return slot;
        }
        snprintf(task_ptr->path, sizeof(task_ptr->path), "%s", list[0].path);
        snprintf(task_ptr->fetch_url, sizeof(task_ptr->fetch_url), "%s", list[0].url);
        task_ptr->overlay = list[0].overlay;
        if (count > 1) { /* the rest wait, in the same buffer */
            memmove(list, &list[1], sizeof(*list) * (size_t)(count - 1));
            task_ptr->candidates = list;
            task_ptr->candidate_count = count - 1;
        } else {
            free(list);
        }
    }
    return slot;
}

uint16_t wgf_asset_priv_task_new_direct(const char *path)
{
    const uint16_t slot = new_slot();
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr == NULL) return 0;
    snprintf(task_ptr->origin, sizeof(task_ptr->origin), "%s", path);
    snprintf(task_ptr->path, sizeof(task_ptr->path), "%s", path);
    return slot;
}

/* A task's file is missing where it looked: the next redirect's path, if it has one.
 * It starts over at the next step. */
static bool use_fallback(wgf_asset_priv_task_t *task_ptr)
{
    const wgf_asset_priv_candidate_t *next;
    /* a file already arriving at its loader is that file or nothing: another path's is
       another file, and its loader has the first one's bytes */
    if (task_ptr->arrival) return false;
    if (task_ptr->candidates == NULL || task_ptr->candidate_next >= task_ptr->candidate_count) return false;
    next = &task_ptr->candidates[task_ptr->candidate_next++];
    if (task_ptr->overlay) { /* a redirect without this file: normal for mods and translations */
        wgf_log_debug("wgf_asset: %s not at %s; trying %s", task_ptr->origin, task_ptr->path, next->path);
    } else {
        wgf_log_warn("wgf_asset: %s not found; trying %s", task_ptr->path, next->path);
    }
    snprintf(task_ptr->path, sizeof(task_ptr->path), "%s", next->path);
    snprintf(task_ptr->fetch_url, sizeof(task_ptr->fetch_url), "%s", next->url);
    task_ptr->overlay = next->overlay;
    task_ptr->state = WGF_ASSET_PRIV_NEW;
    task_ptr->fetch_result = WGF_ASSET_PRIV_FETCH_PENDING;
    task_ptr->manifest_checked = false; /* another path: another entry */
    task_ptr->expect_hash[0] = '\0';
    return true;
}

/* Paths an explicit source was found for (an ensure's fetch_url read where it is), so a
 * later create of the path loads it, as wgrender's found_key. */
typedef struct found_t {
    char key[WGF_ASSET_PRIV_PATH_MAX];
    char at[WGF_ASSET_PRIV_PATH_MAX];
} found_t;
static found_t *founds;
static int found_count, found_capacity;

static void record_found(const char *key, const char *at)
{
    int i;
    for (i = 0; i < found_count && strcmp(founds[i].key, key) != 0; i++) {
    }
    if (strcmp(key, at) == 0) { /* found where it is: forget an earlier answer */
        if (i < found_count) founds[i] = founds[--found_count];
        return;
    }
    if (i == found_count && found_count == found_capacity) {
        const int capacity = found_capacity > 0 ? found_capacity * 2 : 16;
        found_t *grown = (found_t *)realloc(founds, sizeof(found_t) * (size_t)capacity);
        if (grown == NULL) return;
        founds = grown;
        found_capacity = capacity;
    }
    snprintf(founds[i].key, sizeof(founds[i].key), "%s", key);
    snprintf(founds[i].at, sizeof(founds[i].at), "%s", at);
    if (i == found_count) found_count++;
}

const char *wgf_asset_priv_found(const char *key)
{
    for (int i = 0; i < found_count; i++) {
        if (strcmp(founds[i].key, key) == 0) return founds[i].at;
    }
    return NULL;
}

static void complete(uint16_t slot, bool ok);

/* Its group told that a member finished. */
static void tell_group(uint16_t group, bool ok)
{
    wgf_asset_priv_task_t *group_ptr = wgf_asset_priv_task_at(group);
    if (group_ptr == NULL) return;
    group_ptr->pending--;
    group_ptr->failed_members += ok ? 0 : 1;
    if (group_ptr->pending <= 0) complete(group, group_ptr->failed_members == 0);
}

/* A task has finished: kept (the program's, or a load's, which its locate hook reads),
 * or freed; its group told. */
static void complete(uint16_t slot, bool ok)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    uint16_t group;
    bool tell;
    if (task_ptr == NULL) return;
    if (task_ptr->manifest_dir != 0) { /* a manifest's: read, then gone; no group, no load */
        wgf_asset_priv_manifest_loaded(task_ptr, ok);
        wgf_asset_priv_task_free(slot);
        return;
    }
    if (!ok && task_ptr->dependency_failed) {
        wgf_log_warn("wgf_asset: %s: a file it names is missing", task_ptr->origin);
    } else if (!ok && task_ptr->optional) {
        wgf_log_warn("wgf_asset: %s: not found (optional, named by %s)", task_ptr->origin,
                     tasks[task_ptr->parent].origin);
    } else if (!ok && !task_ptr->is_group && !task_ptr->is_ping) {
        wgf_log_warn("wgf_asset: %s: not found%s", task_ptr->origin,
                     task_ptr->fetch_url[0] != '\0' ? " (nor fetched)" : "");
    } else if (!ok && task_ptr->is_group) {
        wgf_log_warn("wgf_asset: a group: %d of %d file(s) failed", task_ptr->failed_members, task_ptr->member_count);
    }
    if (ok && task_ptr->caller_url && !task_ptr->is_group && !task_ptr->is_ping) {
        record_found(task_ptr->origin, task_ptr->path); /* what a later create of it loads */
    }
    group = task_ptr->group;
    tell = group != 0 && !task_ptr->dropped;
    if (task_ptr->parent != 0) { /* a dependency: its parent told, and it gone */
        const uint16_t parent = task_ptr->parent;
        const bool optional = task_ptr->optional;
        wgf_asset_priv_task_t *parent_ptr;
        wgf_asset_priv_task_free(slot);
        parent_ptr = wgf_asset_priv_task_at(parent);
        if (parent_ptr == NULL) return;
        parent_ptr->pending--;
        parent_ptr->dependency_failed = parent_ptr->dependency_failed || (!ok && !optional);
        if (parent_ptr->pending <= 0 && parent_ptr->state == WGF_ASSET_PRIV_WAITING) {
            complete(parent, !parent_ptr->dependency_failed);
        }
        return;
    }
    free(task_ptr->candidates);
    task_ptr->candidates = NULL;
    task_ptr->candidate_count = task_ptr->candidate_next = 0;
    if ((task_ptr->kept && !task_ptr->dropped) || task_ptr->request != 0) {
        task_ptr->state = ok ? WGF_ASSET_PRIV_DONE : WGF_ASSET_PRIV_FAILED;
    } else {
        wgf_asset_priv_task_free(slot);
    }
    if (tell) tell_group(group, ok);
}

/* A dependency of the task in `*context` (a slot), as its file names it: queued, once. */
static void add_dependency(const char *uri, const char *fallback_uri, bool required, void *context)
{
    const uint16_t parent = *(const uint16_t *)context;
    wgf_asset_priv_task_t *parent_ptr = wgf_asset_priv_task_at(parent), *child_ptr;
    char path[WGF_ASSET_PRIV_PATH_MAX], fallback[WGF_ASSET_PRIV_PATH_MAX] = "", url[WGF_ASSET_PRIV_URL_MAX] = "";
    uint16_t child;
    if (parent_ptr == NULL || !wgf_asset_priv_is_relative_uri(uri)) return; /* data:, a URL: the file's own business */
    if (!wgf_asset_priv_join_relative(parent_ptr->path, uri, path, sizeof(path))) {
        wgf_log_warn("wgf_asset: %s: can't use what it names, %s (outside the host, or too long)", parent_ptr->path, uri);
        parent_ptr->dependency_failed = parent_ptr->dependency_failed || required;
        return;
    }
    for (uint16_t i = 1; i < pool.capacity; i++) { /* named twice: made local once */
        if (pool.occupied[i] && tasks[i].parent == parent && strcmp(tasks[i].origin, path) == 0) return;
    }
    if (fallback_uri != NULL && wgf_asset_priv_is_relative_uri(fallback_uri)) {
        wgf_asset_priv_join_relative(parent_ptr->path, fallback_uri, fallback, sizeof(fallback));
    }
    if (parent_ptr->caller_url) {
        /* a file fetched from the caller's URL: what it names comes from next to it */
        const char *slash = strrchr(parent_ptr->fetch_url, '/');
        if (parent_ptr->fetch_url[0] != '\0' && slash != NULL) {
            snprintf(url, sizeof(url), "%.*s%s", (int)(slash - parent_ptr->fetch_url) + 1, parent_ptr->fetch_url, uri);
        }
        child = url[0] != '\0' ? wgf_asset_priv_task_new(path, url, false, parent_ptr->flags)
                                 : wgf_asset_priv_task_new_direct(path);
    } else {
        child = wgf_asset_priv_task_new(path, NULL, false, parent_ptr->flags); /* through the redirects */
    }
    parent_ptr = wgf_asset_priv_task_at(parent); /* the new task may have moved the tasks */
    child_ptr = wgf_asset_priv_task_at(child);
    if (parent_ptr == NULL) return; /* never: making a task frees none */
    if (child_ptr == NULL) {
        wgf_log_warn("wgf_asset: %s: no room to make %s local with it", parent_ptr->path, path);
        parent_ptr->dependency_failed = parent_ptr->dependency_failed || required;
        return;
    }
    child_ptr->caller_url = child_ptr->caller_url || parent_ptr->caller_url;
    if (fallback[0] != '\0') { /* tried when its own is missing: a texture's own image for its compressed file */
        wgf_asset_priv_candidate_t *grown = (wgf_asset_priv_candidate_t *)realloc(
            child_ptr->candidates, sizeof(*grown) * (size_t)(child_ptr->candidate_count + 1));
        if (grown != NULL) {
            wgf_asset_priv_candidate_t *c = &grown[child_ptr->candidate_count];
            memset(c, 0, sizeof(*c));
            snprintf(c->path, sizeof(c->path), "%s", fallback);
            if (url[0] != '\0') {
                const char *slash = strrchr(parent_ptr->fetch_url, '/');
                snprintf(c->url, sizeof(c->url), "%.*s%s", (int)(slash - parent_ptr->fetch_url) + 1,
                         parent_ptr->fetch_url, fallback_uri);
            }
            child_ptr->candidates = grown;
            child_ptr->candidate_count++;
        }
    }
    child_ptr->parent = parent;
    child_ptr->optional = !required;
    parent_ptr->pending++;
    parent_ptr->dependency_count++;
}

/* A task's own file is local: the files it names queued (its format's lister). */
static void start_dependencies(uint16_t slot)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    const wgf_core_priv_load_lister_fn list = wgf_core_priv_load_lister(task_ptr->path);
    unsigned char *data = NULL;
    int size = 0;
    uint16_t context = slot;
    task_ptr->dependencies_started = true;
    if (list == NULL || !wgf_core_priv_fs_read(task_ptr->path, &data, &size)) return; /* its loader reports it */
    list(data, size, add_dependency, &context);
    wgf_core_priv_fs_read_free(data);
}

void wgf_asset_priv_task_resolved(uint16_t slot, bool ok)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr == NULL) return;
    if (!ok && use_fallback(task_ptr)) return; /* the next path: looked at in the next step */
    if (ok && !task_ptr->dependencies_started && !task_ptr->is_group && !task_ptr->is_ping &&
        task_ptr->manifest_dir == 0) {
        start_dependencies(slot);
        task_ptr = wgf_asset_priv_task_at(slot);
        if (task_ptr->pending > 0) {
            task_ptr->state = WGF_ASSET_PRIV_WAITING; /* finishes with its last dependency */
            return;
        }
    }
    complete(slot, ok && !task_ptr->dependency_failed);
}

static bool is_finished(const wgf_asset_priv_task_t *task_ptr)
{
    return task_ptr->state == WGF_ASSET_PRIV_DONE || task_ptr->state == WGF_ASSET_PRIV_FAILED;
}

/* One step of `slot`'s task, whatever it is. */
static void step_one(uint16_t slot)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr == NULL || is_finished(task_ptr)) return;
    if (task_ptr->is_group) {
        if (task_ptr->pending <= 0) complete(slot, task_ptr->failed_members == 0); /* empty, or its members first */
        return;
    }
    if (task_ptr->is_ping) {
        const float ms = wgf_asset_priv_platform_ping_poll(task_ptr);
        if (ms <= -2.0f) return; /* still waiting */
        task_ptr->ping_ms = ms;
        if (task_ptr->dropped) {
            wgf_asset_priv_task_free(slot);
        } else {
            task_ptr->state = ms >= 0.0f ? WGF_ASSET_PRIV_DONE : WGF_ASSET_PRIV_FAILED;
        }
        return;
    }
    wgf_asset_priv_platform_step(slot);
}

void wgf_asset_priv_tasks_step(uint16_t slot)
{
    if (!ready) return;
    if (slot != 0) {
        step_one(slot);
        return;
    }
    for (uint16_t i = 1; i < pool.capacity; i++) {
        if (pool.occupied[i]) step_one(i);
    }
}

wgf_handle_t wgf_asset_priv_task_handle(uint16_t slot)
{
    return wgf_asset_priv_task_at(slot) != NULL ? wgf_core_priv_handle_pool_handle_from_index(&pool, slot) : 0;
}

uint16_t wgf_asset_priv_task_slot(wgf_handle_t handle)
{
    return slot_of(handle);
}

uint16_t wgf_asset_priv_task_capacity(void)
{
    return ready ? pool.capacity : 0;
}

uint16_t wgf_asset_priv_task_of_request(wgf_handle_t request)
{
    for (uint16_t i = 1; ready && i < pool.capacity; i++) {
        if (pool.occupied[i] && tasks[i].request == request) return i;
    }
    return 0;
}

void wgf_asset_priv_tasks_stop(void)
{
    if (!ready) return;
    for (uint16_t i = 1; i < pool.capacity; i++) {
        if (pool.occupied[i]) free(tasks[i].candidates);
    }
    wgf_core_priv_handle_pool_destroy(&pool);
    tasks = NULL;
    ready = false;
    free(founds);
    founds = NULL;
    found_count = found_capacity = 0;
}

/* ------------------------------------------------------------- the public API ---- */

wgf_asset_task_t wgf_asset_ensure(const char *path, const char *fetch_url, unsigned int flags)
{
    char logical[WGF_ASSET_PRIV_PATH_MAX], source[WGF_ASSET_PRIV_URL_MAX] = "";
    wgf_asset_priv_source_t found = WGF_ASSET_PRIV_SOURCE_URL;
    uint16_t slot;
    wgf_asset_priv_task_t *task_ptr;

    wgf_asset_priv_install();
    if (path == NULL || !wgf_asset_priv_normalize_path(path, logical, sizeof(logical))) {
        wgf_log_warn("wgf_asset_ensure: %s isn't a path under the host (absolute, a drive, or climbing out with "
                     "\"..\")",
                     path != NULL ? path : "(null)");
        return 0;
    }
    if (fetch_url != NULL && fetch_url[0] != '\0') {
        const wgf_asset_priv_host_kind_t kind = wgf_asset_priv_platform_host_kind();
        found = wgf_asset_priv_resolve_source(wgf_asset_priv_host(), kind, fetch_url, source, sizeof(source));
        if (found == WGF_ASSET_PRIV_SOURCE_LOCAL && strlen(source) >= WGF_ASSET_PRIV_PATH_MAX) {
            found = WGF_ASSET_PRIV_SOURCE_REFUSED; /* it becomes the task's path, which it wouldn't fit */
        }
        if (found == WGF_ASSET_PRIV_SOURCE_REFUSED) {
            wgf_log_warn("wgf_asset_ensure: %s: %s isn't a source this host can read (a local one has to be a path "
                         "under the host; a URL, http or https)",
                         logical, fetch_url);
            return 0;
        }
    }
    slot = wgf_asset_priv_task_new(logical, fetch_url != NULL && fetch_url[0] != '\0' ? source : NULL,
                                   found == WGF_ASSET_PRIV_SOURCE_LOCAL, flags);
    task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr == NULL) return 0;
    task_ptr->kept = true;
    return wgf_core_priv_handle_pool_handle_from_index(&pool, slot);
}

wgf_asset_task_status_t wgf_asset_task_get_status(wgf_asset_task_t task)
{
    const wgf_asset_priv_task_t *task_ptr = kept_task(task);
    if (task_ptr == NULL) return WGF_ASSET_TASK_STATUS_NONE;
    return task_ptr->state == WGF_ASSET_PRIV_DONE     ? WGF_ASSET_TASK_STATUS_DONE
           : task_ptr->state == WGF_ASSET_PRIV_FAILED ? WGF_ASSET_TASK_STATUS_FAILED
                                                      : WGF_ASSET_TASK_STATUS_PENDING;
}

const char *wgf_asset_task_get_path(wgf_asset_task_t task)
{
    static char local[WGF_ASSET_PRIV_URL_MAX];
    const wgf_asset_priv_task_t *task_ptr = kept_task(task);
    if (task_ptr == NULL || task_ptr->is_group || task_ptr->is_ping || task_ptr->state != WGF_ASSET_PRIV_DONE) {
        return "";
    }
    wgf_core_priv_fs_resolve(task_ptr->path, local, sizeof(local));
    return local;
}

/* Rough progress of one file: half for being local, half for the files it names. */
static float task_progress(const wgf_asset_priv_task_t *task_ptr)
{
    if (is_finished(task_ptr)) return 1.0f;
    if (task_ptr->state != WGF_ASSET_PRIV_WAITING || task_ptr->is_ping) return 0.0f;
    return 0.5f + 0.5f * (task_ptr->dependency_count > 0
                              ? (float)(task_ptr->dependency_count - task_ptr->pending) / (float)task_ptr->dependency_count
                              : 1.0f);
}

float wgf_asset_task_get_progress(wgf_asset_task_t task)
{
    const uint16_t slot = slot_of(task);
    const wgf_asset_priv_task_t *task_ptr = kept_task(task);
    float sum = 0.0f;
    if (task_ptr == NULL) return 0.0f;
    if (!task_ptr->is_group || is_finished(task_ptr)) return task_progress(task_ptr);
    if (task_ptr->member_count == 0) return 0.0f;
    for (uint16_t i = 1; i < pool.capacity; i++) {
        if (pool.occupied[i] && tasks[i].group == slot) sum += task_progress(&tasks[i]);
    }
    return sum / (float)task_ptr->member_count;
}

/* Destroy one kept task: freed now if it has finished (or is a group), else when it does. */
static void destroy_task(uint16_t slot)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    if (is_finished(task_ptr) || task_ptr->is_group) {
        wgf_asset_priv_task_free(slot);
    } else {
        task_ptr->dropped = true;
        task_ptr->group = 0;
    }
}

void wgf_asset_priv_task_let_go(uint16_t slot)
{
    if (wgf_asset_priv_task_at(slot) != NULL) destroy_task(slot);
}

bool wgf_asset_task_destroy(wgf_asset_task_t task)
{
    const uint16_t slot = slot_of(task);
    const wgf_asset_priv_task_t *task_ptr = kept_task(task);
    if (task_ptr == NULL) return false;
    if (task_ptr->is_group) {
        for (uint16_t i = 1; i < pool.capacity; i++) {
            if (pool.occupied[i] && i != slot && tasks[i].group == slot) destroy_task(i);
        }
    } else if (task_ptr->group != 0) {
        wgf_asset_priv_task_t *group_ptr = &tasks[task_ptr->group]; /* it no longer counts */
        group_ptr->member_count--;
        if (is_finished(task_ptr)) {
            group_ptr->failed_members -= task_ptr->state == WGF_ASSET_PRIV_FAILED ? 1 : 0;
        } else {
            group_ptr->pending--;
        }
    }
    destroy_task(slot);
    return true;
}

wgf_asset_task_t wgf_asset_group_create(void)
{
    uint16_t slot;
    wgf_asset_priv_install();
    slot = new_slot();
    if (slot == 0) return 0;
    tasks[slot].is_group = true;
    tasks[slot].kept = true;
    tasks[slot].state = WGF_ASSET_PRIV_WAITING;
    return wgf_core_priv_handle_pool_handle_from_index(&pool, slot);
}

bool wgf_asset_group_add(wgf_asset_task_t group, wgf_asset_task_t task)
{
    wgf_asset_priv_task_t *group_ptr = kept_task(group), *task_ptr = kept_task(task);
    if (group_ptr == NULL || task_ptr == NULL || !group_ptr->is_group || task_ptr->is_group || task_ptr->is_ping ||
        task_ptr->group != 0 || is_finished(group_ptr)) {
        wgf_log_warn("wgf_asset_group_add: needs a group still pending and a file task that isn't in a group");
        return false;
    }
    task_ptr->group = slot_of(group);
    group_ptr->member_count++;
    if (is_finished(task_ptr)) {
        group_ptr->failed_members += task_ptr->state == WGF_ASSET_PRIV_FAILED ? 1 : 0;
    } else {
        group_ptr->pending++;
    }
    return true;
}

wgf_asset_task_t wgf_asset_ping_host(const char *host, int timeout_ms)
{
    uint16_t slot;
    wgf_asset_priv_install();
    slot = new_slot();
    if (slot == 0) return 0;
    tasks[slot].is_ping = true;
    tasks[slot].kept = true;
    tasks[slot].state = WGF_ASSET_PRIV_WAITING; /* answered at the next update at the soonest */
    if (host == NULL) host = wgf_asset_priv_host();
    snprintf(tasks[slot].path, sizeof(tasks[slot].path), "%s", host);
    wgf_asset_priv_platform_ping(&tasks[slot], host, timeout_ms > 0 ? timeout_ms : 5000);
    return wgf_core_priv_handle_pool_handle_from_index(&pool, slot);
}

float wgf_asset_ping_get_milliseconds(wgf_asset_task_t ping)
{
    const wgf_asset_priv_task_t *task_ptr = kept_task(ping);
    return task_ptr != NULL && task_ptr->is_ping && task_ptr->state == WGF_ASSET_PRIV_DONE ? task_ptr->ping_ms
                                                                                           : 0.0f;
}
