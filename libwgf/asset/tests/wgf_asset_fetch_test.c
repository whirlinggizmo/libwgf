#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_asset.h"
#include "wgf_asset_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"
#include "wgf_core_thread_priv.h"
#include "wgf_time.h"

/* The program's downloads, natively (wgf_asset_set_fetching): wgrender's fetcher tests,
 * ported (a fetch hook, a host only ever read, a broken download, answers from another
 * thread, six out at once), and libwgf's own: answers out of order, one never answered
 * (and the timeout that gives it up), a ping through the fetcher, and manifests served
 * from a directory. No network: a test fetcher writes what a downloader would. Files
 * under asset_fetch_test/. */

#define TEST_DIR "asset_fetch_test"

static int failures;

#define CHECK(x)                                                                                                     \
    do {                                                                                                             \
        if (!(x)) {                                                                                                  \
            printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #x);                                                    \
            failures++;                                                                                              \
        }                                                                                                            \
    } while (0)

/* A program's downloader: handed each request, as a program's loop would. */
typedef void (*fetcher_t)(wgf_handle_t request, const char *url, const char *dest, void *user);
static fetcher_t fetcher;
static void *fetcher_user;

/* An update, then the requests it made handed to the fetcher. */
static void tick(void)
{
    wgf_handle_t request;
    wgf_core_priv_update();
    while (fetcher != NULL && (request = wgf_asset_fetch_next()) != 0) {
        fetcher(request, wgf_asset_fetch_get_url(request), wgf_asset_fetch_get_dest(request), fetcher_user);
    }
}

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return;
    fputs(text, f);
    fclose(f);
}

static bool read_back(const char *path, const char *want)
{
    char got[64] = "";
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    if (fgets(got, sizeof(got), f) == NULL) got[0] = '\0';
    fclose(f);
    return strcmp(got, want) == 0;
}

/* Ensure a file and tick until it finishes: DONE or FAILED, and its path. */
static int ready_count, failed_count;
static char completed_path[1024];
static void ensure_and_tick(const char *key, const char *source, unsigned flags)
{
    const wgf_handle_t task = wgf_asset_ensure(key, source, flags);
    for (int i = 0; i < 200 && wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_PENDING; i++) tick();
    ready_count = wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_DONE;
    failed_count = wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_FAILED;
    snprintf(completed_path, sizeof(completed_path), "%s", wgf_asset_task_get_path(task));
    wgf_asset_task_destroy(task);
}

/* --------------------------------------------------------------- fetchers ---- */

static int fetch_calls;
static char fetched_url[1024];

/* Writes "fetched" where it is told, and answers as `*user` says. */
static void test_fetcher(wgf_handle_t request, const char *url, const char *dest, void *user)
{
    FILE *f;
    fetch_calls++;
    snprintf(fetched_url, sizeof(fetched_url), "%s", url);
    f = fopen(dest, "wb");
    if (f != NULL) {
        fputs("fetched", f);
        fclose(f);
    }
    wgf_asset_fetch_done(request, f != NULL && *(const bool *)user);
}

/* Serves files from a directory: the URL's path past the host, read from `user`. */
static void directory_fetcher(wgf_handle_t request, const char *url, const char *dest, void *user)
{
    const char *path = strstr(url, "example.com/game/");
    char source[1024];
    FILE *in, *out;
    bool ok = false;
    fetch_calls++;
    snprintf(fetched_url, sizeof(fetched_url), "%s", url);
    if (wgf_asset_fetch_is_ping(request)) {
        CHECK(dest[0] == '\0'); /* a ping wants no file */
        wgf_asset_fetch_done(request, true);
        return;
    }
    if (path != NULL && snprintf(source, sizeof(source), "%s/%s", (const char *)user, path + strlen("example.com/game/")) <
                            (int)sizeof(source) &&
        (in = fopen(source, "rb")) != NULL) {
        if ((out = fopen(dest, "wb")) != NULL) {
            char buffer[4096];
            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) fwrite(buffer, 1, n, out);
            fclose(out);
            ok = true;
        }
        fclose(in);
    }
    wgf_asset_fetch_done(request, ok); /* a 404: false */
}

/* Requests held, to answer later. */
static wgf_handle_t held[16];
static int held_count;
static void holding_fetcher(wgf_handle_t request, const char *url, const char *dest, void *user)
{
    (void)url;
    (void)user;
    if (dest[0] != '\0') write_text(dest, "fetched");
    if (held_count < 16) held[held_count++] = request;
}

