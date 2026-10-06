/* A sound's load, natively, ported from wgrender's wgr_audio.c: the file read and its
 * format found by its content on a worker; a decoded sound decoded whole into float
 * samples there, a streamed one kept as its file's bytes (wgf_audio_priv_reader_t) for
 * the mixer to decode as it plays; then published into the record under the lock. The
 * decoders are dr_wav, dr_mp3, and Xiph's vorbisfile (wgf_audio_decoders_impl.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dr_mp3.h"
#include "dr_wav.h"
#include "vorbis/vorbisfile.h"
#include "wgf_audio_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_log.h"

typedef struct prepared_t {
    wgf_audio_priv_format_t format;
    int channels, sample_rate;
    uint64_t frames;
    float *pcm;           /* decoded */
    unsigned char *bytes; /* streamed: the file, fs's (wgf_core_priv_fs_read_free) */
    int size;
} prepared_t;

/* ------------------------------------------------- Ogg through vorbisfile ---- */

/* A file in memory, read through vorbisfile's callbacks. */
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
    const ogg_int64_t to = whence == SEEK_SET   ? offset
                           : whence == SEEK_CUR ? (ogg_int64_t)m->at + offset
                                                : (ogg_int64_t)m->size + offset;
    if (to < 0 || to > (ogg_int64_t)m->size) return -1;
    m->at = (size_t)to;
    return 0;
}

static long memory_tell(void *source)
{
    return (long)((memory_t *)source)->at;
}

static const ov_callbacks memory_callbacks = {memory_read, memory_seek, NULL, memory_tell};

/* --------------------------------------------------------------- probing ---- */

static bool ends_with(const char *s, const char *suffix)
{
    const size_t ls = strlen(s), lsuf = strlen(suffix);
    if (lsuf > ls) return false;
    for (size_t i = 0; i < lsuf; i++) {
        char c = s[ls - lsuf + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != suffix[i]) return false;
    }
    return true;
}

/* The format of `data` by its content, trying first what its name says, with its
 * channels, rate, and length; NONE when no decoder takes it. */
static wgf_audio_priv_format_t probe(const unsigned char *data, size_t size, const char *path, prepared_t *out)
{
    const wgf_audio_priv_format_t first = ends_with(path, ".ogg")   ? WGF_AUDIO_PRIV_FORMAT_OGG
                                          : ends_with(path, ".wav") ? WGF_AUDIO_PRIV_FORMAT_WAV
                                                                    : WGF_AUDIO_PRIV_FORMAT_MP3;
    const wgf_audio_priv_format_t order[3] = {first, first == WGF_AUDIO_PRIV_FORMAT_WAV ? WGF_AUDIO_PRIV_FORMAT_MP3
                                                                                          : WGF_AUDIO_PRIV_FORMAT_WAV,
                                              first == WGF_AUDIO_PRIV_FORMAT_OGG ? WGF_AUDIO_PRIV_FORMAT_MP3
                                                                                 : WGF_AUDIO_PRIV_FORMAT_OGG};
    for (int i = 0; i < 3; i++) {
        if (order[i] == WGF_AUDIO_PRIV_FORMAT_WAV) {
            drwav wav;
            if (!drwav_init_memory(&wav, data, size, NULL)) continue;
            out->channels = (int)wav.channels;
            out->sample_rate = (int)wav.sampleRate;
            out->frames = wav.totalPCMFrameCount;
            drwav_uninit(&wav);
        } else if (order[i] == WGF_AUDIO_PRIV_FORMAT_MP3) {
            drmp3 mp3;
            if (!drmp3_init_memory(&mp3, data, size, NULL)) continue;
            out->channels = (int)mp3.channels;
            out->sample_rate = (int)mp3.sampleRate;
            out->frames = drmp3_get_pcm_frame_count(&mp3); /* reads the frames' headers, not their samples */
            drmp3_uninit(&mp3);
        } else {
            memory_t memory = {data, size, 0};
            OggVorbis_File file;
            vorbis_info *info;
            ogg_int64_t total;
            if (ov_open_callbacks(&memory, &file, NULL, 0, memory_callbacks) != 0) continue;
            info = ov_info(&file, -1);
            total = ov_pcm_total(&file, -1);
            out->channels = info != NULL ? info->channels : 0;
            out->sample_rate = info != NULL ? (int)info->rate : 0;
            out->frames = total > 0 ? (uint64_t)total : 0;
            ov_clear(&file);
        }
        if (out->channels >= 1 && out->sample_rate > 0 && out->frames > 0) return order[i];
    }
    return WGF_AUDIO_PRIV_FORMAT_NONE;
}

