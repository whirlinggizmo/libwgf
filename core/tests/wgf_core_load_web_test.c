#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten.h>

#include "wgf_core_priv.h"
#include "wgf.h"
#include "wgf_core_fs_priv.h"
#include "wgf_fs.h"
#include "wgf_core_load_priv.h"

/* The load pipeline on a returning visit, in a real browser (tools/run_in_browser.py
 * loads this page twice in one browser context). Visit 1 stores a file; visit 2,
 * whose memory starts empty, loads it, so the pipeline reads it from IndexedDB
 * before preparing it. */

static int visit;
static int step;
static int failures;
static wgf_handle_t task;
static int loaded;    /* 1 ready, 2 failed */
static char text[64];

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: visit %d: %s\n", visit, what);
        failures++;
    }
}

static void *prepare(const char *path)
{
    unsigned char *data;
    int size;
    char *copy;
    if (!wgf_core_priv_fs_read(path, &data, &size)) return NULL;
    copy = (char *)malloc((size_t)size + 1);
    memcpy(copy, data, (size_t)size + 1);
    wgf_core_priv_fs_read_free(data);
    return copy;
}

static wgf_core_priv_load_step_t finish(void *prepared, wgf_handle_t resource)
{
    (void)resource;
    snprintf(text, sizeof(text), "%s", (const char *)prepared);
    loaded = 1;
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void discard(void *prepared)
{
    free(prepared);
}

static void fail(wgf_handle_t resource)
{
    (void)resource;
    loaded = 2;
}

static const wgf_core_priv_loader_t loader = {"text", prepare, finish, discard, fail, NULL, false, NULL};

static void finish_visit(void)
{
    wgf_core_priv_shutdown();
    emscripten_force_exit(failures == 0 ? 0 : 1); /* ends the frame loop too */
}

static void frame(void)
{
    wgf_core_priv_update();
    if (visit == 1) {
        if (step == 0) {
            task = wgf_fs_write_text("level/intro.txt", "stored last visit");
            step++;
        } else if (step == 1 && wgf_fs_task_get_status(task) != WGF_FS_TASK_STATUS_PENDING) {
            expect(wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_DONE, "write");
            wgf_fs_task_destroy(task);
            step++;
        } else if (step == 2 && wgf_core_priv_fs_is_settled()) {
            finish_visit();
        }
    } else {
        if (step == 0) {
            if (!wgf_core_priv_fs_is_ready()) return;
            expect(!wgf_core_priv_fs_exists("level/intro.txt"), "memory starts empty");
            expect(wgf_core_priv_fs_is_cached("level/intro.txt"), "the store has it");
            expect(wgf_core_priv_load_request(&loader, "level/intro.txt", 1), "request");
            step++;
        } else if (step == 1 && loaded != 0) {
            expect(loaded == 1 && strcmp(text, "stored last visit") == 0, "loaded from the store");
            wgf_core_priv_fs_clear();
            step++;
        } else if (step == 2 && wgf_core_priv_fs_is_settled()) {
            finish_visit();
        }
    }
}

int main(int argc, char **argv)
{
    visit = argc > 1 ? atoi(argv[1]) : 0;
    if (visit < 1 || visit > 2) {
        printf("FAIL: run by tools/run_in_browser.py --visits 2, which passes the visit number\n");
        return 1;
    }
    wgf_core_priv_init();
    emscripten_set_main_loop(frame, 0, 1); /* doesn't return: finish_visit exits */
    return 0;
}
