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
#include "wgf_emitter2d.h"
#include "wgf_emitter3d.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_mouse.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Particle emitters, libwgt's gfx-particles (wgrender's particles example). 3D emitters on
 * a 3D stage: a fountain (blended drops under gravity), sparks (added, from a point
 * circling the fountain: they trail behind it, are thrown along by it, slowed by drag, and
 * stretched along their motion), and a campfire: flames (added puffs from a flipbook
 * texture, colored by a curve) under smoke (blended, with size and color curves: a dark
 * puff that spreads, lightens, and fades as it rises). The steady ones are prewarmed, so
 * they're already going when they appear. Click or tap anywhere for a 2D confetti burst
 * there, on a 2D stage over the 3D one: squares cut from the middle of the particle
 * texture (so their spin shows), each tinted by a color picked from the emitter's palette.
 * The camera turns slowly around the stage. Each emitter is an actor: the sparks' source
 * moves by its actor's position.
 *
 *   click / tap   confetti
 *   Space         pause the steady emitters (the particles alive finish their lives)
 *   O             stop or restart the camera
 *   Esc           quit, where quitting means anything
 *
 * libwgt's, 1:1: "libwgt" reads "libwgf" in the title and heading, its scene and canvas
 * are libwgf's 3D and 2D stages, and its frame rate libwgf's overlay. */

#define PARTICLE_PATH "textures/particle.png"
#define FLAME_PATH "textures/flame.png" /* a 4x4 flipbook */

#define ORBIT_RADIUS 16.0f
#define ORBIT_SPEED 0.15f /* radians a second */

static struct {
    wgf_actor_t stage, camera, flat;
    wgf_actor_t fountain, sparks, smoke, flame, confetti;
    bool paused, orbit;
    float time, orbit_angle; /* radians around the fountain; holds while the orbit is stopped */
} g;

static wgf_actor_t add_emitter(wgf_texture_t texture, float x, float y, float z)
{
    const wgf_actor_t emitter = wgf_emitter3d_create();
    wgf_emitter3d_set_texture(emitter, texture);
    wgf_actor_set_position(emitter, x, y, z);
    wgf_actor_set_parent(emitter, g.stage);
    return emitter;
}

static void make_fountain(wgf_texture_t texture)
{
    g.fountain = add_emitter(texture, 0.0f, 0.2f, 0.0f);
    wgf_emitter3d_set_max(g.fountain, 4096);
    wgf_emitter3d_set_rate(g.fountain, 1200.0f);
    wgf_emitter3d_set_life(g.fountain, 1.4f, 1.9f);
    wgf_emitter3d_set_spawn_box(g.fountain, 0.15f, 0.0f, 0.15f);
    wgf_emitter3d_set_velocity(g.fountain, 0.0f, 9.0f, 0.0f, 0.22f, 0.15f);
    wgf_emitter3d_set_gravity(g.fountain, 0.0f, -9.8f, 0.0f);
    wgf_emitter3d_set_size(g.fountain, 0.22f, 0.12f, 0.4f);
    wgf_emitter3d_set_color(g.fountain, wgf_color_make(150, 210, 255, 230), wgf_color_make(60, 120, 255, 0));
    wgf_emitter3d_set_alpha_mode(g.fountain, WGF_ALPHA_MODE_BLEND, 0.0f);
    wgf_emitter3d_prewarm(g.fountain, 2.0f); /* already running when it opens */
}

static void make_sparks(wgf_texture_t texture)
{
    g.sparks = add_emitter(texture, 3.5f, 1.5f, 0.0f);
    wgf_emitter3d_set_max(g.sparks, 2048);
    wgf_emitter3d_set_rate(g.sparks, 600.0f);
    wgf_emitter3d_set_life(g.sparks, 0.5f, 1.2f);
    wgf_emitter3d_set_velocity(g.sparks, 0.0f, 4.0f, 0.0f, 1.2f, 0.6f);
    wgf_emitter3d_set_gravity(g.sparks, 0.0f, -6.0f, 0.0f);
    wgf_emitter3d_set_drag(g.sparks, 1.5f);             /* they slow down */
    wgf_emitter3d_set_inherit_velocity(g.sparks, 0.4f); /* thrown along by the moving source */
    wgf_emitter3d_set_stretch(g.sparks, 0.04f);         /* streaks along their motion */
    wgf_emitter3d_set_size(g.sparks, 0.08f, 0.02f, 0.5f);
    wgf_emitter3d_set_color(g.sparks, wgf_color_make(255, 220, 120, 255), wgf_color_make(255, 60, 10, 0));
    /* WGF_ALPHA_MODE_ADD is the default */
}

