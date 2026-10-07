#include "wgf_draw.h"

#include <math.h>
#include <stddef.h>

#include "draw/wgf_gfx_draw3d_priv.h"
#include "node/wgf_gfx_node_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "sokol_gfx.h" /* sokol_gl needs it first */
#include "text/wgf_gfx_font_priv.h"
#include "util/sokol_gl.h"

/* Immediate mode in 3D, libwgt's (wgrender's shape3d geometry): recorded into the
 * frame's sokol_gl recording through a 3D camera's view, depth tested; its geometry is
 * also a stage's 3D shape nodes' (wgf_gfx_shape3d.c). Its own file, so a program drawing
 * only in 2D links none of it. */

#define PI 3.14159265358979323846f
#define CIRCLE_SEGMENTS 36

/* The camera 3D calls draw through until end_3d, in the frame it was begun in; 0 in 2D. */
static wgf_node_t camera_3d;
static unsigned camera_3d_frame;

static bool in_3d(void)
{
    return wgf_gfx_priv_is_in_frame() && camera_3d != 0 && camera_3d_frame == wgf_gfx_priv_render_get_frame() &&
           wgf_node_get_type(camera_3d) == WGF_NODE_TYPE_CAMERA3D;
}

bool wgf_draw_begin_3d(wgf_node_t camera)
{
    float x, y, width, height;
    wgf_vec3_t position;
    wgf_mat4_t view_proj;
    if (!wgf_gfx_priv_is_in_frame() || wgf_node_get_type(camera) != WGF_NODE_TYPE_CAMERA3D) return false;
    wgf_gfx_priv_render_get_visible(&x, &y, &width, &height);
    view_proj = wgf_gfx_priv_camera3d_view_projection(camera, height > 0.0f ? width / height : 1.0f, &position);
    wgf_gfx_priv_render_set_3d(view_proj.m);
    camera_3d = camera;
    camera_3d_frame = wgf_gfx_priv_render_get_frame();
    return true;
}

void wgf_draw_end_3d(void)
{
    if (in_3d()) wgf_gfx_priv_render_end_3d();
    camera_3d = 0;
}

static void set_color(wgf_color_t color)
{
    sgl_c4b((uint8_t)wgf_color_get_red(color), (uint8_t)wgf_color_get_green(color), (uint8_t)wgf_color_get_blue(color),
            (uint8_t)wgf_color_get_alpha(color));
}

void wgf_gfx_priv_draw3d_line(float x0, float y0, float z0, float x1, float y1, float z1, wgf_color_t color)
{
    sgl_begin_lines();
    set_color(color);
    sgl_v3f(x0, y0, z0);
    sgl_v3f(x1, y1, z1);
    sgl_end();
}

void wgf_draw_line_3d(float x0, float y0, float z0, float x1, float y1, float z1, wgf_color_t color)
{
    if (in_3d()) wgf_gfx_priv_draw3d_line(x0, y0, z0, x1, y1, z1, color);
}

void wgf_gfx_priv_draw3d_cube(float cx, float cy, float cz, float width, float height, float length,
                              wgf_color_t color)
{
    const float x0 = cx - width * 0.5f, x1 = cx + width * 0.5f;
    const float y0 = cy - height * 0.5f, y1 = cy + height * 0.5f;
    const float z0 = cz - length * 0.5f, z1 = cz + length * 0.5f;
    /* each face's corners counterclockwise seen from outside: +z, -z, +x, -x, +y, -y */
    const float faces[6][4][3] = {
        {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}},
        {{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}},
        {{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}},
        {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}},
        {{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}},
        {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}},
    };
    int f, c;
    sgl_begin_quads();
    set_color(color);
    for (f = 0; f < 6; f++) {
        for (c = 0; c < 4; c++) sgl_v3f(faces[f][c][0], faces[f][c][1], faces[f][c][2]);
    }
    sgl_end();
}

void wgf_draw_cube(float cx, float cy, float cz, float width, float height, float length, wgf_color_t color)
{
    if (in_3d()) wgf_gfx_priv_draw3d_cube(cx, cy, cz, width, height, length, color);
}

void wgf_draw_cube_wires(float cx, float cy, float cz, float width, float height, float length, wgf_color_t color)
{
    const float x[2] = {cx - width * 0.5f, cx + width * 0.5f};
    const float y[2] = {cy - height * 0.5f, cy + height * 0.5f};
    const float z[2] = {cz - length * 0.5f, cz + length * 0.5f};
    int i;
    if (!in_3d()) return;
    sgl_begin_lines();
    set_color(color);
    for (i = 0; i < 4; i++) { /* the four edges along each axis */
        const int a = i & 1, b = i >> 1;
        sgl_v3f(x[0], y[a], z[b]);
        sgl_v3f(x[1], y[a], z[b]);
        sgl_v3f(x[a], y[0], z[b]);
        sgl_v3f(x[a], y[1], z[b]);
        sgl_v3f(x[a], y[b], z[0]);
        sgl_v3f(x[a], y[b], z[1]);
    }
    sgl_end();
}