/* --------------------------------------------------------------- the tests ---- */

static void test_fetch_hook(void)
{
    bool succeed = true;
    fetch_calls = 0;
    CHECK(wgf_asset_set_cache_dir(TEST_DIR "/cache"));
    CHECK(strcmp(wgf_asset_get_cache_dir(), TEST_DIR "/cache") == 0);
    CHECK(!wgf_asset_set_cache_dir("") && !wgf_asset_set_cache_dir(NULL));
    CHECK(!wgf_asset_is_fetching() && wgf_asset_set_fetching(true) && wgf_asset_is_fetching());
    fetcher = test_fetcher;
    fetcher_user = &succeed;

    /* a local directory host behaves as it always has: the fetcher isn't asked */
    wgf_asset_set_host(TEST_DIR "/cache");
    ensure_and_tick("nothing/here.bin", NULL, WGF_ASSET_ENSURE_NONE);
    CHECK(fetch_calls == 0 && failed_count == 1);

    /* a URL host makes the same miss a download, and the task has a local path */
    remove(TEST_DIR "/cache/textures/rock.png");
    wgf_asset_set_host("https://assets.example.com/game");
    ensure_and_tick("textures/rock.png", NULL, WGF_ASSET_ENSURE_NONE);
    CHECK(fetch_calls == 1 && ready_count == 1);
    CHECK(strcmp(fetched_url, "https://assets.example.com/game/textures/rock.png") == 0);
    CHECK(read_back(TEST_DIR "/cache/textures/rock.png", "fetched")); /* it landed in the cache directory */

    /* cached now: the next ensure resolves without asking the fetcher again */
    ensure_and_tick("textures/rock.png", NULL, WGF_ASSET_ENSURE_NONE);
    CHECK(fetch_calls == 1 && ready_count == 1);

    /* a fetcher that says it failed fails the task rather than hanging it */
    succeed = false;
    remove(TEST_DIR "/cache/textures/rock.png");
    ensure_and_tick("textures/rock.png", NULL, WGF_ASSET_ENSURE_FORCE_FETCH);
    CHECK(fetch_calls == 2 && failed_count == 1);

    /* a cached copy can be dropped, so the next ensure fetches again */
    succeed = true;
    fetch_calls = 0;
    CHECK(!wgf_asset_evict("textures/rock.png")); /* a failed download leaves nothing behind */
    ensure_and_tick("textures/rock.png", NULL, WGF_ASSET_ENSURE_NONE);
    CHECK(fetch_calls == 1 && read_back(TEST_DIR "/cache/textures/rock.png", "fetched"));
    CHECK(wgf_asset_evict("textures/rock.png") && !read_back(TEST_DIR "/cache/textures/rock.png", "fetched"));
    ensure_and_tick("textures/rock.png", NULL, WGF_ASSET_ENSURE_NONE);
    CHECK(fetch_calls == 2);

    /* a fetch_url is the download's source, used as it is, the file kept under its key */
    fetch_calls = 0;
    CHECK(wgf_asset_evict("textures/rock.png"));
    ensure_and_tick("textures/rock.png", "https://mirror.example.net/signed/rock.png?sig=1", WGF_ASSET_ENSURE_NONE);
    CHECK(fetch_calls == 1 && strcmp(fetched_url, "https://mirror.example.net/signed/rock.png?sig=1") == 0);
    CHECK(read_back(TEST_DIR "/cache/textures/rock.png", "fetched"));

    /* a "://" redirect target is a download source too */
    CHECK(wgf_asset_add_redirect("textures/", "https://cdn.example.com/hd/textures/"));
    fetch_calls = 0;
    CHECK(wgf_asset_evict("textures/rock.png"));
    ensure_and_tick("textures/rock.png", NULL, WGF_ASSET_ENSURE_NONE);
    CHECK(fetch_calls == 1 && strcmp(fetched_url, "https://cdn.example.com/hd/textures/rock.png") == 0);
    wgf_asset_clear_redirects();

    /* clearing deletes what was downloaded into the cache directory, by its list */
    wgf_asset_clear_cache();
    CHECK(!read_back(TEST_DIR "/cache/textures/rock.png", "fetched"));
    CHECK(!wgf_asset_evict("textures/rock.png"));
    fetcher = NULL;
}

/* A local host is only ever read: what a task downloads from a URL of its own lands in
 * the cache, and counts there only for that URL. */
