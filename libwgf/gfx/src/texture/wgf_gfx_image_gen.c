#include "wgf_image.h"

#include <math.h>
#include <stdint.h>

#include "texture/wgf_gfx_image_priv.h"
#include "wgf_log.h"
#include "wgf_trig.h"

/* Images generated, raylib's GenImage*: gradients, checks, and noise. Gradients are
 * worked in floats, their ends exact; noise is worked in integers alone (a hash of its
 * cell and the seed, Perlin's in 16.16 fixed point), so a seed gives the same pixels on
 * every platform, which floats, contracted into fused multiply-adds on some and not
 * others, wouldn't promise. */

/* --- helpers ------------------------------------------------------------------ */

static void put(unsigned char *p, wgf_color_t color)
{
    p[0] = (unsigned char)(color >> 24);
    p[1] = (unsigned char)(color >> 16);
    p[2] = (unsigned char)(color >> 8);
    p[3] = (unsigned char)color;
}

/* `a` toward `b` by `f`, 0 to 1 (clamped): `a` at 0 and `b` at 1 exactly, each channel
 * rounded. */
static void put_mix(unsigned char *p, wgf_color_t a, wgf_color_t b, float f)
{
    int shift;
    if (!(f > 0.0f)) f = 0.0f; /* NaN too */
    if (f > 1.0f) f = 1.0f;
    for (shift = 24; shift >= 0; shift -= 8) {
        const float ca = (float)((a >> shift) & 0xFFu), cb = (float)((b >> shift) & 0xFFu);
        *p++ = (unsigned char)(int)(ca + (cb - ca) * f + 0.5f);
    }
}

static void put_gray(unsigned char *p, int value)
{
    p[0] = p[1] = p[2] = (unsigned char)value;
    p[3] = 255;
}

static float clamp01(float value)
{
    return value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f; /* NaN: 0 */
}

/* A 32-bit hash of three numbers (Chris Wellons' lowbias32 over their mix). */
static uint32_t hash3(uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t h = (x * 0x8da6b343u) ^ (y * 0xd8163841u) ^ (z * 0xcb1ab31fu);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

/* --- gradients and checks ------------------------------------------------------- */

wgf_image_t wgf_image_create_gradient_linear(int width, int height, float angle, wgf_color_t start, wgf_color_t end)
{
    unsigned char *pixels;
    const wgf_image_t image = wgf_gfx_priv_image_add(width, height, &pixels, "wgf_image_create_gradient_linear");
    const float radians = isfinite(angle) ? fmodf(angle, 360.0f) * 0.017453292519943295f : 0.0f;
    float dx = wgf_trig_cos(radians), dy = wgf_trig_sin(radians), low, high, span;
    int x, y;
    if (image == 0) return 0;
    if (fabsf(dx) < 1e-6f) dx = 0.0f; /* a right angle's cosine, exactly: its ends exact */
    if (fabsf(dy) < 1e-6f) dy = 0.0f;
    { /* the corners furthest back and furthest along the direction */
        const float corners[4] = {0.0f, (float)(width - 1) * dx, (float)(height - 1) * dy,
                                  (float)(width - 1) * dx + (float)(height - 1) * dy};
        int i;
        low = high = corners[0];
        for (i = 1; i < 4; i++) {
            if (corners[i] < low) low = corners[i];
            if (corners[i] > high) high = corners[i];
        }
    }
    span = high - low;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const float along = (float)x * dx + (float)y * dy;
            put_mix(pixels + ((size_t)y * (size_t)width + (size_t)x) * 4, start, end,
                    span > 0.0f ? (along - low) / span : 0.0f);
        }
    }
    return image;
}

wgf_image_t wgf_image_create_gradient_radial(int width, int height, float density, wgf_color_t inner,
                                             wgf_color_t outer)
{
    unsigned char *pixels;
    const wgf_image_t image = wgf_gfx_priv_image_add(width, height, &pixels, "wgf_image_create_gradient_radial");
    const float cx = (float)(width - 1) * 0.5f, cy = (float)(height - 1) * 0.5f;
    const float radius = (cx < cy ? cx : cy), solid = radius * clamp01(density);
    int x, y;
    if (image == 0) return 0;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const float ox = (float)x - cx, oy = (float)y - cy, distance = sqrtf(ox * ox + oy * oy);
            const float f = radius > solid ? (distance - solid) / (radius - solid) : (distance > solid ? 1.0f : 0.0f);
            put_mix(pixels + ((size_t)y * (size_t)width + (size_t)x) * 4, inner, outer, f);
        }
    }
    return image;
}

