#ifndef WGF_ASSET_PRIV_H
#define WGF_ASSET_PRIV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wgf_asset.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"

/* The asset part (ROADMAP.md, phase 3; ported from wgrender's wgr_asset.c, split by what
 * each piece does): where a resource's file comes from. Installed by the first resource
 * made from a path (wgf_asset_priv_resource_create) or the first wgf_asset_* call, it
 * sets core's load hooks (wgf_core_load_priv.h), so every load's file is made local
 * here: read where it is, read from the cache, or fetched. Main thread only, but for
 * what the header says may be called from any thread. */

#define WGF_ASSET_PRIV_PATH_MAX WGF_CORE_PRIV_FS_PATH_MAX
#define WGF_ASSET_PRIV_URL_MAX 1024

/* ---------------------------------------------------------------- the part ---- */

/* The part on core's list and its hooks set: once, at the first use. */
void wgf_asset_priv_install(void);

/* A resource of `kind` made from `path` (wgf_core_priv_resource_create), the asset part
 * installed first: what gfx and audio call to make a resource from a path, so a path is
 * an asset (ROADMAP.md, phase 3, Rob's). */
wgf_handle_t wgf_asset_priv_resource_create(wgf_core_priv_handle_kind_t kind, const char *path);

/* The host as the tasks read it: what was set, or the default (natively the storage
 * root, on the web the directory of the document's base URL), without a trailing "/". */
const char *wgf_asset_priv_host(void);
bool wgf_asset_priv_host_is_url(void);
wgf_asset_cache_mode_t wgf_asset_priv_cache_mode(void);

/* ------------------------------------------------------------------ paths ---- */

/* `path` normalized as a file under the host ("\" as "/", "." and ".." resolved); false
 * for one that is absolute, names a drive, climbs above the host, or is empty. */
bool wgf_asset_priv_normalize_path(const char *path, char *out, size_t out_size);

/* A redirect's prefix or path target, normalized, keeping the "/" it ends with. */
bool wgf_asset_priv_normalize_prefix(const char *text, char *out, size_t out_size);

/* What a reference names, read against the host as a browser reads one against a
 * directory (wgrender's wgri_asset_resolve_source). */
typedef enum wgf_asset_priv_host_kind_t {
    WGF_ASSET_PRIV_HOST_BROWSER = 0, /* the web: anything the browser fetches */
    WGF_ASSET_PRIV_HOST_URL = 1,     /* natively, a URL host: http and https only */
    WGF_ASSET_PRIV_HOST_LOCAL = 2    /* natively, a directory: a path under it */
} wgf_asset_priv_host_kind_t;
typedef enum wgf_asset_priv_source_t {
    WGF_ASSET_PRIV_SOURCE_REFUSED = 0,
    WGF_ASSET_PRIV_SOURCE_URL = 1,  /* `out` is a URL */
    WGF_ASSET_PRIV_SOURCE_LOCAL = 2 /* `out` is a path under a local host */
} wgf_asset_priv_source_t;
wgf_asset_priv_source_t wgf_asset_priv_resolve_source(const char *host, wgf_asset_priv_host_kind_t kind,
                                                      const char *ref, char *out, size_t out_size);

/* Whether `uri`, as a file writes it, names a file beside it: not absolute, no scheme
 * (http:, data:, a drive). */
bool wgf_asset_priv_is_relative_uri(const char *uri);
/* `uri` read against the directory of `base_path` into `out`: %XX decoded, "." dropped,
 * ".." applied; false when it climbs above the base's top directory, holds a ":", or
 * doesn't fit. wgrender's wgri_asset_join_relative. */
bool wgf_asset_priv_join_relative(const char *base_path, const char *uri, char *out, size_t out_size);

/* The local path a file: URL names ("file:///opt/game" -> "/opt/game"); false for one
 * that isn't, or names another machine. */
bool wgf_asset_priv_file_url_path(const char *url, char *out, size_t out_size);

/* Whether `text` starts with a URL's scheme ("https:"). */
bool wgf_asset_priv_has_scheme(const char *text);

/* The key a URL path is kept and found under: ".url/" + the first 16 hex digits of the
 * URL's sha256 + "/" + its file's name, so its extension still picks its loader. False
 * for a URL with no file name, or one too long. */
bool wgf_asset_priv_url_key(const char *url, char *key, size_t key_size);

/* -------------------------------------------------------------- redirects ---- */

/* Where a file at `path` is looked for, in turn (wgf_asset_add_redirect): each path
 * rule's target, newest first, then `path` itself, each with the URL the newest URL
 * rule matching it gives ("" for none). */
