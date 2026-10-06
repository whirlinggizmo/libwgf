#ifndef WGF_ASSET_H
#define WGF_ASSET_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An asset task: an ensure, a group, a ping, or a download the program answers (a
 * fetch request); a handle of kind "asset.task". */
typedef wgf_handle_t wgf_asset_task_t;

/* Assets: where a resource's file comes from. A path is an asset: creating a resource
 * from a path (wgf_texture_create, wgf_font_create, wgf_sound_create, a scene's) goes
 * through here, as does any call below, and that is what links it -- a program that
 * only makes generated content links none of it. A relative path is looked for under
 * the asset host, which needs no setting:
 *
 *   natively  the host is the program's own directory (where its executable is,
 *             wherever it was started from), the file storage's root (wgf_fs.h):
 *             files are read where they are, as without this layer
 *   web       the host is the page's own directory (its document's base URL, so a
 *             <base href> moves it): a file not bundled into the page and not cached
 *             is fetched from beside the page, cached in the browser's storage, and
 *             checked again on later visits as the cache mode says
 *
 * so a web page loads its files with no call here at all, and one that should never
 * fetch bundles what it loads. Set another host to fetch from elsewhere (a CDN), or
 * natively to download (fetching, below). A path that is a URL ("https://...") is
 * fetched from there, cached under its own name; natively that needs fetching turned
 * on, and without it the load fails, logged with the URL.
 *
 * A resource makes its own file local as it loads (wgf_resource.h), so a program only
 * ENSURES a file to have it local without loading it: to fetch ahead (a level's files
 * during a menu), from an explicit source (a fetch_url), or to read it itself. An
 * ensure is a task: a handle whose status, local path and progress the program reads,
 * nothing called back, destroyed when done with. A path ensured from an explicit
 * source is what a later create of that path loads, wherever it was found.
 *
 * A format whose files name other files is ensured with them, when the layer that
 * reads it registers how to list them (core's lister, wgf_core_load_priv.h): the task
 * is DONE once all are local. None of libwgf's formats names others yet.
 *
 * libwgt's asset layer, carried whole (docs/HISTORY.md). */

typedef enum wgf_asset_task_status_t {
    WGF_ASSET_TASK_STATUS_NONE = 0, /* not a task (or one destroyed) */
    WGF_ASSET_TASK_STATUS_PENDING = 1,
    WGF_ASSET_TASK_STATUS_DONE = 2, /* the file is local, and every file it names */
    WGF_ASSET_TASK_STATUS_FAILED = 3
} wgf_asset_task_status_t;

/* wgf_asset_ensure's flags, or'd. */
enum {
    WGF_ASSET_ENSURE_NONE = 0,
    /* download again even if cached; does nothing natively without fetching */
    WGF_ASSET_ENSURE_FORCE_FETCH = 1 << 0
};

/* The asset host relative paths resolve against. A URL ("https://host/assets") is a
 * fetch origin on both platforms: a missing file is downloaded from it and cached, on
 * the web in the browser's storage and natively in the cache directory, by the program
 * (fetching, below). Anything else is a local directory: an absolute path as it is, a
 * relative one ("../assets") against the program's own directory on both platforms,
 * the executable's natively and the page's on the web, never the working directory,
 * so the same line in a program means the same thing everywhere and a double-clicked
 * program finds its files (Rob, 2026-10-04); a file: URL ("file:///opt/game/assets")
 * names one too, natively only (a browser reads no file: URLs). A local host is only
 * ever read: what is downloaded under one (a fetch_url's file, a "://" redirect's) goes
 * in the cache directory, so a shipped file is never overwritten. The same paths
 * everywhere; only the host differs. NULL or "" puts back the default above, the
 * program's directory, as "." does. False for a host of 512 bytes or more, or a
 * relative one too long a path under the program's directory. */
WGF_API bool wgf_asset_set_host(const char *host);
/* The host, set or the default (without a trailing slash): natively the storage root,
 * the program's directory or the directory set, as a path (a relative one joined to
 * the program's directory); on the web the directory of the document's base URL. */
WGF_API const char *wgf_asset_get_host(void);

/* Natively, where downloads land and later runs find them: a local directory, made as
 * needed. By default the program's own under the user's cache directory, <user
 * cache>/<company>/<app> by the program's identity (wgf_set_app_company and
 * wgf_set_app_name in wgf.h; set them for anything shipped): $XDG_CACHE_HOME or
 * ~/.cache on Linux, ~/Library/Caches on macOS, %LOCALAPPDATA%'s <company>/<app>/cache
 * on Windows; ".wgf-cache" in the storage root where there is none. Set it before the
 * first download. False on the web, which caches in the browser, and for a path of 512
 * bytes or more. */