wgf_image_t wgf_image_create_gradient_square(int width, int height, float density, wgf_color_t inner,
                                             wgf_color_t outer)
{
    unsigned char *pixels;
    const wgf_image_t image = wgf_gfx_priv_image_add(width, height, &pixels, "wgf_image_create_gradient_square");
    const float cx = (float)(width - 1) * 0.5f, cy = (float)(height - 1) * 0.5f, solid = clamp01(density);
    int x, y;
    if (image == 0) return 0;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) { /* 0 at the middle, 1 at the edges' pixels */
            const float nx = cx > 0.0f ? fabsf((float)x - cx) / cx : 0.0f;
            const float ny = cy > 0.0f ? fabsf((float)y - cy) / cy : 0.0f;
            const float distance = nx > ny ? nx : ny;
            const float f = solid < 1.0f ? (distance - solid) / (1.0f - solid) : (distance >= 1.0f ? 1.0f : 0.0f);
            put_mix(pixels + ((size_t)y * (size_t)width + (size_t)x) * 4, inner, outer, f);
        }
    }
    return image;
}

wgf_image_t wgf_image_create_checked(int width, int height, int check_width, int check_height, wgf_color_t a,
                                     wgf_color_t b)
{
    unsigned char *pixels;
    wgf_image_t image;
    int x, y;
    if (check_width <= 0 || check_height <= 0) {
        wgf_log_error("wgf_image_create_checked: checks of %dx%d", check_width, check_height);
        return 0;
    }
    image = wgf_gfx_priv_image_add(width, height, &pixels, "wgf_image_create_checked");
    if (image == 0) return 0;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            put(pixels + ((size_t)y * (size_t)width + (size_t)x) * 4,
                (x / check_width + y / check_height) % 2 == 0 ? a : b);
        }
    }
    return image;
}

/* --- noise ------------------------------------------------------------------------ */

wgf_image_t wgf_image_create_white_noise(int width, int height, float factor, int seed)
{
    unsigned char *pixels;
    const wgf_image_t image = wgf_gfx_priv_image_add(width, height, &pixels, "wgf_image_create_white_noise");
    const uint32_t threshold = (uint32_t)(clamp01(factor) * 65536.0f); /* of 65536: a power of two, exact */
    int x, y;
    if (image == 0) return 0;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const uint32_t h = hash3((uint32_t)x, (uint32_t)y, (uint32_t)seed) & 0xFFFFu;
            put_gray(pixels + ((size_t)y * (size_t)width + (size_t)x) * 4, h < threshold ? 255 : 0);
        }
    }
    return image;
}

#define ONE 65536 /* 1 in 16.16 fixed point */
#define OCTAVES 6

/* a times b in 16.16, rounded toward zero: defined for negative numbers, as a shift isn't */
static int64_t mul(int64_t a, int64_t b)
{
    return a * b / ONE;
}

/* Perlin's fade, 6t^5 - 15t^4 + 10t^3, for t in 0 to ONE. */
static int64_t fade(int64_t t)
{
    return mul(mul(mul(t, t), t), mul(t, 6 * t - 15 * ONE) + 10 * ONE);
}

/* The corner (cx, cy)'s gradient, one of eight, dotted with the offset (dx, dy) from it. */
static int64_t corner(uint32_t cx, uint32_t cy, uint32_t seed, int64_t dx, int64_t dy)
{
    switch (hash3(cx, cy, seed) & 7u) {
    case 0: return dx + dy;
    case 1: return -dx + dy;
    case 2: return dx - dy;
    case 3: return -dx - dy;
    case 4: return dx;
    case 5: return -dx;
    case 6: return dy;
    default: return -dy;
    }
}

/* Perlin noise at (x, y), 16.16 with the cell in the bits above the fraction, kept as
 * unsigned so a negative place wraps (defined) and its cell is its floor's: -1 to 1, in
 * 16.16. */
