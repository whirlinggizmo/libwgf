#include <math.h>
#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_camera2d.h"
#include "wgf_camera3d.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_follow.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_presentation.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"

/* Cameras that follow, headless, the part's update stepped by the test: a 2D camera
 * catching up, free in its dead zone, kept in its bounds, and shaken; a 3D camera behind a
 * target as it turns (CHASE), fixed, and orbiting; snapping; what is refused; and a camera
 * whose target is destroyed staying put. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void frames(int n)
{
    while (n-- > 0) wgf_core_priv_part_update(1.0f / 60.0f);
}

static int near(float a, float b, float within)
{
    return fabsf(a - b) < within;
}

int main(void)
{
    wgf_actor_t stage, camera, hero, world, eye, car;
    wgf_vec3_t at;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(wgf_gfx_priv_start(), "setup");
    wgf_platform_priv_set_size(800, 600);
    wgf_presentation_set(WGF_PRESENTATION_MODE_FIT, 800, 600);

    stage = wgf_stage2d_create();
    camera = wgf_camera2d_create();
    hero = wgf_actor_create();
    wgf_actor_set_parent(camera, stage);
    wgf_actor_set_parent(hero, stage);
    expect(!wgf_follow_set_target(hero, camera) && !wgf_follow_set_smoothing(camera, 2), "only a camera follows");
    expect(wgf_follow_set_target(camera, hero) && wgf_follow_get_target(camera) == hero && near(wgf_follow_get_smoothing(camera), 8, 1e-6f),
           "a 2D camera following, its smoothing 8");
    wgf_actor_set_position(hero, 100, 50, 0);
    frames(1);
    at = wgf_actor_get_position(camera);
    expect(near(at.x, 100, 1e-3f) && near(at.y, 50, 1e-3f), "put on its target at the first frame");
    wgf_actor_set_position(hero, 200, 50, 0);
    frames(1);
    at = wgf_actor_get_position(camera);
    expect(at.x > 100.5f && at.x < 199.0f, "catching up, smoothed");
    frames(120);
    expect(near(wgf_actor_get_position(camera).x, 200, 0.05f), "caught up");
    expect(wgf_follow_set_dead_zone(camera, 100, 60) && near(wgf_follow_get_dead_zone(camera).x, 100, 1e-6f) &&
               !wgf_follow_set_dead_zone(camera, -1, 0),
           "a dead zone; a negative one refused");
    wgf_actor_set_position(hero, 240, 50, 0);
    frames(60);
    expect(near(wgf_actor_get_position(camera).x, 200, 1e-3f), "inside the dead zone: the view stays");
    wgf_actor_set_position(hero, 300, 50, 0);
    frames(120);
    expect(near(wgf_actor_get_position(camera).x, 250, 0.05f), "past it: caught up to the zone's edge");
    expect(wgf_follow_set_bounds(camera, 0, 0, 1000, 700) && near(wgf_follow_get_bounds(camera).z, 1000, 1e-6f),
           "bounds");
    wgf_actor_set_position(hero, 980, 690, 0);
    frames(240);
    at = wgf_actor_get_position(camera);
    expect(near(at.x, 600, 0.05f) && near(at.y, 400, 0.05f), "the view kept inside its bounds");
    expect(wgf_follow_shake(camera, 0.5f) && wgf_follow_shake(camera, 0.8f) && near(wgf_follow_get_trauma(camera), 1, 1e-6f) &&
               !wgf_follow_shake(camera, -1),
           "shaken, trauma held to 1");
    frames(6);
    at = wgf_actor_get_position(camera);
    expect(!(near(at.x, 600, 1e-3f) && near(at.y, 400, 1e-3f)) && wgf_follow_get_trauma(camera) < 1.0f, "thrown, settling");
    frames(60);
    at = wgf_actor_get_position(camera);
    expect(wgf_follow_get_trauma(camera) == 0.0f && near(at.x, 600, 1e-3f), "settled");

    world = wgf_stage3d_create();
    eye = wgf_camera3d_create();
    car = wgf_actor_create();
    wgf_actor_set_parent(eye, world);
    wgf_actor_set_parent(car, world);
    expect(wgf_follow_set_target(eye, car) && wgf_follow_get_mode(eye) == WGF_FOLLOW_MODE_CHASE &&
               near(wgf_follow_get_offset(eye).z, 6, 1e-6f),
           "a 3D camera chasing, 6 back and 3 up");
    frames(1);
    at = wgf_actor_get_position(eye);
    expect(near(at.x, 0, 1e-3f) && near(at.y, 3, 1e-3f) && near(at.z, 6, 1e-3f), "behind a target facing -z");
    wgf_actor_set_rotation(car, 0, 1.5707963f, 0); /* a quarter turn: facing -x */
    frames(240);
    at = wgf_actor_get_position(eye);
    expect(near(at.x, 6, 0.02f) && near(at.z, 0, 0.02f), "swung round behind it as it turned");
    expect(wgf_follow_set_mode(eye, WGF_FOLLOW_MODE_FIXED) && wgf_follow_set_offset(eye, 0, 10, 0.01f), "fixed, overhead");
    frames(1);
    expect(near(wgf_actor_get_position(eye).y, 10, 1e-3f), "fixed: the offset as it is");
    expect(wgf_follow_set_mode(eye, WGF_FOLLOW_MODE_ORBIT) && wgf_follow_set_orbit(eye, 1.5707963f, 0, 4) &&
               !wgf_follow_set_orbit(eye, 0, 0, 0) && near(wgf_follow_get_orbit(eye).z, 4, 1e-6f),
           "orbiting; a distance of 0 refused");
    frames(1);
    at = wgf_actor_get_position(eye);
    expect(near(at.x, 4, 1e-3f) && near(at.y, 0, 1e-3f) && near(at.z, 0, 1e-3f), "on the target's +x side");
    expect(wgf_follow_set_orbit(eye, 0, 3, 4) && near(wgf_follow_get_orbit(eye).y, 1.5533430f, 1e-5f), "its pitch clamped");
    wgf_follow_set_mode(eye, WGF_FOLLOW_MODE_FIXED);
    wgf_follow_set_offset(eye, 0, 3, 6);
    wgf_follow_set_smoothing(eye, 1);
    wgf_actor_set_position(car, 50, 0, 0);
    expect(wgf_follow_snap(eye) && near(wgf_actor_get_position(eye).x, 50, 1e-3f), "snapped to it at once");

    wgf_actor_destroy(car, WGF_ACTOR_DESTROY_CHILDREN);
    frames(10);
    expect(near(wgf_actor_get_position(eye).x, 50, 1e-3f), "its target gone: staying where it is");
    expect(wgf_follow_set_target(eye, 0) && wgf_follow_get_target(eye) == 0, "stopped following");

    { /* the screen and the world */
        const wgf_actor_t flat = wgf_stage2d_create(), view = wgf_camera2d_create();
        const wgf_actor_t space = wgf_stage3d_create(), lens = wgf_camera3d_create();
        wgf_vec2_t p;
        wgf_vec3_t q;
        wgf_actor_set_parent(view, flat);
        wgf_stage2d_set_camera(flat, view);
        wgf_actor_set_position(view, 300, 200, 0);
        wgf_camera2d_set_zoom(view, 2);
        p = wgf_stage2d_world_to_screen(flat, 310, 200);
        expect(near(p.x, 420, 1e-3f) && near(p.y, 300, 1e-3f), "2D: a point of the stage on the screen, zoomed");
        p = wgf_stage2d_screen_to_world(flat, 400, 300);
        expect(near(p.x, 300, 1e-3f) && near(p.y, 200, 1e-3f), "and the screen's center back in the stage");
        wgf_actor_set_parent(lens, space);
        wgf_actor_set_position(lens, 0, 0, 10);
        q = wgf_camera3d_world_to_screen(lens, 0, 0, 0);
        expect(near(q.x, 400, 1e-2f) && near(q.y, 300, 1e-2f) && q.z > 0 && q.z < 1, "3D: in front, at the screen's center");
        q = wgf_camera3d_world_to_screen(lens, 1, 1, 0);
        expect(q.x > 400 && q.y < 300, "up and right of it: up and right on the screen");
        expect(wgf_camera3d_world_to_screen(lens, 0, 0, 20).z < 0, "behind the camera: below 0");
        q = wgf_camera3d_screen_to_direction(lens, 400, 300);
        expect(near(q.x, 0, 1e-4f) && near(q.z, -1, 1e-4f), "the ray under the center: straight ahead");
        q = wgf_camera3d_screen_to_origin(lens, 400, 300);
        expect(near(q.x, 0, 1e-3f) && q.z < 10 && q.z > 9, "from the near plane");
        q = wgf_camera3d_world_to_screen(lens, 2, -1, 3);
        {
            const wgf_vec3_t o = wgf_camera3d_screen_to_origin(lens, q.x, q.y), d = wgf_camera3d_screen_to_direction(lens, q.x, q.y);
            const float t = (3 - o.z) / d.z;
            expect(near(o.x + d.x * t, 2, 1e-2f) && near(o.y + d.y * t, -1, 1e-2f), "a point's place on the screen, cast back, meets it");
        }
        expect(wgf_camera3d_world_to_screen(flat, 0, 0, 0).x == 0 && wgf_stage2d_world_to_screen(space, 1, 1).x == 0,
               "the wrong kind: 0");
        wgf_actor_destroy(flat, WGF_ACTOR_DESTROY_CHILDREN);
        wgf_actor_destroy(space, WGF_ACTOR_DESTROY_CHILDREN);
    }
    wgf_actor_destroy(world, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