/* --------------------------------------------------------------- decoding ---- */

static float *decode_ogg(const unsigned char *data, size_t size, uint64_t frames, int channels)
{
    memory_t memory = {data, size, 0};
    OggVorbis_File file;
    float *pcm, **planes;
    uint64_t done = 0;
    int section;
    long got;
    if (ov_open_callbacks(&memory, &file, NULL, 0, memory_callbacks) != 0) return NULL;
    pcm = (float *)calloc((size_t)frames * (size_t)channels, sizeof(float));
    while (pcm != NULL && done < frames && (got = ov_read_float(&file, &planes, 4096, &section)) != 0) {
        if (got < 0) continue; /* a hole in the data: skipped, as a voice would */
        if ((uint64_t)got > frames - done) got = (long)(frames - done);
        for (long f = 0; f < got; f++) {
            for (int c = 0; c < channels; c++) pcm[(done + (uint64_t)f) * (uint64_t)channels + (uint64_t)c] = planes[c][f];
        }
        done += (uint64_t)got;
    }
    ov_clear(&file);
    return pcm;
}

static float *decode(const unsigned char *data, size_t size, prepared_t *out)
{
    if (out->format == WGF_AUDIO_PRIV_FORMAT_WAV) {
        drwav_uint64 frames;
        unsigned int channels, rate;
        float *pcm = drwav_open_memory_and_read_pcm_frames_f32(data, size, &channels, &rate, &frames, NULL);
        out->frames = frames;
        return pcm;
    }
    if (out->format == WGF_AUDIO_PRIV_FORMAT_MP3) {
        drmp3_config config;
        drmp3_uint64 frames;
        float *pcm = drmp3_open_memory_and_read_pcm_frames_f32(data, size, &config, &frames, NULL);
        out->frames = frames;
        return pcm;
    }
    return decode_ogg(data, size, out->frames, out->channels);
}

/* dr_libs' buffers are freed with their own free (the C runtime's, by default). */
static void free_pcm(wgf_audio_priv_format_t format, float *pcm)
{
    if (format == WGF_AUDIO_PRIV_FORMAT_WAV) drwav_free(pcm, NULL);
    else if (format == WGF_AUDIO_PRIV_FORMAT_MP3) drmp3_free(pcm, NULL);
    else free(pcm);
}

/* ------------------------------------------------------ a file arriving ---- */

/* A streamed sound's file still arriving (core's arrival; ROADMAP.md, phase 3): read a
 * piece an update on the main thread, by fs's rule for a file read while it arrives
 * (wgf_core_fs_priv.h), into blocks the mixer reads from. READY once its start decodes,
 * with its length when the file says it (WAV's header, an MP3's Xing or Info frame),
 * else 0 until the file is whole; FAILED if the file is. */

#define FEED_MOST (1024 * 1024) /* read in an update, at most, a sound */
#define PROBE_EVERY (64 * 1024) /* tried again as so much more arrives */

typedef struct wgf_audio_priv_arriving_t {
    wgf_handle_t arrival;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];    /* where it will be */
    char partial[WGF_CORE_PRIV_FS_PATH_MAX]; /* where it is until then */
    bool probed;                             /* its start decoded: READY */
    size_t probed_at;                        /* what had arrived at the last try */
} wgf_audio_priv_arriving_t;

static wgf_handle_t *feeding; /* sounds whose files are arriving */
static int feeding_count, feeding_capacity;

static size_t cursor_read(void *user, void *out, size_t want)
{
    return wgf_audio_priv_cursor_read((wgf_audio_priv_cursor_t *)user, out, want);
}

static drwav_bool32 cursor_wav_seek(void *user, int offset, drwav_seek_origin origin)
{
    return wgf_audio_priv_cursor_seek((wgf_audio_priv_cursor_t *)user, offset,
                                      origin == DRWAV_SEEK_SET ? 0 : origin == DRWAV_SEEK_CUR ? 1 : 2);
}