static int64_t perlin(uint64_t x, uint64_t y, uint32_t seed)
{
    const uint32_t cx = (uint32_t)(x >> 16), cy = (uint32_t)(y >> 16);
    const int64_t fx = (int64_t)(x & 0xFFFFu), fy = (int64_t)(y & 0xFFFFu);
    const int64_t u = fade(fx), v = fade(fy);
    const int64_t n00 = corner(cx, cy, seed, fx, fy), n10 = corner(cx + 1u, cy, seed, fx - ONE, fy);
    const int64_t n01 = corner(cx, cy + 1u, seed, fx, fy - ONE), n11 = corner(cx + 1u, cy + 1u, seed, fx - ONE, fy - ONE);
    const int64_t top = n00 + mul(n10 - n00, u), bottom = n01 + mul(n11 - n01, u);
    return top + mul(bottom - top, v);
}

/* `value` over `divisor` (> 0), rounded down. */
static int64_t floor_div(int64_t value, int64_t divisor)
{
    const int64_t q = value / divisor;
    return q * divisor > value ? q - 1 : q;
}

wgf_image_t wgf_image_create_perlin_noise(int width, int height, int offset_x, int offset_y, float scale, int seed)
{
    unsigned char *pixels;
    wgf_image_t image;
    int64_t scale_fixed;
    int x, y, octave;
    if (!(scale > 0.0f && scale <= 4096.0f)) { /* NaN too */
        wgf_log_error("wgf_image_create_perlin_noise: a scale of %g", (double)scale);
        return 0;
    }
    image = wgf_gfx_priv_image_add(width, height, &pixels, "wgf_image_create_perlin_noise");
    if (image == 0) return 0;
    scale_fixed = (int64_t)(scale * 65536.0f); /* by a power of two: exact, then truncated */
    for (y = 0; y < height; y++) {
        /* the first octave's place, in 16.16: `scale` cells across the image's width, square */
        const uint64_t py = (uint64_t)floor_div(((int64_t)y + offset_y) * scale_fixed, width);
        for (x = 0; x < width; x++) {
            const uint64_t px = (uint64_t)floor_div(((int64_t)x + offset_x) * scale_fixed, width);
            int64_t sum = 0;
            for (octave = 0; octave < OCTAVES; octave++) { /* twice the frequency, half the weight */
                sum += perlin(px << octave, py << octave, (uint32_t)seed + (uint32_t)octave * 0x9e3779b9u) /
                       ((int64_t)1 << octave);
            }
            if (sum < -ONE) sum = -ONE;
            if (sum > ONE) sum = ONE;
            put_gray(pixels + ((size_t)y * (size_t)width + (size_t)x) * 4, (int)((sum + ONE) * 255 / (2 * ONE)));
        }
    }
    return image;
}

/* The square root of `value`, rounded down. */
static uint32_t isqrt(uint64_t value)
{
    uint64_t root = 0, bit = (uint64_t)1 << 62;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)root;
}

wgf_image_t wgf_image_create_cellular(int width, int height, int tile_size, int seed)
{
    unsigned char *pixels;
    wgf_image_t image;
    int x, y;
    if (tile_size <= 0) {
        wgf_log_error("wgf_image_create_cellular: a tile's side of %d", tile_size);
        return 0;
    }
    image = wgf_gfx_priv_image_add(width, height, &pixels, "wgf_image_create_cellular");
    if (image == 0) return 0;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const int tx = x / tile_size, ty = y / tile_size;
            uint64_t nearest = UINT64_MAX;
            uint32_t distance;
            int i, j;
            for (j = ty - 1; j <= ty + 1; j++) { /* the points of this tile and those around it */
                for (i = tx - 1; i <= tx + 1; i++) {
                    const uint32_t h = hash3((uint32_t)i, (uint32_t)j, (uint32_t)seed);
                    const int64_t ox = (int64_t)i * tile_size + (int64_t)((h & 0xFFFFu) % (uint32_t)tile_size) - x;
                    const int64_t oy = (int64_t)j * tile_size + (int64_t)((h >> 16) % (uint32_t)tile_size) - y;
                    const uint64_t d = (uint64_t)(ox * ox + oy * oy);
                    if (d < nearest) nearest = d;
                }
            }
            distance = isqrt(nearest);
            put_gray(pixels + ((size_t)y * (size_t)width + (size_t)x) * 4,
                     distance >= (uint32_t)tile_size ? 255 : (int)(distance * 255u / (uint32_t)tile_size));
        }
    }
    return image;
}
