#ifndef WGF_ASSET_H
#define WGF_ASSET_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An asset task: a handle of kind "asset.task". It is an ensure, a group, a ping, or a
 * download that the program answers (a fetch request). */
typedef wgf_handle_t wgf_asset_task_t;

/* Assets: where a resource's file comes from.
 *
 * A path is an asset. Creating a resource from a path goes through here, as does every
 * call below. That includes wgf_texture_create, wgf_font_create, wgf_sound_create, and a
 * scene's files. Those calls are what link this layer, so a program that only makes
 * generated content links none of it.
 *
 * A relative path is looked for under the asset host, which needs no setting:
 *
 *   natively  the host is the program's own directory (where its executable is,
 *             wherever it was started from), which is the file storage's root
 *             (wgf_fs.h). Files are read where they are, as without this layer
 *   web       the host is the page's own directory: its document's base URL, so a
 *             <base href> moves it. A file that isn't bundled into the page and isn't
 *             cached is fetched from beside the page and cached in the browser's
 *             storage. On later visits it is checked again as the cache mode says. A
 *             file asked for before that storage has opened (the first frames) waits
 *             for it. So no kept file is lost, and a game needs no wait of its own
 *
 * So a web page loads its files with no call here at all. A page that should never fetch
 * bundles what it loads.
 *
 * - Set another host to fetch from elsewhere, such as a CDN, or natively to download (see
 *   fetching, below).
 * - A path that is a URL ("https://...") is fetched from there, and cached under its own
 *   name. Natively that needs fetching turned on. Without it the load fails, logged with
 *   the URL.
 *
 * A resource makes its own file local as it loads (wgf_resource.h). So a program only
 * ENSURES a file to have it local without loading it:
 * - to fetch ahead, such as a level's files during a menu
 * - to fetch from an explicit source (a fetch_url)
 * - to read the file itself
 *
 * An ensure is a task: a handle whose status, local path, and progress the program reads.
 * Nothing is called back. Destroy the task when you are done with it. A path ensured from
 * an explicit source is what a later create of that path loads, wherever it was found.
 *
 * A file in a format that names other files is ensured with them, when the layer that
 * reads the format registers how to list them (core's lister, wgf_core_load_priv.h). The
 * task is DONE once all of them are local.
 * - A glTF file's buffers and images are listed, in a program with glTF (one that calls
 *   wgf_mesh_create).
 * - A scene's files aren't listed yet. To ensure a scene ahead, also ensure the files its
 *   lines name. */

typedef enum wgf_asset_task_status_t {
    WGF_ASSET_TASK_STATUS_NONE = 0, /* not a task, or a destroyed one */
    WGF_ASSET_TASK_STATUS_PENDING = 1,
    WGF_ASSET_TASK_STATUS_DONE = 2, /* the file is local, and so is every file it names */
    WGF_ASSET_TASK_STATUS_FAILED = 3
} wgf_asset_task_status_t;

/* The flags for wgf_asset_ensure, combined with or. */
enum {
    WGF_ASSET_ENSURE_NONE = 0,
    /* downloads again even if cached; does nothing natively without fetching */
    WGF_ASSET_ENSURE_FORCE_FETCH = 1 << 0
};

/* Sets the asset host that relative paths resolve against.
 *
 * - A URL ("https://host/assets") is a fetch origin on both platforms. A missing file is
 *   downloaded from it and cached: on the web in the browser's storage, and natively in
 *   the cache directory, by the program (see fetching, below).
 * - Anything else is a local directory. An absolute path is used as it is.
 * - A relative path ("../assets") is read against the program's own directory on both
 *   platforms: the executable's natively, and the page's on the web. It is never read
 *   against the working directory. So the same line in a program means the same
 *   everywhere, and a double-clicked program finds its files.
 * - A file: URL ("file:///opt/game/assets") names a local directory too, natively only. A
 *   browser reads no file: URLs.
 * - NULL or "" puts back the default above, the program's directory, as "." does.
 *
 * A local host is only ever read. A file downloaded under one, such as a fetch_url's file
 * or a "://" redirect's, goes in the cache directory, so a shipped file is never
 * overwritten. The paths are the same everywhere, and only the host differs.
 *
 * Returns false for a host of 512 bytes or more, or for a relative one that makes too long
 * a path under the program's directory. */
WGF_API bool wgf_asset_set_host(const char *host);
/* Returns the host, as set or the default, without a trailing slash.
 *
 * - Natively it is the storage root: the program's directory or the directory set, as a
 *   path. A relative one is joined to the program's directory.
 * - On the web it is the directory of the document's base URL. */