static drwav_bool32 cursor_wav_tell(void *user, drwav_int64 *at)
{
    *at = (drwav_int64)((wgf_audio_priv_cursor_t *)user)->at;
    return DRWAV_TRUE;
}

static drmp3_bool32 cursor_mp3_seek(void *user, int offset, drmp3_seek_origin origin)
{
    return wgf_audio_priv_cursor_seek((wgf_audio_priv_cursor_t *)user, offset,
                                      origin == DRMP3_SEEK_SET ? 0 : origin == DRMP3_SEEK_CUR ? 1 : 2);
}

static drmp3_bool32 cursor_mp3_tell(void *user, drmp3_int64 *at)
{
    *at = (drmp3_int64)((wgf_audio_priv_cursor_t *)user)->at;
    return DRMP3_TRUE;
}

static size_t cursor_ogg_read(void *out, size_t size, size_t count, void *user)
{
    return size > 0 ? cursor_read(user, out, size * count) / size : 0;
}

static int cursor_ogg_seek(void *user, ogg_int64_t offset, int whence)
{
    return wgf_audio_priv_cursor_seek((wgf_audio_priv_cursor_t *)user, offset,
                                      whence == SEEK_SET ? 0 : whence == SEEK_CUR ? 1 : 2)
               ? 0
               : -1;
}

static long cursor_ogg_tell(void *user)
{
    return (long)((wgf_audio_priv_cursor_t *)user)->at;
}

/* The format of what has arrived of `reader`, as probe finds a whole file's, with its
 * length when known: by the file's own word while it arrives, and counted once whole, as
 * a local file's is. */
static wgf_audio_priv_format_t probe_reader(const wgf_audio_priv_reader_t *reader, const char *path,
                                            prepared_t *out, bool *length_known)
{
    const wgf_audio_priv_format_t first = ends_with(path, ".ogg")   ? WGF_AUDIO_PRIV_FORMAT_OGG
                                          : ends_with(path, ".wav") ? WGF_AUDIO_PRIV_FORMAT_WAV
                                                                    : WGF_AUDIO_PRIV_FORMAT_MP3;
    const wgf_audio_priv_format_t order[3] = {first, first == WGF_AUDIO_PRIV_FORMAT_WAV ? WGF_AUDIO_PRIV_FORMAT_MP3
                                                                                          : WGF_AUDIO_PRIV_FORMAT_WAV,
                                              first == WGF_AUDIO_PRIV_FORMAT_OGG ? WGF_AUDIO_PRIV_FORMAT_MP3
                                                                                 : WGF_AUDIO_PRIV_FORMAT_OGG};
    for (int i = 0; i < 3; i++) {
        wgf_audio_priv_cursor_t cursor = {*reader, 0};
        out->frames = 0;
        *length_known = false;
        if (order[i] == WGF_AUDIO_PRIV_FORMAT_WAV) {
            drwav wav;
            if (!drwav_init(&wav, cursor_read, cursor_wav_seek, cursor_wav_tell, &cursor, NULL)) continue;
            out->channels = (int)wav.channels;
            out->sample_rate = (int)wav.sampleRate;
            out->frames = wav.totalPCMFrameCount; /* its header's */
            *length_known = true;
            drwav_uninit(&wav);
        } else if (order[i] == WGF_AUDIO_PRIV_FORMAT_MP3) {
            drmp3 mp3;
            if (!drmp3_init(&mp3, cursor_read, cursor_mp3_seek, cursor_mp3_tell, NULL, &cursor, NULL)) continue;
            out->channels = (int)mp3.channels;
            out->sample_rate = (int)mp3.sampleRate;
            if (reader->whole || mp3.totalPCMFrameCount != DRMP3_UINT64_MAX) { /* whole, or its Xing frame says */
                out->frames = drmp3_get_pcm_frame_count(&mp3);
                *length_known = true;
            }
            drmp3_uninit(&mp3);
        } else {
            const ov_callbacks seekable = {cursor_ogg_read, cursor_ogg_seek, NULL, cursor_ogg_tell};
            const ov_callbacks unseekable = {cursor_ogg_read, NULL, NULL, NULL};
            OggVorbis_File file;
            vorbis_info *info;
            if (ov_open_callbacks(&cursor, &file, NULL, 0, reader->whole ? seekable : unseekable) != 0) continue;
            info = ov_info(&file, -1);
            out->channels = info != NULL ? info->channels : 0;
            out->sample_rate = info != NULL ? (int)info->rate : 0;
            if (reader->whole) {
                const ogg_int64_t total = ov_pcm_total(&file, -1);
                out->frames = total > 0 ? (uint64_t)total : 0;
                *length_known = true;
            }
            ov_clear(&file);
        }
        if (out->channels >= 1 && out->sample_rate > 0 && (!*length_known || out->frames > 0)) return order[i];
    }
    return WGF_AUDIO_PRIV_FORMAT_NONE;
}