typedef struct wgf_asset_priv_candidate_t {
    char path[WGF_ASSET_PRIV_PATH_MAX];
    char url[WGF_ASSET_PRIV_URL_MAX];
    bool overlay; /* a redirect's: missing is normal, not a warning */
} wgf_asset_priv_candidate_t;
/* The candidates for `path`, malloc'd (`*count` of them, at least 1), or NULL without
 * memory. */
wgf_asset_priv_candidate_t *wgf_asset_priv_plan(const char *path, int *count);

/* -------------------------------------------------------------- freshness ---- */

/* Seconds since 1970 a response with these Cache-Control and Age headers stays fresh
 * until, from `now`; 0 for never (no-cache, no-store, no max-age). */
double wgf_asset_priv_fresh_until(const char *cache_control, const char *age, double now);

/* ----------------------------------------------------------------- sha256 ---- */

#define WGF_ASSET_PRIV_SHA256_TEXT 72 /* "sha256:" + 64 hex + NUL */
/* A hash made in pieces, so a large file is hashed a slice an update, never stalling one;
 * text, all at once. */
typedef struct wgf_asset_priv_sha256_t {
    uint32_t h[8];
    unsigned char block[64];
    size_t pending; /* bytes in `block` */
    uint64_t size;
} wgf_asset_priv_sha256_t;
void wgf_asset_priv_sha256_begin(wgf_asset_priv_sha256_t *state);
void wgf_asset_priv_sha256_feed(wgf_asset_priv_sha256_t *state, const unsigned char *data, size_t size);
void wgf_asset_priv_sha256_end(wgf_asset_priv_sha256_t *state, char out[WGF_ASSET_PRIV_SHA256_TEXT]);
void wgf_asset_priv_sha256_text(const unsigned char *data, size_t size, char out[WGF_ASSET_PRIV_SHA256_TEXT]);

/* --------------------------------------------------------------- manifests ---- */

/* One directory's manifest (tools/gen_manifest.py): the hash of each file in it, and
 * of each subdirectory's own manifest.json.
 *
 *   { "wgf_manifest": 1,
 *     "files": { "tiles.png": "sha256:<64 hex>", ... },
 *     "dirs":  { "models": "sha256:<64 hex>", ... } }
 *
 * Other top-level keys are ignored (a later version may add some). */
typedef struct wgf_asset_priv_manifest_entry_t {
    char *name;
    char hash[WGF_ASSET_PRIV_SHA256_TEXT];
    bool dir;
} wgf_asset_priv_manifest_entry_t;

typedef struct wgf_asset_priv_manifest_t {
    wgf_asset_priv_manifest_entry_t *entries; /* sorted by (dir, name) */
    int count;
} wgf_asset_priv_manifest_t;

/* Read a manifest. False, with nothing to free, for anything that isn't one exactly:
 * not JSON, another version, a hash that isn't "sha256:" + 64 lowercase hex, a name
 * that is empty, "." or "..", or has a "/", or the same name twice. A manifest wrong
 * anywhere is trusted nowhere. */
bool wgf_asset_priv_manifest_parse(const char *json, size_t size, wgf_asset_priv_manifest_t *out);
/* The hash a manifest gives a file (dir false) or a subdirectory's manifest (dir
 * true), or NULL when it doesn't list the name. */
const char *wgf_asset_priv_manifest_find(const wgf_asset_priv_manifest_t *manifest, const char *name, bool dir);
void wgf_asset_priv_manifest_free(wgf_asset_priv_manifest_t *manifest);

/* What the manifest tree says about `slot`'s file (wgf_asset_set_manifest): LISTED, with
 * the hash its bytes must have in `hash`; NOT_LISTED; or LOOKING while a manifest on the
 * way to it is still loading (asked for: a task of its own). May add tasks. */
enum { WGF_ASSET_PRIV_NOT_LISTED = 0, WGF_ASSET_PRIV_LISTED, WGF_ASSET_PRIV_LOOKING };
int wgf_asset_priv_manifest_lookup(uint16_t slot, char hash[WGF_ASSET_PRIV_SHA256_TEXT]);
/* A manifest's task finished: what it made local read, or that directory given up. */
struct wgf_asset_priv_task_t;
void wgf_asset_priv_manifest_loaded(const struct wgf_asset_priv_task_t *task_ptr, bool ok);
/* Every manifest forgotten (wgf_asset_clear_cache, the part's stop). */
void wgf_asset_priv_manifests_forget(void);

