#include "wgf_core_fs_priv.h"

#include <stdio.h>
#include <string.h>

#include <emscripten.h>

#include "wgf_log.h"

/* Web storage: files are read and written in MEMFS under the root (default "/wgf"),
 * and kept between visits in an IndexedDB store, one record per file: init reads
 * only the store's list of paths, a cached file is read into MEMFS when it's
 * needed (wgf_core_priv_fs_cache_read_begin), and a write stores the file. Each
 * file's metadata, if it has any, is in a second store ("meta", same key), all of
 * which init reads with the list. */

/* Bump to invalidate every cached file on the next visit. The last resort, not how
 * a changed file reaches a returning visitor: revalidation does that. This is for a
 * cached file that is wrong in a way nothing can detect -- wgrender once stored a
 * compressing host's gzip bytes under an asset's name, and a font made of those is
 * accepted and then fails later rather than at once. */
#define FS_CACHE_EPOCH 1

static bool fs_transient; /* wgf_core_priv_fs_set_persistent(false) */

/* Open the store and read its keys (full MEMFS paths). It can't be awaited (JSPI
 * can't suspend inside a RAF-driven callback), so this is a polled barrier:
 * Module.wgf_core_fs_state is 0 pending / 1 ready / 2 no store (files still work in
 * MEMFS, nothing persists), surfaced by wgf_core_priv_fs_is_ready(). */
EM_JS(void, wgf_core_priv_fs_js_open, (const char *root_c, int epoch), {
    const root = UTF8ToString(root_c);
    Module.wgf_core_fs_state = 0;
    Module.wgf_core_fs_keys = new Set();
    Module.wgf_core_fs_meta = new Map();    /* full path -> the stored file's metadata */
    Module.wgf_core_fs_pending = new Map(); /* full path -> its last store write in flight */
    Module.wgf_core_fs_ops = 0;
    Module.wgf_core_fs_cleared = 0;         /* writes up to this op were cleared away */
    Module.wgf_core_fs_reads = new Map();
    Module.wgf_core_fs_next_read = 1;
    const done = (state, err) => {
        Module.wgf_core_fs_state = state;
        performance.mark("wgf:fs-ready");
        if (err) console.warn("wgf_core_fs: no persistent cache", err);
    };
    try {
        FS.mkdirTree(root);
        const open = indexedDB.open("wgf_core_fs:" + root, 1);
        open.onupgradeneeded = () => {
            const db = open.result;
            if (!db.objectStoreNames.contains("files")) db.createObjectStore("files");
            if (!db.objectStoreNames.contains("meta")) db.createObjectStore("meta");
        };
        open.onerror = () => done(2, open.error);
        open.onsuccess = () => {
            Module.wgf_core_fs_db = open.result;
            const listKeys = () => {
                /* the metadata too, which is small: a check of it has to answer now */
                const tx = Module.wgf_core_fs_db.transaction(["files", "meta"]);
                const keys = tx.objectStore("files").getAllKeys();
                const metaKeys = tx.objectStore("meta").getAllKeys();
                const metas = tx.objectStore("meta").getAll(); /* in the same key order */
                tx.oncomplete = () => {
                    for (const key of keys.result) Module.wgf_core_fs_keys.add(key);
                    metaKeys.result.forEach((key, i) => {
                        if (Module.wgf_core_fs_keys.has(key)) Module.wgf_core_fs_meta.set(key, metas.result[i]);
                    });
                    done(1);
                };
                tx.onabort = () => done(2, tx.error);
            };
            const epochKey = "\u0000wgf_cache_epoch";
            const store = Module.wgf_core_fs_db.transaction("files").objectStore("files");
            const got = store.get(epochKey);
            got.onsuccess = () => {
                if (got.result === epoch) {
                    listKeys();
                    return;
                }
                console.info("wgf_core_fs: cache from an older build, clearing it");
                const tx = Module.wgf_core_fs_db.transaction(["files", "meta"], "readwrite");
                const files = tx.objectStore("files");
                files.clear();
                tx.objectStore("meta").clear();
                files.put(epoch, epochKey);
                tx.oncomplete = () => listKeys();
                tx.onabort = () => done(2, tx.error);
            };
            got.onerror = () => listKeys(); /* can't tell: keep what is there */
        };
    } catch (e) {
        done(2, e);
    }
})

