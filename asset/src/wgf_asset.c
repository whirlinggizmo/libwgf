#include "wgf_asset_priv.h"

#include <stdio.h>
#include <string.h>

#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"

/* The asset part: installed by the first resource made from a path or the first
 * wgf_asset_* call, it sets core's load hooks, so every load's file is made local here
 * (ROADMAP.md, phase 3). The host and the cache's settings, and what core asks. */

static struct {
    char host[512]; /* set with wgf_asset_set_host; "" = the default */
    wgf_asset_cache_mode_t cache_mode;
} settings;

/* ------------------------------------------------------------------- hooks ---- */

/* Core's locate hook: request `request`'s file local, its task made the first time it
 * is asked, and moved on at once (a file on disk is local in the same update). */
static wgf_core_priv_locate_t locate(wgf_handle_t request, char *path, size_t path_size, wgf_handle_t *arrival)
{
    uint16_t slot = wgf_asset_priv_task_of_request(request);
    wgf_asset_priv_task_t *task_ptr;
    if (slot == 0) {
        if (wgf_asset_priv_has_scheme(path)) { /* a URL: kept under its key, fetched from it */
            char key[WGF_ASSET_PRIV_PATH_MAX];
            if (!wgf_asset_priv_url_key(path, key, sizeof(key))) {
                wgf_log_warn("wgf_asset: %s: a URL naming no file", path);
                return WGF_CORE_PRIV_LOCATE_FAILED;
            }
            slot = wgf_asset_priv_task_new(key, path, false, 0);
        } else if (wgf_asset_priv_found(path) != NULL) { /* an ensure's explicit source: read there */
            slot = wgf_asset_priv_task_new(path, wgf_asset_priv_found(path), true, 0);
        } else {
            slot = wgf_asset_priv_task_new(path, NULL, false, 0);
        }
        task_ptr = wgf_asset_priv_task_at(slot);
        if (task_ptr == NULL) return WGF_CORE_PRIV_LOCATE_FAILED;
        task_ptr->request = request;
        task_ptr->streams = arrival != NULL;
        wgf_asset_priv_tasks_step(slot);
    }
    task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr == NULL) return WGF_CORE_PRIV_LOCATE_FAILED;
    if (task_ptr->state == WGF_ASSET_PRIV_DONE) {
        snprintf(path, path_size, "%s", task_ptr->path);
        wgf_asset_priv_task_free(slot);
        return WGF_CORE_PRIV_LOCATE_LOCAL;
    }
    if (task_ptr->state == WGF_ASSET_PRIV_FAILED) {
        wgf_asset_priv_task_free(slot);
        return WGF_CORE_PRIV_LOCATE_FAILED;
    }
    if (arrival != NULL && wgf_asset_priv_platform_arriving(task_ptr)) { /* bytes have come: the loader's from here */
        snprintf(path, path_size, "%s", task_ptr->path);
        task_ptr->request = 0;
        task_ptr->kept = true;
        task_ptr->arrival = true;
        *arrival = wgf_asset_priv_task_handle(slot);
        return WGF_CORE_PRIV_LOCATE_ARRIVING;
    }
    return WGF_CORE_PRIV_LOCATE_PENDING;
}

/* Core's arrival hooks: a file a loader reads while it arrives is its task, DONE once
 * whole (moved into place, and checked against a manifest that lists it). */
static wgf_core_priv_arrival_t arrival_of(wgf_handle_t arrival)
{
    const wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(wgf_asset_priv_task_slot(arrival));
    if (task_ptr == NULL || !task_ptr->arrival || task_ptr->state == WGF_ASSET_PRIV_FAILED) {
        return WGF_CORE_PRIV_ARRIVAL_FAILED;
    }
    return task_ptr->state == WGF_ASSET_PRIV_DONE ? WGF_CORE_PRIV_ARRIVAL_WHOLE : WGF_CORE_PRIV_ARRIVAL_ARRIVING;
}

static void arrival_end(wgf_handle_t arrival)
{
    const uint16_t slot = wgf_asset_priv_task_slot(arrival);
    const wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr == NULL || !task_ptr->arrival) return;
    if (task_ptr->streaming) {
        wgf_asset_priv_task_free(slot); /* nothing here fetches it */
    } else {
        wgf_asset_priv_task_let_go(slot); /* the download goes on, cached */
    }
}

