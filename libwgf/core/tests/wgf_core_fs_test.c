#include <stdio.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf.h"
#include "wgf_fs.h"
#include "wgf_handle.h"

/* The public fs API, through its sugar: every request is a task that finishes
 * only during wgf_update. Files land under fs_test_root/ in the working dir. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* Update until the task settles, as a frame loop would. */
static wgf_fs_task_status_t settle(wgf_handle_t task)
{
    int i;
    for (i = 0; i < 100 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    return wgf_fs_task_get_status(task);
}

int main(void)
{
    static const unsigned char binary[4] = {0, 1, 2, 255};
    wgf_handle_t t, w;

    expect(wgf_fs_read("a.txt") == 0, "a request before init is refused");
    expect(!wgf_fs_set_root("x"), "set_root before init is refused");

    wgf_core_priv_init();
    expect(wgf_fs_set_root("fs_test_root/"), "set_root");
    expect(strcmp(wgf_fs_get_root(), "fs_test_root") == 0, "get_root reads it back, trimmed");

    /* refusals, all without a task */
    expect(wgf_fs_read("") == 0, "an empty path is refused");
    expect(wgf_fs_read("/etc/passwd") == 0, "an absolute path is refused");
    expect(wgf_fs_read("../x") == 0 && wgf_fs_read("a\\..\\..\\x") == 0 && wgf_fs_read("a/..") == 0,
           "paths climbing above the root, or naming nothing, are refused");
    expect(wgf_fs_read("C:x") == 0 && wgf_fs_read("cache:x") == 0 && wgf_fs_read("a\nb") == 0,
           "paths with : or control characters are refused");
    t = wgf_fs_read(".\\textures//..\\models/./box.glb");
    expect(t != 0 && strcmp(wgf_fs_task_get_path(t), "models/box.glb") == 0, "a task's path is normalized");
    wgf_fs_task_destroy(t);
    expect(wgf_fs_write("x", NULL, 3) == 0, "NULL data with a size is refused");
    expect(wgf_fs_task_get_status(0) == WGF_FS_TASK_STATUS_NONE, "0 is not a task");
    expect(strcmp(wgf_fs_task_get_path(12345), "") == 0 && wgf_fs_task_get_size(12345) == 0 &&
               wgf_fs_task_get_data(12345) == NULL,
           "getters on a handle that isn't a task");
    expect(!wgf_fs_task_destroy(12345), "destroying a handle that isn't a task");

    /* a missing file */
    t = wgf_fs_read("missing.txt");
    expect(t != 0, "read returns a task");
    expect(strcmp(wgf_handle_get_kind_name(t), "core.fs_task") == 0, "its kind");
    expect(wgf_fs_task_get_status(t) == WGF_FS_TASK_STATUS_PENDING, "pending until update, even natively");
    expect(settle(t) == WGF_FS_TASK_STATUS_NOT_FOUND, "a missing file is NOT_FOUND");
    expect(strcmp(wgf_fs_task_get_path(t), "missing.txt") == 0, "the task keeps its path");
    expect(wgf_fs_task_get_data(t) == NULL && strcmp(wgf_fs_task_get_text(t), "") == 0, "no data for NOT_FOUND");
    expect(wgf_fs_task_destroy(t), "destroy");
    expect(!wgf_fs_task_destroy(t), "a destroyed task is stale");
    expect(wgf_fs_task_get_status(t) == WGF_FS_TASK_STATUS_NONE, "a stale task has no status");

    /* text, in a directory that doesn't exist yet */
    w = wgf_fs_write_text("saves/slot1.json", "{\"level\": 3}");
    expect(w != 0 && wgf_fs_task_get_status(w) == WGF_FS_TASK_STATUS_PENDING, "write is pending");
    expect(settle(w) == WGF_FS_TASK_STATUS_DONE, "write done");
    wgf_fs_task_destroy(w);
    t = wgf_fs_read("saves/slot1.json");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE, "read done");
    expect(wgf_fs_task_get_size(t) == 12 && strcmp(wgf_fs_task_get_text(t), "{\"level\": 3}") == 0, "text read back");
    expect(wgf_fs_task_get_data(t)[12] == 0, "data is NUL-terminated");
    wgf_fs_task_destroy(t);

    /* bytes, with a zero inside */
    w = wgf_fs_write("saves/blob.bin", binary, 4);
    expect(settle(w) == WGF_FS_TASK_STATUS_DONE, "binary write done");
    wgf_fs_task_destroy(w);
    t = wgf_fs_read("saves/blob.bin");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE, "binary read done");
    expect(wgf_fs_task_get_size(t) == 4 && memcmp(wgf_fs_task_get_data(t), binary, 4) == 0, "bytes read back");
    wgf_fs_task_destroy(t);

    /* an empty file */
    w = wgf_fs_write("saves/empty", NULL, 0);
    expect(settle(w) == WGF_FS_TASK_STATUS_DONE, "empty write done");
    wgf_fs_task_destroy(w);
    t = wgf_fs_read("saves/empty");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE && wgf_fs_task_get_size(t) == 0 &&
               strcmp(wgf_fs_task_get_text(t), "") == 0,
           "empty read back");
    wgf_fs_task_destroy(t);

    /* exists and remove */
    t = wgf_fs_exists("saves/slot1.json");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE, "exists: yes");
    wgf_fs_task_destroy(t);
    t = wgf_fs_exists("saves/slot2.json");
    expect(settle(t) == WGF_FS_TASK_STATUS_NOT_FOUND, "exists: no");
    wgf_fs_task_destroy(t);
    t = wgf_fs_remove("saves/slot1.json");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE, "remove");
    wgf_fs_task_destroy(t);
    t = wgf_fs_remove("saves/slot1.json");
    expect(settle(t) == WGF_FS_TASK_STATUS_NOT_FOUND, "remove again: NOT_FOUND");
    wgf_fs_task_destroy(t);
    t = wgf_fs_exists("saves/slot1.json");
    expect(settle(t) == WGF_FS_TASK_STATUS_NOT_FOUND, "gone");
    wgf_fs_task_destroy(t);

    /* a write destroyed while pending still happens */
    w = wgf_fs_write_text("saves/fire_and_forget.txt", "kept");
    expect(wgf_fs_task_destroy(w), "destroy a pending write");
    expect(wgf_fs_task_get_status(w) == WGF_FS_TASK_STATUS_NONE, "it is gone to the caller");
    wgf_core_priv_update();
    t = wgf_fs_read("saves/fire_and_forget.txt");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE && strcmp(wgf_fs_task_get_text(t), "kept") == 0,
           "the write still landed");
    wgf_fs_task_destroy(t);

    /* several tasks in flight at once, each with its own result */
    {
        wgf_handle_t a = wgf_fs_read("saves/blob.bin");
        wgf_handle_t b = wgf_fs_read("saves/nope");
        wgf_handle_t c = wgf_fs_write_text("saves/c.txt", "c");
        wgf_core_priv_update();
        expect(wgf_fs_task_get_status(a) == WGF_FS_TASK_STATUS_DONE && wgf_fs_task_get_size(a) == 4, "a: read");
        expect(wgf_fs_task_get_status(b) == WGF_FS_TASK_STATUS_NOT_FOUND, "b: not found");
        expect(wgf_fs_task_get_status(c) == WGF_FS_TASK_STATUS_DONE, "c: written");
        wgf_fs_task_destroy(a);
        wgf_fs_task_destroy(b);
        wgf_fs_task_destroy(c);
    }

    /* directories */
    t = wgf_fs_mkdir("dirs/a/b");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE, "mkdir makes parents");
    wgf_fs_task_destroy(t);
    t = wgf_fs_mkdir("dirs/a/b");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE, "mkdir of an existing directory is DONE");
    wgf_fs_task_destroy(t);
    settle(wgf_fs_write_text("dirs/a/b/deep.txt", "deep"));
    t = wgf_fs_rmdir("dirs");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE, "rmdir removes a tree");
    wgf_fs_task_destroy(t);
    t = wgf_fs_exists("dirs/a/b/deep.txt");
    expect(settle(t) == WGF_FS_TASK_STATUS_NOT_FOUND, "and what was in it");
    wgf_fs_task_destroy(t);
    t = wgf_fs_rmdir("dirs");
    expect(settle(t) == WGF_FS_TASK_STATUS_NOT_FOUND, "rmdir again: NOT_FOUND");
    wgf_fs_task_destroy(t);

    /* tidy what this run made, and leave a pending task for shutdown to drop */
    t = wgf_fs_rmdir("saves");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE, "rmdir saves");
    wgf_fs_task_destroy(t);
    t = wgf_fs_read("saves/blob.bin");
    wgf_core_priv_shutdown();
    expect(wgf_fs_task_get_status(t) == WGF_FS_TASK_STATUS_NONE, "shutdown drops tasks");
    expect(wgf_fs_read("a.txt") == 0, "a request after shutdown is refused");

    return failures == 0 ? 0 : 1;
}