EM_JS(int, wgf_core_priv_fs_js_state, (void), { return Module.wgf_core_fs_state | 0; })

/* Nothing still on its way to the store: no write, removal, or clear in flight. */
EM_JS(int, wgf_core_priv_fs_js_settled, (void), {
    return (!Module.wgf_core_fs_pending || Module.wgf_core_fs_pending.size === 0) &&
           !(Module.wgf_core_fs_clearing > 0) ? 1 : 0;
})

/* Does the store list any file under `prefix` (a directory's full path plus "/")? */
EM_JS(int, wgf_core_priv_fs_js_has_prefix, (const char *prefix_c), {
    if (!Module.wgf_core_fs_keys) return 0;
    const prefix = UTF8ToString(prefix_c);
    for (const key of Module.wgf_core_fs_keys) if (key.startsWith(prefix)) return 1;
    return 0;
})

EM_JS(int, wgf_core_priv_fs_js_has, (const char *full_c), {
    return Module.wgf_core_fs_keys && Module.wgf_core_fs_keys.has(UTF8ToString(full_c)) ? 1 : 0;
})

/* Read a cached file into MEMFS; returns an id for wgf_core_priv_fs_js_read_state. */
EM_JS(int, wgf_core_priv_fs_js_read, (const char *full_c), {
    const full = UTF8ToString(full_c);
    const id = Module.wgf_core_fs_next_read++;
    const fail = (err) => {
        Module.wgf_core_fs_keys.delete(full);
        Module.wgf_core_fs_reads.set(id, 2);
        console.warn("wgf_core_fs: couldn't read " + full + " from the cache", err);
    };
    Module.wgf_core_fs_reads.set(id, 0);
    try {
        const get = Module.wgf_core_fs_db.transaction("files").objectStore("files").get(full);
        get.onsuccess = () => {
            if (!(get.result instanceof Blob)) return fail("missing");
            get.result.arrayBuffer().then((buffer) => { /* read off the main thread */
                FS.mkdirTree(full.substring(0, full.lastIndexOf("/")) || "/");
                FS.writeFile(full, new Uint8Array(buffer), { canOwn: true }); /* no copy */
                Module.wgf_core_fs_reads.set(id, 1);
            }).catch(fail);
        };
        get.onerror = () => fail(get.error);
    } catch (e) {
        fail(e);
    }
    return id;
})

/* 0 pending, 1 read (the id is done), 2 failed (the id is done). */
EM_JS(int, wgf_core_priv_fs_js_read_state, (int id), {
    const state = Module.wgf_core_fs_reads.get(id) | 0;
    if (state !== 0) Module.wgf_core_fs_reads.delete(id);
    return state;
})

/* Keep a file for later visits, with its metadata or none (asynchronous; a failure
 * only means it isn't kept). File and metadata are one transaction, so the store
 * never pairs new bytes with old validators, or old bytes with new ones. The
 * in-memory lists follow when it commits, unless a later write, delete or clear of
 * the same path has overtaken it. */
