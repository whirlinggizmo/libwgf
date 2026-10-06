#include "wgf_asset_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"
#include "wgf_core_thread_priv.h"
#include "wgf_log.h"
#include "wgf_time.h"

/* Natively, files are on disk: a local host is the storage's root (wgf_fs.h), read where
 * it is. A file that has to be downloaded (under a URL host, a URL path, a fetch_url, a
 * "://" redirect for a file the host hasn't) is downloaded by the program, which libwgf
 * asks (wgf_asset_set_fetching): libwgf has no HTTP client natively. Ported from
 * wgrender's wgr_asset.c, with a timeout for an answer that never comes, pings through
 * the fetcher, and a listed download hashed a slice an update. */

#define MAX_DOWNLOADS 6               /* the program's downloads at once: a browser's limit per server */
#define DOWNLOADS_LIST ".wgf-downloads" /* what libwgf downloaded into the cache directory, a path a line */
#define HASH_SLICE_MS 2.0             /* of an update, at the most, hashing one download */
#define HASH_SLICE (64 * 1024)

static struct {
    char cache_dir[512]; /* set by the program; "" = the default */
    bool fetching;       /* the program downloads (wgf_asset_set_fetching) */
    uint32_t order;      /* the last request made */
    int out;             /* downloads asked for and not answered */
    float timeout;       /* seconds a taken request may go unanswered; 0: for ever */
} native;

/* Where downloads go: set, or <the user's cache>/<the executable's name>, or ".wgf-cache"
 * in the storage's root. */
/* The cache directory: set, or the program's under the user's cache,
 * <user cache>/<company>/<app> (wgf.h's identity), or ".wgf-cache" in the storage root
 * where there is no user cache directory to name. */
static const char *cache_dir(void)
{
    static char derived[512];
    if (native.cache_dir[0] != '\0') return native.cache_dir;
    if (!wgf_core_priv_app_cache_dir(derived, sizeof(derived))) snprintf(derived, sizeof(derived), ".wgf-cache");
    return derived;
}

void wgf_asset_priv_platform_default_host(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s", wgf_core_priv_fs_root());
}

void wgf_asset_priv_platform_install(void)
{
    wgf_core_priv_fs_set_cache_root(cache_dir());
}

void wgf_asset_priv_platform_host_set(bool url)
{
    /* a URL host's files are the cache directory's; a local one moves the root itself */
    if (url) wgf_core_priv_fs_set_root(cache_dir());
    wgf_core_priv_fs_set_cache_root(cache_dir());
}

bool wgf_asset_priv_platform_lists(void)
{
    return wgf_asset_priv_host_is_url() && native.fetching; /* a local host's files are simply there */
}

/* ---------------------------------------------------- the downloads list ---- */

static void note_download(const char *path)
{
    char list[600];
    FILE *f;
    if (strncmp(path, WGF_CORE_PRIV_FS_CACHE, sizeof(WGF_CORE_PRIV_FS_CACHE) - 1) == 0) {
        path += sizeof(WGF_CORE_PRIV_FS_CACHE) - 1; /* the list is the cache's own: paths under it */
    } else if (!wgf_asset_priv_host_is_url()) {
        return; /* not a download: a local host is only ever read */
    }
    snprintf(list, sizeof(list), "%s/" DOWNLOADS_LIST, cache_dir());
    f = fopen(list, "ab");
    if (f == NULL) {
        wgf_log_warn("wgf_asset: couldn't note %s in %s; wgf_asset_clear_cache won't delete it", path, list);
        return;
    }
    fprintf(f, "%s\n", path);
    fclose(f);
}

/* The directories above `root`/`path`, deepest first, removed while they are empty. */
static void remove_empty_parents(const char *root, const char *path)
{
    char full[1024];
    const size_t root_len = strlen(root);
    char *slash;
    if ((size_t)snprintf(full, sizeof(full), "%s/%s", root, path) >= sizeof(full)) return;
    while ((slash = strrchr(full, '/')) != NULL && (size_t)(slash - full) > root_len) {
        *slash = '\0';
        if (!wgf_core_priv_os_rmdir(full)) return; /* not empty: it and what is above it stay */
    }
}

