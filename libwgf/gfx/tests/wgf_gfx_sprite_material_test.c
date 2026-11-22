#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "sprite/wgf_gfx_sprite_batch_priv.h"
#include "stage/wgf_gfx_stage3d_priv.h"
#include "wgf_camera3d.h"
#include "wgf_handle.h"
#include "wgf_light.h"
#include "wgf_material.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_sprite.h"
#include "wgf_stage2d.h"
#include "wgf_stage3d.h"
#include "wgf_texture.h"

/* Sprites drawn with a material (wgf_sprite_set_material), on sokol's dummy backend: the
 * material held and given back; on a 3D stage, a sprite with one a part the stage shades (not
 * a quad of the sprites' batch), lit by the lights that reach it when its material is PBR and
 * by none when UNLIT; sprites alike in texture and material one draw whatever their regions;
 * and on a 2D stage, drawn as without one. Pixels: the gfx-lights example. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int most_lights; /* the most lights any of the last frame's parts was lit by */

/* One frame drawing `stage`: how many of its parts are lit by a light, the draws its parts
 * took, and the sprites its batch drew. */
static int drawn(wgf_actor_t stage, int *draws, int *batched)
{
    int lit = 0, i;
    most_lights = 0;
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    for (i = 0; i < wgf_gfx_priv_stage3d_get_item_count(); i++) {
        const int lights = wgf_gfx_priv_stage3d_get_item_lights(i);
        if (lights > 0) lit++;
        if (lights > most_lights) most_lights = lights;
    }
    wgf_gfx_priv_end_frame();
    *draws = wgf_gfx_priv_stage3d_get_draw_calls();
    *batched = wgf_gfx_priv_sprite_batch_get_drawn();
    return lit;
}

int main(void)
{
    wgf_actor_t stage, camera, sun, lamp, sprites[3], flat;
    wgf_texture_t sheet;
    wgf_material_t lit, unlit;
    int draws = 0, batched = 0, i;

    expect(wgf_gfx_priv_start(), "setup");
    wgf_platform_priv_set_size(640, 480);
    stage = wgf_stage3d_create();
    camera = wgf_camera3d_create();
    wgf_actor_set_position(camera, 0, 0, 5);
    wgf_actor_set_parent(camera, stage);
    wgf_stage3d_set_camera(stage, camera);
    sun = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_set_parent(sun, stage);
    lamp = wgf_light_create(WGF_LIGHT_TYPE_POINT); /* near the first sprite alone */
    wgf_light_set_range(lamp, 0.5f);
    wgf_actor_set_position(lamp, -1.0f, 0, 0.2f);
    wgf_actor_set_parent(lamp, stage);

    sheet = wgf_texture_create_blank(64, 64);
    lit = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    unlit = wgf_material_create(WGF_MATERIAL_SHADING_UNLIT);
    for (i = 0; i < 3; i++) {
        sprites[i] = wgf_sprite_create(sheet);
        wgf_sprite_set_size(sprites[i], 0.5f, 0.5f);
        wgf_sprite_set_source(sprites[i], 16.0f * (float)i, 0, 16, 16); /* a region each */
        wgf_sprite_set_alpha_mode(sprites[i], WGF_ALPHA_MODE_OPAQUE, 0.5f);
        wgf_actor_set_position(sprites[i], (float)i - 1.0f, 0, 0);
        wgf_actor_set_parent(sprites[i], stage);
    }

    expect(wgf_sprite_get_material(sprites[0]) == 0, "a sprite has no material");
    expect(drawn(stage, &draws, &batched) == 0 && draws == 0 && batched == 3,
           "without one: three quads of the sprites' batch, no part");
    expect(!wgf_sprite_set_material(sprites[0], sheet) && !wgf_sprite_set_material(stage, lit) &&
               !wgf_sprite_set_material(0, lit) && wgf_sprite_get_material(stage) == 0,
           "a handle that isn't a material, or isn't a sprite: refused");
    expect(wgf_sprite_set_material(sprites[0], lit) && wgf_sprite_get_material(sprites[0]) == lit, "a material set, read back");
    expect(drawn(stage, &draws, &batched) == 1 && draws == 1 && batched == 2,
           "with one: a part the stage shades, out of the batch");
    expect(most_lights == 2, "lit by the sun and the lamp beside it");
    for (i = 1; i < 3; i++) expect(wgf_sprite_set_material(sprites[i], lit), "the others too");
    expect(drawn(stage, &draws, &batched) == 3 && batched == 0, "three parts");
    expect(draws == 2, "alike but for their regions: one draw for the two the sun alone lights, one for the lamp's");
    wgf_actor_set_visible(lamp, false);
    expect(drawn(stage, &draws, &batched) == 3 && draws == 1, "the same lights: one draw, a region each");
    expect(wgf_sprite_set_alpha_mode(sprites[1], WGF_ALPHA_MODE_MASK, 0.25f) && drawn(stage, &draws, &batched) == 3 &&
               draws == 2,
           "a cutoff of its own: a draw of its own");
    expect(wgf_sprite_set_material(sprites[2], unlit) && drawn(stage, &draws, &batched) == 2 && draws == 3,
           "an UNLIT material: a draw of its own, no lights chosen for it");
    expect(wgf_resource_release(lit) && wgf_resource_get_status(lit) != WGF_RESOURCE_STATUS_NONE,
           "the sprites hold the material");
    expect(wgf_sprite_set_material(sprites[0], 0) && wgf_sprite_get_material(sprites[0]) == 0 &&
               drawn(stage, &draws, &batched) == 1 && draws == 2 && batched == 1,
           "0: back to its texture and tint, in the batch");
    wgf_actor_destroy(sprites[1], WGF_ACTOR_DESTROY_CHILDREN);
    expect(!wgf_handle_is_alive(lit), "the last sprite holding it gone: let go");

    /* a 2D stage has no lights: drawn as without one */
    flat = wgf_stage2d_create();
    wgf_actor_set_parent(sprites[2], flat);
    wgf_gfx_priv_begin_frame();
    wgf_stage2d_draw(flat);
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
    expect(wgf_gfx_priv_stage3d_get_draw_calls() == 0, "on a 2D stage: drawn as without one, no part of a 3D stage's");

    wgf_resource_release(unlit);
    wgf_resource_release(sheet);
    wgf_gfx_priv_stop();
    return failures == 0 ? 0 : 1;
}
