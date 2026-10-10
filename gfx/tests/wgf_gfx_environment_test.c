#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/wgf_gfx_brdf_lut.h"
#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_environment_priv.h"
#include "wgf_actor.h"
#include "wgf_asset.h"
#include "wgf_camera3d.h"
#include "wgf_core_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_environment.h"
#include "wgf_fs.h"
#include "wgf_log.h"
#include "wgf_platform_priv.h"
#include "wgf_resource.h"
#include "wgf_stage3d.h"
#include "wgf_time.h"

/* Environments, as libwgt's test (wgrender's checks): the CPU preparation -- the direction
 * mappings, irradiance, prefiltering, half floats -- and the baked BRDF table against the
 * values it is known to have; then, on sokol's dummy backend (a headless build), the
 * resource -- a .hdr loaded, shared by its path, a missing file FAILED, loaded again in
 * place, released while it loads -- and a stage's hold on one, and a stage drawn with one.
 * Pixels: wgf_gfx_environment_web_test. */

static int failures;

static void check(int ok, const char *what, int line)
{
    if (!ok) {
        printf("FAIL: line %d: %s\n", line, what);
        failures++;
    }
}

#define CHECK(c) check((c) ? 1 : 0, #c, __LINE__)
#define CHECK_NEAR(a, b, eps) check(fabs((double)(a) - (double)(b)) <= (double)(eps), #a " near " #b, __LINE__)
#define CHECK_VEC3_NEAR(v, ex, ey, ez, eps)                                                                      \
    do {                                                                                                         \
        const wgf_vec3_t v_ = (v);                                                                               \
        check(fabsf(v_.x - (float)(ex)) <= (eps) && fabsf(v_.y - (float)(ey)) <= (eps) &&                         \
                  fabsf(v_.z - (float)(ez)) <= (eps),                                                            \
              #v " near (" #ex ", " #ey ", " #ez ")", __LINE__);                                                 \
    } while (0)
#define EPS 1e-3f

/* deterministic pseudo-random unit vectors */
static wgf_vec3_t random_dir(unsigned *state)
{
    for (;;) {
        wgf_vec3_t v;
        float c[3];
        int i;
        for (i = 0; i < 3; i++) {
            *state = *state * 1664525u + 1013904223u;
            c[i] = ((float)(*state >> 8) / 16777216.0f) * 2.0f - 1.0f;
        }
        v = wgf_vec3_make(c[0], c[1], c[2]);
        if (wgf_vec3_dot(v, v) > 0.01f && wgf_vec3_dot(v, v) <= 1.0f) return wgf_vec3_normalize(v);
    }
}

/* an equirect image filled from a function of direction */
static wgf_gfx_priv_env_image_t make_image(int width, int height, wgf_vec3_t (*fn)(wgf_vec3_t))
{
    wgf_gfx_priv_env_image_t image;
    int x, y;
    image.width = width;
    image.height = height;
    image.rgb = (float *)malloc((size_t)width * (size_t)height * 3 * sizeof(float));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const wgf_vec3_t c = fn(wgf_gfx_priv_environment_equirect_dir(((float)x + 0.5f) / (float)width,
                                                                          ((float)y + 0.5f) / (float)height));
            float *p = &image.rgb[((size_t)y * (size_t)width + (size_t)x) * 3];
            p[0] = c.x;
            p[1] = c.y;
            p[2] = c.z;
        }
    }
    return image;
}

static wgf_vec3_t constant_two(wgf_vec3_t d)
{
    (void)d;
    return wgf_vec3_make(2, 2, 2);
}

static wgf_vec3_t sky(wgf_vec3_t d)
{
    const float c = d.y > 0 ? d.y : 0;
    return wgf_vec3_make(c, c, c);
}

static wgf_vec3_t smooth(wgf_vec3_t d)
{
    return wgf_vec3_make(1.0f + 0.5f * d.x, 1.0f + 0.5f * d.y, 1.0f + 0.5f * d.z);
}