static void arriving_done(int index)
{
    feeding[index] = feeding[--feeding_count];
}

/* The sound FAILED, its file let go of; its blocks stay until it is freed, as a voice
 * may be reading them. */
static void arriving_fail(wgf_handle_t sound, wgf_audio_priv_sound_t *record)
{
    wgf_audio_priv_arriving_t *arriving = record->arriving;
    if (wgf_core_priv_load_hooks.arrival_end != NULL) wgf_core_priv_load_hooks.arrival_end(arriving->arrival);
    wgf_audio_priv_lock();
    record->arriving = NULL;
    wgf_core_priv_resource_failed(sound);
    wgf_audio_priv_unlock();
    free(arriving);
}

void wgf_audio_priv_sound_arrive(wgf_handle_t sound, wgf_audio_priv_sound_t *record, const char *path,
                                 wgf_handle_t arrival)
{
    wgf_audio_priv_arriving_t *arriving = (wgf_audio_priv_arriving_t *)calloc(1, sizeof(*arriving));
    unsigned char **blocks = (unsigned char **)calloc(WGF_AUDIO_PRIV_BLOCKS, sizeof(unsigned char *));
    if (feeding_count == feeding_capacity) {
        const int capacity = feeding_capacity > 0 ? feeding_capacity * 2 : 8;
        wgf_handle_t *grown = (wgf_handle_t *)realloc(feeding, sizeof(wgf_handle_t) * (size_t)capacity);
        if (grown != NULL) {
            feeding = grown;
            feeding_capacity = capacity;
        }
    }
    if (arriving == NULL || blocks == NULL || feeding_count == feeding_capacity ||
        !wgf_core_priv_fs_partial_path(path, arriving->partial, sizeof(arriving->partial))) {
        free(arriving);
        free(blocks);
        if (wgf_core_priv_load_hooks.arrival_end != NULL) wgf_core_priv_load_hooks.arrival_end(arrival);
        wgf_core_priv_resource_failed(sound);
        return;
    }
    arriving->arrival = arrival;
    snprintf(arriving->path, sizeof(arriving->path), "%s", path);
    wgf_audio_priv_lock();
    record->arriving = arriving;
    record->reader.blocks = blocks;
    wgf_audio_priv_unlock();
    feeding[feeding_count++] = sound;
}

