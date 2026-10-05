#include "wgf_core_fs_priv.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_fs.h"
#include "wgf_core_handle_priv.h"
#include "wgf_log.h"
#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"

/* The part of fs that is the same everywhere: roots, paths, reading and writing
 * through stdio (real files natively, MEMFS on the web), and the public API's
 * tasks. What differs -- the web's IndexedDB cache, the native metadata sidecars
 * -- is in wgf_core_fs_native.c and wgf_core_fs_web.c. */

static char fs_root[512];
static char fs_cache_root[512]; /* native: where WGF_CORE_PRIV_FS_CACHE paths are */
static char fs_user_root[512];  /* where WGF_CORE_PRIV_FS_USER paths are; "" until the first is asked for */
static bool fs_running;

/* The root user: paths are under, found the first time: natively the program's data
 * directory, on the web the root's ".user/", since the root is what the browser keeps.
 * "" when there is none. */
static const char *user_root(void)
{
    if (fs_user_root[0] == '\0') {
        char dir[sizeof(fs_user_root)];
#if defined(__EMSCRIPTEN__)
        const bool found = snprintf(dir, sizeof(dir), "%s/.user", fs_root) < (int)sizeof(dir);
#else
        const bool found = wgf_core_priv_app_data_dir(dir, sizeof(dir));
#endif
        if (found) {
            snprintf(fs_user_root, sizeof(fs_user_root), "%s", dir);
            wgf_log_info("wgf_core_fs: the program's own files (user:) under %s", fs_user_root);
        }
    }
    return fs_user_root;
}

/* `path` without WGF_CORE_PRIV_FS_CACHE or _USER, and the root it is under. */
static const char *root_of(const char **path)
{
    if (strncmp(*path, WGF_CORE_PRIV_FS_CACHE, sizeof(WGF_CORE_PRIV_FS_CACHE) - 1) == 0) {
        *path += sizeof(WGF_CORE_PRIV_FS_CACHE) - 1;
        return fs_cache_root;
    }
    if (strncmp(*path, WGF_CORE_PRIV_FS_USER, sizeof(WGF_CORE_PRIV_FS_USER) - 1) == 0) {
        *path += sizeof(WGF_CORE_PRIV_FS_USER) - 1;
        return user_root();
    }
    return fs_root;
}

/* Copy `root` into `out` with trailing slashes trimmed, so resolve's "%s/%s" join
 * stays clean. */
static void set_trimmed(char *out, size_t out_size, const char *root)
{
    size_t n;
    snprintf(out, out_size, "%s", root != NULL ? root : "");
    n = strlen(out);
    while (n > 1 && out[n - 1] == '/') out[--n] = '\0';
}

/* Create each parent directory of `full` (best effort). */
static void mkdir_parents(const char *full)
{
    char tmp[1104]; /* as large as a path with its slash (wgf_core_priv_fs_mkdir) */
    size_t i;
    snprintf(tmp, sizeof(tmp), "%s", full);
    for (i = 1; tmp[i] != '\0'; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            wgf_core_priv_os_mkdir(tmp);
            tmp[i] = '/';
        }
    }
}

const char *wgf_core_priv_fs_root(void)
{
    return fs_root;
}

void wgf_core_priv_fs_set_root(const char *root)
{
    set_trimmed(fs_root, sizeof(fs_root), root);
#if defined(__EMSCRIPTEN__)
    fs_user_root[0] = '\0'; /* under the root on the web: found again under the new one */
#endif
    wgf_core_priv_fs_platform_root_changed(fs_root);
}

void wgf_core_priv_fs_set_cache_root(const char *root)
{
    set_trimmed(fs_cache_root, sizeof(fs_cache_root), root);
}

/* Join the root with `path` (absolute paths pass through). */
void wgf_core_priv_fs_resolve(const char *path, char *out, size_t out_size)
{
    const char *root;
    if (path == NULL) {
        out[0] = '\0';
        return;
    }
    root = root_of(&path);
    if (root[0] != '\0' && path[0] != '/') {
        snprintf(out, out_size, "%s/%s", root, path);
    } else {
        snprintf(out, out_size, "%s", path);
    }
}

void wgf_core_priv_fs_make_parents(const char *path)
{
    char full[1100];
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    mkdir_parents(full);
}

