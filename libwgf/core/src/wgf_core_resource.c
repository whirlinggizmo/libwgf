#include "wgf_resource.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_core_load_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"

/* The resource core, ported from wgrender's wgr_resource.c (wgf_core_resource_priv.h). */

#define KINDS (WGF_CORE_PRIV_HANDLE_KIND_MASK + 1)

static struct {
    wgf_core_priv_handle_pool_t *pool;
    const wgf_core_priv_resource_kind_t *kind;
} kinds[KINDS];

void wgf_core_priv_resource_register(wgf_core_priv_handle_pool_t *pool, const wgf_core_priv_resource_kind_t *kind)
{
    if (pool != NULL && pool->kind < KINDS) {
        kinds[pool->kind].pool = kind != NULL ? pool : NULL;
        kinds[pool->kind].kind = kind;
    }
}

/* The header of the record in slot `index` of `pool`. */
static wgf_core_priv_resource_t *header(const wgf_core_priv_handle_pool_t *pool, uint16_t index)
{
    return (wgf_core_priv_resource_t *)((char *)*pool->items + (size_t)index * pool->item_size);
}

static wgf_core_priv_handle_pool_t *pool_of(wgf_handle_t resource)
{
    return resource != 0 ? kinds[WGF_CORE_PRIV_HANDLE_KIND(resource)].pool : NULL;
}

wgf_core_priv_resource_t *wgf_core_priv_resource_get_at(wgf_handle_t resource, const char *caller)
{
    wgf_core_priv_handle_pool_t *pool_ptr = pool_of(resource);
    uint16_t index = 0;
    return pool_ptr != NULL && wgf_core_priv_handle_pool_resolve_at(pool_ptr, resource, &index, caller)
               ? header(pool_ptr, index)
               : NULL;
}

static void lock(const wgf_core_priv_resource_kind_t *kind)
{
    if (kind->lock != NULL) kind->lock();
}

static void unlock(const wgf_core_priv_resource_kind_t *kind)
{
    if (kind->unlock != NULL) kind->unlock();
}

/* Free the record of `resource`: what it holds, its loads, its slot. */
static void free_record(wgf_core_priv_handle_pool_t *pool, wgf_handle_t resource, wgf_core_priv_resource_t *record)
{
    const wgf_core_priv_resource_kind_t *kind = kinds[pool->kind].kind;
    wgf_core_priv_load_cancel(resource); /* its file's load, or one of the module's own beside it */
    lock(kind);
    kind->free(resource, record);
    memset(record, 0, pool->item_size);
    wgf_core_priv_handle_pool_free(pool, resource);
    unlock(kind);
}

void wgf_core_priv_resource_unregister(wgf_core_priv_handle_pool_t *pool)
{
    if (pool == NULL || pool->kind >= KINDS || kinds[pool->kind].pool != pool) return;
    for (uint16_t i = 1; i < pool->capacity; i++) {
        const wgf_handle_t resource = wgf_core_priv_handle_pool_handle_from_index(pool, i);
        if (resource != 0) free_record(pool, resource, header(pool, i));
    }
    kinds[pool->kind].pool = NULL;
    kinds[pool->kind].kind = NULL;
}

/* A new record of `kind`, with its defaults and one reference; 0 when the pool is full. */
static wgf_handle_t new_record(wgf_core_priv_handle_kind_t kind)
{
    wgf_core_priv_handle_pool_t *pool = (unsigned)kind < KINDS ? kinds[kind].pool : NULL;
    wgf_handle_t handle;
    uint16_t index = 0;

    if (pool == NULL) return 0;
    lock(kinds[kind].kind); /* the pool may grow, and move */
    handle = wgf_core_priv_handle_pool_alloc(pool);
    if (handle == 0 || !wgf_core_priv_handle_pool_resolve(pool, handle, &index)) {
        unlock(kinds[kind].kind);
        wgf_log_error("%s: no room for another", kinds[kind].kind->create);
        return 0;
    }
    memset(header(pool, index), 0, pool->item_size);
    if (kinds[kind].kind->init != NULL) kinds[kind].kind->init(header(pool, index));
    header(pool, index)->refs = 1;
    unlock(kinds[kind].kind);
    return handle;
}