WGF_API bool wgf_asset_set_cache_dir(const char *dir);
/* The directory downloads go in: set, or the default above. "" on the web. */
WGF_API const char *wgf_asset_get_cache_dir(void);

/* Downloads, natively, by the program: libwgf has no HTTP client there, so a program
 * that turns fetching on is asked for each download -- a file missing under a URL
 * host, a URL path, a fetch_url, a "://" redirect. Without it, such a load fails.
 *
 * Poll for work once a frame, on the main thread:
 *
 *     wgf_asset_task_t request;
 *     while ((request = wgf_asset_fetch_next()) != 0) {
 *         start_download(wgf_asset_fetch_get_url(request),
 *                        wgf_asset_fetch_get_dest(request), request);
 *     }
 *
 * and report each one with wgf_asset_fetch_done, from any thread: start it and return,
 * do the work on a thread of your own, and answer from there; the answer is taken at
 * the next update, on the main thread. At most 6 are out at once, a browser's limit per
 * server; the rest wait their turn. A request nobody takes waits, and so does every
 * task behind it; one taken and not answered within the fetch timeout fails.
 *
 * Bytes never cross this boundary: a downloader deals in files, as curl, WinHTTP and
 * NSURLSession do. The destination's directories exist. It is where the download is
 * written until it is whole: libwgf moves it into place when told it succeeded and
 * deletes it when not, so a failed or broken-off download never leaves half a file to
 * be read and never costs the copy that was there. Success with nothing written is a
 * failure, except for a ping (wgf_asset_fetch_is_ping): a request for which only
 * whether the host answered is wanted, with no destination (wgf_asset_ping_host;
 * wgrender had no native ping of a URL host). */

/* Turn the program's downloading on or off (off by default). Off fails the requests
 * not yet taken, at the next update; ones taken are still answered. False on the web,
 * where the browser downloads. */
WGF_API bool wgf_asset_set_fetching(bool enabled);
WGF_API bool wgf_asset_is_fetching(void);
/* The next download to do, oldest first, or 0 for none. Each is handed out once. */
WGF_API wgf_asset_task_t wgf_asset_fetch_next(void);
/* What a request downloads, and where to write it. "" for anything that isn't a request
 * waiting on its answer, and a ping's destination. Borrowed: valid until the next
 * call. */
WGF_API const char *wgf_asset_fetch_get_url(wgf_asset_task_t request);
WGF_API const char *wgf_asset_fetch_get_dest(wgf_asset_task_t request);
/* Whether a request is a ping: ask the URL (a HEAD request does) and answer
 * wgf_asset_fetch_done with whether the host replied at all, any status counting, and
 * write nothing. False for anything that isn't a request waiting on its answer. */
WGF_API bool wgf_asset_fetch_is_ping(wgf_asset_task_t request);
/* What became of a download (for a ping, whether the host answered); any thread. False
 * when it can't be taken: not a request waiting on its answer, or libwgf stopped
 * meanwhile. */
WGF_API bool wgf_asset_fetch_done(wgf_asset_task_t request, bool ok);
/* Natively, seconds a request may go unanswered once the downloader has taken it before
 * it fails; 0, the default, waits for ever, as wgrender's did. Only a downloader can
 * tell a slow download from a stalled one (curl's low-speed limit does), so the default
 * hides nothing: a downloader that drops requests is a bug that shows as tasks staying
 * PENDING. False for a negative or non-finite one, and on the web, where the browser
 * fetches and the fetch has no timeout of its own: the call stores nothing there. */
WGF_API bool wgf_asset_set_fetch_timeout(float seconds);
WGF_API float wgf_asset_get_fetch_timeout(void);

/* Forget a cached file, so the next load or ensure fetches it again: the file and what
 * was kept about it, from the browser's storage or the cache directory -- never a local
 * host's own file. False when there was none, or for a path that isn't under the host
 * (as wgf_asset_ensure reads one). A cache can hold a file that is wrong rather than
 * old (a host that once served gzip bytes under its name), which revalidation keeps;
 * this drops it. libwgf also drops one itself when a loader rejects a cached file, and
 * fetches it once more. */
WGF_API bool wgf_asset_evict(const char *path);