WGF_API const char *wgf_asset_get_host(void);

/* Sets the cache directory, natively: where downloads land and later runs find them.
 *
 * It is a local directory, made as needed. By default it is the program's own directory
 * under the user's cache directory: <user cache>/<company>/<product>, by the program's
 * identity (see wgf_identity.h: set it for anything you ship).
 * - On Linux, <user cache> is $XDG_CACHE_HOME or ~/.cache.
 * - On macOS it is ~/Library/Caches.
 * - On Windows the directory is %LOCALAPPDATA%'s <company>/<product>/cache.
 * - Where there is no user cache directory, it is ".wgf-cache" in the storage root.
 *
 * Set it before the first download. Returns false on the web, which caches in the
 * browser, and for a path of 512 bytes or more. */
WGF_API bool wgf_asset_set_cache_dir(const char *dir);
/* Returns the directory downloads go in, as set or the default above. Returns "" on the
 * web. */
WGF_API const char *wgf_asset_get_cache_dir(void);

/* Downloads, natively, done by the program.
 *
 * libwgf has no HTTP client on native platforms. So a program that turns fetching on is
 * asked for each download: a file missing under a URL host, a URL path, a fetch_url, or a
 * "://" redirect. Without fetching, such a load fails.
 *
 * Poll for work once a frame, on the main thread:
 *
 *     wgf_asset_task_t request;
 *     while ((request = wgf_asset_fetch_next()) != 0) {
 *         start_download(wgf_asset_fetch_get_url(request),
 *                        wgf_asset_fetch_get_dest(request), request);
 *     }
 *
 * Then answer each one with wgf_asset_fetch_done, from any thread. Start the download and
 * return, do the work on a thread of your own, and answer from there. The answer is taken
 * at the next update, on the main thread.
 *
 * - At most 6 requests are out at once, a browser's limit per server. The rest wait their
 *   turn.
 * - A request nobody takes waits, and so does every task behind it.
 * - A request taken and not answered within the fetch timeout fails.
 *
 * Bytes never cross this boundary: a downloader deals in files, as curl, WinHTTP, and
 * NSURLSession do.
 * - The destination's directories exist.
 * - The destination is where the download is written until it is whole. libwgf moves it
 *   into place when told it succeeded, and deletes it when not. So a failed or broken-off
 *   download never leaves half a file to be read, and never costs the copy that was there.
 * - Success with nothing written is a failure, except for a ping (wgf_asset_fetch_is_ping).
 *   A ping asks only whether the host answered, and has no destination (see
 *   wgf_asset_ping_host). */

/* Sets or gets whether the program does downloads (default: off).
 *
 * Turning it off fails the requests not yet taken, at the next update. Requests already
 * taken are still answered. The setter returns false on the web, where the browser
 * downloads. */
WGF_API bool wgf_asset_set_fetching(bool enabled);
WGF_API bool wgf_asset_is_fetching(void);
/* Returns the next download to do, oldest first, or 0 if there is none. Each one is handed
 * out once. */
WGF_API wgf_asset_task_t wgf_asset_fetch_next(void);
/* Returns the URL a request downloads, and the destination to write it to.
 *
 * Each string is borrowed, valid until the next call. Returns "" for a handle that isn't a
 * request waiting on its answer, and for a ping's destination. */
WGF_API const char *wgf_asset_fetch_get_url(wgf_asset_task_t request);
WGF_API const char *wgf_asset_fetch_get_dest(wgf_asset_task_t request);
/* Returns whether a request is a ping.
 *
 * For a ping, ask the URL, as a HEAD request does, and write nothing. Answer
 * wgf_asset_fetch_done with whether the host replied at all, with any status counting.
 *
 * Returns false for a handle that isn't a request waiting on its answer. */
WGF_API bool wgf_asset_fetch_is_ping(wgf_asset_task_t request);
/* Answers a request with whether its download succeeded, or for a ping, whether the host
 * answered. Call it from any thread.
 *
 * Returns false if the answer can't be taken: `request` isn't a request waiting on its
 * answer, or libwgf stopped meanwhile. */
WGF_API bool wgf_asset_fetch_done(wgf_asset_task_t request, bool ok);
/* Sets or gets the fetch timeout, natively: the seconds a request may go unanswered, once
 * the downloader has taken it, before it fails (default: 0, which waits for ever).
 *
 * Only a downloader can tell a slow download from a stalled one, as curl's low-speed limit
 * does. So the default hides nothing: a downloader that drops requests is a bug that shows
 * as tasks staying PENDING.
 *
 * The setter returns false for a negative or non-finite value. It also returns false on
 * the web, where the browser fetches and the fetch has no timeout of its own, and stores
 * nothing there. */