static void test_readonly_host(void)
{
    bool succeed = true;
    const char *shipped = TEST_DIR "/ro/assets/textures/rock.png";
    const char *cached = TEST_DIR "/ro/cache/textures/rock.png";
    CHECK(wgf_asset_set_cache_dir(TEST_DIR "/ro/cache"));
    wgf_asset_set_host(TEST_DIR "/ro/assets");
    wgf_core_priv_fs_make_parents("textures/rock.png");
    write_text(shipped, "shipped");
    remove(cached);
    fetcher = test_fetcher;
    fetcher_user = &succeed;
    fetch_calls = 0;

    ensure_and_tick("textures/rock.png", "https://mirror.example.net/rock.png", 0);
    CHECK(fetch_calls == 1 && ready_count == 1);
    CHECK(read_back(cached, "fetched") && read_back(shipped, "shipped"));
    ensure_and_tick("textures/rock.png", "https://mirror.example.net/rock.png", 0);
    CHECK(fetch_calls == 1 && ready_count == 1); /* the same source: the cached copy */
    ensure_and_tick("textures/rock.png", "https://other.example.net/rock.png", 0);
    CHECK(fetch_calls == 2 && ready_count == 1); /* another source: that copy isn't this one */
    ensure_and_tick("textures/rock.png", NULL, 0);
    CHECK(fetch_calls == 2 && ready_count == 1 && strstr(completed_path, "ro/assets/textures/rock.png") != NULL);
    CHECK(wgf_asset_evict("textures/rock.png") && read_back(shipped, "shipped"));
    ensure_and_tick("textures/rock.png", "https://mirror.example.net/rock.png", 0);
    wgf_asset_clear_cache();
    CHECK(!read_back(cached, "fetched") && read_back(shipped, "shipped"));

    /* a URL redirect downloads only what the host hasn't, into the cache */
    CHECK(wgf_asset_add_redirect("textures/", "https://cdn.example.com/textures/"));
    fetch_calls = 0;
    ensure_and_tick("textures/rock.png", NULL, 0);
    CHECK(fetch_calls == 0 && ready_count == 1);
    ensure_and_tick("textures/missing.png", NULL, 0);
    CHECK(fetch_calls == 1 && ready_count == 1 && read_back(TEST_DIR "/ro/cache/textures/missing.png", "fetched"));
    wgf_asset_clear_redirects();

    /* not fetching: a URL of its own can't be had, and the host's file isn't it */
    CHECK(wgf_asset_set_fetching(false));
    ensure_and_tick("textures/rock.png", "https://mirror.example.net/rock.png", 0);
    CHECK(ready_count == 0 && failed_count == 1);
    CHECK(wgf_asset_set_fetching(true));
    fetcher = NULL;
}

/* A download is written apart (.part/) and takes the file's place only when the fetcher
 * says it worked. */
enum { WRITE_AND_SUCCEED, WRITE_NEWER_AND_SUCCEED, WRITE_HALF_AND_FAIL, SUCCEED_WITHOUT_WRITING };
static int broken_mode;
static char broken_dest[1100];
static void broken_fetcher(wgf_handle_t request, const char *url, const char *dest, void *user)
{
    (void)url;
    (void)user;
    fetch_calls++;
    snprintf(broken_dest, sizeof(broken_dest), "%s", dest);
    if (broken_mode != SUCCEED_WITHOUT_WRITING) {
        write_text(dest, broken_mode == WRITE_AND_SUCCEED ? "fetched" : broken_mode == WRITE_NEWER_AND_SUCCEED ? "newer"
                                                                                                                : "hal");
    }
    wgf_asset_fetch_done(request, broken_mode != WRITE_HALF_AND_FAIL);
}

