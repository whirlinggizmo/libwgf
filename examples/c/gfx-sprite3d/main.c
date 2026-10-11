#include <math.h>
#include <stddef.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape3d.h"
#include "wgf_sprite.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* A textured sprite on a 3D stage, facing the camera as it circles, bobbing over a
 * pedestal, over a grid. libwgt's gfx-sprite3d (wgrender-c's sprite3d), 1:1: "libwgt"
 * reads "libwgf" in the title and heading, its scene is a stage3d, and its frame rate
 * libwgf's overlay.
 *
 *   Escape   quit, where quitting means anything */

#define LOGO_PATH "sprites/logo/wg-logo-bw-alpha.png"

static wgf_actor_t stage, camera, sprite;
static float t; /* seconds of frames */

static void init(void *user)
{
    wgf_texture_t texture;
    wgf_actor_t pedestal;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(20, 22, 30, 255));
    stage = wgf_stage3d_create();
    camera = wgf_camera3d_create();
    wgf_actor_set_parent(camera, stage);
    wgf_stage3d_set_camera(stage, camera);

    /* a pedestal for depth reference: a 3D shape, unlit */
    pedestal = wgf_shape3d_create();
    wgf_shape3d_set_cube(pedestal, 3.0f, 0.5f, 3.0f);
    wgf_shape3d_set_color(pedestal, WGF_COLOR_DARKGRAY);
    wgf_actor_set_position(pedestal, 0.0f, 0.25f, 0.0f);
    wgf_actor_set_parent(pedestal, stage);

    texture = wgf_texture_create(LOGO_PATH); /* nothing shows until it has loaded */
    sprite = wgf_sprite_create(texture);
    wgf_resource_release(texture); /* the sprite holds its own reference */
    wgf_sprite_set_size(sprite, 6.0f, 6.0f);
    wgf_sprite_set_facing(sprite, WGF_SPRITE_FACING_CAMERA);
    wgf_actor_set_parent(sprite, stage);
}

static void frame(void *user)
{
    (void)user;
    t += wgf_loop_get_frame_delta();
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    wgf_actor_set_position(camera, cosf(t * 0.3f) * 13.0f, 7.0f, sinf(t * 0.3f) * 13.0f);
    wgf_actor_look_at(camera, 0.0f, 2.5f, 0.0f, 0, 1, 0);
    wgf_actor_set_position(sprite, 0.0f, 3.5f + sinf(t * 1.5f) * 0.8f, 0.0f); /* bob */

    wgf_draw_begin_3d(camera);
    wgf_draw_grid(20, 1.0f, WGF_COLOR_DARKGRAY);
    wgf_draw_end_3d();
    wgf_stage3d_draw(stage);
    wgf_draw_text(0, "libwgf sprite3d", 12, 36, 24, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, wgf_resource_get_status(wgf_sprite_get_texture(sprite)) == WGF_RESOURCE_STATUS_READY
                         ? "a sprite facing the camera"
                         : "loading logo...",
                  12, 70, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf sprite3d");
    wgf_window_set_size(900, 700);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255)); /* libwgt's wgt_loop_draw_fps */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
