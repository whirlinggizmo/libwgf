#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GLES3/gl3.h>
#include <emscripten.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_app.h"
#include "wgf_camera3d.h"
#include "wgf_color.h"
#include "wgf_fs.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_render.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* glTF files, pixel by pixel, in a browser (tools/run_in_browser.py): a square in a .gltf,
 * its buffer and its image (a red PNG) files beside it, drawn unlit with its texture; the
 * same square in a .glb, unlit green; and a missing file, the placeholder checker on a
 * cube. A frame loop, since the web store opens between frames. */

static int failures, frames, step;
static wgf_fs_task_t tasks[4];
static wgf_mesh_t meshes[3]; /* the .gltf, the .glb, the missing one */
static wgf_actor_t stage;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* A 1 by 1 PNG, red. */
static const unsigned char red_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
    0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

/* A unit square facing +z: its corners' positions (48 bytes), texture coordinates (32), and
 * two triangles' indices (12). */
static unsigned char square[92];

static void make_square(void)
{
    static const float positions[12] = {-0.5f, -0.5f, 0, 0.5f, -0.5f, 0, 0.5f, 0.5f, 0, -0.5f, 0.5f, 0};
    static const float uvs[8] = {0, 1, 1, 1, 1, 0, 0, 0};
    static const unsigned short indices[6] = {0, 1, 2, 0, 2, 3};
    memcpy(square, positions, sizeof(positions));
    memcpy(square + 48, uvs, sizeof(uvs));
    memcpy(square + 80, indices, sizeof(indices));
}

/* The square's glTF, its buffer named by `buffer` and its material by `material`. */
#define SQUARE_JSON(buffer, material)                                                                                  \
    "{\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"KHR_materials_unlit\"],"                                \
    "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"name\":\"square\",\"mesh\":0}],"                          \
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":2,"                   \
    "\"material\":0}]}],\"materials\":[{\"extensions\":{\"KHR_materials_unlit\":{}}," material "}],"                  \
    "\"buffers\":[{" buffer "\"byteLength\":92}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":48},"                  \
    "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":80,\"byteLength\":12}],"        \
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\","                          \
    "\"min\":[-0.5,-0.5,0],\"max\":[0.5,0.5,0]},{\"bufferView\":1,\"componentType\":5126,\"count\":4,"               \
    "\"type\":\"VEC2\"},{\"bufferView\":2,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]"
static const char gltf[] = SQUARE_JSON(
    "\"uri\":\"square.bin\",",
    "\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}") ",\"textures\":[{\"source\":0,\"sampler\":0}],"
    "\"samplers\":[{\"magFilter\":9728,\"minFilter\":9728}],\"images\":[{\"uri\":\"red.png\"}]}";
static const char glb_json[] = SQUARE_JSON("", "\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,1,0,1]}") "}";

static void put32(unsigned char *at, unsigned int value)
{
    at[0] = (unsigned char)(value & 0xFF);
    at[1] = (unsigned char)((value >> 8) & 0xFF);
    at[2] = (unsigned char)((value >> 16) & 0xFF);
    at[3] = (unsigned char)((value >> 24) & 0xFF);
}

/* The .glb: its header, its JSON chunk (padded with spaces), its BIN chunk. */
static wgf_fs_task_t write_glb(const char *path)
{
    static unsigned char glb[4096];
    const unsigned int json_size = (unsigned int)((sizeof(glb_json) - 1 + 3) & ~3u);
    const unsigned int total = 12 + 8 + json_size + 8 + (unsigned int)sizeof(square);
    memset(glb, ' ', sizeof(glb));
    memcpy(glb, "glTF", 4);
    put32(glb + 4, 2);
    put32(glb + 8, total);
    put32(glb + 12, json_size);
    memcpy(glb + 16, "JSON", 4);
    memcpy(glb + 20, glb_json, sizeof(glb_json) - 1);
    put32(glb + 20 + json_size, (unsigned int)sizeof(square));
    memcpy(glb + 24 + json_size, "BIN\0", 4);
    memcpy(glb + 28 + json_size, square, sizeof(square));
    return wgf_fs_write(path, glb, (int)total);
}