static void test_broken_download(void)
{
    const char *final = TEST_DIR "/broken/textures/rock.png";
    const char *partial = TEST_DIR "/broken/.part/textures/rock.png";
    CHECK(wgf_asset_set_cache_dir(TEST_DIR "/broken"));
    wgf_asset_set_host("https://assets.example.com/game");
    fetcher = broken_fetcher;
    remove(final);
    fetch_calls = 0;

    broken_mode = WRITE_AND_SUCCEED;
    ensure_and_tick("textures/rock.png", NULL, 0);
    CHECK(fetch_calls == 1 && ready_count == 1 && strstr(broken_dest, "/.part/") != NULL);
    CHECK(read_back(final, "fetched") && !read_back(partial, "fetched")); /* moved, not copied */
    broken_mode = WRITE_HALF_AND_FAIL;
    ensure_and_tick("textures/rock.png", NULL, WGF_ASSET_ENSURE_FORCE_FETCH);
    CHECK(fetch_calls == 2 && failed_count == 1 && read_back(final, "fetched") && !read_back(partial, "hal"));
    broken_mode = SUCCEED_WITHOUT_WRITING;
    ensure_and_tick("textures/rock.png", NULL, WGF_ASSET_ENSURE_FORCE_FETCH);
    CHECK(fetch_calls == 3 && failed_count == 1 && read_back(final, "fetched"));
    wgf_core_priv_fs_make_parents(WGF_CORE_PRIV_FS_CACHE ".part/textures/rock.png");
    write_text(partial, "junk"); /* what a broken-off run left is never taken for a download */
    ensure_and_tick("textures/rock.png", NULL, WGF_ASSET_ENSURE_FORCE_FETCH);
    CHECK(fetch_calls == 4 && failed_count == 1 && read_back(final, "fetched") && !read_back(partial, "junk"));
    broken_mode = WRITE_NEWER_AND_SUCCEED;
    ensure_and_tick("textures/rock.png", NULL, WGF_ASSET_ENSURE_FORCE_FETCH);
    CHECK(fetch_calls == 5 && ready_count == 1 && read_back(final, "newer")); /* replaced, on Windows too */
    fetcher = NULL;
}

/* Answered from a thread of the fetcher's own, as a real downloader's would be. */
typedef struct async_download_t {
    wgf_handle_t request;
    char dest[1100];
} async_download_t;
static async_download_t async_download;
static wgf_core_priv_thread_t async_thread;

static void async_worker(void *arg)
{
    const async_download_t *d = (const async_download_t *)arg;
    write_text(d->dest, "fetched");
    wgf_asset_fetch_done(d->request, true); /* from this thread, not the main one */
}

static void threaded_fetcher(wgf_handle_t request, const char *url, const char *dest, void *user)
{
    (void)url;
    (void)user;
    fetch_calls++;
    async_download.request = request;
    snprintf(async_download.dest, sizeof(async_download.dest), "%s", dest);
    CHECK(wgf_core_priv_thread_create(&async_thread, async_worker, &async_download));
}

static int count_done(const wgf_handle_t *tasks, int count)
{
    int done = 0;
    for (int i = 0; i < count; i++) done += wgf_asset_task_get_status(tasks[i]) == WGF_ASSET_TASK_STATUS_DONE;
    return done;
}

static void test_async_and_order(void)
{
    char key[64];
    wgf_handle_t tasks[10], task;
    CHECK(wgf_asset_set_cache_dir(TEST_DIR "/async"));
    wgf_asset_set_host("https://assets.example.com/game");

    /* answered from another thread, taken at the next update */
    fetcher = threaded_fetcher;
    remove(TEST_DIR "/async/textures/rock.png");
    fetch_calls = 0;
    task = wgf_asset_ensure("textures/rock.png", NULL, 0);
    tick();
    CHECK(fetch_calls == 1);
    wgf_core_priv_thread_join(&async_thread);
    CHECK(wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_PENDING);
    for (int i = 0; i < 4 && wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_PENDING; i++) tick();
    CHECK(wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_DONE);
    wgf_asset_task_destroy(task);

    /* six out at once, the rest waiting; answered out of order, newest first */
    fetcher = holding_fetcher;
    held_count = 0;
    for (int n = 0; n < 10; n++) {
        char cached[160];
        snprintf(key, sizeof(key), "many/%d.bin", n);
        snprintf(cached, sizeof(cached), TEST_DIR "/async/%s", key);
        remove(cached);
        tasks[n] = wgf_asset_ensure(key, NULL, 0);
    }
    for (int i = 0; i < 3; i++) tick();
    CHECK(held_count == 6);
    CHECK(wgf_asset_fetch_done(held[5], true) && wgf_asset_fetch_done(held[2], true));
    for (int i = 0; i < 3; i++) tick();
    CHECK(held_count == 8 && count_done(tasks, 10) == 2); /* two answered: two more out */
    CHECK(wgf_asset_task_get_status(tasks[5]) == WGF_ASSET_TASK_STATUS_DONE &&
          wgf_asset_task_get_status(tasks[2]) == WGF_ASSET_TASK_STATUS_DONE &&
          wgf_asset_task_get_status(tasks[0]) == WGF_ASSET_TASK_STATUS_PENDING);
    for (int n = 7; n >= 0; n--) {
        if (n != 5 && n != 2) wgf_asset_fetch_done(held[n], true);
    }
    for (int i = 0; i < 3; i++) tick();
    CHECK(held_count == 10);
    wgf_asset_fetch_done(held[9], true);
    wgf_asset_fetch_done(held[8], true);
    for (int i = 0; i < 3; i++) tick();
    CHECK(count_done(tasks, 10) == 10);
    CHECK(wgf_asset_fetch_done(held[0], true)); /* answered twice: queued, then turned away at the update */
    tick();
    CHECK(count_done(tasks, 10) == 10);
    for (int n = 0; n < 10; n++) wgf_asset_task_destroy(tasks[n]);
    fetcher = NULL;
}

