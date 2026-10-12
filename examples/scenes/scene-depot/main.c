#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_font.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_random.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_text.h"
#include "wgf_window.h"

/* A combination scene (step 13): a depot, flown through. Rows of stacks of crates and
 * barrels -- about two thousand models sharing two meshes and two materials, so the stage
 * draws them instanced, each with a tint of its own -- under a sun casting shadows, every
 * stack labeled in the world with 3D text facing the camera, and the camera flying a loop
 * down the aisles and back over the top.
 *
 *   SPACE        the camera stopped and started
 *   Esc          quit, where quitting means anything
 *
 * It mixes instancing, shadows, 3D text, and a moving camera at a size no example does;
 * what it found is in docs/HISTORY.md ("Milestone 2, step 13"). */

#define FONT_PATH "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"
#define ROWS 8           /* aisles run along z between them */
#define STACKS 20        /* stacks a row */
#define ROW_GAP 7.0f     /* meters between rows' middles: aisles 4.6 m wide */
#define STACK_GAP 3.2f   /* between stacks' middles */
#define CRATE 1.2f       /* a crate's side: a stack is two by two of them */
#define HEIGHT_MAX 5     /* layers a stack, at most */
#define AISLE_HALF (STACK_GAP * (float)STACKS * 0.5f + 4.0f) /* the aisles' half length, past the ends */
#define LOOP_SECONDS 60.0f

static struct {
    wgf_actor_t stage, camera;
    int models, labels;
    bool stopped;
    float clock;
} g;

static wgf_material_t pbr(float r, float gr, float b, float roughness, float metallic)
{
    const wgf_material_t material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(material, "base_color", r, gr, b, 1.0f);
    wgf_material_set_float(material, "roughness", roughness);
    wgf_material_set_float(material, "metallic", metallic);
    return material;
}

static void place(wgf_mesh_t mesh, wgf_material_t material, float x, float y, float z, float turn, wgf_color_t tint)
{
    const wgf_actor_t model = wgf_model_create(mesh);
    wgf_model_set_material(model, -1, material);
    wgf_model_set_tint(model, tint);
    wgf_actor_set_position(model, x, y, z);
    wgf_actor_set_rotation(model, 0, turn, 0);
    wgf_actor_set_parent(model, g.stage);
    g.models++;
}

/* A stack: layers of two by two crates, each a little askew, or of four barrels standing;
 * its label above. */
static void stack(wgf_mesh_t crate, wgf_mesh_t barrel, wgf_material_t wood, wgf_material_t steel, wgf_font_t font,
                  int row, int column)
{
    const float x = ((float)row - (float)(ROWS - 1) * 0.5f) * ROW_GAP;
    const float z = ((float)column - (float)(STACKS - 1) * 0.5f) * STACK_GAP;
    const bool barrels = wgf_random_get_float() < 0.25f;
    const int height = wgf_random_get_int(1, barrels ? 2 : HEIGHT_MAX);
    char name[16];
    float top = 0;
    int i;
    for (i = 0; i < height; i++) {
        if (barrels) { /* four barrels a layer, standing */
            int k;
            const wgf_color_t tint = wgf_color_make((unsigned char)wgf_random_get_int(140, 255), 90, 70, 255);
            for (k = 0; k < 4; k++) {
                place(barrel, steel, x + (k % 2 ? 0.45f : -0.45f), top + 0.6f, z + (k / 2 ? 0.45f : -0.45f), 0, tint);
            }
            top += 1.2f;
        } else {
            int k;
            for (k = 0; k < 4; k++) {
                const unsigned char shade = (unsigned char)wgf_random_get_int(170, 255);
                place(crate, wood, x + (k % 2 ? 0.61f : -0.61f), top + CRATE * 0.5f, z + (k / 2 ? 0.61f : -0.61f),
                      wgf_random_get_range(-0.08f, 0.08f),
                      wgf_color_make(shade, (unsigned char)(shade - 20), (unsigned char)(shade - 60), 255));
            }
            top += CRATE;
        }
    }
    {
        const wgf_actor_t label = wgf_text_create(font);
        snprintf(name, sizeof(name), "%c-%02d", 'A' + row, column + 1);
        wgf_text_set_string(label, name);
        wgf_text_set_font_size(label, 0.5f);
        wgf_text_set_color(label, wgf_color_make(255, 220, 120, 255));
        wgf_actor_set_position(label, x, top + 0.6f, z);
        wgf_actor_set_parent(label, g.stage);
        g.labels++;
    }
}

