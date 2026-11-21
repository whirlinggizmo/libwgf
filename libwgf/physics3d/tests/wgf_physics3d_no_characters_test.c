#include <stdio.h>

#include "wgf_actor.h"
#include "wgf_body.h"
#include "wgf_component.h"
#include "wgf_core_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "wgf_log.h"
#include "wgf_physics.h"
#include "wgf_stage3d.h"

/* A program that never names characters (wgf_physics_enable_characters): their file isn't
 * linked, so the character type is refused, said, while every other body still is one. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    wgf_actor_t stage, walker;
    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_FATAL); /* the refusal logs, on purpose */
    expect(wgf_gfx_priv_start(), "gfx");
    stage = wgf_stage3d_create();
    expect(wgf_physics_set_gravity(0, -9.81f, 0), "physics started");
    walker = wgf_actor_create();
    wgf_actor_set_parent(walker, stage);
    expect(wgf_actor_add_component(walker, WGF_COMPONENT_BODY), "a body");
    expect(!wgf_physics_is_characters_enabled(), "characters not linked");
    expect(!wgf_body_set_type(walker, WGF_BODY_TYPE_CHARACTER) && wgf_body_get_type(walker) != WGF_BODY_TYPE_CHARACTER,
           "the character type refused");
    expect(wgf_body_set_type(walker, WGF_BODY_TYPE_KINEMATIC), "another type still taken");
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    if (failures == 0) printf("wgf_physics3d_no_characters_test: passed\n");
    return failures == 0 ? 0 : 1;
}
