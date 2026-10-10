#include "texture/wgf_gfx_ktx_priv.h"

#include <stdint.h>
#include <string.h>

/* KTX 1: a 12-byte identifier, 13 little-endian 32-bit header fields, key/value data,
 * then each mipmap level as its byte size followed by its data (padded to 4 bytes).
 * https://registry.khronos.org/KTX/specs/1.0/ktxspec.v1.html */

#define HEADER_SIZE 64
#define BLOCK_BYTES 16 /* a 4x4 block of every format below */

static const unsigned char IDENTIFIER[12] = {0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, '\r', '\n', 0x1A, '\n'};

enum {
    GL_COMPRESSED_RGBA_BPTC_UNORM = 0x8E8C,   /* BC7 */
    GL_COMPRESSED_RGBA_ASTC_4x4_KHR = 0x93B0, /* ASTC 4x4 */
    GL_COMPRESSED_RGBA8_ETC2_EAC = 0x9278     /* ETC2 RGBA */
};

static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t field(const unsigned char *bytes, int index)
{
    return read32(bytes + 12 + index * 4);
}

/* The format gfx uploads it as: the plain (not sRGB) ones, like RGBA8 textures, whose
 * colors are sRGB values the shaders treat as such. */
static sg_pixel_format format_of(uint32_t gl_internal_format)
{
    switch (gl_internal_format) {
        case GL_COMPRESSED_RGBA_BPTC_UNORM: return SG_PIXELFORMAT_BC7_RGBA;
        case GL_COMPRESSED_RGBA_ASTC_4x4_KHR: return SG_PIXELFORMAT_ASTC_4x4_RGBA;
        case GL_COMPRESSED_RGBA8_ETC2_EAC: return SG_PIXELFORMAT_ETC2_RGBA8;
        default: return SG_PIXELFORMAT_NONE;
    }
}

static bool fail(const char **error, const char *why)
{
    if (error != NULL) *error = why;
    return false;
}

bool wgf_gfx_priv_ktx_parse(const unsigned char *bytes, size_t size, wgf_gfx_priv_ktx_t *out, const char **error)
{
    uint32_t key_value_bytes, mips, level;
    size_t at;
    int w, h;

    memset(out, 0, sizeof(*out));
    if (bytes == NULL || size < HEADER_SIZE || memcmp(bytes, IDENTIFIER, sizeof(IDENTIFIER)) != 0) {
        return fail(error, "not a KTX 1 file");
    }
    if (field(bytes, 0) != 0x04030201u) return fail(error, "big-endian KTX files aren't supported");
    if (field(bytes, 1) != 0 || field(bytes, 3) != 0) {
        return fail(error, "not a compressed texture (glType / glFormat set)");
    }
    out->format = format_of(field(bytes, 4));
    if (out->format == SG_PIXELFORMAT_NONE) return fail(error, "not BC7, ASTC 4x4 or ETC2 RGBA");
    if (field(bytes, 6) < 1 || field(bytes, 7) < 1 || field(bytes, 6) > 16384 || field(bytes, 7) > 16384) {
        return fail(error, "bad size");
    }
    out->width = (int)field(bytes, 6);
    out->height = (int)field(bytes, 7);
    if (field(bytes, 8) > 1 || field(bytes, 9) > 1 || field(bytes, 10) != 1) {
        return fail(error, "only 2D textures (not 3D, arrays or cubemaps)");
    }
    mips = field(bytes, 11) == 0 ? 1 : field(bytes, 11);
    if (mips > SG_MAX_MIPMAPS) return fail(error, "too many mipmap levels");
    key_value_bytes = field(bytes, 12);
    if (key_value_bytes > size - HEADER_SIZE) return fail(error, "truncated (key/value data)");
    at = HEADER_SIZE + key_value_bytes;
    w = out->width;
    h = out->height;
    for (level = 0; level < mips; level++) {
        const size_t expected = (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * BLOCK_BYTES;
        uint32_t level_size;
        if (at + 4 > size) return fail(error, "truncated (mipmap level size)");
        level_size = read32(bytes + at);
        at += 4;
        if (level_size != expected) return fail(error, "a mipmap level's size doesn't match its dimensions");
        if (level_size > size - at) return fail(error, "truncated (mipmap data)");
        out->levels[level] = bytes + at;
        out->sizes[level] = level_size;
        at += (level_size + 3u) & ~3u;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    out->mip_count = (int)mips;
    return true;
}
