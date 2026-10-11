#include <math.h>
#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "wgf_camera3d.h"
#include "wgf_light.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"

/* Stages, lights, and models on sokol's dummy backend: their settings and refusals,
 * what a model holds, which models a stage's draw keeps (culled against the camera's
 * view, or not), which lights reach which, and which parts go up as one draw (libwgt's
 * instancing). Pixels: wgf_gfx_stage3d_web_test. */

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
static int drawn(wgf_actor_t stage)
{
    int count;
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    count = wgf_gfx_priv_stage3d_get_item_count();
    wgf_gfx_priv_end_frame();
    return count;
}

/* One frame drawing `stage`: how many parts it kept; `draws`, the draws they took. */
static int drawn_in(wgf_actor_t stage, int *draws)
{
    const int count = drawn(stage);
    *draws = wgf_gfx_priv_stage3d_get_draw_calls();
    return count;
}

/* Models that agree on everything but where they stand, and their tint, go up as one draw;
 * libwgt's test of it. */
static void test_instancing(void)
{
    const wgf_actor_t stage = wgf_stage3d_create(), camera = wgf_camera3d_create();
    const wgf_mesh_t cube = wgf_mesh_create_cube(0.2f, 0.2f, 0.2f);
    wgf_actor_t cubes[50], glass[3];
    wgf_material_t materials[50];
    int i, draws;
    wgf_actor_set_position(camera, 0, 0, 20);
    wgf_actor_set_parent(camera, stage);
    wgf_stage3d_set_camera(stage, camera);
    for (i = 0; i < 50; i++) {
        cubes[i] = wgf_model_create(cube);
        wgf_actor_set_position(cubes[i], (float)(i % 10) - 4.5f, (float)(i / 10) - 2.0f, 0);
        wgf_model_set_tint(cubes[i], wgf_color_make((unsigned char)(i * 5), 100, 200, 255)); /* a tint each */
        wgf_actor_set_parent(cubes[i], stage);
    }
    expect(drawn_in(stage, &draws) == 50 && draws == 1, "50 cubes sharing a mesh and a material: one draw");
    for (i = 0; i < 50; i++) {
        materials[i] = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
        wgf_model_set_material(cubes[i], 0, materials[i]);
    }
    expect(drawn_in(stage, &draws) == 50 && draws == 50, "a material each: a draw each");
    for (i = 0; i < 50; i++) wgf_model_set_material(cubes[i], 0, i % 2 == 0 ? materials[0] : materials[1]);
    expect(drawn_in(stage, &draws) == 50 && draws == 2, "two materials, interleaved in the tree: sorted together");
    for (i = 0; i < 50; i++) {
        wgf_model_set_material(cubes[i], 0, 0);
        wgf_resource_release(materials[i]);
    }
    for (i = 0; i < 10; i++) wgf_actor_set_scale(cubes[i], -1, 1, 1);
    expect(drawn_in(stage, &draws) == 50 && draws == 2, "mirrored ones apart: two draws");
    for (i = 0; i < 10; i++) wgf_actor_set_scale(cubes[i], 1, 1, 1);
    for (i = 0; i < 10; i++) wgf_model_set_shadow_receiving(cubes[i], false);
    expect(drawn_in(stage, &draws) == 50 && draws == 2, "ones that receive no shadows apart");
    for (i = 0; i < 10; i++) wgf_model_set_shadow_receiving(cubes[i], true);
    { /* a point light that reaches only the left of them: two sets of lights */
        const wgf_actor_t light = wgf_light_create(WGF_LIGHT_TYPE_POINT);
        wgf_light_set_range(light, 2.0f);
        wgf_actor_set_position(light, -4.0f, 0, 0);
        wgf_actor_set_parent(light, stage);
        expect(drawn_in(stage, &draws) == 50 && draws == 2, "different lights: apart");
        wgf_actor_destroy(light, WGF_ACTOR_DESTROY_CHILDREN);
    }
    /* see-through ones keep their back-to-front order: alike neighbours share a draw, but
       two alike either side of one that differs aren't joined across it */
    {
        const wgf_material_t other = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
        for (i = 0; i < 3; i++) {
            glass[i] = wgf_model_create(cube);
            wgf_model_set_tint(glass[i], wgf_color_make(255, 255, 255, 100));
            wgf_actor_set_position(glass[i], 0, 4, (float)i); /* far to near */
            wgf_actor_set_parent(glass[i], stage);
        }
        expect(drawn_in(stage, &draws) == 53 && draws == 2, "three see-through alike, neighbours: one draw");
        wgf_model_set_material(glass[1], 0, other);
        wgf_resource_release(other);
        expect(drawn_in(stage, &draws) == 53 && draws == 4, "the middle one apart: the outer two not joined across it");
    }
    wgf_resource_release(cube);
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
}

