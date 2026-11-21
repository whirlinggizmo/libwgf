#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* setenv */
#endif
#include <stdio.h>
#include <stdlib.h>

#include "wgf_app.h"
#include "wgf_window.h"

/* A program that never enables the autopilot, headless, given one all the same: it links
 * none of the runner, says so, and quits before its first frame, rather than running on
 * as if flown while a check waits for a result. */

static int failures, frames;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void on_frame(void *user)
{
    (void)user;
    if (++frames > 10) wgf_app_quit(); /* it ran on: the refusal failed */
}

int main(void)
{
    const char *path = "wgf_app_no_autopilot_test.txt";
    FILE *f = fopen(path, "w");
    if (f != NULL) {
        fputs("wgf-autopilot 1\nat 5 end\n", f);
        fclose(f);
    }
#if defined(_WIN32)
    _putenv_s("LIBWGF_AUTOPILOT", path);
#else
    setenv("LIBWGF_AUTOPILOT", path, 1);
#endif
    expect(!wgf_app_is_autopilot_enabled(), "no runner unless enabled");
    wgf_window_set_size(64, 64);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    expect(frames <= 1, "given an autopilot without the runner: refused, and quit");
    remove(path);
    if (failures == 0) printf("wgf_app_no_autopilot_test: passed\n");
    return failures == 0 ? 0 : 1;
}
