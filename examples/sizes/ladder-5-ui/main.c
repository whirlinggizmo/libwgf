#include <stddef.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_canvas.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_entity.h"
#include "wgf_motion.h"
#include "wgf_node.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape2d.h"
#include "wgf_sprite.h"
#include "wgf_texture.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* The feature cost ladder's step 5 (tools/measure_sizes.py, docs/benchmarks.md): the
 * skeleton (examples/c/app-skeleton) and, each adding one thing to the one before,
 *   1. text: one line drawn in the built-in font (fontstash, stb_truetype, the font).
 *   2. textures: one image loaded and drawn (the texture loader, stb_image).
 *   3. 2D sprites: the image as a sprite node in a canvas (nodes, canvases, sprites).
 *   4. ecs: an entity with a shape and motion under the canvas (flecs, the systems).
 *   5. ui: a panel with a label and a button (Clay, the widgets).
 * Each step's size less the one before is what its feature costs on its own. Nothing to
 * look at for its own sake: it is measured, not shown. */

static wgf_texture_t tiles;
static wgf_node_t world;

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(WGF_COLOR_RAYWHITE);
    wgf_asset_set_host("../assets");
    tiles = wgf_texture_create("textures/tiles.png");
    world = wgf_canvas_create();
    {
        const wgf_node_t sprite = wgf_sprite_create(tiles);
        wgf_node_set_position(sprite, 400, 300, 0);
        wgf_node_set_parent(sprite, world);
    }
    {
        const wgf_entity_t ship = wgf_entity_create(world);
        wgf_entity_set_position(ship, 200, 300, 0);
        wgf_entity_add_component(ship, WGF_COMPONENT_SHAPE2D);
        wgf_shape2d_set_circle(wgf_entity_get_component_node(ship, WGF_COMPONENT_SHAPE2D), 30);
        wgf_entity_add_component(ship, WGF_COMPONENT_MOTION);
        wgf_motion_set_spin(ship, 0, 0, 1);
    }
}

static void frame(void *user)
{
    (void)user;
    wgf_draw_text(0, "libwgf", 40, 40, 32, WGF_COLOR_DARKGRAY);
    wgf_draw_texture(tiles, 40, 100, 128, 128, WGF_COLOR_WHITE);
    wgf_canvas_draw(world);
    if (wgf_ui_begin()) {
        wgf_ui_begin_panel("panel");
        wgf_ui_label("libwgf", 0);
        wgf_ui_button("button", "Button");
        wgf_ui_end_panel();
        wgf_ui_end();
    }
}

int main(void)
{
    wgf_window_set_title("ladder-5-ui");
    wgf_window_set_size(800, 600);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
