#include "wgf_asset_priv.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wgf_log.h"
#include "wgf_time.h"

/* On the web: a file in this visit's store is local; one in the cache (IndexedDB) is
 * read into it once the cache mode says it is current, else the host is asked first;
 * one in neither is fetched, and kept with what its response said (wgf_asset.h).
 * Ported from wgrender's wgr_asset.c, but polled: its JS called back into C, which had
 * to be exported (EMSCRIPTEN_KEEPALIVE) and so was linked into every program, loading
 * files or not. Here a fetch's answer waits in JS until the task's step asks for it.
 * The JS keeps its state under quoted keys of Module (Module["wgf_asset_fetches"]), so
 * a minifier renaming properties leaves them alone. */

#define MAX_FETCHES 256 /* downloads at once; more tasks wait */
static int fetching;    /* downloads in flight */

/* The directory of the document's base URL (a <base href> moves it), without the "/"
 * it ends with. */
EM_JS(void, wgf_asset_js_base, (char *out, int size), {
    let base = "";
    try {
        base = new URL(".", typeof document !== "undefined" ? document.baseURI : location.href).href;
    } catch (e) {
    }
    if (base.endsWith("/")) base = base.slice(0, -1);
    stringToUTF8(lengthBytesUTF8(base) < size ? base : "", out, size);
})

void wgf_asset_priv_platform_default_host(char *out, size_t out_size)
{
    wgf_asset_js_base(out, (int)out_size);
}

/* Fetch `url` for the task whose handle is `id` (a reused slot's generation differs, so
 * a late answer finds no task), keeping what comes back under Module["wgf_asset_fetches"]
 * until the task's step takes it (wgf_asset_js_fetch_poll): JS never calls into C, so
 * nothing is exported, and a program that loads no file links none of this.
 *
 * `mode` FETCH_PLAIN: a GET the browser's cache may answer. FETCH_REVALIDATE: a cached
 * copy exists, so this asks whether it is still current: on the page's own origin a
 * conditional GET with the copy's validators, past the browser's cache (a 304 is the
 * server's); on another origin the conditional headers would need a CORS preflight the
 * host may refuse, so there the browser revalidates its own cache (no-cache) and hands
 * back a 200 either way. FETCH_CURRENT: what the host has now (no-cache).
 * `headers_only`: a 2xx is dropped at its headers, its body never read (a streamed
 * sound's element fetches that file itself), done with no bytes. */
EM_JS(void, wgf_asset_js_fetch, (int id, const char *url_c, int mode, const char *etag_c, const char *modified_c,
                                  int hash, int headers_only), {
    const url = UTF8ToString(url_c);
    const controller = new AbortController();
    const init = {credentials: "same-origin", signal: controller.signal};
    if (mode === 2) init.cache = "no-cache";
    if (mode === 1) {
        let same = false;
        try {
            same = new URL(url, location.href).origin === location.origin;
        } catch (e) {
        }
        const etag = UTF8ToString(etag_c);
        const modified = UTF8ToString(modified_c);
        if (same && (etag || modified)) {
            init.headers = {};
            if (etag) init.headers["If-None-Match"] = etag;
            if (modified) init.headers["If-Modified-Since"] = modified;
            init.cache = "no-store";
        } else {
            init.cache = "no-cache";
        }
    }
    if (!Module["wgf_asset_fetches"]) Module["wgf_asset_fetches"] = new Map();
    const fetches = Module["wgf_asset_fetches"];
    const entry = {"done": false, "status": 0, "headers": ["", "", "", "", ""], "bytes": null};
    fetches.set(id, entry);
    fetch(url, init)
        .then((response) => {
            const h = response.headers;
            entry["headers"] = [h.get("ETag") || "", h.get("Last-Modified") || "", h.get("Cache-Control") || "",
                                h.get("Age") || "", ""];
            entry["status"] = response.status;
            if (!response.ok) {
                if (response.status !== 304) console.warn("wgf_asset: " + url + ": HTTP " + response.status);
                entry["done"] = true;
                return;
            }
            if (headers_only) {
                controller.abort(); /* the body is the element's to fetch */
                entry["done"] = true;
                return;
            }
            /* byteLength is the decoded size, whatever the host did on the wire */
            return response.arrayBuffer().then((buffer) => {
                /* a listed file hashed by the browser, off the main thread, where the page may
                   (a secure context); else C hashes it a slice an update */
                const subtle = hash && globalThis.crypto && crypto.subtle;
                return (subtle ? subtle.digest("SHA-256", buffer) : Promise.resolve(null))
                    .catch(() => null)
                    .then((digest) => {
                        if (digest) {
                            entry["headers"][4] = "sha256:" + Array.from(new Uint8Array(digest),
                                                                        (b) => b.toString(16).padStart(2, "0")).join("");
                        }
                        entry["bytes"] = new Uint8Array(buffer);
                        entry["done"] = true;
                    });
            });
        })
        .catch((err) => {
            console.warn("wgf_asset: " + url + ": " + err);
            entry["status"] = 0;
            entry["bytes"] = null;
            entry["done"] = true;
        });
})

