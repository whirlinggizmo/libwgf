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
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Drawing into textures (wgf_texture_create_target, wgf_render_begin_target):
 *
 *   - "pixel view": the stage drawn into a 160x100 texture and shown 4x larger with
 *     nearest filtering, a low-resolution pixel-art look
 *   - "minimap": the same stage from a top-down orthographic camera
 *     (wgf_stage3d_draw_with), in a 256x256 texture
 *   - "label": text in two fonts drawn into a 256x128 texture, used as the base color
 *     texture of the spinning sphere's material, and shown on its own
 *
 * Each frame draws the label first, so the views that use it show this frame's label.
 * Escape quits, where quitting means anything.
 *
 * libwgt's render target example (wgrender's), but for the walker: libwgt's is a skinned
 * character, playing its animation, and skinning is milestone 3's, so the toy car drives
 * the circle in its place. */

#define CAR_PATH "models/toy_car.glb"
#define FONT_PATH "fonts/Komika/KOMIKAH_.ttf"

enum { PIXEL_W = 160, PIXEL_H = 100, PIXEL_SCALE = 4, MINIMAP = 256, LABEL_W = 256, LABEL_H = 128 };

static const wgf_color_t BACKGROUND = 0x181A22FFu;
static const wgf_color_t FRAME_COLOR = 0x5A606EFFu;

static struct {
    wgf_actor_t stage, camera, top_camera;
    wgf_texture_t pixel_view, minimap, label; /* render targets */
    wgf_font_t font;
    wgf_actor_t car, globe;
    float time;
} g;

/* A model of `mesh` at (x, y, z), scaled, with `material` (0: the mesh's), on the stage. */
static wgf_actor_t place(wgf_mesh_t mesh, float x, float y, float z, float scale, float scale_y,
                         wgf_material_t material)
{
    const wgf_actor_t model = wgf_model_create(mesh);
    wgf_resource_release(mesh); /* the model holds it */
    wgf_actor_set_position(model, x, y, z);
    wgf_actor_set_scale(model, scale, scale_y, scale);
    if (material != 0) {
        wgf_model_set_material(model, -1, material);
        wgf_resource_release(material); /* and its material */
    }
    wgf_actor_set_parent(model, g.stage);
    return model;
}

static void init(void *user)
{
    wgf_material_t material;
    wgf_actor_t sun;
    wgf_mesh_t mesh;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(BACKGROUND);

    g.pixel_view = wgf_texture_create_target(PIXEL_W, PIXEL_H);
    wgf_texture_set_sampling(g.pixel_view, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_FILTER_NEAREST);
    wgf_render_set_target_clear_color(g.pixel_view, BACKGROUND);
    g.minimap = wgf_texture_create_target(MINIMAP, MINIMAP);
    wgf_render_set_target_clear_color(g.minimap, wgf_color_make(12, 14, 18, 255));
    g.label = wgf_texture_create_target(LABEL_W, LABEL_H);
    wgf_render_set_target_clear_color(g.label, wgf_color_make(30, 60, 140, 255));

    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_actor_set_position(g.camera, 0, 6.0f, 10.0f);
    wgf_actor_look_at(g.camera, 0, 0.8f, 0, 0, 1, 0);
    wgf_stage3d_set_camera(g.stage, g.camera);
    g.top_camera = wgf_camera3d_create();
    wgf_camera3d_set_orthographic(g.top_camera, true);
    wgf_camera3d_set_ortho_height(g.top_camera, 9.0f);
    wgf_actor_set_position(g.top_camera, 0, 12, 0);
    wgf_actor_look_at(g.top_camera, 0, 0, 0, 0, 0, -1); /* looking down, -z up the map */

    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_light_set_intensity(sun, 3.0f);
    wgf_actor_look_at(sun, -0.5f, -1.0f, -0.4f, 0, 1, 0); /* from the origin, shining that way */
    wgf_actor_set_parent(sun, g.stage);
    wgf_stage3d_set_ambient(g.stage, WGF_COLOR_WHITE, 0.25f);

    /* ground: a flattened sphere */
    material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(material, "base_color", 0.25f, 0.3f, 0.25f, 1.0f);
    wgf_material_set_float(material, "metallic", 0.0f);
    place(wgf_mesh_create_sphere(0.5f, 32, 64), 0, -0.05f, 0, 8.0f, 0.1f, material);

    mesh = wgf_mesh_create(CAR_PATH);
    g.car = wgf_model_create(mesh);
    wgf_resource_release(mesh);
    wgf_actor_set_parent(g.car, g.stage);

    /* the globe wears the label: drawn into each frame, used as any texture */
    material = wgf_material_create(WGF_MATERIAL_SHADING_UNLIT);
    wgf_material_set_texture(material, "base_color_texture", g.label);
    wgf_material_set_vec2(material, "base_color_texture_scale", 2.0f, 1.0f); /* twice around */
    wgf_material_set_texture_sampling(material, "base_color_texture", WGF_TEXTURE_WRAP_REPEAT, WGF_TEXTURE_WRAP_CLAMP,
                                      WGF_TEXTURE_FILTER_LINEAR);
    g.globe = place(wgf_mesh_create_sphere(0.5f, 32, 64), -2.2f, 1.2f, 0, 1.6f, 1.6f, material);

    g.font = wgf_font_create(FONT_PATH); /* the built-in font until it has loaded */
}