WGF_API bool wgf_asset_set_fetch_timeout(float seconds);
WGF_API float wgf_asset_get_fetch_timeout(void);

/* Forgets a cached file, so the next load or ensure fetches it again.
 *
 * It removes the file, and what was kept about it, from the browser's storage or the cache
 * directory. It never removes a local host's own file.
 *
 * A cache can hold a file that is wrong rather than old, such as from a host that once
 * served gzip bytes under its name. Revalidation keeps such a file, and this drops it.
 * libwgf also drops a cached file itself when a loader rejects it, and fetches it once
 * more.
 *
 * Returns false if there was no cached file, or for a path that isn't under the host (as
 * wgf_asset_ensure reads a path). */
WGF_API bool wgf_asset_evict(const char *path);

/* Enables loading assets again in place (wgf_asset_reload), or says whether it is enabled.
 *
 * - The wgf tool's own builds enable it (`wgf serve`, `build`, and `run`); a game's export
 *   doesn't. On the web the host decides: it is enabled when the host exports this call.
 * - Call it before the assets are made. What a reload keeps is kept from then on: the
 *   changes a program makes to a document's elements, the name of a model's clip playing.
 * - Without it, nothing is kept for a reload, and a program pays nothing for one.
 *
 * Returns true. */
WGF_API bool wgf_asset_enable_reload(void);
WGF_API bool wgf_asset_is_reload_enabled(void);

/* Loads again, in place, every resource made from `path`, and every file that names it,
 * such as a glTF whose buffer or image it is. `wgf serve` does this with an asset saved
 * while the game runs. It needs wgf_asset_enable_reload.
 *
 * - Each file is fetched anew, never from the cache or this run's store.
 * - Each resource keeps its handle, and stays READY all the while. Whatever uses it draws
 *   the old data until the new file is in, then the new data, from that frame on.
 * - A glTF's tree is kept too (wgf_model.h). Each node's actor stays, moved and given its
 *   new mesh. A node the file no longer has is removed with its actor, and a new node gets
 *   a new actor.
 * - A file that doesn't load keeps what was loaded, with one error logged.
 * - A FAILED resource that now loads becomes READY.
 * - These load again: a texture (compressed ones too), an image, a cubemap, a glTF mesh
 *   with its images, a sprite sheet, a Tiled map, an environment, a shader, a sound, a
 *   font, and a text buffer. Each kind's header says what is kept across it.
 * - These don't: a scene and a UI document. Each is left as it is, with a warning.
 * - A resource still loading is left as it is.
 *
 * Returns the number of resources set loading. Returns 0 if nothing was made from `path`,
 * for a path that isn't under the host, or when reload isn't enabled (logged once). */
WGF_API int wgf_asset_reload(const char *path);

/* Returns true if wgf_asset_reload(path) would bring the saved file into everything made
 * from it: at least one resource was made from `path` or from a file that names it, and
 * each is of a kind that loads again. A tool that watches files asks this first, and starts
 * the program again for a file it can't load again in place.
 *
 * Returns false if any resource made from it is of a kind that doesn't load again, if
 * nothing was made from it (a scene whose actors are already made, a file not loaded yet),
 * for a path that isn't under the host, or when reload isn't enabled. */
WGF_API bool wgf_asset_can_reload(const char *path);

/* Forgets every cached file, so the next ensure of any file fetches it again. It also
 * forgets what was read of the manifests, so the root manifest is asked about again.
 *
 * Natively, every file libwgf downloaded into the cache directory is deleted, with its
 * metadata and the directories that leaves empty. libwgf keeps a list of its downloads
 * there (".wgf-downloads"), and never deletes a file it didn't download.
 *
 * Resources already made stay as they are. Call it while nothing is loading, since a load
 * in flight may fail. */
WGF_API void wgf_asset_clear_cache(void);