static void make_smoke(wgf_texture_t texture)
{
    g.smoke = add_emitter(texture, -5.0f, 1.5f, -2.0f); /* above the fire */
    wgf_emitter3d_set_max(g.smoke, 512);
    wgf_emitter3d_set_rate(g.smoke, 30.0f);
    wgf_emitter3d_set_life(g.smoke, 3.0f, 4.5f);
    wgf_emitter3d_set_spawn_sphere(g.smoke, 0.3f);
    wgf_emitter3d_set_velocity(g.smoke, 0.0f, 1.4f, 0.0f, 0.35f, 0.3f);
    wgf_emitter3d_set_gravity(g.smoke, 0.35f, 0.0f, 0.0f); /* a breeze */
    wgf_emitter3d_set_spin(g.smoke, -0.8f, 0.8f);
    /* curves: a quick puff that keeps spreading; dark, then light, then gone */
    wgf_emitter3d_clear_size_keys(g.smoke);
    wgf_emitter3d_add_size_key(g.smoke, 0.0f, 0.3f);
    wgf_emitter3d_add_size_key(g.smoke, 0.15f, 1.4f);
    wgf_emitter3d_add_size_key(g.smoke, 1.0f, 3.6f);
    wgf_emitter3d_clear_color_keys(g.smoke);
    wgf_emitter3d_add_color_key(g.smoke, 0.0f, wgf_color_make(40, 36, 34, 0));
    wgf_emitter3d_add_color_key(g.smoke, 0.1f, wgf_color_make(50, 46, 44, 190));
    wgf_emitter3d_add_color_key(g.smoke, 0.5f, wgf_color_make(130, 130, 140, 120));
    wgf_emitter3d_add_color_key(g.smoke, 1.0f, wgf_color_make(170, 170, 180, 0));
    wgf_emitter3d_set_alpha_mode(g.smoke, WGF_ALPHA_MODE_BLEND, 0.0f);
    wgf_emitter3d_prewarm(g.smoke, 5.0f);
}

/* A campfire's flames: puffs from a flipbook, played once over each one's life, glowing
 * white-yellow, then orange, red, and out as they rise and break up. */
static void make_flame(wgf_texture_t texture)
{
    g.flame = add_emitter(texture, -5.0f, 0.3f, -2.0f);
    wgf_emitter3d_set_frames(g.flame, 4, 4, 0, 0.0f);
    wgf_emitter3d_set_rate(g.flame, 40.0f);
    wgf_emitter3d_set_life(g.flame, 0.7f, 1.1f);
    wgf_emitter3d_set_spawn_sphere(g.flame, 0.3f);
    wgf_emitter3d_set_velocity(g.flame, 0.0f, 1.8f, 0.0f, 0.2f, 0.3f);
    wgf_emitter3d_set_drag(g.flame, 0.8f);
    wgf_emitter3d_set_spin(g.flame, -1.5f, 1.5f);
    wgf_emitter3d_clear_size_keys(g.flame);
    wgf_emitter3d_add_size_key(g.flame, 0.0f, 0.6f);
    wgf_emitter3d_add_size_key(g.flame, 0.3f, 1.1f);
    wgf_emitter3d_add_size_key(g.flame, 1.0f, 0.4f);
    wgf_emitter3d_clear_color_keys(g.flame);
    wgf_emitter3d_add_color_key(g.flame, 0.0f, wgf_color_make(255, 235, 190, 0));
    wgf_emitter3d_add_color_key(g.flame, 0.08f, wgf_color_make(255, 235, 190, 110));
    wgf_emitter3d_add_color_key(g.flame, 0.25f, wgf_color_make(255, 150, 40, 100));
    wgf_emitter3d_add_color_key(g.flame, 0.65f, wgf_color_make(200, 50, 15, 60));
    wgf_emitter3d_add_color_key(g.flame, 1.0f, wgf_color_make(80, 15, 5, 0));
    wgf_emitter3d_prewarm(g.flame, 1.0f); /* added: WGF_ALPHA_MODE_ADD, the default */
}

