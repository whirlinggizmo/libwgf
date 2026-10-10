#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "texture/wgf_gfx_ktx_priv.h"

/* KTX files read: a good one with its mipmaps, and each way one can be wrong or cut
 * short, refused with a reason and nothing read past its end (the sanitizer presets
 * check that). libwgt's. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static unsigned char file[1024];

static void put32(size_t at, uint32_t value)
{
    file[at] = (unsigned char)value;
    file[at + 1] = (unsigned char)(value >> 8);
    file[at + 2] = (unsigned char)(value >> 16);
    file[at + 3] = (unsigned char)(value >> 24);
}

/* A KTX 1 file of `format` (a GL internal format), `width` by `height`, with `mips`
 * levels (each its blocks of 16 bytes; 0 writes one, as the header's 0 means) and
 * `key_value` bytes of key/value data; its size. A size too big for the buffer has no
 * levels written: the header refuses it first. */
static size_t make(uint32_t format, int width, int height, int mips, int key_value)
{
    static const unsigned char identifier[12] = {0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, '\r', '\n', 0x1A, '\n'};
    size_t at;
    int level, w = width, h = height;
    memset(file, 0, sizeof(file));
    memcpy(file, identifier, sizeof(identifier));
    put32(12, 0x04030201u); /* endianness */
    put32(28, format);      /* glInternalFormat */
    put32(36, (uint32_t)width);
    put32(40, (uint32_t)height);
    put32(52, 1); /* faces */
    put32(56, (uint32_t)mips);
    put32(60, (uint32_t)key_value);
    at = 64 + (size_t)key_value;
    for (level = 0; level < (mips > 0 ? mips : 1); level++) {
        const uint32_t size = (uint32_t)(((w + 3) / 4) * ((h + 3) / 4) * 16);
        if (at + 4 + size > sizeof(file)) break;
        put32(at, size);
        memset(&file[at + 4], level + 1, size); /* each level's bytes its number */
        at += 4 + size;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return at;
}

static void refused(size_t size, const char *why)
{
    wgf_gfx_priv_ktx_t ktx;
    const char *error = NULL;
    char what[160];
    const bool ok = wgf_gfx_priv_ktx_parse(file, size, &ktx, &error);
    snprintf(what, sizeof(what), "refused: %s", why);
    expect(!ok && error != NULL && strstr(error, why) != NULL, what);
    if (ok || error == NULL || strstr(error, why) == NULL) printf("  said: %s\n", error != NULL ? error : "(nothing)");
}

int main(void)
{
    wgf_gfx_priv_ktx_t ktx;
    const char *error = NULL;
    size_t size;

    /* BC7, 8 by 4, three levels: 8x4, 4x2, 2x1, a block or two each */
    size = make(0x8E8C, 8, 4, 3, 8);
    expect(wgf_gfx_priv_ktx_parse(file, size, &ktx, &error), "a good BC7 file");
    expect(ktx.format == SG_PIXELFORMAT_BC7_RGBA && ktx.width == 8 && ktx.height == 4 && ktx.mip_count == 3,
           "its format, size, and levels");
    expect(ktx.sizes[0] == 32 && ktx.sizes[1] == 16 && ktx.sizes[2] == 16 && ktx.levels[0][0] == 1 &&
               ktx.levels[1][0] == 2 && ktx.levels[2][15] == 3,
           "each level found, past the key/value data");
    size = make(0x93B0, 4, 4, 1, 0);
    expect(wgf_gfx_priv_ktx_parse(file, size, &ktx, &error) && ktx.format == SG_PIXELFORMAT_ASTC_4x4_RGBA, "ASTC 4x4");
    size = make(0x9278, 4, 4, 0, 0); /* 0 levels: one */
    expect(wgf_gfx_priv_ktx_parse(file, size, &ktx, &error) && ktx.format == SG_PIXELFORMAT_ETC2_RGBA8 &&
               ktx.mip_count == 1,
           "ETC2, 0 levels read as one");

    size = make(0x8E8C, 4, 4, 1, 0);
    file[1] = 'X';
    refused(size, "not a KTX 1 file");
    refused(40, "not a KTX 1 file");
    size = make(0x8E8C, 4, 4, 1, 0);
    put32(12, 0x01020304u);
    refused(size, "big-endian");
    size = make(0x8E8C, 4, 4, 1, 0);
    put32(16, 0x1401); /* glType: an uncompressed texture */
    refused(size, "not a compressed texture");
    refused(make(0x83F0, 4, 4, 1, 0), "not BC7, ASTC 4x4 or ETC2 RGBA"); /* DXT1 */
    refused(make(0x8E8C, 0, 4, 0, 0), "bad size");
    refused(make(0x8E8C, 20000, 4, 0, 0), "bad size");
    size = make(0x8E8C, 4, 4, 1, 0);
    put32(52, 6); /* a cubemap */
    refused(size, "only 2D textures");
    refused(make(0x8E8C, 4, 4, 40, 0), "too many mipmap levels");
    size = make(0x8E8C, 4, 4, 1, 0);
    put32(60, 4000);
    refused(size, "truncated (key/value data)");
    size = make(0x8E8C, 8, 8, 2, 0);
    refused(size - 16 - 4, "truncated (mipmap level size)"); /* the second level's size cut off */
    refused(size - 1, "truncated (mipmap data)");
    size = make(0x8E8C, 8, 8, 1, 0);
    put32(64, 16); /* says one block where there are four */
    refused(size, "doesn't match its dimensions");
    expect(!wgf_gfx_priv_ktx_parse(NULL, 0, &ktx, &error), "no bytes");
    return failures == 0 ? 0 : 1;
}