EM_JS(void, wgf_core_priv_fs_js_put, (const char *full_c, const unsigned char *data, int size, int has_meta,
                                     const char *etag_c, const char *modified_c, double fresh_until,
                                     const char *hash_c), {
    if (!Module.wgf_core_fs_db) return;
    const full = UTF8ToString(full_c);
    const meta = has_meta ? {
        etag: UTF8ToString(etag_c),
        lastModified: UTF8ToString(modified_c),
        freshUntil: fresh_until,
        hash: UTF8ToString(hash_c),
    } : null;
    /* a Blob: reading it back doesn't unpack the bytes on the main thread */
    const blob = new Blob([HEAPU8.slice(data, data + size)]);
    const op = ++Module.wgf_core_fs_ops;
    Module.wgf_core_fs_pending.set(full, op);
    const settle = () => {
        const current = Module.wgf_core_fs_pending.get(full) === op;
        if (current) Module.wgf_core_fs_pending.delete(full);
        return current && op > Module.wgf_core_fs_cleared;
    };
    try {
        const tx = Module.wgf_core_fs_db.transaction(["files", "meta"], "readwrite");
        tx.objectStore("files").put(blob, full);
        if (meta) tx.objectStore("meta").put(meta, full);
        else tx.objectStore("meta").delete(full);
        tx.oncomplete = () => {
            if (!settle()) return;
            Module.wgf_core_fs_keys.add(full);
            if (meta) Module.wgf_core_fs_meta.set(full, meta);
            else Module.wgf_core_fs_meta.delete(full);
        };
        tx.onabort = () => {
            settle();
            console.warn("wgf_core_fs: couldn't cache " + full, tx.error);
        };
    } catch (e) {
        settle();
        console.warn("wgf_core_fs: couldn't cache " + full, e);
    }
})

/* Copy a cached file's metadata into C's buffers; 0 when it has none. A value that
 * doesn't fit is left empty, not cut (wgf_core_priv_fs_meta_t). */
EM_JS(int, wgf_core_priv_fs_js_meta_get, (const char *full_c, char *etag, int etag_size, char *modified,
                                         int modified_size, char *hash, int hash_size, double *fresh_until), {
    const meta = Module.wgf_core_fs_meta && Module.wgf_core_fs_meta.get(UTF8ToString(full_c));
    if (!meta) return 0;
    const copy = (text, out, out_size) => {
        text = typeof text === "string" ? text : "";
        stringToUTF8(lengthBytesUTF8(text) < out_size ? text : "", out, out_size);
    };
    copy(meta.etag, etag, etag_size);
    copy(meta.lastModified, modified, modified_size);
    copy(meta.hash, hash, hash_size);
    HEAPF64[fresh_until >> 3] = +meta.freshUntil || 0;
    return 1;
})

/* Replace a cached file's metadata; 0 when the file isn't in the store, or a write of
 * it is still in flight (the write brings its own). */
EM_JS(int, wgf_core_priv_fs_js_meta_set, (const char *full_c, const char *etag_c, const char *modified_c,
                                         double fresh_until, const char *hash_c), {
    const full = UTF8ToString(full_c);
    if (!Module.wgf_core_fs_db || !Module.wgf_core_fs_keys.has(full) || Module.wgf_core_fs_pending.has(full)) return 0;
    const meta = {
        etag: UTF8ToString(etag_c),
        lastModified: UTF8ToString(modified_c),
        freshUntil: fresh_until,
        hash: UTF8ToString(hash_c),
    };
    /* at once: it describes the same bytes as before, so a failure to keep it leaves
       the store's older metadata, which is still true of them */
    Module.wgf_core_fs_meta.set(full, meta);
    try {
        const tx = Module.wgf_core_fs_db.transaction("meta", "readwrite");
        tx.objectStore("meta").put(meta, full);
        tx.onabort = () => console.warn("wgf_core_fs: couldn't keep the metadata of " + full, tx.error);
    } catch (e) {
        console.warn("wgf_core_fs: couldn't keep the metadata of " + full, e);
    }
    return 1;
})

/* Forget one cached file. A cached file can be wrong, and without this there is no
 * way back: the bad copy is read in preference to the network, for good. */
EM_JS(void, wgf_core_priv_fs_js_delete, (const char *full_c), {
    const full = UTF8ToString(full_c);
    if (Module.wgf_core_fs_keys) Module.wgf_core_fs_keys.delete(full);
    if (Module.wgf_core_fs_meta) Module.wgf_core_fs_meta.delete(full);
    if (!Module.wgf_core_fs_db) return;
    const op = ++Module.wgf_core_fs_ops; /* overtakes a write still in flight */
    Module.wgf_core_fs_pending.set(full, op);
    const settle = () => {
        if (Module.wgf_core_fs_pending.get(full) === op) Module.wgf_core_fs_pending.delete(full);
    };
    try {
        const tx = Module.wgf_core_fs_db.transaction(["files", "meta"], "readwrite");
        tx.objectStore("files").delete(full);
        tx.objectStore("meta").delete(full);
        tx.oncomplete = settle;
        tx.onabort = settle;
    } catch (e) {
        settle();
        console.warn("wgf_core_fs: couldn't forget " + full, e);
    }
})