/* A texture with a 2 pixel frame and a caption above it. */
static void draw_panel(wgf_texture_t texture, float x, float y, float w, float h, const char *caption)
{
    wgf_draw_rectangle(x - 2, y - 2, w + 4, h + 4, FRAME_COLOR);
    wgf_draw_texture(texture, x, y, w, h, WGF_COLOR_WHITE);
    wgf_draw_text(0, caption, x, y - 20, 16, WGF_COLOR_LIGHTGRAY);
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    const float gx = cosf(g.time * 0.6f) * 2.5f, gz = sinf(g.time * 0.6f) * 2.5f;
    char line[64];
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    g.time += dt;
    wgf_actor_set_position(g.car, gx, 0, gz); /* drives in a circle */
    wgf_actor_set_rotation(g.car, 0, -g.time * 0.6f, 0);
    wgf_actor_set_scale(g.car, 0.6f, 0.6f, 0.6f);
    wgf_actor_set_rotation(g.globe, 0, g.time * 0.8f, 0);

    /* 1. the label, first, so the views below use this frame's text */
    if (wgf_render_begin_target(g.label)) {
        wgf_draw_text(g.font, "libwgf", 20, 14, 64, WGF_COLOR_RAYWHITE);
        snprintf(line, sizeof(line), "t = %.1f", (double)g.time);
        wgf_draw_text(0, line, 24, 92, 16, WGF_COLOR_GOLD);
        wgf_render_end_target();
    }

    /* 2. a low-resolution view of the stage */
    if (wgf_render_begin_target(g.pixel_view)) {
        wgf_stage3d_draw(g.stage);
        wgf_render_end_target();
    }

    /* 3. the minimap, from above */
    if (wgf_render_begin_target(g.minimap)) {
        wgf_stage3d_draw_with(g.stage, g.top_camera);
        wgf_render_end_target();
    }

    /* the screen */
    draw_panel(g.pixel_view, 24, 92, PIXEL_W * PIXEL_SCALE, PIXEL_H * PIXEL_SCALE, "pixel view (160x100, nearest)");
    draw_panel(g.minimap, 700, 92, MINIMAP, MINIMAP, "minimap (orthographic, from above)");
    draw_panel(g.label, 700, 400, LABEL_W, LABEL_H, "label (text drawn into a texture)");
    wgf_draw_text(0, "libwgf render targets: wgf_texture_create_target + wgf_render_begin_target", 12, 36, 16,
                  WGF_COLOR_RAYWHITE);
}

int main(void)
{
    wgf_window_set_title("libwgf render targets");
    wgf_window_set_size(1000, 560);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