/* Whether fetch `id` has finished: 1 with its HTTP status (0: no answer) and its body's
 * size (-1: none), 0 while it runs or for one not known. */
EM_JS(int, wgf_asset_js_fetch_poll, (int id, int *status, int *size), {
    const fetches = Module["wgf_asset_fetches"];
    const entry = fetches && fetches.get(id);
    if (!entry || !entry["done"]) return 0;
    HEAP32[status >> 2] = entry["status"];
    HEAP32[size >> 2] = entry["bytes"] ? entry["bytes"].length : -1;
    return 1;
})

/* A finished fetch's body into `out` (its size, from wgf_asset_js_fetch_poll). */
EM_JS(void, wgf_asset_js_fetch_take, (int id, unsigned char *out), {
    const fetches = Module["wgf_asset_fetches"];
    const entry = fetches && fetches.get(id);
    if (entry && entry["bytes"]) HEAPU8.set(entry["bytes"], out);
})

/* One of the finished response's headers into `out` (0 ETag, 1 Last-Modified,
 * 2 Cache-Control, 3 Age; and 4, the body's sha256 when the browser hashed it), or "" --
 * also for one too long for `out`, never cut. */
EM_JS(void, wgf_asset_js_fetch_header, (int id, int which, char *out, int out_size), {
    const fetches = Module["wgf_asset_fetches"];
    const entry = fetches && fetches.get(id);
    const text = entry ? entry["headers"][which] : "";
    stringToUTF8(lengthBytesUTF8(text) < out_size ? text : "", out, out_size);
})

/* Fetch `id` forgotten: taken, or its task gone. One still running keeps running, and
 * what it brings is dropped. */
EM_JS(void, wgf_asset_js_fetch_forget, (int id), {
    const fetches = Module["wgf_asset_fetches"];
    if (fetches) fetches.delete(id);
})

/* What the response said about the file, for keeping with it. */
static wgf_core_priv_fs_meta_t response_meta(int id)
{
    wgf_core_priv_fs_meta_t meta;
    char cache_control[256], age[32];
    memset(&meta, 0, sizeof(meta));
    wgf_asset_js_fetch_header(id, 0, meta.etag, (int)sizeof(meta.etag));
    wgf_asset_js_fetch_header(id, 1, meta.last_modified, (int)sizeof(meta.last_modified));
    wgf_asset_js_fetch_header(id, 2, cache_control, (int)sizeof(cache_control));
    wgf_asset_js_fetch_header(id, 3, age, (int)sizeof(age));
    wgf_asset_js_fetch_header(id, 4, meta.hash, (int)sizeof(meta.hash));
    meta.fresh_until = wgf_asset_priv_fresh_until(cache_control, age, (double)time(NULL));
    return meta;
}

/* A listed download the browser didn't hash (no crypto.subtle: not a secure context),
 * hashed here a slice an update, so a large file never stalls a frame. */
typedef struct wgf_asset_priv_hashing_t {
    unsigned char *data;
    int size, done;
    wgf_asset_priv_sha256_t sha;
    wgf_core_priv_fs_meta_t meta;
} hashing_t;

#define HASH_SLICE_MS 2.0     /* of an update, at the most, hashing one download */
#define HASH_SLICE (64 * 1024) /* bytes fed at a time between looks at the clock */

/* A listed download's bytes, their hash known: kept, or refused when it isn't the
 * manifest's. Takes `data`. */
static void keep_listed(wgf_asset_priv_task_t *task_ptr, unsigned char *data, int size, wgf_core_priv_fs_meta_t *meta)
{
    if (strcmp(meta->hash, task_ptr->expect_hash) != 0) {
        wgf_log_warn("wgf_asset: %s isn't what the manifest lists (%.19s..., not %.19s...); not kept", task_ptr->path,
                     meta->hash, task_ptr->expect_hash);
        task_ptr->fetch_result = WGF_ASSET_PRIV_FETCH_FAILED;
    } else {
        task_ptr->fetch_result = wgf_core_priv_fs_write_meta(task_ptr->path, data, size, meta)
                                     ? WGF_ASSET_PRIV_FETCH_OK
                                     : WGF_ASSET_PRIV_FETCH_FAILED;
    }
    free(data);
}