void wgf_gfx_priv_draw3d_sphere(float cx, float cy, float cz, float radius, wgf_color_t color)
{
    const int rings = 16, sectors = 24;
    int r, s;
    sgl_begin_triangles();
    set_color(color);
    for (r = 0; r < rings; r++) {
        const float phi0 = PI * (float)r / (float)rings - PI * 0.5f;
        const float phi1 = PI * (float)(r + 1) / (float)rings - PI * 0.5f;
        const float y0 = cy + radius * sinf(phi0), y1 = cy + radius * sinf(phi1);
        const float c0 = radius * cosf(phi0), c1 = radius * cosf(phi1);
        for (s = 0; s < sectors; s++) {
            const float th0 = 2.0f * PI * (float)s / (float)sectors;
            const float th1 = 2.0f * PI * (float)(s + 1) / (float)sectors;
            const float ax = cosf(th0), az = sinf(th0), bx = cosf(th1), bz = sinf(th1);
            sgl_v3f(cx + c0 * ax, y0, cz + c0 * az);
            sgl_v3f(cx + c1 * ax, y1, cz + c1 * az);
            sgl_v3f(cx + c1 * bx, y1, cz + c1 * bz);
            sgl_v3f(cx + c0 * ax, y0, cz + c0 * az);
            sgl_v3f(cx + c1 * bx, y1, cz + c1 * bz);
            sgl_v3f(cx + c0 * bx, y0, cz + c0 * bz);
        }
    }
    sgl_end();
}

void wgf_draw_sphere(float cx, float cy, float cz, float radius, wgf_color_t color)
{
    if (in_3d()) wgf_gfx_priv_draw3d_sphere(cx, cy, cz, radius, color);
}

void wgf_draw_grid(int slices, float spacing, wgf_color_t color)
{
    const float half = (float)slices * spacing * 0.5f;
    int i;
    if (!in_3d() || slices < 1) return;
    sgl_begin_lines();
    set_color(color);
    for (i = 0; i <= slices; i++) {
        const float p = -half + (float)i * spacing;
        sgl_v3f(p, 0.0f, -half);
        sgl_v3f(p, 0.0f, half);
        sgl_v3f(-half, 0.0f, p);
        sgl_v3f(half, 0.0f, p);
    }
    sgl_end();
}

/* An immediate primitive's own transform: its center, then its angles (radians), as a
 * node's rotation turns it (x, then y, then z). */
static void push_placement(float cx, float cy, float cz, float rx, float ry, float rz)
{
    sgl_matrix_mode_modelview();
    sgl_push_matrix();
    sgl_translate(cx, cy, cz);
    sgl_rotate(rz, 0.0f, 0.0f, 1.0f);
    sgl_rotate(ry, 0.0f, 1.0f, 0.0f);
    sgl_rotate(rx, 1.0f, 0.0f, 0.0f);
}

void wgf_gfx_priv_draw3d_rectangle(float width, float height, wgf_color_t color)
{
    const float hw = width * 0.5f, hh = height * 0.5f;
    sgl_begin_quads();
    set_color(color);
    sgl_v3f(-hw, -hh, 0.0f);
    sgl_v3f(hw, -hh, 0.0f);
    sgl_v3f(hw, hh, 0.0f);
    sgl_v3f(-hw, hh, 0.0f);
    sgl_end();
}

void wgf_gfx_priv_draw3d_circle(float radius, wgf_color_t color)
{
    int i;
    sgl_begin_line_strip();
    set_color(color);
    for (i = 0; i <= CIRCLE_SEGMENTS; i++) {
        const float a = 2.0f * PI * (float)i / CIRCLE_SEGMENTS;
        sgl_v3f(cosf(a) * radius, sinf(a) * radius, 0.0f);
    }
    sgl_end();
}

void wgf_gfx_priv_draw3d_strip(const float *points, int count, wgf_color_t color)
{
    int i;
    if (count < 2) return;
    sgl_begin_line_strip();
    set_color(color);
    for (i = 0; i < count; i++) sgl_v3f(points[i * 3], points[i * 3 + 1], points[i * 3 + 2]);
    sgl_end();
}

void wgf_draw_rectangle_3d(float cx, float cy, float cz, float width, float height, float rx, float ry, float rz,
                           wgf_color_t color)
{
    if (!in_3d()) return;
    push_placement(cx, cy, cz, rx, ry, rz);
    wgf_gfx_priv_draw3d_rectangle(width, height, color);
    sgl_pop_matrix();
}

void wgf_draw_circle_3d(float cx, float cy, float cz, float radius, float rx, float ry, float rz, wgf_color_t color)
{
    if (!in_3d()) return;
    push_placement(cx, cy, cz, rx, ry, rz);
    wgf_gfx_priv_draw3d_circle(radius, color);
    sgl_pop_matrix();
}

void wgf_draw_text_3d(wgf_font_t font, const char *text, float x, float y, float z, float size, wgf_color_t color)
{
    wgf_mat4_t world, m;
    if (!in_3d() || text == NULL) return;
    /* the camera's turn, so the block faces it; y down the block, as text runs */
    world = wgf_gfx_priv_node_get_world_matrix(camera_3d);
    m = wgf_mat4_from_trs(wgf_vec3_make(x, y, z), wgf_mat4_get_rotation(world), wgf_vec3_make(1.0f, -1.0f, 1.0f));
    wgf_gfx_priv_font_draw_block_3d(font, text, size > 0.0f ? size : 1.0f, color, 0.0f, WGF_TEXT_HALIGN_CENTER,
                                    WGF_TEXT_VALIGN_MIDDLE, m.m);
}