/* ------------------------------------------------------------------ tasks ---- */

/* A request to make a file local: an ensure's (kept for the program), a load's (for
 * core's locate hook), a group, or a ping. */
typedef enum wgf_asset_priv_state_t {
    WGF_ASSET_PRIV_NEW = 0,  /* to be looked at in the next step */
    WGF_ASSET_PRIV_FETCHING, /* web: downloading, or reading the cache (cache_read) */
    WGF_ASSET_PRIV_WAITING,  /* a group on its members, a ping on its answer */
    WGF_ASSET_PRIV_DONE,
    WGF_ASSET_PRIV_FAILED
} wgf_asset_priv_state_t;

typedef struct wgf_asset_priv_task_t {
    char path[WGF_ASSET_PRIV_PATH_MAX];     /* where it is looked for now */
    char origin[WGF_ASSET_PRIV_PATH_MAX];   /* the path as asked for, before redirects */
    char fetch_url[WGF_ASSET_PRIV_URL_MAX]; /* its source when not the host + path: "" none */
    bool caller_url;                        /* the caller named the source: no redirects */
    wgf_asset_priv_candidate_t *candidates; /* the redirects' paths still to try (malloc'd), or NULL */
    int candidate_count, candidate_next;
    bool overlay;
    unsigned int flags;
    wgf_asset_priv_state_t state;
    bool kept;           /* the program's (an ensure, a group, a ping), until destroyed */
    bool dropped;        /* destroyed while pending: freed when it finishes */
    wgf_handle_t request; /* core's load request it locates, or 0 */
    bool arrival;         /* a file a loader reads while it arrives (core's arrival), kept for it */
    bool streams;         /* its loader takes a file arriving (a streamed sound's) */
    bool streaming;       /* web: not fetched here; its loader's element fetches it from its URL */
    /* the manifest (wgf_asset_set_manifest) */
    bool manifest_checked;                       /* looked up: expect_hash is its word, or "" (not listed) */
    char expect_hash[WGF_ASSET_PRIV_SHA256_TEXT]; /* what the file's bytes must hash to; "" anything */
    int manifest_dir;                            /* loads the tree's manifest_dir - 1th directory's; 0 none */
    unsigned manifest_generation;
    bool manifest_root; /* ... and it is the root's, asked about once a run */
    /* a listed download hashed a slice an update before it is kept (malloc'd), or NULL */
    struct wgf_asset_priv_hashing_t *hashing;
    /* web */
    int cache_read;   /* reading the file from the cache, or 0 */
    int fetch_result; /* WGF_ASSET_PRIV_FETCH_* while FETCHING a download */
    bool revalidating;
    /* the files it names (core's lister: a .gltf's buffers and images), made local with it */
    uint16_t parent;           /* slot of the task it is a dependency of; 0 none */
    bool optional;             /* one its parent can do without */
    bool dependencies_started; /* listed, once its own file was local */
    bool dependency_failed;    /* a required one failed */
    int dependency_count;      /* added in all */
    /* groups */
    bool is_group;
    uint16_t group; /* slot of the group it is in; 0 none */
    int pending, member_count, failed_members;
    /* natively, a download the program makes (wgf_asset_set_fetching) */
    bool fetch_taken;     /* handed out by wgf_asset_fetch_next */
    uint32_t fetch_order; /* requests are handed out oldest first */
    double fetch_taken_at; /* when, for the fetch timeout */
    /* pings */
    bool is_ping;
    int ping_id;
    float ping_ms;
} wgf_asset_priv_task_t;

enum {
    WGF_ASSET_PRIV_FETCH_PENDING = 0,
    WGF_ASSET_PRIV_FETCH_OK,
    WGF_ASSET_PRIV_FETCH_FAILED,
    WGF_ASSET_PRIV_FETCH_USE_CACHE /* the cached copy is current */
};

/* The task in `slot`, or NULL; good until the next task is made. */
wgf_asset_priv_task_t *wgf_asset_priv_task_at(uint16_t slot);
/* A new task for `path` (normalized) looked for through the redirects (`fetch_url` NULL)
 * or from `fetch_url` alone (its source, resolved; `local` when that is a file under a
 * local host), and its slot; 0 without room. */
uint16_t wgf_asset_priv_task_new(const char *path, const char *fetch_url, bool local, unsigned int flags);
/* `named` (normalized) is a file `file` names (a glTF's buffer or image): wgf_asset_reload
 * of `named` loads `file` again. */
