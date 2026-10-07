#include <math.h>
#include <stddef.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_light.h"
#include "wgf_loop.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_node.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shape3d.h"
#include "wgf_stage.h"
#include "wgf_texture.h"
#include "wgf_window.h"

/* glTF metallic-roughness materials made in code:
 *   - top row: dielectric (metallic 0) spheres, roughness 0 to 1 left to right
 *   - middle row: metal (metallic 1) spheres, the same roughness steps
 *   - bottom row: unlit, emissive, normal mapped, and alpha blended, turning so the
 *     normal map moves
 * One sphere mesh backs every sphere; each model draws it with a material of its own.
 * A sun, an orbiting point light (a small unlit sphere marks it), and a little ambient
 * light the stage. Keys: 1 the sun, 2 the point light, Escape quits where quitting
 * means anything.
 *
 * wgrender-c's materials example, which differs for the size table: its spheres are a
 * glTF file's and its fifth bottom model an animated character with its body gilded,
 * which libwgf can't load until glTF (milestone 2, step 6). Here the sphere is
 * generated (wgf_mesh_create_sphere, 32 rings and 64 segments, the file's), and the
 * character's place holds a gold torus. And:
 *   - a light is switched with wgf_node_set_visible, libwgf's lights shining while
 *     visible, where wgrender's have an enabled flag of their own;
 *   - the sun is aimed with wgf_node_look_at, and the lamp and its marker moved as nodes;
 *   - the status line is two draws, no snprintf, so the program links no formatting. */

#define NORMAL_MAP_PATH "textures/tiles_normal.png"

enum { COLUMNS = 5, SPHERE_COUNT = 2 * COLUMNS + 4 };

static struct {
    wgf_node_t stage, camera, sun, lamp, lamp_marker, torus;
    wgf_node_t spheres[SPHERE_COUNT];
    float time;
} g;

/* A sphere at (x, y) drawn with `material`; the model keeps its own reference. */
static wgf_node_t create_sphere(wgf_mesh_t mesh, float x, float y, wgf_material_t material)
{
    const wgf_node_t model = wgf_model_create(mesh);
    wgf_node_set_position(model, x, y, 0);
    wgf_model_set_material(model, 0, material);
    wgf_resource_release(material);
    wgf_node_set_parent(model, g.stage);
    return model;
}

static wgf_material_t create_pbr(float r, float gr, float b, float metallic, float roughness)
{
    const wgf_material_t material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    wgf_material_set_vec4(material, "base_color", r, gr, b, 1.0f); /* linear */
    wgf_material_set_float(material, "metallic", metallic);
    wgf_material_set_float(material, "roughness", roughness);
    return material;
}

