#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_canvas.h"
#include "wgf_color.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_time.h"

/* Sprites on sokol's dummy backend (a headless build): the defaults, each setting read
 * back, sizes before and after the texture is READY, what is refused, and what a
 * canvas draws: nothing while the texture is PENDING, the checker once it FAILED, the
 * texture once READY. References are wgf_gfx_node_test's. Pixels:
 * wgf_gfx_canvas_web_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static const unsigned char png_2x2[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00,
    0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81,
    0x34, 0x18, 0x00, 0x00, 0x49, 0xc8, 0x09, 0xf7, 0xf9, 0xab, 0xb6, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

static int drawn(wgf_node_t canvas)
{
    int before;
    wgf_platform_priv_set_size(320, 240);
    wgf_platform_priv_headless_set_dpi_scale(1.0f);
    wgf_gfx_priv_begin_frame();
    before = sgl_num_vertices();
    wgf_canvas_draw(canvas);
    before = sgl_num_vertices() - before;
    wgf_gfx_priv_end_frame();
    return before;
}

static void settle(wgf_texture_t texture)
{
    const double start = wgf_time_get_seconds();
    while (wgf_resource_get_status(texture) == WGF_RESOURCE_STATUS_PENDING && wgf_time_get_seconds() - start < 30.0) {
        wgf_core_priv_update();
    }
}

int main(void)
{
    wgf_node_t canvas, sprite;
    wgf_texture_t texture, missing;
    wgf_fs_task_t write;
    int i;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the missing texture warns, on purpose */
    expect(wgf_gfx_priv_start(), "setup");
    write = wgf_fs_write("sprite_test/rgbw.png", png_2x2, (int)sizeof(png_2x2));
    for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(write);

    canvas = wgf_canvas_create();
    texture = wgf_texture_create("sprite_test/rgbw.png");
    sprite = wgf_sprite_create(texture);
    wgf_node_set_parent(sprite, canvas);
    expect(wgf_node_get_type(sprite) == WGF_NODE_TYPE_SPRITE && wgf_sprite_get_texture(sprite) == texture, "a sprite");
    expect(wgf_sprite_get_pivot(sprite).x == 0.5f && wgf_sprite_get_pivot(sprite).y == 0.5f &&
               wgf_sprite_get_tint(sprite) == WGF_COLOR_WHITE && wgf_sprite_get_source(sprite).z == 0.0f,
           "defaults: centered, white, the whole texture");
    expect(wgf_sprite_get_size(sprite).x == 0.0f, "no size while its texture is PENDING and none is set");
    expect(drawn(canvas) == 0, "and nothing drawn");
    settle(texture);
    expect(wgf_sprite_get_size(sprite).x == 2.0f && wgf_sprite_get_size(sprite).y == 2.0f, "READY: the texture's size");
    expect(drawn(canvas) == 6, "drawn: one quad");

    expect(wgf_sprite_set_source(sprite, 1, 0, 1, 2) && wgf_sprite_get_source(sprite).x == 1.0f &&
               wgf_sprite_get_source(sprite).w == 2.0f && wgf_sprite_get_size(sprite).x == 1.0f,
           "a region: its size");
    expect(wgf_sprite_set_size(sprite, 32, 16) && wgf_sprite_get_size(sprite).x == 32.0f, "a size set");
    expect(wgf_sprite_set_size(sprite, 0, 16) && wgf_sprite_get_size(sprite).x == 1.0f, "unset again: the region's");
    expect(wgf_sprite_set_pivot(sprite, 0, 1) && wgf_sprite_get_pivot(sprite).y == 1.0f, "a pivot");
    expect(wgf_sprite_set_tint(sprite, WGF_COLOR_RED) && wgf_sprite_get_tint(sprite) == WGF_COLOR_RED, "a tint");

    missing = wgf_texture_create("sprite_test/missing.png");
    expect(wgf_sprite_set_texture(sprite, missing) && wgf_sprite_get_texture(sprite) == missing, "another texture");
    wgf_resource_release(missing);
    settle(missing);
    expect(drawn(canvas) == 6, "a FAILED texture: the checker, drawn");
    expect(wgf_sprite_set_texture(sprite, 0) && drawn(canvas) == 0, "no texture: nothing");
    expect(!wgf_sprite_set_texture(sprite, canvas) && !wgf_sprite_set_texture(sprite, 12345),
           "a handle that isn't a texture is refused");
    expect(wgf_sprite_create(canvas) == 0, "and a sprite of one");
    expect(!wgf_sprite_set_tint(canvas, WGF_COLOR_RED) && wgf_sprite_get_texture(canvas) == 0,
           "sprite calls refuse other kinds of node");

    wgf_resource_release(texture);
    wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
    write = wgf_fs_rmdir("sprite_test");
    for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(write);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