/* A request taken and never answered: PENDING for good by default (a downloader that
 * drops one is a bug that shows), FAILED past the timeout when one is set. */
static void test_never_answered(void)
{
    wgf_handle_t task;
    CHECK(wgf_asset_set_cache_dir(TEST_DIR "/silent"));
    wgf_asset_set_host("https://assets.example.com/game");
    remove(TEST_DIR "/silent/textures/rock.png");
    CHECK(wgf_asset_get_fetch_timeout() == 0.0f);
    CHECK(!wgf_asset_set_fetch_timeout(-1.0f) && !wgf_asset_set_fetch_timeout(INFINITY));
    fetcher = holding_fetcher;
    held_count = 0;
    task = wgf_asset_ensure("textures/rock.png", NULL, 0);
    for (int i = 0; i < 3; i++) tick();
    wgf_core_priv_os_sleep(0.3);
    for (int i = 0; i < 3; i++) tick();
    CHECK(held_count == 1 && wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_PENDING); /* no timeout: waits */
    CHECK(wgf_asset_set_fetch_timeout(0.2f) && wgf_asset_get_fetch_timeout() == 0.2f);
    tick();
    CHECK(wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_FAILED); /* taken 0.3 s ago: given up */
    CHECK(!read_back(TEST_DIR "/silent/textures/rock.png", "fetched"));       /* the partial file isn't kept */
    CHECK(wgf_asset_fetch_done(held[0], true));                               /* a late answer: turned away */
    tick();
    CHECK(!read_back(TEST_DIR "/silent/textures/rock.png", "fetched"));
    wgf_asset_task_destroy(task);
    CHECK(wgf_asset_set_fetch_timeout(0.0f));
    fetcher = NULL;
}

/* A URL host pinged through the fetcher: a request with no destination, its answer
 * whether the host replied. */
static void test_ping(void)
{
    wgf_handle_t ping;
    wgf_asset_set_host("https://assets.example.com/game");
    fetcher = directory_fetcher;
    fetcher_user = TEST_DIR "/served";
    fetch_calls = 0;
    ping = wgf_asset_ping_host(NULL, 0);
    for (int i = 0; i < 4 && wgf_asset_task_get_status(ping) == WGF_ASSET_TASK_STATUS_PENDING; i++) tick();
    CHECK(fetch_calls == 1 && strcmp(fetched_url, "https://assets.example.com/game/") == 0);
    CHECK(wgf_asset_task_get_status(ping) == WGF_ASSET_TASK_STATUS_DONE && wgf_asset_ping_get_milliseconds(ping) >= 0.0f);
    wgf_asset_task_destroy(ping);
    CHECK(wgf_asset_set_fetching(false));
    ping = wgf_asset_ping_host(NULL, 0);
    tick();
    CHECK(wgf_asset_task_get_status(ping) == WGF_ASSET_TASK_STATUS_FAILED); /* nobody to ask */
    wgf_asset_task_destroy(ping);
    CHECK(wgf_asset_set_fetching(true));
    fetcher = NULL;
}

/* Manifests natively: a URL host and the program's downloads. A listed file whose
 * recorded hash matches is used without a request; one that doesn't match what the
 * manifest lists is refused; only the root is asked about again. */
static void write_manifest(const char *dir, const char *files_json, const char *dirs_json)
{
    char path[512], text[2048];
    snprintf(path, sizeof(path), "%s/manifest.json", dir);
    snprintf(text, sizeof(text), "{\"wgf_manifest\": 1, \"files\": {%s}, \"dirs\": {%s}}", files_json, dirs_json);
    write_text(path, text);
}

static void hash_file(const char *path, char out[WGF_ASSET_PRIV_SHA256_TEXT])
{
    unsigned char data[4096];
    size_t n = 0;
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        n = fread(data, 1, sizeof(data), f);
        fclose(f);
    }
    wgf_asset_priv_sha256_text(data, n, out);
}

