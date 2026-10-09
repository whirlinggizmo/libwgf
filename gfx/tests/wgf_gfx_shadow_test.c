#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_shadow_priv.h"
#include "wgf_actor.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_core_priv.h"
#include "wgf_light.h"
#include "wgf_log.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"

/* Shadows on sokol's dummy backend (a headless build), libwgt's checks (wgrender's): the
 * state a light and a model carry, the fits that decide what a light's map covers, then
 * what the depth pass draws -- which casters, into how many layers -- and the cull that
 * keeps a caster out of view whose shadow reaches into it. Pixels: wgf_gfx_shadow_web_test. */

static int failures;

static void check(int ok, const char *what, int line)
{
    if (!ok) {
        printf("FAIL: line %d: %s\n", line, what);
        failures++;
    }
}

#define CHECK(c) check((c) ? 1 : 0, #c, __LINE__)
#define CHECK_NEAR(a, b, eps) check(fabs((double)(a) - (double)(b)) <= (double)(eps), #a " near " #b, __LINE__)

static void test_state(void)
{
    const wgf_actor_t sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    const wgf_actor_t spot = wgf_light_create(WGF_LIGHT_TYPE_SPOT);
    const wgf_actor_t point = wgf_light_create(WGF_LIGHT_TYPE_POINT);
    const wgf_actor_t model = wgf_model_create(0);
    const wgf_color_t blue = wgf_color_make(0, 0, 255, 255);

    CHECK(!wgf_light_is_shadow_casting(sun)); /* off until asked: a map costs a pass */
    CHECK(wgf_light_set_shadow_casting(sun, true) && wgf_light_is_shadow_casting(sun));
    CHECK(wgf_light_set_shadow_casting(sun, false) && !wgf_light_is_shadow_casting(sun));
    /* spot lights cast too; point lights would need six maps, so they don't */
    CHECK(wgf_light_set_shadow_casting(spot, true) && wgf_light_is_shadow_casting(spot));
    CHECK(!wgf_light_set_shadow_casting(point, true) && !wgf_light_is_shadow_casting(point));
    CHECK(wgf_light_set_shadow_casting(point, false)); /* turning it off is always fine */

    /* the settings take sensible values and refuse silly ones */
    CHECK(wgf_light_get_shadow_distance(sun) == 50.0f && wgf_light_get_shadow_map_size(sun) == 2048 &&
          wgf_light_get_shadow_bias_constant(sun) == 1.0f && wgf_light_get_shadow_bias_slope(sun) == 4.0f &&
          wgf_light_get_shadow_strength(sun) == 1.0f && wgf_light_get_shadow_color(sun) == 0x000000FFu);
    CHECK(wgf_light_set_shadow_distance(sun, 25.0f));
    CHECK(!wgf_light_set_shadow_distance(sun, 0.0f) && !wgf_light_set_shadow_distance(sun, -5.0f) &&
          wgf_light_get_shadow_distance(sun) == 25.0f);
    CHECK(wgf_light_set_shadow_map_size(sun, 64) && wgf_light_get_shadow_map_size(sun) == 256);
    CHECK(wgf_light_set_shadow_map_size(sun, 9000) && wgf_light_get_shadow_map_size(sun) == 4096);
    CHECK(wgf_light_set_shadow_map_size(sun, 1500) && wgf_light_get_shadow_map_size(sun) == 1024);
    CHECK(!wgf_light_set_shadow_map_size(sun, 0) && !wgf_light_set_shadow_map_size(sun, -256));
    CHECK(wgf_light_set_shadow_bias(sun, 2.0f, 6.0f) && wgf_light_get_shadow_bias_constant(sun) == 2.0f &&
          wgf_light_get_shadow_bias_slope(sun) == 6.0f);
    CHECK(!wgf_light_set_shadow_bias(sun, -2.0f, 6.0f) && !wgf_light_set_shadow_bias(sun, 2.0f, -6.0f));
    CHECK(wgf_light_set_shadow_strength(sun, -0.1f) && wgf_light_get_shadow_strength(sun) == 0.0f);
    CHECK(wgf_light_set_shadow_strength(sun, 1.5f) && wgf_light_get_shadow_strength(sun) == 1.0f);
    CHECK(wgf_light_set_shadow_color(sun, blue) && wgf_light_get_shadow_color(sun) == blue);
    CHECK(wgf_light_get_shadow_map_size(0) == 0 && wgf_light_get_shadow_strength(0) == 0.0f);
    /* and nothing works on a handle that isn't a light */
    CHECK(!wgf_light_set_shadow_casting(0, true) && !wgf_light_is_shadow_casting(0));
    CHECK(!wgf_light_set_shadow_distance(model, 10.0f) && !wgf_light_set_shadow_map_size(model, 1024));
    CHECK(!wgf_light_set_shadow_bias(0, 1.0f, 1.0f) && !wgf_light_set_shadow_strength(0, 0.5f));
    CHECK(!wgf_light_set_shadow_color(0, blue));

    /* a model casts and receives until it's told otherwise */
    CHECK(wgf_model_is_shadow_casting(model) && wgf_model_is_shadow_receiving(model));
    CHECK(wgf_model_set_shadow_casting(model, false) && !wgf_model_is_shadow_casting(model) &&
          wgf_model_is_shadow_receiving(model));
    CHECK(wgf_model_set_shadow_receiving(model, false) && !wgf_model_is_shadow_receiving(model));
    CHECK(!wgf_model_set_shadow_casting(sun, true) && !wgf_model_is_shadow_casting(sun));

    wgf_actor_destroy(model, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(point, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(spot, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(sun, WGF_ACTOR_DESTROY_CHILDREN);
}

/* A point through the light's matrix, in its clip space. */
static wgf_vec3_t to_light_clip(wgf_mat4_t m, wgf_vec3_t world)
{
    const float x = m.m[0] * world.x + m.m[4] * world.y + m.m[8] * world.z + m.m[12];
    const float y = m.m[1] * world.x + m.m[5] * world.y + m.m[9] * world.z + m.m[13];
    const float z = m.m[2] * world.x + m.m[6] * world.y + m.m[10] * world.z + m.m[14];
    const float w = m.m[3] * world.x + m.m[7] * world.y + m.m[11] * world.z + m.m[15];
    return wgf_vec3_make(x / w, y / w, z / w);
}

/* A camera at `position` looking at `target`, 60 degrees tall, as the fits see it. */
static wgf_gfx_priv_shadow_camera_t camera_at(wgf_vec3_t position, wgf_vec3_t target, float aspect)
{
    wgf_gfx_priv_shadow_camera_t cam;
    memset(&cam, 0, sizeof(cam));
    cam.position = position;
    cam.forward = wgf_vec3_normalize(wgf_vec3_sub(target, position));
    cam.up = wgf_vec3_make(0.0f, 1.0f, 0.0f);
    cam.fov = 60.0f * 3.14159265f / 180.0f;
    cam.ortho_height = 10.0f;
    cam.near_z = 0.1f;
    cam.aspect = aspect;
    return cam;
}

static void test_fit(void)
{
    const wgf_gfx_priv_shadow_camera_t cam = camera_at(wgf_vec3_make(0, 4, 10), wgf_vec3_make(0, 0, 0), 16.0f / 9.0f);
    const wgf_vec3_t straight_down = wgf_vec3_make(0.0f, -1.0f, 0.0f);
    wgf_gfx_priv_shadow_camera_t nudged = cam;

    /* what the camera looks at is inside what the light's map covers */
    const wgf_gfx_priv_shadow_fit_t fit = wgf_gfx_priv_shadow_fit_directional(&cam, straight_down, 30.0f, 1024, 50.0f);
    const wgf_vec3_t middle = to_light_clip(fit.view_proj, wgf_vec3_make(0.0f, 0.0f, 0.0f));
    CHECK(middle.x > -1.0f && middle.x < 1.0f && middle.y > -1.0f && middle.y < 1.0f && middle.z > -1.0f &&
          middle.z < 1.0f);
    CHECK(fit.texel_world > 0.0f);
    CHECK(fit.depth_range > 30.0f); /* the slice, plus the pull-back for casters behind it */
    { /* a point far outside the camera's view isn't covered */
        const wgf_vec3_t far_away = to_light_clip(fit.view_proj, wgf_vec3_make(500.0f, 0.0f, 0.0f));
        CHECK(far_away.x < -1.0f || far_away.x > 1.0f);
    }
    { /* a bigger map over the same ground means smaller texels */
        const wgf_gfx_priv_shadow_fit_t coarse =
            wgf_gfx_priv_shadow_fit_directional(&cam, straight_down, 30.0f, 512, 50.0f);
        const wgf_gfx_priv_shadow_fit_t fine = wgf_gfx_priv_shadow_fit_directional(&cam, straight_down, 30.0f, 4096, 50.0f);
        CHECK(fine.texel_world < coarse.texel_world);
        CHECK_NEAR(coarse.texel_world / fine.texel_world, 8.0f, 0.001f);
    }
    /* less distance covers less ground, so its texels are smaller again */
    CHECK(wgf_gfx_priv_shadow_fit_directional(&cam, straight_down, 10.0f, 1024, 50.0f).texel_world < fit.texel_world);
    /* snapped to whole texels: nudging the camera by less than one doesn't move the map,
       which is what keeps a shadow's edge from crawling */
    nudged.position.x += fit.texel_world * 0.1f;
    {
        const wgf_gfx_priv_shadow_fit_t shifted =
            wgf_gfx_priv_shadow_fit_directional(&nudged, straight_down, 30.0f, 1024, 50.0f);
        const wgf_vec3_t before = to_light_clip(fit.view_proj, wgf_vec3_make(1.0f, 0.0f, 1.0f));
        const wgf_vec3_t after = to_light_clip(shifted.view_proj, wgf_vec3_make(1.0f, 0.0f, 1.0f));
        CHECK_NEAR(after.x, before.x, 1e-3f);
        CHECK_NEAR(after.y, before.y, 1e-3f);
    }
    { /* any direction works */
        const wgf_gfx_priv_shadow_fit_t sideways =
            wgf_gfx_priv_shadow_fit_directional(&cam, wgf_vec3_make(1.0f, -0.2f, 0.3f), 20.0f, 1024, 50.0f);
        const wgf_vec3_t seen = to_light_clip(sideways.view_proj, wgf_vec3_make(0.0f, 0.0f, 0.0f));
        CHECK(seen.x > -1.0f && seen.x < 1.0f && seen.y > -1.0f && seen.y < 1.0f);
    }
    { /* nonsense in, something sane out */
        const wgf_gfx_priv_shadow_fit_t degenerate =
            wgf_gfx_priv_shadow_fit_directional(&cam, wgf_vec3_make(0, 0, 0), -5.0f, 0, 0.0f);
        CHECK(degenerate.texel_world > 0.0f && degenerate.depth_range > 0.0f);
    }
}

/* A spot light's map covers its own cone, from where it stands. */
static void test_fit_spot(void)
{
    const wgf_vec3_t at = wgf_vec3_make(0.0f, 6.0f, 0.0f), down = wgf_vec3_make(0.0f, -1.0f, 0.0f);
    const float cos_outer = cosf(0.5f);
    const wgf_gfx_priv_shadow_fit_t fit = wgf_gfx_priv_shadow_fit_spot(at, down, cos_outer, 20.0f, 0.2f, 1024);
    const wgf_vec3_t under = to_light_clip(fit.view_proj, wgf_vec3_make(0.0f, 0.0f, 0.0f));
    const wgf_vec3_t inside = to_light_clip(fit.view_proj, wgf_vec3_make(1.0f, 0.0f, 0.0f));
    const wgf_vec3_t outside = to_light_clip(fit.view_proj, wgf_vec3_make(20.0f, 0.0f, 0.0f));
    const wgf_vec3_t behind = to_light_clip(fit.view_proj, wgf_vec3_make(0.0f, 12.0f, 0.0f));
    CHECK_NEAR(under.x, 0.0f, 1e-3f);
    CHECK_NEAR(under.y, 0.0f, 1e-3f);
    CHECK(under.z > -1.0f && under.z < 1.0f);
    CHECK(inside.x > -1.0f && inside.x < 1.0f && inside.y > -1.0f && inside.y < 1.0f);
    CHECK(outside.x < -1.0f || outside.x > 1.0f);
    CHECK(behind.z < -1.0f || behind.z > 1.0f); /* behind the lamp: behind the projection */
    CHECK_NEAR(fit.depth_range, 20.0f, 1e-3f);
    CHECK(wgf_gfx_priv_shadow_fit_spot(at, down, cosf(0.9f), 20.0f, 0.2f, 1024).texel_world > fit.texel_world);
    CHECK(wgf_gfx_priv_shadow_fit_spot(at, down, cos_outer, 20.0f, 0.2f, 4096).texel_world < fit.texel_world);
    {
        const wgf_gfx_priv_shadow_fit_t silly =
            wgf_gfx_priv_shadow_fit_spot(at, wgf_vec3_make(0, 0, 0), 2.0f, -1.0f, -1.0f, 0);
        CHECK(silly.texel_world > 0.0f && silly.depth_range > 0.0f);
    }
}

/* A frame of `stage`, ended: how many casters went into its shadow maps (every layer's),
 * the layers drawn, and the parts the stage kept (drawn, or kept for their shadows). */
static int shadow_draws(wgf_actor_t stage, int *layers, int *kept)
{
    wgf_platform_priv_set_size(64, 64);
    wgf_platform_priv_headless_set_dpi_scale(1.0f);
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    if (kept != NULL) *kept = wgf_gfx_priv_stage3d_get_item_count();
    wgf_gfx_priv_end_frame();
    if (layers != NULL) *layers = wgf_gfx_priv_shadow_get_layers();
    return wgf_gfx_priv_shadow_get_draw_calls();
}

static wgf_actor_t add_model(wgf_actor_t stage, wgf_mesh_t mesh, float x, float y, float z)
{
    const wgf_actor_t model = wgf_model_create(mesh);
    wgf_actor_set_position(model, x, y, z);
    wgf_actor_set_parent(model, stage);
    return model;
}

/* What the depth pass draws: the casters the light's map reaches, a draw each. */
static void test_casters(void)
{
    const wgf_actor_t stage = wgf_stage3d_create(), camera = wgf_camera3d_create();
    const wgf_actor_t sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL), spot = wgf_light_create(WGF_LIGHT_TYPE_SPOT);
    const wgf_mesh_t cube = wgf_mesh_create_cube(1, 1, 1), plane = wgf_mesh_create_plane(20, 20, 0);
    wgf_actor_t floor, a, b, c, glass;
    int layers, kept;

    wgf_actor_set_position(camera, 0, 5, 12);
    wgf_actor_look_at(camera, 0, 0, 0, 0, 1, 0);
    wgf_actor_set_parent(camera, stage);
    wgf_stage3d_set_camera(stage, camera);
    wgf_actor_set_parent(sun, stage);
    wgf_actor_look_at(sun, -0.3f, -1.0f, -0.2f, 0, 1, 0); /* from the origin, down and away */
    floor = add_model(stage, plane, 0, 0, 0);
    a = add_model(stage, cube, -2, 1, 0);
    b = add_model(stage, cube, 0, 1, 0);
    c = add_model(stage, cube, 2, 1, 0);

    CHECK(shadow_draws(stage, &layers, &kept) == 0 && layers == 0 && kept == 4); /* no light casts: no pass */
    wgf_light_set_shadow_casting(sun, true);
    CHECK(shadow_draws(stage, &layers, NULL) == 4 && layers == 1); /* the floor and the three cubes */
    wgf_model_set_shadow_casting(floor, false);
    CHECK(shadow_draws(stage, NULL, NULL) == 3); /* a floor that only receives */
    wgf_actor_set_visible(c, false);
    CHECK(shadow_draws(stage, NULL, &kept) == 2 && kept == 3); /* hidden: drawn nowhere, its shadow too */
    wgf_actor_set_visible(c, true);

    /* see-through parts don't cast: a blob shadow would double up */
    glass = add_model(stage, cube, 0, 3, 0);
    wgf_model_set_tint(glass, wgf_color_make(255, 255, 255, 100));
    CHECK(shadow_draws(stage, NULL, NULL) == 3);

    /* a map nothing samples is a pass for nothing */
    wgf_model_set_shadow_receiving(floor, false);
    wgf_model_set_shadow_receiving(a, false);
    wgf_model_set_shadow_receiving(b, false);
    wgf_model_set_shadow_receiving(c, false);
    wgf_model_set_shadow_receiving(glass, false);
    CHECK(shadow_draws(stage, &layers, NULL) == 0 && layers == 0);
    wgf_model_set_shadow_receiving(floor, true);

    /* two casting lights: a layer each, the casters drawn into both */
    wgf_actor_set_position(spot, 0, 8, 0);
    wgf_actor_look_at(spot, 0, 0, 0, 0, 0, -1);
    wgf_light_set_range(spot, 20.0f);
    wgf_light_set_shadow_casting(spot, true);
    wgf_actor_set_parent(spot, stage);
    CHECK(shadow_draws(stage, &layers, NULL) == 6 && layers == 2);
    /* a hidden light doesn't shine, so it casts nothing */
    wgf_actor_set_visible(spot, false);
    CHECK(shadow_draws(stage, &layers, NULL) == 3 && layers == 1);

    /* culling keeps a caster out of view whose shadow could fall into it -- the sun shines
       down, so a cube high above the view throws its shadow into it -- and drops one whose
       shadow can't */
    {
        const wgf_actor_t high = add_model(stage, cube, 0, 40, 0), aside = add_model(stage, cube, 200, 1, 0);
        CHECK(shadow_draws(stage, NULL, &kept) == 4 && kept == 6); /* floor, 3 cubes, glass, and high's shadow */
        wgf_model_set_shadow_casting(high, false);
        CHECK(shadow_draws(stage, NULL, &kept) == 3 && kept == 5); /* one that doesn't cast: culled as any */
        (void)aside;
    }

    wgf_resource_release(cube);
    wgf_resource_release(plane);
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
}

int main(void)
{
    wgf_core_priv_init();
    CHECK(wgf_gfx_priv_start());
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the refusals warn on purpose */
    test_state();
    wgf_log_set_level(WGF_LOG_LEVEL_INFO);
    test_fit();
    test_fit_spot();
    test_casters();
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