static void test_mapping(void)
{
    unsigned state = 7;
    wgf_gfx_priv_env_image_t image;
    wgf_gfx_priv_env_cube_t cube;
    int i;

    /* the equirect centre looks down -z, v = 0 straight up */
    CHECK_VEC3_NEAR(wgf_gfx_priv_environment_equirect_dir(0.5f, 0.5f), 0, 0, -1, EPS);
    CHECK_VEC3_NEAR(wgf_gfx_priv_environment_equirect_dir(0.75f, 0.5f), 1, 0, 0, EPS);
    CHECK_NEAR(wgf_gfx_priv_environment_equirect_dir(0.3f, 0.0f).y, 1, EPS);
    for (i = 0; i < 200; i++) {
        const wgf_vec3_t d = random_dir(&state);
        float u, v, s, t;
        int face;
        wgf_gfx_priv_environment_dir_equirect(d, &u, &v);
        CHECK_VEC3_NEAR(wgf_gfx_priv_environment_equirect_dir(u, v), d.x, d.y, d.z, EPS);
        wgf_gfx_priv_environment_dir_cube(d, &face, &s, &t);
        CHECK(face >= 0 && face < 6 && s >= 0 && s <= 1 && t >= 0 && t <= 1);
        CHECK_VEC3_NEAR(wgf_gfx_priv_environment_cube_dir(face, s, t), d.x, d.y, d.z, EPS);
    }
    /* face order +X -X +Y -Y +Z -Z (sokol's, OpenGL's) */
    CHECK_VEC3_NEAR(wgf_gfx_priv_environment_cube_dir(0, 0.5f, 0.5f), 1, 0, 0, EPS);
    CHECK_VEC3_NEAR(wgf_gfx_priv_environment_cube_dir(3, 0.5f, 0.5f), 0, -1, 0, EPS);
    CHECK_VEC3_NEAR(wgf_gfx_priv_environment_cube_dir(5, 0.5f, 0.5f), 0, 0, -1, EPS);
    /* +X face: s runs toward -z, t toward -y (OpenGL's cube map orientation) */
    CHECK(wgf_gfx_priv_environment_cube_dir(0, 1.0f, 0.5f).z < -0.5f);
    CHECK(wgf_gfx_priv_environment_cube_dir(0, 0.5f, 1.0f).y < -0.5f);

    /* a cubemap made from an image samples as the image does */
    image = make_image(128, 64, smooth);
    CHECK(wgf_gfx_priv_environment_cube_from_equirect(&image, 32, &cube));
    CHECK(cube.mip_count == 6);
    for (i = 0; i < 100; i++) {
        const wgf_vec3_t d = random_dir(&state);
        const wgf_vec3_t a = wgf_gfx_priv_environment_sample_equirect(&image, d);
        CHECK_VEC3_NEAR(wgf_gfx_priv_environment_sample_cube(&cube, d, 0.0f), a.x, a.y, a.z, 0.03f);
    }
    wgf_gfx_priv_environment_cube_free(&cube);
    free(image.rgb);
}

static void test_irradiance(void)
{
    wgf_gfx_priv_env_sh_t sh;
    wgf_gfx_priv_env_image_t image;
    unsigned state = 11;
    int i;

    /* constant radiance L: irradiance over pi is L every way */
    image = make_image(128, 64, constant_two);
    wgf_gfx_priv_environment_project_sh(&image, &sh);
    for (i = 0; i < 20; i++) CHECK_VEC3_NEAR(wgf_gfx_priv_environment_eval_sh(&sh, random_dir(&state)), 2, 2, 2, 0.01f);
    free(image.rgb);

    /* radiance cos(elevation) above the horizon: facing up E = 2pi/3, so E/pi = 2/3; facing
       down nothing (9 coefficients come close); facing the horizon E/pi = 2/(3pi) */
    image = make_image(256, 128, sky);
    wgf_gfx_priv_environment_project_sh(&image, &sh);
    CHECK_NEAR(wgf_gfx_priv_environment_eval_sh(&sh, wgf_vec3_make(0, 1, 0)).x, 2.0f / 3.0f, 0.03f);
    CHECK_NEAR(wgf_gfx_priv_environment_eval_sh(&sh, wgf_vec3_make(0, -1, 0)).x, 0.0f, 0.05f);
    CHECK_NEAR(wgf_gfx_priv_environment_eval_sh(&sh, wgf_vec3_make(1, 0, 0)).x, 2.0f / (3.0f * 3.14159265f), 0.02f);
    free(image.rgb);
}