wgf_handle_t wgf_core_priv_resource_add(wgf_core_priv_handle_kind_t kind)
{
    const wgf_handle_t handle = new_record(kind);
    wgf_core_priv_resource_t *record_ptr = wgf_core_priv_resource_get(handle);
    if (record_ptr != NULL) record_ptr->status = WGF_RESOURCE_STATUS_READY;
    return handle;
}

wgf_handle_t wgf_core_priv_resource_find(wgf_core_priv_handle_kind_t kind, const char *path)
{
    wgf_core_priv_handle_pool_t *pool = (unsigned)kind < KINDS ? kinds[kind].pool : NULL;
    char key[WGF_CORE_PRIV_FS_PATH_MAX];
    if (pool == NULL || !wgf_core_priv_fs_normalize_path(path, key, sizeof(key))) return 0;
    for (uint16_t i = 1; i < pool->capacity; i++) {
        if (pool->occupied[i] && strcmp(header(pool, i)->path, key) == 0) {
            header(pool, i)->refs++;
            return wgf_core_priv_handle_pool_handle_from_index(pool, i);
        }
    }
    return 0;
}

void wgf_core_priv_resource_set_path(wgf_handle_t resource, const char *path)
{
    wgf_core_priv_resource_t *record_ptr = wgf_core_priv_resource_get(resource);
    if (record_ptr == NULL || !wgf_core_priv_fs_normalize_path(path, record_ptr->path, sizeof(record_ptr->path))) {
        return;
    }
    wgf_core_priv_resource_loaded(resource, record_ptr->path);
}

wgf_handle_t wgf_core_priv_resource_create(wgf_core_priv_handle_kind_t kind, const char *path)
{
    wgf_core_priv_handle_pool_t *pool = (unsigned)kind < KINDS ? kinds[kind].pool : NULL;
    const wgf_core_priv_resource_kind_t *desc = pool != NULL ? kinds[kind].kind : NULL;
    char key[WGF_CORE_PRIV_FS_PATH_MAX] = "";
    const char *request = path != NULL ? path : "";
    wgf_core_priv_resource_t *record;
    wgf_handle_t handle;

    if (desc == NULL) return 0; /* its module isn't running */
    if (wgf_core_priv_fs_normalize_path(path, key, sizeof(key)) ||
        (path != NULL && wgf_core_priv_load_hooks.url_key != NULL &&
         wgf_core_priv_load_hooks.url_key(path, key, sizeof(key)))) { /* a URL: its key, the asset part's */
        handle = wgf_core_priv_resource_find(kind, key); /* the same file: the same resource, whatever its status */
        if (handle != 0) return handle;
    } else {
        key[0] = '\0'; /* a bad path: never shared; the pipeline fails it in a later update */
    }
    handle = new_record(kind);
    record = wgf_core_priv_resource_get(handle);
    if (record == NULL) return 0;
    memcpy(record->path, key, sizeof(key));
    if (key[0] == '\0') {
        snprintf(record->found, sizeof(record->found), "%s", request);
    } else if (desc->map != NULL) {
        desc->map(key, record->found, sizeof(record->found));
    } else {
        memcpy(record->found, key, sizeof(key));
    }
    record->status = WGF_RESOURCE_STATUS_PENDING;
    if (!wgf_core_priv_load_request(desc->loader(record->found), key[0] != '\0' && strstr(request, "://") != NULL
                                                                     ? request /* a URL, which the asset part maps */
                                                                     : record->found,
                                    handle)) {
        wgf_log_warn("%s: %s: nothing to load it with (is core running?)", desc->create, request);
        record->status = WGF_RESOURCE_STATUS_FAILED;
    }
    return handle;
}

void wgf_core_priv_resource_retain(wgf_handle_t resource)
{
    wgf_core_priv_resource_t *record_ptr = wgf_core_priv_resource_get(resource);
    if (record_ptr != NULL) record_ptr->refs++;
}

void wgf_core_priv_resource_loaded(wgf_handle_t resource, const char *found)
{
    wgf_core_priv_resource_t *record_ptr = wgf_core_priv_resource_get(resource);
    if (record_ptr == NULL) return;
    record_ptr->status = WGF_RESOURCE_STATUS_READY;
    record_ptr->reloading = false;
    if (found != NULL) snprintf(record_ptr->found, sizeof(record_ptr->found), "%s", found);
}