static void make_confetti(wgf_texture_t texture)
{
    g.confetti = wgf_emitter2d_create();
    wgf_emitter2d_set_texture(g.confetti, texture);
    wgf_actor_set_parent(g.confetti, g.flat);
    wgf_emitter2d_set_source(g.confetti, 24.0f, 24.0f, 16.0f, 16.0f); /* the dot's solid middle: squares */
    wgf_emitter2d_set_max(g.confetti, 4096);
    wgf_emitter2d_set_life(g.confetti, 1.2f, 2.2f);
    wgf_emitter2d_set_velocity(g.confetti, 0.0f, -420.0f, 1.3f, 0.7f);
    wgf_emitter2d_set_gravity(g.confetti, 0.0f, 700.0f);
    wgf_emitter2d_set_drag(g.confetti, 0.8f); /* flutters down instead of dropping */
    wgf_emitter2d_set_size(g.confetti, 12.0f, 6.0f, 0.5f);
    wgf_emitter2d_set_spin(g.confetti, -8.0f, 8.0f);
    wgf_emitter2d_set_color(g.confetti, WGF_COLOR_WHITE, wgf_color_make(255, 255, 255, 0));
    /* each piece picks one of these at birth */
    wgf_emitter2d_add_palette_color(g.confetti, WGF_COLOR_RED);
    wgf_emitter2d_add_palette_color(g.confetti, WGF_COLOR_GOLD);
    wgf_emitter2d_add_palette_color(g.confetti, WGF_COLOR_LIME);
    wgf_emitter2d_add_palette_color(g.confetti, WGF_COLOR_SKYBLUE);
    wgf_emitter2d_add_palette_color(g.confetti, WGF_COLOR_VIOLET);
    wgf_emitter2d_set_alpha_mode(g.confetti, WGF_ALPHA_MODE_BLEND, 0.0f);
}

/* A burst where (x, y) is: no trail from where the last one was. */
static void burst_confetti(float x, float y)
{
    wgf_emitter2d_skip_trail(g.confetti);
    wgf_actor_set_position(g.confetti, x, y, 0.0f);
    wgf_emitter2d_burst(g.confetti, 300);
}

static void init(void *user)
{
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_texture_t particle, flame;
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(14, 16, 24, 255));
    g.orbit = true;
    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(g.camera, 3.14159265f / 4.0f);
    wgf_actor_set_parent(g.camera, g.stage);
    wgf_stage3d_set_camera(g.stage, g.camera);
    g.flat = wgf_stage2d_create(); /* the confetti's, over the 3D stage */

    /* the emitters run at once; their particles are drawn once the textures load */
    particle = wgf_texture_create(PARTICLE_PATH);
    make_fountain(particle);
    make_sparks(particle);
    make_smoke(particle);
    make_confetti(particle);
    wgf_resource_release(particle); /* each emitter holds its own reference */
    flame = wgf_texture_create(FLAME_PATH);
    make_flame(flame);
    wgf_resource_release(flame);

    burst_confetti((float)wgf_window_get_width() * 0.5f, (float)wgf_window_get_height() * 0.4f); /* one to start */
}

static void set_paused(bool paused)
{
    g.paused = paused;
    wgf_emitter3d_set_emitting(g.fountain, !paused);
    wgf_emitter3d_set_emitting(g.sparks, !paused);
    wgf_emitter3d_set_emitting(g.smoke, !paused);
    wgf_emitter3d_set_emitting(g.flame, !paused);
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    char line[128];
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    g.time += dt;
    /* the sparks' source circles the fountain; the sparks stay where they were born */
    wgf_actor_set_position(g.sparks, 3.5f * cosf(g.time * 1.3f), 1.5f + 0.8f * sinf(g.time * 2.1f),
                          3.5f * sinf(g.time * 1.3f));
    if (wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT)) {
        const wgf_vec2_t mouse = wgf_mouse_get_position();
        burst_confetti(mouse.x, mouse.y);
    }
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) set_paused(!g.paused);
    if (wgf_keyboard_is_pressed(WGF_KEY_O)) g.orbit = !g.orbit;
    if (g.orbit) g.orbit_angle += dt * ORBIT_SPEED;
    wgf_actor_set_position(g.camera, ORBIT_RADIUS * sinf(g.orbit_angle), 6.0f, ORBIT_RADIUS * cosf(g.orbit_angle));
    wgf_actor_look_at(g.camera, 0.0f, 3.0f, 0.0f, 0, 1, 0);

    wgf_draw_begin_3d(g.camera);
    wgf_draw_grid(24, 1.0f, WGF_COLOR_DARKGRAY);
    wgf_draw_end_3d();
    wgf_stage3d_draw(g.stage);
    wgf_stage2d_draw(g.flat);

    wgf_draw_text(0, "libwgf particles", 12, 36, 24, WGF_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "click / tap: confetti   space: %s   O: %s the camera", g.paused ? "resume" : "pause",
             g.orbit ? "stop" : "turn");
    wgf_draw_text(0, line, 12, 70, 16, WGF_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "fountain %d   sparks %d   flame %d   smoke %d   confetti %d",
             wgf_emitter3d_get_count(g.fountain), wgf_emitter3d_get_count(g.sparks), wgf_emitter3d_get_count(g.flame),
             wgf_emitter3d_get_count(g.smoke), wgf_emitter2d_get_count(g.confetti));
    wgf_draw_text(0, line, 12, 94, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf particles");
    wgf_window_set_size(1000, 700);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255)); /* libwgt's wgt_loop_draw_fps */
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