static void test_prefilter(void)
{
    wgf_gfx_priv_env_cube_t source, filtered;
    wgf_gfx_priv_env_image_t image;
    double source_mean = 0;
    wgf_vec3_t up0, up3, down0, down3;
    int m, i;

    /* a constant environment stays constant at every roughness */
    image = make_image(128, 64, constant_two);
    CHECK(wgf_gfx_priv_environment_cube_from_equirect(&image, 32, &source));
    CHECK(wgf_gfx_priv_environment_prefilter(&source, 16, 4, 32, &filtered));
    CHECK(filtered.mip_count == 4 && filtered.size == 16);
    for (m = 0; m < filtered.mip_count; m++) {
        const int s = 16 >> m;
        for (i = 0; i < 6 * s * s * 3; i++) CHECK_NEAR(filtered.mips[m][i], 2.0f, 0.01f);
    }
    wgf_gfx_priv_environment_cube_free(&source);
    wgf_gfx_priv_environment_cube_free(&filtered);
    free(image.rgb);

    /* blurring keeps the average: each mip's mean is the source's */
    image = make_image(128, 64, sky);
    CHECK(wgf_gfx_priv_environment_cube_from_equirect(&image, 32, &source));
    CHECK(wgf_gfx_priv_environment_prefilter(&source, 16, 4, 64, &filtered));
    for (i = 0; i < 6 * 32 * 32 * 3; i++) source_mean += source.mips[0][i];
    source_mean /= 6.0 * 32 * 32 * 3;
    for (m = 0; m < filtered.mip_count; m++) {
        const int s = 16 >> m;
        double mean = 0;
        for (i = 0; i < 6 * s * s * 3; i++) mean += filtered.mips[m][i];
        mean /= 6.0 * s * s * 3;
        CHECK_NEAR(mean, source_mean, 0.03);
    }
    /* rougher mips are blurrier: straight up darker, straight down brighter */
    up0 = wgf_gfx_priv_environment_sample_cube(&filtered, wgf_vec3_make(0, 1, 0), 0);
    up3 = wgf_gfx_priv_environment_sample_cube(&filtered, wgf_vec3_make(0, 1, 0), 3);
    down0 = wgf_gfx_priv_environment_sample_cube(&filtered, wgf_vec3_make(0, -1, 0), 0);
    down3 = wgf_gfx_priv_environment_sample_cube(&filtered, wgf_vec3_make(0, -1, 0), 3);
    CHECK(up3.x < up0.x && down3.x > down0.x);
    wgf_gfx_priv_environment_cube_free(&source);
    wgf_gfx_priv_environment_cube_free(&filtered);
    free(image.rgb);
}

static float lut(int i)
{
    return (float)wgf_gfx_brdf_lut[i] / 255.0f;
}