/* The pixel at (x, y) from the top-left of the 32-pixel-high canvas. */
static void read_pixel(int x, int y, unsigned char rgba[4])
{
    glReadPixels(x, 31 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static void expect_pixel(int x, int y, wgf_color_t color, const char *what)
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    read_pixel(x, y, rgba);
    if (rgba[0] != wgf_color_get_red(color) || rgba[1] != wgf_color_get_green(color) ||
        rgba[2] != wgf_color_get_blue(color)) {
        printf("FAIL: %s: at %d, %d read %d %d %d, expected %d %d %d\n", what, x, y, rgba[0], rgba[1], rgba[2],
               wgf_color_get_red(color), wgf_color_get_green(color), wgf_color_get_blue(color));
        failures++;
    }
}

static void finish_test(void)
{
    int i;
    for (i = 0; i < 3; i++) wgf_resource_release(meshes[i]);
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void draw_and_check(void)
{
    static const float places[3] = {-1.5f, 1.5f, 0.0f};
    int i;
    for (i = 0; i < 3; i++) {
        const wgf_actor_t model = wgf_model_create(meshes[i]);
        wgf_actor_set_position(model, places[i], 0, 0);
        wgf_actor_set_parent(model, stage);
    }
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
    expect_pixel(22, 16, wgf_color_make(255, 0, 0, 255), "the .gltf's square: its image, from the file beside it");
    expect_pixel(42, 16, wgf_color_make(0, 255, 0, 255), "the .glb's square: its color");
    expect_pixel(60, 4, WGF_COLOR_DARKGRAY, "the clear color past them");
    {
        unsigned char rgba[4] = {0, 0, 0, 0};
        read_pixel(32, 16, rgba);
        expect(rgba[0] == rgba[2] && rgba[1] == 0 && (rgba[0] == 255 || rgba[0] == 20),
               "the missing file: the placeholder's checker, magenta or near black");
    }
}

static void on_frame(void *user)
{
    int i, pending = 0;
    (void)user;
    if (++frames > 600) {
        printf("FAIL: still waiting at step %d\n", step);
        failures++;
        finish_test();
        return;
    }
    if (step == 0) {
        make_square();
        tasks[0] = wgf_fs_write("models/square.gltf", (const unsigned char *)gltf, (int)sizeof(gltf) - 1);
        tasks[1] = wgf_fs_write("models/square.bin", square, (int)sizeof(square));
        tasks[2] = wgf_fs_write("models/red.png", red_png, (int)sizeof(red_png));
        tasks[3] = write_glb("models/square.glb");
        step++;
        return;
    }
    if (step == 1) {
        for (i = 0; i < 4; i++) pending += wgf_fs_task_get_status(tasks[i]) == WGF_FS_TASK_STATUS_PENDING;
        if (pending > 0) return;
        for (i = 0; i < 4; i++) {
            expect(wgf_fs_task_get_status(tasks[i]) == WGF_FS_TASK_STATUS_DONE, "a file written");
            wgf_fs_task_destroy(tasks[i]);
        }
        meshes[0] = wgf_mesh_create("models/square.gltf");
        meshes[1] = wgf_mesh_create("models/square.glb");
        meshes[2] = wgf_mesh_create("models/missing.glb");
        step++;
        return;
    }
    for (i = 0; i < 3; i++) pending += wgf_resource_get_status(meshes[i]) == WGF_RESOURCE_STATUS_PENDING;
    if (pending > 0) return;
    expect(wgf_resource_get_status(meshes[0]) == WGF_RESOURCE_STATUS_READY, "the .gltf loaded");
    expect(wgf_resource_get_status(meshes[1]) == WGF_RESOURCE_STATUS_READY, "the .glb loaded");
    expect(wgf_resource_get_status(meshes[2]) == WGF_RESOURCE_STATUS_FAILED, "the missing file failed");
    draw_and_check();
    finish_test();
}

int main(void)
{
    wgf_actor_t camera;
    wgf_window_set_size(64, 32);
    wgf_render_set_clear_color(WGF_COLOR_DARKGRAY);
    stage = wgf_stage3d_create();
    wgf_stage3d_set_tonemap(stage, WGF_STAGE3D_TONEMAP_NONE, 0.0f);
    camera = wgf_camera3d_create();
    wgf_camera3d_set_fov(camera, 0.9f);
    wgf_actor_set_position(camera, 0.0f, 0.0f, 5.0f);
    wgf_stage3d_set_camera(stage, camera);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