/* This visit's copies of the files under `root` (MEMFS), gone: with the store cleared,
 * the next read of any of them has to fetch it. */
EM_JS(void, wgf_core_priv_fs_js_memfs_clear, (const char *root_c), {
    const walk = (dir) => {
        let names;
        try { names = FS.readdir(dir); } catch (e) { return; }
        for (const name of names) {
            if (name === "." || name === "..") continue;
            const path = dir + "/" + name;
            try {
                if (FS.isDir(FS.stat(path).mode)) {
                    walk(path);
                    FS.rmdir(path);
                } else {
                    FS.unlink(path);
                }
            } catch (e) {
                console.warn("wgf_core_fs: couldn't remove " + path, e);
            }
        }
    };
    walk(UTF8ToString(root_c));
})

/* Forget every cached file under `prefix` (a directory's full path plus "/"), as
 * a removed directory's. A write of one still in flight lands after this and is
 * then kept; a removed directory written into at the same time is a race the
 * caller made. */
EM_JS(void, wgf_core_priv_fs_js_delete_prefix, (const char *prefix_c), {
    const prefix = UTF8ToString(prefix_c);
    if (Module.wgf_core_fs_keys) {
        for (const key of Array.from(Module.wgf_core_fs_keys)) {
            if (!key.startsWith(prefix)) continue;
            Module.wgf_core_fs_keys.delete(key);
            if (Module.wgf_core_fs_meta) Module.wgf_core_fs_meta.delete(key);
        }
    }
    if (!Module.wgf_core_fs_db) return;
    Module.wgf_core_fs_clearing = (Module.wgf_core_fs_clearing | 0) + 1;
    const settle = () => { Module.wgf_core_fs_clearing--; };
    try {
        const range = IDBKeyRange.bound(prefix, prefix + "\uffff");
        const tx = Module.wgf_core_fs_db.transaction(["files", "meta"], "readwrite");
        tx.objectStore("files").delete(range);
        tx.objectStore("meta").delete(range);
        tx.oncomplete = settle;
        tx.onabort = settle;
    } catch (e) {
        settle();
        console.warn("wgf_core_fs: couldn't forget " + prefix, e);
    }
})

EM_JS(void, wgf_core_priv_fs_js_clear, (void), {
    if (Module.wgf_core_fs_keys) Module.wgf_core_fs_keys.clear();
    if (Module.wgf_core_fs_meta) Module.wgf_core_fs_meta.clear();
    if (!Module.wgf_core_fs_db) return;
    Module.wgf_core_fs_cleared = Module.wgf_core_fs_ops; /* writes in flight land before the clear */
    Module.wgf_core_fs_clearing = (Module.wgf_core_fs_clearing | 0) + 1;
    const settle = () => { Module.wgf_core_fs_clearing--; };
    try {
        const tx = Module.wgf_core_fs_db.transaction(["files", "meta"], "readwrite");
        tx.objectStore("files").clear();
        tx.objectStore("meta").clear();
        tx.oncomplete = settle;
        tx.onabort = settle;
    } catch (e) {
        settle();
        console.warn("wgf_core_fs: couldn't clear the cache", e);
    }
})

const char *wgf_core_priv_fs_platform_default_root(void)
{
    return "/wgf";
}

void wgf_core_priv_fs_platform_init(const char *root)
{
    /* Open the cache (its list of files); wgf_core_priv_fs_is_ready() reflects it. */
    wgf_core_priv_fs_js_open(root, FS_CACHE_EPOCH);
    wgf_log_info("wgf_core_fs: files in %s, kept in IndexedDB", root);
}

