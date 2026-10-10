#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "texture/wgf_gfx_texture_priv.h"
#include "util/sokol_gl.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_core_resource_priv.h"
#include "wgf_core_priv.h"
#include "wgf_draw.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_texture.h"
#include "wgf_time.h"

/* Textures on sokol's dummy backend (a headless build): one made before there is a GPU
 * waits for it; a file is decoded, uploaded, and READY at its size; the same path is
 * the same texture; the last reference frees it; a missing or broken file FAILED;
 * sampling reads back and refuses what isn't one; and drawing: a READY texture draws,
 * a PENDING one draws nothing, a FAILED one draws the placeholder. Loaded again
 * (wgf_asset_reload): the same handle, READY all the while, at the new file's size once
 * it is in; a broken file keeps what it had; a FAILED one whose file is there now is READY.
 * Pixels: wgf_gfx_texture_web_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* A 4x4 PNG, red: the rock saved again, bigger. */
static const unsigned char boulder_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00, 0x12, 0x49,
    0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x19, 0x33, 0x90, 0x2e, 0x00, 0x00, 0x3c, 0x40,
    0x1f, 0xe1, 0x1a, 0xf3, 0xa5, 0x48, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

/* A 2x2 PNG, one pixel see-through. */
static const unsigned char rock_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x13, 0x49,
    0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x08, 0x41, 0xe0, 0x3f, 0x18, 0x00, 0x00, 0x3f,
    0xd2, 0x08, 0xf8, 0x65, 0x89, 0xa5, 0xdd, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static void begin_frame(void)
{
    wgf_platform_priv_set_size(64, 64);
    wgf_platform_priv_headless_set_dpi_scale(1.0f);
    wgf_gfx_priv_begin_frame();
}

/* Update until none of `textures` is pending, as a frame loop would; a worker decodes,
 * so wait on time, not on a count of updates. */
static void settle(const wgf_texture_t *textures, int count)
{
    const double start = wgf_time_get_seconds();
    for (;;) {
        int i, pending = 0;
        for (i = 0; i < count; i++) pending += wgf_resource_get_status(textures[i]) == WGF_RESOURCE_STATUS_PENDING;
        if (pending == 0 || wgf_time_get_seconds() - start > 30.0) return;
        wgf_core_priv_update();
    }
}