/* One update's slice of a download being hashed; kept or refused once it is all hashed. */
static void hash_slice(wgf_asset_priv_task_t *task_ptr)
{
    hashing_t *job = task_ptr->hashing;
    const double start = wgf_time_get_seconds();
    while (job->done < job->size && (wgf_time_get_seconds() - start) * 1000.0 < HASH_SLICE_MS) {
        const int n = job->size - job->done < HASH_SLICE ? job->size - job->done : HASH_SLICE;
        wgf_asset_priv_sha256_feed(&job->sha, job->data + job->done, (size_t)n);
        job->done += n;
    }
    if (job->done < job->size) return; /* more next update */
    wgf_asset_priv_sha256_end(&job->sha, job->meta.hash);
    task_ptr->hashing = NULL;
    keep_listed(task_ptr, job->data, job->size, &job->meta);
    free(job);
}

/* The task's download finished with HTTP `status` (0: no answer), `size` bytes of body
 * (-1: none): the answer decides what becomes of a cached copy (wgf_asset.h,
 * REVALIDATE). */
static void fetch_finished(wgf_asset_priv_task_t *task_ptr, int id, int status, int size)
{
    const wgf_core_priv_fs_meta_t meta_answered = response_meta(id);
    wgf_core_priv_fs_meta_t meta = meta_answered;
    unsigned char *data = NULL;
    if (size >= 0 && (data = (unsigned char *)malloc(size > 0 ? (size_t)size : 1)) != NULL) {
        wgf_asset_js_fetch_take(id, data);
    }
    wgf_asset_js_fetch_forget(id);
    if (fetching > 0) fetching--;
    if (data != NULL && status / 100 == 2 && task_ptr->expect_hash[0] != '\0') {
        /* hashed before it is kept: a copy's hash is always that of the bytes it holds */
        hashing_t *job;
        if (meta.hash[0] != '\0') { /* the browser's */
            keep_listed(task_ptr, data, size, &meta);
            return;
        }
        job = (hashing_t *)calloc(1, sizeof(*job));
        if (job == NULL) {
            free(data);
            task_ptr->fetch_result = WGF_ASSET_PRIV_FETCH_FAILED;
            return;
        }
        job->data = data;
        job->size = size;
        job->meta = meta;
        wgf_asset_priv_sha256_begin(&job->sha);
        task_ptr->hashing = job; /* hashed a slice an update, from the next */
        return;
    } else if (data != NULL && status / 100 == 2) {
        meta.hash[0] = '\0'; /* hashed only for the manifest's files */
        task_ptr->fetch_result = wgf_core_priv_fs_write_meta(task_ptr->path, data, size, &meta)
                                     ? WGF_ASSET_PRIV_FETCH_OK
                                     : WGF_ASSET_PRIV_FETCH_FAILED;
    } else if (task_ptr->revalidating && status == 304) {
        wgf_core_priv_fs_meta_t kept;
        wgf_core_priv_fs_meta_get(task_ptr->path, &kept);
        /* a 304 need not repeat the validators; the bytes, and so their hash, are the same */
        if (meta.etag[0] == '\0') memcpy(meta.etag, kept.etag, sizeof(meta.etag));
        if (meta.last_modified[0] == '\0') memcpy(meta.last_modified, kept.last_modified, sizeof(meta.last_modified));
        memcpy(meta.hash, kept.hash, sizeof(meta.hash));
        wgf_core_priv_fs_meta_set(task_ptr->path, &meta);
        task_ptr->fetch_result = WGF_ASSET_PRIV_FETCH_USE_CACHE;
    } else if (task_ptr->revalidating && status / 100 == 4) {
        wgf_log_info("wgf_asset: %s is gone from the host (HTTP %d); forgetting the cached copy", task_ptr->path,
                     status);
        wgf_core_priv_fs_remove(task_ptr->path);
        task_ptr->fetch_result = WGF_ASSET_PRIV_FETCH_FAILED;
    } else if (task_ptr->revalidating) {
        wgf_log_debug("wgf_asset: no answer about %s (HTTP %d); using the cached copy", task_ptr->path, status);
        task_ptr->fetch_result = WGF_ASSET_PRIV_FETCH_USE_CACHE;
    } else {
        task_ptr->fetch_result = WGF_ASSET_PRIV_FETCH_FAILED;
    }
    free(data);
}

