#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* setenv */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_identity.h"

/* The program's own files, "user:" paths: written and read back under the user's data
 * directory by the program's identity natively (pointed here at a folder in the
 * working dir), and in the browser's store on the web. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static wgf_fs_task_status_t settle(wgf_fs_task_t task)
{
    int i;
    for (i = 0; i < 100 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    return wgf_fs_task_get_status(task);
}

int main(void)
{
    wgf_fs_task_t t;
#if !defined(__EMSCRIPTEN__)
    char data[1024], expected[1200];
    char *cwd = wgf_core_priv_os_getcwd();
    snprintf(data, sizeof(data), "%s/fs_user_test_data", cwd != NULL ? cwd : ".");
    free(cwd);
#if defined(_WIN32)
    _putenv_s("APPDATA", data);
#else
    setenv("XDG_DATA_HOME", data, 1);
#endif
#endif
    expect(wgf_identity_set_company("WgfTest") && wgf_identity_set_product("fs_user_test"), "the identity");
    wgf_core_priv_init();
    expect(wgf_fs_set_root("fs_user_test_root"), "set_root");

    expect(wgf_fs_write_text("user:../escape.txt", "no") == 0, "a user: path above its root is refused");
    expect(wgf_fs_write_text("user:", "no") == 0, "a user: path naming nothing is refused");

    t = wgf_fs_write_text("user:saves/high.txt", "12345");
    expect(t != 0 && strcmp(wgf_fs_task_get_path(t), "user:saves/high.txt") == 0, "a user: write, its path kept");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE, "the write is DONE");
    wgf_fs_task_destroy(t);

    t = wgf_fs_read("user:saves/high.txt");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE && strcmp(wgf_fs_task_get_text(t), "12345") == 0,
           "the file reads back");
    wgf_fs_task_destroy(t);

    t = wgf_fs_read("saves/high.txt");
    expect(settle(t) == WGF_FS_TASK_STATUS_NOT_FOUND, "it isn't under the root");
    wgf_fs_task_destroy(t);

#if !defined(__EMSCRIPTEN__)
    {
        FILE *f;
        snprintf(expected, sizeof(expected), "%s/WgfTest/fs_user_test/saves/high.txt", data);
        f = fopen(expected, "rb");
        expect(f != NULL, "natively, under <data>/<company>/<product>");
        if (f != NULL) fclose(f);
    }
#endif
    t = wgf_fs_remove("user:saves/high.txt");
    expect(settle(t) == WGF_FS_TASK_STATUS_DONE, "removed");
    wgf_fs_task_destroy(t);

    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
