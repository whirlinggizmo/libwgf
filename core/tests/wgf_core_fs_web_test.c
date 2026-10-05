#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten.h>

#include "wgf_core_priv.h"
#include "wgf.h"
#include "wgf_core_fs_priv.h"
#include "wgf_fs.h"

/* The web store across visits, in a real browser (tools/run_in_browser.py loads this page
 * three times in one browser context, passing the visit number). IndexedDB answers
 * only between frames, so the test runs as a frame loop: each frame updates core
 * and takes one step when the step before it is finished.
 *
 *   1. writes files, one with metadata, and a directory; waits until stored
 *   2. a fresh page, whose memory holds none of them: the store's list says they
 *      are there, a read brings one in, the metadata came back, and rmdir removes
 *      a directory known only to the store
 *   3. the removed directory stayed removed; clears the store */

static int visit;
static int step;
static int failures;
static wgf_handle_t task;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: visit %d: %s\n", visit, what);
        failures++;
    }
}

static void finish(void)
{
    wgf_core_priv_shutdown();
    emscripten_force_exit(failures == 0 ? 0 : 1); /* ends the frame loop too */
}

/* True once `task` is no longer pending; then it is checked and destroyed. */
static int settled(wgf_fs_task_status_t expected, const char *what)
{
    const wgf_fs_task_status_t status = wgf_fs_task_get_status(task);
    if (status == WGF_FS_TASK_STATUS_PENDING) return 0;
    expect(status == expected, what);
    return 1;
}

static void visit_1(void)
{
    wgf_core_priv_fs_meta_t meta;
    switch (step) {
        case 0:
            task = wgf_fs_write_text("saves/keep.txt", "kept between visits");
            step++;
            break;
        case 1:
            if (!settled(WGF_FS_TASK_STATUS_DONE, "write keep.txt")) return;
            wgf_fs_task_destroy(task);
            memset(&meta, 0, sizeof(meta));
            snprintf(meta.etag, sizeof(meta.etag), "\"v7\"");
            snprintf(meta.hash, sizeof(meta.hash), "sha256:cafe");
            meta.fresh_until = 42.5;
            expect(wgf_core_priv_fs_write_meta("saves/meta.bin", (const unsigned char *)"m", 1, &meta),
                   "write with meta");
            task = wgf_fs_write_text("doomed/inner/a.txt", "a");
            step++;
            break;
        case 2:
            if (!settled(WGF_FS_TASK_STATUS_DONE, "write doomed/inner/a.txt")) return;
            wgf_fs_task_destroy(task);
            task = wgf_fs_write_text("user:high.txt", "999");
            step++;
            break;
        case 3:
            if (!settled(WGF_FS_TASK_STATUS_DONE, "write user:high.txt")) return;
            wgf_fs_task_destroy(task);
            step++;
            break;
        case 4:
            if (!wgf_core_priv_fs_is_settled()) return; /* every write committed to the store */
            finish();
            break;
    }
}

static void visit_2(void)
{
    wgf_core_priv_fs_meta_t meta;
    switch (step) {
        case 0:
            if (!wgf_core_priv_fs_is_ready()) return; /* the store's list read */
            expect(!wgf_core_priv_fs_exists("saves/keep.txt"), "a fresh page has no copy in memory");
            expect(wgf_core_priv_fs_is_cached("saves/keep.txt"), "the store lists keep.txt");
            expect(wgf_core_priv_fs_is_cached("doomed/inner/a.txt"), "the store lists doomed/inner/a.txt");
            expect(wgf_core_priv_fs_meta_get("saves/meta.bin", &meta) && strcmp(meta.etag, "\"v7\"") == 0 &&
                       strcmp(meta.hash, "sha256:cafe") == 0 && meta.fresh_until == 42.5,
                   "metadata came back with the list");
            task = wgf_fs_exists("saves/keep.txt");
            step++;
            break;
        case 1:
            if (!settled(WGF_FS_TASK_STATUS_DONE, "exists answers from the store")) return;
            wgf_fs_task_destroy(task);
            task = wgf_fs_read("saves/keep.txt");
            step++;
            break;
        case 2:
            if (!settled(WGF_FS_TASK_STATUS_DONE, "read brings a stored file in")) return;
            expect(strcmp(wgf_fs_task_get_text(task), "kept between visits") == 0, "its bytes");
            wgf_fs_task_destroy(task);
            expect(wgf_core_priv_fs_exists("saves/keep.txt"), "now in memory too");
            task = wgf_fs_rmdir("doomed");
            step++;
            break;
        case 3:
            if (!settled(WGF_FS_TASK_STATUS_DONE, "rmdir of a directory only the store knows")) return;
            wgf_fs_task_destroy(task);
            expect(!wgf_core_priv_fs_is_cached("doomed/inner/a.txt"), "its files left the list");
            step++;
            break;
        case 4:
            task = wgf_fs_read("user:high.txt");
            step++;
            break;
        case 5:
            if (!settled(WGF_FS_TASK_STATUS_DONE, "a user: file kept between visits")) return;
            expect(strcmp(wgf_fs_task_get_text(task), "999") == 0, "its bytes");
            wgf_fs_task_destroy(task);
            step++;
            break;
        case 6:
            if (!wgf_core_priv_fs_is_settled()) return;
            finish();
            break;
    }
}

static void visit_3(void)
{
    switch (step) {
        case 0:
            if (!wgf_core_priv_fs_is_ready()) return;
            expect(!wgf_core_priv_fs_is_cached("doomed/inner/a.txt"), "the removed directory stayed removed");
            expect(wgf_core_priv_fs_is_cached("saves/keep.txt"), "keep.txt is still stored");
            wgf_core_priv_fs_clear(); /* leave nothing behind */
            expect(!wgf_core_priv_fs_is_cached("saves/keep.txt"), "clear forgets the store");
            step++;
            break;
        case 1:
            if (!wgf_core_priv_fs_is_settled()) return;
            finish();
            break;
    }
}

static void frame(void)
{
    wgf_core_priv_update();
    if (visit == 1) visit_1();
    else if (visit == 2) visit_2();
    else visit_3();
}

int main(int argc, char **argv)
{
    visit = argc > 1 ? atoi(argv[1]) : 0;
    if (visit < 1 || visit > 3) {
        printf("FAIL: run by tools/run_in_browser.py --visits 3, which passes the visit number\n");
        return 1;
    }
    wgf_core_priv_init();
    emscripten_set_main_loop(frame, 0, 1); /* doesn't return: finish exits */
    return 0;
}
