#include <stdio.h>
#include <string.h>

#include "wgf_core_load_priv.h"
#include "wgf_core_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_font.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_time.h"

/* A program that never enables bitmap fonts (wgf_font_enable_bitmaps): what reads their files
 * isn't linked, so a .fnt's font FAILED, said, and no lister names its pages. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void wait_task(wgf_handle_t task)
{
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

int main(void)
{
    static const char fnt[] = "info face=\"t\" size=8\ncommon lineHeight=10 base=8 scaleW=32 scaleH=16 pages=1\n"
                              "page id=0 file=\"page.png\"\nchars count=1\n"
                              "char id=65 x=0 y=0 width=5 height=7 xoffset=0 yoffset=1 xadvance=6 page=0 chnl=15\n";
    wgf_font_t font;
    double start;
    wgf_core_priv_init();
    expect(wgf_gfx_priv_start(), "setup");
    wait_task(wgf_fs_write("no_bitmaps_test/test.fnt", (const unsigned char *)fnt, (int)strlen(fnt)));
    expect(!wgf_font_is_bitmaps_enabled(), "bitmap fonts not linked");
    expect(wgf_core_priv_load_lister("no_bitmaps_test/test.fnt") == NULL, "and no lister for a .fnt");
    wgf_log_set_level(WGF_LOG_LEVEL_FATAL); /* the refusal logs, on purpose */
    font = wgf_font_create("no_bitmaps_test/test.fnt");
    start = wgf_time_get_seconds();
    while (wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0)
        wgf_core_priv_update();
    wgf_log_set_level(WGF_LOG_LEVEL_INFO);
    expect(font != 0 && wgf_resource_get_status(font) == WGF_RESOURCE_STATUS_FAILED, "a bitmap font FAILED");
    wgf_resource_release(font);
    wgf_gfx_priv_stop();
    wait_task(wgf_fs_rmdir("no_bitmaps_test"));
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_gfx_font_no_bitmaps_test: passed\n");
    return failures == 0 ? 0 : 1;
}
