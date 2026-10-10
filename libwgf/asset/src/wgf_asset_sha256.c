#include "wgf_asset_priv.h"

#include <stdint.h>
#include <string.h>

/* SHA-256 as FIPS 180-4 gives it, one message at a time: the files it hashes are
 * whole in memory already (a download, or a cached file read to be checked). wgrender's
 * wgr_sha256.c, ported, its one call split into begin, feed, and end, so a large file can
 * be hashed a slice an update. */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotr(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void block(uint32_t h[8], const unsigned char *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, k;
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        const uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

void wgf_asset_priv_sha256_begin(wgf_asset_priv_sha256_t *state)
{
    static const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(state->h, initial, sizeof(initial));
    state->size = 0;
    state->pending = 0;
}

void wgf_asset_priv_sha256_feed(wgf_asset_priv_sha256_t *state, const unsigned char *data, size_t size)
{
    state->size += size;
    if (state->pending > 0) { /* fill the block a last feed left part of */
        const size_t take = size < 64 - state->pending ? size : 64 - state->pending;
        memcpy(state->block + state->pending, data, take);
        state->pending += take;
        data += take;
        size -= take;
        if (state->pending < 64) return;
        block(state->h, state->block);
        state->pending = 0;
    }
    for (; size >= 64; data += 64, size -= 64) block(state->h, data);
    memcpy(state->block, data, size);
    state->pending = size;
}

void wgf_asset_priv_sha256_end(wgf_asset_priv_sha256_t *state, char out[WGF_ASSET_PRIV_SHA256_TEXT])
{
    static const char HEX[] = "0123456789abcdef";
    unsigned char tail[128];
    const uint64_t bits = (uint64_t)state->size * 8u;
    const size_t rest = state->pending;
    const size_t tail_size = rest < 56 ? 64 : 128;
    /* the last partial block, a 1 bit, zeros, and the length in bits: one block or two */
    memset(tail, 0, sizeof(tail));
    memcpy(tail, state->block, rest);
    tail[rest] = 0x80;
    for (int i = 0; i < 8; i++) tail[tail_size - 1 - i] = (unsigned char)(bits >> (i * 8));
    block(state->h, tail);
    if (tail_size == 128) block(state->h, tail + 64);
    memcpy(out, "sha256:", 7);
    for (int i = 0; i < 32; i++) {
        const unsigned char byte = (unsigned char)(state->h[i / 4] >> (24 - (i % 4) * 8));
        out[7 + i * 2] = HEX[byte >> 4];
        out[8 + i * 2] = HEX[byte & 15];
    }
    out[71] = '\0';
}

void wgf_asset_priv_sha256_text(const unsigned char *data, size_t size, char out[WGF_ASSET_PRIV_SHA256_TEXT])
{
    wgf_asset_priv_sha256_t state;
    wgf_asset_priv_sha256_begin(&state);
    wgf_asset_priv_sha256_feed(&state, data, size);
    wgf_asset_priv_sha256_end(&state, out);
}
