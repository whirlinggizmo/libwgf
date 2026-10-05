#ifndef WGF_FS_H
#define WGF_FS_H

#include <stdbool.h>
#include <string.h>

#include "wgf.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Local file storage, the same on every platform. Native: files under the root, the
 * program's own directory by default (where its executable is, wherever it was
 * started from: a double-clicked program finds its files), never the working
 * directory. Web: files kept in the browser between visits.
 * Paths are relative to the root. "\\" is read as "/", "." and empty parts are
 * dropped, and ".." takes back the part before it, so "textures\\..\\models/box.glb"
 * is "models/box.glb". A path that is absolute, has a ":" or a control character,
 * climbs above the root, or names nothing is refused, so a path can only name
 * something under the root. A task's path is the normalized one.
 *
 * A path starting with "user:" names the program's own file that must last, such as a
 * save or its settings ("user:highscore.txt"), apart from the files it ships with:
 * natively under the user's data directory, by the program's identity
 * (<data>/<company>/<product>, wgf_identity.h: ~/.local/share on Linux, %APPDATA% on
 * Windows); on the web in the browser's store, beside the root's other files. The rest
 * of such a path keeps the rule above, under that directory. A request for one is
 * refused where there is no user data directory to be found (no HOME), with a warning. A symbolic link you put under the
 * root is followed by read and write; remove and rmdir remove the link itself,
 * never what it points to. Text is UTF-8.
 *
 * Every request returns a task (kind "core.fs_task"): poll its status, read its
 * result, then destroy it. A task's status changes only during the runtime's
 * update at the start of a frame, never inside the call that made the request. A request made before storage is
 * ready waits for it. Destroying a task that is still pending lets it finish and
 * discards its result, so a write is never lost. */

/* A request's task: a handle of kind "core.fs_task". */
typedef wgf_handle_t wgf_fs_task_t;

typedef enum wgf_fs_task_status_t {
    WGF_FS_TASK_STATUS_NONE = 0,      /* not a task */
    WGF_FS_TASK_STATUS_PENDING = 1,
    WGF_FS_TASK_STATUS_DONE = 2,
    WGF_FS_TASK_STATUS_NOT_FOUND = 3, /* no such file: a read, exists, or remove of one */
    WGF_FS_TASK_STATUS_FAILED = 4     /* the file couldn't be read or written */
} wgf_fs_task_status_t;

/* The root paths resolve against. Native: a directory, absolute or relative to the
 * working dir; the default is the program's own directory (the executable's; the
 * working dir where that can't be told). Web: "/wgf" by default; another root is another browser store, and
 * requests wait while it opens. False when core isn't running. */
WGF_API bool wgf_fs_set_root(const char *root);
WGF_API const char *wgf_fs_get_root(void);

/* Requests. Each returns a task, or 0 when core isn't running, `path` breaks the
 * rule above or is too long, `data` is NULL with a size, or memory runs out. */
WGF_API wgf_fs_task_t wgf_fs_read(const char *path); /* DONE: the data; NOT_FOUND; FAILED */
/* Copies `data` before returning, and makes the parent directories. DONE once
 * written, so a read of the path gives the new bytes; on the web the copy kept
 * between visits is stored in the background after that. */
WGF_API wgf_fs_task_t wgf_fs_write(const char *path, const unsigned char *data, int size);
WGF_API wgf_fs_task_t wgf_fs_exists(const char *path); /* DONE: it exists; NOT_FOUND */
WGF_API wgf_fs_task_t wgf_fs_remove(const char *path); /* DONE; NOT_FOUND */
/* Make a directory and its parents. DONE when it exists after, so an existing
 * one is DONE; FAILED. */
WGF_API wgf_fs_task_t wgf_fs_mkdir(const char *path);
/* Remove a directory and everything in it. DONE; NOT_FOUND when there is no such
 * directory (a link to one isn't one: remove the link instead); FAILED when
 * something in it couldn't be removed. */
WGF_API wgf_fs_task_t wgf_fs_rmdir(const char *path);

static inline wgf_fs_task_t wgf_fs_write_text(const char *path, const char *text)
{
    return wgf_fs_write(path, (const unsigned char *)text, text != NULL ? (int)strlen(text) : 0);
}

/* Tasks. A getter answers NONE, "", NULL, or 0 for a handle that isn't a task. */
WGF_API wgf_fs_task_status_t wgf_fs_task_get_status(wgf_fs_task_t task);
WGF_API const char *wgf_fs_task_get_path(wgf_fs_task_t task);
/* A DONE read's bytes, NUL-terminated so they also read as text, valid until the
 * task is destroyed. Any other task: NULL, 0, and "". */
WGF_API const unsigned char *wgf_fs_task_get_data(wgf_fs_task_t task);
WGF_API int wgf_fs_task_get_size(wgf_fs_task_t task);
WGF_API const char *wgf_fs_task_get_text(wgf_fs_task_t task);
/* False for a handle that isn't a task. */
WGF_API bool wgf_fs_task_destroy(wgf_fs_task_t task);

#ifdef __cplusplus
}
#endif

#endif
