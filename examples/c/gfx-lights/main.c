#include <math.h>
#include <stddef.h>

#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_actor.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape3d.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* Directional, point, and spot lights on a stage. Five models on a grid:
 *   - a dim warm sun (directional)
 *   - a cyan point light orbiting through them, falling off with its range (a small
 *     sphere marks it; shapes are unlit, so it shows the light's color)
 *   - a white spotlight sweeping across them from above
 * The stage starts unlit (no lights, no ambient): everything here is explicit. Keys: 1
 * the sun, 2 the point light, 3 the spotlight, Escape quits where quitting means
 * anything.
 *
 * wgrender-c's lights example, which differs for the size table: its models are a
 * glTF character, animated, and behind them stand lit billboard sprites with a normal
 * map, which libwgf has none of until glTF (milestone 2, step 6), skinning (milestone
 * 3), and sprites in 3D (step 11). Here the models are generated capsules, white, at
 * the characters' places, and there are no billboards. And:
 *   - a light is switched with wgf_actor_set_visible, and aimed with wgf_actor_look_at
 *     where wgrender's is given a direction;
 *   - the grid is immediate mode in 3D, as wgrender's is;
 *   - the status line is three draws, no snprintf, so the program links no formatting;
 *   - the readout is libwgf's overlay (wgf_debug_show_fps), at wgrender's place. */

enum { MODEL_COUNT = 5 };

static struct {
    wgf_actor_t stage, camera, sun, lamp, lamp_marker, spot;
    float time;
} g;

static void init(void *user)
{
    const wgf_mesh_t capsule = wgf_mesh_create_capsule(0.4f, 1.8f, 16, 32);
    int i;
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(12, 13, 18, 255));
    g.camera = wgf_camera3d_create();
    wgf_actor_set_position(g.camera, 0, 4.5f, 10);
    wgf_actor_look_at(g.camera, 0, 1, 0, 0, 1, 0);
    g.stage = wgf_stage3d_create();
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, wgf_color_make(90, 110, 160, 255), 0.05f);

    for (i = 0; i < MODEL_COUNT; i++) {
        const wgf_actor_t model = wgf_model_create(capsule);
        wgf_actor_set_position(model, -4.0f + 2.0f * (float)i, 0.9f, (i % 2) ? -0.8f : 0.8f);
        wgf_actor_set_parent(model, g.stage);
    }
    wgf_resource_release(capsule); /* the models hold their own references */

    g.sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(g.sun, -0.4f, -1.0f, -0.6f, 0, 1, 0);
    wgf_light_set_color(g.sun, wgf_color_make(255, 210, 160, 255));
    wgf_light_set_intensity(g.sun, 1.1f);
    wgf_actor_set_parent(g.sun, g.stage);

    g.lamp = wgf_light_create(WGF_LIGHT_TYPE_POINT);
    wgf_light_set_color(g.lamp, wgf_color_make(60, 220, 255, 255));
    wgf_light_set_intensity(g.lamp, 20.0f);
    wgf_light_set_range(g.lamp, 5.0f);
    wgf_actor_set_parent(g.lamp, g.stage);
    g.lamp_marker = wgf_shape3d_create();
    wgf_shape3d_set_sphere(g.lamp_marker, 0.12f);
    wgf_shape3d_set_color(g.lamp_marker, wgf_color_make(60, 220, 255, 255));
    wgf_actor_set_parent(g.lamp_marker, g.lamp);

    g.spot = wgf_light_create(WGF_LIGHT_TYPE_SPOT);
    wgf_actor_set_position(g.spot, 0, 6, 2);
    wgf_light_set_spot_cone(g.spot, 0.14f, 0.28f); /* radians: about 8 and 16 degrees */
    wgf_light_set_intensity(g.spot, 125.0f);
    wgf_actor_set_parent(g.spot, g.stage);
}

static void toggle(wgf_actor_t light)
{
    wgf_actor_set_visible(light, !wgf_actor_is_visible(light));
}

static void frame(void *user)
{
    float lx, lz;
    (void)user;
    if (wgf_keyboard_is_pressed(WGF_KEY_1)) toggle(g.sun);
    if (wgf_keyboard_is_pressed(WGF_KEY_2)) toggle(g.lamp);
    if (wgf_keyboard_is_pressed(WGF_KEY_3)) toggle(g.spot);
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    g.time += wgf_loop_get_frame_delta();
    /* the point light orbits through the row; the spot sweeps left and right */
    lx = sinf(g.time * 0.6f) * 5.0f;
    lz = cosf(g.time * 0.6f) * 2.0f;
    wgf_actor_set_position(g.lamp, lx, 1.2f, lz);
    wgf_actor_set_visible(g.lamp_marker, wgf_actor_is_visible(g.lamp));
    wgf_actor_look_at(g.spot, sinf(g.time * 0.8f) * 0.7f, 5.0f, 1.7f, 0, 1, 0);

    if (wgf_draw_begin_3d(g.camera)) {
        wgf_draw_grid(20, 1.0f, wgf_color_make(40, 42, 50, 255));
        wgf_draw_end_3d();
    }
    wgf_stage3d_draw(g.stage);

    wgf_draw_text(0, "libwgf lights: directional, point, spot", 12, 12, 20, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, wgf_actor_is_visible(g.sun) ? "[1] sun on" : "[1] sun off", 12, 40, 16, WGF_COLOR_LIGHTGRAY);
    wgf_draw_text(0, wgf_actor_is_visible(g.lamp) ? "[2] point light on" : "[2] point light off", 132, 40, 16,
                  WGF_COLOR_LIGHTGRAY);
    wgf_draw_text(0, wgf_actor_is_visible(g.spot) ? "[3] spotlight on" : "[3] spotlight off", 312, 40, 16,
                  WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf lights");
    wgf_window_set_size(1000, 600);
    wgf_window_set_msaa(true);
    wgf_window_set_resizable(true);
    wgf_debug_show_fps(0, 12, 64, 16.0f, wgf_color_make(0, 255, 0, 255)); /* wgrender's text_draw_fps */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
