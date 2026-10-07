#include <math.h>
#include <stdio.h>

#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_camera3d.h"
#include "wgf_platform_priv.h"
#include "wgf_shape3d.h"
#include "wgf_stage.h"

/* 3D shape nodes on sokol's dummy backend: each kind's settings, read back, and their
 * refusals; a line strip from a caller's array, and read back into one; and a stage's
 * draw recording its shapes (and nothing for a shape with nothing to draw, or hidden).
 * Pixels: wgf_gfx_stage_web_test. */

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
    return fabsf(a - b) < 1e-5f;
}

/* How many vertices a frame drawing `stage` records. */
static int recorded(wgf_node_t stage)
{
    int count;
    wgf_gfx_priv_begin_frame();
    count = sgl_num_vertices();
    wgf_stage_draw(stage);
    count = sgl_num_vertices() - count;
    wgf_gfx_priv_end_frame();
    return count;
}

int main(void)
{
    const float strip[9] = {0, 0, 0, 1, 0, 0, 1, 1, 0};
    float back[9] = {0};
    wgf_node_t shape, stage, camera, plain;
    wgf_vec3_t v;

    expect(wgf_gfx_priv_start(), "setup");
    wgf_platform_priv_set_size(320, 240);
    shape = wgf_shape3d_create();
    expect(wgf_node_get_type(shape) == WGF_NODE_TYPE_SHAPE3D && wgf_shape3d_get_kind(shape) == WGF_SHAPE3D_KIND_NONE &&
               wgf_shape3d_get_color(shape) == WGF_COLOR_WHITE,
           "a shape: nothing yet, white");
    expect(wgf_shape3d_set_cube(shape, 1, 2, 3) && wgf_shape3d_get_kind(shape) == WGF_SHAPE3D_KIND_CUBE &&
               near(wgf_shape3d_get_size(shape).z, 3) && wgf_shape3d_get_radius(shape) == 0.0f,
           "a cube");
    expect(!wgf_shape3d_set_sphere(shape, -1) && wgf_shape3d_get_kind(shape) == WGF_SHAPE3D_KIND_CUBE,
           "a size below 0: refused, the shape kept");
    expect(wgf_shape3d_set_sphere(shape, 2) && near(wgf_shape3d_get_radius(shape), 2) &&
               near(wgf_shape3d_get_size(shape).x, 0),
           "a sphere");
    expect(wgf_shape3d_set_rectangle(shape, 4, 5) && near(wgf_shape3d_get_size(shape).y, 5) &&
               wgf_shape3d_get_size(shape).z == 0.0f,
           "a rectangle");
    expect(wgf_shape3d_set_circle(shape, 1.5f) && wgf_shape3d_get_kind(shape) == WGF_SHAPE3D_KIND_CIRCLE, "a circle");
    expect(wgf_shape3d_set_line(shape, -1, -2, -3, 4, 5, 6), "a line, its ends any numbers");
    v = wgf_shape3d_get_line_start(shape);
    expect(near(v.x, -1) && near(v.z, -3) && near(wgf_shape3d_get_line_end(shape).y, 5), "its ends read back");
    expect(!wgf_shape3d_set_line_strip(shape, strip, 8) && wgf_shape3d_get_kind(shape) == WGF_SHAPE3D_KIND_LINE,
           "a strip of floats not in threes: refused");
    expect(wgf_shape3d_set_line_strip(shape, strip, 9) && wgf_shape3d_get_point_count(shape) == 3, "a strip of three");
    expect(wgf_shape3d_get_points(shape, back, 9) == 9 && near(back[6], 1) && near(back[7], 1),
           "its points read back into an array");
    expect(wgf_shape3d_get_points(shape, back, 4) == 4, "as many as the array holds");
    expect(wgf_shape3d_set_color(shape, WGF_COLOR_RED) && wgf_shape3d_get_color(shape) == WGF_COLOR_RED, "its color");
    plain = wgf_node_create();
    expect(!wgf_shape3d_set_cube(plain, 1, 1, 1) && wgf_shape3d_get_kind(plain) == WGF_SHAPE3D_KIND_NONE,
           "a node that isn't a 3D shape: refused");

    /* on a stage */
    stage = wgf_stage_create();
    camera = wgf_camera3d_create();
    wgf_node_set_position(camera, 0, 0, 10);
    wgf_stage_set_camera(stage, camera);
    expect(recorded(stage) == 0, "a stage of nothing records nothing");
    wgf_node_set_parent(shape, stage);
    expect(recorded(stage) == 3, "a strip of three points");
    wgf_shape3d_set_cube(shape, 1, 1, 1);
    expect(recorded(stage) == 36, "a cube: six faces of two triangles");
    wgf_node_set_visible(shape, false);
    expect(recorded(stage) == 0, "a hidden shape: nothing");
    wgf_node_set_visible(shape, true);
    wgf_node_destroy(shape, WGF_NODE_DESTROY_CHILDREN);
    shape = wgf_shape3d_create();
    wgf_node_set_parent(shape, stage);
    expect(recorded(stage) == 0, "a shape with nothing set: nothing");

    wgf_gfx_priv_stop();
    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
