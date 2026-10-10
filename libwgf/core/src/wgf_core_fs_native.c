#include "wgf_core_fs_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_log.h"
#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"

/* Native storage: files are files under the root, by default the program's own
 * directory (the executable's; Rob, 2026-10-04: a double-clicked program finds its
 * files, wherever it was started from), never the working directory; "" where the
 * executable's directory can't be told, so paths resolve as-is. There is no cache to
 * read from, so nothing is ever "cached"; a file's metadata is a sidecar under the
 * root's ".meta/". */

const char *wgf_core_priv_fs_platform_default_root(void)
{
    static char dir[1024];
    if (dir[0] == '\0' && !wgf_core_priv_os_executable_dir(dir, sizeof(dir))) dir[0] = '\0';
    return dir;
}

void wgf_core_priv_fs_platform_init(const char *root)
{
    if (root[0] == '/' || (root[0] != '\0' && root[1] == ':')) {
        wgf_log_info("wgf_core_fs: files under %s", root);
    } else {
        char *cwd = wgf_core_priv_os_getcwd();
        wgf_log_info("wgf_core_fs: files under the working dir (%s/%s)", cwd != NULL ? cwd : "?", root);
        free(cwd);
    }
}

bool wgf_core_priv_fs_is_ready(void)
{
    return true;
}

/* The sidecar under the root's ".meta/", mirroring the file's path, as a path fs
 * resolves. Never beside the file: "foo.png.meta" could be an asset's own name.
 * Only relative paths have one (an absolute path isn't the cache's). */
#define META_DIR ".meta"

static bool meta_path(const char *path, char *out, size_t out_size)
{
    const bool cached =
        path != NULL && strncmp(path, WGF_CORE_PRIV_FS_CACHE, sizeof(WGF_CORE_PRIV_FS_CACHE) - 1) == 0;
    if (cached) path += sizeof(WGF_CORE_PRIV_FS_CACHE) - 1; /* the sidecar is under the file's own root */
    if (path == NULL || path[0] == '\0' || path[0] == '/') return false;
    return (size_t)snprintf(out, out_size, "%s" META_DIR "/%s", cached ? WGF_CORE_PRIV_FS_CACHE : "", path) <
           out_size;
}

static void meta_remove(const char *path)
{
    char rel[600], side[1100];
    if (!meta_path(path, rel, sizeof(rel))) return;
    wgf_core_priv_fs_resolve(rel, side, sizeof(side));
    remove(side);
}

/* One "key value" line each; a value is one header's worth, so never a newline. */
static bool meta_write(const char *path, const wgf_core_priv_fs_meta_t *meta)
{
    char rel[600], side[1100];
    FILE *f;
    bool ok;
    if (!meta_path(path, rel, sizeof(rel))) return false;
    wgf_core_priv_fs_resolve(rel, side, sizeof(side));
    wgf_core_priv_fs_make_parents(rel);
    f = fopen(side, "wb");
    if (f == NULL) return false;
    ok = fprintf(f, "wgf_meta 1\netag %s\nlast-modified %s\nfresh-until %.17g\nhash %s\nsource %s\n", meta->etag,
                 meta->last_modified, meta->fresh_until, meta->hash, meta->source) > 0;
    return (fclose(f) == 0) && ok;
}

/* Copy a sidecar value into a field; too long for it and it stays empty (never cut). */
static void meta_field(char *out, size_t out_size, const char *value)
{
    if (strlen(value) < out_size) snprintf(out, out_size, "%s", value);
}

