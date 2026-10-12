#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_body.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_component.h"
#include "wgf_debug.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_physics.h"
#include "wgf_probe.h"
#include "wgf_random.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* A combination scene (step 13): a yard streamed in. Kenney's racing kit pieces (glTF
 * files) are asked for a few at a time, the nearest first, as a game streams a level's
 * sections around its camera, behind a progress panel; each piece is a static body of its
 * own triangles, made by physics once its file has arrived and its models are built, and balls
 * and crates fall from above all along, landing on what has arrived and falling past
 * what hasn't. On the web each file is fetched as it is asked for; natively it is read.
 *
 *   R            stream the yard again (every piece let go of, and asked for anew)
 *   Esc          quit, where quitting means anything
 *
 * It mixes streaming, glTF files, physics, shadows, and the UI in one frame, which no
 * example does; what it found is in docs/HISTORY.md ("Milestone 2, step 13"). */

#define S 4.0f                /* the kit's pieces are a meter a tile: four here */
#define PIECES_MAX 64
#define DROPS_MAX 160
#define STREAM_EVERY 0.15f /* seconds between pieces asked for */
#define DROP_EVERY 0.12f

typedef struct piece_t {
    const char *file; /* under models/kit/ */
    float x, z, turn; /* where, in tiles; its turn about y, in quarter turns */
} piece_t;

/* The yard: a road two pieces wide, a grandstand along it, barrier walls on its other
 * side, trees beyond, light posts, and cones on the road. */
static piece_t layout[PIECES_MAX];
static int layout_count;

static struct {
    wgf_actor_t stage, camera;
    wgf_actor_t pieces[PIECES_MAX]; /* 0 until asked for */
    int asked;                      /* pieces asked for so far, in the layout's order */
    wgf_actor_t drops[DROPS_MAX];
    wgf_mesh_t ball, crate;
    wgf_material_t ball_material, crate_material;
    float stream_clock, drop_clock, time, done_time;
} g;

static void add(const char *file, float x, float z, float turn)
{
    if (layout_count < PIECES_MAX) layout[layout_count++] = (piece_t){file, x, z, turn};
}

static void make_layout(void)
{
    int i;
    for (i = 0; i < 5; i++) { /* the road: two lanes of long straights, 2 tiles each */
        add("roadStraightLong", 0, 4.0f - 2.0f * (float)i, 0);
        add("roadStraightLong", 1, 4.0f - 2.0f * (float)i, 0);
    }
    for (i = 0; i < 6; i++) add("grandStand", 2.15f, 2.0f - (float)i, 3); /* facing the road */
    for (i = 0; i < 10; i++) add("barrierWall", -0.15f, 4.0f - (float)i, 1);
    add("treeLarge", -2.0f, 3.0f, 0);
    add("treeSmall", -2.6f, 1.2f, 0);
    add("treeLarge", -1.8f, -0.8f, 0);
    add("treeSmall", -2.4f, -2.6f, 0);
    add("treeLarge", -2.1f, -4.2f, 0);
    for (i = 0; i < 3; i++) add("lightPostLarge", 2.3f, 3.0f - 3.0f * (float)i, 0);
    for (i = 0; i < 6; i++) add("pylon", 0.5f + (float)(i % 2), 2.5f - 1.3f * (float)i, 0);
}

static void ask(int i)
{
    char path[64];
    const piece_t *p = &layout[i];
    wgf_mesh_t mesh;
    snprintf(path, sizeof(path), "models/kit/%s.glb", p->file);
    mesh = wgf_mesh_create(path); /* PENDING: fetched or read, then its models built */
    g.pieces[i] = wgf_model_create(mesh);
    wgf_resource_release(mesh); /* the model holds it */
    wgf_actor_set_position(g.pieces[i], p->x * S, 0, p->z * S);
    wgf_actor_set_rotation(g.pieces[i], 0, p->turn * 1.5707963f, 0);
    wgf_actor_set_scale(g.pieces[i], S, S, S);
    wgf_actor_set_parent(g.pieces[i], g.stage);
    /* a static body of its triangles, made as asked for: physics makes it once the file's
       models are built (and again should the file load again) */
    wgf_actor_add_component(g.pieces[i], WGF_COMPONENT_BODY);
    wgf_body_set_type(g.pieces[i], WGF_BODY_TYPE_STATIC);
    wgf_body_set_shape(g.pieces[i], WGF_BODY_SHAPE_MESH, 0, 0, 0);
}

/* The pieces arrived: those whose file's models are built (a glTF file's models are actors
 * made under its root as it loads). */
static int arrived(void)
{
    int i, count = 0;
    for (i = 0; i < g.asked; i++) count += wgf_actor_get_child_count(g.pieces[i]) > 0;
    return count;
}

static void stream_again(void)
{
    int i;
    for (i = 0; i < g.asked; i++) wgf_actor_destroy(g.pieces[i], WGF_ACTOR_DESTROY_CHILDREN);
    for (i = 0; i < PIECES_MAX; i++) g.pieces[i] = 0;
    g.asked = 0;
    g.done_time = 0;
}