/* How a cached file is treated on a later run.
 *
 *   REVALIDATE  the default. A copy still fresh by its Cache-Control (max-age not
 *               passed, or immutable) is used without a request. Any other copy is
 *               checked with the server first:
 *               - 304: the copy is used, and is fresh again.
 *               - 200: the new file replaces it.
 *               - 4xx: the copy is deleted and the load fails.
 *               - 5xx, or no answer (offline, a timeout): the copy is used.
 *               no-cache and no-store make a copy never fresh, but it is kept, for the
 *               next check and for starting offline. On the page's own origin the
 *               check is a conditional GET. On another origin it is a GET the browser
 *               revalidates from its own cache, since a conditional header there needs
 *               CORS consent. So a changed file is always noticed, and an unchanged one
 *               is stored again.
 *   TRUST       a cached copy is used without asking, however old. For offline-first
 *               play, a deploy whose files never change, or a program that evicts by
 *               itself.
 *   OFF         nothing is kept between runs, and what earlier runs kept is neither
 *               used nor deleted. For development.
 *
 * Natively, downloads in the cache directory are used as they are, in every mode. Asking
 * the server needs headers the program's fetcher doesn't carry. (Manifests do work
 * natively; see below.)
 *
 * FORCE_FETCH is a plain GET in every mode, and fails without an answer. A mode applies to
 * every file checked after it is set. */
typedef enum wgf_asset_cache_mode_t {
    WGF_ASSET_CACHE_MODE_REVALIDATE = 0,
    WGF_ASSET_CACHE_MODE_TRUST = 1,
    WGF_ASSET_CACHE_MODE_OFF = 2
} wgf_asset_cache_mode_t;
/* Sets or gets the cache mode (default: REVALIDATE). The setter returns false for a value
 * that isn't a mode. */
WGF_API bool wgf_asset_set_cache_mode(wgf_asset_cache_mode_t mode);
WGF_API wgf_asset_cache_mode_t wgf_asset_get_cache_mode(void);

/* Sets the asset manifest, which holds a hash of each file's contents.
 *
 * With a manifest, a cached copy whose hash still matches is used with no request, and a
 * file that changed is fetched once. tools/gen_manifest.py writes manifests, and `wgf
 * export` writes them into a game's web export.
 *
 * - `path` is the root manifest's path, under the host ("manifest.json").
 * - A manifest lists the files beside it. For each directory it lists the hash of that
 *   directory's own manifest.json. That manifest is fetched only when a file under it is
 *   first needed, and only if its hash changed.
 * - The root is asked about once a run, as REVALIDATE asks, whatever the mode. Without an
 *   answer the cached root is used. Without either, nothing is listed.
 * - A listed file is fetched past the browser's cache, and hashed before it is kept.
 *   Bytes that don't match, such as from a host still serving the old file or a deploy
 *   half done, are not kept, and the load fails (logged).
 * - These files are cached as the cache mode says: a file no manifest lists, a file
 *   ensured with a fetch_url, and every file under a manifest that couldn't be read or
 *   didn't match its hash.
 * - Natively a manifest needs a URL host and fetching.
 *
 * NULL or "" sets no manifest (the default). Set it before the loads it should cover.
 * Setting it again forgets what was read of the last one.
 *
 * Returns false for a path that isn't relative (one starting with "/" or holding "://"),
 * or for one of 512 bytes or more. */
WGF_API bool wgf_asset_set_manifest(const char *path);
/* Returns the root manifest's path, as set, or "" if there is none. */
WGF_API const char *wgf_asset_get_manifest(void);

/* Makes a file local. Returns a task that is PENDING, then DONE or FAILED in a later
 * update, never in this call. A DONE task has a local path that opens directly.
 *
 *   path       the file, under the host, as a resource's create names it. "\" is read
 *              as "/", and "." and ".." are resolved. A path that is absolute, names a
 *              drive (any ":"), or climbs above the host is refused (0)
 *   fetch_url  NULL, or the SOURCE only, for this call: a mirror, a signed link, or a
 *              versioned name. The bytes are still cached and found under `path`. It
 *              is read against the host as a browser reads a URL against a directory:
 *              "music/v2/a.mp3" under it, "../x" beside it, "/x" at its origin's root,
 *              and an absolute URL as it is. Natively an absolute one must be http or
 *              https, and needs fetching, not a URL host. Under a local host a relative
 *              one is a file under it, read where it is, held to `path`'s rules.
 *              Anything else, such as a file: URL or one leaving a local host, is
 *              refused (0)
 *   flags      WGF_ASSET_ENSURE_* flags
 *
 * The task is kept until wgf_asset_task_destroy. Returns 0 if a path is refused as above,
 * or if there is no room for another task. */
WGF_API wgf_asset_task_t wgf_asset_ensure(const char *path, const char *fetch_url, unsigned int flags);

/* Returns a task's status, for a file, a group, or a ping. Returns NONE for a handle that
 * isn't a task. The status changes only in an update, so a frame reading it sees each
 * change once. */
