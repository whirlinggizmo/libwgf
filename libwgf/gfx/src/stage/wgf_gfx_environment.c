#include "wgf_environment.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/wgf_gfx_brdf_lut.h"
#include "material/wgf_gfx_material_priv.h"
#include "render/wgf_gfx_render_priv.h"
#include "stage/wgf_gfx_environment_priv.h"
#include "texture/wgf_gfx_texture_priv.h"
#include "wgf_asset_priv.h" /* a resource made from a path: the asset part locates it */
#include "wgf_core_fs_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_log.h"

/* the shader's sources and reflection for the build's backend; a headless build's dummy
   backend takes the GL one, which it only checks the layout of */
#if defined(SOKOL_DUMMY_BACKEND) && !defined(SOKOL_GLCORE)
#define SOKOL_GLCORE
#endif
#include "shaders/wgf_gfx_background.glsl.h"

/* Environments, libwgt's wgt_gfx_environment.c (wgrender's wgr_environment): an
 * equirectangular image prepared on a worker -- spherical harmonics for diffuse light, a
 * source cubemap for the background, a GGX-prefiltered one for reflections -- through
 * core's load pipeline, then uploaded as half floats on the main thread; loaded again in
 * place (wgf_asset_reload), the new cubes made before the old are let go of. The split-sum
 * BRDF table is baked (tools/gen_brdf_lut.py), in bytes where libwgt's is half floats. The
 * background is a full-screen triangle at
 * the far plane. A part, installed by the first wgf_environment_create, which sets the
 * stage's hooks; once gfx runs it asks whether the backend filters half floats. */

#define MIN_SOURCE_CUBE_SIZE 64   /* the source cubemap matches the image: width / 4 a face, */
#define MAX_SOURCE_CUBE_SIZE 1024 /* rounded down to a power of two */
#define PREFILTER_SAMPLES 96
#define PI_F 3.14159265358979f
#define PI_D 3.14159265358979323846

/* ===================================================================== CPU ==== */

static float clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

wgf_vec3_t wgf_gfx_priv_environment_equirect_dir(float u, float v)
{
    const float phi = (u - 0.5f) * 2.0f * PI_F;
    const float theta = v * PI_F;
    return wgf_vec3_make(sinf(theta) * sinf(phi), cosf(theta), -sinf(theta) * cosf(phi));
}

void wgf_gfx_priv_environment_dir_equirect(wgf_vec3_t dir, float *u, float *v)
{
    const wgf_vec3_t d = wgf_vec3_normalize(dir);
    *u = 0.5f + atan2f(d.x, -d.z) / (2.0f * PI_F);
    *v = acosf(d.y < -1.0f ? -1.0f : (d.y > 1.0f ? 1.0f : d.y)) / PI_F;
}

wgf_vec3_t wgf_gfx_priv_environment_cube_dir(int face, float s, float t)
{
    const float sc = 2.0f * s - 1.0f, tc = 2.0f * t - 1.0f;
    wgf_vec3_t d;
    switch (face) {
        case 0: d = wgf_vec3_make(1.0f, -tc, -sc); break;
        case 1: d = wgf_vec3_make(-1.0f, -tc, sc); break;
        case 2: d = wgf_vec3_make(sc, 1.0f, tc); break;
        case 3: d = wgf_vec3_make(sc, -1.0f, -tc); break;
        case 4: d = wgf_vec3_make(sc, -tc, 1.0f); break;
        default: d = wgf_vec3_make(-sc, -tc, -1.0f); break;
    }
    return wgf_vec3_normalize(d);
}

void wgf_gfx_priv_environment_dir_cube(wgf_vec3_t d, int *face, float *s, float *t)
{
    const float ax = fabsf(d.x), ay = fabsf(d.y), az = fabsf(d.z);
    float ma, sc, tc;
    if (ax >= ay && ax >= az) {
        ma = ax;
        if (d.x > 0.0f) {
            *face = 0;
            sc = -d.z;
            tc = -d.y;
        } else {
            *face = 1;
            sc = d.z;
            tc = -d.y;
        }
    } else if (ay >= az) {
        ma = ay;
        if (d.y > 0.0f) {
            *face = 2;
            sc = d.x;
            tc = d.z;
        } else {
            *face = 3;
            sc = d.x;
            tc = -d.z;
        }
    } else {
        ma = az;
        if (d.z > 0.0f) {
            *face = 4;
            sc = d.x;
            tc = -d.y;
        } else {
            *face = 5;
            sc = -d.x;
            tc = -d.y;
        }
    }
    if (ma <= 0.0f) ma = 1.0f;
    *s = 0.5f * (sc / ma + 1.0f);
    *t = 0.5f * (tc / ma + 1.0f);
}

/* Bilinear among the four texels around (x, y) of a `width` by `height` RGB grid, `wrap` on
 * x (around the horizon) or clamped. */
static wgf_vec3_t bilinear(const float *rgb, int width, int height, float x, float y, bool wrap)
{
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1, y1, i;
    const float fx = x - (float)x0, fy = y - (float)y0;
    wgf_vec3_t out = {0, 0, 0};
    int corner_x[4], corner_y[4];
    float weight[4];
    x1 = x0 + 1;
    y1 = y0 + 1;
    if (wrap) {
        x0 = ((x0 % width) + width) % width;
        x1 = ((x1 % width) + width) % width;
    } else {
        x0 = x0 < 0 ? 0 : (x0 >= width ? width - 1 : x0);
        x1 = x1 < 0 ? 0 : (x1 >= width ? width - 1 : x1);
    }
    y0 = y0 < 0 ? 0 : (y0 >= height ? height - 1 : y0);
    y1 = y1 < 0 ? 0 : (y1 >= height ? height - 1 : y1);
    corner_x[0] = x0, corner_x[1] = x1, corner_x[2] = x0, corner_x[3] = x1;
    corner_y[0] = y0, corner_y[1] = y0, corner_y[2] = y1, corner_y[3] = y1;
    weight[0] = (1 - fx) * (1 - fy), weight[1] = fx * (1 - fy), weight[2] = (1 - fx) * fy, weight[3] = fx * fy;
    for (i = 0; i < 4; i++) {
        const float *p = &rgb[((size_t)corner_y[i] * (size_t)width + (size_t)corner_x[i]) * 3];
        out.x += p[0] * weight[i];
        out.y += p[1] * weight[i];
        out.z += p[2] * weight[i];
    }
    return out;
}

