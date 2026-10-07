#include <math.h>
#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_stage_priv.h"
#include "wgf_camera3d.h"
#include "wgf_light.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_stage.h"

/* Stages, lights, and models on sokol's dummy backend: their settings and refusals,
 * what a model holds, which models a stage's draw keeps (culled against the camera's
 * view, or not), and which lights reach which. Pixels: wgf_gfx_stage_web_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

/* One frame drawing `stage`: how many parts it kept. */
static int drawn(wgf_node_t stage)
{
    int count;
    wgf_gfx_priv_begin_frame();
    wgf_stage_draw(stage);
    count = wgf_gfx_priv_stage_get_item_count();
    wgf_gfx_priv_end_frame();
    return count;
}

int main(void)
{
    wgf_node_t stage, camera, model, behind, light, near_light, far_light, group;
    wgf_mesh_t cube;
    wgf_material_t own, gold;

    expect(wgf_gfx_priv_start(), "setup");
    wgf_platform_priv_set_size(640, 480);

    stage = wgf_stage_create();
    expect(wgf_node_get_type(stage) == WGF_NODE_TYPE_STAGE, "a stage is a node of its type");
    expect(wgf_stage_get_camera(stage) == 0 && wgf_stage_get_ambient_intensity(stage) == 0.0f &&
               wgf_stage_get_tonemap(stage) == WGF_STAGE_TONEMAP_NEUTRAL && wgf_stage_get_exposure(stage) == 0.0f &&
               wgf_stage_is_culling(stage),
           "its defaults: no camera, no ambient, neutral, culling");
    expect(wgf_stage_set_ambient(stage, WGF_COLOR_WHITE, -2.0f) && wgf_stage_get_ambient_intensity(stage) == 0.0f &&
               wgf_stage_set_ambient(stage, WGF_COLOR_BLUE, 0.25f) &&
               wgf_stage_get_ambient_color(stage) == WGF_COLOR_BLUE,
           "ambient: its intensity clamped");
    expect(!wgf_stage_set_tonemap(stage, (wgf_stage_tonemap_t)5, 0.0f) &&
               wgf_stage_set_tonemap(stage, WGF_STAGE_TONEMAP_ACES, 1.5f) && near(wgf_stage_get_exposure(stage), 1.5f),
           "tone mapping, and one that isn't refused");
    group = wgf_node_create();
    expect(!wgf_node_set_parent(stage, group), "a stage is always a root");
    camera = wgf_camera3d_create();
    expect(!wgf_stage_set_camera(stage, group) && wgf_stage_set_camera(stage, camera) &&
               wgf_stage_get_camera(stage) == camera,
           "its camera: a 3D camera, or refused");

    /* models */
    cube = wgf_mesh_create_cube(1, 1, 1);
    expect(wgf_model_create(group) == 0, "a model of something that isn't a mesh: none");
    model = wgf_model_create(cube);
    expect(model != 0 && wgf_model_get_mesh(model) == cube && wgf_model_get_tint(model) == WGF_COLOR_WHITE,
           "a model of the cube, white");
    expect(wgf_model_get_material(model, 0) == wgf_mesh_get_material(cube, 0), "drawn with the mesh's material");
    own = wgf_material_create(WGF_MATERIAL_SHADING_UNLIT);
    expect(wgf_model_set_material(model, 0, own) && wgf_model_get_material(model, 0) == own,
           "its own material in slot 0");
    expect(wgf_resource_release(own) && wgf_material_get_shading(own) == WGF_MATERIAL_SHADING_UNLIT,
           "the model holds a reference of its own");
    expect(!wgf_model_set_material(model, 32, own) && !wgf_model_set_material(model, 0, cube),
           "a slot out of range, or not a material: refused");
    gold = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    expect(wgf_model_set_material(model, -1, gold) && wgf_model_get_material(model, 31) == gold,
           "-1: every slot");
    expect(wgf_model_set_material(model, -1, 0) && wgf_model_get_material(model, 0) == wgf_mesh_get_material(cube, 0),
           "0: back to the mesh's");
    wgf_resource_release(gold);
    expect(wgf_resource_get_status(gold) == WGF_RESOURCE_STATUS_NONE, "a material no model holds is freed");
    wgf_node_set_parent(model, stage);

    /* the draw: in view, out of view, culling off */
    wgf_node_set_position(camera, 0, 0, 5);
    expect(drawn(stage) == 1, "a cube in front of the camera: drawn");
    behind = wgf_model_create(cube);
    wgf_node_set_parent(behind, stage);
    wgf_node_set_position(behind, 0, 0, 10);
    expect(drawn(stage) == 1, "one behind the camera: culled");
    wgf_stage_set_culling(stage, false);
    expect(drawn(stage) == 2, "culling off: drawn");
    wgf_stage_set_culling(stage, true);
    wgf_node_set_visible(model, false);
    expect(drawn(stage) == 0, "a hidden model: not drawn");
    wgf_node_set_visible(model, true);
    wgf_node_set_parent(model, group);
    wgf_node_set_parent(group, stage);
    wgf_node_set_enabled(group, false);
    expect(drawn(stage) == 0, "under a disabled node: not drawn");
    wgf_node_set_enabled(group, true);
    expect(drawn(stage) == 1, "enabled again");
    wgf_stage_set_camera(stage, 0);
    expect(drawn(stage) == 0, "no camera: nothing");
    wgf_stage_set_camera(stage, camera);

    /* lights: a directional reaches everything; a point light only within its range */
    light = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    expect(wgf_light_create((wgf_light_type_t)3) == 0, "a light type that isn't one: none");
    expect(wgf_light_get_type(light) == WGF_LIGHT_TYPE_DIRECTIONAL && wgf_light_get_color(light) == WGF_COLOR_WHITE &&
               near(wgf_light_get_intensity(light), 1.0f) && wgf_light_get_range(light) == 0.0f &&
               near(wgf_light_get_spot_outer_angle(light), 3.14159265f / 4.0f),
           "a light's defaults");
    expect(wgf_light_set_intensity(light, -1.0f) && wgf_light_get_intensity(light) == 0.0f &&
               wgf_light_set_spot_cone(light, 2.0f, 9.0f) && near(wgf_light_get_spot_outer_angle(light), 1.5707963f) &&
               near(wgf_light_get_spot_inner_angle(light), 1.5707963f),
           "intensity and cone clamped");
    wgf_light_set_intensity(light, 3.0f);
    wgf_node_set_parent(light, stage);
    near_light = wgf_light_create(WGF_LIGHT_TYPE_POINT);
    wgf_light_set_range(near_light, 2.0f);
    wgf_node_set_parent(near_light, stage);
    wgf_node_set_position(near_light, 0, 1.5f, 0);
    far_light = wgf_light_create(WGF_LIGHT_TYPE_POINT);
    wgf_light_set_range(far_light, 2.0f);
    wgf_node_set_parent(far_light, stage);
    wgf_node_set_position(far_light, 50, 0, 0);
    wgf_model_set_material(model, 0, 0);
    wgf_gfx_priv_begin_frame();
    wgf_stage_draw(stage);
    expect(wgf_gfx_priv_stage_get_item_lights(0) == 2, "the sun and the lamp near it light it; the far lamp doesn't");
    wgf_gfx_priv_end_frame();
    wgf_node_set_visible(light, false);
    wgf_gfx_priv_begin_frame();
    wgf_stage_draw(stage);
    expect(wgf_gfx_priv_stage_get_item_lights(0) == 1, "a hidden light shines on nothing");
    wgf_gfx_priv_end_frame();

    /* destroyed: its camera gone, the stage has none; a model lets go of its mesh */
    wgf_node_destroy(camera, WGF_NODE_DESTROY_CHILDREN);
    expect(wgf_stage_get_camera(stage) == 0, "a destroyed camera leaves the stage with none");
    wgf_node_destroy(stage, WGF_NODE_DESTROY_CHILDREN);
    expect(wgf_resource_release(cube) && wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_NONE,
           "the models gone, the mesh's last reference was the caller's");

    wgf_gfx_priv_stop();
    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