int main(void)
{
    wgf_actor_t stage, camera, model, behind, light, near_light, far_light, group;
    wgf_mesh_t cube;
    wgf_material_t own, gold;

    expect(wgf_gfx_priv_start(), "setup");
    wgf_platform_priv_set_size(640, 480);

    stage = wgf_stage3d_create();
    expect(wgf_actor_get_kind(stage) == WGF_ACTOR_KIND_STAGE3D, "a stage is an actor of its type");
    expect(wgf_stage3d_get_camera(stage) == 0 && wgf_stage3d_get_ambient_intensity(stage) == 0.0f &&
               wgf_stage3d_get_tonemap(stage) == WGF_STAGE3D_TONEMAP_NEUTRAL && wgf_stage3d_get_exposure(stage) == 0.0f &&
               wgf_stage3d_is_culling(stage),
           "its defaults: no camera, no ambient, neutral, culling");
    expect(wgf_stage3d_set_ambient(stage, WGF_COLOR_WHITE, -2.0f) && wgf_stage3d_get_ambient_intensity(stage) == 0.0f &&
               wgf_stage3d_set_ambient(stage, WGF_COLOR_BLUE, 0.25f) &&
               wgf_stage3d_get_ambient_color(stage) == WGF_COLOR_BLUE,
           "ambient: its intensity clamped");
    expect(!wgf_stage3d_set_tonemap(stage, (wgf_stage3d_tonemap_t)5, 0.0f) &&
               wgf_stage3d_set_tonemap(stage, WGF_STAGE3D_TONEMAP_ACES, 1.5f) && near(wgf_stage3d_get_exposure(stage), 1.5f),
           "tone mapping, and one that isn't refused");
    group = wgf_actor_create();
    expect(!wgf_actor_set_parent(stage, group), "a stage is always a root");
    camera = wgf_camera3d_create();
    expect(!wgf_stage3d_set_camera(stage, group) && wgf_stage3d_set_camera(stage, camera) &&
               wgf_stage3d_get_camera(stage) == camera,
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
    wgf_actor_set_parent(model, stage);

    /* the draw: in view, out of view, culling off */
    wgf_actor_set_position(camera, 0, 0, 5);
    expect(drawn(stage) == 1, "a cube in front of the camera: drawn");
    behind = wgf_model_create(cube);
    wgf_actor_set_parent(behind, stage);
    wgf_actor_set_position(behind, 0, 0, 10);
    expect(drawn(stage) == 1, "one behind the camera: culled");
    wgf_stage3d_set_culling(stage, false);
    expect(drawn(stage) == 2, "culling off: drawn");
    wgf_stage3d_set_culling(stage, true);
    wgf_actor_set_visible(model, false);
    expect(drawn(stage) == 0, "a hidden model: not drawn");
    wgf_actor_set_visible(model, true);
    wgf_actor_set_parent(model, group);
    wgf_actor_set_parent(group, stage);
    wgf_actor_set_enabled(group, false);
    expect(drawn(stage) == 0, "under a disabled actor: not drawn");
    wgf_actor_set_enabled(group, true);
    expect(drawn(stage) == 1, "enabled again");
    wgf_stage3d_set_camera(stage, 0);
    expect(drawn(stage) == 0, "no camera: nothing");
    wgf_stage3d_set_camera(stage, camera);

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
    wgf_actor_set_parent(light, stage);
    near_light = wgf_light_create(WGF_LIGHT_TYPE_POINT);
    wgf_light_set_range(near_light, 2.0f);
    wgf_actor_set_parent(near_light, stage);
    wgf_actor_set_position(near_light, 0, 1.5f, 0);
    far_light = wgf_light_create(WGF_LIGHT_TYPE_POINT);
    wgf_light_set_range(far_light, 2.0f);
    wgf_actor_set_parent(far_light, stage);
    wgf_actor_set_position(far_light, 50, 0, 0);
    wgf_model_set_material(model, 0, 0);
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    expect(wgf_gfx_priv_stage3d_get_item_lights(0) == 2, "the sun and the lamp near it light it; the far lamp doesn't");
    wgf_gfx_priv_end_frame();
    wgf_actor_set_visible(light, false);
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    expect(wgf_gfx_priv_stage3d_get_item_lights(0) == 1, "a hidden light shines on nothing");
    wgf_gfx_priv_end_frame();

    /* a spot facing away from the model's center: it reaches a model only as large as to be
       in its cone (a floor ahead of a headlight) */
    {
        const wgf_actor_t spot = wgf_light_create(WGF_LIGHT_TYPE_SPOT);
        wgf_actor_set_visible(near_light, false);
        wgf_light_set_range(spot, 10.0f);
        wgf_actor_set_parent(spot, stage);
        wgf_actor_set_position(spot, 0, 1, 3);
        wgf_actor_look_at(spot, 0, 0, 8, 0, 1, 0); /* ahead, a little down */
        wgf_gfx_priv_begin_frame();
        wgf_stage3d_draw(stage);
        expect(wgf_gfx_priv_stage3d_get_item_lights(0) == 0, "a spot facing away from a small model misses it");
        wgf_gfx_priv_end_frame();
        wgf_actor_set_scale(model, 20, 0.1f, 20); /* a floor under the spot, its center behind it */
        wgf_gfx_priv_begin_frame();
        wgf_stage3d_draw(stage);
        expect(wgf_gfx_priv_stage3d_get_item_lights(0) == 1, "and reaches one its cone falls on past its center");
        wgf_gfx_priv_end_frame();
        wgf_actor_set_scale(model, 1, 1, 1);
    }

    /* destroyed: its camera gone, the stage has none; a model lets go of its mesh */
    wgf_actor_destroy(camera, WGF_ACTOR_DESTROY_CHILDREN);
    expect(wgf_stage3d_get_camera(stage) == 0, "a destroyed camera leaves the stage with none");
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    expect(wgf_resource_release(cube) && wgf_resource_get_status(cube) == WGF_RESOURCE_STATUS_NONE,
           "the models gone, the mesh's last reference was the caller's");

    test_instancing();
    wgf_gfx_priv_stop();
    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