wgf_vec3_t wgf_gfx_priv_environment_sample_equirect(const wgf_gfx_priv_env_image_t *image, wgf_vec3_t dir)
{
    float u, v;
    wgf_gfx_priv_environment_dir_equirect(dir, &u, &v);
    return bilinear(image->rgb, image->width, image->height, u * (float)image->width - 0.5f,
                    v * (float)image->height - 0.5f, true);
}

static wgf_vec3_t sample_face(const wgf_gfx_priv_env_cube_t *cube, int mip, int face, float s, float t)
{
    const int size = cube->size >> mip > 0 ? cube->size >> mip : 1;
    const float *data = cube->mips[mip] + (size_t)face * (size_t)size * (size_t)size * 3;
    return bilinear(data, size, size, s * (float)size - 0.5f, t * (float)size - 0.5f, false);
}

wgf_vec3_t wgf_gfx_priv_environment_sample_cube(const wgf_gfx_priv_env_cube_t *cube, wgf_vec3_t dir, float lod)
{
    const float max_lod = (float)(cube->mip_count - 1);
    int face, m0, m1;
    float s, t, f;
    wgf_vec3_t a, b;
    lod = lod < 0.0f ? 0.0f : (lod > max_lod ? max_lod : lod);
    m0 = (int)floorf(lod);
    m1 = m0 + 1 < cube->mip_count ? m0 + 1 : m0;
    f = lod - (float)m0;
    wgf_gfx_priv_environment_dir_cube(dir, &face, &s, &t);
    a = sample_face(cube, m0, face, s, t);
    if (f <= 0.0f || m1 == m0) return a;
    b = sample_face(cube, m1, face, s, t);
    return wgf_vec3_make(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f);
}

