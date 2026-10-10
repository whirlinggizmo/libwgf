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

void wgf_gfx_priv_environment_project_sh(const wgf_gfx_priv_env_image_t *image, wgf_gfx_priv_env_sh_t *out)
{
    /* Lambert convolution per band (Ramamoorthi & Hanrahan), divided by pi */
    static const float band[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    double sum[9][3] = {{0}};
    const double pixel_area = (2.0 * PI_D / image->width) * (PI_D / image->height);
    int px, py, k, c;
    for (py = 0; py < image->height; py++) {
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
    for (k = 0; k < 9; k++) {
        for (c = 0; c < 3; c++) out->c[k][c] = (float)sum[k][c] * band[k];
    }
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

bool wgf_gfx_priv_environment_cube_from_equirect(const wgf_gfx_priv_env_image_t *image, int size,
                                                 wgf_gfx_priv_env_cube_t *out)
{
    int mip_count = 1, face, x, y, m, c, sx, sy;
    while ((size >> (mip_count - 1)) > 1 && mip_count < 16) mip_count++;
    if (!cube_alloc(out, size, mip_count)) return false;
    for (face = 0; face < 6; face++) { /* mip 0: 2x2 supersampled a texel */
        for (y = 0; y < size; y++) {
            for (x = 0; x < size; x++) {
                float *p = &out->mips[0][((size_t)face * size * size + (size_t)y * size + x) * 3];
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
    for (m = 1; m < mip_count; m++) { /* box-filtered mips */
        const int src = size >> (m - 1), dst = size >> m > 0 ? size >> m : 1;
        for (face = 0; face < 6; face++) {
            const float *s = out->mips[m - 1] + (size_t)face * src * src * 3;
            for (y = 0; y < dst; y++) {
                for (x = 0; x < dst; x++) {
                    float *p = &out->mips[m][((size_t)face * dst * dst + (size_t)y * dst + x) * 3];
                    const int x1 = 2 * x + 1 < src ? 2 * x + 1 : 2 * x, y1 = 2 * y + 1 < src ? 2 * y + 1 : 2 * y;
                    for (c = 0; c < 3; c++) {
                        p[c] = 0.25f * (s[(2 * y * src + 2 * x) * 3 + c] + s[(2 * y * src + x1) * 3 + c] +
                                        s[(y1 * src + 2 * x) * 3 + c] + s[(y1 * src + x1) * 3 + c]);
                    }
                }
            }
        }
    }
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

/* The GGX half vector about +z for Hammersley point i of n (alpha = roughness^2). */
static wgf_vec3_t importance_ggx(int i, int n, float alpha)
{
    const float xi_x = (float)i / (float)n, xi_y = radical_inverse((uint32_t)i);
    const float phi = 2.0f * PI_F * xi_x;
    const float cos_theta = sqrtf((1.0f - xi_y) / (1.0f + (alpha * alpha - 1.0f) * xi_y));
    const float sin_theta = sqrtf(1.0f - cos_theta * cos_theta);
    return wgf_vec3_make(sin_theta * cosf(phi), sin_theta * sinf(phi), cos_theta);
}

static wgf_vec3_t to_basis(wgf_vec3_t v, wgf_vec3_t n)
{
    const wgf_vec3_t up = fabsf(n.z) < 0.999f ? wgf_vec3_make(0, 0, 1) : wgf_vec3_make(1, 0, 0);
    const wgf_vec3_t tx = wgf_vec3_normalize(wgf_vec3_cross(up, n));
    const wgf_vec3_t ty = wgf_vec3_cross(n, tx);
    return wgf_vec3_make(tx.x * v.x + ty.x * v.y + n.x * v.z, tx.y * v.x + ty.y * v.y + n.y * v.z,
                         tx.z * v.x + ty.z * v.y + n.z * v.z);
}

bool wgf_gfx_priv_environment_prefilter(const wgf_gfx_priv_env_cube_t *source, int size, int mip_count, int samples,
                                        wgf_gfx_priv_env_cube_t *out)
{
    const float source_texel_solid_angle = 4.0f * PI_F / (6.0f * (float)source->size * (float)source->size);
    int m, face, x, y, i;
    if (!cube_alloc(out, size, mip_count)) return false;
    for (m = 0; m < mip_count; m++) {
        const int s = size >> m > 0 ? size >> m : 1;
        const float roughness = mip_count > 1 ? (float)m / (float)(mip_count - 1) : 0.0f;
        const float alpha = roughness * roughness;
        for (face = 0; face < 6; face++) {
            for (y = 0; y < s; y++) {
                for (x = 0; x < s; x++) {
                    float *p = &out->mips[m][((size_t)face * s * s + (size_t)y * s + x) * 3];
                    const wgf_vec3_t n =
                        wgf_gfx_priv_environment_cube_dir(face, ((float)x + 0.5f) / (float)s, ((float)y + 0.5f) / (float)s);
                    wgf_vec3_t sum = {0, 0, 0};
                    float weight = 0.0f;
                    if (m == 0) { /* mirror-like: the source at this resolution */
                        const wgf_vec3_t col =
                            wgf_gfx_priv_environment_sample_cube(source, n, log2f((float)source->size / (float)s));
                        p[0] = col.x;
                        p[1] = col.y;
                        p[2] = col.z;
                        continue;
                    }
                    for (i = 0; i < samples; i++) {
                        const wgf_vec3_t h = to_basis(importance_ggx(i, samples, alpha), n);
                        const float n_dot_h = wgf_vec3_dot(n, h);
                        const wgf_vec3_t l = wgf_vec3_sub(wgf_vec3_scale(h, 2.0f * n_dot_h), n); /* n about h (v = n) */
                        const float n_dot_l = wgf_vec3_dot(n, l);
                        float a2, dd, pdf, sample_solid_angle, lod;
                        wgf_vec3_t col;
                        if (n_dot_l <= 0.0f) continue;
                        /* filtered importance sampling: a blurrier source mip where samples are
                           sparse, which removes noise with few samples */
                        a2 = alpha * alpha;
                        dd = n_dot_h * n_dot_h * (a2 - 1.0f) + 1.0f;
                        pdf = a2 / (PI_F * dd * dd) * 0.25f; /* D n.h / (4 v.h), v.h == n.h */
                        sample_solid_angle = 1.0f / ((float)samples * pdf + 1e-6f);
                        lod = 0.5f * log2f(sample_solid_angle / source_texel_solid_angle) + 1.0f;
                        col = wgf_gfx_priv_environment_sample_cube(source, l, lod);
                        sum = wgf_vec3_make(sum.x + col.x * n_dot_l, sum.y + col.y * n_dot_l, sum.z + col.z * n_dot_l);
                        weight += n_dot_l;
                    }
                    if (weight > 0.0f) {
                        p[0] = sum.x / weight;
                        p[1] = sum.y / weight;
                        p[2] = sum.z / weight;
                    }
                }
            }
        }
    }
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

bool wgf_gfx_priv_environment_load_image(const char *path, wgf_gfx_priv_env_image_t *out)
{
    unsigned char *bytes;
    int size = 0, w = 0, h = 0;
    memset(out, 0, sizeof(*out));
    if (!wgf_core_priv_fs_read(path, &bytes, &size)) return false;
    if (wgf_gfx_priv_environment_is_hdr(bytes, size)) {
        float *rgb = wgf_gfx_priv_environment_decode_hdr(bytes, size, &w, &h); /* linear already */
        if (rgb != NULL && w > 0 && h > 0) {
            out->rgb = (float *)malloc((size_t)w * (size_t)h * 3 * sizeof(float));
            if (out->rgb != NULL) memcpy(out->rgb, rgb, (size_t)w * (size_t)h * 3 * sizeof(float));
        }
        wgf_gfx_priv_environment_hdr_free(rgb);
    } else { /* a PNG or JPEG: the texture loader's decoder, from sRGB */
        void *decoded = wgf_gfx_priv_texture_decode(bytes, size, path);
        if (decoded != NULL) {
            const unsigned char *pixels = wgf_gfx_priv_texture_decoded_pixels(decoded, &w, &h);
            size_t i;
            out->rgb = (float *)malloc((size_t)w * (size_t)h * 3 * sizeof(float));
            for (i = 0; out->rgb != NULL && i < (size_t)w * (size_t)h; i++) {
                out->rgb[i * 3] = wgf_gfx_priv_srgb_to_linear((float)pixels[i * 4] / 255.0f);
                out->rgb[i * 3 + 1] = wgf_gfx_priv_srgb_to_linear((float)pixels[i * 4 + 1] / 255.0f);
                out->rgb[i * 3 + 2] = wgf_gfx_priv_srgb_to_linear((float)pixels[i * 4 + 2] / 255.0f);
            }
            wgf_gfx_priv_texture_decoded_free(decoded);
        }
    }
    wgf_core_priv_fs_read_free(bytes);
    out->width = w;
    out->height = h;
    return out->rgb != NULL && w > 0 && h > 0;
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

/* The CPU half of loading an environment (any thread), as libwgt's. */
typedef struct prepared_t {
    wgf_gfx_priv_env_sh_t sh;
    wgf_gfx_priv_env_cube_t source;
    wgf_gfx_priv_env_cube_t prefiltered;
} prepared_t;

static void discard(void *data)
{
    prepared_t *prepared = (prepared_t *)data;
    if (prepared == NULL) return;
    wgf_gfx_priv_environment_cube_free(&prepared->source);
    wgf_gfx_priv_environment_cube_free(&prepared->prefiltered);
    free(prepared);
}

static void *prepare(const char *path)
{
    wgf_gfx_priv_env_image_t image;
    prepared_t *prepared;
    int source_size = MIN_SOURCE_CUBE_SIZE;
    bool built;
    if (!wgf_gfx_priv_environment_load_image(path, &image)) {
        wgf_log_warn("wgf_gfx_environment: %s: not an image it can read", path);
        free(image.rgb);
        return NULL;
    }
    prepared = (prepared_t *)calloc(1, sizeof(prepared_t));
    if (prepared == NULL) {
        free(image.rgb);
        return NULL;
    }
    wgf_gfx_priv_environment_project_sh(&image, &prepared->sh);
    while (source_size * 2 <= image.width / 4 && source_size < MAX_SOURCE_CUBE_SIZE) source_size *= 2;
    built = wgf_gfx_priv_environment_cube_from_equirect(&image, source_size, &prepared->source);
    free(image.rgb);
    if (!built || !wgf_gfx_priv_environment_prefilter(&prepared->source, WGF_GFX_PRIV_ENVIRONMENT_CUBE_SIZE,
                                                      WGF_GFX_PRIV_ENVIRONMENT_MIP_COUNT, PREFILTER_SAMPLES,
                                                      &prepared->prefiltered)) {
        wgf_log_warn("wgf_gfx_environment: %s: out of memory preparing it", path);
        discard(prepared);
        return NULL;
    }
    return prepared;
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

static const wgf_core_priv_loader_t loader = {"environment", prepare, finish, discard, fail, NULL, true};

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
        wgf_core_priv_part_install(&part);
        wgf_gfx_priv_set_environment_hooks(&hooks);
    }
    if (!wgf_gfx_priv_render_is_running()) return false;
    if (!env.set_up) setup();
    return true;
}
