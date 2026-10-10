#include <math.h>
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
#include "wgf_environment.h"
#include "wgf_fs.h"
#include "wgf_material.h"
#include "wgf_mesh.h"
#include "wgf_model.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_window.h"

/* Environments' pixels, libwgt's background test and more: an equirectangular .hdr whose
 * radiance tells its direction (0.5 + 0.5 x the direction, each channel an axis). Drawn
 * as the background, the middle of the frame shows the direction the camera looks, as the
 * image's own pixels say it; blurred, it goes toward the average. Lighting a matte white
 * sphere, seen down -z: its top (facing +y) greener than its bottom, its right (+x) redder
 * than its left, and turned half a turn, the other way about; at intensity 0, black. A
 * smooth metal sphere's middle mirrors what is behind the camera (+z): bluest. WebGL2,
 * through app's runtime, the frame's pixels read back before the browser shows them. */

#define SIZE 64

static int failures;
static wgf_environment_t environment;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static unsigned char hdr[64 + 64 * 32 * 4];

static int make_hdr(void)
{
    static const char header[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n";
    const int head = (int)sizeof(header) - 1;
    int x, y;
    memcpy(hdr, header, (size_t)head);
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 64; x++) {
            /* the equirectangular convention: u = 0.5 looks down -z, u grows toward +x, v = 0 is up */
            const float phi = (((float)x + 0.5f) / 64.0f - 0.5f) * 2.0f * 3.14159265f;
            const float theta = ((float)y + 0.5f) / 32.0f * 3.14159265f;
            const float d[3] = {sinf(theta) * sinf(phi), cosf(theta), -sinf(theta) * cosf(phi)};
            const float c[3] = {0.5f + 0.5f * d[0], 0.5f + 0.5f * d[1], 0.5f + 0.5f * d[2]};
            const float top = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
            unsigned char *p = &hdr[head + (y * 64 + x) * 4];
            int e;
            const float scale = frexpf(top, &e) * 256.0f / top;
            p[0] = (unsigned char)(c[0] * scale);
            p[1] = (unsigned char)(c[1] * scale);
            p[2] = (unsigned char)(c[2] * scale);
            p[3] = (unsigned char)(e + 128);
        }
    }
    return head + 64 * 32 * 4;
}

/* A linear value as the frame stores it: sRGB encoded, 0..255. */
static int to_srgb8(float linear)
{
    const float c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * powf(linear, 1.0f / 2.4f) - 0.055f;
    return (int)(c * 255.0f + 0.5f);
}

/* The pixel at column x, row y from the top. */
static void pixel_at(int x, int y, int rgb[3])
{
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, SIZE - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    rgb[0] = rgba[0];
    rgb[1] = rgba[1];
    rgb[2] = rgba[2];
}

static void draw(wgf_actor_t stage)
{
    if (wgf_gfx_priv_is_in_frame()) wgf_gfx_priv_end_frame();
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
}

static void test_background(void)
{
    static const float looks[3][3] = {{0, 0, -1}, {1, 0, 0}, {0, 1, 0.001f}};
    static const char *names[3] = {"looking down -z", "looking down +x", "looking up"};
    const wgf_actor_t sky = wgf_stage3d_create(), eye = wgf_camera3d_create();
    int i, c, rgb[3];
    wgf_actor_set_parent(eye, sky);
    wgf_stage3d_set_camera(sky, eye);
    wgf_stage3d_set_tonemap(sky, WGF_STAGE3D_TONEMAP_NONE, 0);
    expect(wgf_stage3d_set_background(sky, environment, 0.0f), "a background");
    for (i = 0; i < 3; i++) {
        int want[3];
        bool near = true;
        wgf_actor_look_at(eye, looks[i][0], looks[i][1], looks[i][2], 0, i == 2 ? 0.0f : 1.0f, i == 2 ? -1.0f : 0.0f);
        draw(sky);
        pixel_at(SIZE / 2, SIZE / 2, rgb);
        for (c = 0; c < 3; c++) {
            want[c] = to_srgb8(0.5f + 0.5f * looks[i][c]);
            near = near && abs(rgb[c] - want[c]) <= 6;
        }
        if (!near) printf("  %s: %d %d %d, not %d %d %d\n", names[i], rgb[0], rgb[1], rgb[2], want[0], want[1], want[2]);
        expect(near, names[i]);
    }
    /* fully blurred: looking up, toward the average (0.5 a channel) */
    wgf_stage3d_set_background(sky, environment, 1.0f);
    draw(sky);
    pixel_at(SIZE / 2, SIZE / 2, rgb);
    expect(rgb[1] < to_srgb8(0.95f) && abs(rgb[0] - rgb[2]) <= 12, "blurred: toward the average");
    wgf_actor_destroy(sky, WGF_ACTOR_DESTROY_CHILDREN);
}