/* Where a file at `path` is fetched from: `fetch_url`, its own source, or the host's. */
static bool source_url(const char *path, const char *fetch_url, char *url, size_t url_size)
{
    const int n = fetch_url[0] != '\0' ? snprintf(url, url_size, "%s", fetch_url)
                                       : snprintf(url, url_size, "%s/%s", wgf_asset_priv_host(), path);
    return n >= 0 && (size_t)n < url_size;
}

static bool arrival_source(wgf_handle_t arrival, int index, char *url, size_t url_size)
{
    const wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(wgf_asset_priv_task_slot(arrival));
    const wgf_asset_priv_candidate_t *next;
    if (task_ptr == NULL || !task_ptr->arrival || index < 0) return false;
    if (index == 0) return source_url(task_ptr->path, task_ptr->fetch_url, url, url_size);
    if (task_ptr->candidates == NULL || task_ptr->candidate_next + index - 1 >= task_ptr->candidate_count) return false;
    next = &task_ptr->candidates[task_ptr->candidate_next + index - 1];
    return source_url(next->path, next->url, url, url_size);
}

static void forget(wgf_handle_t request)
{
    wgf_asset_priv_task_free(wgf_asset_priv_task_of_request(request));
}

/* A file that won't load may be a bad copy rather than a bad file: a host that
 * compresses once served gzip bytes under a file's name, and a cache keeps what it was
 * given. Forget a cached copy and fetch once more before calling it a failure, as
 * wgrender's refetch_once; natively the file is the program's, and nothing is fetched
 * again. */
static bool refetch(const char *path)
{
#if defined(__EMSCRIPTEN__)
    if (!wgf_core_priv_fs_is_cached(path)) return false;
    wgf_log_warn("wgf_asset: %s didn't load; forgetting the cached copy and fetching it again", path);
    wgf_core_priv_fs_remove(path);
    return true;
#else
    (void)path;
    return false;
#endif
}

static void update(void)
{
    wgf_asset_priv_platform_update(); /* natively, the program's answers */
    wgf_asset_priv_tasks_step(0);
}

static void stop(void)
{
    wgf_asset_priv_tasks_stop();
    wgf_asset_priv_manifests_forget();
    memset(&wgf_core_priv_load_hooks, 0, sizeof(wgf_core_priv_load_hooks));
}

static wgf_core_priv_part_t part = {.name = "asset",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_ASSET,
                                    .order = WGF_CORE_PRIV_PART_ASSET,
                                    .stop = stop};

void wgf_asset_priv_install(void)
{
    if (part.installed) return;
    wgf_core_priv_part_install(&part);
    wgf_core_priv_load_hooks.update = update;
    wgf_core_priv_load_hooks.locate = locate;
    wgf_core_priv_load_hooks.forget = forget;
    wgf_core_priv_load_hooks.refetch = refetch;
    wgf_core_priv_load_hooks.url_key = wgf_asset_priv_url_key;
    wgf_core_priv_load_hooks.arrival = arrival_of;
    wgf_core_priv_load_hooks.arrival_end = arrival_end;
    wgf_core_priv_load_hooks.arrival_source = arrival_source;
    wgf_asset_priv_platform_install();
}

wgf_handle_t wgf_asset_priv_resource_create(wgf_core_priv_handle_kind_t kind, const char *path)
{
    wgf_asset_priv_install(); /* a path is an asset */
    return wgf_core_priv_resource_create(kind, path);
}

/* -------------------------------------------------------------- settings ---- */

const char *wgf_asset_priv_host(void)
{
    static char host[512];
    if (settings.host[0] != '\0') return settings.host;
    wgf_asset_priv_platform_default_host(host, sizeof(host));
    return host;
}

bool wgf_asset_priv_host_is_url(void)
{
    const char *host = wgf_asset_priv_host();
    return strstr(host, "://") != NULL && strncmp(host, "file:", 5) != 0;
}

wgf_asset_cache_mode_t wgf_asset_priv_cache_mode(void)
{
    return settings.cache_mode;
}