bool wgf_core_priv_fs_exists(const char *path)
{
    char full[1100];
    FILE *f;
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    f = (full[0] != '\0') ? fopen(full, "rb") : NULL;
    if (f == NULL) return false;
    fclose(f);
    return true;
}

bool wgf_core_priv_fs_is_dir(const char *path)
{
    char full[1100];
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    return full[0] != '\0' && wgf_core_priv_os_is_dir(full);
}

bool wgf_core_priv_fs_is_real_dir(const char *path)
{
    char full[1100];
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    return full[0] != '\0' && wgf_core_priv_os_is_real_dir(full);
}

bool wgf_core_priv_fs_mkdir(const char *path)
{
    char full[1100], with_slash[1104];
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
    snprintf(with_slash, sizeof(with_slash), "%s/", full); /* so the last part is made too */
    mkdir_parents(with_slash);
    return wgf_core_priv_os_is_dir(full);
}

bool wgf_core_priv_fs_rmdir(const char *path)
{
    char full[1100];
    bool in_memory;
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
    /* a link to a directory is not one: removing what it points to would reach
       outside the root. On the web the directory may be only in the store. */
    in_memory = wgf_core_priv_os_is_real_dir(full);
    if (!in_memory && !wgf_core_priv_fs_platform_has_stored_dir(full)) return false;
    if (in_memory && !wgf_core_priv_os_remove_tree(full)) return false;
    wgf_core_priv_fs_platform_removed_dir(path, full);
    return true;
}

bool wgf_core_priv_fs_read(const char *path, unsigned char **out_data, int *out_size)
{
    char full[1100];
    FILE *f;
    long size;
    unsigned char *bytes;

    if (out_data == NULL || out_size == NULL) return false;
    *out_data = NULL;
    *out_size = 0;

    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    f = (full[0] != '\0') ? fopen(full, "rb") : NULL;
    if (f == NULL) return false;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return false;
    }
    bytes = (unsigned char *)malloc((size_t)size + 1);
    if (bytes == NULL) {
        fclose(f);
        return false;
    }
    if (size > 0 && fread(bytes, 1, (size_t)size, f) != (size_t)size) {
        free(bytes);
        fclose(f);
        return false;
    }
    fclose(f);
    bytes[size] = '\0';
    *out_data = bytes;
    *out_size = (int)size;
    return true;
}

void wgf_core_priv_fs_read_free(unsigned char *data)
{
    free(data);
}

