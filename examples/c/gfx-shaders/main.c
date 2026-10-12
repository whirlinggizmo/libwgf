#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_environment.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_resource.h"
#include "wgf_render.h"
#include "wgf_shader.h"
#include "wgf_shape3d.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* Materials drawn by shaders of your own (wgf_shader.h).
 *
 *   - left: toon shading (lights in flat bands, a rim light) on the toy car
 *   - middle: a sphere dissolving and coming back through a noise texture, with a
 *     glowing edge (time, a texture, discard)
 *   - right: a sphere of water rippling in waves (a vertex hook moves the surface)
 *     reflecting the stage's environment
 *
 * The shaders are the .glsl files in examples/shaders, packed for every graphics
 * backend by tools/pack_shader.py into the .wgfshader files in examples/assets/shaders
 * (tools/gen_shaders.py). Each loads as any file does, and its material is made at
 * once: the values set on it are kept until the shader has loaded. A sun, a point light
 * circling in front, and an environment (a sunset, lighting only) light the stage.
 *
 *   1 / 2    the sun / the point light on and off
 *   Esc      quit, where quitting means anything
 *
 * libwgt's gfx-shaders (wgrender's shaders), which differs for the size table: libwgf
 * has no skinning or animation yet, so the toon shading is on the toy car in place of
 * the walking character; and no custom sprite materials yet (milestone 2.5), so the
 * logo outlined and flashing by its sprite effects shader, in the world and in the
 * screen's corner, is left out. */

#define CAR_PATH "models/toy_car.glb"
#define NOISE_PATH "textures/noise.png"
#define ENVIRONMENT_PATH "environments/venice_sunset_1k.hdr"
#define FLOOR_Y -0.3f
#define SPHERE_Y (FLOOR_Y + 0.5f) /* spheres 1 m across, resting on the floor */

static struct {
    wgf_actor_t stage, camera;
    wgf_actor_t car, dissolving, rippling; /* models */
    wgf_actor_t sun, lamp, lamp_marker;
    wgf_material_t toon; /* the car's, once its file's tree is built */
    bool sun_on, lamp_on;
    float time;
} g = {.sun_on = true, .lamp_on = true};

/* A material drawn by the shader in examples/assets/shaders/<name>.wgfshader. */
static wgf_material_t custom(const char *path)
{
    const wgf_shader_t shader = wgf_shader_create(path);
    const wgf_material_t material = wgf_material_create_custom(shader);
    wgf_resource_release(shader); /* the material holds its own reference */
    return material;
}

/* `material` in every slot of every model under `actor`: a file's models are its tree's
 * actors, made once it has loaded, each with its own slots. */
static void set_below(wgf_actor_t actor, wgf_material_t material)
{
    int i;
    for (i = 0; i < wgf_actor_get_child_count(actor); i++) {
        const wgf_actor_t child = wgf_actor_get_child(actor, i);
        if (wgf_actor_get_kind(child) == WGF_ACTOR_KIND_MODEL) wgf_model_set_material(child, -1, material);
        set_below(child, material);
    }
}

/* A model of `mesh` at (x, y, z), on the stage. */
static wgf_actor_t place(wgf_mesh_t mesh, float x, float y, float z)
{
    const wgf_actor_t model = wgf_model_create(mesh);
    wgf_resource_release(mesh); /* the model holds it */
    wgf_actor_set_position(model, x, y, z);
    wgf_actor_set_parent(model, g.stage);
    return model;
}

