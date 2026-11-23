#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "sprite/wgf_gfx_sprite_batch2d_priv.h"
#include "wgf_color.h"
#include "wgf_core_priv.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_sprite_batch.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"
#include "wgf_time.h"

/* Sprite batches on sokol's dummy backend (a headless build): the defaults, each array
 * written and read back, what is refused and clipped, and what a stage draws: nothing while
 * the texture is PENDING or with no quads, every quad in one draw once READY, the checker
 * once FAILED, a batch drawn by two stages written once, and nothing on a 3D stage. */

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

/* One frame drawing `first` (and `second`, unless 0): the quads drawn, and in `draws` the
 * draws. */
static int drawn(wgf_actor_t first, wgf_actor_t second, int *draws)
{
    wgf_platform_priv_set_size(320, 240);
    wgf_platform_priv_headless_set_dpi_scale(1.0f);
    wgf_gfx_priv_begin_frame();
    wgf_stage2d_draw(first);
    if (second != 0) wgf_stage2d_draw(second);
    wgf_gfx_priv_end_frame();
    if (draws != NULL) *draws = wgf_gfx_priv_sprite_batch2d_get_draw_calls();
    return wgf_gfx_priv_sprite_batch2d_get_drawn();
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
    wgf_actor_t stage, batch;
    wgf_texture_t texture, missing;
    wgf_fs_task_t write;
    float xy[8] = {1, 2, 3, 4, 5, 6, 7, 8}, out[16] = {0};
    int tints[2] = {(int)0xFF0000FFu, (int)0x00FF00FFu}, tints_out[4] = {0};
    int i, draws = 0;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the missing texture warns, on purpose */
    expect(wgf_gfx_priv_start(), "setup");
    write = wgf_fs_write("sprite_batch_test/rgbw.png", png_2x2, (int)sizeof(png_2x2));
    for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(write);

    stage = wgf_stage2d_create();
    texture = wgf_texture_create("sprite_batch_test/rgbw.png");
    batch = wgf_sprite_batch_create(texture);
    wgf_actor_set_parent(batch, stage);
    expect(wgf_actor_get_kind(batch) == WGF_ACTOR_KIND_SPRITE_BATCH && wgf_sprite_batch_get_texture(batch) == texture &&
               wgf_sprite_batch_get_count(batch) == 0,
           "a batch, with no quads");
    expect(wgf_sprite_batch_get_pivot(batch).x == 0.5f &&
               wgf_sprite_batch_get_alpha_mode(batch) == WGF_ALPHA_MODE_BLEND &&
               wgf_sprite_batch_get_alpha_cutoff(batch) == 0.5f,
           "defaults: centered, blended");

    expect(wgf_sprite_batch_set_count(batch, 4) && wgf_sprite_batch_get_count(batch) == 4, "four quads");
    expect(wgf_sprite_batch_get_tints(batch, 0, tints_out, 4) == 4 && tints_out[3] == (int)WGF_COLOR_WHITE &&
               wgf_sprite_batch_get_positions(batch, 0, out, 8) == 8 && out[7] == 0.0f,
           "made white, at 0, 0");
    expect(drawn(stage, 0, NULL) == 0, "nothing drawn while the texture is PENDING");
    settle(texture);
    expect(drawn(stage, 0, &draws) == 4 && draws == 1, "READY: every quad, one draw");
    expect(drawn(stage, stage, &draws) == 8 && draws == 2, "drawn twice in a frame: two draws, one upload");

    expect(wgf_sprite_batch_set_positions(batch, 0, xy, 8) == 4 &&
               wgf_sprite_batch_get_positions(batch, 1, out, 6) == 6 &&
               out[0] == 3.0f && out[5] == 8.0f,
           "positions written and read back from the second");
    expect(wgf_sprite_batch_set_positions(batch, 3, xy, 8) == 1 &&
               wgf_sprite_batch_get_positions(batch, 3, out, 2) == 2 &&
               out[0] == 1.0f,
           "a write past the count clipped to it");
    expect(wgf_sprite_batch_set_positions(batch, 0, xy, 3) == 0 &&
               wgf_sprite_batch_set_positions(batch, 4, xy, 2) == 0 &&
               wgf_sprite_batch_set_positions(batch, -1, xy, 2) == 0 &&
               wgf_sprite_batch_set_positions(batch, 0, NULL, 2) == 0,
           "an odd count, a first outside, and NULL write none");
    expect(wgf_sprite_batch_set_sizes(batch, 0, xy, 4) == 2 && wgf_sprite_batch_get_sizes(batch, 0, out, 4) == 4 &&
               out[3] == 4.0f,
           "sizes");
    expect(wgf_sprite_batch_set_rotations(batch, 2, xy, 2) == 2 &&
               wgf_sprite_batch_get_rotations(batch, 2, out, 9) == 2 &&
               out[1] == 2.0f,
           "rotations, a read as many as there are");
    {
        const float regions[4] = {1.4f, -3.0f, 1.0f, 40000.0f};
        expect(wgf_sprite_batch_set_regions(batch, 1, regions, 4) == 1 &&
                   wgf_sprite_batch_get_regions(batch, 1, out, 4) == 4 && out[0] == 1.0f && out[1] == 0.0f &&
                   out[3] == 32767.0f && wgf_sprite_batch_set_regions(batch, 0, regions, 3) == 0,
               "regions rounded and clamped, four a quad");
    }
    expect(wgf_sprite_batch_set_tints(batch, 0, tints, 2) == 2 &&
               wgf_sprite_batch_get_tints(batch, 0, tints_out, 2) == 2 &&
               tints_out[0] == (int)0xFF0000FFu && tints_out[1] == (int)0x00FF00FFu,
           "tints, as packed colors");
    expect(wgf_sprite_batch_set_pivot(batch, 0, 1) && wgf_sprite_batch_get_pivot(batch).y == 1.0f, "a pivot");
    expect(wgf_sprite_batch_set_alpha_mode(batch, WGF_ALPHA_MODE_MASK, 2.0f) &&
               wgf_sprite_batch_get_alpha_mode(batch) == WGF_ALPHA_MODE_MASK &&
               wgf_sprite_batch_get_alpha_cutoff(batch) == 1.0f &&
               !wgf_sprite_batch_set_alpha_mode(batch, (wgf_alpha_mode_t)7, 0.5f),
           "an alpha mode: its cutoff clamped, one that isn't refused");
    expect(drawn(stage, 0, &draws) == 4 && draws == 1, "drawn after writes");

    expect(wgf_sprite_batch_set_count(batch, 2) && wgf_sprite_batch_get_count(batch) == 2 &&
               wgf_sprite_batch_get_tints(batch, 0, tints_out, 4) == 2 && drawn(stage, 0, NULL) == 2,
           "fewer: those past the count dropped");
    expect(wgf_sprite_batch_set_count(batch, 3) && wgf_sprite_batch_get_tints(batch, 2, tints_out, 1) == 1 &&
               tints_out[0] == (int)WGF_COLOR_WHITE,
           "more again: made afresh");
    expect(!wgf_sprite_batch_set_count(batch, -1) && !wgf_sprite_batch_set_count(batch, (1 << 24) + 1) &&
               wgf_sprite_batch_get_count(batch) == 3,
           "a count below 0 or too large refused");
    expect(wgf_sprite_batch_set_count(batch, 20000) && drawn(stage, 0, NULL) == 20000, "the buffer grown: 20,000");

    { /* on a 3D stage: nothing */
        const wgf_actor_t world = wgf_stage3d_create();
        const wgf_actor_t other = wgf_sprite_batch_create(texture);
        wgf_sprite_batch_set_count(other, 5);
        wgf_actor_set_parent(other, world);
        wgf_platform_priv_set_size(320, 240);
        wgf_gfx_priv_begin_frame();
        wgf_stage3d_draw(world);
        wgf_gfx_priv_end_frame();
        expect(wgf_gfx_priv_sprite_batch2d_get_drawn() == 0, "a batch on a 3D stage draws nothing");
        wgf_actor_destroy(world, WGF_ACTOR_DESTROY_CHILDREN);
    }
    { /* destroyed in the frame it was drawn: its buffer kept until the frame's end */
        const wgf_actor_t doomed = wgf_sprite_batch_create(texture);
        wgf_sprite_batch_set_count(doomed, 3);
        wgf_actor_set_parent(doomed, stage);
        wgf_platform_priv_set_size(320, 240);
        wgf_gfx_priv_begin_frame();
        wgf_stage2d_draw(stage);
        wgf_actor_destroy(doomed, WGF_ACTOR_DESTROY_CHILDREN);
        wgf_gfx_priv_end_frame();
        expect(wgf_gfx_priv_sprite_batch2d_get_drawn() == 20003, "a batch destroyed after its draw is still drawn");
    }

    missing = wgf_texture_create("sprite_batch_test/missing.png");
    expect(wgf_sprite_batch_set_texture(batch, missing) && wgf_sprite_batch_get_texture(batch) == missing,
           "another texture");
    wgf_resource_release(missing);
    settle(missing);
    expect(drawn(stage, 0, NULL) == 20000, "a FAILED texture: the checker, drawn");
    expect(wgf_sprite_batch_set_texture(batch, 0) && drawn(stage, 0, NULL) == 0, "no texture: nothing");
    expect(!wgf_sprite_batch_set_texture(batch, stage) && wgf_sprite_batch_create(stage) == 0,
           "a handle that isn't a texture is refused");
    expect(!wgf_sprite_batch_set_count(stage, 3) && wgf_sprite_batch_get_count(stage) == 0 &&
               wgf_sprite_batch_set_positions(stage, 0, xy, 2) == 0,
           "batch calls refuse other kinds of actor");

    wgf_resource_release(texture);
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    write = wgf_fs_rmdir("sprite_batch_test");
    for (i = 0; i < 1000 && wgf_fs_task_get_status(write) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    wgf_fs_task_destroy(write);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