/* Every file the list names deleted, with its metadata, then the list: how many. */
static int delete_downloads(void)
{
    char list[600], line[1024], relative[1024], file[1600], meta_root[600];
    FILE *f;
    int deleted = 0;
    snprintf(list, sizeof(list), "%s/" DOWNLOADS_LIST, cache_dir());
    snprintf(meta_root, sizeof(meta_root), "%s/.meta", cache_dir());
    f = fopen(list, "rb");
    if (f == NULL) return 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t n = strlen(line);
        if (n == 0 || line[n - 1] != '\n') { /* too long to be one of ours: skip the rest */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {
            }
            continue;
        }
        line[--n] = '\0';
        if (n > 0 && line[n - 1] == '\r') line[--n] = '\0';
        if (n == 0) continue;
        if (!wgf_asset_priv_normalize_path(line, relative, sizeof(relative))) {
            wgf_log_warn("wgf_asset_clear_cache: %s lists %s, which isn't under it; left alone", list, line);
            continue;
        }
        if (snprintf(file, sizeof(file), "%s/%s", cache_dir(), relative) >= (int)sizeof(file)) continue;
        deleted += remove(file) == 0 ? 1 : 0; /* listed twice, or evicted since: already gone */
        if (snprintf(file, sizeof(file), "%s/%s", meta_root, relative) < (int)sizeof(file)) remove(file);
        remove_empty_parents(cache_dir(), relative);
        remove_empty_parents(meta_root, relative);
    }
    fclose(f);
    remove(list);
    wgf_core_priv_os_rmdir(meta_root); /* if nothing else was described there */
    return deleted;
}

void wgf_asset_priv_platform_clear(void)
{
    const int deleted = delete_downloads();
    if (deleted > 0) wgf_log_warn("wgf_asset_clear_cache: deleted %d downloaded file(s) from %s", deleted, cache_dir());
}

/* ----------------------------------------------------------- the answers ---- */

/* The program's answers, from whatever thread it answers on, applied at the next update
 * on the main thread, as a browser's fetch reports back. The lock is made once and never
 * destroyed, so an answer coming after libwgf stops finds it and is turned away. */
typedef struct answer_t {
    wgf_handle_t request;
    bool ok;
} answer_t;

static struct {
    wgf_core_priv_mutex_t lock;
    bool live, open;
    answer_t *list;
    int count, capacity;
} answers;

static void open_answers(bool open)
{
    if (!answers.live) {
        wgf_core_priv_mutex_init(&answers.lock);
        answers.live = true;
    }
    wgf_core_priv_mutex_lock(&answers.lock);
    answers.open = open;
    free(answers.list); /* none carries over a restart */
    answers.list = NULL;
    answers.count = answers.capacity = 0;
    wgf_core_priv_mutex_unlock(&answers.lock);
}

bool wgf_asset_fetch_done(wgf_asset_task_t request, bool ok)
{
    bool queued = false;
    if (!answers.live) return false;
    wgf_core_priv_mutex_lock(&answers.lock);
    if (answers.open && answers.count == answers.capacity) {
        const int capacity = answers.capacity > 0 ? answers.capacity * 2 : 16;
        answer_t *grown = (answer_t *)realloc(answers.list, sizeof(*grown) * (size_t)capacity);
        if (grown != NULL) {
            answers.list = grown;
            answers.capacity = capacity;
        }
    }
    if (answers.open && answers.count < answers.capacity) {
        answers.list[answers.count].request = request;
        answers.list[answers.count].ok = ok;
        answers.count++;
        queued = true;
    }
    wgf_core_priv_mutex_unlock(&answers.lock);
    return queued;
}

/* A listed download hashed a slice an update, read from its file a slice at a time, so
 * a large one never stalls a frame or sits whole in memory. */