static void sh_basis(wgf_vec3_t n, float y[9])
{
    y[0] = 0.282095f;
    y[1] = 0.488603f * n.y;
    y[2] = 0.488603f * n.z;
    y[3] = 0.488603f * n.x;
    y[4] = 1.092548f * n.x * n.y;
    y[5] = 1.092548f * n.y * n.z;
    y[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    y[7] = 1.092548f * n.x * n.z;
    y[8] = 0.546274f * (n.x * n.x - n.y * n.y);
}

/* Rows y0..y1 of the image's projection, added to `sum`. */
static void project_sh_rows(const wgf_gfx_priv_env_image_t *image, int y0, int y1, double sum[9][3])
{
    const double pixel_area = (2.0 * PI_D / image->width) * (PI_D / image->height);
    int px, py, k, c;
    for (py = y0; py < y1; py++) {
        const float v = ((float)py + 0.5f) / (float)image->height;
        const double solid_angle = pixel_area * sin(v * PI_D);
        for (px = 0; px < image->width; px++) {
            const wgf_vec3_t dir = wgf_gfx_priv_environment_equirect_dir(((float)px + 0.5f) / (float)image->width, v);
            const float *rgb = &image->rgb[((size_t)py * (size_t)image->width + (size_t)px) * 3];
            float y[9];
            sh_basis(dir, y);
            for (k = 0; k < 9; k++) {
                for (c = 0; c < 3; c++) sum[k][c] += rgb[c] * y[k] * solid_angle;
            }
        }
    }
}

/* The coefficients from the whole image's sums: the Lambert convolution per band
 * (Ramamoorthi & Hanrahan), divided by pi. */
static void sh_from_sums(double sum[9][3], wgf_gfx_priv_env_sh_t *out)
{
    static const float band[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    int k, c;
    for (k = 0; k < 9; k++) {
        for (c = 0; c < 3; c++) out->c[k][c] = (float)sum[k][c] * band[k];
    }
}

void wgf_gfx_priv_environment_project_sh(const wgf_gfx_priv_env_image_t *image, wgf_gfx_priv_env_sh_t *out)
{
    double sum[9][3] = {{0}};
    project_sh_rows(image, 0, image->height, sum);
    sh_from_sums(sum, out);
}

wgf_vec3_t wgf_gfx_priv_environment_eval_sh(const wgf_gfx_priv_env_sh_t *sh, wgf_vec3_t n)
{
    float y[9];
    wgf_vec3_t out = {0, 0, 0};
    int k;
    sh_basis(n, y);
    for (k = 0; k < 9; k++) {
        out.x += sh->c[k][0] * y[k];
        out.y += sh->c[k][1] * y[k];
        out.z += sh->c[k][2] * y[k];
    }
    return out;
}

void wgf_gfx_priv_environment_cube_free(wgf_gfx_priv_env_cube_t *cube)
{
    int m;
    for (m = 0; m < 16; m++) {
        free(cube->mips[m]);
        cube->mips[m] = NULL;
    }
    cube->mip_count = 0;
}

static bool cube_alloc(wgf_gfx_priv_env_cube_t *cube, int size, int mip_count)
{
    int m;
    memset(cube, 0, sizeof(*cube));
    cube->size = size;
    cube->mip_count = mip_count;
    for (m = 0; m < mip_count; m++) {
        const size_t s = (size_t)(size >> m > 0 ? size >> m : 1);
        cube->mips[m] = (float *)calloc(6 * s * s * 3, sizeof(float));
        if (cube->mips[m] == NULL) {
            wgf_gfx_priv_environment_cube_free(cube);
            return false;
        }
    }
    return true;
}

static int mip_count_of(int size)
{
    int mip_count = 1;
    while ((size >> (mip_count - 1)) > 1 && mip_count < 16) mip_count++;
    return mip_count;
}

/* Rows y0..y1 of a face of the source cube's mip 0, 2x2 supersampled a texel. */
static void cube_rows(const wgf_gfx_priv_env_image_t *image, wgf_gfx_priv_env_cube_t *cube, int face, int y0, int y1)
{
    const int size = cube->size;
    int x, y, sx, sy;
    for (y = y0; y < y1; y++) {
        for (x = 0; x < size; x++) {
            float *p = &cube->mips[0][((size_t)face * size * size + (size_t)y * size + x) * 3];
            for (sy = 0; sy < 2; sy++) {
                for (sx = 0; sx < 2; sx++) {
                    const wgf_vec3_t d =
                        wgf_gfx_priv_environment_cube_dir(face, ((float)x + 0.25f + 0.5f * sx) / (float)size,
                                                          ((float)y + 0.25f + 0.5f * sy) / (float)size);
                    const wgf_vec3_t col = wgf_gfx_priv_environment_sample_equirect(image, d);
                    p[0] += col.x * 0.25f;
                    p[1] += col.y * 0.25f;
                    p[2] += col.z * 0.25f;
                }
            }
        }
    }
}

/* Mips 1 on, box-filtered from mip 0. */
static void box_mips(wgf_gfx_priv_env_cube_t *cube)
{
    int m, face, x, y, c;
    for (m = 1; m < cube->mip_count; m++) {
        const int src = cube->size >> (m - 1), dst = cube->size >> m > 0 ? cube->size >> m : 1;
        for (face = 0; face < 6; face++) {
            const float *s = cube->mips[m - 1] + (size_t)face * src * src * 3;
            for (y = 0; y < dst; y++) {
                for (x = 0; x < dst; x++) {
                    float *p = &cube->mips[m][((size_t)face * dst * dst + (size_t)y * dst + x) * 3];
                    const int x1 = 2 * x + 1 < src ? 2 * x + 1 : 2 * x, y1 = 2 * y + 1 < src ? 2 * y + 1 : 2 * y;
                    for (c = 0; c < 3; c++) {
                        p[c] = 0.25f * (s[(2 * y * src + 2 * x) * 3 + c] + s[(2 * y * src + x1) * 3 + c] +
                                        s[(y1 * src + 2 * x) * 3 + c] + s[(y1 * src + x1) * 3 + c]);
                    }
                }
            }
        }
    }
}

bool wgf_gfx_priv_environment_cube_from_equirect(const wgf_gfx_priv_env_image_t *image, int size,
                                                 wgf_gfx_priv_env_cube_t *out)
{
    int face;
    if (!cube_alloc(out, size, mip_count_of(size))) return false;
    for (face = 0; face < 6; face++) cube_rows(image, out, face, 0, size);
    box_mips(out);
    return true;
}

static float radical_inverse(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return (float)bits * 2.3283064365386963e-10f;
}

/* A prefilter sample, the same for every texel of a mip (v = n): its light direction about
 * +z, whose z is its weight (n.l), and the source mip it reads. */
typedef struct prefilter_sample_t {
    float x, y, z, lod;
} prefilter_sample_t;

/* Mip m's samples (n of them, Hammersley points through GGX at roughness m / (mip_count -
 * 1)), those below the horizon left out; how many are kept. Filtered importance sampling: a
 * sample reads a blurrier source mip where samples are sparse, which removes noise with few
 * samples. */
static int prefilter_samples(const wgf_gfx_priv_env_cube_t *source, int mip, int mip_count, int n,
                             prefilter_sample_t *out)
{
    const float source_texel_solid_angle = 4.0f * PI_F / (6.0f * (float)source->size * (float)source->size);
    const float roughness = mip_count > 1 ? (float)mip / (float)(mip_count - 1) : 0.0f;
    const float alpha = roughness * roughness, a2 = alpha * alpha;
    int i, kept = 0;
    for (i = 0; i < n; i++) {
        const float phi = 2.0f * PI_F * (float)i / (float)n, xi = radical_inverse((uint32_t)i);
        const float cos_theta = sqrtf((1.0f - xi) / (1.0f + (a2 - 1.0f) * xi));
        const float sin_theta = sqrtf(1.0f - cos_theta * cos_theta);
        const float hx = sin_theta * cosf(phi), hy = sin_theta * sinf(phi), hz = cos_theta;
        const float lz = 2.0f * hz * hz - 1.0f; /* n about h, n = +z */
        float dd, pdf;
        if (lz <= 0.0f) continue;
        dd = hz * hz * (a2 - 1.0f) + 1.0f;
        pdf = a2 / (PI_F * dd * dd) * 0.25f; /* D n.h / (4 v.h), v.h == n.h */
        out[kept].x = 2.0f * hz * hx;
        out[kept].y = 2.0f * hz * hy;
        out[kept].z = lz;
        out[kept].lod = 0.5f * log2f(1.0f / ((float)n * pdf + 1e-6f) / source_texel_solid_angle) + 1.0f;
        kept++;
    }
    return kept;
}

/* Rows y0..y1 of a face of the prefiltered cube's mip `mip`, from its samples. */
static void prefilter_rows(const wgf_gfx_priv_env_cube_t *source, wgf_gfx_priv_env_cube_t *out, int mip, int face,
                           int y0, int y1, const prefilter_sample_t *samples, int count)
{
    const int s = out->size >> mip > 0 ? out->size >> mip : 1;
    int x, y, i;
    for (y = y0; y < y1; y++) {
        for (x = 0; x < s; x++) {
            float *p = &out->mips[mip][((size_t)face * s * s + (size_t)y * s + x) * 3];
            const wgf_vec3_t n =
                wgf_gfx_priv_environment_cube_dir(face, ((float)x + 0.5f) / (float)s, ((float)y + 0.5f) / (float)s);
            wgf_vec3_t sum = {0, 0, 0}, tx, ty;
            float weight = 0.0f;
            if (mip == 0) { /* mirror-like: the source at this resolution */
                const wgf_vec3_t col =
                    wgf_gfx_priv_environment_sample_cube(source, n, log2f((float)source->size / (float)s));
                p[0] = col.x;
                p[1] = col.y;
                p[2] = col.z;
                continue;
            }
            tx = wgf_vec3_normalize(wgf_vec3_cross(fabsf(n.z) < 0.999f ? wgf_vec3_make(0, 0, 1) : wgf_vec3_make(1, 0, 0), n));
            ty = wgf_vec3_cross(n, tx);
            for (i = 0; i < count; i++) {
                const prefilter_sample_t *k = &samples[i];
                const wgf_vec3_t l = wgf_vec3_make(tx.x * k->x + ty.x * k->y + n.x * k->z,
                                                   tx.y * k->x + ty.y * k->y + n.y * k->z,
                                                   tx.z * k->x + ty.z * k->y + n.z * k->z);
                const wgf_vec3_t col = wgf_gfx_priv_environment_sample_cube(source, l, k->lod);
                sum = wgf_vec3_make(sum.x + col.x * k->z, sum.y + col.y * k->z, sum.z + col.z * k->z);
                weight += k->z;
            }
            if (weight > 0.0f) {
                p[0] = sum.x / weight;
                p[1] = sum.y / weight;
                p[2] = sum.z / weight;
            }
        }
    }
}

bool wgf_gfx_priv_environment_prefilter(const wgf_gfx_priv_env_cube_t *source, int size, int mip_count, int samples,
                                        wgf_gfx_priv_env_cube_t *out)
{
    prefilter_sample_t *table = (prefilter_sample_t *)malloc(sizeof(prefilter_sample_t) * (size_t)(samples > 0 ? samples : 1));
    int m, face;
    if (table == NULL || !cube_alloc(out, size, mip_count)) {
        free(table);
        return false;
    }
    for (m = 0; m < mip_count; m++) {
        const int count = m > 0 ? prefilter_samples(source, m, mip_count, samples, table) : 0;
        const int s = size >> m > 0 ? size >> m : 1;
        for (face = 0; face < 6; face++) prefilter_rows(source, out, m, face, 0, s, table, count);
    }
    free(table);
    return true;
}

uint16_t wgf_gfx_priv_environment_half_from_float(float value)
{
    uint32_t bits, sign, mantissa, half;
    int32_t exponent;
    memcpy(&bits, &value, sizeof(bits));
    sign = (bits >> 16) & 0x8000u;
    exponent = (int32_t)((bits >> 23) & 0xFF) - 127 + 15;
    mantissa = bits & 0x007FFFFFu;
    if (((bits >> 23) & 0xFF) == 0xFF) return (uint16_t)(sign | 0x7C00u | (mantissa ? 0x200u : 0u)); /* inf, NaN */
    if (exponent >= 31) return (uint16_t)(sign | 0x7BFFu); /* too large: the largest finite half */
    if (exponent <= 0) {
        int shift;
        if (exponent < -10) return (uint16_t)sign; /* to zero */
        mantissa |= 0x00800000u;
        shift = 14 - exponent;
        return (uint16_t)(sign | ((mantissa + (1u << (shift - 1))) >> shift));
    }
    /* to nearest; a carry out of the mantissa bumps the exponent, as it should */
    half = ((uint32_t)exponent << 10) + ((mantissa + 0x00001000u) >> 13);
    return (uint16_t)(sign | (half >= 0x7C00u ? 0x7BFFu : half));
}

/* --- Radiance .hdr, a band of rows at a time ------------------------------------- */

/* The header of a .hdr (`#?RADIANCE` or `#?RGBE`, its 32-bit_rle_rgbe format, and a
 * "-Y height +X width" size, the only orientation stb_image reads too), its size, and
 * where its pixels start; false for anything else. */
static bool hdr_header(const unsigned char *bytes, int size, int *width, int *height, int *start)
{
    int pos = 0, w = 0, h = 0;
    char line[128];
    bool first = true;
    for (;;) { /* lines up to the empty one */
        int n = 0;
        while (pos < size && bytes[pos] != '\n') {
            if (n < (int)sizeof(line) - 1) line[n++] = (char)bytes[pos];
            pos++;
        }
        if (pos >= size) return false;
        pos++;
        line[n] = '\0';
        if (first) {
            if (strcmp(line, "#?RADIANCE") != 0 && strcmp(line, "#?RGBE") != 0) return false;
            first = false;
        } else if (n == 0) {
            break;
        } else if (strncmp(line, "FORMAT=", 7) == 0 && strcmp(line + 7, "32-bit_rle_rgbe") != 0) {
            return false;
        }
    }
    {
        int n = 0;
        while (pos < size && bytes[pos] != '\n') {
            if (n < (int)sizeof(line) - 1) line[n++] = (char)bytes[pos];
            pos++;
        }
        if (pos >= size) return false;
        pos++;
        line[n] = '\0';
        if (sscanf(line, "-Y %d +X %d", &h, &w) != 2 || w <= 0 || h <= 0 || w > 32768 || h > 32768) return false;
    }
    *width = w;
    *height = h;
    *start = pos;
    return true;
}

bool wgf_gfx_priv_environment_is_hdr(const unsigned char *bytes, int size)
{
    int w, h, start;
    return hdr_header(bytes, size, &w, &h, &start);
}

typedef struct hdr_reader_t {
    const unsigned char *bytes;
    int size, pos;
    int rle; /* -1: not known before the first scanline; then 0 or 1, for the whole image (as stb_image) */
    unsigned char *scanline; /* RGBE, a row */
} hdr_reader_t;

static void rgbe_to_float(const unsigned char *rgbe, float *out)
{
    if (rgbe[3] == 0) {
        out[0] = out[1] = out[2] = 0.0f;
    } else {
        const float f = ldexpf(1.0f, (int)rgbe[3] - (128 + 8));
        out[0] = (float)rgbe[0] * f;
        out[1] = (float)rgbe[1] * f;
        out[2] = (float)rgbe[2] * f;
    }
}

/* Rows y0..y1 of `image` from the reader; false for data that ends or is broken. */
static bool hdr_rows(hdr_reader_t *r, wgf_gfx_priv_env_image_t *image, int y0, int y1)
{
    const int w = image->width;
    int y, x, c;
    for (y = y0; y < y1; y++) {
        if (r->rle < 0) {
            r->rle = w >= 8 && w < 32768 && r->pos + 4 <= r->size && r->bytes[r->pos] == 2 &&
                     r->bytes[r->pos + 1] == 2 && ((r->bytes[r->pos + 2] << 8) | r->bytes[r->pos + 3]) == w;
        }
        if (!r->rle) { /* flat RGBE */
            if (r->pos + w * 4 > r->size) return false;
            for (x = 0; x < w; x++) rgbe_to_float(&r->bytes[r->pos + x * 4], &image->rgb[((size_t)y * w + x) * 3]);
            r->pos += w * 4;
            continue;
        }
        if (r->pos + 4 > r->size || r->bytes[r->pos] != 2 || r->bytes[r->pos + 1] != 2 ||
            ((r->bytes[r->pos + 2] << 8) | r->bytes[r->pos + 3]) != w) {
            return false;
        }
        r->pos += 4;
        for (c = 0; c < 4; c++) { /* each channel's runs: (128 + n, byte) or (n, n bytes) */
            x = 0;
            while (x < w) {
                int n;
                if (r->pos >= r->size) return false;
                n = r->bytes[r->pos++];
                if (n > 128) {
                    n -= 128;
                    if (n > w - x || r->pos >= r->size) return false;
                    while (n-- > 0) r->scanline[(x++) * 4 + c] = r->bytes[r->pos];
                    r->pos++;
                } else {
                    if (n == 0 || n > w - x || r->pos + n > r->size) return false;
                    while (n-- > 0) r->scanline[(x++) * 4 + c] = r->bytes[r->pos++];
                }
            }
        }
        for (x = 0; x < w; x++) rgbe_to_float(&r->scanline[x * 4], &image->rgb[((size_t)y * w + x) * 3]);
    }
    return true;
}

/* An image file's start: a .hdr's size and its pixels' memory, left to decode in rows from
 * `r`; a PNG or JPEG decoded whole (the texture loader's decoder, from sRGB). False when it
 * can't be read. `bytes` stays the caller's, to free once the rows are read. */
static bool image_begin(const unsigned char *bytes, int size, const char *path, wgf_gfx_priv_env_image_t *out,
                        hdr_reader_t *r)
{
    int w = 0, h = 0, start = 0;
    memset(out, 0, sizeof(*out));
    memset(r, 0, sizeof(*r));
    if (hdr_header(bytes, size, &w, &h, &start)) {
        out->rgb = (float *)malloc((size_t)w * (size_t)h * 3 * sizeof(float));
        r->scanline = (unsigned char *)malloc((size_t)w * 4);
        if (out->rgb == NULL || r->scanline == NULL) {
            free(out->rgb);
            free(r->scanline);
            out->rgb = NULL;
            r->scanline = NULL;
            return false;
        }
        r->bytes = bytes;
        r->size = size;
        r->pos = start;
        r->rle = -1;
    } else {
        void *decoded = wgf_gfx_priv_texture_decode(bytes, size, path);
        const unsigned char *pixels;
        size_t i;
        if (decoded == NULL) return false;
        pixels = wgf_gfx_priv_texture_decoded_pixels(decoded, &w, &h);
        out->rgb = (float *)malloc((size_t)w * (size_t)h * 3 * sizeof(float));
        for (i = 0; out->rgb != NULL && i < (size_t)w * (size_t)h; i++) {
            out->rgb[i * 3] = wgf_gfx_priv_srgb_to_linear((float)pixels[i * 4] / 255.0f);
            out->rgb[i * 3 + 1] = wgf_gfx_priv_srgb_to_linear((float)pixels[i * 4 + 1] / 255.0f);
            out->rgb[i * 3 + 2] = wgf_gfx_priv_srgb_to_linear((float)pixels[i * 4 + 2] / 255.0f);
        }
        wgf_gfx_priv_texture_decoded_free(decoded);
        if (out->rgb == NULL) return false;
    }
    out->width = w;
    out->height = h;
    return w > 0 && h > 0;
}

bool wgf_gfx_priv_environment_load_image(const char *path, wgf_gfx_priv_env_image_t *out)
{
    unsigned char *bytes;
    int size = 0;
    hdr_reader_t r;
    bool ok;
    memset(out, 0, sizeof(*out));
    if (!wgf_core_priv_fs_read(path, &bytes, &size)) return false;
    ok = image_begin(bytes, size, path, out, &r) && (r.bytes == NULL || hdr_rows(&r, out, 0, out->height));
    free(r.scanline);
    wgf_core_priv_fs_read_free(bytes);
    if (!ok) {
        free(out->rgb);
        out->rgb = NULL;
    }
    return ok;
}

/* ===================================================================== GPU ==== */

typedef struct environment_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    sg_image cube;                     /* prefiltered for lighting: a mip a roughness */
    sg_view cube_view;
    sg_image background; /* the image at its own resolution, box-filtered mips (for blur) */
    sg_view background_view;
    int background_mip_count;
    wgf_gfx_priv_env_sh_t sh;
} environment_t;

static bool pool_ready;
static wgf_core_priv_handle_pool_t pool;
static environment_t *environments;

static struct {
    bool set_up;  /* asked once gfx runs */
    bool ready;   /* half floats filtered: environments can be had */
    sg_image lut; /* the split-sum table, baked */
    sg_view lut_view;
    sg_sampler cube_sampler, lut_sampler;
    sg_shader background_shader;
    sg_pipeline background_pipeline;
    sg_buffer background_triangle;
} env;

static environment_t *environment_of(wgf_handle_t environment)
{
    uint16_t index;
    if (!pool_ready || !wgf_core_priv_handle_pool_resolve(&pool, environment, &index)) return NULL;
    return &environments[index];
}

static wgf_core_priv_resource_kind_t resource_kind; /* below, with what it names */
static bool ensure(void);

static bool ensure_pool(void)
{
    if (!pool_ready) {
        pool_ready = wgf_core_priv_handle_pool_init(&pool, WGF_CORE_PRIV_HANDLE_KIND_ENVIRONMENT, (void **)&environments,
                                                    sizeof(environment_t), 8, 4096);
        if (pool_ready) wgf_core_priv_resource_register(&pool, &resource_kind);
    }
    return pool_ready;
}

/* An RGBA16F cubemap from a float RGB cube, and a view of it; false when the GPU refused it. */
static bool make_cube_image(const wgf_gfx_priv_env_cube_t *cube, sg_image *image, sg_view *view)
{
    sg_image_desc desc;
    sg_view_desc view_desc;
    uint16_t *levels[16] = {NULL};
    bool ok;
    int m;
    memset(&desc, 0, sizeof(desc));
    desc.type = SG_IMAGETYPE_CUBE;
    desc.width = desc.height = cube->size;
    desc.num_mipmaps = cube->mip_count;
    desc.pixel_format = SG_PIXELFORMAT_RGBA16F;
    desc.label = "wgf-environment-cube";
    for (m = 0; m < cube->mip_count; m++) {
        const size_t s = (size_t)(cube->size >> m > 0 ? cube->size >> m : 1), texels = 6 * s * s;
        size_t i;
        int c;
        levels[m] = (uint16_t *)malloc(texels * 4 * sizeof(uint16_t));
        if (levels[m] == NULL) {
            int k;
            for (k = 0; k < m; k++) free(levels[k]);
            return false;
        }
        for (i = 0; i < texels; i++) {
            for (c = 0; c < 3; c++) levels[m][i * 4 + c] = wgf_gfx_priv_environment_half_from_float(cube->mips[m][i * 3 + c]);
            levels[m][i * 4 + 3] = wgf_gfx_priv_environment_half_from_float(1.0f);
        }
        desc.data.mip_levels[m].ptr = levels[m];
        desc.data.mip_levels[m].size = texels * 4 * sizeof(uint16_t);
    }
    *image = sg_make_image(&desc);
    for (m = 0; m < cube->mip_count; m++) free(levels[m]);
    ok = sg_query_image_state(*image) == SG_RESOURCESTATE_VALID;
    if (ok) {
        memset(&view_desc, 0, sizeof(view_desc));
        view_desc.texture.image = *image;
        *view = sg_make_view(&view_desc);
    } else {
        sg_destroy_image(*image);
    }
    return ok;
}

/* The CPU half of loading an environment (any thread), as libwgt's, in steps
 * (prepare_step), each about a millisecond natively, so that on the web, where the main
 * thread prepares, it spreads over frames: the .hdr's rows decoded, the irradiance a band
 * of rows at a time, the source cube a band of a face at a time and its mips, then the
 * prefiltered cube a band of a face of a mip at a time. */
typedef enum stage_t { STAGE_DECODE, STAGE_SH, STAGE_SOURCE, STAGE_PREFILTER, STAGE_DONE } stage_t;

#define STEP_PIXELS 32768  /* the image's pixels a step: decoding, the irradiance */
#define STEP_SAMPLES 16384 /* the equirect samples a step (4 a source texel), or prefilter samples a step */

typedef struct prepared_t {
    wgf_gfx_priv_env_sh_t sh;
    wgf_gfx_priv_env_cube_t source;
    wgf_gfx_priv_env_cube_t prefiltered;
    /* the steps' own */
    stage_t stage;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
    unsigned char *bytes; /* the file, while its .hdr rows are read */
    hdr_reader_t reader;
    wgf_gfx_priv_env_image_t image;
    double sums[9][3];
    int face, mip, row;
    prefilter_sample_t samples[PREFILTER_SAMPLES];
    int sample_count;
} prepared_t;

static void discard(void *data)
{
    prepared_t *prepared = (prepared_t *)data;
    if (prepared == NULL) return;
    if (prepared->bytes != NULL) wgf_core_priv_fs_read_free(prepared->bytes);
    free(prepared->reader.scanline);
    free(prepared->image.rgb);
    wgf_gfx_priv_environment_cube_free(&prepared->source);
    wgf_gfx_priv_environment_cube_free(&prepared->prefiltered);
    free(prepared);
}

static void *prepare(const char *path)
{
    prepared_t *prepared = (prepared_t *)calloc(1, sizeof(prepared_t));
    int size = 0;
    if (prepared == NULL) return NULL;
    snprintf(prepared->path, sizeof(prepared->path), "%s", path);
    if (!wgf_core_priv_fs_read(path, &prepared->bytes, &size) ||
        !image_begin(prepared->bytes, size, path, &prepared->image, &prepared->reader)) {
        wgf_log_warn("wgf_gfx_environment: %s: not an image it can read", path);
        discard(prepared);
        return NULL;
    }
    if (prepared->reader.bytes == NULL) { /* decoded whole */
        wgf_core_priv_fs_read_free(prepared->bytes);
        prepared->bytes = NULL;
        prepared->stage = STAGE_SH;
    }
    return prepared;
}

/* How many rows of `width` make a step of `budget`, at least one. */
static int rows_for(int budget, int width)
{
    return width > 0 && budget / width > 0 ? budget / width : 1;
}

static wgf_core_priv_load_step_t prepare_step(void *data)
{
    prepared_t *p = (prepared_t *)data;
    wgf_gfx_priv_env_image_t *image = &p->image;
    switch (p->stage) {
        case STAGE_DECODE: {
            const int end = p->row + rows_for(STEP_PIXELS, image->width) < image->height
                                ? p->row + rows_for(STEP_PIXELS, image->width)
                                : image->height;
            if (!hdr_rows(&p->reader, image, p->row, end)) {
                wgf_log_warn("wgf_gfx_environment: %s: a broken .hdr", p->path);
                return WGF_CORE_PRIV_LOAD_FAILED;
            }
            p->row = end;
            if (end == image->height) {
                wgf_core_priv_fs_read_free(p->bytes);
                p->bytes = NULL;
                free(p->reader.scanline);
                p->reader.scanline = NULL;
                p->row = 0;
                p->stage = STAGE_SH;
            }
            return WGF_CORE_PRIV_LOAD_MORE;
        }
        case STAGE_SH: {
            const int step = rows_for(STEP_PIXELS, image->width);
            const int end = p->row + step < image->height ? p->row + step : image->height;
            project_sh_rows(image, p->row, end, p->sums);
            p->row = end;
            if (end == image->height) {
                int size = MIN_SOURCE_CUBE_SIZE;
                sh_from_sums(p->sums, &p->sh);
                while (size * 2 <= image->width / 4 && size < MAX_SOURCE_CUBE_SIZE) size *= 2;
                if (!cube_alloc(&p->source, size, mip_count_of(size))) break;
                p->row = p->face = 0;
                p->stage = STAGE_SOURCE;
            }
            return WGF_CORE_PRIV_LOAD_MORE;
        }
        case STAGE_SOURCE: {
            const int size = p->source.size, step = rows_for(STEP_SAMPLES / 4, size);
            const int end = p->row + step < size ? p->row + step : size;
            cube_rows(image, &p->source, p->face, p->row, end);
            p->row = end;
            if (end == size && ++p->face < 6) p->row = 0;
            if (p->face == 6) {
                box_mips(&p->source);
                free(image->rgb);
                image->rgb = NULL;
                if (!cube_alloc(&p->prefiltered, WGF_GFX_PRIV_ENVIRONMENT_CUBE_SIZE, WGF_GFX_PRIV_ENVIRONMENT_MIP_COUNT)) {
                    break;
                }
                p->row = p->face = p->mip = 0;
                p->sample_count = 0;
                p->stage = STAGE_PREFILTER;
            }
            return WGF_CORE_PRIV_LOAD_MORE;
        }
        case STAGE_PREFILTER: {
            const int s = p->prefiltered.size >> p->mip > 0 ? p->prefiltered.size >> p->mip : 1;
            const int step = rows_for(STEP_SAMPLES, s * (p->mip > 0 ? p->sample_count : 1));
            const int end = p->row + step < s ? p->row + step : s;
            prefilter_rows(&p->source, &p->prefiltered, p->mip, p->face, p->row, end, p->samples, p->sample_count);
            p->row = end;
            if (end < s) return WGF_CORE_PRIV_LOAD_MORE;
            p->row = 0;
            if (++p->face < 6) return WGF_CORE_PRIV_LOAD_MORE;
            p->face = 0;
            if (++p->mip == p->prefiltered.mip_count) {
                p->stage = STAGE_DONE;
                return WGF_CORE_PRIV_LOAD_DONE;
            }
            p->sample_count =
                prefilter_samples(&p->source, p->mip, p->prefiltered.mip_count, PREFILTER_SAMPLES, p->samples);
            return WGF_CORE_PRIV_LOAD_MORE;
        }
        case STAGE_DONE:
            return WGF_CORE_PRIV_LOAD_DONE;
    }
    wgf_log_warn("wgf_gfx_environment: %s: out of memory preparing it", p->path);
    return WGF_CORE_PRIV_LOAD_FAILED;
}

static void release_images(environment_t *env_ptr)
{
    if (env_ptr->cube_view.id != SG_INVALID_ID) sg_destroy_view(env_ptr->cube_view);
    if (env_ptr->cube.id != SG_INVALID_ID) sg_destroy_image(env_ptr->cube);
    if (env_ptr->background_view.id != SG_INVALID_ID) sg_destroy_view(env_ptr->background_view);
    if (env_ptr->background.id != SG_INVALID_ID) sg_destroy_image(env_ptr->background);
}

static wgf_core_priv_load_step_t finish(void *data, wgf_handle_t resource)
{
    const prepared_t *prepared = (const prepared_t *)data;
    environment_t *env_ptr = environment_of(resource);
    sg_image background = {SG_INVALID_ID}, cube = {SG_INVALID_ID};
    sg_view background_view = {SG_INVALID_ID}, cube_view = {SG_INVALID_ID};
    if (env_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    if (!ensure()) return WGF_CORE_PRIV_LOAD_MORE; /* no GPU yet: wait for one */
    if (!env.ready) {
        wgf_log_warn("wgf_gfx_environment: %s: this graphics backend can't have environments", env_ptr->resource.path);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    if (!make_cube_image(&prepared->source, &background, &background_view)) {
        wgf_log_warn("wgf_gfx_environment: %s: the GPU didn't take it", env_ptr->resource.path);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    if (!make_cube_image(&prepared->prefiltered, &cube, &cube_view)) {
        wgf_log_warn("wgf_gfx_environment: %s: the GPU didn't take it", env_ptr->resource.path);
        sg_destroy_view(background_view);
        sg_destroy_image(background);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    env_ptr = environment_of(resource);
    release_images(env_ptr); /* loaded again: the old ones go only now the new are made */
    env_ptr->background = background;
    env_ptr->background_view = background_view;
    env_ptr->cube = cube;
    env_ptr->cube_view = cube_view;
    env_ptr->background_mip_count = prepared->source.mip_count;
    env_ptr->sh = prepared->sh;
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"environment", prepare, finish, discard, fail, NULL, true, prepare_step};

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void free_environment(wgf_handle_t environment, void *record)
{
    (void)environment;
    if (env.set_up) release_images((environment_t *)record); /* sokol is still up: gfx stops its parts first */
}

static wgf_core_priv_resource_kind_t resource_kind = {.create = "wgf_environment_create",
                                                      .loader = loader_of,
                                                      .free = free_environment};

wgf_environment_t wgf_environment_create(const char *path)
{
    ensure();
    return ensure_pool() ? wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_ENVIRONMENT, path) : 0;
}

bool wgf_gfx_priv_environment_get_lighting(wgf_handle_t environment, wgf_gfx_priv_env_lighting_t *out)
{
    const environment_t *env_ptr = environment_of(environment);
    if (!env.ready || env_ptr == NULL || env_ptr->resource.status != WGF_RESOURCE_STATUS_READY) return false;
    out->cube = env_ptr->cube_view;
    out->cube_sampler = env.cube_sampler;
    out->sh = env_ptr->sh;
    out->max_lod = (float)(WGF_GFX_PRIV_ENVIRONMENT_MIP_COUNT - 1);
    out->brdf = env.lut_view;
    out->brdf_sampler = env.lut_sampler;
    return true;
}

/* ------------------------------------------------------------------ background ---- */

static sg_backend shader_backend(void)
{
    const sg_backend backend = sg_query_backend();
    return backend == SG_BACKEND_DUMMY ? SG_BACKEND_GLCORE : backend;
}

static void draw_background(wgf_handle_t environment, wgf_mat4_t view_projection, float blur, float intensity,
                            float rotation, int tonemap, float exposure)
{
    const environment_t *env_ptr = environment_of(environment);
    const wgf_mat4_t inverse = wgf_mat4_invert(view_projection);
    sg_bindings bind;
    bg_params_t params;
    if (!env.ready || env_ptr == NULL || env_ptr->resource.status != WGF_RESOURCE_STATUS_READY) return;
    memcpy(params.inv_view_proj, inverse.m, sizeof(params.inv_view_proj));
    params.bg_env[0] = intensity;
    params.bg_env[1] = clamp01(blur) * (float)(env_ptr->background_mip_count - 1);
    params.bg_env[2] = cosf(rotation);
    params.bg_env[3] = sinf(rotation);
    params.bg_tonemap[0] = (float)tonemap;
    params.bg_tonemap[1] = exp2f(exposure);
    params.bg_tonemap[2] = params.bg_tonemap[3] = 0.0f;
    memset(&bind, 0, sizeof(bind));
    bind.vertex_buffers[0] = env.background_triangle;
    bind.views[VIEW_bg_tex] = env_ptr->background_view;
    bind.samplers[SMP_bg_smp] = env.cube_sampler;
    sg_apply_pipeline(env.background_pipeline);
    sg_apply_bindings(&bind);
    sg_apply_uniforms(UB_bg_params, &SG_RANGE(params));
    sg_draw(0, 3, 1);
}

/* ------------------------------------------------------------------- lifecycle ---- */

static void setup(void)
{
    static const float triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    sg_sampler_desc sampler;
    sg_pipeline_desc pipeline;
    sg_buffer_desc buffer;
    sg_image_desc image;
    sg_view_desc view;
    memset(&env, 0, sizeof(env));
    env.set_up = true;
    if (!sg_query_pixelformat(SG_PIXELFORMAT_RGBA16F).filter) {
        wgf_log_warn("wgf_gfx_environment: half-float textures can't be filtered on this backend; environments are "
                     "off");
        return;
    }
    memset(&sampler, 0, sizeof(sampler));
    sampler.min_filter = sampler.mag_filter = sampler.mipmap_filter = SG_FILTER_LINEAR;
    sampler.wrap_u = sampler.wrap_v = sampler.wrap_w = SG_WRAP_CLAMP_TO_EDGE;
    env.cube_sampler = sg_make_sampler(&sampler);
    sampler.mipmap_filter = SG_FILTER_NEAREST;
    env.lut_sampler = sg_make_sampler(&sampler);
    env.background_shader = sg_make_shader(background_shader_desc(shader_backend()));
    memset(&pipeline, 0, sizeof(pipeline));
    pipeline.shader = env.background_shader;
    pipeline.layout.attrs[ATTR_background_bg_position].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pipeline.depth.write_enabled = false;
    pipeline.label = "wgf-background";
    env.background_pipeline = sg_make_pipeline(&pipeline);
    memset(&buffer, 0, sizeof(buffer));
    buffer.usage.vertex_buffer = true;
    buffer.data.ptr = triangle;
    buffer.data.size = sizeof(triangle);
    env.background_triangle = sg_make_buffer(&buffer);
    memset(&image, 0, sizeof(image));
    image.width = image.height = WGF_GFX_BRDF_LUT_SIZE;
    image.pixel_format = SG_PIXELFORMAT_RG8;
    image.data.mip_levels[0].ptr = wgf_gfx_brdf_lut;
    image.data.mip_levels[0].size = sizeof(wgf_gfx_brdf_lut);
    env.lut = sg_make_image(&image);
    memset(&view, 0, sizeof(view));
    view.texture.image = env.lut;
    env.lut_view = sg_make_view(&view);
    env.ready = true;
}

static void stop(void)
{
    if (pool_ready) {
        wgf_core_priv_resource_unregister(&pool);
        wgf_core_priv_handle_pool_destroy(&pool);
        pool_ready = false;
    }
    if (env.ready) {
        sg_destroy_buffer(env.background_triangle);
        sg_destroy_pipeline(env.background_pipeline);
        sg_destroy_shader(env.background_shader);
        sg_destroy_sampler(env.cube_sampler);
        sg_destroy_sampler(env.lut_sampler);
        sg_destroy_view(env.lut_view);
        sg_destroy_image(env.lut);
    }
    memset(&env, 0, sizeof(env));
}

static wgf_core_priv_part_t part = {.name = "environment",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_ENVIRONMENT,
                                    .stop = stop};

static const wgf_gfx_priv_environment_hooks_t hooks = {wgf_gfx_priv_environment_get_lighting, draw_background};

/* The first environment: environments join gfx's stop and the stage's hooks, and the
 * backend is asked once gfx runs; false until it runs. */
static bool ensure(void)
{
    if (!part.installed) {
        wgf_core_priv_load_enable_steps(); /* its preparation is in steps */
        wgf_core_priv_part_install(&part);
        wgf_gfx_priv_set_environment_hooks(&hooks);
    }
    if (!wgf_gfx_priv_render_is_running()) return false;
    if (!env.set_up) setup();
    return true;
}