WGF_API wgf_asset_task_status_t wgf_asset_task_get_status(wgf_asset_task_t task);
/* Returns the local path of a DONE file task, which opens directly.
 *
 * It is where the file was found, such as through a redirect or a fetch_url. The path is
 * borrowed, valid while the task is, until the next ensure. Returns "" until the task is
 * DONE, for a group, and for a handle that isn't a task. */
WGF_API const char *wgf_asset_task_get_path(wgf_asset_task_t task);
/* Returns a task's rough progress, from 0 to 1.
 *
 * - A file counts half for being local, and half for the files it names.
 * - A group's progress is its members' average.
 * - It is 1 once DONE or FAILED.
 *
 * Returns 0 for a handle that isn't a task. For resources that are loading, read their
 * statuses instead (wgf_resource.h). */
WGF_API float wgf_asset_task_get_progress(wgf_asset_task_t task);
/* Destroys a task. A task still PENDING runs on, and its result is dropped, though a file
 * still lands in the cache. A group's members are destroyed with it. Returns false for a
 * handle that isn't a task. */
WGF_API bool wgf_asset_task_destroy(wgf_asset_task_t task);

/* Creates a group: one task for many files, such as a level's files fetched ahead.
 *
 * - A group is DONE once every member is.
 * - It is FAILED once every member has finished and any of them failed.
 * - An empty group is DONE at the next update.
 * - Members keep their own statuses and paths. */
WGF_API wgf_asset_task_t wgf_asset_group_create(void);
/* Adds a file task (from wgf_asset_ensure) to a group, whether or not it has finished.
 * Returns false for a handle that isn't a file task, for a task already in a group, or for
 * a group that has finished. */
WGF_API bool wgf_asset_group_add(wgf_asset_task_t group, wgf_asset_task_t task);

/* Adds a redirect, or clears them all. A redirect takes files from somewhere else, for
 * mods, translations, or a CDN. Files whose path starts with `prefix` are looked for under
 * `target` instead:
 *
 *   wgf_asset_add_redirect("textures/", "mods/hd/textures/");
 *       textures/rock.png loads mods/hd/textures/rock.png if it exists, else its own
 *   wgf_asset_add_redirect("models/", "https://cdn.example.com/game/models/");
 *       a target with "://" is where the file downloads from (by the browser, or
 *       natively by the program's fetcher). It is still cached and loaded as models/...
 *
 * - Path rules stack. Every matching rule is tried, the last added first, then the file's
 *   own path. So later rules sit on top: a mod over a mod, or fr-CA over fr.
 * - A file missing under a path rule isn't an error. The next rule is tried (on the web,
 *   with a request each).
 * - A download rule doesn't stack. The newest matching one is where the file downloads
 *   from.
 * - Prefixes are plain text, matched at the start: "textures/", not "*.png".
 *
 * Redirects apply to every file a create loads, or an ensure makes local unless it gave a
 * fetch_url. They also apply to the files those name, such as a model's buffers and images,
 * beside wherever the model came from. wgf_resource_get_path and wgf_asset_task_get_path
 * say which file was found.
 *
 * There can be up to 32 rules. add_redirect returns false when they are full, for an empty
 * prefix or target, or for a prefix or path target not under the host (as
 * wgf_asset_ensure reads a path). A trailing "/" is kept. */
WGF_API bool wgf_asset_add_redirect(const char *prefix, const char *target);
WGF_API void wgf_asset_clear_redirects(void);

/* Pings an asset host. Returns a task that, in a later update, is DONE if the host answered
 * within `timeout_ms` milliseconds, or FAILED if it didn't.
 *
 * - A `timeout_ms` of 0 or less means 5000.
 * - NULL pings the current host.
 * - On the web it is a HEAD request. Any response counts, even a 404, and another origin
 *   needs no CORS headers.
 * - Natively, a local host is DONE if its directory exists. A URL host is asked through
 *   the program's fetcher, as a ping request (wgf_asset_fetch_is_ping). Without fetching,
 *   the task is FAILED.
 *
 * Returns 0 if there is no room for another task. */
WGF_API wgf_asset_task_t wgf_asset_ping_host(const char *host, int timeout_ms);
/* Returns a DONE ping's round trip, in milliseconds. Returns 0 for a ping that isn't DONE,
 * and for a handle that isn't a ping. */
WGF_API float wgf_asset_ping_get_milliseconds(wgf_asset_task_t ping);

#ifdef __cplusplus
}
#endif

#endif /* WGF_ASSET_H */
