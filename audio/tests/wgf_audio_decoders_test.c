#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dr_mp3.h"
#include "dr_wav.h"
#include "vorbis/vorbisfile.h"

/* The decoders on untrusted files (deps/README.md, "Untrusted input"): a whole WAV,
 * MP3, and Ogg file decodes; cut short at every length through its header and at
 * steps after, with its header's fields made nonsense, and with bytes flipped through
 * it, each either fails to open or decodes what it can. Run under the sanitizer
 * presets, nothing may read past a file or overflow: the test passing elsewhere only
 * says nothing crashed. The MP3 and Ogg are the examples' (examples/assets/, CC0); Ogg
 * through Xiph's vorbisfile. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

#define MOST_FRAMES (1 << 20) /* a broken header can claim hours: decode no more than this */

static float scratch[4096 * 8];

/* Frames decoded, or -1 when the file didn't open. */
static long decode_wav(const unsigned char *data, size_t size)
{
    drwav wav;
    long total = 0;
    drwav_uint64 got;
    if (!drwav_init_memory(&wav, data, size, NULL)) return -1;
    if (wav.channels == 0 || wav.channels > 8) {
        drwav_uninit(&wav);
        return 0;
    }
    while (total < MOST_FRAMES && (got = drwav_read_pcm_frames_f32(&wav, 4096, scratch)) > 0) total += (long)got;
    drwav_uninit(&wav);
    return total;
}

static long decode_mp3(const unsigned char *data, size_t size)
{
    drmp3 mp3;
    long total = 0;
    drmp3_uint64 got;
    if (!drmp3_init_memory(&mp3, data, size, NULL)) return -1;
    if (mp3.channels == 0 || mp3.channels > 8) {
        drmp3_uninit(&mp3);
        return 0;
    }
    while (total < MOST_FRAMES && (got = drmp3_read_pcm_frames_f32(&mp3, 4096, scratch)) > 0) total += (long)got;
    drmp3_uninit(&mp3);
    return total;
}

/* A file in memory, read through vorbisfile's callbacks, as the streamed reader will. */
typedef struct memory_t {
    const unsigned char *data;
    size_t size, at;
} memory_t;

static size_t memory_read(void *out, size_t size, size_t count, void *source)
{
    memory_t *m = (memory_t *)source;
    size_t want = size * count, left = m->size - m->at;
    if (want > left) want = left;
    memcpy(out, m->data + m->at, want);
    m->at += want;
    return size > 0 ? want / size : 0;
}

static int memory_seek(void *source, ogg_int64_t offset, int whence)
{
    memory_t *m = (memory_t *)source;
    ogg_int64_t to = whence == SEEK_SET ? offset : whence == SEEK_CUR ? (ogg_int64_t)m->at + offset
                                                                        : (ogg_int64_t)m->size + offset;
    if (to < 0 || to > (ogg_int64_t)m->size) return -1;
    m->at = (size_t)to;
    return 0;
}

static long memory_tell(void *source)
{
    return (long)((memory_t *)source)->at;
}

static long decode_ogg(const unsigned char *data, size_t size)
{
    const ov_callbacks callbacks = {memory_read, memory_seek, NULL, memory_tell};
    memory_t memory = {data, size, 0};
    OggVorbis_File file;
    long total = 0, got;
    int section;
    float **pcm;
    if (ov_open_callbacks(&memory, &file, NULL, 0, callbacks) != 0) return -1;
    while (total < MOST_FRAMES && (got = ov_read_float(&file, &pcm, 4096, &section)) != 0) {
        if (got > 0) total += got; /* a hole in the data (negative) is skipped, as a voice would */
    }
    ov_clear(&file);
    return total;
}

typedef long (*decode_fn)(const unsigned char *data, size_t size);

/* Each length of `file` through its first `header` bytes, then every step to its end,
 * from a copy of exactly that length, so a read past it is past the allocation. */
static void cut_short(decode_fn decode, const unsigned char *file, size_t size, size_t header)
{
    size_t length, step = size / 97 + 1;
    for (length = 0; length < size; length += length < header ? 1 : step) {
        unsigned char *copy = (unsigned char *)malloc(length + 1);
        if (copy == NULL) continue;
        memcpy(copy, file, length);
        (void)decode(copy, length);
        free(copy);
    }
}