bool wgf_asset_priv_platform_arriving(const wgf_asset_priv_task_t *task_ptr)
{
    return task_ptr->streaming;
}

void wgf_asset_priv_platform_task_freed(wgf_asset_priv_task_t *task_ptr, wgf_handle_t handle)
{
    if (task_ptr->hashing != NULL) {
        free(task_ptr->hashing->data);
        free(task_ptr->hashing);
        task_ptr->hashing = NULL;
    }
    if (task_ptr->state == WGF_ASSET_PRIV_FETCHING && task_ptr->cache_read == 0 && !task_ptr->streaming &&
        task_ptr->fetch_result == WGF_ASSET_PRIV_FETCH_PENDING) {
        wgf_asset_js_fetch_forget((int)handle); /* what it brings is dropped */
        if (fetching > 0) fetching--;
    }
}

enum { FETCH_PLAIN = 0, FETCH_REVALIDATE, FETCH_CURRENT }; /* wgf_asset_js_fetch's modes */

/* Download the task's file; `cached` (or NULL) is the metadata of a cached copy this asks
 * about. A file the manifest lists, and the root manifest, come from the host as it is
 * now. A streamed sound's is asked about only (headers_only): a 2xx is its element's to
 * fetch. */
static void start_fetch(uint16_t slot, const wgf_core_priv_fs_meta_t *cached)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    char joined[WGF_ASSET_PRIV_URL_MAX];
    const char *url;
    task_ptr->state = WGF_ASSET_PRIV_FETCHING;
    task_ptr->fetch_result = WGF_ASSET_PRIV_FETCH_PENDING;
    task_ptr->revalidating = cached != NULL;
    if (task_ptr->fetch_url[0] != '\0') { /* its own source wins; else the host + its path */
        url = task_ptr->fetch_url;
    } else {
        snprintf(joined, sizeof(joined), "%s/%s", wgf_asset_priv_host(), task_ptr->path);
        url = joined;
    }
    fetching++;
    wgf_asset_js_fetch((int)wgf_asset_priv_task_handle(slot), url,
                       cached != NULL                                                        ? FETCH_REVALIDATE
                       : task_ptr->expect_hash[0] != '\0' || task_ptr->manifest_root ? FETCH_CURRENT
                                                                                     : FETCH_PLAIN,
                       cached != NULL ? cached->etag : "", cached != NULL ? cached->last_modified : "",
                       task_ptr->expect_hash[0] != '\0', task_ptr->streams);
}

/* Whether a redirect's path still to try has a copy here: in this visit's store, or in
 * the cache as the mode allows. */
static bool later_local(const wgf_asset_priv_task_t *task_ptr)
{
    for (int i = task_ptr->candidate_next; task_ptr->candidates != NULL && i < task_ptr->candidate_count; i++) {
        const char *path = task_ptr->candidates[i].path;
        if (wgf_core_priv_fs_exists(path) ||
            (wgf_asset_priv_cache_mode() != WGF_ASSET_CACHE_MODE_OFF && wgf_core_priv_fs_is_cached(path))) {
            return true;
        }
    }
    return false;
}

/* A file with no usable copy here, to be downloaded. A streamed sound's is its
 * <audio> element's to fetch, as it plays, the only download (Rob's: streaming is for a
 * big file), never entering the cache, checked by no manifest; at once when nothing
 * after it could be local, else asked about first (headers only), so a redirect's file
 * missing falls through to a copy here, which wins. */
static void download(uint16_t slot)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr->streams && !later_local(task_ptr)) {
        task_ptr->state = WGF_ASSET_PRIV_FETCHING;
        task_ptr->fetch_result = WGF_ASSET_PRIV_FETCH_PENDING;
        task_ptr->streaming = true;
        return;
    }
    start_fetch(slot, NULL);
}