typedef struct wgf_asset_priv_hashing_t {
    FILE *file;
    wgf_asset_priv_sha256_t sha;
} hashing_t;

/* A download in place and checked: its source recorded, noted for clear_cache, resolved. */
static void downloaded(uint16_t slot)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr->fetch_url[0] != '\0') { /* what a later ensure matches its own source against */
        wgf_core_priv_fs_meta_t meta;
        wgf_core_priv_fs_meta_get(task_ptr->path, &meta);
        snprintf(meta.source, sizeof(meta.source), "%s", task_ptr->fetch_url);
        wgf_core_priv_fs_meta_set(task_ptr->path, &meta);
    }
    note_download(task_ptr->path);
    wgf_asset_priv_task_resolved(slot, true);
}

/* One update's slice of a listed download's hash; the verdict once it is all read. */
static void hash_slice(uint16_t slot)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    hashing_t *job = task_ptr->hashing;
    const double start = wgf_time_get_seconds();
    static unsigned char buffer[HASH_SLICE];
    size_t n = 1;
    while (n > 0 && (wgf_time_get_seconds() - start) * 1000.0 < HASH_SLICE_MS) {
        n = fread(buffer, 1, sizeof(buffer), job->file);
        wgf_asset_priv_sha256_feed(&job->sha, buffer, n);
    }
    if (n > 0) return; /* more next update */
    {
        wgf_core_priv_fs_meta_t meta;
        memset(&meta, 0, sizeof(meta));
        wgf_asset_priv_sha256_end(&job->sha, meta.hash);
        fclose(job->file);
        free(job);
        task_ptr->hashing = NULL;
        if (strcmp(meta.hash, task_ptr->expect_hash) != 0) {
            wgf_log_warn("wgf_asset: %s isn't what the manifest lists (%.19s..., not %.19s...); not kept",
                         task_ptr->path, meta.hash, task_ptr->expect_hash);
            wgf_core_priv_fs_remove(task_ptr->path);
            wgf_asset_priv_task_resolved(slot, false);
            return;
        }
        wgf_core_priv_fs_meta_set(task_ptr->path, &meta);
        downloaded(slot);
    }
}

/* An answer: the download moved into place (or deleted), then checked against the
 * manifest, or the task tries its next path. */