static bool meta_read(const char *path, wgf_core_priv_fs_meta_t *out)
{
    char rel[600], side[1100];
    char line[1100];
    FILE *f;
    bool versioned = false;
    if (!meta_path(path, rel, sizeof(rel))) return false;
    wgf_core_priv_fs_resolve(rel, side, sizeof(side));
    f = fopen(side, "rb");
    if (f == NULL) return false;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t n = strlen(line);
        if (n == 0 || line[n - 1] != '\n') { /* a line longer than any field: skip the rest */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        line[--n] = '\0';
        if (n > 0 && line[n - 1] == '\r') line[--n] = '\0';
        if (strcmp(line, "wgf_meta 1") == 0) versioned = true;
        else if (strncmp(line, "etag ", 5) == 0) meta_field(out->etag, sizeof(out->etag), line + 5);
        else if (strncmp(line, "last-modified ", 14) == 0)
            meta_field(out->last_modified, sizeof(out->last_modified), line + 14);
        else if (strncmp(line, "fresh-until ", 12) == 0) out->fresh_until = strtod(line + 12, NULL);
        else if (strncmp(line, "hash ", 5) == 0) meta_field(out->hash, sizeof(out->hash), line + 5);
        else if (strncmp(line, "source ", 7) == 0) meta_field(out->source, sizeof(out->source), line + 7);
    }
    fclose(f);
    return versioned;
}

void wgf_core_priv_fs_platform_before_write(const char *path)
{
    meta_remove(path); /* before the bytes change: it describes the old ones */
}

void wgf_core_priv_fs_platform_after_write(const char *path, const char *full, const unsigned char *data,
                                          int size, const wgf_core_priv_fs_meta_t *meta)
{
    (void)full;
    (void)data;
    (void)size;
    if (meta != NULL && !meta_write(path, meta)) {
        /* the bytes landed; without their metadata they are only fetched once more */
        wgf_log_warn("wgf_core_fs: couldn't keep the metadata of %s", path);
    }
}

bool wgf_core_priv_fs_meta_get(const char *path, wgf_core_priv_fs_meta_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (!wgf_core_priv_fs_exists(path) || !meta_read(path, out)) {
        memset(out, 0, sizeof(*out)); /* a sidecar alone, or a broken one, is none */
        return false;
    }
    return true;
}

bool wgf_core_priv_fs_meta_set(const char *path, const wgf_core_priv_fs_meta_t *meta)
{
    if (meta == NULL) return false;
    return wgf_core_priv_fs_exists(path) && meta_write(path, meta);
}

bool wgf_core_priv_fs_remove(const char *path)
{
    char full[1100];
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
    meta_remove(path);
    return remove(full) == 0;
}

void wgf_core_priv_fs_clear(void)
{
    /* files are files: whoever downloaded them deletes their own, since only it
       knows which files those are */
}

void wgf_core_priv_fs_set_persistent(bool persistent)
{
    (void)persistent;
}

bool wgf_core_priv_fs_is_cached(const char *path)
{
    (void)path;
    return false;
}

int wgf_core_priv_fs_cache_read_begin(const char *path)
{
    (void)path;
    return 0;
}

int wgf_core_priv_fs_cache_read_poll(int id)
{
    (void)id;
    return -1;
}

void wgf_core_priv_fs_platform_root_changed(const char *root)
{
    wgf_log_debug("wgf_core_fs: root is now \"%s\"", root);
}

void wgf_core_priv_fs_platform_removed_dir(const char *path, const char *full)
{
    /* the metadata sidecars mirror the tree under .meta/: that part goes too */
    char rel[600], side[1100];
    (void)full;
    if (!meta_path(path, rel, sizeof(rel))) return;
    wgf_core_priv_fs_resolve(rel, side, sizeof(side));
    if (wgf_core_priv_os_is_dir(side)) wgf_core_priv_os_remove_tree(side);
}

bool wgf_core_priv_fs_is_settled(void)
{
    return true; /* a write is on disk when it returns */
}

bool wgf_core_priv_fs_platform_has_stored_dir(const char *full)
{
    (void)full; /* files are files: a directory is on disk or isn't there */
    return false;
}

/* The program's own files: its data directory, by its identity (wgf_core_identity.c). */
bool wgf_core_priv_fs_platform_user_root(const char *root, char *out, size_t out_size)
{
    (void)root;
    return wgf_core_priv_app_data_dir(out, out_size);
}
