#include <stdbool.h>
#include <stdio.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_loop.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_mouse.h"
#include "wgf_pointer.h"
#include "wgf_presentation.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape2d.h"
#include "wgf_sprite.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_trig.h"
#include "wgf_window.h"

/* Picking and the pointer: click 3D models and 2D sprites to select them. A 3D stage of
 * shapes and an animated robot (Quaternius's, skinned: picked as it is posed this frame),
 * each hit by its triangles through the camera (wgf_stage3d_pick, through the pointer,
 * wgf_pointer.h): the one under the pointer drawn in wires, a click selecting it (gold).
 * Over it, a HUD of rounded-rectangle panels (wgf_draw_rectangle_rounded, a 2D shape's
 * corner radius) and bunnies hit only where their texels show (wgf_sprite_set_pick_alpha):
 * a bunny under the pointer grows, a click turns it gold, and a press on one captures the
 * pointer, so a drag that starts on it leaves the camera alone. Where the pointer meets the
 * world a small sphere marks the point, with its distance. Drag the background to turn the
 * camera; Escape quits where quitting means anything. */

#define SHAPES 4
#define BUNNIES 5

static struct {
    wgf_actor_t world, camera, hud;
    wgf_actor_t models[SHAPES + 1]; /* the shapes, then the robot */
    wgf_actor_t bunnies[BUNNIES], buttons[2];
    wgf_color_t tints[SHAPES + 1];
    bool selected[SHAPES + 1], chosen[BUNNIES];
    bool robot_playing;
    float yaw, pitch, time;
    int clicks;
} g = {.pitch = 0.35f};

static const char *const names[SHAPES + 1] = {"cube", "sphere", "torus", "cylinder", "robot"};

static void init(void *user)
{
    wgf_mesh_t meshes[SHAPES];
    wgf_texture_t bunny;
    wgf_actor_t sun;
    int i;
    (void)user;
    wgf_asset_set_host("../assets");
    wgf_render_set_clear_color(wgf_color_make(34, 40, 52, 255));

    g.world = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_actor_set_parent(g.camera, g.world);
    wgf_stage3d_set_camera(g.world, g.camera);
    wgf_stage3d_set_ambient(g.world, WGF_COLOR_WHITE, 0.35f);
    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(sun, -0.5f, -1.0f, -0.4f, 0, 1, 0);
    wgf_light_set_intensity(sun, 2.5f);
    wgf_actor_set_parent(sun, g.world);

    meshes[0] = wgf_mesh_create_cube(1.4f, 1.4f, 1.4f);
    meshes[1] = wgf_mesh_create_sphere(0.8f, 16, 24);
    meshes[2] = wgf_mesh_create_torus(0.7f, 0.25f, 16, 24);
    meshes[3] = wgf_mesh_create_cylinder(0.6f, 1.6f, 24);
    g.tints[0] = wgf_color_make(220, 90, 80, 255);
    g.tints[1] = wgf_color_make(90, 170, 230, 255);
    g.tints[2] = wgf_color_make(120, 210, 120, 255);
    g.tints[3] = wgf_color_make(200, 140, 230, 255);
    g.tints[SHAPES] = WGF_COLOR_WHITE;
    for (i = 0; i < SHAPES; i++) {
        g.models[i] = wgf_model_create(meshes[i]);
        wgf_resource_release(meshes[i]); /* the model holds its own */
        wgf_actor_set_parent(g.models[i], g.world);
        wgf_actor_set_position(g.models[i], -4.5f + 3.0f * (float)i, 0.9f, -1.5f);
        wgf_model_set_tint(g.models[i], g.tints[i]);
    }
    {
        const wgf_mesh_t robot = wgf_mesh_create("models/robot.glb");
        g.models[SHAPES] = wgf_model_create(robot);
        wgf_resource_release(robot);
        wgf_actor_set_parent(g.models[SHAPES], g.world);
        wgf_actor_set_position(g.models[SHAPES], 0.0f, 0.0f, 2.5f);
        wgf_actor_set_scale(g.models[SHAPES], 0.45f, 0.45f, 0.45f);
    }

    /* the HUD: two rounded buttons, and bunnies hit only where they show */
    g.hud = wgf_stage2d_create();
    bunny = wgf_texture_create("textures/wabbit_alpha.png");
    for (i = 0; i < BUNNIES; i++) {
        g.bunnies[i] = wgf_sprite_create(bunny);
        wgf_actor_set_parent(g.bunnies[i], g.hud);
        wgf_sprite_set_size(g.bunnies[i], 64, 74);
        wgf_sprite_set_pick_alpha(g.bunnies[i], 0.5f);
    }
    wgf_resource_release(bunny);
    for (i = 0; i < 2; i++) {
        g.buttons[i] = wgf_shape2d_create();
        wgf_shape2d_set_rectangle(g.buttons[i], 150, 40);
        wgf_shape2d_set_corner_radius(g.buttons[i], 14);
        wgf_actor_set_parent(g.buttons[i], g.hud);
    }
    wgf_pointer_set_interactive(g.world, true);
    wgf_pointer_set_interactive(g.hud, true);
}