static void apply_answer(wgf_handle_t request, bool ok)
{
    const uint16_t slot = wgf_asset_priv_task_slot(request);
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    char partial[600], dest[1024];
    bool written;
    if (task_ptr == NULL || task_ptr->state != WGF_ASSET_PRIV_FETCHING) return; /* not one waiting on an answer */
    task_ptr->state = WGF_ASSET_PRIV_NEW;
    if (native.out > 0) native.out--; /* room for the next download */
    if (task_ptr->is_ping) {
        task_ptr->ping_ms = ok ? (float)((wgf_time_get_seconds() - task_ptr->fetch_taken_at) * 1000.0) : -1.0f;
        task_ptr->state = WGF_ASSET_PRIV_WAITING; /* answered: the ping's step finishes it */
        return;
    }
    written = wgf_core_priv_fs_partial_path(task_ptr->path, partial, sizeof(partial)) &&
              wgf_core_priv_fs_exists(partial);
    if (ok && !written) {
        wgf_log_warn("wgf_asset: the program said %s downloaded, but wrote nothing", task_ptr->path);
        ok = false;
    } else if (ok && !wgf_core_priv_fs_replace(partial, task_ptr->path)) {
        wgf_log_warn("wgf_asset: couldn't move the download of %s into place", task_ptr->path);
        ok = false;
    } else if (ok) { /* new bytes: what described the old ones goes */
        wgf_core_priv_fs_meta_t none;
        memset(&none, 0, sizeof(none));
        wgf_core_priv_fs_meta_set(task_ptr->path, &none);
    }
    if (!ok && written) wgf_core_priv_fs_remove(partial); /* the copy that was there stays */
    if (written) { /* downloads are the cache's: its .part/ keeps no empty directories */
        const size_t prefix = strncmp(partial, WGF_CORE_PRIV_FS_CACHE, sizeof(WGF_CORE_PRIV_FS_CACHE) - 1) == 0
                                  ? sizeof(WGF_CORE_PRIV_FS_CACHE) - 1
                                  : 0;
        remove_empty_parents(cache_dir(), partial + prefix);
    }
    if (ok && task_ptr->expect_hash[0] != '\0') { /* hashed before it counts, a slice an update */
        hashing_t *job = (hashing_t *)calloc(1, sizeof(*job));
        wgf_core_priv_fs_resolve(task_ptr->path, dest, sizeof(dest));
        if (job == NULL || (job->file = fopen(dest, "rb")) == NULL) {
            free(job);
            wgf_asset_priv_task_resolved(slot, false);
            return;
        }
        wgf_asset_priv_sha256_begin(&job->sha);
        task_ptr->hashing = job;
        task_ptr->state = WGF_ASSET_PRIV_FETCHING; /* hashing, from the next update */
        return;
    }
    if (ok && wgf_core_priv_fs_exists(task_ptr->path)) {
        downloaded(slot);
        return;
    }
    if (!ok && task_ptr->manifest_root && wgf_core_priv_fs_exists(task_ptr->path)) {
        wgf_log_info("wgf_asset: couldn't fetch %s; using the one from before", task_ptr->path);
        wgf_asset_priv_task_resolved(slot, true);
        return;
    }
    wgf_log_warn("wgf_asset: downloading %s failed", task_ptr->path);
    wgf_asset_priv_task_resolved(slot, false); /* the next path, if it has one */
}

void wgf_asset_priv_platform_update(void)
{
    answer_t *list;
    int count;
    if (!answers.live) open_answers(true);
    wgf_core_priv_mutex_lock(&answers.lock);
    list = answers.list;
    count = answers.count;
    answers.list = NULL; /* taken whole: answers arriving meanwhile start a new list */
    answers.count = answers.capacity = 0;
    wgf_core_priv_mutex_unlock(&answers.lock);
    for (int i = 0; i < count; i++) apply_answer(list[i].request, list[i].ok);
    free(list);
    if (native.timeout > 0.0f) { /* taken and never answered: a downloader that dropped it */
        const double now = wgf_time_get_seconds();
        for (uint16_t slot = 1; slot < wgf_asset_priv_task_capacity(); slot++) {
            wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
            if (task_ptr != NULL && task_ptr->state == WGF_ASSET_PRIV_FETCHING && task_ptr->fetch_taken &&
                task_ptr->hashing == NULL && now - task_ptr->fetch_taken_at > native.timeout) {
                wgf_log_warn("wgf_asset: %s: no answer in %.0f s; given up", task_ptr->origin, native.timeout);
                apply_answer(wgf_asset_priv_task_handle(slot), false);
            }
        }
    }
}

/* ------------------------------------------------------------- the steps ---- */

