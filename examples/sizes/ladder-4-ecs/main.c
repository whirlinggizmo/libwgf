#include <stddef.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_stage2d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_component.h"
#include "wgf_motion.h"
#include "wgf_actor.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape2d.h"
#include "wgf_sprite.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* The feature cost ladder's step 4 (tools/measure_sizes.py, docs/benchmarks.md): the
 * skeleton (examples/c/app-skeleton) and, each adding one thing to the one before,
 *   1. text: one line drawn in the built-in font (fontstash, stb_truetype, the font).
 *   2. textures: one image loaded and drawn (the texture loader, stb_image).
 *   3. 2D sprites: the image as a sprite actor on a 2D stage (actors, 2D stages, sprites).
 *   4. ecs: a shape with motion on the stage2d (flecs, the systems).
 * Each step's size less the one before is what its feature costs on its own. Nothing to
 * look at for its own sake: it is measured, not shown. */

static wgf_texture_t tiles;
static wgf_actor_t world;

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(WGF_COLOR_RAYWHITE);
    wgf_asset_set_host("../assets");
    tiles = wgf_texture_create("textures/tiles.png");
    world = wgf_stage2d_create();
    {
        const wgf_actor_t sprite = wgf_sprite_create(tiles);
        wgf_actor_set_position(sprite, 400, 300, 0);
        wgf_actor_set_parent(sprite, world);
    }
    {
        const wgf_actor_t ship = wgf_shape2d_create();
        wgf_actor_set_parent(ship, world);
        wgf_actor_set_position(ship, 200, 300, 0);
        wgf_shape2d_set_circle(ship, 30);
        wgf_actor_add_component(ship, WGF_COMPONENT_MOTION);
        wgf_motion_set_spin(ship, 0, 0, 1);
    }
}

static void frame(void *user)
{
    (void)user;
    wgf_draw_text(0, "libwgf", 40, 40, 32, WGF_COLOR_DARKGRAY);
    wgf_draw_texture(tiles, 40, 100, 128, 128, WGF_COLOR_WHITE);
    wgf_stage2d_draw(world);
}

int main(void)
{
    wgf_window_set_title("ladder-4-ecs");
    wgf_window_set_size(800, 600);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
