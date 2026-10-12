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
#include "wgf_emitter3d.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_probe.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shader.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_ui.h"
#include "wgf_window.h"

/* A combination scene (step 13): a showroom, as a game's menu over its world would be.
 * The toy car turns on a platform under a sun casting shadows, its exhaust smoking and a
 * grinder's sparks flying beside it, with a menu panel over it -- all drawn into a render
 * target at a render scale (50, 75, or 100% of the screen's pixels, as a game renders
 * below the screen's size to keep its frame rate), drawn back over the whole screen, and
 * shown through a screen effect, a vignette.
 *
 *   1 / 2 / 3    render scale 50 / 75 / 100%   (or the menu's buttons)
 *   E            the vignette on and off
 *   P            the particles on and off
 *   Esc          quit, where quitting means anything
 *
 * It mixes the stage, shadows, particles, the UI, a target, and an effect in one frame,
 * which no example does; what it found is in docs/HISTORY.md ("Milestone 2, step 13").
 * One finding shows here: below 100% the menu, laid out in the target's pixels, is drawn
 * larger than it was laid out, but the pointer is read in the screen's, so a click lands
 * where the button would be at 100% (docs/ROADMAP.md, "From the combination scenes": a
 * render scale in the presentation will fix it); the keys work at every scale. */

#define CAR_PATH "models/toy_car.glb"
#define PARTICLE_PATH "textures/particle.png"
#define VIGNETTE_PATH "shaders/vignette.wgfshader"

static const int SCALES[] = {50, 75, 100}; /* percent of the screen's pixels */

static struct {
    wgf_actor_t stage, camera, platform, car, smoke, sparks;
    wgf_material_t vignette;
    wgf_texture_t target;
    int target_width, target_height;
    int scale; /* an index of SCALES */
    bool vignette_on, particles_on;
    float time;
} g = {.scale = 2, .vignette_on = true, .particles_on = true};

static wgf_material_t custom(const char *path)
{
    const wgf_shader_t shader = wgf_shader_create(path);
    const wgf_material_t material = wgf_material_create_custom(shader);
    wgf_resource_release(shader); /* the material holds its own reference */
    return material;
}

static wgf_actor_t model(wgf_mesh_t mesh, float r, float gr, float b, float roughness)
{
    const wgf_actor_t actor = wgf_model_create(mesh);
    const wgf_material_t material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_resource_release(mesh); /* the model holds it */
    wgf_material_set_vec4(material, "base_color", r, gr, b, 1.0f);
    wgf_material_set_float(material, "roughness", roughness);
    wgf_model_set_material(actor, -1, material);
    wgf_resource_release(material);
    return actor;
}

static void set_effects(void)
{
    wgf_render_clear_effects();
    if (g.vignette_on) wgf_render_add_effect(g.vignette);
}

static void set_particles(void)
{
    wgf_emitter3d_set_emitting(g.smoke, g.particles_on);
    wgf_emitter3d_set_emitting(g.sparks, g.particles_on);
}

/* The target at the render scale of the screen's pixels, made again when either changes. */
static void fit_target(void)
{
    const float pixels = wgf_render_get_dpi_scale() * (float)SCALES[g.scale] / 100.0f;
    const int width = (int)lroundf((float)wgf_render_get_width() * pixels);
    const int height = (int)lroundf((float)wgf_render_get_height() * pixels);
    if (g.target != 0 && width == g.target_width && height == g.target_height) return;
    if (g.target != 0) wgf_resource_release(g.target);
    g.target = wgf_texture_create_target(width > 0 ? width : 1, height > 0 ? height : 1);
    wgf_render_set_target_clear_color(g.target, wgf_color_make(18, 20, 26, 255));
    g.target_width = width;
    g.target_height = height;
}

