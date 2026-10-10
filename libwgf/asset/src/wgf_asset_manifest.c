#include "wgf_asset_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_log.h"

/* The manifest tree (wgf_asset_set_manifest), wgrender's: a manifest.json in a
 * directory gives the hash of every file beside it and of each subdirectory's own
 * manifest, so a cached copy whose hash still matches is used with no request, and only
 * the root is asked about, once a run. One record per directory whose manifest was
 * wanted this run, read or not. */

enum { MANIFEST_LOADING = 0, MANIFEST_READY, MANIFEST_FAILED };

typedef struct manifest_dir_t {
    char dir[WGF_ASSET_PRIV_PATH_MAX]; /* under the root manifest's directory: "" for its own, "textures", ... */
    int state;
    wgf_asset_priv_manifest_t manifest;
} manifest_dir_t;

static char root_path[512]; /* the root's path under the host; "" = no manifest */
static size_t base_len;     /* how much of it is its directory, with the "/" */
static unsigned generation; /* moved on when the manifest changes */
static manifest_dir_t *dirs;
static int dir_count, dir_capacity;

void wgf_asset_priv_manifests_forget(void)
{
    for (int i = 0; i < dir_count; i++) wgf_asset_priv_manifest_free(&dirs[i].manifest);
    free(dirs);
    dirs = NULL;
    dir_count = dir_capacity = 0;
    generation++; /* a manifest's task still in flight is for the old one */
}

bool wgf_asset_set_manifest(const char *path)
{
    const char *slash;
    wgf_asset_priv_install();
    if (path == NULL) path = "";
    if (path[0] == '/' || strstr(path, "://") != NULL || strlen(path) >= sizeof(root_path)) {
        wgf_log_warn("wgf_asset_set_manifest: %s isn't a path under the host", path);
        return false;
    }
    wgf_asset_priv_manifests_forget();
    snprintf(root_path, sizeof(root_path), "%s", path);
    slash = strrchr(root_path, '/');
    base_len = slash != NULL ? (size_t)(slash - root_path) + 1 : 0;
    return true;
}

const char *wgf_asset_get_manifest(void)
{
    return root_path;
}

/* The record for directory `dir`, wanted now if it wasn't before: its manifest.json made
 * local by a task of its own, which (but for the root's) has to hash to `hash`. NULL
 * without room. Adding a task may move the tasks. */
static manifest_dir_t *want(const char *dir, const char *hash)
{
    manifest_dir_t *record;
    wgf_asset_priv_task_t *task_ptr;
    char path[WGF_ASSET_PRIV_PATH_MAX];
    int written;
    uint16_t slot;

    for (int i = 0; i < dir_count; i++) {
        if (strcmp(dirs[i].dir, dir) == 0) return &dirs[i];
    }
    if (dir_count == dir_capacity) {
        const int capacity = dir_capacity > 0 ? dir_capacity * 2 : 16;
        manifest_dir_t *grown = (manifest_dir_t *)realloc(dirs, sizeof(*grown) * (size_t)capacity);
        if (grown == NULL) return NULL;
        dirs = grown;
        dir_capacity = capacity;
    }
    record = &dirs[dir_count++];
    memset(record, 0, sizeof(*record));
    snprintf(record->dir, sizeof(record->dir), "%s", dir);
    written = dir[0] == '\0' ? snprintf(path, sizeof(path), "%s", root_path)
                             : snprintf(path, sizeof(path), "%.*s%s/manifest.json", (int)base_len, root_path, dir);
    slot = written > 0 && (size_t)written < sizeof(path) ? wgf_asset_priv_task_new_direct(path) : 0;
    task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr == NULL) {
        dirs[dir_count - 1].state = MANIFEST_FAILED;
        return &dirs[dir_count - 1];
    }
    task_ptr->manifest_checked = true;
    snprintf(task_ptr->expect_hash, sizeof(task_ptr->expect_hash), "%s", hash);
    task_ptr->manifest_dir = dir_count;
    task_ptr->manifest_generation = generation;
    task_ptr->manifest_root = dir[0] == '\0';
    return &dirs[dir_count - 1];
}

int wgf_asset_priv_manifest_lookup(uint16_t slot, char hash[WGF_ASSET_PRIV_SHA256_TEXT])
{
    const wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    const manifest_dir_t *record;
    char rest[WGF_ASSET_PRIV_PATH_MAX], dir[WGF_ASSET_PRIV_PATH_MAX] = "";
    char *name = rest;

    if (task_ptr == NULL || root_path[0] == '\0' || task_ptr->fetch_url[0] != '\0' ||
        strncmp(task_ptr->path, root_path, base_len) != 0 || !wgf_asset_priv_platform_lists()) {
        return WGF_ASSET_PRIV_NOT_LISTED;
    }
    snprintf(rest, sizeof(rest), "%s", task_ptr->path + base_len);
    record = want("", "");
    while (record != NULL && record->state == MANIFEST_READY) {
        char *slash = strchr(name, '/');
        char wanted[WGF_ASSET_PRIV_SHA256_TEXT];
        const char *listed;
        if (slash == NULL) {
            listed = wgf_asset_priv_manifest_find(&record->manifest, name, false);
            if (listed == NULL) return WGF_ASSET_PRIV_NOT_LISTED;
            memcpy(hash, listed, WGF_ASSET_PRIV_SHA256_TEXT);
            return WGF_ASSET_PRIV_LISTED;
        }
        *slash = '\0';
        listed = wgf_asset_priv_manifest_find(&record->manifest, name, true);
        if (listed == NULL) return WGF_ASSET_PRIV_NOT_LISTED;
        memcpy(wanted, listed, sizeof(wanted));
        {
            const size_t used = strlen(dir), more = strlen(name) + (used > 0 ? 1 : 0);
            if (used + more >= sizeof(dir)) return WGF_ASSET_PRIV_NOT_LISTED;
            if (used > 0) dir[used] = '/';
            memcpy(dir + used + (used > 0 ? 1 : 0), name, strlen(name) + 1);
        }
        record = want(dir, wanted);
        name = slash + 1;
    }
    return record != NULL && record->state == MANIFEST_LOADING ? WGF_ASSET_PRIV_LOOKING : WGF_ASSET_PRIV_NOT_LISTED;
}

void wgf_asset_priv_manifest_loaded(const wgf_asset_priv_task_t *task_ptr, bool ok)
{
    manifest_dir_t *record;
    unsigned char *data = NULL;
    int size = 0;

    if (task_ptr->manifest_generation != generation || task_ptr->manifest_dir > dir_count) {
        return; /* the manifest was set again meanwhile */
    }
    record = &dirs[task_ptr->manifest_dir - 1];
    record->state = MANIFEST_FAILED;
    if (!ok && task_ptr->manifest_root) { /* a host without one */
        wgf_log_info("wgf_asset: no manifest at %s; files are cached as the cache mode says", task_ptr->path);
        return;
    }
    if (!ok) {
        wgf_log_warn("wgf_asset: no manifest %s; what it would list is cached as the cache mode says", task_ptr->path);
        return;
    }
    if (wgf_core_priv_fs_read(task_ptr->path, &data, &size) &&
        wgf_asset_priv_manifest_parse((const char *)data, (size_t)size, &record->manifest)) {
        record->state = MANIFEST_READY;
    } else {
        wgf_log_warn("wgf_asset: %s isn't a manifest; what it would list is cached as the cache mode says",
                     task_ptr->path);
    }
    wgf_core_priv_fs_read_free(data);
}