/* Forget every cached file, so the next ensure of any fetches it again, and what was
 * read of the manifests, so the root is asked about again. Natively, every file libwgf
 * downloaded into the cache directory is deleted, with its metadata and the directories
 * that leaves empty: it keeps a list of its downloads there (".wgf-downloads") and
 * never deletes a file it didn't download. Resources already made stay as they are.
 * Call it while nothing is loading: a load in flight may fail. */
WGF_API void wgf_asset_clear_cache(void);

/* How a cached file is treated on a later run (ROADMAP phase 3, Rob's):
 *
 *   REVALIDATE  the default. A copy still fresh by its Cache-Control (max-age not
 *               passed, or immutable) is used without a request; any other is checked
 *               with the server first: 304, the copy is used (and fresh again); 200,
 *               the new file replaces it; 4xx, the copy is deleted and the load fails;
 *               no answer (offline, a timeout) or 5xx, the copy is used. no-cache and
 *               no-store make a copy never fresh, but it is kept, for the next check
 *               and for starting offline. On the page's own origin the check is a
 *               conditional GET; on another, a GET the browser revalidates from its
 *               own cache (a conditional header there needs CORS consent), so a
 *               changed file is always noticed and an unchanged one stored again.
 *   TRUST       a cached copy is used without asking, however old: offline-first play,
 *               a deploy whose files never change, or a program that evicts by itself.
 *   OFF         nothing is kept between runs, and what earlier runs kept is neither
 *               used nor deleted (development).
 *
 * Natively, downloads in the cache directory are used as they are in every mode, as
 * wgrender's: asking the server needs headers the program's fetcher doesn't carry
 * (manifests work natively, below). FORCE_FETCH is a plain GET in every mode, and fails
 * without an answer. A mode applies to every file checked after it is set. */
typedef enum wgf_asset_cache_mode_t {
    WGF_ASSET_CACHE_MODE_REVALIDATE = 0,
    WGF_ASSET_CACHE_MODE_TRUST = 1,
    WGF_ASSET_CACHE_MODE_OFF = 2
} wgf_asset_cache_mode_t;
/* False for a value that isn't a mode. */
WGF_API bool wgf_asset_set_cache_mode(wgf_asset_cache_mode_t mode);
WGF_API wgf_asset_cache_mode_t wgf_asset_get_cache_mode(void);

/* An asset manifest: each file's contents hashed, so a cached copy whose hash still
 * matches is used with no request, and one that changed is fetched once
 * (tools/gen_manifest.py writes them). `path` is the root manifest's, under the host
 * ("manifest.json"). A manifest lists the files beside it and, for each directory, the
 * hash of its own manifest.json, fetched only when a file under it is first needed,
 * and only if its hash changed.
 *
 * The root is asked about once a run, as REVALIDATE asks, whatever the mode; without
 * an answer the cached root is used, and without either nothing is listed. A listed
 * file is fetched past the browser's cache and hashed before it is kept: bytes that
 * don't match (a host still serving the old file, a deploy half done) are not kept,
 * and the load fails, logged. A file no manifest lists, one ensured with a fetch_url,
 * and every file under a manifest that couldn't be read or didn't match its hash are
 * cached as the cache mode says. Natively a manifest needs a URL host and fetching.
 *
 * NULL or "" for none (the default). False for a path that isn't relative (starting
 * with "/" or holding "://"), or of 512 bytes or more. Set it before the loads it
 * should cover; setting it again forgets what was read of the last one. */
WGF_API bool wgf_asset_set_manifest(const char *path);
/* The root manifest's path, as set; "" for none. */
WGF_API const char *wgf_asset_get_manifest(void);

/* Make a file local: a task PENDING, then DONE or FAILED in a later update (never in
 * this call), with a local path that opens directly.
 *
 *   path       the file, under the host, as a resource's create names it: "\" is read
 *              as "/", "." and ".." resolved; one that is absolute, names a drive (any
 *              ":"), or climbs above the host is refused (0)
 *   fetch_url  NULL, or the SOURCE only, for this call: a mirror, a signed link, a
 *              versioned name; the bytes are still cached and found under `path`. Read
 *              against the host as a browser reads a URL against a directory:
 *              "music/v2/a.mp3" under it, "../x" beside it, "/x" at its origin's root,
 *              an absolute URL as it is. Natively an absolute one must be http or
 *              https and needs fetching, not a URL host. Under a local host a relative
 *              one is a file under it, read where it is, held to `path`'s rules.
 *              Anything else (a file: URL, one leaving a local host) is refused (0)
 *   flags      WGF_ASSET_ENSURE_*
 *
 * The task, kept until wgf_asset_task_destroy, or 0 (refused as above, or no room). */