void wgf_asset_priv_platform_step(uint16_t slot)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
    if (task_ptr == NULL) return;
    if (task_ptr->hashing != NULL) {
        hash_slice(slot);
        return;
    }
    if (task_ptr->state != WGF_ASSET_PRIV_NEW) return; /* the program answers with wgf_asset_fetch_done */
    if (!task_ptr->manifest_checked) {
        char hash[WGF_ASSET_PRIV_SHA256_TEXT];
        const int listed = wgf_asset_priv_manifest_lookup(slot, hash);
        task_ptr = wgf_asset_priv_task_at(slot); /* the lookup may have added tasks */
        if (listed == WGF_ASSET_PRIV_LOOKING) return;
        task_ptr->manifest_checked = true;
        if (listed == WGF_ASSET_PRIV_LISTED) memcpy(task_ptr->expect_hash, hash, sizeof(hash));
    }
    /* A local host is only ever read, as a browser only reads its host. A file the caller
       gave a URL of its own lives in the cache under it, as does one a URL redirect would
       download because the host hasn't it; what is there counts only if it came from that
       URL, so a shipped file is never overwritten and an old download never hides it. */
    if (!wgf_asset_priv_host_is_url() && task_ptr->fetch_url[0] != '\0' &&
        strncmp(task_ptr->path, WGF_CORE_PRIV_FS_CACHE, sizeof(WGF_CORE_PRIV_FS_CACHE) - 1) != 0 &&
        (task_ptr->caller_url || !wgf_core_priv_fs_exists(task_ptr->path))) {
        char cached[sizeof(task_ptr->path)];
        if (snprintf(cached, sizeof(cached), WGF_CORE_PRIV_FS_CACHE "%s", task_ptr->path) >= (int)sizeof(cached)) {
            wgf_log_warn("wgf_asset: %s is too long to keep in the cache", task_ptr->path);
            wgf_asset_priv_task_resolved(slot, false);
            return;
        }
        snprintf(task_ptr->path, sizeof(task_ptr->path), "%s", cached);
    }
    {
        const bool have = wgf_core_priv_fs_exists(task_ptr->path);
        const bool forced = (task_ptr->flags & WGF_ASSET_ENSURE_FORCE_FETCH) != 0;
        const bool sourced =
            strncmp(task_ptr->path, WGF_CORE_PRIV_FS_CACHE, sizeof(WGF_CORE_PRIV_FS_CACHE) - 1) == 0;
        /* a file the manifest lists is current when its recorded hash is the listed one;
           the root manifest is fetched once a run, whatever is there */
        bool current = have && !forced && !task_ptr->manifest_root;
        if (current && task_ptr->expect_hash[0] != '\0') {
            wgf_core_priv_fs_meta_t meta;
            current = wgf_core_priv_fs_meta_get(task_ptr->path, &meta) && strcmp(meta.hash, task_ptr->expect_hash) == 0;
        }
        if (current && sourced) { /* downloaded from this URL, not another */
            wgf_core_priv_fs_meta_t meta;
            current = wgf_core_priv_fs_meta_get(task_ptr->path, &meta) && strcmp(meta.source, task_ptr->fetch_url) == 0;
        }
        if (current) {
            wgf_asset_priv_task_resolved(slot, true);
            return;
        }
        if (native.fetching && (wgf_asset_priv_host_is_url() || task_ptr->fetch_url[0] != '\0')) {
            char partial[600], dest[1024];
            if (native.out >= MAX_DOWNLOADS) return; /* waits for a download to finish, as a browser queues them */
            /* the program writes a partial file (.part/), which takes the file's place only
               when it says it worked: a failed or broken-off download never leaves half a
               file where one is read, and never costs the copy that was there */
            if (!wgf_core_priv_fs_partial_path(task_ptr->path, partial, sizeof(partial))) {
                wgf_log_warn("wgf_asset: %s is too long to download", task_ptr->path);
                wgf_asset_priv_task_resolved(slot, false);
                return;
            }
            wgf_core_priv_fs_resolve(partial, dest, sizeof(dest));
            wgf_core_priv_fs_make_parents(partial);         /* the program only has to write */
            wgf_core_priv_fs_make_parents(task_ptr->path);  /* and the file has somewhere to go */
            remove(dest);                                   /* what a broken-off run left */
            task_ptr->state = WGF_ASSET_PRIV_FETCHING;      /* a request, for wgf_asset_fetch_next */
            task_ptr->fetch_taken = false;
            task_ptr->fetch_order = ++native.order;
            native.out++;
            return;
        }
        if (task_ptr->fetch_url[0] != '\0' && (sourced || !have)) {
            wgf_log_warn("wgf_asset: %s would be downloaded from %s; natively that needs wgf_asset_set_fetching",
                         task_ptr->origin, task_ptr->fetch_url);
            wgf_asset_priv_task_resolved(slot, false);
            return;
        }
        if (!have && wgf_asset_priv_host_is_url() && !native.fetching) {
            wgf_log_warn("wgf_asset: %s would be downloaded from %s; natively that needs wgf_asset_set_fetching",
                         task_ptr->origin, wgf_asset_priv_host());
        }
        wgf_asset_priv_task_resolved(slot, have);
    }
}