void wgf_core_priv_resource_failed(wgf_handle_t resource)
{
    wgf_core_priv_resource_t *record_ptr = wgf_core_priv_resource_get(resource);
    if (record_ptr == NULL) return;
    if (record_ptr->reloading) { /* what it had is kept */
        record_ptr->reloading = false;
        wgf_log_error("wgf_asset_reload: %s didn't load again: what was loaded is kept", record_ptr->path);
        return;
    }
    record_ptr->status = WGF_RESOURCE_STATUS_FAILED;
}

bool wgf_core_priv_resource_is_reloading(wgf_handle_t resource)
{
    const wgf_core_priv_resource_t *record_ptr = wgf_core_priv_resource_get(resource);
    return record_ptr != NULL && record_ptr->reloading;
}

int wgf_core_priv_resource_reload(const char *path)
{
    char key[WGF_CORE_PRIV_FS_PATH_MAX];
    int count = 0;
    if (!wgf_core_priv_fs_normalize_path(path, key, sizeof(key))) return 0;
    for (unsigned kind = 0; kind < KINDS; kind++) {
        wgf_core_priv_handle_pool_t *pool = kinds[kind].pool;
        const wgf_core_priv_resource_kind_t *desc = kinds[kind].kind;
        for (uint16_t i = 1; pool != NULL && i < pool->capacity; i++) {
            wgf_core_priv_resource_t *record = header(pool, i);
            const wgf_core_priv_loader_t *loader;
            const wgf_handle_t handle = wgf_core_priv_handle_pool_handle_from_index(pool, i);
            if (handle == 0 || strcmp(record->path, key) != 0 || record->permanent || record->reloading ||
                record->status == WGF_RESOURCE_STATUS_PENDING || desc->loader == NULL) {
                continue;
            }
            loader = desc->loader(record->found);
            if (loader == NULL || !loader->reloads) {
                wgf_log_warn("wgf_asset_reload: %s: a %s doesn't load again (textures and glTF meshes do)", key,
                             loader != NULL ? loader->name : "resource");
                continue;
            }
            record->reloading = true;
            if (!wgf_core_priv_load_request_reload(loader, record->found, handle)) {
                record = header(pool, i);
                record->reloading = false;
                continue;
            }
            count++;
        }
    }
    return count;
}

wgf_resource_status_t wgf_resource_get_status(wgf_handle_t resource)
{
    const wgf_core_priv_resource_t *record_ptr = wgf_core_priv_resource_get(resource);
    return record_ptr != NULL ? record_ptr->status : WGF_RESOURCE_STATUS_NONE;
}

const char *wgf_resource_get_path(wgf_handle_t resource)
{
    const wgf_core_priv_resource_t *record_ptr = wgf_core_priv_resource_get(resource);
    return record_ptr != NULL && record_ptr->status == WGF_RESOURCE_STATUS_READY ? record_ptr->found : "";
}

bool wgf_resource_release(wgf_handle_t resource)
{
    wgf_core_priv_handle_pool_t *pool_ptr = pool_of(resource);
    wgf_core_priv_resource_t *record_ptr = wgf_core_priv_resource_get(resource);

    if (record_ptr == NULL) {
        if (resource != 0) {
            wgf_log_warn("wgf_resource_release: %u isn't a resource (released already, or an object)",
                         (unsigned int)resource);
        }
        return false;
    }
    if (record_ptr->permanent) return true;
    if (record_ptr->refs > 0) record_ptr->refs--;
    if (record_ptr->refs == 0) free_record(pool_ptr, resource, record_ptr);
    return true;
}

bool wgf_resource_set_load_budget(float milliseconds)
{
    if (!isfinite(milliseconds) || milliseconds < 0.0f) return false;
    wgf_core_priv_load_set_budget(milliseconds); /* 0: one step a frame */
    return true;
}

float wgf_resource_get_load_budget(void)
{
    return wgf_core_priv_load_get_budget();
}

void wgf_core_priv_resource_set_found(wgf_handle_t resource, const char *found)
{
    wgf_core_priv_resource_t *record_ptr = wgf_core_priv_resource_get(resource);
    if (record_ptr != NULL && found != NULL) snprintf(record_ptr->found, sizeof(record_ptr->found), "%s", found);
}