static void init(void *user)
{
    wgf_actor_t sun, ground;
    wgf_mesh_t crate, barrel, plane;
    wgf_material_t wood, steel, concrete;
    wgf_font_t font;
    int row, column;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(150, 180, 215, 255));
    wgf_random_set_seed(11);

    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(g.camera, 1.1f);
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, wgf_color_make(160, 180, 220, 255), 0.3f);
    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_light_set_color(sun, wgf_color_make(255, 236, 210, 255));
    wgf_light_set_intensity(sun, 2.5f);
    wgf_actor_look_at(sun, -0.35f, -1.0f, -0.55f, 0, 1, 0);
    wgf_light_set_shadow_casting(sun, true);
    wgf_light_set_shadow_distance(sun, 45.0f);
    wgf_actor_set_parent(sun, g.stage);

    plane = wgf_mesh_create_plane(90.0f, 90.0f, 0);
    concrete = pbr(0.42f, 0.42f, 0.4f, 0.9f, 0.0f);
    ground = wgf_model_create(plane);
    wgf_model_set_material(ground, -1, concrete);
    wgf_actor_set_parent(ground, g.stage);
    wgf_resource_release(plane);
    wgf_resource_release(concrete);

    crate = wgf_mesh_create_cube(CRATE, CRATE, CRATE);
    barrel = wgf_mesh_create_cylinder(0.4f, 1.2f, 20);
    wood = pbr(0.62f, 0.45f, 0.26f, 0.8f, 0.0f);
    steel = pbr(0.75f, 0.75f, 0.78f, 0.35f, 0.9f);
    font = wgf_font_create(FONT_PATH); /* the labels draw once it has loaded */
    for (row = 0; row < ROWS; row++) {
        for (column = 0; column < STACKS; column++) stack(crate, barrel, wood, steel, font, row, column);
    }
    wgf_resource_release(crate); /* the models hold them */
    wgf_resource_release(barrel);
    wgf_resource_release(wood);
    wgf_resource_release(steel);
    wgf_resource_release(font); /* the labels hold it */
}

/* The camera's loop: down the middle aisle at head height, then up over the stacks,
 * swinging out over the rows, and down to the start; a point along it, `t` 0 to 1. */
static void loop_at(float t, float *x, float *y, float *z)
{
    t -= floorf(t);
    if (t < 0.5f) { /* the aisle */
        *x = 0;
        *y = 2.0f;
        *z = -AISLE_HALF + 2.0f * AISLE_HALF * (t / 0.5f);
    } else { /* back over the top */
        const float u = (t - 0.5f) / 0.5f;
        *x = sinf(u * 3.14159265f) * ROW_GAP * 2.0f;
        *y = 2.0f + 14.0f * sinf(u * 3.14159265f);
        *z = AISLE_HALF - 2.0f * AISLE_HALF * u;
    }
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    float x, y, z, ax, ay, az;
    char line[64];
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) g.stopped = !g.stopped;
    if (!g.stopped) g.clock = fmodf(g.clock + dt, LOOP_SECONDS);

    loop_at(g.clock / LOOP_SECONDS, &x, &y, &z);
    loop_at((g.clock + 1.5f) / LOOP_SECONDS, &ax, &ay, &az); /* where it will be: looked at */
    wgf_actor_set_position(g.camera, x, y, z);
    wgf_actor_look_at(g.camera, ax, ay - 0.4f, az, 0, 1, 0);

    wgf_stage3d_draw(g.stage);
    snprintf(line, sizeof(line), "%d models, %d labels", g.models, g.labels);
    wgf_draw_text(0, line, 12, 36, 16, WGF_COLOR_RAYWHITE);
}

int main(void)
{
    wgf_window_set_title("libwgf depot");
    wgf_window_set_size(960, 540);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