/* What came of one sound's file: false once it is done with (whole, or FAILED). */
static bool feed_one(wgf_handle_t sound, wgf_audio_priv_sound_t *record)
{
    wgf_audio_priv_arriving_t *arriving = record->arriving;
    const wgf_core_priv_arrival_t state =
        wgf_core_priv_load_hooks.arrival != NULL ? wgf_core_priv_load_hooks.arrival(arriving->arrival)
                                                 : WGF_CORE_PRIV_ARRIVAL_FAILED;
    size_t available = record->reader.available; /* only this thread changes it */
    long long got = 0;
    bool whole;
    if (state == WGF_CORE_PRIV_ARRIVAL_FAILED) { /* logged by what fetched it */
        arriving_fail(sound, record);
        return false;
    }
    /* checked first: a file that failed is gone from .part/, and what is under its path
       then is the copy from before, never the rest of this one */
    for (size_t read = 0; read < FEED_MOST;) {
        const size_t block = available / WGF_AUDIO_PRIV_BLOCK, offset = available % WGF_AUDIO_PRIV_BLOCK;
        if (block >= WGF_AUDIO_PRIV_BLOCKS) {
            wgf_log_warn("wgf_sound: %s: %.0f bytes and more: over the 2 GB a streamed sound may be", arriving->path,
                         (double)available);
            arriving_fail(sound, record);
            return false;
        }
        if (record->reader.blocks[block] == NULL &&
            (record->reader.blocks[block] = (unsigned char *)malloc(WGF_AUDIO_PRIV_BLOCK)) == NULL) {
            got = -1; /* no memory now: again next update, never taken for the end */
            break;
        }
        got = wgf_core_priv_fs_read_at(arriving->partial, available, record->reader.blocks[block] + offset,
                                       WGF_AUDIO_PRIV_BLOCK - offset);
        if (got < 0) { /* gone from .part/: under its path, from the same place */
            got = wgf_core_priv_fs_read_at(arriving->path, available, record->reader.blocks[block] + offset,
                                           WGF_AUDIO_PRIV_BLOCK - offset);
        }
        if (got <= 0) break;
        available += (size_t)got;
        read += (size_t)got;
    }
    whole = state == WGF_CORE_PRIV_ARRIVAL_WHOLE && got == 0;
    if (!arriving->probed && (whole || available >= arriving->probed_at + PROBE_EVERY)) {
        wgf_audio_priv_reader_t so_far = record->reader;
        prepared_t found;
        bool length_known;
        memset(&found, 0, sizeof(found));
        so_far.available = available;
        so_far.size = whole ? available : 0;
        so_far.whole = whole;
        arriving->probed_at = available;
        found.format = probe_reader(&so_far, arriving->path, &found, &length_known);
        if (found.format != WGF_AUDIO_PRIV_FORMAT_NONE && found.channels > 2) {
            wgf_log_warn("wgf_sound: %s: %d channels; a sound is mono or stereo", arriving->path, found.channels);
            arriving_fail(sound, record);
            return false;
        }
        if (found.format != WGF_AUDIO_PRIV_FORMAT_NONE) {
            wgf_audio_priv_lock();
            record->format = found.format;
            record->channels = found.channels;
            record->sample_rate = found.sample_rate;
            record->frames = length_known ? found.frames : 0;
            record->duration = length_known ? (float)((double)found.frames / (double)found.sample_rate) : 0.0f;
            record->length_known = length_known;
            record->reader.available = available;
            wgf_core_priv_resource_loaded(sound, NULL);
            wgf_audio_priv_unlock();
            arriving->probed = true;
        } else if (whole) {
            wgf_log_warn("wgf_sound: %s: not WAV, MP3, or Ogg Vorbis that decodes", arriving->path);
            arriving_fail(sound, record);
            return false;
        }
    }
    if (whole) { /* its length counted as a local file's is, then the file let go of */
        wgf_audio_priv_reader_t all = record->reader;
        prepared_t found;
        bool length_known = false;
        memset(&found, 0, sizeof(found));
        all.available = all.size = available;
        all.whole = true;
        if (probe_reader(&all, arriving->path, &found, &length_known) == WGF_AUDIO_PRIV_FORMAT_NONE) {
            found.frames = record->frames; /* what it said while it arrived */
            length_known = record->length_known;
        }
        if (wgf_core_priv_load_hooks.arrival_end != NULL) wgf_core_priv_load_hooks.arrival_end(arriving->arrival);
        wgf_audio_priv_lock();
        record->reader = all;
        if (length_known) {
            record->frames = found.frames;
            record->duration = (float)((double)found.frames / (double)record->sample_rate);
            record->length_known = true;
        }
        record->arriving = NULL;
        wgf_audio_priv_unlock();
        free(arriving);
        return false;
    }
    wgf_audio_priv_lock();
    record->reader.available = available;
    wgf_audio_priv_unlock();
    return true;
}

void wgf_audio_priv_sound_feed(void)
{
    for (int i = 0; i < feeding_count;) {
        const wgf_handle_t sound = feeding[i];
        wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(sound);
        if (sound_ptr == NULL || sound_ptr->arriving == NULL || !feed_one(sound, sound_ptr)) {
            arriving_done(i); /* freed (its arrival ended then), whole, or FAILED */
        } else {
            i++;
        }
    }
}

