#include <stdio.h>
#include <string.h>

#include "wgf_asset.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"

/* The asset host natively, with the working directory elsewhere (Rob, 2026-10-04): the
 * default host is the program's own directory, where its executable is, and a relative
 * host resolves against it, never against the working directory; "" puts the default
 * back. Native only (CMake builds it there). Files land beside the test's executable,
 * under host_test_assets/, and go when it ends. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* Ensure `path` and tick until it finishes: whether it is DONE. */
static bool ensured(const char *path)
{
    const wgf_handle_t task = wgf_asset_ensure(path, NULL, 0);
    wgf_asset_task_status_t status;
    for (int i = 0; i < 100 && wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_PENDING; i++) {
        wgf_core_priv_update();
    }
    status = wgf_asset_task_get_status(task);
    wgf_asset_task_destroy(task);
    return status == WGF_ASSET_TASK_STATUS_DONE;
}

int main(void)
{
    char exe_dir[1024], elsewhere[1100];
    if (!wgf_core_priv_os_executable_dir(exe_dir, sizeof(exe_dir))) {
        printf("wgf_asset_host_test: SKIPPING (the executable's directory can't be told here)\n");
        return 77;
    }
    wgf_core_priv_init(); /* fs's root: the executable's directory */
    expect(strcmp(wgf_core_priv_fs_root(), exe_dir) == 0, "fs's default root is the executable's directory");
    expect(wgf_core_priv_fs_write("host_test_assets/sub/note.txt", (const unsigned char *)"note", 4), "a file under a host");
    expect(wgf_core_priv_fs_write("host_test_assets/beside.txt", (const unsigned char *)"beside", 6), "a file beside it");
    expect(wgf_core_priv_fs_write("beside_the_program.txt", (const unsigned char *)"here", 4), "a file beside the executable");

    /* the working directory somewhere else: nothing below may depend on it */
    snprintf(elsewhere, sizeof(elsewhere), "%s/host_test_assets/sub", exe_dir);
    expect(wgf_core_priv_os_chdir(elsewhere), "the working directory moved");

    expect(strcmp(wgf_asset_get_host(), exe_dir) == 0, "the default host is the executable's directory");
    expect(ensured("beside_the_program.txt"), "a file beside the executable is found with the default host");
    expect(!ensured("note.txt"), "a file in the working directory isn't");

    expect(wgf_asset_set_host("host_test_assets"), "a relative host set");
    expect(ensured("sub/note.txt"), "a relative host resolves against the executable's directory");
    expect(ensured("beside.txt"), "and finds its files");
    expect(!ensured("beside_the_program.txt"), "not the executable's own");

    expect(wgf_asset_set_host("host_test_assets/../host_test_assets/sub"), "a relative host with dots set");
    expect(ensured("note.txt"), "resolved as a path under the executable's directory");

    expect(wgf_asset_set_host(""), "\"\" puts the default back");
    expect(strcmp(wgf_asset_get_host(), exe_dir) == 0, "the default host again");
    expect(ensured("beside_the_program.txt"), "and the executable's files again");

    wgf_core_priv_os_chdir(exe_dir); /* out of the directory first: Windows won't remove a process's working directory */
    expect(wgf_core_priv_fs_rmdir("host_test_assets") && wgf_core_priv_fs_remove("beside_the_program.txt"), "tidy");
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