/* A ball or a crate from above the yard; one fallen past everything let go of. */
static void drop(void)
{
    int i, free = -1;
    for (i = 0; i < DROPS_MAX; i++) {
        if (g.drops[i] != 0 && wgf_actor_get_position(g.drops[i]).y < -30.0f) {
            wgf_actor_destroy(g.drops[i], WGF_ACTOR_DESTROY_CHILDREN);
            g.drops[i] = 0;
        }
        if (g.drops[i] == 0 && free < 0) free = i;
    }
    if (free < 0) return;
    {
        const bool ball = wgf_random_get_float() < 0.5f;
        const wgf_actor_t d = wgf_model_create(ball ? g.ball : g.crate);
        wgf_model_set_material(d, -1, ball ? g.ball_material : g.crate_material);
        wgf_actor_set_position(d, wgf_random_get_range(-3.0f, 14.0f), 18.0f, wgf_random_get_range(-20.0f, 16.0f));
        wgf_actor_set_parent(d, g.stage);
        wgf_actor_add_component(d, WGF_COMPONENT_BODY);
        if (ball) {
            wgf_body_set_shape(d, WGF_BODY_SHAPE_SPHERE, 0.45f, 0, 0);
            wgf_body_set_bounce(d, 0.4f);
        } else {
            wgf_body_set_shape(d, WGF_BODY_SHAPE_BOX, 0.8f, 0.8f, 0.8f);
        }
        wgf_body_set_spin(d, wgf_random_get_range(-2, 2), wgf_random_get_range(-2, 2), wgf_random_get_range(-2, 2));
        g.drops[free] = d;
    }
}

static wgf_material_t pbr(float r, float gr, float b, float roughness)
{
    const wgf_material_t material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(material, "base_color", r, gr, b, 1.0f);
    wgf_material_set_float(material, "roughness", roughness);
    return material;
}

static void init(void *user)
{
    wgf_actor_t sun;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(120, 160, 205, 255)); /* a sky */
    wgf_random_set_seed(7);
    make_layout();
    wgf_physics_set_gravity(0, -9.81f, 0);

    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, wgf_color_make(150, 170, 210, 255), 0.35f);
    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_light_set_color(sun, wgf_color_make(255, 240, 220, 255));
    wgf_light_set_intensity(sun, 2.4f);
    wgf_actor_look_at(sun, -0.45f, -1.0f, -0.3f, 0, 1, 0);
    wgf_light_set_shadow_casting(sun, true);
    wgf_light_set_shadow_distance(sun, 60.0f);
    wgf_actor_set_parent(sun, g.stage);

    g.ball = wgf_mesh_create_sphere(0.45f, 16, 24);
    g.crate = wgf_mesh_create_cube(0.8f, 0.8f, 0.8f);
    g.ball_material = pbr(0.9f, 0.35f, 0.15f, 0.35f);
    g.crate_material = pbr(0.55f, 0.4f, 0.22f, 0.8f);

    wgf_ui_set_style_color(WGF_UI_COLOR_PANEL, wgf_color_make(16, 20, 30, 220));
    wgf_ui_set_style_value(WGF_UI_VALUE_TEXT_SIZE, 16);
}

/* The progress panel, at the top, while pieces are still to arrive and a moment after. */
static void progress(int arrived)
{
    char line[64];
    if (!wgf_ui_begin()) return;
    wgf_ui_begin_box("screen", WGF_UI_DIRECTION_COLUMN); /* the root centers: a box the screen's size */
    wgf_ui_set_width(WGF_UI_SIZING_GROW, 0);
    wgf_ui_set_height(WGF_UI_SIZING_GROW, 0);
    wgf_ui_set_align(WGF_UI_ALIGN_CENTER, WGF_UI_ALIGN_START);
    wgf_ui_set_padding(0, 24);
    wgf_ui_begin_panel("progress");
    snprintf(line, sizeof(line), arrived < layout_count ? "Streaming the yard: %d of %d pieces"
                                                        : "The yard: all %d of %d pieces",
             arrived, layout_count);
    wgf_ui_label(line, 0);
    wgf_ui_progress((float)arrived / (float)layout_count);
    wgf_ui_end_panel();
    wgf_ui_end_box();
    wgf_ui_end();
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    int count;
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_R)) stream_again();

    g.time += dt;
    g.stream_clock += dt;
    while (g.stream_clock >= STREAM_EVERY && g.asked < layout_count) { /* the next piece, nearest first */
        g.stream_clock -= STREAM_EVERY;
        ask(g.asked++);
    }
    if (g.asked == layout_count) g.stream_clock = 0;
    g.drop_clock += dt;
    while (g.drop_clock >= DROP_EVERY) {
        g.drop_clock -= DROP_EVERY;
        drop();
    }
    count = arrived();
    if (count == layout_count && g.done_time == 0) g.done_time = g.time;
    wgf_probe_set_value("stream.arrived", count);
    { /* the drops at rest on the yard, for an autopilot */
        int i, resting = 0;
        for (i = 0; i < DROPS_MAX; i++) {
            const wgf_vec3_t v = g.drops[i] != 0 ? wgf_body_get_velocity(g.drops[i]) : (wgf_vec3_t){0, -99, 0};
            resting += g.drops[i] != 0 && fabsf(v.y) < 0.5f && wgf_actor_get_position(g.drops[i]).y > -1.0f;
        }
        wgf_probe_set_value("stream.resting", resting);
    }

    wgf_actor_set_position(g.camera, 6.0f + 30.0f * sinf(g.time * 0.08f), 16.0f, -2.0f + 30.0f * cosf(g.time * 0.08f));
    wgf_actor_look_at(g.camera, 6.0f, 0, -2.0f, 0, 1, 0);
    wgf_stage3d_draw(g.stage);
    if (g.done_time == 0 || g.time - g.done_time < 2.0f) progress(count);
}

int main(void)
{
    wgf_window_set_title("libwgf stream");
    wgf_window_set_size(960, 540);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