/* ------------------------------------------------------------- the load ---- */

void *wgf_audio_priv_sound_prepare(const char *path, bool streamed)
{
    unsigned char *data;
    int size;
    prepared_t *prepared;
    if (!wgf_core_priv_fs_read(path, &data, &size)) return NULL;
    prepared = (prepared_t *)calloc(1, sizeof(prepared_t));
    if (prepared == NULL) {
        wgf_core_priv_fs_read_free(data);
        return NULL;
    }
    prepared->format = probe(data, (size_t)size, path, prepared);
    if (prepared->format == WGF_AUDIO_PRIV_FORMAT_NONE) {
        wgf_log_warn("wgf_sound: %s: not WAV, MP3, or Ogg Vorbis that decodes", path);
    } else if (prepared->channels > 2) {
        wgf_log_warn("wgf_sound: %s: %d channels; a sound is mono or stereo", path, prepared->channels);
        prepared->format = WGF_AUDIO_PRIV_FORMAT_NONE;
    } else if (streamed) {
        prepared->bytes = data; /* decoded as it plays */
        prepared->size = size;
        return prepared;
    } else if ((prepared->pcm = decode(data, (size_t)size, prepared)) == NULL || prepared->frames == 0) {
        wgf_log_warn("wgf_sound: %s: didn't decode", path);
        if (prepared->pcm != NULL) free_pcm(prepared->format, prepared->pcm);
        prepared->pcm = NULL;
        prepared->format = WGF_AUDIO_PRIV_FORMAT_NONE;
    }
    wgf_core_priv_fs_read_free(data);
    if (prepared->format == WGF_AUDIO_PRIV_FORMAT_NONE) {
        free(prepared);
        return NULL;
    }
    return prepared;
}

wgf_core_priv_load_step_t wgf_audio_priv_sound_finish(void *data, wgf_handle_t sound, wgf_audio_priv_sound_t *record)
{
    prepared_t *prepared = (prepared_t *)data;
    wgf_audio_priv_lock();
    record->format = prepared->format;
    record->channels = prepared->channels;
    record->sample_rate = prepared->sample_rate;
    record->frames = prepared->frames;
    record->duration = (float)((double)prepared->frames / (double)prepared->sample_rate);
    record->length_known = true;
    record->pcm = prepared->pcm;
    record->reader.bytes = prepared->bytes;
    record->reader.size = record->reader.available = (size_t)prepared->size; /* a local file: all of it */
    record->reader.whole = true;
    wgf_audio_priv_unlock();
    prepared->pcm = NULL; /* the record's now */
    prepared->bytes = NULL;
    wgf_core_priv_resource_loaded(sound, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

void wgf_audio_priv_sound_discard(void *data)
{
    prepared_t *prepared = (prepared_t *)data;
    if (prepared == NULL) return;
    if (prepared->pcm != NULL) free_pcm(prepared->format, prepared->pcm);
    if (prepared->bytes != NULL) wgf_core_priv_fs_read_free(prepared->bytes);
    free(prepared);
}

void wgf_audio_priv_sound_free(wgf_handle_t sound, wgf_audio_priv_sound_t *record)
{
    (void)sound;
    if (record->pcm != NULL) free_pcm(record->format, record->pcm);
    if (record->reader.bytes != NULL) wgf_core_priv_fs_read_free(record->reader.bytes);
    if (record->reader.blocks != NULL) {
        for (int i = 0; i < WGF_AUDIO_PRIV_BLOCKS && record->reader.blocks[i] != NULL; i++) free(record->reader.blocks[i]);
        free(record->reader.blocks);
    }
    if (record->arriving != NULL) { /* freed while its file arrived */
        if (wgf_core_priv_load_hooks.arrival_end != NULL) wgf_core_priv_load_hooks.arrival_end(record->arriving->arrival);
        free(record->arriving);
    }
    record->pcm = NULL;
    record->arriving = NULL;
    memset(&record->reader, 0, sizeof(record->reader));
}

void wgf_audio_priv_sound_stop(void)
{
    free(feeding); /* every sound freed: their files let go of */
    feeding = NULL;
    feeding_count = feeding_capacity = 0;
}
