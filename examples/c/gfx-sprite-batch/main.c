#include <math.h>
#include <stddef.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_color.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_render.h"
#include "wgf_sprite_batch.h"
#include "wgf_stage2d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* A sprite batch: 4000 quads of one texture in one actor, one draw. Each is a region of the
 * tile sheet, turning and drifting round in a spiral, tinted along it, written as arrays
 * each frame; the batch itself turns slowly about the window's middle, through its actor.
 * Beside it, a second batch of the bunny, additive, its quads sized and placed once. Escape
 * quits where quitting means anything. */

#define QUADS 4000
#define BUNNIES 64

static wgf_actor_t spiral;
static float positions[QUADS * 2], rotations[QUADS], time_passed;

static void init(void *user)
{
    static float regions[QUADS * 4], sizes[QUADS * 2], places[BUNNIES * 2];
    static int tints[QUADS];
    wgf_actor_t stage, bunnies;
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(12, 14, 22, 255));
    stage = wgf_stage2d_create();

    spiral = wgf_sprite_batch_create(wgf_texture_create("textures/tiles.png"));
    wgf_sprite_batch_set_count(spiral, QUADS);
    for (i = 0; i < QUADS; i++) {
        const float t = (float)i / QUADS;
        regions[4 * i] = (float)((i % 5) * 16), regions[4 * i + 1] = (float)(((i / 5) % 3) * 16);
        regions[4 * i + 2] = 16, regions[4 * i + 3] = 16;
        sizes[2 * i] = sizes[2 * i + 1] = 6.0f + 14.0f * t;
        tints[i] = (int)wgf_color_make((int)(120 + 135 * t), (int)(200 - 120 * t), 255, 255);
    }
    wgf_sprite_batch_set_regions(spiral, 0, regions, QUADS * 4);
    wgf_sprite_batch_set_sizes(spiral, 0, sizes, QUADS * 2);
    wgf_sprite_batch_set_tints(spiral, 0, tints, QUADS);
    wgf_actor_set_position(spiral, 400, 300, 0);
    wgf_actor_set_parent(spiral, stage);

    bunnies = wgf_sprite_batch_create(wgf_texture_create("textures/wabbit_alpha.png"));
    wgf_sprite_batch_set_count(bunnies, BUNNIES);
    wgf_sprite_batch_set_alpha_mode(bunnies, WGF_ALPHA_MODE_ADD, 0.5f);
    for (i = 0; i < BUNNIES; i++) {
        places[2 * i] = 20.0f + (float)(i % 8) * 24.0f;
        places[2 * i + 1] = 20.0f + (float)(i / 8) * 30.0f;
    }
    wgf_sprite_batch_set_positions(bunnies, 0, places, BUNNIES * 2);
    wgf_sprite_batch_set_pivot(bunnies, 0, 0);
    wgf_actor_set_parent(bunnies, stage);
}

static void frame(void *user)
{
    int i;
    (void)user;
    time_passed += wgf_loop_get_frame_delta();
    if (wgf_keyboard_is_pressed(WGF_KEY_ESCAPE) && wgf_app_can_quit()) wgf_app_quit();
    for (i = 0; i < QUADS; i++) {
        const float t = (float)i / QUADS, angle = t * 40.0f + time_passed * 0.6f, radius = 20.0f + 260.0f * t;
        positions[2 * i] = cosf(angle) * radius;
        positions[2 * i + 1] = sinf(angle) * radius;
        rotations[i] = angle * 2.0f;
    }
    wgf_sprite_batch_set_positions(spiral, 0, positions, QUADS * 2);
    wgf_sprite_batch_set_rotations(spiral, 0, rotations, QUADS);
    wgf_actor_set_rotation(spiral, 0, 0, time_passed * 0.1f);
    wgf_stage2d_draw(wgf_actor_get_parent(spiral));
}

int main(void)
{
    wgf_window_set_title("libwgf: a sprite batch");
    wgf_window_set_size(800, 600);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
