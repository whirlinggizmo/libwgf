#ifndef WGF_CORE_FS_PRIV_H
#define WGF_CORE_FS_PRIV_H

#include <stdbool.h>
#include <stddef.h>

/* Local file storage, no network. Native: real files via stdio, relative to the
 * working dir. Web: files in MEMFS under a root dir, kept between visits in
 * IndexedDB, one record per file. Init reads only the cache's list of files; a
 * cached file is read into MEMFS when it's needed (wgf_core_priv_fs_cache_read_begin),
 * so startup doesn't grow with the cache. A cached file can carry metadata
 * (wgf_core_priv_fs_meta_t): on the web in a second IndexedDB store, read at init
 * with the list; native in a sidecar under the root's ".meta/".
 *
 * The public wgf_fs.h sits on top of this: its requests are tasks that run
 * in wgf_core_priv_fs_update. Cribbed from wgrender's wgr_fs. */

/* The longest path a request may name, its NUL included. */
#define WGF_CORE_PRIV_FS_PATH_MAX 1024

void wgf_core_priv_fs_init(const char *root_dir); /* root_dir NULL -> platform default */
void wgf_core_priv_fs_deinit(void);
/* Run the public API's pending tasks; wgf_update calls it. */
void wgf_core_priv_fs_update(void);

/* Override the local root (base dir reads/writes resolve against). */
void wgf_core_priv_fs_set_root(const char *root);
const char *wgf_core_priv_fs_root(void);

/* Native: a path starting with WGF_CORE_PRIV_FS_CACHE names a file under the cache
 * root rather than the root -- a download kept apart from a local host, which is
 * only ever read. No key has a ":", so the prefix can't be one's own. Unused on
 * the web, whose root is the cache. */
#define WGF_CORE_PRIV_FS_CACHE "cache:"
void wgf_core_priv_fs_set_cache_root(const char *root);

/* `path` as a key under the root, into `out`: "\\" read as "/", "." and empty
 * parts dropped, ".." taking back the part before it. False when it is absolute,
 * has a ":" (a drive, or the cache prefix), has a control character, climbs above
 * the root, names nothing, or doesn't fit. What is true can only name something
 * under the root. Cribbed from wgrender's wgri_asset_normalize_path. */
bool wgf_core_priv_fs_normalize_path(const char *path, char *out, size_t out_size);

/* Build the directly-openable local path for `path` (root + path). */
void wgf_core_priv_fs_resolve(const char *path, char *out, size_t out_size);

/* True once the local store is usable. Native: always. Web: once the cache's list
 * of files is read. */
bool wgf_core_priv_fs_is_ready(void);

/* True when nothing written is still on its way to the store. Native: always.
 * Web: once every write, removal, and clear sent to IndexedDB has finished. */
bool wgf_core_priv_fs_is_settled(void);

/* Is `path` local (openable) right now? */
bool wgf_core_priv_fs_exists(const char *path);

/* Directories. is_dir follows a link; is_real_dir doesn't, so a link to a
 * directory isn't one. mkdir makes the parents too and is true when the directory
 * exists after; rmdir removes everything under it, never following a link out
 * of it, and is false when there is no real directory there or something
 * couldn't go. */
bool wgf_core_priv_fs_is_dir(const char *path);
bool wgf_core_priv_fs_is_real_dir(const char *path);
bool wgf_core_priv_fs_mkdir(const char *path);
bool wgf_core_priv_fs_rmdir(const char *path);

/* Read an entire file into a malloc'd buffer (NUL-terminated for convenience;
 * `*out_size` excludes the terminator). Free it with wgf_core_priv_fs_read_free. */
bool wgf_core_priv_fs_read(const char *path, unsigned char **out_data, int *out_size);
void wgf_core_priv_fs_read_free(unsigned char *data);

/* What is known about where a cached file came from, kept beside it: the
 * response's validators, how long it may be used without asking, and the hash of
 * the bytes stored. An empty string is "none". A value too long for its field is
 * dropped, never cut: a cut ETag would ask the server about some other version. */
typedef struct wgf_core_priv_fs_meta_t {
    char etag[128];
    char last_modified[64];
    double fresh_until; /* seconds since 1970 (wall clock); 0 = never fresh */
    char hash[72];      /* "sha256:" + 64 hex */
    char source[1024];  /* native: the URL a download came from ("" = the host's own) */
} wgf_core_priv_fs_meta_t;

/* Write a file (creating parent dirs); on the web also keep it in the cache. Any
 * metadata the old file had is dropped: it described other bytes. */