/* A stage with a sphere of radius 1 filling most of the frame, seen down -z, lit by the
 * environment alone. */
static wgf_actor_t sphere_stage(float metallic, float roughness)
{
    const wgf_actor_t stage = wgf_stage3d_create(), eye = wgf_camera3d_create();
    const wgf_mesh_t mesh = wgf_mesh_create_sphere(1.0f, 32, 48);
    const wgf_material_t material = wgf_material_create(WGF_MATERIAL_SHADING_PBR);
    const wgf_actor_t ball = wgf_model_create(mesh);
    wgf_material_set_float(material, "metallic", metallic);
    wgf_material_set_float(material, "roughness", roughness);
    wgf_model_set_material(ball, 0, material);
    wgf_resource_release(material);
    wgf_resource_release(mesh);
    wgf_camera3d_set_orthographic(eye, true);
    wgf_camera3d_set_ortho_height(eye, 2.5f);
    wgf_actor_set_position(eye, 0, 0, 5);
    wgf_actor_look_at(eye, 0, 0, 0, 0, 1, 0);
    wgf_actor_set_parent(eye, stage);
    wgf_stage3d_set_camera(stage, eye);
    wgf_stage3d_set_tonemap(stage, WGF_STAGE3D_TONEMAP_NONE, 0);
    wgf_actor_set_parent(ball, stage);
    return stage;
}

static void test_lighting(void)
{
    const int edge = (int)(SIZE / 2 * 0.7f); /* 0.7 of the radius out: well on the sphere */
    wgf_actor_t stage = sphere_stage(0.0f, 1.0f);
    int top[3], bottom[3], left[3], right[3], middle[3];

    expect(wgf_stage3d_set_environment(stage, environment, 1.0f, 0.0f), "an environment");
    draw(stage);
    pixel_at(SIZE / 2, SIZE / 2 - edge, top);
    pixel_at(SIZE / 2, SIZE / 2 + edge, bottom);
    pixel_at(SIZE / 2 - edge, SIZE / 2, left);
    pixel_at(SIZE / 2 + edge, SIZE / 2, right);
    printf("matte: top %d %d %d, bottom %d %d %d, left %d %d %d, right %d %d %d\n", top[0], top[1], top[2], bottom[0],
           bottom[1], bottom[2], left[0], left[1], left[2], right[0], right[1], right[2]);
    expect(top[1] > bottom[1] + 20, "matte: its top, facing up, greener than its bottom");
    expect(right[0] > left[0] + 20, "matte: its right, facing +x, redder than its left");
    expect(top[0] > 60 && bottom[1] > 30, "matte: lit all over");

    wgf_stage3d_set_environment(stage, environment, 1.0f, 3.14159265f);
    draw(stage);
    pixel_at(SIZE / 2 - edge, SIZE / 2, left);
    pixel_at(SIZE / 2 + edge, SIZE / 2, right);
    expect(left[0] > right[0] + 20, "turned half a turn: its left redder");

    wgf_stage3d_set_environment(stage, environment, 0.0f, 0.0f);
    draw(stage);
    pixel_at(SIZE / 2, SIZE / 2, middle);
    expect(middle[0] < 8 && middle[1] < 8 && middle[2] < 8, "at intensity 0: black");
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);

    stage = sphere_stage(1.0f, 0.0f);
    wgf_stage3d_set_environment(stage, environment, 1.0f, 0.0f);
    draw(stage);
    pixel_at(SIZE / 2, SIZE / 2, middle);
    printf("mirror: middle %d %d %d\n", middle[0], middle[1], middle[2]);
    expect(middle[2] > middle[0] + 30 && middle[2] > middle[1] + 30, "a smooth metal mirrors what is behind the camera");
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
}

/* The .hdr written to a file and loaded, a frame at a time, then the test. */
static void on_frame(void *user)
{
    static int frames;
    static wgf_fs_task_t write;
    (void)user;
    if (++frames == 1) {
        write = wgf_fs_write("directions.hdr", hdr, make_hdr());
    } else if (environment == 0 && wgf_fs_task_get_status(write) != WGF_FS_TASK_STATUS_PENDING) {
        wgf_fs_task_destroy(write);
        environment = wgf_environment_create("directions.hdr");
    } else if (environment != 0 && wgf_resource_get_status(environment) != WGF_RESOURCE_STATUS_PENDING) {
        expect(wgf_resource_get_status(environment) == WGF_RESOURCE_STATUS_READY, "the environment loaded");
        test_background();
        test_lighting();
        wgf_resource_release(environment);
        emscripten_force_exit(failures == 0 ? 0 : 1);
    } else if (frames > 6000) {
        printf("FAIL: the environment never loaded\n");
        emscripten_force_exit(1);
    }
}

int main(void)
{
    wgf_window_set_size(SIZE, SIZE);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