/* Once its first byte is written, not when the program takes it: a request that fails
 * before any byte (a path rule's candidate missing, a mod's file over a CDN) falls through
 * to the next candidate, as a file missing under a path rule should. */
bool wgf_asset_priv_platform_arriving(const wgf_asset_priv_task_t *task_ptr)
{
    char partial[600];
    unsigned char first;
    return task_ptr->state == WGF_ASSET_PRIV_FETCHING && task_ptr->fetch_taken && task_ptr->hashing == NULL &&
           !task_ptr->is_ping && wgf_core_priv_fs_partial_path(task_ptr->path, partial, sizeof(partial)) &&
           wgf_core_priv_fs_read_at(partial, 0, &first, 1) == 1;
}

void wgf_asset_priv_platform_task_freed(wgf_asset_priv_task_t *task_ptr, wgf_handle_t handle)
{
    (void)handle;
    if (task_ptr->hashing != NULL) {
        fclose(task_ptr->hashing->file);
        free(task_ptr->hashing);
        task_ptr->hashing = NULL;
    }
    if (task_ptr->state == WGF_ASSET_PRIV_FETCHING && native.out > 0) native.out--; /* its answer is turned away */
}

/* ----------------------------------------------------------------- pings ---- */

void wgf_asset_priv_platform_ping(wgf_asset_priv_task_t *task_ptr, const char *host, int timeout_ms)
{
    char dir[512];
    (void)timeout_ms;
    snprintf(dir, sizeof(dir), "%s", host);
    if (strncmp(host, "file:", 5) == 0 && !wgf_asset_priv_file_url_path(host, dir, sizeof(dir))) {
        task_ptr->ping_ms = -1.0f; /* not a directory on this machine */
    } else if (strstr(host, "://") != NULL && strncmp(host, "file:", 5) != 0) {
        if (!native.fetching) {
            wgf_log_warn("wgf_asset_ping_host: %s: natively a URL host is pinged by the program's downloader "
                         "(wgf_asset_set_fetching)",
                         host);
            task_ptr->ping_ms = -1.0f;
            return;
        }
        snprintf(task_ptr->fetch_url, sizeof(task_ptr->fetch_url), "%s/", host); /* the host's root; any answer counts */
        task_ptr->state = WGF_ASSET_PRIV_FETCHING; /* a ping request, for wgf_asset_fetch_next */
        task_ptr->fetch_taken = false;
        task_ptr->fetch_order = ++native.order;
        task_ptr->fetch_taken_at = wgf_time_get_seconds();
        task_ptr->ping_ms = -2.0f;
        native.out++;
    } else {
        task_ptr->ping_ms = wgf_core_priv_os_is_dir(dir[0] != '\0' ? dir : ".") ? 0.0f : -1.0f;
    }
}

float wgf_asset_priv_platform_ping_poll(wgf_asset_priv_task_t *task_ptr)
{
    if (task_ptr->state == WGF_ASSET_PRIV_FETCHING) return -2.0f; /* not answered yet */
    return task_ptr->ping_ms;
}

/* ---------------------------------------------------------- the public API ---- */

bool wgf_asset_set_cache_dir(const char *dir)
{
    wgf_asset_priv_install();
    if (dir == NULL || dir[0] == '\0' || strlen(dir) >= sizeof(native.cache_dir)) {
        wgf_log_warn("wgf_asset_set_cache_dir: a directory (under 512 bytes) is needed");
        return false;
    }
    snprintf(native.cache_dir, sizeof(native.cache_dir), "%s", dir);
    wgf_asset_priv_platform_host_set(wgf_asset_priv_host_is_url());
    return true;
}