static void remove_file(const char *path)
{
    const wgf_fs_task_t task = wgf_fs_remove(path);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

static void write_file(const char *path, const unsigned char *data, int size)
{
    const wgf_fs_task_t task = wgf_fs_write(path, data, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(task);
}

/* The vertices drawing `texture` records. */
static int drawn(wgf_texture_t texture)
{
    int before;
    begin_frame();
    before = sgl_num_vertices();
    wgf_draw_texture(texture, 0, 0, 16, 16, WGF_COLOR_WHITE);
    before = sgl_num_vertices() - before;
    wgf_gfx_priv_end_frame();
    return before;
}

int main(void)
{
    wgf_texture_t early, rock, again, missing, broken;
    const unsigned char not_an_image[] = "this is not an image";

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the failures below warn, on purpose */
    write_file("images/rock.png", rock_png, (int)sizeof(rock_png));
    write_file("images/broken.png", not_an_image, (int)sizeof(not_an_image));

    /* before there is a GPU: decoded, then waiting for one */
    early = wgf_texture_create("images/rock.png");
    expect(early != 0 && wgf_resource_get_status(early) == WGF_RESOURCE_STATUS_PENDING, "pending at once");
    {
        int i;
        for (i = 0; i < 200; i++) wgf_core_priv_update();
    }
    expect(wgf_resource_get_status(early) == WGF_RESOURCE_STATUS_PENDING, "with no GPU yet, it waits for one");
    expect(wgf_gfx_priv_start(), "gfx starts");
    settle(&early, 1);
    expect(wgf_resource_get_status(early) == WGF_RESOURCE_STATUS_READY && wgf_texture_get_width(early) == 2 &&
               wgf_texture_get_height(early) == 2,
           "then READY, at its size");
    expect(strcmp(wgf_resource_get_path(early), "images/rock.png") == 0, "the file it read");

    /* one texture a file, reference counted */
    rock = wgf_texture_create("images/rock.png");
    expect(rock == early, "the same path is the same texture");
    again = wgf_texture_create("./images\\rock.png");
    expect(again == early, "and the same path written another way");
    wgf_resource_release(again);
    wgf_resource_release(rock);
    expect(wgf_resource_get_status(early) == WGF_RESOURCE_STATUS_READY, "still held once");
    wgf_resource_release(early);
    expect(wgf_resource_get_status(early) == WGF_RESOURCE_STATUS_NONE && wgf_texture_get_width(early) == 0,
           "the last reference frees it, and its handle goes stale");

    /* failures */
    missing = wgf_texture_create("images/missing.png");
    broken = wgf_texture_create("images/broken.png");
    {
        const wgf_texture_t both[2] = {missing, broken};
        settle(both, 2);
    }
    expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED, "a missing file FAILED");
    expect(wgf_resource_get_status(broken) == WGF_RESOURCE_STATUS_FAILED, "a file that isn't an image FAILED");
    expect(wgf_texture_get_width(broken) == 0, "with no size");

    /* sampling */
    rock = wgf_texture_create("images/rock.png");
    settle(&rock, 1);
    expect(wgf_texture_get_wrap_u(rock) == WGF_TEXTURE_WRAP_CLAMP && wgf_texture_get_filter(rock) == WGF_TEXTURE_FILTER_LINEAR,
           "defaults: clamp, linear");
    expect(wgf_texture_set_sampling(rock, WGF_TEXTURE_WRAP_REPEAT, WGF_TEXTURE_WRAP_MIRROR, WGF_TEXTURE_FILTER_NEAREST) &&
               wgf_texture_get_wrap_u(rock) == WGF_TEXTURE_WRAP_REPEAT &&
               wgf_texture_get_wrap_v(rock) == WGF_TEXTURE_WRAP_MIRROR &&
               wgf_texture_get_filter(rock) == WGF_TEXTURE_FILTER_NEAREST,
           "sampling reads back");
    expect(!wgf_texture_set_sampling(rock, (wgf_texture_wrap_t)3, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_LINEAR) &&
               !wgf_texture_set_sampling(rock, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, (wgf_texture_filter_t)2) &&
               !wgf_texture_set_sampling(12345, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_LINEAR),
           "a wrap or filter that isn't one, or a handle that isn't a texture, is refused");

    /* drawing */
    expect(drawn(rock) > 0, "a READY texture draws");
    expect(drawn(broken) > 0, "a FAILED one draws the placeholder");
    {
        const wgf_texture_t pending = wgf_texture_create("images/rock.png#another"); /* a new path: a new load */
        expect(wgf_resource_get_status(pending) == WGF_RESOURCE_STATUS_PENDING && drawn(pending) == 0,
               "a PENDING one draws nothing");
        wgf_resource_release(pending);
    }
    expect(drawn(12345) == 0 && drawn(0) == 0, "nothing for a handle that isn't a texture");
    wgf_draw_texture(rock, 0, 0, 16, 16, WGF_COLOR_WHITE); /* outside a frame: nothing, and no harm */

    /* loaded again, in place */
    {
        const double start = wgf_time_get_seconds();
        write_file("images/rock.png", boulder_png, (int)sizeof(boulder_png));
        expect(wgf_asset_reload("images/rock.png") == 1 && wgf_resource_get_status(rock) == WGF_RESOURCE_STATUS_READY &&
                   wgf_texture_get_width(rock) == 2,
               "loading again: READY all the while, at its old size until the new file is in");
        while (wgf_core_priv_resource_is_reloading(rock) && wgf_time_get_seconds() - start < 30.0) wgf_core_priv_update();
        expect(wgf_texture_get_width(rock) == 4 && wgf_texture_get_height(rock) == 4 && drawn(rock) > 0,
               "the same handle at the new file's size, drawn");
        write_file("images/rock.png", not_an_image, (int)sizeof(not_an_image));
        expect(wgf_asset_reload("images/rock.png") == 1, "a broken file, loading again");
        while (wgf_core_priv_resource_is_reloading(rock) && wgf_time_get_seconds() - start < 30.0) wgf_core_priv_update();
        expect(wgf_resource_get_status(rock) == WGF_RESOURCE_STATUS_READY && wgf_texture_get_width(rock) == 4,
               "keeps what it had (one error logged)");
        write_file("images/missing.png", rock_png, (int)sizeof(rock_png));
        expect(wgf_asset_reload("images/missing.png") == 1, "a FAILED texture whose file is there now, loading again");
        while (wgf_core_priv_resource_is_reloading(missing) && wgf_time_get_seconds() - start < 30.0) wgf_core_priv_update();
        expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_READY && wgf_texture_get_width(missing) == 2,
               "READY now");
        remove_file("images/missing.png"); /* missing again for the next run */
    }

    wgf_resource_release(rock);
    wgf_resource_release(missing);
    wgf_resource_release(broken);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