bool wgf_asset_set_host(const char *host)
{
    size_t n;
    wgf_asset_priv_install();
    if (host == NULL) host = "";
    if (strlen(host) >= sizeof(settings.host)) {
        wgf_log_warn("wgf_asset_set_host: a host of 512 bytes or more");
        return false;
    }
    snprintf(settings.host, sizeof(settings.host), "%s", host);
    n = strlen(settings.host);
    while (n > 1 && settings.host[n - 1] == '/') settings.host[--n] = '\0';
#if !defined(__EMSCRIPTEN__)
    if (settings.host[0] != '\0' && !wgf_asset_priv_host_is_url()) {
        /* a local host is the storage's root, named as a path or a file: URL; a relative
           path against the program's own directory, never the working directory, so a
           double-clicked program finds its files and a host named in code means the same
           thing here as on the web (Rob, 2026-10-04) */
        char local[512], joined[WGF_CORE_PRIV_FS_PATH_MAX];
        const char *root = local;
        snprintf(local, sizeof(local), "%s", settings.host);
        if (strncmp(settings.host, "file:", 5) == 0 &&
            !wgf_asset_priv_file_url_path(settings.host, local, sizeof(local))) {
            wgf_log_warn("wgf_asset_set_host: %s isn't a file: URL naming a directory on this machine", settings.host);
        }
        if (local[0] != '/' && local[0] != '\\' && !(local[0] != '\0' && local[1] == ':') &&
            wgf_core_priv_fs_platform_default_root()[0] != '\0') {
            if (snprintf(joined, sizeof(joined), "%s/%s", wgf_core_priv_fs_platform_default_root(), local) >=
                (int)sizeof(joined)) {
                wgf_log_warn("wgf_asset_set_host: %s is too long a path under the program's directory", local);
                return false;
            }
            root = joined;
        }
        wgf_core_priv_fs_set_root(root);
        settings.host[0] = '\0'; /* the root, read live from now on */
    } else if (settings.host[0] == '\0') {
        wgf_core_priv_fs_set_root(wgf_core_priv_fs_platform_default_root()); /* the default: the program's directory */
    }
    wgf_asset_priv_platform_host_set(wgf_asset_priv_host_is_url()); /* a URL host's files: the cache directory */
#else
    if (strncmp(settings.host, "file:", 5) == 0) {
        wgf_log_warn("wgf_asset_set_host: %s: a browser doesn't read file: URLs", settings.host);
    }
#endif
    return true;
}

const char *wgf_asset_get_host(void)
{
    wgf_asset_priv_install();
    return wgf_asset_priv_host();
}

bool wgf_asset_set_cache_mode(wgf_asset_cache_mode_t mode)
{
    wgf_asset_priv_install();
    if (mode != WGF_ASSET_CACHE_MODE_REVALIDATE && mode != WGF_ASSET_CACHE_MODE_TRUST &&
        mode != WGF_ASSET_CACHE_MODE_OFF) {
        wgf_log_warn("wgf_asset_set_cache_mode: %d isn't a cache mode", (int)mode);
        return false;
    }
    settings.cache_mode = mode;
    wgf_core_priv_fs_set_persistent(mode != WGF_ASSET_CACHE_MODE_OFF);
    return true;
}

wgf_asset_cache_mode_t wgf_asset_get_cache_mode(void)
{
    return settings.cache_mode;
}

bool wgf_asset_evict(const char *path)
{
    char logical[WGF_ASSET_PRIV_PATH_MAX];
    wgf_asset_priv_install();
    if (path == NULL || !wgf_asset_priv_normalize_path(path, logical, sizeof(logical))) {
        wgf_log_warn("wgf_asset_evict: %s isn't a path under the host", path != NULL ? path : "(null)");
        return false;
    }
#if !defined(__EMSCRIPTEN__)
    if (wgf_asset_priv_host_is_url()) return wgf_core_priv_fs_remove(logical); /* the cache is the root */
    { /* the cache's copy: a local host is only ever read */
        char cached[WGF_ASSET_PRIV_PATH_MAX + 8];
        snprintf(cached, sizeof(cached), WGF_CORE_PRIV_FS_CACHE "%s", logical);
        return wgf_core_priv_fs_remove(cached);
    }
#else
    return wgf_core_priv_fs_remove(logical);
#endif
}

void wgf_asset_clear_cache(void)
{
    wgf_asset_priv_install();
    wgf_core_priv_fs_clear();
    wgf_asset_priv_platform_clear(); /* natively, what libwgf downloaded, by its list */
    wgf_asset_priv_manifests_forget(); /* the root is asked about again, and the rest as needed */
}