/* The baked table (tools/gen_brdf_lut.py) has what the split sum is known to give. */
static void test_brdf_lut(void)
{
    enum { N = WGF_GFX_BRDF_LUT_SIZE };
    int x, y;
    for (y = 0; y < N; y++) {
        for (x = 0; x < N; x++) {
            const float a = lut((y * N + x) * 2);
            const float b = lut((y * N + x) * 2 + 1);
            CHECK(a >= 0.0f && b >= 0.0f && a + b <= 1.01f);
            /* rougher reflects less overall (more shadowing and masking), but at grazing
               angles, where the integral has its well-known bump */
            if (y > 0 && ((float)x + 0.5f) / N >= 0.25f) {
                CHECK(a + b <= lut(((y - 1) * N + x) * 2) +
                                   lut(((y - 1) * N + x) * 2 + 1) + 0.01f);
            }
        }
    }
    /* Unreal's and LearnOpenGL's tables have about (0.72, 0.02) at n.v 0.5, roughness 0.5 */
    CHECK_NEAR(lut((N / 2 * N + N / 2) * 2), 0.72f, 0.04f);
    CHECK_NEAR(lut((N / 2 * N + N / 2) * 2 + 1), 0.02f, 0.02f);
    /* nearly smooth: Schlick's Fresnel split, A = 1 - (1 - n.v)^5 and B = (1 - n.v)^5 */
    for (x = 4; x < N; x++) {
        const float fresnel = powf(1.0f - ((float)x + 0.5f) / N, 5.0f);
        CHECK_NEAR(lut(x * 2), 1.0f - fresnel, 0.03f);
        CHECK_NEAR(lut(x * 2 + 1), fresnel, 0.03f);
    }
}

static void test_half_float(void)
{
    CHECK(wgf_gfx_priv_environment_half_from_float(0.0f) == 0x0000);
    CHECK(wgf_gfx_priv_environment_half_from_float(1.0f) == 0x3C00);
    CHECK(wgf_gfx_priv_environment_half_from_float(0.5f) == 0x3800);
    CHECK(wgf_gfx_priv_environment_half_from_float(-2.0f) == 0xC000);
    CHECK(wgf_gfx_priv_environment_half_from_float(65504.0f) == 0x7BFF);
    CHECK(wgf_gfx_priv_environment_half_from_float(1e9f) == 0x7BFF); /* clamped to the largest half */
    CHECK(wgf_gfx_priv_environment_half_from_float(1e-9f) == 0x0000);
    /* Python's struct 'e' (IEEE 754 half, to nearest) */
    CHECK(wgf_gfx_priv_environment_half_from_float(0.1f) == 0x2E66);
    CHECK(wgf_gfx_priv_environment_half_from_float(1e-4f) == 0x068E);
    CHECK(wgf_gfx_priv_environment_half_from_float(3e-5f) == 0x01F7); /* subnormal */
}

