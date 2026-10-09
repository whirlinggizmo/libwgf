#include <stdio.h>
#include <stdlib.h>

#include <GLES3/gl3.h>
#include <emscripten.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_light.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* Where a shadow starts, measured, libwgt's test (wgrender's pillar): a thin pillar on a
 * floor, seen straight down through an orthographic camera 4 units tall on a 400-pixel
 * canvas, so a pixel column is a hundredth of a unit and the pillar's foot is at column
 * 100. Under a spot whose far plane is just past the foot, the shadow must start at the
 * foot (with the slack taken in depth units it started 2.21 units past it); under a
 * directional light the same. Then a caster out of view: a cube high over the floor, out
 * of the camera's sight, still shadows the floor in it; one that doesn't cast, doesn't; and
 * a floor that doesn't receive shows no shadow. WebGL2, through app's runtime, the frame's
 * pixels read back before the browser shows them. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The red of the pixel at column x, row y from the top. The lit floor reads about 200 and
 * the shadowed about 64 (its 0.05 ambient), so under 100 is shadow. */
static int red_at(int x, int y)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, 399 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return rgba[0];
}

/* The column, from 106 on (past the pillar's own 12 pixels), of the first floor pixel along
 * the middle row in shadow, or 400 for none: where the shadow starts. */
static int shadow_start(void)
{
    static unsigned char row[400 * 4];
    int x;
    glReadPixels(0, 399 - 200, 400, 1, GL_RGBA, GL_UNSIGNED_BYTE, row);
    for (x = 106; x < 400; x++) {
        if (row[x * 4] < 100) return x;
    }
    return 400;
}

static wgf_actor_t floor_model;

static wgf_actor_t pillar_stage(wgf_actor_t light, float pillar_y)
{
    const wgf_actor_t room = wgf_stage3d_create(), eye = wgf_camera3d_create();
    const wgf_mesh_t floor_mesh = wgf_mesh_create_plane(40, 40, 0), pillar_mesh = wgf_mesh_create_cube(0.12f, 3, 0.12f);
    const wgf_actor_t pillar = wgf_model_create(pillar_mesh);
    floor_model = wgf_model_create(floor_mesh);
    wgf_resource_release(floor_mesh);
    wgf_resource_release(pillar_mesh);
    wgf_camera3d_set_orthographic(eye, true);
    wgf_camera3d_set_ortho_height(eye, 4.0f); /* 400 pixels: 100 a unit; x -1..3 across, the foot at column 100 */
    wgf_camera3d_set_clip(eye, 0.1f, 100.0f);
    wgf_actor_set_position(eye, 1, 10, 0);
    wgf_actor_look_at(eye, 1, 0, 0, 0, 0, -1);
    wgf_actor_set_parent(eye, room);
    wgf_stage3d_set_camera(room, eye);
    wgf_stage3d_set_tonemap(room, WGF_STAGE3D_TONEMAP_NONE, 0);
    wgf_stage3d_set_ambient(room, WGF_COLOR_WHITE, 0.05f);
    wgf_actor_set_parent(light, room);
    wgf_actor_set_parent(floor_model, room);
    wgf_actor_set_position(pillar, 0, pillar_y, 0);
    wgf_actor_set_parent(pillar, room);
    return room;
}

static void draw(wgf_actor_t stage)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
}

static void run_test(void)
{
    wgf_actor_t room, light;
    int start;

    /* a spot from the -x side, aimed down at the floor past the pillar, its far plane 11
       away where the foot is 10: the case that lifted the shadow */
    light = wgf_light_create(WGF_LIGHT_TYPE_SPOT);
    wgf_actor_set_position(light, -8, 6, 0);
    wgf_actor_look_at(light, 1, 0, 0, 0, 1, 0); /* along (9, -6, 0) */
    wgf_light_set_intensity(light, 400.0f);
    wgf_light_set_range(light, 40.0f);
    wgf_light_set_spot_cone(light, 0.30f, 0.40f);
    wgf_light_set_shadow_casting(light, true);
    wgf_light_set_shadow_map_size(light, 1024);
    wgf_light_set_shadow_distance(light, 11.0f);
    room = pillar_stage(light, 1.5f);
    draw(room);
    start = shadow_start();
    printf("spot: the shadow starts %.2f units past the foot\n", (start - 100) / 100.0);
    expect(start < 400, "a spot: the pillar casts a shadow on the floor");
    expect(start <= 112, "a spot: its shadow starts at the pillar's foot, within a tenth of a unit");
    wgf_actor_destroy(room, WGF_ACTOR_DESTROY_CHILDREN);

    /* the same under a directional light along the same direction */
    light = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(light, 9, -6, 0, 0, 1, 0);
    wgf_light_set_intensity(light, 3.0f);
    wgf_light_set_shadow_casting(light, true);
    wgf_light_set_shadow_map_size(light, 1024);
    wgf_light_set_shadow_distance(light, 20.0f);
    room = pillar_stage(light, 1.5f);
    draw(room);
    start = shadow_start();
    printf("directional: the shadow starts %.2f units past the foot\n", (start - 100) / 100.0);
    expect(start < 400, "a directional light: the pillar casts a shadow on the floor");
    expect(start <= 112, "a directional light: its shadow starts at the pillar's foot");
    expect(red_at(300, 20) > 150, "the floor away from the shadow: lit");
    wgf_actor_destroy(room, WGF_ACTOR_DESTROY_CHILDREN);

    /* a caster out of view: the pillar lifted to 11 to 14 up, above the camera (at 10),
       the sun straight down: its shadow, a 12-pixel square, under it at column 100 */
    light = wgf_light_create(WGF_LIGHT_TYPE_DIRECTIONAL);
    wgf_actor_look_at(light, 0, -1, 0, 0, 0, -1);
    wgf_light_set_intensity(light, 3.0f);
    wgf_light_set_shadow_casting(light, true);
    wgf_light_set_shadow_map_size(light, 1024);
    wgf_light_set_shadow_distance(light, 20.0f);
    room = pillar_stage(light, 12.5f);
    draw(room);
    printf("out of view: the floor under it reads %d, beside it %d\n", red_at(100, 200), red_at(150, 200));
    expect(red_at(100, 200) < 100 && red_at(150, 200) > 150, "a caster out of view still shadows the floor in it");
    wgf_model_set_shadow_receiving(floor_model, false);
    draw(room);
    expect(red_at(100, 200) > 150, "a floor that doesn't receive: no shadow on it");
    wgf_actor_destroy(room, WGF_ACTOR_DESTROY_CHILDREN);

    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void on_frame(void *user)
{
    static int frames;
    (void)user;
    if (++frames == 2) run_test(); /* the runtime's first frame done: gfx is up */
}

int main(void)
{
    wgf_window_set_size(400, 400);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
