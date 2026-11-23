#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "wgf_actor.h"
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
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_trig.h"
#include "wgf_window.h"

/* A heightmap, changed in place every frame: rolling hills made from a grid of heights
 * (wgf_mesh_create_heightmap), a ripple running out from the middle across them, its
 * corners moved, their normals made again, and each colored by its height, water to snow
 * (wgf_mesh_set_positions, _normals, _colors), lit by a sun under a white material the
 * vertex colors tint. The camera turns around it; W stills the ripple, O stops the camera,
 * and Escape quits where quitting means anything. */

enum { SIDE = 96, CORNERS = SIDE * SIDE };

#define WIDTH 16.0f  /* the terrain's size in x and z */
#define HEIGHT 2.5f  /* a height of 1, in units */

static struct {
    wgf_actor_t stage, camera;
    wgf_mesh_t land;
    bool orbit, rippling;
    float angle, time;
    float hills[CORNERS];        /* the heights the ripple runs over */
    float positions[CORNERS * 3];
    float colors[CORNERS * 4];
} g = {.orbit = true, .rippling = true};

/* A height's color: deep water, shallows, sand, grass, rock, snow, blended between. */
static void color_of(float height, float *out)
{
    static const float stops[6][4] = {
        {0.00f, 0.05f, 0.15f, 0.45f}, {0.18f, 0.10f, 0.45f, 0.75f}, {0.26f, 0.85f, 0.80f, 0.55f},
        {0.34f, 0.25f, 0.62f, 0.22f}, {0.62f, 0.45f, 0.40f, 0.35f}, {0.85f, 0.97f, 0.97f, 1.00f},
    };
    int i;
    if (height <= stops[0][0]) height = stops[0][0];
    for (i = 0; i < 5; i++) {
        if (height <= stops[i + 1][0] || i == 4) {
            float t = (height - stops[i][0]) / (stops[i + 1][0] - stops[i][0]);
            int c;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            for (c = 0; c < 3; c++) out[c] = stops[i][c + 1] + (stops[i + 1][c + 1] - stops[i][c + 1]) * t;
            out[3] = 1.0f;
            return;
        }
    }
}

/* Every corner placed and colored for the ripple at `g.time`, the mesh changed to match. */
static void shape_land(void)
{
    int row, column;
    for (row = 0; row < SIDE; row++) {
        for (column = 0; column < SIDE; column++) {
            const int i = row * SIDE + column;
            const float x = ((float)column / (SIDE - 1) - 0.5f) * WIDTH;
            const float z = ((float)row / (SIDE - 1) - 0.5f) * WIDTH;
            const float distance = sqrtf(x * x + z * z);
            const float fade = 1.0f / (1.0f + distance * 0.25f);
            const float height = g.hills[i] + 0.15f * fade * wgf_trig_sin(distance * 1.6f - g.time * 3.0f);
            g.positions[i * 3] = x;
            g.positions[i * 3 + 1] = height * HEIGHT;
            g.positions[i * 3 + 2] = z;
            color_of(height, &g.colors[i * 4]);
        }
    }
    wgf_mesh_set_positions(g.land, g.positions, CORNERS * 3);
    wgf_mesh_set_normals(g.land, NULL, 0); /* smooth again, from the triangles as they are now */
    wgf_mesh_set_colors(g.land, g.colors, CORNERS * 4);
}

static void init(void *user)
{
    wgf_actor_t sun, model;
    wgf_material_t ground;
    int row, column;
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(150, 190, 225, 255));

    g.camera = wgf_camera3d_create();
    g.stage = wgf_stage3d_create();
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, wgf_color_make(170, 200, 255, 255), 0.35f);
    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(sun, -0.6f, -1.0f, -0.3f, 0, 1, 0); /* from the origin, shining that way */
    wgf_light_set_intensity(sun, 3.0f);
    wgf_actor_set_parent(sun, g.stage);

    for (row = 0; row < SIDE; row++) { /* rolling hills, higher toward the back */
        for (column = 0; column < SIDE; column++) {
            const float u = (float)column / (SIDE - 1), v = (float)row / (SIDE - 1);
            g.hills[row * SIDE + column] = 0.3f + 0.12f * wgf_trig_sin(u * 9.0f) * wgf_trig_cos(v * 7.0f) +
                                           0.06f * wgf_trig_sin(u * 23.0f + v * 17.0f) + 0.35f * (1.0f - v) * u;
        }
    }
    g.land = wgf_mesh_create_heightmap(g.hills, CORNERS, SIDE, WIDTH, HEIGHT, WIDTH);
    model = wgf_model_create(g.land);
    ground = wgf_material_create(WGF_MATERIAL_SHADING_PBR); /* white: the corners' colors show */
    wgf_material_set_float(ground, "metallic", 0.0f);
    wgf_material_set_float(ground, "roughness", 0.85f);
    wgf_model_set_material(model, 0, ground);
    wgf_resource_release(ground); /* the model keeps it alive */
    wgf_actor_set_parent(model, g.stage);
    shape_land();
}

static void frame(void *user)
{
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_O)) g.orbit = !g.orbit;
    if (wgf_keyboard_is_pressed(WGF_KEY_W)) g.rippling = !g.rippling;
    if (g.orbit) g.angle += wgf_loop_get_frame_delta() * 0.15f;
    if (g.rippling) {
        g.time += wgf_loop_get_frame_delta();
        shape_land();
    }
    wgf_actor_set_position(g.camera, 16.0f * wgf_trig_sin(g.angle), 9.5f, 16.0f * wgf_trig_cos(g.angle));
    wgf_actor_look_at(g.camera, 0, 0.5f, 0, 0, 1, 0);

    wgf_stage3d_draw(g.stage);
    wgf_draw_text(0, "libwgf heightmap: a mesh changed in place each frame, colored at its corners", 12, 36, 20,
                  WGF_COLOR_BLACK);
    wgf_draw_text(0,
                  wgf_app_can_quit() ? "W: still the ripple   O: stop the camera   ESC: quit"
                                     : "W: still the ripple   O: stop the camera",
                  12, 64, 16, WGF_COLOR_DARKGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf heightmap");
    wgf_window_set_size(1000, 600);
    wgf_window_set_flags(WGF_WINDOW_FLAG_MSAA);
    wgf_window_set_resizable(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 120, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
