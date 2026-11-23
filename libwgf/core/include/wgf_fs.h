#ifndef WGF_FS_H
#define WGF_FS_H

#include <stdbool.h>
#include <string.h>

#include "wgf.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Local file storage that works the same on every platform.
 *
 * - Natively, files live under the root. By default the root is the program's own
 *   directory, where its executable is, wherever it was started from. So a double-clicked
 *   program finds its files. The root is never the working directory.
 * - On the web, files are kept in the browser between visits.
 *
 * Paths are relative to the root, and are normalized:
 * - "\\" is read as "/".
 * - "." and empty parts are dropped.
 * - ".." removes the part before it, so "textures\\..\\models/box.glb" is "models/box.glb".
 *
 * A path is refused if it is absolute, has a ":" or a control character, climbs above the
 * root, or names nothing. So a path can only name a file under the root. A task's path is
 * the normalized one.
 *
 * A path starting with "user:" names a file of the program's own that must last, such as
 * a save or its settings ("user:highscore.txt"). These files are kept apart from the files
 * the program ships with.
 * - Natively they go under the user's data directory, by the program's identity:
 *   <data>/<company>/<product> (see wgf_identity.h). <data> is ~/.local/share on Linux and
 *   %APPDATA% on Windows.
 * - On the web they go in the browser's store, beside the root's other files.
 * - The rest of such a path follows the rules above, under that directory.
 * - A "user:" request is refused, with a warning, where no user data directory can be
 *   found (no HOME).
 *
 * A symbolic link you put under the root is followed by read and write. remove and rmdir
 * remove the link itself, never what it points to. Text is UTF-8.
 *
 * Every request returns a task, a handle of kind "core.fs_task". Poll its status, read its
 * result, then destroy it.
 * - A task's status changes only during the runtime's update at the start of a frame,
 *   never inside the call that made the request.
 * - A request made before storage is ready waits for it.
 * - Destroying a task that is still pending lets it finish and discards its result, so a
 *   write is never lost. */

/* The task a request returns: a handle of kind "core.fs_task". */
typedef wgf_handle_t wgf_fs_task_t;

typedef enum wgf_fs_task_status_t {
    WGF_FS_TASK_STATUS_NONE = 0,      /* not a task */
    WGF_FS_TASK_STATUS_PENDING = 1,
    WGF_FS_TASK_STATUS_DONE = 2,
    WGF_FS_TASK_STATUS_NOT_FOUND = 3, /* no such file, for a read, exists, or remove */
    WGF_FS_TASK_STATUS_FAILED = 4     /* the file couldn't be read or written */
} wgf_fs_task_status_t;

/* Sets or gets the root that paths resolve against.
 *
 * - Natively the root is a directory, absolute or relative to the working directory. The
 *   default is the program's own directory, where its executable is. Where that can't be
 *   told, the default is the working directory.
 * - On the web the default is "/wgf". Another root is another browser store, and requests
 *   wait while it opens.
 *
 * The setter returns false if core isn't running. */
WGF_API bool wgf_fs_set_root(const char *root);
WGF_API const char *wgf_fs_get_root(void);

/* Requests. Each one returns a task.
 *
 * Returns 0 if core isn't running, if `path` breaks the rules above or is too long, if
 * `data` is NULL with a size, or if memory runs out. */
WGF_API wgf_fs_task_t wgf_fs_read(const char *path); /* DONE with the data, NOT_FOUND, or FAILED */
/* Writes `data` to `path`, making the parent directories.
 *
 * `data` is copied before the call returns. The task is DONE once the file is written, so
 * a read of the path then gives the new bytes. On the web, the copy kept between visits is
 * stored in the background after that. */
WGF_API wgf_fs_task_t wgf_fs_write(const char *path, const unsigned char *data, int size);
WGF_API wgf_fs_task_t wgf_fs_exists(const char *path); /* DONE if it exists, else NOT_FOUND */
WGF_API wgf_fs_task_t wgf_fs_remove(const char *path); /* DONE, or NOT_FOUND */
/* Makes a directory and its parents.
 *
 * The task is DONE if the directory exists afterwards, so an existing directory is DONE.
 * Otherwise it is FAILED. */
WGF_API wgf_fs_task_t wgf_fs_mkdir(const char *path);
/* Removes a directory and everything in it.
 *
 * - DONE once it is removed.
 * - NOT_FOUND if there is no such directory. A link to a directory isn't one: remove the
 *   link instead.
 * - FAILED if a file or directory in it couldn't be removed. */
WGF_API wgf_fs_task_t wgf_fs_rmdir(const char *path);
/* Lists what a directory holds.
 *
 * When DONE, the task's text holds the names, sorted, one a line. A directory's name has
 * a slash after it: "levels/\nsave.json\n". An empty directory gives "". On the web the
 * list also includes the files kept only in the browser's store.
 *
 * The task is NOT_FOUND if there is no such directory. */
WGF_API wgf_fs_task_t wgf_fs_list(const char *path);

static inline wgf_fs_task_t wgf_fs_write_text(const char *path, const char *text)
{
    return wgf_fs_write(path, (const unsigned char *)text, text != NULL ? (int)strlen(text) : 0);
}

/* Reads a task. A getter returns NONE, "", NULL, or 0 for a handle that isn't a task. */
WGF_API wgf_fs_task_status_t wgf_fs_task_get_status(wgf_fs_task_t task);
WGF_API const char *wgf_fs_task_get_path(wgf_fs_task_t task);
/* Returns a DONE read's bytes, or a list's text, and their size.
 *
 * The bytes end in a NUL, so they also read as text. They are valid until the task is
 * destroyed. Any other task gives NULL, 0, and "". */
WGF_API const unsigned char *wgf_fs_task_get_data(wgf_fs_task_t task);
WGF_API int wgf_fs_task_get_size(wgf_fs_task_t task);
WGF_API const char *wgf_fs_task_get_text(wgf_fs_task_t task);
/* Destroys a task. Returns false for a handle that isn't a task. */
WGF_API bool wgf_fs_task_destroy(wgf_fs_task_t task);

#ifdef __cplusplus
}
#endif

#endif