/* Each frame's HUD layout, from the visible size, so it stays at the window's edges. */
static void place_hud(float width, float height)
{
    int i;
    for (i = 0; i < BUNNIES; i++) {
        const float x = width - 60.0f, y = 150.0f + 82.0f * (float)i;
        const bool over = wgf_pointer_get_hover_state(g.bunnies[i]) == WGF_INPUT_STATE_DOWN ||
                          wgf_pointer_get_hover_state(g.bunnies[i]) == WGF_INPUT_STATE_PRESSED;
        const float grow = over ? 1.15f : 1.0f;
        wgf_actor_set_position(g.bunnies[i], x, y, 0);
        wgf_actor_set_scale(g.bunnies[i], grow, grow, 1);
        wgf_sprite_set_tint(g.bunnies[i], g.chosen[i] ? WGF_COLOR_GOLD : WGF_COLOR_WHITE);
    }
    for (i = 0; i < 2; i++) {
        const wgf_input_state_t press = wgf_pointer_get_press_state(g.buttons[i]);
        const bool over = wgf_pointer_get_hovered() == g.buttons[i];
        wgf_actor_set_position(g.buttons[i], 24.0f + 170.0f * (float)i, height - 64.0f, 0);
        wgf_shape2d_set_color(g.buttons[i], press == WGF_INPUT_STATE_DOWN || press == WGF_INPUT_STATE_PRESSED
                                                ? wgf_color_make(240, 200, 90, 255)
                                                : (over ? wgf_color_make(110, 150, 220, 255)
                                                        : wgf_color_make(70, 100, 170, 255)));
    }
}

