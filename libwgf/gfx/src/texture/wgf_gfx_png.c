#include "texture/wgf_gfx_png_priv.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A PNG writer of libwgf's own, for screenshots (wgf_texture_read_save): each row filtered
 * by the pixel before it (PNG's Sub), the rows deflated as one block of the fixed codes
 * (RFC 1951 3.2.6), matches found through a hash of three bytes. Smaller than a stored
 * file by far for what a frame holds (flat colors, gradients); a fraction of a full
 * deflater's work, so a frame's encoding takes a few hundredths of a second. */

#define WINDOW 32768
#define HASH_BITS 15
#define CHAIN 16
#define MATCH_MAX 258

typedef struct bits_t {
    unsigned char *out;
    size_t length;
    uint32_t buffer;
    int count;
} bits_t;

static void put_bits(bits_t *bits, uint32_t value, int count)
{
    bits->buffer |= value << bits->count;
    bits->count += count;
    while (bits->count >= 8) {
        bits->out[bits->length++] = (unsigned char)(bits->buffer & 0xFFu);
        bits->buffer >>= 8;
        bits->count -= 8;
    }
}

/* A Huffman code, written its first bit first (deflate's bits otherwise go low first). */
static void put_code(bits_t *bits, uint32_t code, int length)
{
    uint32_t reversed = 0;
    int i;
    for (i = 0; i < length; i++) reversed |= ((code >> i) & 1u) << (length - 1 - i);
    put_bits(bits, reversed, length);
}

static void put_symbol(bits_t *bits, int symbol)
{
    if (symbol < 144) put_code(bits, 0x30u + (uint32_t)symbol, 8);
    else if (symbol < 256) put_code(bits, 0x190u + (uint32_t)(symbol - 144), 9);
    else if (symbol < 280) put_code(bits, (uint32_t)(symbol - 256), 7);
    else put_code(bits, 0xC0u + (uint32_t)(symbol - 280), 8);
}

static const uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                         31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t distance_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void put_match(bits_t *bits, int length, int distance)
{
    int k = 28;
    while (length_base[k] > length) k--;
    put_symbol(bits, 257 + k);
    put_bits(bits, (uint32_t)(length - length_base[k]), length_extra[k]);
    k = 29;
    while (distance_base[k] > distance) k--;
    put_code(bits, (uint32_t)k, 5);
    put_bits(bits, (uint32_t)(distance - distance_base[k]), distance_extra[k]);
}

static uint32_t hash3(const unsigned char *at)
{
    return (((uint32_t)at[0] << 16 | (uint32_t)at[1] << 8 | at[2]) * 2654435761u) >> (32 - HASH_BITS);
}

/* `data` deflated into `bits` as one final block of the fixed codes; false out of memory. */
static bool deflate_fixed(const unsigned char *data, size_t size, bits_t *bits)
{
    int *head = (int *)malloc(sizeof(int) << HASH_BITS);
    int *prev = (int *)malloc(sizeof(int) * WINDOW);
    size_t i = 0;
    if (head == NULL || prev == NULL) {
        free(head);
        free(prev);
        return false;
    }
    memset(head, 0xFF, sizeof(int) << HASH_BITS); /* -1: none */
    put_bits(bits, 1, 1); /* the last block */
    put_bits(bits, 1, 2); /* of the fixed codes */
    while (i < size) {
        int best = 0, best_distance = 0;
        if (i + 3 <= size) {
            const uint32_t h = hash3(data + i);
            const size_t most = size - i < MATCH_MAX ? size - i : MATCH_MAX;
            int j = head[h], chain = CHAIN;
            while (j >= 0 && i - (size_t)j <= WINDOW && chain-- > 0) {
                size_t n = 0;
                int next;
                while (n < most && data[(size_t)j + n] == data[i + n]) n++;
                if ((int)n > best) {
                    best = (int)n;
                    best_distance = (int)(i - (size_t)j);
                    if (n == most) break;
                }
                next = prev[j & (WINDOW - 1)];
                if (next >= j) break; /* the slot was taken by a newer position: the chain ends */
                j = next;
            }
            prev[i & (WINDOW - 1)] = head[h];
            head[h] = (int)i;
        }
        if (best >= 3) {
            size_t k;
            put_match(bits, best, best_distance);
            for (k = 1; k < (size_t)best; k++) { /* the matched positions hashed too, for later matches */
                const size_t at = i + k;
                if (at + 3 <= size) {
                    const uint32_t h = hash3(data + at);
                    prev[at & (WINDOW - 1)] = head[h];
                    head[h] = (int)at;
                }
            }
            i += (size_t)best;
        } else {
            put_symbol(bits, data[i]);
            i++;
        }
    }
    put_symbol(bits, 256); /* the block's end */
    if (bits->count > 0) put_bits(bits, 0, 8 - bits->count);
    free(head);
    free(prev);
    return true;
}

static uint32_t crc_table[256];

static uint32_t crc32_of(const unsigned char *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    if (crc_table[1] == 0) {
        uint32_t n;
        for (n = 0; n < 256; n++) {
            uint32_t c = n;
            int k;
            for (k = 0; k < 8; k++) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[n] = c;
        }
    }
    for (i = 0; i < size; i++) crc = crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void put_u32(unsigned char *at, uint32_t value)
{
    at[0] = (unsigned char)(value >> 24);
    at[1] = (unsigned char)(value >> 16);
    at[2] = (unsigned char)(value >> 8);
    at[3] = (unsigned char)value;
}

/* A chunk at `at`: its length, `type`, the `size` bytes already at at + 8, and its CRC.
 * Its whole length. */
static size_t finish_chunk(unsigned char *at, const char *type, size_t size)
{
    put_u32(at, (uint32_t)size);
    memcpy(at + 4, type, 4);
    put_u32(at + 8 + size, crc32_of(at + 4, size + 4));
    return size + 12;
}

unsigned char *wgf_gfx_priv_png_encode(const unsigned char *rgba, int width, int height, int *out_size)
{
    const size_t row = (size_t)width * 4u + 1u;
    size_t raw_size, capacity, length, y;
    unsigned char *raw, *out;
    bits_t bits;
    uint32_t a = 1, b = 0;
    if (out_size != NULL) *out_size = 0;
    if (rgba == NULL || out_size == NULL || width <= 0 || height <= 0 || width > 16384 || height > 16384) return NULL;
    raw_size = row * (size_t)height;
    raw = (unsigned char *)malloc(raw_size);
    /* the signature, IHDR, IDAT's head, the zlib stream (at most 9 bits a byte, and its
       header and sum), IDAT's CRC, IEND */
    capacity = 8 + 25 + 8 + 2 + raw_size + raw_size / 8 + 16 + 4 + 4 + 12;
    out = (unsigned char *)malloc(capacity);
    if (raw == NULL || out == NULL) {
        free(raw);
        free(out);
        return NULL;
    }
    for (y = 0; y < (size_t)height; y++) { /* Sub: each byte less the pixel's before it */
        const unsigned char *from = rgba + y * (size_t)width * 4u;
        unsigned char *to = raw + y * row;
        size_t x;
        to[0] = 1;
        for (x = 0; x < (size_t)width * 4u; x++) to[1 + x] = (unsigned char)(from[x] - (x >= 4 ? from[x - 4] : 0));
    }
    memcpy(out, "\x89PNG\r\n\x1a\n", 8);
    length = 8;
    put_u32(out + length + 8, (uint32_t)width);
    put_u32(out + length + 12, (uint32_t)height);
    memcpy(out + length + 16, "\x08\x06\x00\x00\x00", 5); /* 8 bits a channel, RGBA, deflate, adaptive filters, no interlace */
    length += finish_chunk(out + length, "IHDR", 13);
    bits.out = out + length + 8;
    bits.out[0] = 0x78; /* zlib: deflate with a 32K window, no dictionary */
    bits.out[1] = 0x01;
    bits.length = 2;
    bits.buffer = 0;
    bits.count = 0;
    if (!deflate_fixed(raw, raw_size, &bits)) {
        free(raw);
        free(out);
        return NULL;
    }
    for (y = 0; y < raw_size; y++) { /* Adler-32 of what was deflated */
        a = (a + raw[y]) % 65521u;
        b = (b + a) % 65521u;
    }
    put_u32(bits.out + bits.length, (b << 16) | a);
    bits.length += 4;
    free(raw);
    length += finish_chunk(out + length, "IDAT", bits.length);
    length += finish_chunk(out + length, "IEND", 0);
    *out_size = (int)length;
    return out;
}