void wgf_asset_priv_platform_step(uint16_t slot)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr == NULL || task_ptr->streaming) return; /* its loader's element fetches it */
    if (task_ptr->cache_read != 0) {
        const int read = wgf_core_priv_fs_cache_read_poll(task_ptr->cache_read);
        if (read == 0) return; /* still reading */
        task_ptr->cache_read = 0;
        if (read > 0) {
            wgf_asset_priv_task_resolved(slot, true);
        } else {
            task_ptr->state = WGF_ASSET_PRIV_NEW; /* dropped from the cache: download it */
        }
        return;
    }
    if (task_ptr->hashing != NULL) {
        hash_slice(task_ptr);
        if (task_ptr->hashing != NULL) return; /* more next update */
    } else if (task_ptr->state == WGF_ASSET_PRIV_FETCHING && task_ptr->fetch_result == WGF_ASSET_PRIV_FETCH_PENDING) {
        const int id = (int)wgf_asset_priv_task_handle(slot);
        int status = 0, size = -1;
        if (!wgf_asset_js_fetch_poll(id, &status, &size)) return; /* still downloading */
        if (task_ptr->streams && status / 100 == 2 && size < 0) { /* there: its element fetches it */
            wgf_asset_js_fetch_forget(id);
            if (fetching > 0) fetching--;
            task_ptr->streaming = true;
            return;
        }
        fetch_finished(task_ptr, id, status, size);
        if (task_ptr->hashing != NULL) return; /* hashed a slice an update, from the next */
    }
    if (task_ptr->state == WGF_ASSET_PRIV_FETCHING) {
        if (task_ptr->fetch_result == WGF_ASSET_PRIV_FETCH_USE_CACHE) {
            task_ptr->revalidating = false;
            task_ptr->cache_read = wgf_core_priv_fs_cache_read_begin(task_ptr->path); /* resolves in a later step */
            if (task_ptr->cache_read == 0) task_ptr->state = WGF_ASSET_PRIV_NEW; /* gone meanwhile: download it */
            return;
        }
        wgf_asset_priv_task_resolved(slot, task_ptr->fetch_result == WGF_ASSET_PRIV_FETCH_OK);
        return;
    }
    if (task_ptr->state != WGF_ASSET_PRIV_NEW) return;
    if (!task_ptr->manifest_checked) {
        char hash[WGF_ASSET_PRIV_SHA256_TEXT];
        const int listed = wgf_asset_priv_manifest_lookup(slot, hash);
        task_ptr = wgf_asset_priv_task_at(slot); /* the lookup may have added tasks */
        if (listed == WGF_ASSET_PRIV_LOOKING) return;
        task_ptr->manifest_checked = true;
        if (listed == WGF_ASSET_PRIV_LISTED) memcpy(task_ptr->expect_hash, hash, sizeof(hash));
    }
    /* FORCE_FETCH downloads again. Otherwise a file already in this visit's store is
       used. A cached one the manifest lists is used if its hash is the listed one, and
       fetched if not; any other is used if the mode trusts it or it is still fresh, and
       asked about if not (wgf_asset.h). The root manifest is always asked about. */
    if (!(task_ptr->flags & WGF_ASSET_ENSURE_FORCE_FETCH) && !task_ptr->manifest_root &&
        wgf_core_priv_fs_exists(task_ptr->path)) {
        wgf_asset_priv_task_resolved(slot, true);
        return;
    }
    if (!(task_ptr->flags & WGF_ASSET_ENSURE_FORCE_FETCH) && wgf_asset_priv_cache_mode() != WGF_ASSET_CACHE_MODE_OFF &&
        wgf_core_priv_fs_is_cached(task_ptr->path)) {
        wgf_core_priv_fs_meta_t meta;
        const bool has_meta = wgf_core_priv_fs_meta_get(task_ptr->path, &meta);
        const bool fresh = has_meta && meta.fresh_until > (double)time(NULL);
        const bool listed = task_ptr->expect_hash[0] != '\0';
        if (listed ? strcmp(meta.hash, task_ptr->expect_hash) == 0
                   : !task_ptr->manifest_root && (wgf_asset_priv_cache_mode() == WGF_ASSET_CACHE_MODE_TRUST || fresh)) {
            task_ptr->state = WGF_ASSET_PRIV_FETCHING;
            task_ptr->cache_read = wgf_core_priv_fs_cache_read_begin(task_ptr->path); /* resolves in a later step */
            if (task_ptr->cache_read == 0) task_ptr->state = WGF_ASSET_PRIV_NEW;
            return;
        }
        if (fetching >= MAX_FETCHES) return;
        if (listed) {
            download(slot); /* listed and changed: fetched as it is now */
        } else {
            start_fetch(slot, &meta);
        }
        return;
    }
    if (fetching >= MAX_FETCHES) return; /* waits for a download to finish */
    download(slot);                      /* a miss, or forced: downloaded, cached, resolved in a later step */
}

