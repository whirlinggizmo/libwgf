#include <stdio.h>
#include <string.h>

#include "mesh/wgf_gfx_mesh_record_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_actor.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_draw.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_platform_priv.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"

/* A lost GPU context's bookkeeping on sokol's dummy backend (a headless build), the loss a
 * test's (wgf_platform_priv_headless_set_gpu_lost): while lost no frame begins, so a draw,
 * a stage's draw, and a target begun do nothing; a target or blank texture made meanwhile
 * is READY with no GPU objects, and a blank texture's writes are taken; given back, the
 * parts' restores run once, before the frame, which is_context_restored marks alone; a
 * mesh's buffers are dropped and sent again by its next draw, and the target's objects
 * made; a loss and its return between two frames are seen by the count of restores. The
 * pixels coming back are checked in a browser (wgf_gfx_context_web_test). */

static int failures;
static int restores_run;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void count_restore(void)
{
    restores_run++;
}

static wgf_core_priv_part_t test_part = {.name = "context test",
                                         .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                         .order = WGF_CORE_PRIV_PART_MESHES,
                                         .restore = count_restore};

static bool has_target_objects(wgf_texture_t texture)
{
    sg_attachments attachments;
    int width, height;
    return wgf_gfx_priv_texture_get_target(texture, &attachments, &width, &height) &&
           attachments.colors[0].id != SG_INVALID_ID && attachments.depth_stencil.id != SG_INVALID_ID;
}

/* The frame app's runtime runs, as far as gfx goes: the context asked first. */
static void frame(wgf_actor_t stage)
{
    wgf_gfx_priv_render_update_context();
    wgf_gfx_priv_begin_frame();
    wgf_draw_rectangle(0, 0, 10, 10, WGF_COLOR_RED);
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
}

int main(void)
{
    static const float positions[9] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
    unsigned char pixels[4 * 4 * 4];
    wgf_actor_t stage, camera, model;
    wgf_mesh_t mesh;
    wgf_texture_t target, made_lost, blank;
    const wgf_gfx_priv_mesh_record_t *record;
    unsigned frame_before;

    wgf_core_priv_init();
    wgf_platform_priv_set_size(64, 64);
    expect(wgf_gfx_priv_start(), "start");
    wgf_core_priv_part_install(&test_part);
    expect(!wgf_render_is_context_lost() && !wgf_render_is_context_restored(), "not lost, not restored, at the start");

    stage = wgf_stage3d_create();
    camera = wgf_camera3d_create();
    wgf_actor_set_position(camera, 0, 0, 5);
    wgf_stage3d_set_camera(stage, camera);
    mesh = wgf_mesh_create_triangles(positions, 9, NULL, 0, NULL, 0, NULL, 0);
    model = wgf_model_create(mesh);
    wgf_actor_set_parent(model, stage);
    target = wgf_texture_create_target(16, 16);
    blank = wgf_texture_create_blank(4, 4);
    memset(pixels, 0x80, sizeof(pixels));
    expect(wgf_texture_write(blank, pixels, (int)sizeof(pixels)), "a blank texture written");

    frame(stage);
    record = wgf_gfx_priv_mesh_record(mesh);
    expect(record != NULL && record->primitive_count == 1 && record->primitives[0].vertex_buffer.id != SG_INVALID_ID,
           "a drawn mesh's buffers made");
    expect(has_target_objects(target), "a target's objects made");

    /* lost: no frame, nothing drawn, nothing said */
    wgf_platform_priv_headless_set_gpu_lost(true);
    frame_before = wgf_gfx_priv_render_get_frame();
    wgf_gfx_priv_render_update_context();
    expect(wgf_render_is_context_lost() && wgf_gfx_priv_render_is_lost(), "lost, once asked");
    wgf_gfx_priv_begin_frame();
    expect(!wgf_gfx_priv_is_in_frame(), "no frame begins while lost");
    expect(!wgf_render_begin_target(target), "a target isn't begun while lost");
    wgf_draw_rectangle(0, 0, 10, 10, WGF_COLOR_RED); /* nothing, and nothing said */
    wgf_render_push_clip(0, 0, 4, 4);
    wgf_render_pop_clip();
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
    frame(stage);
    expect(wgf_gfx_priv_render_get_frame() == frame_before, "no frame counted while lost");
    made_lost = wgf_texture_create_target(8, 8);
    expect(made_lost != 0 && wgf_resource_get_status(made_lost) == WGF_RESOURCE_STATUS_READY,
           "a target made while lost: READY");
    expect(!has_target_objects(made_lost), "a target made while lost: no GPU objects yet");
    expect(wgf_texture_write(blank, pixels, (int)sizeof(pixels)) &&
               wgf_texture_write(blank, pixels, (int)sizeof(pixels)),
           "a blank texture's writes taken while lost, every one");
    expect(restores_run == 0, "no restore while lost");

    /* given back: the restores run once, before the frame, which is marked */
    wgf_platform_priv_headless_set_gpu_lost(false);
    wgf_gfx_priv_render_update_context();
    expect(!wgf_render_is_context_lost() && wgf_render_is_context_restored(), "back, and restored this frame");
    expect(restores_run == 1, "the parts' restores ran once");
    expect(has_target_objects(made_lost), "the target made while lost: its objects made");
    record = wgf_gfx_priv_mesh_record(mesh);
    expect(record->primitives[0].vertex_buffer.id == SG_INVALID_ID, "the mesh's buffers dropped, to send again");
    wgf_gfx_priv_begin_frame();
    expect(wgf_gfx_priv_is_in_frame(), "a frame begins once back");
    expect(!wgf_texture_write(blank, pixels, (int)sizeof(pixels)) || !wgf_gfx_priv_render_can_be_lost(),
           "the frame it is back in takes no write where the last was kept");
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
    record = wgf_gfx_priv_mesh_record(mesh);
    expect(record->primitives[0].vertex_buffer.id != SG_INVALID_ID, "the mesh's buffers sent again as it drew");
    frame(stage);
    expect(!wgf_render_is_context_restored(), "restored for one frame alone");
    expect(restores_run == 1, "no restore without a loss");

    /* lost and given back between two frames: seen by the count of restores */
    wgf_platform_priv_headless_set_gpu_lost(true);
    wgf_platform_priv_headless_set_gpu_lost(false);
    wgf_gfx_priv_render_update_context();
    expect(!wgf_render_is_context_lost() && wgf_render_is_context_restored() && restores_run == 2,
           "a loss and its return between frames: restored");

    wgf_gfx_priv_stop();
    expect(!wgf_render_is_context_lost() && !wgf_render_is_context_restored(), "neither, once gfx stops");
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_gfx_context_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
