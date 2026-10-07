#include <math.h>
#include <stdio.h>

#include "node/wgf_gfx_node_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_draw.h"
#include "wgf_platform_priv.h"

/* 3D cameras, look_at, and immediate mode in 3D on sokol's dummy backend: the
 * settings and their refusals, where a camera aimed with look_at sees a point, and
 * which draws record. Pixels: wgf_gfx_draw3d_web_test. */

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

/* Where `p` lands in clip space through `m`, divided by w. */
static wgf_vec3_t project(wgf_mat4_t m, float x, float y, float z)
{
    const float cx = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
    const float cy = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
    const float cz = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
    const float cw = m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15];
    return wgf_vec3_make(cx / cw, cy / cw, cz / cw);
}

int main(void)
{
    wgf_node_t camera, parent, plain;
    wgf_vec3_t position, at;
    wgf_mat4_t view_proj;
    int before;

    expect(wgf_gfx_priv_start(), "setup");
    camera = wgf_camera3d_create();
    expect(wgf_node_get_type(camera) == WGF_NODE_TYPE_CAMERA3D, "a 3D camera is a node of its type");
    expect(near(wgf_camera3d_get_fov(camera), 3.14159265f / 3.0f) && near(wgf_camera3d_get_near(camera), 0.1f) &&
               near(wgf_camera3d_get_far(camera), 1000.0f) && !wgf_camera3d_is_orthographic(camera) &&
               near(wgf_camera3d_get_ortho_height(camera), 10.0f),
           "its defaults");
    expect(wgf_camera3d_set_fov(camera, 10.0f) && near(wgf_camera3d_get_fov(camera), 3.13f) &&
               wgf_camera3d_set_fov(camera, 0.0f) && near(wgf_camera3d_get_fov(camera), 0.01f),
           "the field of view, clamped");
    expect(!wgf_camera3d_set_clip(camera, 0.0f, 10.0f) && !wgf_camera3d_set_clip(camera, 5.0f, 5.0f) &&
               wgf_camera3d_set_clip(camera, 0.5f, 50.0f) && near(wgf_camera3d_get_far(camera), 50.0f),
           "the clip planes: 0 < near < far, or refused");
    expect(!wgf_camera3d_set_ortho_height(camera, 0.0f) && wgf_camera3d_set_ortho_height(camera, 4.0f) &&
               wgf_camera3d_set_orthographic(camera, true) && wgf_camera3d_is_orthographic(camera),
           "orthographic, and its height");
    plain = wgf_node_create();
    expect(!wgf_camera3d_set_fov(plain, 1.0f) && wgf_camera3d_get_far(plain) == 0.0f,
           "a node that isn't a 3D camera: refused, and 0");

    /* aimed with look_at, the camera sees its target in the middle */
    wgf_camera3d_set_orthographic(camera, false);
    wgf_camera3d_set_fov(camera, 1.0f);
    wgf_node_set_position(camera, 10.0f, 5.0f, 10.0f);
    expect(wgf_node_look_at(camera, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f), "look_at");
    view_proj = wgf_gfx_priv_camera3d_view_projection(camera, 4.0f / 3.0f, &position);
    at = project(view_proj, 0.0f, 1.0f, 0.0f);
    expect(near(at.x, 0.0f) && near(at.y, 0.0f) && at.z > -1.0f && at.z < 1.0f, "the target, in the view's middle");
    at = project(view_proj, 0.0f, 3.0f, 0.0f);
    expect(at.y > 0.1f, "up is up");
    expect(near(position.x, 10.0f) && near(position.z, 10.0f), "where it sees from");
    expect(!wgf_node_look_at(camera, 10.0f, 5.0f, 10.0f, 0.0f, 1.0f, 0.0f), "a target where it is: refused");
    expect(!wgf_node_look_at(camera, 10.0f, 9.0f, 10.0f, 0.0f, 1.0f, 0.0f), "up along the line to it: refused");

    /* under a turned parent, it still points at the target */
    parent = wgf_node_create();
    wgf_node_set_rotation(parent, 0.0f, 1.2f, 0.0f);
    wgf_node_set_position(parent, 3.0f, 0.0f, 0.0f);
    wgf_node_set_parent(camera, parent);
    wgf_node_set_position(camera, 0.0f, 2.0f, 8.0f);
    expect(wgf_node_look_at(camera, -4.0f, 0.0f, 2.0f, 0.0f, 1.0f, 0.0f), "look_at under a parent");
    view_proj = wgf_gfx_priv_camera3d_view_projection(camera, 1.0f, &position);
    at = project(view_proj, -4.0f, 0.0f, 2.0f);
    expect(near(at.x, 0.0f) && near(at.y, 0.0f), "the target in the middle, the parent's turn taken off");

    /* immediate mode in 3D records only between begin and end, in a frame */
    expect(!wgf_draw_begin_3d(camera), "begin_3d outside a frame: refused");
    wgf_platform_priv_set_size(640, 480);
    wgf_gfx_priv_begin_frame();
    before = sgl_num_vertices();
    wgf_draw_cube(0, 0, 0, 1, 1, 1, WGF_COLOR_RED);
    expect(sgl_num_vertices() == before, "a 3D draw outside 3D: nothing");
    expect(!wgf_draw_begin_3d(plain), "begin_3d with a node that isn't a 3D camera: refused");
    expect(wgf_draw_begin_3d(camera), "begin_3d");
    wgf_draw_cube(0, 0, 0, 1, 1, 1, WGF_COLOR_RED);
    expect(sgl_num_vertices() - before == 36, "a cube: six faces of two triangles");
    before = sgl_num_vertices();
    wgf_draw_cube_wires(0, 0, 0, 1, 1, 1, WGF_COLOR_RED);
    expect(sgl_num_vertices() - before == 24, "its edges: twelve lines");
    before = sgl_num_vertices();
    wgf_draw_grid(4, 1.0f, WGF_COLOR_GRAY);
    expect(sgl_num_vertices() - before == 20, "a grid of 4: five lines each way");
    wgf_draw_line_3d(0, 0, 0, 1, 1, 1, WGF_COLOR_RED);
    wgf_draw_sphere(0, 0, 0, 1, WGF_COLOR_RED);
    wgf_draw_rectangle_3d(0, 0, 0, 1, 1, 0, 0.5f, 0, WGF_COLOR_RED);
    wgf_draw_circle_3d(0, 0, 0, 1, 0, 0, 0, WGF_COLOR_RED);
    wgf_draw_text_3d(0, "3D", 0, 2, 0, 1, WGF_COLOR_WHITE);
    wgf_draw_end_3d();
    before = sgl_num_vertices();
    wgf_draw_cube(0, 0, 0, 1, 1, 1, WGF_COLOR_RED);
    expect(sgl_num_vertices() == before, "after end_3d: nothing");
    wgf_draw_begin_3d(camera); /* left in 3D: the frame ends there, the next starts in 2D */
    wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    before = sgl_num_vertices();
    wgf_draw_cube(0, 0, 0, 1, 1, 1, WGF_COLOR_RED);
    expect(sgl_num_vertices() == before, "a new frame starts in 2D");
    wgf_gfx_priv_end_frame();

    wgf_gfx_priv_stop();
    printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