/* Bytes flipped through `file`, by a fixed seed: the same broken files every run. */
static void flipped(decode_fn decode, const unsigned char *file, size_t size, int variants)
{
    uint32_t seed = 0x2545F491u;
    unsigned char *copy = (unsigned char *)malloc(size);
    if (copy == NULL) return;
    for (int v = 0; v < variants; v++) {
        memcpy(copy, file, size);
        for (int k = 0; k < 8; k++) {
            seed = seed * 1664525u + 1013904223u;
            copy[(seed >> 8) % size] ^= (unsigned char)(1u << (seed & 7u));
        }
        (void)decode(copy, size);
    }
    free(copy);
}

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    unsigned char *data = NULL;
    long length = 0;
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) > 0 && fseek(file, 0, SEEK_SET) == 0 &&
        (data = (unsigned char *)malloc((size_t)length)) != NULL && fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(file);
    *size = data != NULL ? (size_t)length : 0;
    return data;
}

static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) { put16(p, v & 0xFFFFu); put16(p + 2, v >> 16); }

/* A 16-bit stereo WAV of `frames` frames, a ramp: 44 bytes of header, then the data. */
static size_t make_wav(unsigned char *out, int frames)
{
    const uint32_t data_size = (uint32_t)frames * 4u;
    memcpy(out, "RIFF", 4);
    put32(out + 4, 36u + data_size);
    memcpy(out + 8, "WAVEfmt ", 8);
    put32(out + 16, 16);
    put16(out + 20, 1);           /* PCM */
    put16(out + 22, 2);           /* channels */
    put32(out + 24, 44100);       /* rate */
    put32(out + 28, 44100u * 4u); /* bytes a second */
    put16(out + 32, 4);           /* block align */
    put16(out + 34, 16);          /* bits */
    memcpy(out + 36, "data", 4);
    put32(out + 40, data_size);
    for (int i = 0; i < frames * 2; i++) put16(out + 44 + i * 2, (unsigned)(i * 37) & 0xFFFFu);
    return 44 + data_size;
}

static void test_wav(void)
{
    enum { FRAMES = 2000 };
    static unsigned char wav[44 + FRAMES * 4], broken[44 + FRAMES * 4];
    const size_t size = make_wav(wav, FRAMES);
    /* each header field made nonsense: offset, value */
    static const struct { int at, bytes; uint32_t value; } fields[] = {
        {4, 4, 0xFFFFFFFFu},  {16, 4, 0xFFFFFFF0u}, {16, 4, 0},  {20, 2, 0xFFFE}, {20, 2, 3},  {22, 2, 0},
        {22, 2, 0xFFFF},      {24, 4, 0},           {28, 4, 0},  {32, 2, 0},      {32, 2, 0xFFFF}, {34, 2, 0},
        {34, 2, 7},           {34, 2, 64},          {34, 2, 0xFFFF}, {40, 4, 0xFFFFFFFFu}, {40, 4, 0x7FFFFFFFu},
    };
    expect(decode_wav(wav, size) == FRAMES, "a whole WAV decodes every frame");
    cut_short(decode_wav, wav, size, 64);
    for (size_t f = 0; f < sizeof(fields) / sizeof(fields[0]); f++) {
        memcpy(broken, wav, size);
        if (fields[f].bytes == 2) put16(broken + fields[f].at, fields[f].value);
        else put32(broken + fields[f].at, fields[f].value);
        (void)decode_wav(broken, size);
    }
    flipped(decode_wav, wav, size, 256);
}

static void test_file(const char *name, decode_fn decode, size_t most, size_t header)
{
    char path[512];
    size_t size;
    unsigned char *file;
    snprintf(path, sizeof(path), "%s/%s", WGF_TEST_ASSETS, name);
    file = read_file(path, &size);
    expect(file != NULL, name);
    if (file == NULL) return;
    if (size > most) size = most; /* the start of a long file: cut short already, and quick */
    expect(decode(file, size) > 0, "the file's start decodes");
    cut_short(decode, file, size, header);
    flipped(decode, file, size, 64);
    free(file);
}

int main(void)
{
    test_wav();
    test_file("music/a_hero_is_born.mp3", decode_mp3, 48 * 1024, 512);
    test_file("sounds/click_004.ogg", decode_ogg, 64 * 1024, 4096);
    return failures == 0 ? 0 : 1;
}
