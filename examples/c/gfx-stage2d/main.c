#include <math.h>
#include <stddef.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera2d.h"
#include "wgf_stage2d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_emitter2d.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_actor.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape2d.h"
#include "wgf_sprite.h"
#include "wgf_text.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* A 2D world on a 2D stage with a camera, and a HUD stage over it: a polygon rock
 * turning, a ring and a box, a sprite of a loaded texture, a text actor, and a particle
 * emitter circling and trailing; the camera zooms in and out. Escape quits where
 * quitting means anything. */

static wgf_actor_t world, hud, camera, rock, sprite, emitter;
static float time_passed;

static void init(void *user)
{
    static const float rock_points[] = {0, -40, 28, -30, 40, -4, 30, 26, 6, 40, -24, 30, -40, 6, -30, -24};
    wgf_texture_t tiles;
    wgf_actor_t ring, box, label;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(12, 14, 22, 255));

    world = wgf_stage2d_create();
    camera = wgf_camera2d_create();
    wgf_stage2d_set_camera(world, camera);

    rock = wgf_shape2d_create();
    wgf_shape2d_set_polygon(rock, rock_points, (int)(sizeof(rock_points) / sizeof(rock_points[0])));
    wgf_shape2d_set_outline(rock, 2);
    wgf_actor_set_parent(rock, world);

    ring = wgf_shape2d_create();
    wgf_shape2d_set_circle(ring, 30);
    wgf_shape2d_set_outline(ring, 3);
    wgf_shape2d_set_color(ring, WGF_COLOR_SKYBLUE);
    wgf_actor_set_position(ring, -120, 0, 0);
    wgf_actor_set_parent(ring, world);

    box = wgf_shape2d_create();
    wgf_shape2d_set_rectangle(box, 50, 30);
    wgf_shape2d_set_pivot(box, 0.5f, 0.5f);
    wgf_shape2d_set_color(box, WGF_COLOR_MAROON);
    wgf_actor_set_position(box, 120, 0, 0);
    wgf_actor_set_parent(box, world);

    tiles = wgf_texture_create("textures/tiles.png");
    sprite = wgf_sprite_create(tiles);
    wgf_resource_release(tiles); /* the sprite holds its own */
    wgf_sprite_set_size(sprite, 64, 64);
    wgf_actor_set_position(sprite, 0, 110, 0);
    wgf_actor_set_parent(sprite, world);

    emitter = wgf_emitter2d_create();
    wgf_emitter2d_set_rate(emitter, 120);
    wgf_emitter2d_set_life(emitter, 0.4f, 0.9f);
    wgf_emitter2d_set_speed(emitter, 10, 40);
    wgf_emitter2d_set_direction(emitter, 0, 6.2831853f);
    wgf_emitter2d_set_size(emitter, 4, 0);
    wgf_emitter2d_set_color(emitter, WGF_COLOR_GOLD, wgf_color_make(255, 60, 0, 0));
    wgf_actor_set_parent(emitter, world);

    hud = wgf_stage2d_create();
    label = wgf_text_create(0);
    wgf_text_set_string(label, "libwgf: gfx-stage2d");
    wgf_text_set_font_size(label, 20);
    wgf_actor_set_position(label, 12, 12, 0);
    wgf_actor_set_parent(label, hud);
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    time_passed += dt;
    wgf_actor_set_rotation(rock, 0, 0, time_passed * 0.6f);
    wgf_actor_set_rotation(sprite, 0, 0, -time_passed * 0.3f);
    wgf_actor_set_position(emitter, cosf(time_passed * 1.5f) * 160.0f, sinf(time_passed * 1.5f) * 80.0f, 0);
    wgf_camera2d_set_zoom(camera, 1.25f + 0.25f * sinf(time_passed * 0.5f));
    wgf_stage2d_draw(world);
    wgf_stage2d_draw(hud);
    wgf_draw_text(0, "immediate mode text, over the 2D stages", 12, 40, 14, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("gfx-stage2d");
    wgf_window_set_size(800, 450);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