static void frame(void *user)
{
    const wgf_vec2_t at = wgf_mouse_get_position();
    const wgf_actor_t hovered = wgf_pointer_get_hovered();
    const wgf_vec4_t visible = wgf_presentation_get_visible();
    const float vw = visible.z, vh = visible.w;
    float hit[8] = {0};
    wgf_actor_t under;
    char line[160];
    int i;
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    g.time += wgf_loop_get_frame_delta();

    /* the robot plays once its file is in */
    if (!g.robot_playing &&
        wgf_resource_get_status(wgf_model_get_mesh(g.models[SHAPES])) == WGF_RESOURCE_STATUS_READY) {
        const int clip = wgf_model_find_animation(g.models[SHAPES], "Robot_Dance");
        wgf_model_play_animation(g.models[SHAPES], clip >= 0 ? clip : 0, 0.0f);
        g.robot_playing = true;
    }

    /* clicks: a model or a bunny chosen; the buttons select all, or none */
    for (i = 0; i <= SHAPES; i++) {
        if (wgf_pointer_is_clicked(g.models[i])) {
            g.selected[i] = !g.selected[i];
            g.clicks++;
        }
    }
    for (i = 0; i < BUNNIES; i++) {
        if (wgf_pointer_is_clicked(g.bunnies[i])) {
            g.chosen[i] = !g.chosen[i];
            g.clicks++;
        }
    }
    for (i = 0; i < 2; i++) {
        if (wgf_pointer_is_clicked(g.buttons[i])) {
            int k;
            for (k = 0; k <= SHAPES; k++) g.selected[k] = i == 0;
            for (k = 0; k < BUNNIES; k++) g.chosen[k] = i == 0;
        }
    }
    for (i = 0; i <= SHAPES; i++) wgf_model_set_tint(g.models[i], g.selected[i] ? WGF_COLOR_GOLD : g.tints[i]);
    for (i = 0; i < SHAPES; i++) wgf_actor_set_rotation(g.models[i], 0.0f, g.time * 0.5f + (float)i, 0.0f);

    /* the camera: dragged when the press wasn't on the HUD (a press there captures the pointer) */
    if (wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT) && !wgf_input_is_pointer_captured() &&
        wgf_pointer_get_held() == 0) {
        const wgf_vec2_t d = wgf_mouse_get_delta();
        g.yaw -= d.x * 0.008f;
        g.pitch += d.y * 0.006f;
        g.pitch = g.pitch < 0.05f ? 0.05f : (g.pitch > 1.2f ? 1.2f : g.pitch);
    } else if (!wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT)) {
        g.yaw += wgf_loop_get_frame_delta() * 0.1f;
    }
    wgf_actor_set_position(g.camera, 11.0f * wgf_trig_sin(g.yaw) * wgf_trig_cos(g.pitch),
                           1.0f + 11.0f * wgf_trig_sin(g.pitch), 11.0f * wgf_trig_cos(g.yaw) * wgf_trig_cos(g.pitch));
    wgf_actor_look_at(g.camera, 0, 0.8f, 0, 0, 1, 0);

    place_hud(vw, vh);

    wgf_stage3d_draw(g.world);
    /* the world under the pointer, picked here too: its point marked, the hovered model in wires */
    under = wgf_input_is_pointer_captured() ? 0 : wgf_stage3d_pick(g.world, at.x, at.y, hit, 8);
    if (wgf_draw_begin_3d(g.camera)) {
        wgf_draw_grid(12, 1.0f, wgf_color_make(70, 80, 96, 255));
        if (hovered != 0 && wgf_actor_get_kind(hovered) == WGF_ACTOR_KIND_MODEL) {
            wgf_draw_model_wires(hovered, WGF_COLOR_WHITE);
        }
        if (under != 0) {
            wgf_draw_sphere(hit[4], hit[5], hit[6], 0.06f, WGF_COLOR_YELLOW);
            wgf_draw_line_3d(hit[4], hit[5], hit[6], hit[4] + hit[0] * 0.5f, hit[5] + hit[1] * 0.5f,
                             hit[6] + hit[2] * 0.5f, WGF_COLOR_YELLOW);
        }
        wgf_draw_end_3d();
    }

    /* the HUD's panels, rounded, then its actors over them */
    wgf_draw_rectangle_rounded(12, 12, 520, 96, 16, 0, wgf_color_make(16, 20, 28, 210));
    wgf_draw_rectangle_rounded_lines(12, 12, 520, 96, 16, 0, 2, wgf_color_make(110, 150, 220, 255));
    wgf_draw_rectangle_rounded(vw - 104, 100, 88, 430, 24, 0, wgf_color_make(16, 20, 28, 170));
    wgf_draw_text(0, "libwgf picking: click a model or a bunny", 28, 24, 20, WGF_COLOR_WHITE);
    if (under != 0) {
        int k;
        const char *name = "something";
        for (k = 0; k <= SHAPES; k++) {
            if (g.models[k] == under) name = names[k];
        }
        snprintf(line, sizeof(line), "under the pointer: the %s, %.2f away, at %.2f, %.2f, %.2f", name, hit[7], hit[4],
                 hit[5], hit[6]);
    } else {
        snprintf(line, sizeof(line), "under the pointer: %s", hovered != 0 ? "a bunny or a button" : "nothing");
    }
    wgf_draw_text(0, line, 28, 54, 16, wgf_color_make(200, 210, 230, 255));
    snprintf(line, sizeof(line), "clicks: %d   drag the background to turn%s", g.clicks,
             wgf_app_can_quit() ? "   ESC: quit" : "");
    wgf_draw_text(0, line, 28, 78, 16, wgf_color_make(150, 160, 180, 255));
    wgf_stage2d_draw(g.hud);
    wgf_draw_text(0, "select all", 54, vh - 54, 18, WGF_COLOR_WHITE);
    wgf_draw_text(0, "select none", 218, vh - 54, 18, WGF_COLOR_WHITE);
}

int main(void)
{
    wgf_window_set_title("libwgf picking");
    wgf_window_set_size(1000, 640);
    wgf_window_set_flags(WGF_WINDOW_FLAG_MSAA);
    wgf_window_set_resizable(true);
    wgf_debug_show_fps(0, 12, 116, 14.0f, wgf_color_make(0, 160, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
