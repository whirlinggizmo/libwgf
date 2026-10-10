#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wgf_core_priv.h"
#include "wgf.h"
#include "wgf_fs.h"
#include "wgf_handle.h"

/* The root is a jail for removal too: a symbolic link under it to a directory
 * outside is followed by read, but remove and rmdir take the link, never what it
 * points to. POSIX only (CMake builds it there): it makes links. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static wgf_fs_task_status_t settle(wgf_handle_t task)
{
    wgf_fs_task_status_t status;
    int i;
    for (i = 0; i < 100 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    status = wgf_fs_task_get_status(task);
    wgf_fs_task_destroy(task);
    return status;
}

static int outside_exists(void)
{
    struct stat st;
    return stat("links_test_outside/keep.txt", &st) == 0;
}

int main(void)
{
    FILE *f;
    wgf_handle_t t;

    /* outside the root: a directory with a file that must survive */
    mkdir("links_test_outside", 0755);
    f = fopen("links_test_outside/keep.txt", "w");
    if (f == NULL) {
        printf("FAIL: couldn't make the outside file\n");
        return 1;
    }
    fputs("keep", f);
    fclose(f);

    wgf_core_priv_init();
    wgf_fs_set_root("links_test_root");
    expect(settle(wgf_fs_mkdir("inner")) == WGF_FS_TASK_STATUS_DONE, "mkdir inner");
    unlink("links_test_root/out");
    unlink("links_test_root/inner/out");
    expect(symlink("../links_test_outside", "links_test_root/out") == 0, "link at the top");
    expect(symlink("../../links_test_outside", "links_test_root/inner/out") == 0, "link inside a directory");

    /* read follows a link the user placed */
    t = wgf_fs_read("out/keep.txt");
    wgf_core_priv_update();
    expect(wgf_fs_task_get_status(t) == WGF_FS_TASK_STATUS_DONE && strcmp(wgf_fs_task_get_text(t), "keep") == 0,
           "read follows the link");
    wgf_fs_task_destroy(t);

    /* rmdir of the link itself: not a directory */
    expect(settle(wgf_fs_rmdir("out")) == WGF_FS_TASK_STATUS_NOT_FOUND, "rmdir of a link is NOT_FOUND");
    expect(outside_exists(), "the outside file survives rmdir of the link");

    /* rmdir of a directory holding a link: the link goes, its target stays */
    expect(settle(wgf_fs_rmdir("inner")) == WGF_FS_TASK_STATUS_DONE, "rmdir of a directory holding a link");
    expect(outside_exists(), "the outside file survives rmdir of the directory");

    /* remove of the link: the link goes, its target stays */
    expect(settle(wgf_fs_remove("out")) == WGF_FS_TASK_STATUS_DONE, "remove the link");
    expect(outside_exists(), "the outside file survives remove of the link");

    wgf_core_priv_shutdown();
    remove("links_test_outside/keep.txt");
    rmdir("links_test_outside");
    rmdir("links_test_root");
    return failures == 0 ? 0 : 1;
}
