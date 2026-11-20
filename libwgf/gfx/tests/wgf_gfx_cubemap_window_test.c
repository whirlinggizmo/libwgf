#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>

#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_environment_priv.h"
#include "texture/wgf_gfx_png_priv.h"
#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_cubemap.h"
#include "wgf_fs.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_shader.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* Cubemaps sampled by a custom shader, in a window (tools/run_in_xvfb.py, OpenGL on a
 * virtual display), their pixels read back: a plane filling a 32 by 32 canvas, drawn by the
 * tests' cube shader (libwgf/gfx/tests/shaders/cube.glsl: its cubemap `sky` sampled along
 * `dir`, unlit). Each face of the images written here is four quadrants -- its own color at
 * the top left, white at the top right, black at the bottom left, grey at the bottom right
 * -- so a face read from the wrong place, turned, or mirrored shows. Every face's three
 * quadrants are sampled along the directions the environment's cubes are made by
 * (wgf_gfx_priv_environment_cube_dir, GL's convention), for a 4 by 3 cross found by its
 * shape, a 3 by 4 one given, and six files; white with no cubemap set, and a magenta and
 * black checker for one that FAILED. */

#define FACE 4 /* a face's side, in texels */

static const wgf_color_t face_colors[6] = {0xFF0000FFu, 0x00FF00FFu, 0x0000FFFFu,
                                           0xFFFF00FFu, 0xFF00FFFFu, 0x00FFFFFFu}; /* +X ... -Z */
static const char *const face_names[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
static const char *const face_files[6] = {"cubes/px.png", "cubes/nx.png", "cubes/py.png",
                                          "cubes/ny.png", "cubes/pz.png", "cubes/nz.png"};

static int failures, frames;
static wgf_actor_t stage, plane;
static wgf_material_t material;
static wgf_cubemap_t cross, tall, six, missing;
static wgf_fs_task_t writes[9];
static int write_count;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The texel at (x, y) of face `face`: its quadrant's color. */
static wgf_color_t quadrant(int face, int x, int y)
{
    const bool right = x >= FACE / 2, bottom = y >= FACE / 2;
    if (!right && !bottom) return face_colors[face];
    if (right && !bottom) return WGF_COLOR_WHITE;
    if (!right) return WGF_COLOR_BLACK;
    return wgf_color_make(128, 128, 128, 255);
}

static void put(unsigned char *rgba, int width, int x, int y, wgf_color_t color)
{
    unsigned char *p = &rgba[((size_t)y * (size_t)width + (size_t)x) * 4];
    p[0] = wgf_color_get_red(color);
    p[1] = wgf_color_get_green(color);
    p[2] = wgf_color_get_blue(color);
    p[3] = 255;
}

static void write_png(const char *path, const unsigned char *rgba, int width, int height)
{
    int size = 0;
    unsigned char *png = wgf_gfx_priv_png_encode(rgba, width, height, &size);
    if (png == NULL) {
        expect(0, "a PNG encoded");
        return;
    }
    writes[write_count++] = wgf_fs_write(path, png, size);
    free(png);
}

/* An image of faces at `cells` (each face's column and row), `columns` by `rows` faces. */
static void write_layout(const char *path, const int cells[6][2], int columns, int rows)
{
    const int width = columns * FACE, height = rows * FACE;
    unsigned char *rgba = (unsigned char *)calloc((size_t)width * (size_t)height, 4);
    int face, x, y;
    if (rgba == NULL) return;
    for (face = 0; face < 6; face++) {
        for (y = 0; y < FACE; y++) {
            for (x = 0; x < FACE; x++) {
                put(rgba, width, cells[face][0] * FACE + x, cells[face][1] * FACE + y, quadrant(face, x, y));
            }
        }
    }
    write_png(path, rgba, width, height);
    free(rgba);
}

static void write_files(void)
{
    static const int four_by_three[6][2] = {{2, 1}, {0, 1}, {1, 0}, {1, 2}, {1, 1}, {3, 1}};
    static const int three_by_four[6][2] = {{1, 1}, {1, 3}, {1, 0}, {1, 2}, {0, 1}, {2, 1}};
    unsigned char rgba[FACE * FACE * 4];
    char shader[1 << 16];
    FILE *file = fopen(WGF_TEST_FILES "/shaders/cube.wgfshader", "rb");
    size_t size = file != NULL ? fread(shader, 1, sizeof(shader), file) : 0;
    int face, x, y;
    if (file != NULL) fclose(file);
    expect(size > 0 && size < sizeof(shader), "the tests' cube shader read");
    writes[write_count++] = wgf_fs_write("cubes/cube.wgfshader", (const unsigned char *)shader, (int)size);
    write_layout("cubes/wide.png", four_by_three, 4, 3);
    write_layout("cubes/tall.png", three_by_four, 3, 4);
    for (face = 0; face < 6; face++) {
        for (y = 0; y < FACE; y++) {
            for (x = 0; x < FACE; x++) put(rgba, FACE, x, y, quadrant(face, x, y));
        }
        write_png(face_files[face], rgba, FACE, FACE);
    }
}

/* The pixel in the middle of the canvas, within `slack` of `color`'s channels. */
static bool pixel_is(wgf_color_t color, int slack, unsigned char rgba[4])
{
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return abs(rgba[0] - (int)wgf_color_get_red(color)) <= slack &&
           abs(rgba[1] - (int)wgf_color_get_green(color)) <= slack &&
           abs(rgba[2] - (int)wgf_color_get_blue(color)) <= slack;
}

/* One frame of the stage sampling along `dir`, the frame's pixels left to read. */
static void draw(wgf_vec3_t dir)
{
    wgf_material_set_vec3(material, "dir", dir.x, dir.y, dir.z);
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
}

/* Every face of `cubemap`: its quadrants where they should be. */
static void expect_faces(wgf_cubemap_t cubemap, const char *what)
{
    static const float at[3][2] = {{0.25f, 0.25f}, {0.75f, 0.25f}, {0.75f, 0.75f}}; /* s, t: from the top left */
    int face, q;
    wgf_material_set_cubemap(material, "sky", cubemap);
    for (face = 0; face < 6; face++) {
        for (q = 0; q < 3; q++) {
            const int x = (int)(at[q][0] * FACE), y = (int)(at[q][1] * FACE);
            const wgf_color_t want = quadrant(face, x, y);
            unsigned char rgba[4];
            draw(wgf_gfx_priv_environment_cube_dir(face, at[q][0], at[q][1]));
            if (!pixel_is(want, 2, rgba)) {
                printf("FAIL: %s: face %s at %.2f, %.2f read %d %d %d, expected %d %d %d\n", what, face_names[face],
                       (double)at[q][0], (double)at[q][1], rgba[0], rgba[1], rgba[2], wgf_color_get_red(want),
                       wgf_color_get_green(want), wgf_color_get_blue(want));
                failures++;
            }
        }
    }
}

static bool pending(wgf_handle_t resource)
{
    return wgf_resource_get_status(resource) == WGF_RESOURCE_STATUS_PENDING;
}

static void on_frame(void *user)
{
    unsigned char rgba[4];
    int i;
    (void)user;
    frames++;
    if (frames > 600) {
        expect(0, "loaded within 600 frames");
        exit(1);
    }
    if (write_count > 0) { /* the files first */
        for (i = 0; i < write_count; i++) {
            if (wgf_fs_task_get_status(writes[i]) == WGF_FS_TASK_STATUS_PENDING) return;
        }
        for (i = 0; i < write_count; i++) {
            expect(wgf_fs_task_get_status(writes[i]) == WGF_FS_TASK_STATUS_DONE, "a file written");
            wgf_fs_task_destroy(writes[i]);
        }
        write_count = 0;
        {
            const wgf_shader_t shader = wgf_shader_create("cubes/cube.wgfshader");
            material = wgf_material_create_custom(shader);
            wgf_resource_release(shader); /* the material holds it */
        }
        wgf_material_set_texture_sampling(material, "sky", WGF_TEXTURE_WRAP_CLAMP, WGF_TEXTURE_WRAP_CLAMP,
                                          WGF_TEXTURE_FILTER_NEAREST); /* a texel each, no blending */
        wgf_model_set_material(plane, 0, material);
        cross = wgf_cubemap_create("cubes/wide.png", WGF_CUBEMAP_LAYOUT_AUTO);
        tall = wgf_cubemap_create("cubes/tall.png", WGF_CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR);
        six = wgf_cubemap_create_faces(face_files[0], face_files[1], face_files[2], face_files[3], face_files[4],
                                       face_files[5]);
        missing = wgf_cubemap_create("cubes/missing.png", WGF_CUBEMAP_LAYOUT_AUTO);
        return;
    }
    if (pending(wgf_material_get_shader(material)) || pending(cross) || pending(tall) || pending(six) ||
        pending(missing)) {
        return;
    }
    expect(wgf_resource_get_status(wgf_material_get_shader(material)) == WGF_RESOURCE_STATUS_READY, "the shader");
    expect(wgf_resource_get_status(cross) == WGF_RESOURCE_STATUS_READY &&
               wgf_cubemap_get_layout(cross) == WGF_CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE &&
               wgf_cubemap_get_size(cross) == FACE,
           "the 4 by 3 cross, found by its shape");
    expect(wgf_resource_get_status(tall) == WGF_RESOURCE_STATUS_READY, "the 3 by 4 cross");
    expect(wgf_resource_get_status(six) == WGF_RESOURCE_STATUS_READY, "six files");
    expect(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED, "a missing file FAILED");

    draw(wgf_vec3_make(1.0f, 0.0f, 0.0f));
    expect(pixel_is(WGF_COLOR_WHITE, 1, rgba), "white with no cubemap set");
    expect_faces(cross, "the 4 by 3 cross");
    expect_faces(tall, "the 3 by 4 cross");
    expect_faces(six, "six files");
    wgf_material_set_cubemap(material, "sky", missing);
    draw(wgf_vec3_make(0.3f, 0.2f, 1.0f));
    expect(pixel_is(wgf_color_make(255, 0, 255, 255), 1, rgba) || pixel_is(WGF_COLOR_BLACK, 1, rgba),
           "a FAILED cubemap: its checker, magenta or black");
    exit(failures == 0 ? 0 : 1);
}

static void init(void *user)
{
    (void)user;
    write_files();
}

int main(void)
{
    wgf_actor_t camera;

    wgf_window_set_size(32, 32);
    wgf_render_set_clear_color(WGF_COLOR_BLACK);
    stage = wgf_stage3d_create();
    wgf_stage3d_set_tonemap(stage, WGF_STAGE3D_TONEMAP_NONE, 0.0f);
    camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(camera, 0.9f);
    wgf_actor_set_position(camera, 0.0f, 0.0f, 5.0f);
    wgf_stage3d_set_camera(stage, camera);

    plane = wgf_model_create(wgf_mesh_create_plane(10.0f, 10.0f, 0)); /* the whole canvas */
    wgf_actor_set_rotation(plane, 1.5707963f, 0.0f, 0.0f);           /* facing the camera */
    wgf_actor_set_parent(plane, stage);

    wgf_app_run(init, NULL, on_frame, NULL, NULL);
    return 0;
}