static void init(void *user)
{
    wgf_material_t material;
    (void)user;
    wgf_asset_set_host("../assets"); /* the examples' files: one level above every program */
    wgf_render_set_clear_color(wgf_color_make(20, 22, 28, 255));
    g.stage = wgf_stage3d_create();
    g.camera = wgf_camera3d_create();
    wgf_actor_set_position(g.camera, 0, 1.2f, 5.0f);
    wgf_actor_look_at(g.camera, 0, 0.3f, 0, 0, 1, 0);
    wgf_stage3d_set_camera(g.stage, g.camera);
    wgf_stage3d_set_ambient(g.stage, WGF_COLOR_WHITE, 0.15f);
    {
        const wgf_environment_t environment = wgf_environment_create(ENVIRONMENT_PATH);
        wgf_stage3d_set_environment(g.stage, environment, 1.0f, 0.0f); /* lighting only: the background stays dark */
        wgf_resource_release(environment); /* the stage holds its own reference */
    }

    g.sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_light_set_color(g.sun, wgf_color_make(255, 244, 228, 255));
    wgf_light_set_intensity(g.sun, 1.5f); /* soft: the point light and the environment show too */
    wgf_actor_look_at(g.sun, -0.4f, -0.7f, -0.6f, 0, 1, 0); /* from the origin, shining that way */
    wgf_actor_set_parent(g.sun, g.stage);

    g.lamp = wgf_light_create(WGF_LIGHT_TYPE_POINT);
    wgf_light_set_color(g.lamp, wgf_color_make(120, 190, 255, 255));
    wgf_light_set_intensity(g.lamp, 9.0f); /* falls off with distance squared: ~2.3 at 2 m */
    wgf_light_set_range(g.lamp, 8.0f);
    wgf_actor_set_parent(g.lamp, g.stage);
    g.lamp_marker = wgf_shape3d_create();
    wgf_shape3d_set_sphere(g.lamp_marker, 0.05f);
    wgf_shape3d_set_color(g.lamp_marker, WGF_COLOR_SKYBLUE);
    wgf_actor_set_parent(g.lamp_marker, g.lamp);

    /* a floor, so the models stand somewhere */
    material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(material, "base_color", 0.04f, 0.04f, 0.045f, 1.0f); /* dark: the lights show on it */
    wgf_material_set_float(material, "metallic", 0.0f);
    wgf_material_set_float(material, "roughness", 0.8f);
    wgf_model_set_material(place(wgf_mesh_create_plane(6.0f, 6.0f, 0), 0, FLOOR_Y, 0), -1, material);
    wgf_resource_release(material); /* the model holds its own reference */

    /* toon shading on the car, every part of it */
    g.car = place(wgf_mesh_create(CAR_PATH), -1.9f, FLOOR_Y, 0);
    wgf_actor_set_rotation(g.car, 0, 0.4f, 0);
    wgf_actor_set_scale(g.car, 0.5f, 0.5f, 0.5f); /* wheels on the floor at its origin */
    material = custom("shaders/toon.wgfshader");
    wgf_material_set_color(material, "color", wgf_color_make(255, 196, 120, 255));
    wgf_material_set_float(material, "bands", 3.0f);
    wgf_material_set_float(material, "rim", 0.35f);
    g.toon = material; /* given to the car's models once the file has loaded and its tree is built */

    /* dissolving through a noise texture */
    g.dissolving = place(wgf_mesh_create_sphere(0.5f, 32, 64), 0.0f, SPHERE_Y, 0);
    material = custom("shaders/dissolve.wgfshader");
    wgf_material_set_vec4(material, "color", 0.55f, 0.6f, 0.7f, 1.0f); /* linear */
    wgf_material_set_vec3(material, "edge_color", 4.0f, 1.2f, 0.2f);
    wgf_material_set_float(material, "speed", 0.15f);
    wgf_material_set_double_sided(material, true); /* the inside shows through the holes */
    {
        const wgf_texture_t noise = wgf_texture_create(NOISE_PATH); /* white until it loads */
        wgf_material_set_texture(material, "noise_tex", noise);
        wgf_resource_release(noise); /* the material holds its own reference */
    }
    wgf_model_set_material(g.dissolving, 0, material);
    wgf_resource_release(material);

    /* water, finely divided so its waves are smooth */
    g.rippling = place(wgf_mesh_create_sphere(0.5f, 96, 192), 1.9f, SPHERE_Y, 0);
    material = custom("shaders/wave.wgfshader");
    wgf_material_set_float(material, "amplitude", 0.03f);
    wgf_material_set_float(material, "frequency", 2.5f);
    wgf_material_set_float(material, "wave_speed", 3.0f);
    wgf_material_set_vec4(material, "low_color", 0.0f, 0.03f, 0.1f, 1.0f); /* deep water */
    wgf_material_set_vec4(material, "high_color", 0.05f, 0.3f, 0.35f, 1.0f);
    wgf_material_set_float(material, "roughness", 0.05f);
    wgf_material_set_float(material, "reflectivity", 0.35f); /* real water is 0.02: more, so it shows */
    wgf_model_set_material(g.rippling, 0, material);
    wgf_resource_release(material);
}

static void frame(void *user)
{
    const float dt = wgf_loop_get_frame_delta();
    float lx, ly, lz;
    (void)user;

    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_1)) wgf_actor_set_enabled(g.sun, g.sun_on = !g.sun_on);
    if (wgf_keyboard_is_pressed(WGF_KEY_2)) wgf_actor_set_enabled(g.lamp, g.lamp_on = !g.lamp_on);

    g.time += dt;
    /* circling in front of the models, facing the camera: always in view, and 1.8 m or
       more from them (closer, it would wash them out) */
    lx = cosf(g.time * 0.7f) * 2.2f;
    ly = 0.8f + sinf(g.time * 0.7f) * 1.0f;
    lz = 1.8f;
    wgf_actor_set_position(g.lamp, lx, ly, lz);
    wgf_actor_set_rotation(g.dissolving, 0, g.time * 0.4f, 0);
    if (g.toon != 0 && wgf_actor_get_child_count(g.car) > 0) {
        set_below(g.car, g.toon);
        wgf_resource_release(g.toon); /* its models hold their own references */
        g.toon = 0;
    }

    wgf_stage3d_draw(g.stage);
    wgf_draw_text(0, "libwgf custom shaders: toon, dissolve, water", 12, 36, 20, WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, wgf_app_can_quit() ? "1 sun, 2 point light, Esc quit" : "1 sun, 2 point light", 12, 64, 16,
                  WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf shaders");
    wgf_window_set_size(960, 540);
    wgf_window_set_msaa(true);
    wgf_debug_show_fps(0, 12, 10, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