static void test_manifest(void)
{
    char tiles[WGF_ASSET_PRIV_SHA256_TEXT], dir_manifest[WGF_ASSET_PRIV_SHA256_TEXT], entry[256];
    const char *served = TEST_DIR "/served";
    wgf_core_priv_os_mkdir(TEST_DIR "/served");
    wgf_core_priv_os_mkdir(TEST_DIR "/served/textures");
    write_text(TEST_DIR "/served/textures/tiles.png", "tiles v1");
    hash_file(TEST_DIR "/served/textures/tiles.png", tiles);
    snprintf(entry, sizeof(entry), "\"tiles.png\": \"%s\"", tiles);
    write_manifest(TEST_DIR "/served/textures", entry, "");
    hash_file(TEST_DIR "/served/textures/manifest.json", dir_manifest);
    snprintf(entry, sizeof(entry), "\"textures\": \"%s\"", dir_manifest);
    write_manifest(served, "", entry);

    CHECK(wgf_asset_set_cache_dir(TEST_DIR "/listed"));
    wgf_asset_set_host("https://assets.example.com/game");
    CHECK(wgf_asset_set_manifest("manifest.json"));
    fetcher = directory_fetcher;
    fetcher_user = (void *)served;
    fetch_calls = 0;
    ensure_and_tick("textures/tiles.png", NULL, 0);
    CHECK(ready_count == 1 && fetch_calls == 3); /* the root, the directory's, the file */
    CHECK(read_back(TEST_DIR "/listed/textures/tiles.png", "tiles v1"));
    ensure_and_tick("textures/tiles.png", NULL, 0);
    CHECK(ready_count == 1 && fetch_calls == 3); /* listed and matching: no request */

    /* a new run: only the root asked about */
    CHECK(wgf_asset_set_manifest("manifest.json"));
    ensure_and_tick("textures/tiles.png", NULL, 0);
    CHECK(ready_count == 1 && fetch_calls == 4);

    /* a stale host: the manifest lists v2, the host still serves v1 -- refused */
    write_text(TEST_DIR "/served/textures/tiles.png", "tiles v2");
    hash_file(TEST_DIR "/served/textures/tiles.png", tiles);
    write_text(TEST_DIR "/served/textures/tiles.png", "tiles v1");
    snprintf(entry, sizeof(entry), "\"tiles.png\": \"%s\"", tiles);
    write_manifest(TEST_DIR "/served/textures", entry, "");
    hash_file(TEST_DIR "/served/textures/manifest.json", dir_manifest);
    snprintf(entry, sizeof(entry), "\"textures\": \"%s\"", dir_manifest);
    write_manifest(served, "", entry);
    CHECK(wgf_asset_set_manifest("manifest.json"));
    fetch_calls = 0;
    ensure_and_tick("textures/tiles.png", NULL, 0);
    CHECK(failed_count == 1 && fetch_calls == 3); /* the root, the directory's, the file, refused */
    CHECK(!read_back(TEST_DIR "/listed/textures/tiles.png", "tiles v1")); /* not kept */

    /* caught up: the host serves v2 */
    write_text(TEST_DIR "/served/textures/tiles.png", "tiles v2");
    ensure_and_tick("textures/tiles.png", NULL, 0);
    CHECK(ready_count == 1 && read_back(TEST_DIR "/listed/textures/tiles.png", "tiles v2"));
    CHECK(wgf_asset_set_manifest(NULL));
    fetcher = NULL;
}

int main(void)
{
    char exe_dir[1024];
    /* the files this writes by hand and the ones fs writes must meet: fs's root is the
       executable's directory, so work from there (MSVC runs a test from the directory
       above its Debug/) */
    if (wgf_core_priv_os_executable_dir(exe_dir, sizeof(exe_dir))) wgf_core_priv_os_chdir(exe_dir);
    wgf_core_priv_init();
    wgf_core_priv_os_remove_tree(TEST_DIR);
    wgf_core_priv_os_mkdir(TEST_DIR);
    wgf_core_priv_os_mkdir(TEST_DIR "/served");
    test_fetch_hook();
    test_readonly_host();
    test_broken_download();
    test_async_and_order();
    test_never_answered();
    test_ping();
    test_manifest();
    wgf_asset_set_fetching(false);
    wgf_core_priv_shutdown();
    wgf_asset_fetch_done(held[0], true); /* after shutdown: turned away, not a crash (asan watches) */
    wgf_core_priv_os_remove_tree(TEST_DIR);
    printf("wgf_asset_fetch_test: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