const char *wgf_asset_get_cache_dir(void)
{
    wgf_asset_priv_install();
    return cache_dir();
}

bool wgf_asset_set_fetching(bool enabled)
{
    wgf_asset_priv_install();
    native.fetching = enabled;
    if (!enabled) { /* nobody will take these now: they fail at the next update */
        for (uint16_t slot = 1; slot < wgf_asset_priv_task_capacity(); slot++) {
            const wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
            if (task_ptr != NULL && task_ptr->state == WGF_ASSET_PRIV_FETCHING && !task_ptr->fetch_taken &&
                task_ptr->hashing == NULL) {
                wgf_asset_fetch_done(wgf_asset_priv_task_handle(slot), false);
            }
        }
    }
    return true;
}

bool wgf_asset_is_fetching(void)
{
    return native.fetching;
}

/* A request waiting on the program's answer, or NULL. */
static wgf_asset_priv_task_t *request_of(wgf_handle_t request)
{
    wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(wgf_asset_priv_task_slot(request));
    return task_ptr != NULL && task_ptr->state == WGF_ASSET_PRIV_FETCHING && task_ptr->hashing == NULL ? task_ptr
                                                                                                       : NULL;
}

wgf_asset_task_t wgf_asset_fetch_next(void)
{
    uint16_t next = 0;
    wgf_asset_priv_task_t *next_ptr = NULL;
    for (uint16_t slot = 1; slot < wgf_asset_priv_task_capacity(); slot++) {
        wgf_asset_priv_task_t *task_ptr = wgf_asset_priv_task_at(slot);
        if (task_ptr != NULL && task_ptr->state == WGF_ASSET_PRIV_FETCHING && !task_ptr->fetch_taken &&
            task_ptr->hashing == NULL && (next_ptr == NULL || task_ptr->fetch_order < next_ptr->fetch_order)) {
            next = slot;
            next_ptr = task_ptr;
        }
    }
    if (next_ptr == NULL) return 0;
    next_ptr->fetch_taken = true;
    if (!next_ptr->is_ping) next_ptr->fetch_taken_at = wgf_time_get_seconds();
    return wgf_asset_priv_task_handle(next);
}

const char *wgf_asset_fetch_get_url(wgf_asset_task_t request)
{
    static char url[WGF_ASSET_PRIV_URL_MAX];
    const wgf_asset_priv_task_t *task_ptr = request_of(request);
    if (task_ptr == NULL) return "";
    if (task_ptr->fetch_url[0] != '\0') { /* its own source wins, as on the web */
        snprintf(url, sizeof(url), "%s", task_ptr->fetch_url);
    } else {
        if (snprintf(url, sizeof(url), "%s/%s", wgf_asset_priv_host(), task_ptr->path) >= (int)sizeof(url)) {
            return ""; /* too long a URL to hand out */
        }
    }
    return url;
}

const char *wgf_asset_fetch_get_dest(wgf_asset_task_t request)
{
    static char dest[1024];
    char partial[600];
    const wgf_asset_priv_task_t *task_ptr = request_of(request);
    if (task_ptr == NULL || task_ptr->is_ping || !wgf_core_priv_fs_partial_path(task_ptr->path, partial, sizeof(partial))) {
        return "";
    }
    wgf_core_priv_fs_resolve(partial, dest, sizeof(dest));
    return dest;
}

bool wgf_asset_fetch_is_ping(wgf_asset_task_t request)
{
    const wgf_asset_priv_task_t *task_ptr = request_of(request);
    return task_ptr != NULL && task_ptr->is_ping;
}

bool wgf_asset_set_fetch_timeout(float seconds)
{
    wgf_asset_priv_install();
    if (!(seconds >= 0.0f) || seconds > 1e9f) return false; /* negative, or not finite */
    native.timeout = seconds;
    return true;
}

float wgf_asset_get_fetch_timeout(void)
{
    return native.timeout;
}