long long wgf_core_priv_fs_read_at(const char *path, unsigned long long offset, unsigned char *out, size_t max)
{
    char full[1100];
    FILE *f;
    size_t got;
    if (out == NULL && max > 0) return -1;
    if (offset > (unsigned long long)LONG_MAX) return -1; /* fseek's reach: 2 GB on Windows */
    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    f = (full[0] != '\0') ? fopen(full, "rb") : NULL;
    if (f == NULL) return -1;
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    got = max > 0 ? fread(out, 1, max, f) : 0;
    if (got < max && ferror(f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return (long long)got;
}

bool wgf_core_priv_fs_write(const char *path, const unsigned char *data, int size)
{
    return wgf_core_priv_fs_write_meta(path, data, size, NULL);
}

bool wgf_core_priv_fs_write_meta(const char *path, const unsigned char *data, int size,
                                const wgf_core_priv_fs_meta_t *meta)
{
    char full[1100];
    FILE *f;

    wgf_core_priv_fs_resolve(path, full, sizeof(full));
    if (full[0] == '\0') return false;
    wgf_core_priv_fs_platform_before_write(path);
    mkdir_parents(full);
    f = fopen(full, "wb");
    if (f == NULL) return false;
    if (size > 0 && fwrite(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return false;
    }
    fclose(f);
    wgf_core_priv_fs_platform_after_write(path, full, data, size, meta);
    return true;
}

bool wgf_core_priv_fs_partial_path(const char *path, char *out, size_t out_size)
{
    const bool cached =
        path != NULL && strncmp(path, WGF_CORE_PRIV_FS_CACHE, sizeof(WGF_CORE_PRIV_FS_CACHE) - 1) == 0;
    if (cached) path += sizeof(WGF_CORE_PRIV_FS_CACHE) - 1;
    if (path == NULL || path[0] == '\0' || path[0] == '/') return false;
    return (size_t)snprintf(out, out_size, "%s.part/%s", cached ? WGF_CORE_PRIV_FS_CACHE : "", path) < out_size;
}

bool wgf_core_priv_fs_replace(const char *from, const char *to)
{
    char source[1100], target[1100];
    wgf_core_priv_fs_resolve(from, source, sizeof(source));
    wgf_core_priv_fs_resolve(to, target, sizeof(target));
    if (source[0] == '\0' || target[0] == '\0') return false;
    return wgf_core_priv_os_rename_replace(source, target);
}

/* --- the public API: requests as tasks ------------------------------------ */

typedef enum op_t { OP_READ, OP_WRITE, OP_EXISTS, OP_REMOVE, OP_MKDIR, OP_RMDIR } op_t;

typedef struct task_t {
    op_t op;
    wgf_fs_task_status_t status;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
    unsigned char *data; /* a read's result, or a write's copy of its input until it runs */
    int size;
    int cache_read; /* web: the cache read in flight for a read, 0 = none */
    bool detached;  /* destroyed while pending: finish, then free */
} task_t;

static wgf_core_priv_handle_pool_t task_pool;
static task_t *tasks;

bool wgf_core_priv_fs_normalize_path(const char *path, char *out, size_t out_size)
{
    size_t marks[256]; /* where each kept segment starts, for ".." to go back to */
    size_t depth = 0, len = 0, base = 0;
    const char *p = path;

    if (path == NULL || out == NULL || out_size == 0) return false;
    out[0] = '\0';
    if (strncmp(path, WGF_CORE_PRIV_FS_USER, sizeof(WGF_CORE_PRIV_FS_USER) - 1) == 0) {
        /* the program's own files: the prefix kept, the rest a path under their root */
        base = sizeof(WGF_CORE_PRIV_FS_USER) - 1;
        if (base >= out_size) return false;
        memcpy(out, path, base);
        out[base] = '\0';
        path += base;
        len = base;
    }
    if (path[0] == '/' || path[0] == '\\' || strchr(path, ':') != NULL) return false;
    for (p = path; *p != '\0'; p++) {
        if ((unsigned char)*p < 0x20) return false;
    }
    p = path;
    while (*p != '\0') {
        const char *start;
        size_t n;
        while (*p == '/' || *p == '\\') p++;
        if (*p == '\0') break;
        start = p;
        while (*p != '\0' && *p != '/' && *p != '\\') p++;
        n = (size_t)(p - start);
        if (n == 1 && start[0] == '.') continue;
        if (n == 2 && start[0] == '.' && start[1] == '.') {
            if (depth == 0) return false; /* above the root, or the user: root */
            len = marks[--depth];
            out[len] = '\0';
            continue;
        }
        if (depth == sizeof(marks) / sizeof(marks[0])) return false;
        marks[depth++] = len;
        if (len + (len > base ? 1 : 0) + n >= out_size) return false;
        if (len > base) out[len++] = '/';
        memcpy(out + len, start, n);
        len += n;
        out[len] = '\0';
    }
    return len > base; /* else nothing is left to name a file */
}

/* The task `task` names, or NULL for anything else, a detached task included:
 * to its caller it is already gone. */
static task_t *task_of(wgf_handle_t task)
{
    uint16_t index;
    task_t *task_ptr;
    if (!fs_running || !wgf_core_priv_handle_pool_resolve(&task_pool, task, &index)) return NULL;
    task_ptr = &tasks[index];
    return task_ptr->detached ? NULL : task_ptr;
}

static void free_task(wgf_handle_t task, task_t *task_ptr)
{
    free(task_ptr->data);
    task_ptr->data = NULL;
    wgf_core_priv_handle_pool_free(&task_pool, task);
}

static wgf_handle_t new_task(op_t op, const char *path)
{
    wgf_handle_t task;
    uint16_t index;
    task_t *task_ptr;
    char normalized[WGF_CORE_PRIV_FS_PATH_MAX];
    if (!fs_running || !wgf_core_priv_fs_normalize_path(path, normalized, sizeof(normalized))) return 0;
    if (strncmp(normalized, WGF_CORE_PRIV_FS_USER, sizeof(WGF_CORE_PRIV_FS_USER) - 1) == 0 && user_root()[0] == '\0') {
        wgf_log_warn("wgf_fs: %s refused: this machine has no user data directory to keep the program's own "
                     "files in", normalized);
        return 0;
    }
    task = wgf_core_priv_handle_pool_alloc(&task_pool);
    if (task == 0 || !wgf_core_priv_handle_pool_resolve(&task_pool, task, &index)) return 0;
    task_ptr = &tasks[index];
    memset(task_ptr, 0, sizeof(*task_ptr));
    task_ptr->op = op;
    task_ptr->status = WGF_FS_TASK_STATUS_PENDING;
    snprintf(task_ptr->path, sizeof(task_ptr->path), "%s", normalized);
    return task;
}

/* One step of a pending task. Natively every task finishes in its first step;
 * on the web a read of a cached file waits for the cache. */
static void run(task_t *task_ptr)
{
    const char *path = task_ptr->path;
    switch (task_ptr->op) {
        case OP_READ:
            if (task_ptr->cache_read != 0) {
                const int state = wgf_core_priv_fs_cache_read_poll(task_ptr->cache_read);
                if (state == 0) return; /* still reading */
                task_ptr->cache_read = 0;
                if (state < 0) {
                    task_ptr->status = WGF_FS_TASK_STATUS_FAILED;
                    return;
                }
            } else if (!wgf_core_priv_fs_exists(path)) {
                if (wgf_core_priv_fs_is_cached(path)) {
                    task_ptr->cache_read = wgf_core_priv_fs_cache_read_begin(path);
                    if (task_ptr->cache_read != 0) return;
                }
                task_ptr->status = WGF_FS_TASK_STATUS_NOT_FOUND;
                return;
            }
            task_ptr->status = wgf_core_priv_fs_read(path, &task_ptr->data, &task_ptr->size)
                                   ? WGF_FS_TASK_STATUS_DONE
                                   : WGF_FS_TASK_STATUS_FAILED;
            return;
        case OP_WRITE:
            task_ptr->status = wgf_core_priv_fs_write(path, task_ptr->data, task_ptr->size)
                                   ? WGF_FS_TASK_STATUS_DONE
                                   : WGF_FS_TASK_STATUS_FAILED;
            free(task_ptr->data); /* the copy of the input has served */
            task_ptr->data = NULL;
            task_ptr->size = 0;
            return;
        case OP_EXISTS:
            task_ptr->status = (wgf_core_priv_fs_exists(path) || wgf_core_priv_fs_is_cached(path))
                                   ? WGF_FS_TASK_STATUS_DONE
                                   : WGF_FS_TASK_STATUS_NOT_FOUND;
            return;
        case OP_REMOVE:
            task_ptr->status = wgf_core_priv_fs_remove(path) ? WGF_FS_TASK_STATUS_DONE
                                                            : WGF_FS_TASK_STATUS_NOT_FOUND;
            return;
        case OP_MKDIR:
            task_ptr->status = wgf_core_priv_fs_mkdir(path) ? WGF_FS_TASK_STATUS_DONE
                                                           : WGF_FS_TASK_STATUS_FAILED;
            return;
        case OP_RMDIR: {
            char full[1100];
            wgf_core_priv_fs_resolve(path, full, sizeof(full));
            if (!wgf_core_priv_os_is_real_dir(full) && !wgf_core_priv_fs_platform_has_stored_dir(full)) {
                task_ptr->status = WGF_FS_TASK_STATUS_NOT_FOUND;
                return;
            }
            task_ptr->status = wgf_core_priv_fs_rmdir(path) ? WGF_FS_TASK_STATUS_DONE
                                                           : WGF_FS_TASK_STATUS_FAILED;
            return;
        }
    }
}

void wgf_core_priv_fs_update(void)
{
    uint16_t i;
    if (!fs_running || !wgf_core_priv_fs_is_ready()) return; /* every request waits for the store */
    for (i = 1; i < task_pool.capacity; i++) {
        task_t *task_ptr;
        const wgf_handle_t task = wgf_core_priv_handle_pool_handle_from_index(&task_pool, i);
        if (task == 0) continue;
        task_ptr = &tasks[i];
        if (task_ptr->status != WGF_FS_TASK_STATUS_PENDING) continue;
        run(task_ptr);
        if (task_ptr->status != WGF_FS_TASK_STATUS_PENDING && task_ptr->detached) free_task(task, task_ptr);
    }
}

void wgf_core_priv_fs_init(const char *root_dir)
{
    if (fs_running) return;
    set_trimmed(fs_root, sizeof(fs_root), root_dir != NULL ? root_dir : wgf_core_priv_fs_platform_default_root());
    wgf_core_priv_fs_platform_init(fs_root);
    if (!wgf_core_priv_handle_pool_init(&task_pool, WGF_CORE_PRIV_HANDLE_KIND_FS_TASK, (void **)&tasks, sizeof(task_t), 8, 4096)) {
        wgf_log_error("wgf_core_fs: no task pool; requests will be refused");
        return;
    }
    fs_running = true;
}

void wgf_core_priv_fs_deinit(void)
{
    uint16_t i;
    if (!fs_running) return;
    for (i = 1; i < task_pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&task_pool, i) != 0) free(tasks[i].data);
    }
    wgf_core_priv_handle_pool_destroy(&task_pool);
    fs_root[0] = '\0';
    fs_cache_root[0] = '\0';
    fs_user_root[0] = '\0';
    fs_running = false;
}

bool wgf_fs_set_root(const char *root)
{
    uint16_t i;
    if (!fs_running) return false;
    wgf_core_priv_fs_set_root(root);
    /* a cache read in flight was against the old store: it starts over */
    for (i = 1; i < task_pool.capacity; i++) {
        if (wgf_core_priv_handle_pool_handle_from_index(&task_pool, i) != 0) tasks[i].cache_read = 0;
    }
    return true;
}

const char *wgf_fs_get_root(void)
{
    return fs_root;
}

wgf_fs_task_t wgf_fs_read(const char *path)
{
    return new_task(OP_READ, path);
}

wgf_fs_task_t wgf_fs_write(const char *path, const unsigned char *data, int size)
{
    wgf_handle_t task;
    task_t *task_ptr;
    if (size < 0 || (data == NULL && size > 0)) return 0;
    task = new_task(OP_WRITE, path);
    if (task == 0) return 0;
    task_ptr = task_of(task);
    if (size > 0) {
        task_ptr->data = (unsigned char *)malloc((size_t)size);
        if (task_ptr->data == NULL) {
            free_task(task, task_ptr);
            return 0;
        }
        memcpy(task_ptr->data, data, (size_t)size);
        task_ptr->size = size;
    }
    return task;
}

wgf_fs_task_t wgf_fs_exists(const char *path)
{
    return new_task(OP_EXISTS, path);
}

wgf_fs_task_t wgf_fs_remove(const char *path)
{
    return new_task(OP_REMOVE, path);
}

wgf_fs_task_t wgf_fs_mkdir(const char *path)
{
    return new_task(OP_MKDIR, path);
}

wgf_fs_task_t wgf_fs_rmdir(const char *path)
{
    return new_task(OP_RMDIR, path);
}

wgf_fs_task_status_t wgf_fs_task_get_status(wgf_fs_task_t task)
{
    const task_t *task_ptr = task_of(task);
    return task_ptr != NULL ? task_ptr->status : WGF_FS_TASK_STATUS_NONE;
}

const char *wgf_fs_task_get_path(wgf_fs_task_t task)
{
    const task_t *task_ptr = task_of(task);
    return task_ptr != NULL ? task_ptr->path : "";
}

const unsigned char *wgf_fs_task_get_data(wgf_fs_task_t task)
{
    const task_t *task_ptr = task_of(task);
    if (task_ptr == NULL || task_ptr->op != OP_READ || task_ptr->status != WGF_FS_TASK_STATUS_DONE) return NULL;
    return task_ptr->data;
}

int wgf_fs_task_get_size(wgf_fs_task_t task)
{
    return wgf_fs_task_get_data(task) != NULL ? task_of(task)->size : 0;
}

const char *wgf_fs_task_get_text(wgf_fs_task_t task)
{
    const unsigned char *data = wgf_fs_task_get_data(task);
    return data != NULL ? (const char *)data : "";
}

bool wgf_fs_task_destroy(wgf_fs_task_t task)
{
    task_t *task_ptr = task_of(task);
    if (task_ptr == NULL) return false;
    if (task_ptr->status == WGF_FS_TASK_STATUS_PENDING) {
        task_ptr->detached = true; /* it finishes in update, then goes */
        return true;
    }
    free_task(task, task_ptr);
    return true;
}