bool wgf_core_priv_fs_write(const char *path, const unsigned char *data, int size);

/* The same, with the new bytes' metadata kept with them: on the web in one
 * transaction, so a failed store never leaves one without the other; native, the
 * old metadata goes before the file is written and the new comes after, so an
 * interruption leaves none rather than the wrong one. */
bool wgf_core_priv_fs_write_meta(const char *path, const unsigned char *data, int size,
                                const wgf_core_priv_fs_meta_t *meta);

/* A cached file's metadata: false (and `out` zeroed) when it has none. Web: of the
 * copy in the cache; native: of the file under the root. */
bool wgf_core_priv_fs_meta_get(const char *path, wgf_core_priv_fs_meta_t *out);

/* Replace a cached file's metadata, its bytes staying as they are (a 304's new
 * freshness). False when there is no such file (web: none in the cache). */
bool wgf_core_priv_fs_meta_set(const char *path, const wgf_core_priv_fs_meta_t *meta);

/* Forget a file and its metadata (web: the IndexedDB entry too), so the next read
 * fetches it again. wgf_core_priv_fs_clear forgets the whole cache: on the web the
 * store and this visit's copies (MEMFS under the root); native, nothing (a
 * directory's files are not all the cache's). */
bool wgf_core_priv_fs_remove(const char *path);
void wgf_core_priv_fs_clear(void);

/* Where a download of `path` is written until it is whole: under the same root's
 * ".part/", as metadata is under ".meta/" -- never beside the file, where
 * "foo.png.part" could be an asset's own name. False when it doesn't fit.
 *
 * A file there may be read while it arrives, by this rule: its writer only appends,
 * then closes it, then it is moved into place (wgf_core_priv_fs_replace); a reader
 * reads up to the size it sees, never past it, so what it has read is always the
 * start of the whole file, and once the file is gone from ".part/" it reads on from
 * the same offset under the final path. A reader holds no file open between reads
 * (wgf_core_priv_fs_read_at), and reads on the thread that makes the move: Windows
 * can't move a file another handle has open. */
bool wgf_core_priv_fs_partial_path(const char *path, char *out, size_t out_size);

/* Up to `max` bytes of `path` from byte `offset` into `out`, opening and closing it:
 * how many were there, 0 at or past its end, and -1 when there is no such file or
 * it can't be read. */
long long wgf_core_priv_fs_read_at(const char *path, unsigned long long offset, unsigned char *out, size_t max);

/* Move `from` over `to` in one step, replacing what is there: a finished download
 * taking its place, so nothing reads half a file. */
bool wgf_core_priv_fs_replace(const char *from, const char *to);

/* Create the directories above `path`, so something else -- a fetcher writing a
 * download -- can open it for writing. */
void wgf_core_priv_fs_make_parents(const char *path);

/* Web: keep written files between visits (the default). Off, writes stay in MEMFS
 * for this visit and the cache is left as it is: nothing is added, and nothing in
 * it counts as cached. Native: files are files; ignored. */
void wgf_core_priv_fs_set_persistent(bool persistent);

/* Web: is `path` in the cache, readable into the local store? Native: never. */
bool wgf_core_priv_fs_is_cached(const char *path);

/* Read a cached file into the local store, asynchronously: begin returns an id (0
 * when it isn't cached); poll it each frame: 0 = still reading, 1 = now local,
 * -1 = failed (the file is dropped from the cache; fetch it instead). */
int wgf_core_priv_fs_cache_read_begin(const char *path);
int wgf_core_priv_fs_cache_read_poll(int id);

/* Implemented by wgf_core_fs_native.c or wgf_core_fs_web.c; wgf_core_fs.c calls them. */
const char *wgf_core_priv_fs_platform_default_root(void);
void wgf_core_priv_fs_platform_init(const char *root);
/* The root was changed after init. */
void wgf_core_priv_fs_platform_root_changed(const char *root);
/* Does the store hold files under the directory `full`, whether or not it is in
 * memory? Web: a returning visit's files are only in IndexedDB until read. Native:
 * never. */
bool wgf_core_priv_fs_platform_has_stored_dir(const char *full);
/* The directory `path` (resolved to `full`) and all under it were removed. */
void wgf_core_priv_fs_platform_removed_dir(const char *path, const char *full);
/* Around a write of `path` (resolved to `full`) with `meta` or NULL. */
void wgf_core_priv_fs_platform_before_write(const char *path);
void wgf_core_priv_fs_platform_after_write(const char *path, const char *full, const unsigned char *data,
                                          int size, const wgf_core_priv_fs_meta_t *meta);

#endif
