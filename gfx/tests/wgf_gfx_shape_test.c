#include <math.h>
#include <stdio.h>

#include "node/wgf_gfx_node_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "util/sokol_gl.h"
#include "wgf_canvas.h"
#include "wgf_core_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_shape2d.h"

/* 2D shapes on sokol's dummy backend (a headless build): each kind set and read back,
 * the refusals, a polygon's points copied and read back through the caller's array,
 * and what a canvas draws of each. Pixels: wgf_gfx_canvas_web_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The vertices a canvas holding only `shape` records. */
static int drawn(wgf_node_t canvas)
{
    int before;
    wgf_platform_priv_set_size(320, 240);
    wgf_platform_priv_headless_set_dpi_scale(1.0f);
    wgf_gfx_priv_begin_frame();
    before = sgl_num_vertices();
    wgf_canvas_draw(canvas);
    before = sgl_num_vertices() - before;
    wgf_gfx_priv_end_frame();
    return before;
}

int main(void)
{
    const float rock[10] = {0, -20, 18, -6, 12, 16, -12, 16, -18, -6};
    float out[10] = {0};
    wgf_node_t canvas, shape;

    wgf_core_priv_init();
    expect(wgf_gfx_priv_start(), "setup");
    canvas = wgf_canvas_create();
    shape = wgf_shape2d_create();
    wgf_node_set_parent(shape, canvas);

    expect(wgf_node_get_type(shape) == WGF_NODE_TYPE_SHAPE2D && wgf_shape2d_get_kind(shape) == WGF_SHAPE2D_KIND_NONE,
           "a shape of no kind yet");
    expect(wgf_shape2d_get_color(shape) == 0xFFFFFFFFu && wgf_shape2d_get_outline(shape) == 0.0f, "white, filled");
    expect(drawn(canvas) == 0, "which draws nothing");

    expect(wgf_shape2d_set_rectangle(shape, 40, 20) && wgf_shape2d_get_kind(shape) == WGF_SHAPE2D_KIND_RECTANGLE &&
               wgf_shape2d_get_size(shape).x == 40 && wgf_shape2d_get_size(shape).y == 20,
           "a rectangle");
    expect(wgf_shape2d_get_pivot(shape).x == 0.0f, "its own origin: its top-left");
    expect(drawn(canvas) == 6, "filled: two triangles");
    expect(wgf_shape2d_set_outline(shape, 2) && wgf_shape2d_get_outline(shape) == 2 && drawn(canvas) == 24,
           "outlined: four sides of two triangles");
    expect(wgf_shape2d_set_outline(shape, -1) && wgf_shape2d_get_outline(shape) == 0, "an outline below 0 is clamped");
    expect(!wgf_shape2d_set_rectangle(shape, -1, 5) && wgf_shape2d_get_size(shape).x == 40, "a negative size is refused");

    expect(wgf_shape2d_set_circle(shape, 10) && wgf_shape2d_get_kind(shape) == WGF_SHAPE2D_KIND_CIRCLE &&
               wgf_shape2d_get_radius(shape) == 10 && wgf_shape2d_get_size(shape).x == 0,
           "a circle, and the rectangle's size gone");
    expect(wgf_shape2d_get_pivot(shape).x == 0.5f, "its own origin: its center");
    {
        const int small = drawn(canvas);
        wgf_shape2d_set_circle(shape, 200);
        expect(small > 0 && drawn(canvas) > small, "a larger circle gets more segments");
        wgf_node_set_scale(shape, 0.05f, 0.05f, 1);
        expect(drawn(canvas) < small * 2, "and fewer again when its node scales it down");
        wgf_node_set_scale(shape, 1, 1, 1);
    }
    expect(!wgf_shape2d_set_circle(shape, -1), "a negative radius is refused");

    expect(wgf_shape2d_set_line(shape, 1, 2, 30, 40) && wgf_shape2d_get_kind(shape) == WGF_SHAPE2D_KIND_LINE &&
               wgf_shape2d_get_line_start(shape).y == 2 && wgf_shape2d_get_line_end(shape).x == 30,
           "a line");
    expect(drawn(canvas) == 6, "a line: two triangles");

    expect(wgf_shape2d_set_polygon(shape, rock, 10) && wgf_shape2d_get_kind(shape) == WGF_SHAPE2D_KIND_POLYGON &&
               wgf_shape2d_get_point_count(shape) == 5,
           "a polygon of five points");
    expect(wgf_shape2d_get_points(shape, out, 10) == 10 && out[0] == 0 && out[1] == -20 && out[9] == -6,
           "its points read back into the caller's array");
    expect(wgf_shape2d_get_points(shape, out, 4) == 4, "as many as fit");
    expect(drawn(canvas) == 3 * 3, "filled: three triangles");
    wgf_shape2d_set_outline(shape, 1.5f);
    expect(drawn(canvas) == 5 * 6, "outlined, closed: five segments");
    expect(!wgf_shape2d_set_polygon(shape, rock, 4) && !wgf_shape2d_set_polygon(shape, rock, 9) &&
               !wgf_shape2d_set_polygon(shape, NULL, 10),
           "a polygon refuses fewer than three points, an odd count, and NULL");
    expect(wgf_shape2d_get_point_count(shape) == 5, "and keeps what it had");
    wgf_shape2d_set_circle(shape, 3);
    expect(wgf_shape2d_get_point_count(shape) == 0 && wgf_shape2d_get_points(shape, out, 10) == 0,
           "another kind drops the points");

    expect(wgf_shape2d_set_pivot(shape, 0, 0) && wgf_shape2d_get_pivot(shape).x == 0, "a pivot set");
    expect(wgf_shape2d_set_color(shape, 0x12345678u) && wgf_shape2d_get_color(shape) == 0x12345678u, "a color");
    wgf_node_set_visible(shape, false);
    expect(drawn(canvas) == 0, "a hidden shape draws nothing");

    {
        const wgf_node_t plain = wgf_node_create();
        expect(!wgf_shape2d_set_circle(plain, 1) && wgf_shape2d_get_kind(plain) == WGF_SHAPE2D_KIND_NONE &&
                   !wgf_shape2d_set_color(12345, 0) && wgf_shape2d_get_radius(12345) == 0,
               "every call refuses a node that isn't a shape");
        wgf_node_destroy(plain, WGF_NODE_DESTROY_CHILDREN);
    }
    wgf_node_destroy(canvas, WGF_NODE_DESTROY_CHILDREN);
    expect(wgf_shape2d_get_kind(shape) == WGF_SHAPE2D_KIND_NONE, "gone with its canvas");
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