WGF_API wgf_asset_task_t wgf_asset_ensure(const char *path, const char *fetch_url, unsigned int flags);

/* A task's status: a file's, a group's, or a ping's; NONE for anything that isn't one.
 * It changes only in an update, so a frame reading it sees each change once. */
WGF_API wgf_asset_task_status_t wgf_asset_task_get_status(wgf_asset_task_t task);
/* The local path of a DONE file task, which opens directly (where the file was found:
 * a redirect's, a fetch_url's). "" until then, for a group, and for anything that isn't
 * a task. Borrowed: valid while the task is, until the next ensure. */
WGF_API const char *wgf_asset_task_get_path(wgf_asset_task_t task);
/* Rough progress, 0..1: a file counts half for being local and half for the files it
 * names; a group, its members' average. 1 once DONE or FAILED; 0 for anything that
 * isn't a task. For resources loading, read their statuses (wgf_resource.h). */
WGF_API float wgf_asset_task_get_progress(wgf_asset_task_t task);
/* Free a task. One still PENDING runs on, its result dropped (a file still lands in
 * the cache). A group's members go with it. False for anything that isn't a task. */
WGF_API bool wgf_asset_task_destroy(wgf_asset_task_t task);

/* Groups: one task for many files (a level's, fetched ahead). DONE once every member
 * is; FAILED once every member has finished and any failed; an empty one is DONE at
 * the next update. Members keep their own statuses and paths. */
WGF_API wgf_asset_task_t wgf_asset_group_create(void);
/* Add a file task (wgf_asset_ensure) to a group, finished or not. False for anything
 * else, a task already in a group, or a group that has finished. */
WGF_API bool wgf_asset_group_add(wgf_asset_task_t group, wgf_asset_task_t task);

/* Redirects: files from somewhere else, for mods, translations, or a CDN. Files whose
 * path starts with `prefix` are looked for under `target` instead:
 *
 *   wgf_asset_add_redirect("textures/", "mods/hd/textures/");
 *       textures/rock.png loads mods/hd/textures/rock.png if it exists, else its own
 *   wgf_asset_add_redirect("models/", "https://cdn.example.com/game/models/");
 *       a target with "://" is where the file downloads from (the browser, or natively
 *       the program's fetcher); it is still cached and loaded as models/...
 *
 * Path rules stack: every one matching is tried, the last added first, then the file's
 * own path, so later rules sit on top (a mod over a mod, fr-CA over fr). A file missing
 * under a path rule isn't an error; the next is tried (on the web, a request each). A
 * download rule doesn't stack: the newest matching is where it downloads from.
 * Prefixes are plain text, matched at the start ("textures/", not "*.png").
 *
 * They apply to every file a create loads or an ensure makes local (unless it gave a
 * fetch_url), and to the files those name (a model's buffers and images, beside
 * wherever the model came from); wgf_resource_get_path and wgf_asset_task_get_path say
 * which was found. Up to 32 rules; false when full, for an empty prefix or target, or
 * for a prefix or path target not under the host (as wgf_asset_ensure reads a path; a
 * trailing "/" is kept). */
WGF_API bool wgf_asset_add_redirect(const char *prefix, const char *target);
WGF_API void wgf_asset_clear_redirects(void);

/* Ping an asset host: a task DONE in a later update when it answered within
 * `timeout_ms` (0 or less: 5000), FAILED when it didn't. NULL pings the current host.
 * On the web, a HEAD request (any response counts, even a 404; another origin needs no
 * CORS headers). Natively, a local host is DONE when its directory exists, and a URL
 * host is asked through the program's fetcher, as a ping request
 * (wgf_asset_fetch_is_ping; wgrender's had none); without fetching, FAILED. 0 when
 * there's no room. */
WGF_API wgf_asset_task_t wgf_asset_ping_host(const char *host, int timeout_ms);
/* A DONE ping's round trip in milliseconds; 0 for one that isn't DONE, and for
 * anything that isn't a ping. */
WGF_API float wgf_asset_ping_get_milliseconds(wgf_asset_task_t ping);

#ifdef __cplusplus
}
#endif

#endif /* WGF_ASSET_H */