/* A HEAD request to `url`, timed; any response counts (no-cors: a CDN needs no CORS
 * headers for this). An id for wgf_asset_js_ping_poll. */
EM_JS(int, wgf_asset_js_ping_begin, (const char *url_c, int timeout_ms), {
    if (!Module["wgf_asset_pings"]) {
        Module["wgf_asset_pings"] = new Map();
        Module["wgf_asset_next_ping"] = 1;
    }
    const pings = Module["wgf_asset_pings"];
    const id = Module["wgf_asset_next_ping"]++;
    const start = performance.now();
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout_ms);
    pings.set(id, -2);
    fetch(UTF8ToString(url_c), {method: "HEAD", mode: "no-cors", cache: "no-store", signal: controller.signal})
        .then(() => pings.set(id, performance.now() - start))
        .catch(() => pings.set(id, -1))
        .finally(() => clearTimeout(timer));
    return id;
})

/* Milliseconds, -1 (failed), or -2 (still waiting). */
EM_JS(double, wgf_asset_js_ping_poll, (int id), {
    const pings = Module["wgf_asset_pings"];
    const result = pings ? pings.get(id) : undefined;
    if (result === undefined) return -1;
    if (result !== -2) pings.delete(id);
    return result;
})

void wgf_asset_priv_platform_ping(wgf_asset_priv_task_t *task_ptr, const char *host, int timeout_ms)
{
    char url[600];
    snprintf(url, sizeof(url), "%s/", host); /* the host's root; a 404 still answers */
    task_ptr->ping_id = wgf_asset_js_ping_begin(url, timeout_ms);
    task_ptr->ping_ms = -2.0f;
}

float wgf_asset_priv_platform_ping_poll(wgf_asset_priv_task_t *task_ptr)
{
    if (task_ptr->ping_ms > -2.0f) return task_ptr->ping_ms;
    return (float)wgf_asset_js_ping_poll(task_ptr->ping_id);
}

/* ------------------------------------------- what the web has no use for ---- */

/* The browser downloads and caches: there is no program downloader, cache directory, or
 * download list here, and the calls for them answer so (wgf_asset.h). */
void wgf_asset_priv_platform_update(void)
{
}

bool wgf_asset_priv_platform_refetch(const char *path)
{
    if (!wgf_core_priv_fs_is_cached(path)) return false;
    wgf_log_warn("wgf_asset: %s didn't load; forgetting the cached copy and fetching it again", path);
    wgf_core_priv_fs_remove(path);
    return true;
}

bool wgf_asset_priv_platform_evict(const char *logical)
{
    return wgf_core_priv_fs_remove(logical);
}

wgf_asset_priv_host_kind_t wgf_asset_priv_platform_host_kind(void)
{
    return WGF_ASSET_PRIV_HOST_BROWSER;
}

bool wgf_asset_priv_platform_set_host(char *host)
{
    if (strncmp(host, "file:", 5) == 0) {
        wgf_log_warn("wgf_asset_set_host: %s: a browser doesn't read file: URLs", host);
    }
    return true;
}

void wgf_asset_priv_platform_host_set(bool url)
{
    (void)url;
}

void wgf_asset_priv_platform_install(void)
{
}

bool wgf_asset_priv_platform_lists(void)
{
    return true;
}

void wgf_asset_priv_platform_clear(void)
{
}

bool wgf_asset_set_cache_dir(const char *dir)
{
    (void)dir;
    return false;
}

const char *wgf_asset_get_cache_dir(void)
{
    return "";
}

bool wgf_asset_set_fetching(bool enabled)
{
    (void)enabled;
    return false;
}

bool wgf_asset_is_fetching(void)
{
    return false;
}

wgf_asset_task_t wgf_asset_fetch_next(void)
{
    return 0;
}

const char *wgf_asset_fetch_get_url(wgf_asset_task_t request)
{
    (void)request;
    return "";
}

const char *wgf_asset_fetch_get_dest(wgf_asset_task_t request)
{
    (void)request;
    return "";
}

bool wgf_asset_fetch_is_ping(wgf_asset_task_t request)
{
    (void)request;
    return false;
}

bool wgf_asset_fetch_done(wgf_asset_task_t request, bool ok)
{
    (void)request;
    (void)ok;
    return false;
}

bool wgf_asset_set_fetch_timeout(float seconds)
{
    (void)seconds; /* the browser's fetch has no timeout of its own to apply it to */
    return false;
}

float wgf_asset_get_fetch_timeout(void)
{
    return 0.0f;
}