static void init(void *user)
{
    wgf_actor_t sun, floor, wall;
    wgf_texture_t particle;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */

    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, wgf_color_make(110, 130, 170, 255), 0.18f);

    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_light_set_color(sun, wgf_color_make(255, 228, 196, 255));
    wgf_light_set_intensity(sun, 2.6f);
    wgf_actor_look_at(sun, -0.5f, -1.0f, -0.35f, 0, 1, 0); /* from the origin, shining that way */
    wgf_light_set_shadow_casting(sun, true);
    wgf_light_set_shadow_distance(sun, 14.0f);
    wgf_actor_set_parent(sun, g.stage);

    floor = model(wgf_mesh_create_plane(18.0f, 18.0f, 0), 0.08f, 0.08f, 0.09f, 0.7f);
    wgf_actor_set_parent(floor, g.stage);
    wall = model(wgf_mesh_create_cube(18.0f, 5.0f, 0.4f), 0.25f, 0.27f, 0.32f, 0.9f);
    wgf_actor_set_position(wall, 0, 2.5f, -4.0f);
    wgf_actor_set_parent(wall, g.stage);
    g.platform = model(wgf_mesh_create_cylinder(2.7f, 0.2f, 64), 0.55f, 0.56f, 0.6f, 0.3f);
    wgf_actor_set_position(g.platform, 0, 0.1f, 0);
    wgf_actor_set_parent(g.platform, g.stage);
    {
        const wgf_mesh_t mesh = wgf_mesh_create(CAR_PATH);
        g.car = wgf_model_create(mesh);
        wgf_resource_release(mesh);
        wgf_actor_set_position(g.car, 0, 0.2f, 0); /* on the platform, turning with it */
        wgf_actor_set_parent(g.car, g.platform);
    }

    particle = wgf_texture_create(PARTICLE_PATH);
    g.smoke = wgf_emitter3d_create(); /* the exhaust, behind the car: turns with it */
    wgf_emitter3d_set_texture(g.smoke, particle);
    wgf_actor_set_position(g.smoke, 0.55f, 0.45f, -2.1f); /* the car is 4 m long, its front +z */
    wgf_actor_set_parent(g.smoke, g.car);
    wgf_emitter3d_set_max(g.smoke, 256);
    wgf_emitter3d_set_rate(g.smoke, 24.0f);
    wgf_emitter3d_set_life(g.smoke, 2.0f, 3.0f);
    wgf_emitter3d_set_spawn_sphere(g.smoke, 0.08f);
    wgf_emitter3d_set_velocity(g.smoke, 0.0f, 0.5f, -0.8f, 0.2f, 0.2f);
    wgf_emitter3d_set_spin(g.smoke, -0.6f, 0.6f);
    wgf_emitter3d_clear_size_keys(g.smoke);
    wgf_emitter3d_add_size_key(g.smoke, 0.0f, 0.15f);
    wgf_emitter3d_add_size_key(g.smoke, 1.0f, 1.4f);
    wgf_emitter3d_clear_color_keys(g.smoke);
    wgf_emitter3d_add_color_key(g.smoke, 0.0f, wgf_color_make(90, 90, 96, 0));
    wgf_emitter3d_add_color_key(g.smoke, 0.15f, wgf_color_make(110, 110, 118, 150));
    wgf_emitter3d_add_color_key(g.smoke, 1.0f, wgf_color_make(170, 170, 180, 0));
    wgf_emitter3d_set_alpha_mode(g.smoke, WGF_ALPHA_MODE_BLEND, 0.0f);

    g.sparks = wgf_emitter3d_create(); /* a grinder at work by the wall */
    wgf_emitter3d_set_texture(g.sparks, particle);
    wgf_actor_set_position(g.sparks, 3.6f, 1.0f, -3.6f);
    wgf_actor_set_parent(g.sparks, g.stage);
    wgf_emitter3d_set_max(g.sparks, 1024);
    wgf_emitter3d_set_rate(g.sparks, 300.0f);
    wgf_emitter3d_set_life(g.sparks, 0.4f, 0.9f);
    wgf_emitter3d_set_velocity(g.sparks, -1.5f, 2.5f, 2.0f, 1.0f, 0.5f);
    wgf_emitter3d_set_gravity(g.sparks, 0.0f, -9.8f, 0.0f);
    wgf_emitter3d_set_drag(g.sparks, 0.8f);
    wgf_emitter3d_set_stretch(g.sparks, 0.03f); /* streaks along their motion */
    wgf_emitter3d_set_size(g.sparks, 0.06f, 0.02f, 0.4f);
    wgf_emitter3d_set_color(g.sparks, wgf_color_make(255, 230, 140, 255), wgf_color_make(255, 70, 10, 0));
    wgf_resource_release(particle); /* the emitters hold their own references */

    wgf_ui_set_style_color(WGF_UI_COLOR_PANEL, wgf_color_make(16, 20, 30, 220));
    wgf_ui_set_style_color(WGF_UI_COLOR_FOCUS, wgf_color_make(255, 200, 90, 255));
    wgf_ui_set_style_value(WGF_UI_VALUE_CORNER_RADIUS, 6);
    wgf_ui_set_style_value(WGF_UI_VALUE_TEXT_SIZE, 14);
    wgf_ui_set_style_value(WGF_UI_VALUE_BUTTON_WIDTH, 130);
    wgf_ui_set_style_value(WGF_UI_VALUE_GAP, 6);

    g.vignette = custom(VIGNETTE_PATH);
    wgf_material_set_float(g.vignette, "strength", 0.85f);
    wgf_material_set_float(g.vignette, "radius", 0.3f);
    wgf_material_set_vec4(g.vignette, "tint", 1.05f, 1.0f, 0.95f, 1.0f);
    set_effects();
}