bool wgf_core_priv_fs_is_ready(void)
{
    return wgf_core_priv_fs_js_state() != 0; /* 1 = opened, 2 = no cache (files still work) */
}

bool wgf_core_priv_fs_is_settled(void)
{
    return wgf_core_priv_fs_js_settled() != 0;
}

bool wgf_core_priv_fs_platform_has_stored_dir(const char *full)
{
    char prefix[1104];
    snprintf(prefix, sizeof(prefix), "%s/", full);
    return wgf_core_priv_fs_js_has_prefix(prefix) != 0;
}

void wgf_core_priv_fs_platform_root_changed(const char *root)
{
    /* another root is another store: open it, and requests wait until it is */
    wgf_core_priv_fs_js_open(root, FS_CACHE_EPOCH);
    wgf_log_info("wgf_core_fs: files in %s, kept in IndexedDB", root);
}

void wgf_core_priv_fs_platform_removed_dir(const char *path, const char *full)
{
    char prefix[1104];
    (void)path;
    snprintf(prefix, sizeof(prefix), "%s/", full);
    wgf_core_priv_fs_js_delete_prefix(prefix);
}

void wgf_core_priv_fs_platform_before_write(const char *path)
{
    (void)path; /* the store replaces a file's metadata with its bytes, in one transaction */
}

void wgf_core_priv_fs_platform_after_write(const char *path, const char *full, const unsigned char *data,
                                          int size, const wgf_core_priv_fs_meta_t *meta)
{
    (void)path;
    if (fs_transient) return;
    wgf_core_priv_fs_js_put(full, data, size, meta != NULL, meta != NULL ? meta->etag : "",
                           meta != NULL ? meta->last_modified : "", meta != NULL ? meta->fresh_until : 0.0,
                           meta != NULL ? meta->hash : "");
}

bool wgf_core_priv_fs_meta_get(const char *path, wgf_core_priv_fs_meta_t *out)
{
    char full[1100];
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
    return wgf_core_priv_fs_js_meta_get(full, out->etag, (int)sizeof(out->etag), out->last_modified,
                                       (int)sizeof(out->last_modified), out->hash, (int)sizeof(out->hash),
                                       &out->fresh_until) != 0;
}

bool wgf_core_priv_fs_meta_set(const char *path, const wgf_core_priv_fs_meta_t *meta)
{
    char full[1100];
    if (meta == NULL) return false;
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
    return wgf_core_priv_fs_js_meta_set(full, meta->etag, meta->last_modified, meta->fresh_until, meta->hash) != 0;
}

bool wgf_core_priv_fs_remove(const char *path)
{
    char full[1100];
    bool stored;
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
    stored = wgf_core_priv_fs_js_has(full) != 0;
    wgf_core_priv_fs_js_delete(full); /* the cache, so the next read goes to the network */
    return (remove(full) == 0) || stored; /* this visit's copy, the stored one, or both */
}

void wgf_core_priv_fs_clear(void)
{
    const char *root = wgf_core_priv_fs_root();
    wgf_core_priv_fs_js_clear();
    if (root[0] != '\0') wgf_core_priv_fs_js_memfs_clear(root);
}

void wgf_core_priv_fs_set_persistent(bool persistent)
{
    fs_transient = !persistent;
}

bool wgf_core_priv_fs_is_cached(const char *path)
{
    char full[1100];
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    return !fs_transient && full[0] != '\0' && wgf_core_priv_fs_js_has(full);
}

int wgf_core_priv_fs_cache_read_begin(const char *path)
{
    char full[1100];
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    return !fs_transient && full[0] != '\0' && wgf_core_priv_fs_js_has(full) ? wgf_core_priv_fs_js_read(full) : 0;
}

int wgf_core_priv_fs_cache_read_poll(int id)
{
    const int state = id > 0 ? wgf_core_priv_fs_js_read_state(id) : 2;
    return state == 0 ? 0 : (state == 1 ? 1 : -1);
}
