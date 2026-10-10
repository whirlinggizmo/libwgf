#include <stdio.h>
#include <string.h>

#include "wgf_core_priv.h"
#include "wgf.h"
#include "wgf_core_fs_priv.h"

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    wgf_core_priv_update(); /* before init: does nothing */
    wgf_core_priv_shutdown(); /* before init: does nothing */

    wgf_core_priv_init();
    expect(wgf_core_priv_fs_is_ready(), "fs ready after init");
    wgf_core_priv_fs_set_root("custom");
    wgf_core_priv_init(); /* again while running: does nothing */
    expect(strcmp(wgf_core_priv_fs_root(), "custom") == 0, "a second init leaves fs alone");
    wgf_core_priv_update();

    wgf_core_priv_shutdown();
    expect(strcmp(wgf_core_priv_fs_root(), "") == 0, "shutdown stops fs");
    wgf_core_priv_shutdown(); /* again: does nothing */
    wgf_core_priv_update(); /* after shutdown: does nothing */

    wgf_core_priv_init();
    expect(wgf_core_priv_fs_is_ready(), "init starts core again");
    wgf_core_priv_shutdown();

    expect(strlen(wgf_version_get()) > 0, "version works without init");
    return failures == 0 ? 0 : 1;
}