/* The menu, a panel at the left: the render scale, the effect, the particles. */
static void menu(void)
{
    char label[48];
    int i;
    if (!wgf_ui_begin()) return;
    wgf_ui_begin_box("screen", WGF_UI_DIRECTION_COLUMN); /* the root centers: a box the screen's size, */
    wgf_ui_set_width(WGF_UI_SIZING_GROW, 0);             /* its panel at the top left */
    wgf_ui_set_height(WGF_UI_SIZING_GROW, 0);
    wgf_ui_set_align(WGF_UI_ALIGN_START, WGF_UI_ALIGN_START);
    wgf_ui_set_padding(16, 40);
    wgf_ui_begin_panel("showroom");
    wgf_ui_label("Showroom", 22);
    snprintf(label, sizeof(label), "%d x %d pixels", g.target_width, g.target_height);
    wgf_ui_label(label, 12);
    for (i = 0; i < 3; i++) {
        char id[16], text[24];
        snprintf(id, sizeof(id), "scale%d", SCALES[i]);
        snprintf(text, sizeof(text), "%s%d%%", i == g.scale ? "> " : "", SCALES[i]);
        if (wgf_ui_button(id, text)) g.scale = i;
    }
    if (wgf_ui_button("vignette", g.vignette_on ? "Vignette: on" : "Vignette: off")) {
        g.vignette_on = !g.vignette_on;
        set_effects();
    }
    if (wgf_ui_button("particles", g.particles_on ? "Particles: on" : "Particles: off")) {
        g.particles_on = !g.particles_on;
        set_particles();
    }
    if (wgf_app_can_quit() && wgf_ui_button("quit", "Quit")) wgf_app_quit();
    wgf_ui_end_panel();
    wgf_ui_end_box();
    wgf_ui_end();
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_1)) g.scale = 0;
    if (wgf_keyboard_is_pressed(WGF_KEY_2)) g.scale = 1;
    if (wgf_keyboard_is_pressed(WGF_KEY_3)) g.scale = 2;
    if (wgf_keyboard_is_pressed(WGF_KEY_E)) {
        g.vignette_on = !g.vignette_on;
        set_effects();
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_P)) {
        g.particles_on = !g.particles_on;
        set_particles();
    }

    g.time += dt;
    wgf_actor_set_rotation(g.platform, 0, g.time * 0.35f, 0);
    wgf_actor_set_position(g.camera, 6.5f * sinf(g.time * 0.1f), 2.6f, 6.5f);
    wgf_actor_look_at(g.camera, 0, 0.6f, 0, 0, 1, 0);

    fit_target();
    wgf_probe_set_value("showroom.scale", SCALES[g.scale]); /* for an autopilot */
    if (g.target != 0 && wgf_render_begin_target(g.target)) {
        wgf_stage3d_draw(g.stage);
        menu();
        wgf_render_end_target();
    }
    wgf_draw_texture(g.target, 0, 0, (float)wgf_render_get_width(), (float)wgf_render_get_height(),
                     WGF_COLOR_WHITE);
}

int main(void)
{
    wgf_window_set_title("libwgf showroom");
    wgf_window_set_size(960, 540);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