/* A 64 by 32 .hdr of `value` everywhere (flat RGBE: mantissa 128, exponent 129 + log2). */
static int make_hdr(unsigned char *out, int exponent)
{
    static const char header[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n";
    const int head = (int)sizeof(header) - 1;
    int i;
    memcpy(out, header, (size_t)head);
    for (i = 0; i < 64 * 32; i++) {
        out[head + i * 4] = out[head + i * 4 + 1] = out[head + i * 4 + 2] = 128;
        out[head + i * 4 + 3] = (unsigned char)exponent;
    }
    return head + 64 * 32 * 4;
}

/* The same 64 by 32 image of `value`, run-length encoded as Radiance writes it: each
 * scanline's 4 channels a run of 64 (as 127 + 1: a run is at most 127), but its first
 * pixel's red written alone, a literal, to have both kinds. */
static int make_hdr_rle(unsigned char *out, int exponent)
{
    static const char header[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n";
    int n = (int)sizeof(header) - 1, y, c;
    memcpy(out, header, (size_t)n);
    for (y = 0; y < 32; y++) {
        out[n++] = 2, out[n++] = 2, out[n++] = 0, out[n++] = 64;
        for (c = 0; c < 4; c++) {
            const unsigned char v = (unsigned char)(c < 3 ? 128 : exponent);
            if (c == 0) {
                out[n++] = 1, out[n++] = v;       /* a literal of 1 */
                out[n++] = 128 + 63, out[n++] = v; /* a run of 63 */
            } else {
                out[n++] = 128 + 64, out[n++] = v; /* a run of 64 */
            }
        }
    }
    return n;
}

static void write_file(const char *path, const unsigned char *data, int size)
{
    const wgf_fs_task_t task = wgf_fs_write(path, data, size);
    int i;
    for (i = 0; i < 1000 && wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_PENDING; i++) wgf_core_priv_update();
    CHECK(wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_DONE);
    wgf_fs_task_destroy(task);
}

/* A worker prepares it, which takes a moment (the prefilter): wait on time, not on a count
 * of updates. */
static void settle(wgf_environment_t environment)
{
    const double start = wgf_time_get_seconds();
    while ((wgf_resource_get_status(environment) == WGF_RESOURCE_STATUS_PENDING ||
            wgf_core_priv_resource_is_reloading(environment)) &&
           wgf_time_get_seconds() - start < 60.0) {
        wgf_core_priv_update();
    }
}

static void test_api(void)
{
    static unsigned char hdr[64 + 64 * 32 * 4];
    wgf_gfx_priv_env_image_t image;
    wgf_gfx_priv_env_lighting_t lighting;
    wgf_environment_t env, missing;
    wgf_actor_t stage, camera;

    memset(&lighting, 0, sizeof(lighting)); /* read below even when the lighting isn't had */
    { /* run-length encoded: the same image; cut short: refused */
        static unsigned char rle[64 + 32 * (4 + 4 + 6)];
        const int size = make_hdr_rle(rle, 130);
        write_file("environment_test/rle.hdr", rle, size);
        CHECK(wgf_gfx_priv_environment_load_image("environment_test/rle.hdr", &image));
        CHECK(image.width == 64 && image.height == 32 && fabsf(image.rgb[0] - 2.0f) < 1e-3f &&
              fabsf(image.rgb[64 * 32 * 3 - 1] - 2.0f) < 1e-3f);
        free(image.rgb);
        write_file("environment_test/short.hdr", rle, size - 5);
        CHECK(!wgf_gfx_priv_environment_load_image("environment_test/short.hdr", &image) && image.rgb == NULL);
        CHECK(wgf_gfx_priv_environment_is_hdr(rle, size) && !wgf_gfx_priv_environment_is_hdr(rle, 20));
    }
    write_file("environment_test/two.hdr", hdr, make_hdr(hdr, 130)); /* 2 */
    CHECK(wgf_gfx_priv_environment_load_image("environment_test/two.hdr", &image));
    CHECK(image.width == 64 && image.height == 32 && fabsf(image.rgb[0] - 2.0f) < 1e-3f &&
          fabsf(image.rgb[64 * 32 * 3 - 1] - 2.0f) < 1e-3f);
    free(image.rgb);

    env = wgf_environment_create("environment_test/two.hdr");
    CHECK(env != 0 && wgf_resource_get_status(env) == WGF_RESOURCE_STATUS_PENDING);
    CHECK(wgf_environment_create("environment_test/./two.hdr") == env); /* shared by its path */
    wgf_resource_release(env);
    settle(env);
    CHECK(wgf_resource_get_status(env) == WGF_RESOURCE_STATUS_READY);
    CHECK(strcmp(wgf_resource_get_path(env), "environment_test/two.hdr") == 0);
    CHECK(wgf_gfx_priv_environment_get_lighting(env, &lighting));
    CHECK_VEC3_NEAR(wgf_gfx_priv_environment_eval_sh(&lighting.sh, wgf_vec3_make(0, 1, 0)), 2, 2, 2, 0.02f);
    CHECK(!wgf_gfx_priv_environment_get_lighting(0, &lighting));
    { /* a broken .hdr FAILS, from one of its steps */
        const wgf_environment_t broken = wgf_environment_create("environment_test/short.hdr");
        settle(broken);
        CHECK(wgf_resource_get_status(broken) == WGF_RESOURCE_STATUS_FAILED);
        wgf_resource_release(broken);
    }
    missing = wgf_environment_create("environment_test/missing.hdr");
    settle(missing);
    CHECK(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED);
    CHECK(!wgf_gfx_priv_environment_get_lighting(missing, &lighting));
    wgf_resource_release(missing);

    /* loaded again, in place: READY all the while, the new file's light once it is in */
    write_file("environment_test/two.hdr", hdr, make_hdr(hdr, 131)); /* 4 */
    CHECK(wgf_asset_reload("environment_test/two.hdr") == 1 && wgf_resource_get_status(env) == WGF_RESOURCE_STATUS_READY);
    settle(env);
    CHECK(wgf_gfx_priv_environment_get_lighting(env, &lighting));
    CHECK_VEC3_NEAR(wgf_gfx_priv_environment_eval_sh(&lighting.sh, wgf_vec3_make(0, 1, 0)), 4, 4, 4, 0.04f);

    /* a stage's hold on it */
    stage = wgf_stage3d_create();
    CHECK(wgf_stage3d_get_environment(stage) == 0 && wgf_stage3d_get_background(stage) == 0);
    CHECK(wgf_stage3d_set_environment(stage, env, 1.5f, 0.5f) && wgf_stage3d_get_environment(stage) == env &&
          wgf_stage3d_get_environment_intensity(stage) == 1.5f && wgf_stage3d_get_environment_rotation(stage) == 0.5f);
    CHECK(wgf_stage3d_set_background(stage, env, 2.0f) && wgf_stage3d_get_background(stage) == env &&
          wgf_stage3d_get_background_blur(stage) == 1.0f);
    CHECK(!wgf_stage3d_set_environment(stage, stage, 1.0f, 0.0f)); /* not an environment */
    CHECK(!wgf_stage3d_set_background(stage, missing, 0.0f));      /* gone */
    CHECK(!wgf_stage3d_set_environment(env, env, 1.0f, 0.0f));     /* not a stage */
    CHECK(wgf_stage3d_set_environment(stage, env, -1.0f, 0.0f) && wgf_stage3d_get_environment_intensity(stage) == 0.0f);
    wgf_resource_release(env); /* the stage still holds it, twice */
    CHECK(wgf_resource_get_status(env) == WGF_RESOURCE_STATUS_READY);
    CHECK(wgf_stage3d_set_background(stage, 0, 0.5f) && wgf_stage3d_get_background(stage) == 0 &&
          wgf_stage3d_get_background_blur(stage) == 0.0f);
    CHECK(wgf_resource_get_status(env) == WGF_RESOURCE_STATUS_READY);
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN); /* its last reference */
    CHECK(wgf_resource_get_status(env) == WGF_RESOURCE_STATUS_NONE);

    /* a stage drawn with one, background and all */
    env = wgf_environment_create("environment_test/two.hdr");
    settle(env);
    stage = wgf_stage3d_create();
    camera = wgf_camera3d_create();
    wgf_actor_set_parent(camera, stage);
    wgf_stage3d_set_camera(stage, camera);
    wgf_stage3d_set_environment(stage, env, 1.0f, 0.0f);
    wgf_stage3d_set_background(stage, env, 0.5f);
    wgf_resource_release(env);
    wgf_platform_priv_set_size(32, 32);
    wgf_gfx_priv_begin_frame();
    wgf_stage3d_draw(stage);
    wgf_gfx_priv_end_frame();
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    CHECK(wgf_resource_get_status(env) == WGF_RESOURCE_STATUS_NONE);

    /* released while it loads */
    env = wgf_environment_create("environment_test/two.hdr");
    wgf_resource_release(env);
    CHECK(wgf_resource_get_status(env) == WGF_RESOURCE_STATUS_NONE);
    {
        int i;
        for (i = 0; i < 50; i++) wgf_core_priv_update();
    }
    CHECK(wgf_resource_get_status(env) == WGF_RESOURCE_STATUS_NONE);
}

int main(void)
{
    test_mapping();
    test_irradiance();
    test_prefilter();
    test_brdf_lut();
    test_half_float();
    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR); /* the missing file warns, on purpose */
    CHECK(wgf_gfx_priv_start());
    test_api();
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