void wgf_asset_priv_note_named(const char *file, const char *named);
/* A new task for exactly `path`, no redirects (a manifest's); 0 without room. */
uint16_t wgf_asset_priv_task_new_direct(const char *path);
/* Move every pending task on, and `slot`'s alone (0: all). */
void wgf_asset_priv_tasks_step(uint16_t slot);
/* A task's file is local (`ok`) or can't be had: kept, freed, or its group told. */
void wgf_asset_priv_task_resolved(uint16_t slot, bool ok);
/* A task's handle (its generation tells a reused slot apart: what a fetch in flight is
 * known by), and the slot of a live one's handle (0: gone). */
wgf_handle_t wgf_asset_priv_task_handle(uint16_t slot);
uint16_t wgf_asset_priv_task_slot(wgf_handle_t handle);
/* How many task slots there are: slots run 1 to this, less one. */
uint16_t wgf_asset_priv_task_capacity(void);
/* The task locating core's load request `request`, or 0. */
uint16_t wgf_asset_priv_task_of_request(wgf_handle_t request);
/* Where an ensure with an explicit source found `key` (a path), when somewhere else; NULL
 * when it didn't. */
const char *wgf_asset_priv_found(const char *key);
/* Free a task's slot. */
void wgf_asset_priv_task_free(uint16_t slot);
/* Let go of a kept task: freed now if it has finished, else when it does. */
void wgf_asset_priv_task_let_go(uint16_t slot);
/* Every task forgotten, at the part's stop. */
void wgf_asset_priv_tasks_stop(void);

/* --------------------------------------------------------- per platform ---- */

/* Once an update, before the tasks' steps: natively the program's answers applied, and
 * requests past the fetch timeout failed. */
void wgf_asset_priv_platform_update(void);
/* The host just set (`url`: a URL): natively a URL host's files are the cache directory's. */
void wgf_asset_priv_platform_host_set(bool url);

/* A host just set (`host`, the settings' own, its trailing slashes gone) taken by the
 * platform: natively a local one becomes the storage's root, a relative one under the
 * program's directory, and `host` is cleared (the root is read live from then on); in a
 * browser a file: URL is warned of. False for a local host too long a path. */
bool wgf_asset_priv_platform_set_host(char *host);

/* A file that didn't load given one more try: in a browser its cached copy forgotten
 * and fetched again (true), when there was one; natively the file is the program's, and
 * nothing is fetched again (false). */
bool wgf_asset_priv_platform_refetch(const char *path);

/* The copy of `logical` the cache keeps, forgotten; false when it can't be. */
bool wgf_asset_priv_platform_evict(const char *logical);

/* How the host's files are found: a browser's (fetched beside the page), or natively a
 * URL host's (downloaded) or a local one's (read). */
wgf_asset_priv_host_kind_t wgf_asset_priv_platform_host_kind(void);
/* The part installed: natively the cache directory made the storage's cache root. */
void wgf_asset_priv_platform_install(void);
/* Whether a file can be listed by a manifest here: on the web always, natively only when
 * the host is a URL and the program downloads. */
bool wgf_asset_priv_platform_lists(void);
/* Forget every download this platform made (wgf_asset_clear_cache): natively the cache
 * directory's own, by its list. */
void wgf_asset_priv_platform_clear(void);

/* One step of a file task, on this platform: from the cache, the host, or a download. */
void wgf_asset_priv_platform_step(uint16_t slot);
/* Whether a task's file is arriving where it can be read meanwhile: natively a download
 * the program has taken and begun to write (fs's rule for a file read while it
 * arrives); on the web, one its loader fetches itself (`streaming`), which would
 * otherwise be downloaded, not being in the cache. */
bool wgf_asset_priv_platform_arriving(const wgf_asset_priv_task_t *task_ptr);
/* A task about to be freed (`handle` its handle): what this platform holds for it let go
 * of (a fetch in flight forgotten). */
void wgf_asset_priv_platform_task_freed(wgf_asset_priv_task_t *task_ptr, wgf_handle_t handle);
/* A ping of `host` begun: the task's ping_id or ping_ms set. */
void wgf_asset_priv_platform_ping(wgf_asset_priv_task_t *task_ptr, const char *host, int timeout_ms);
/* A ping's answer: milliseconds, -1 unreachable, -2 still waiting. */
float wgf_asset_priv_platform_ping_poll(wgf_asset_priv_task_t *task_ptr);
/* The default host on this platform (natively the storage root; on the web the
 * directory of the document's base URL), into `out`. */
void wgf_asset_priv_platform_default_host(char *out, size_t out_size);

#endif /* WGF_ASSET_PRIV_H */