static void init(void *user)
{
    const float spacing = 1.35f;
    const wgf_mesh_t sphere = wgf_mesh_create_sphere(0.5f, 32, 64);
    wgf_material_t material, tiles;
    wgf_texture_t normal_map;
    int n = 0, c;

    (void)user;
    wgf_asset_set_host("../assets");
    wgf_render_set_clear_color(wgf_color_make(20, 22, 28, 255));

    g.camera = wgf_camera3d_create();
    wgf_node_set_position(g.camera, 0, 1.6f, 7.5f);
    wgf_node_look_at(g.camera, 0, 1.2f, 0, 0, 1, 0);
    g.stage = wgf_stage_create();
    wgf_stage_set_camera(g.stage, g.camera);
    wgf_stage_set_ambient(g.stage, WGF_COLOR_WHITE, 0.12f);

    g.sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_node_look_at(g.sun, -0.4f, -0.7f, -0.6f, 0, 1, 0);
    wgf_light_set_color(g.sun, wgf_color_make(255, 244, 228, 255));
    wgf_light_set_intensity(g.sun, 3.0f);
    wgf_node_set_parent(g.sun, g.stage);

    g.lamp = wgf_light_create(WGF_LIGHT_TYPE_POINT);
    wgf_light_set_color(g.lamp, wgf_color_make(120, 190, 255, 255));
    wgf_light_set_intensity(g.lamp, 8.0f);
    wgf_light_set_range(g.lamp, 10.0f);
    wgf_node_set_parent(g.lamp, g.stage);
    g.lamp_marker = wgf_shape3d_create(); /* shapes are unlit, so it shows the light's color */
    wgf_shape3d_set_sphere(g.lamp_marker, 0.06f);
    wgf_shape3d_set_color(g.lamp_marker, WGF_COLOR_SKYBLUE);
    wgf_node_set_parent(g.lamp_marker, g.lamp);

    /* rows of roughness steps: red plastic, then gold */
    for (c = 0; c < COLUMNS; c++) {
        const float x = ((float)c - (COLUMNS - 1) * 0.5f) * spacing;
        const float roughness = (float)c / (COLUMNS - 1);
        g.spheres[n++] = create_sphere(sphere, x, 2.7f, create_pbr(0.8f, 0.05f, 0.04f, 0.0f, roughness));
        g.spheres[n++] = create_sphere(sphere, x, 1.35f, create_pbr(1.0f, 0.77f, 0.34f, 1.0f, roughness));
    }

    /* unlit: ignores the lights */
    material = wgf_material_create(WGF_MATERIAL_SHADING_UNLIT);
    wgf_material_set_color(material, "base_color", WGF_COLOR_SKYBLUE);
    g.spheres[n++] = create_sphere(sphere, -2 * spacing, 0.0f, material);

    /* emissive: glows whatever the lighting */
    material = create_pbr(0.05f, 0.05f, 0.05f, 0.0f, 0.6f);
    wgf_material_set_vec3(material, "emissive", 1.0f, 0.35f, 0.05f);
    g.spheres[n++] = create_sphere(sphere, -spacing, 0.0f, material);

    /* normal mapped: bevelled tiles */
    tiles = create_pbr(0.6f, 0.6f, 0.62f, 0.0f, 0.45f);
    wgf_material_set_float(tiles, "normal_scale", 1.0f);
    normal_map = wgf_texture_create(NORMAL_MAP_PATH); /* a flat normal until it loads */
    wgf_material_set_texture(tiles, "normal_texture", normal_map);
    wgf_resource_release(normal_map); /* the material holds its own reference */
    g.spheres[n++] = create_sphere(sphere, 0.0f, 0.0f, tiles);

    /* alpha blended glass */
    material = create_pbr(0.3f, 0.9f, 0.5f, 0.0f, 0.1f);
    wgf_material_set_vec4(material, "base_color", 0.3f, 0.9f, 0.5f, 0.35f);
    wgf_material_set_alpha_mode(material, WGF_ALPHA_MODE_BLEND, 0.5f);
    g.spheres[n++] = create_sphere(sphere, spacing, 0.0f, material);
    wgf_resource_release(sphere); /* the models hold their own references */

    /* the character's place: a gold torus */
    {
        const wgf_mesh_t ring = wgf_mesh_create_torus(0.35f, 0.14f, 48, 24);
        g.torus = wgf_model_create(ring);
        wgf_resource_release(ring);
        wgf_node_set_position(g.torus, 2 * spacing, 0.0f, 0);
        wgf_model_set_material(g.torus, 0, create_pbr(1.0f, 0.77f, 0.34f, 1.0f, 0.3f));
        wgf_resource_release(wgf_model_get_material(g.torus, 0));
        wgf_node_set_parent(g.torus, g.stage);
    }
}

static void frame(void *user)
{
    int i;
    (void)user;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();
    if (wgf_keyboard_is_pressed(WGF_KEY_1)) wgf_node_set_visible(g.sun, !wgf_node_is_visible(g.sun));
    if (wgf_keyboard_is_pressed(WGF_KEY_2)) wgf_node_set_visible(g.lamp, !wgf_node_is_visible(g.lamp));

    g.time += wgf_loop_get_frame_delta();
    wgf_node_set_position(g.lamp, cosf(g.time * 0.7f) * 4.0f, 1.4f + sinf(g.time * 0.9f) * 1.2f,
                          sinf(g.time * 0.7f) * 1.5f + 2.0f);
    wgf_node_set_visible(g.lamp_marker, wgf_node_is_visible(g.lamp));
    for (i = 2 * COLUMNS; i < SPHERE_COUNT; i++) { /* turn the bottom row so the normal map moves */
        wgf_node_set_rotation(g.spheres[i], 0, g.time * 0.5f, 0);
    }
    wgf_node_set_rotation(g.torus, 0.6f, g.time * 0.5f, 0);

    wgf_stage_draw(g.stage);
    wgf_draw_text(0, "libwgf materials: metallic-roughness, unlit, emissive, normal map, blend", 12, 12, 16,
                  WGF_COLOR_RAYWHITE);
    wgf_draw_text(0, "roughness 0 -> 1 (left to right)   rows: plastic, gold", 12, 36, 16, WGF_COLOR_LIGHTGRAY);
    wgf_draw_text(0, wgf_node_is_visible(g.sun) ? "[1] sun on" : "[1] sun off", 480, 36, 16, WGF_COLOR_LIGHTGRAY);
    wgf_draw_text(0, wgf_node_is_visible(g.lamp) ? "[2] lamp on" : "[2] lamp off", 600, 36, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf materials");
    wgf_window_set_size(1000, 700);
    wgf_window_set_msaa(true);
    wgf_window_set_resizable(true);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
