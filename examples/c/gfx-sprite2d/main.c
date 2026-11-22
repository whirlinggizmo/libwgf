#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_loop.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_mouse.h"
#include "wgf_pointer.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Screen-space sprites over a 3D stage, libwgt's gfx-sprite2d (wgrender's sprite2d), the same
 * program for the size table:
 *   - "sheet": the 256x256 logo treated as a 2x2 sprite sheet; its source steps
 *     through the four quadrants like animation frames
 *   - "spin": turns about its center (the default pivot)
 *   - "swing": turns about its top-left corner (pivot 0, 0)
 *   - "flip": mirrored by a negative x scale
 *   - "tint": a white copy of the logo cycling through tint colors (a tint multiplies
 *     the texture's color, so it can't show on the black logo)
 *   - a one-off wgf_draw_texture in the corner (no actor)
 * The sprites are on a 2D stage drawn after the 3D one, so they're over the model and
 * picked first: both stages are interactive. Hovering enlarges the sprite under the pointer;
 * its pick alpha lets the pointer through the logo's see-through parts. Positions are
 * logical pixels.
 *
 * Where it isn't libwgt's word for word: "libwgt" reads "libwgf", its scene and canvas are
 * a stage3d and a stage2d, and the animation is named in the one call that plays it.
 *
 *   Esc   quit, where quitting means anything */

#define LOGO_PATH "sprites/logo/wg-logo-bw-alpha.png"
#define WHITE_LOGO_PATH "sprites/logo/wg-logo-white-alpha.png"
#define CHARACTER_PATH "models/woman_casual/woman_casual.glb"

enum { SPRITE_COUNT = 5, TINT_SPRITE = 4, PALETTE_SIZE = 24 };

static const char *names[SPRITE_COUNT] = {"sheet", "spin", "swing", "flip", "tint"};

static struct {
    wgf_actor_t scene, camera, stage, model;
    wgf_texture_t logo;
    wgf_color_t palette[PALETTE_SIZE];
    wgf_actor_t sprites[SPRITE_COUNT];
    float time;
    int frame;
} g;

static void init(void *user)
{
    wgf_actor_t sun;
    wgf_mesh_t mesh;
    wgf_texture_t white;
    int i;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files, one level above every program */
    wgf_render_set_clear_color(wgf_color_make(28, 30, 38, 255));
    for (i = 0; i < PALETTE_SIZE; i++) { /* colors are values, so cycle a palette */
        const float a = (float)i / PALETTE_SIZE * 6.2831853f;
        g.palette[i] = wgf_color_make((int)(127 + 127 * sinf(a)), (int)(127 + 127 * sinf(a + 2.1f)),
                                      (int)(127 + 127 * sinf(a + 4.2f)), 255);
    }

    g.scene = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_actor_set_position(g.camera, 0, 1.4f, 5.5f);
    wgf_actor_look_at(g.camera, 0, 1, 0, 0, 1, 0);
    wgf_actor_set_parent(g.camera, g.scene);
    wgf_stage3d_set_camera(g.scene, g.camera);
    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(sun, -0.5f, -1.0f, -0.7f, 0, 1, 0);
    wgf_light_set_intensity(sun, 3.0f);
    wgf_actor_set_parent(sun, g.scene);
    wgf_stage3d_set_ambient(g.scene, WGF_COLOR_WHITE, 0.35f);

    mesh = wgf_mesh_create(CHARACTER_PATH);
    g.model = wgf_model_create(mesh); /* its nodes appear once it has loaded */
    wgf_resource_release(mesh);
    wgf_model_play_animation(g.model, 3, 0.0f); /* plays once the file is there */
    wgf_model_set_animation_loop(g.model, true);
    wgf_actor_set_parent(g.model, g.scene);

    g.stage = wgf_stage2d_create();
    g.logo = wgf_texture_create(LOGO_PATH); /* kept for the one-off draw */
    white = wgf_texture_create(WHITE_LOGO_PATH);
    for (i = 0; i < SPRITE_COUNT; i++) {
        g.sprites[i] = wgf_sprite_create(i == TINT_SPRITE ? white : g.logo);
        wgf_sprite_set_size(g.sprites[i], 128, 128);
        wgf_sprite_set_pick_alpha(g.sprites[i], 0.5f);
        wgf_actor_set_parent(g.sprites[i], g.stage);
    }
    wgf_resource_release(white); /* the sprite holds its own reference */
    wgf_actor_set_position(g.sprites[0], 140, 170, 0);
    wgf_actor_set_position(g.sprites[1], 140, 380, 0);
    wgf_sprite_set_pivot(g.sprites[2], 0, 0);
    wgf_actor_set_position(g.sprites[2], 700, 110, 0);
    wgf_sprite_set_size(g.sprites[2], 96, 96);
    wgf_actor_set_position(g.sprites[3], 760, 400, 0);
    wgf_actor_set_scale(g.sprites[3], -1, 1, 1);
    wgf_actor_set_position(g.sprites[TINT_SPRITE], 450, 110, 0);
    wgf_sprite_set_size(g.sprites[TINT_SPRITE], 96, 96);

    wgf_pointer_set_interactive(g.scene, true);
    wgf_pointer_set_interactive(g.stage, true); /* drawn last: on top */
}

static void frame(void *user)
{
    const wgf_vec2_t mouse = wgf_mouse_get_position();
    const wgf_actor_t hovered = wgf_pointer_get_hovered();
    const char *hover_name = "nothing";
    char line[128];
    int i;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    g.time += wgf_loop_get_frame_delta();

    /* sprite sheet: one quadrant of the 256x256 texture per animation frame */
    g.frame = (int)(g.time * 2.0f) % 4;
    wgf_sprite_set_source(g.sprites[0], (float)(g.frame % 2) * 128, (float)(g.frame / 2) * 128, 128, 128);
    wgf_actor_set_rotation(g.sprites[1], 0, 0, g.time);
    wgf_actor_set_rotation(g.sprites[2], 0, 0, sinf(g.time * 1.5f) * 0.8f);
    wgf_sprite_set_tint(g.sprites[TINT_SPRITE], g.palette[(int)(g.time * 6.0f) % PALETTE_SIZE]);

    /* hover: the 2D stage's sprites are picked before the model behind them */
    for (i = 0; i < SPRITE_COUNT; i++) {
        const float grow = g.sprites[i] == hovered ? 1.15f : 1.0f;
        wgf_actor_set_scale(g.sprites[i], i == 3 ? -grow : grow, grow, 1); /* "flip" keeps its mirror */
        if (g.sprites[i] == hovered) hover_name = names[i];
    }
    if (hovered != 0 && wgf_actor_get_kind(hovered) == WGF_ACTOR_KIND_MODEL) hover_name = "model";

    wgf_stage3d_draw(g.scene);
    wgf_stage2d_draw(g.stage);
    wgf_draw_texture(g.logo, (float)wgf_window_get_width() - 74, 10, 64, 64, WGF_COLOR_WHITE); /* one-off, no actor */

    wgf_draw_text(0, "libwgf sprite2d: source rect, pivot, rotation, flip, picking", 12, 12, 16, WGF_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "mouse (%d, %d)  hover: %s  sheet frame %d", (int)mouse.x, (int)mouse.y, hover_name,
             g.frame);
    wgf_draw_text(0, line, 12, 36, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf sprite2d");
    wgf_window_set_size(900, 520);
    wgf_window_set_msaa(true);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
