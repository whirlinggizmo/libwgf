/* The mixer, natively, ported from wgrender's wgr_audio.c (mix_sound, stream_fill):
 * every playing voice summed into the device's buffer, resampled to its rate by its
 * pitch, on sokol_audio's thread. It copies what it needs of each playing voice under
 * the audio lock, marking the voice busy, mixes outside it (decoding a streamed sound
 * as it goes), then writes each voice's position back under the lock, unless the game
 * changed the voice meanwhile (its generation moved on), and marks it not busy. A
 * voice's decoder is the mixer's while it's busy, and the main thread's otherwise.
 * Without a window (a headless build) there is no device: tests pull the mixer
 * themselves (wgf_audio_priv_mix). This file is also voices' platform half natively
 * (wgf_audio_priv.h): the mixer reads the voices, so there's nothing to carry over. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dr_mp3.h"
#include "dr_wav.h"
#include "vorbis/vorbisfile.h"
#include "wgf_audio_priv.h"
#include "wgf_log.h"
#if !defined(SOKOL_DUMMY_BACKEND)
#include "sokol_audio.h"
#endif

#define CHUNK_FRAMES 4096 /* a streamed sound decoded so many frames at a time */
#define MOST_VOICES 256   /* mixed at once; past it, the rest wait a buffer */
/* While its file arrives, a decoder reads only with this much past where it is: dr_mp3
 * and vorbisfile take a short read as the end, and don't read on after it. More than
 * an Ogg page (at most 64 KB) and an MP3 frame. */
#define ARRIVING_MARGIN (128 * 1024)

/* ------------------------------------------------------ a reader's cursor ---- */

size_t wgf_audio_priv_cursor_read(wgf_audio_priv_cursor_t *cursor, void *out, size_t want)
{
    const wgf_audio_priv_reader_t *reader = &cursor->reader;
    const size_t left = reader->available > cursor->at ? reader->available - cursor->at : 0;
    size_t done = 0;
    if (want > left) want = left; /* what hasn't arrived reads as the end, for now */
    if (reader->bytes != NULL) {
        memcpy(out, reader->bytes + cursor->at, want);
        done = want;
    }
    while (reader->bytes == NULL && done < want) { /* across blocks */
        const size_t at = cursor->at + done, offset = at % WGF_AUDIO_PRIV_BLOCK;
        size_t n = WGF_AUDIO_PRIV_BLOCK - offset;
        if (n > want - done) n = want - done;
        memcpy((unsigned char *)out + done, reader->blocks[at / WGF_AUDIO_PRIV_BLOCK] + offset, n);
        done += n;
    }
    cursor->at += done;
    return done;
}

bool wgf_audio_priv_cursor_seek(wgf_audio_priv_cursor_t *cursor, long long offset, int origin)
{
    const wgf_audio_priv_reader_t *reader = &cursor->reader;
    long long to;
    if (origin == 2 && !reader->whole) return false; /* its end isn't known yet */
    to = origin == 0 ? offset : origin == 1 ? (long long)cursor->at + offset : (long long)reader->size + offset;
    if (to < 0 || to > (long long)reader->available) return false;
    cursor->at = (size_t)to;
    return true;
}

/* ---------------------------------------------- a streamed sound's decoder ---- */

typedef struct wgf_audio_priv_decoder_t {
    wgf_handle_t sound; /* what it decodes: reopened when the voice's sound changes */
    wgf_audio_priv_format_t format;
    int channels;
    /* the reader as the mixer last copied it, and the decoders' place in it */
    wgf_audio_priv_cursor_t cursor;
    bool unseekable; /* an Ogg opened while its file arrived: read on, or from the start again */
    drwav wav;
    drmp3 mp3;
    OggVorbis_File ogg;
    bool open;
    float buffer[CHUNK_FRAMES * 2];
    uint64_t start; /* the frame buffer[0] is */
    int count;      /* frames in the buffer */
    uint64_t next;  /* the frame the decoder reads next */
} wgf_audio_priv_decoder_t;

static size_t reader_read(void *user, void *out, size_t want)
{
    return wgf_audio_priv_cursor_read(&((wgf_audio_priv_decoder_t *)user)->cursor, out, want);
}

/* origin 0 from the start, 1 from here, 2 from the end, as each decoder numbers them */
static bool reader_seek(wgf_audio_priv_decoder_t *dec, long long offset, int origin)
{
    return wgf_audio_priv_cursor_seek(&dec->cursor, offset, origin);
}

static drwav_bool32 wav_seek(void *user, int offset, drwav_seek_origin origin)
{
    return reader_seek((wgf_audio_priv_decoder_t *)user, offset,
                       origin == DRWAV_SEEK_SET ? 0 : origin == DRWAV_SEEK_CUR ? 1 : 2);
}

static drwav_bool32 wav_tell(void *user, drwav_int64 *cursor)
{
    *cursor = (drwav_int64)((wgf_audio_priv_decoder_t *)user)->cursor.at;
    return DRWAV_TRUE;
}

static drmp3_bool32 mp3_seek(void *user, int offset, drmp3_seek_origin origin)
{
    return reader_seek((wgf_audio_priv_decoder_t *)user, offset,
                       origin == DRMP3_SEEK_SET ? 0 : origin == DRMP3_SEEK_CUR ? 1 : 2);
}

static drmp3_bool32 mp3_tell(void *user, drmp3_int64 *cursor)
{
    *cursor = (drmp3_int64)((wgf_audio_priv_decoder_t *)user)->cursor.at;
    return DRMP3_TRUE;
}

static size_t ogg_read(void *out, size_t size, size_t count, void *user)
{
    return size > 0 ? reader_read(user, out, size * count) / size : 0;
}

static int ogg_seek(void *user, ogg_int64_t offset, int whence)
{
    return reader_seek((wgf_audio_priv_decoder_t *)user, offset,
                       whence == SEEK_SET ? 0 : whence == SEEK_CUR ? 1 : 2)
               ? 0
               : -1;
}

static long ogg_tell(void *user)
{
    return (long)((wgf_audio_priv_decoder_t *)user)->cursor.at;
}

/* Whether a decoder may read on: the file whole, or the margin past where it is there. */
static bool may_read(const wgf_audio_priv_decoder_t *dec)
{
    return dec->cursor.reader.whole || dec->cursor.reader.available >= dec->cursor.at + ARRIVING_MARGIN;
}

static void decoder_shut(wgf_audio_priv_decoder_t *dec)
{
    if (dec->open) {
        if (dec->format == WGF_AUDIO_PRIV_FORMAT_WAV) drwav_uninit(&dec->wav);
        else if (dec->format == WGF_AUDIO_PRIV_FORMAT_MP3) drmp3_uninit(&dec->mp3);
        else ov_clear(&dec->ogg);
    }
    dec->open = false;
}

static void decoder_close(wgf_audio_priv_decoder_t *dec)
{
    if (dec == NULL) return;
    decoder_shut(dec);
    free(dec);
}

/* Opened from the start; false when it won't, or not enough has arrived to try. An Ogg
 * opened while its file arrives can't seek (vorbisfile would look for its end). */
static bool decoder_open(wgf_audio_priv_decoder_t *dec)
{
    decoder_shut(dec);
    dec->cursor.at = 0;
    dec->start = dec->next = 0;
    dec->count = 0;
    if (!may_read(dec)) return false;
    if (dec->format == WGF_AUDIO_PRIV_FORMAT_WAV) {
        dec->open = drwav_init(&dec->wav, reader_read, wav_seek, wav_tell, dec, NULL);
    } else if (dec->format == WGF_AUDIO_PRIV_FORMAT_MP3) {
        dec->open = drmp3_init(&dec->mp3, reader_read, mp3_seek, mp3_tell, NULL, dec, NULL);
    } else {
        const ov_callbacks callbacks = {ogg_read, ogg_seek, NULL, ogg_tell};
        const ov_callbacks unseekable = {ogg_read, NULL, NULL, NULL};
        dec->unseekable = !dec->cursor.reader.whole;
        dec->open = ov_open_callbacks(dec, &dec->ogg, NULL, 0, dec->unseekable ? unseekable : callbacks) == 0;
    }
    return dec->open;
}

static int decoder_fill(wgf_audio_priv_decoder_t *dec);

static bool decoder_seek(wgf_audio_priv_decoder_t *dec, uint64_t frame)
{
    bool sought;
    if (dec->unseekable) { /* from the start again when behind, then read on to it */
        if (frame < dec->next && !decoder_open(dec)) return false;
        while (dec->next + CHUNK_FRAMES <= frame) {
            if (!may_read(dec) || decoder_fill(dec) == 0) return false;
        }
        return true;
    }
    if (!may_read(dec)) return false;
    if (dec->format == WGF_AUDIO_PRIV_FORMAT_WAV) sought = drwav_seek_to_pcm_frame(&dec->wav, frame);
    else if (dec->format == WGF_AUDIO_PRIV_FORMAT_MP3) sought = drmp3_seek_to_pcm_frame(&dec->mp3, frame);
    else sought = ov_pcm_seek(&dec->ogg, (ogg_int64_t)frame) == 0;
    dec->next = frame;
    dec->count = 0;
    return sought;
}

/* The next chunk into the buffer; its frame count (0: the end, or what hasn't arrived). */
static int decoder_fill(wgf_audio_priv_decoder_t *dec)
{
    uint64_t got = 0;
    if (dec->format == WGF_AUDIO_PRIV_FORMAT_WAV) {
        got = drwav_read_pcm_frames_f32(&dec->wav, CHUNK_FRAMES, dec->buffer);
    } else if (dec->format == WGF_AUDIO_PRIV_FORMAT_MP3) {
        got = drmp3_read_pcm_frames_f32(&dec->mp3, CHUNK_FRAMES, dec->buffer);
    } else {
        float **planes;
        int section;
        while (got < CHUNK_FRAMES) {
            const long read = ov_read_float(&dec->ogg, &planes, (int)(CHUNK_FRAMES - got), &section);
            if (read == 0) break;
            if (read < 0) continue; /* a hole in the data: skipped */
            for (long f = 0; f < read; f++) {
                for (int c = 0; c < dec->channels; c++) dec->buffer[(got + (uint64_t)f) * (uint64_t)dec->channels + (uint64_t)c] = planes[c][f];
            }
            got += (uint64_t)read;
        }
    }
    dec->start = dec->next;
    dec->count = (int)got;
    dec->next += got;
    return dec->count;
}

/* Frame `index` of a streamed sound, into `l` and `r`; false when it isn't there (past
 * the end, or not arrived). */
static bool decoder_frame(wgf_audio_priv_decoder_t *dec, uint64_t index, float *l, float *r)
{
    const float *frame;
    if (index < dec->start || index >= dec->start + (uint64_t)dec->count) {
        if (index != dec->next && !decoder_seek(dec, index)) return false; /* rewound, or jumped */
        if (!may_read(dec)) return false; /* not arrived: wait */
        if (decoder_fill(dec) == 0 || index >= dec->start + (uint64_t)dec->count) return false;
    }
    frame = &dec->buffer[(index - dec->start) * (uint64_t)dec->channels];
    *l = frame[0];
    *r = dec->channels > 1 ? frame[1] : frame[0];
    return true;
}

/* ------------------------------------------------------------------- mixing ---- */

typedef struct job_t {
    wgf_handle_t voice, sound;
    unsigned generation;
    const float *pcm;           /* decoded */
    wgf_audio_priv_decoder_t *decoder; /* streamed */
    wgf_audio_priv_format_t format;
    wgf_audio_priv_reader_t reader; /* streamed */
    int channels, rate;
    uint64_t frames;
    bool length_known;
    double position; /* frames */
    double low, high; /* what it plays, in frames: its segment, or the whole sound */
    float pitch, gain_l, gain_r;
    bool loop, done;
    double end;
} job_t;

typedef struct snapshot_t {
    job_t jobs[MOST_VOICES];
    int count;
    float master;
} snapshot_t;

static void copy_voice(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr, void *user)
{
    snapshot_t *snapshot = (snapshot_t *)user;
    const wgf_audio_priv_sound_t *sound_ptr;
    job_t *job;
    if (voice_ptr->state != WGF_PLAY_STATE_PLAYING || voice_ptr->fresh || snapshot->count == MOST_VOICES) return;
    sound_ptr = wgf_audio_priv_sound_of(voice_ptr->sound);
    if (sound_ptr == NULL || sound_ptr->resource.status != WGF_RESOURCE_STATUS_READY || sound_ptr->sample_rate <= 0) {
        return;
    }
    job = &snapshot->jobs[snapshot->count++];
    memset(job, 0, sizeof(*job));
    job->voice = voice;
    job->sound = voice_ptr->sound;
    job->generation = voice_ptr->generation;
    job->pcm = sound_ptr->pcm;
    job->decoder = voice_ptr->decoder;
    job->format = sound_ptr->format;
    job->reader = sound_ptr->reader;
    job->length_known = sound_ptr->length_known;
    job->channels = sound_ptr->channels;
    job->rate = sound_ptr->sample_rate;
    job->frames = sound_ptr->frames;
    job->position = voice_ptr->position * (double)sound_ptr->sample_rate;
    {
        double start, end;
        wgf_audio_priv_voice_range(voice_ptr, &start, &end);
        job->low = floor(start * (double)sound_ptr->sample_rate + 0.5); /* the nearest frame */
        job->high = floor(end * (double)sound_ptr->sample_rate + 0.5);
        if (sound_ptr->length_known && job->high > (double)sound_ptr->frames) job->high = (double)sound_ptr->frames;
    }
    job->pitch = voice_ptr->pitch;
    job->gain_l = voice_ptr->volume * snapshot->master * (voice_ptr->pan > 0.0f ? 1.0f - voice_ptr->pan : 1.0f);
    job->gain_r = voice_ptr->volume * snapshot->master * (voice_ptr->pan < 0.0f ? 1.0f + voice_ptr->pan : 1.0f);
    job->loop = voice_ptr->loop;
    voice_ptr->busy = true;
}

/* Frame `index` of a decoded sound. */
static void pcm_frame(const job_t *job, uint64_t index, float *l, float *r)
{
    const float *frame = &job->pcm[index * (uint64_t)job->channels];
    *l = frame[0];
    *r = job->channels > 1 ? frame[1] : frame[0];
}

/* `job` summed into `out`, `frames` frames at `rate`, linearly interpolated. */
static void mix_job(job_t *job, float *out, int frames, int rate)
{
    const double step = (double)job->pitch * (double)job->rate / (double)rate;
    const double length = job->high - job->low;
    if (job->pitch == 0.0f || length <= 0.0) return; /* held where it is: silent */
    if (job->pcm == NULL) {
        if (job->decoder != NULL && job->decoder->sound != job->sound) { /* the voice's sound changed */
            decoder_close(job->decoder);
            job->decoder = NULL;
        }
        if (job->decoder == NULL) {
            job->decoder = (wgf_audio_priv_decoder_t *)calloc(1, sizeof(wgf_audio_priv_decoder_t));
            if (job->decoder == NULL) return;
            job->decoder->sound = job->sound;
            job->decoder->format = job->format;
            job->decoder->channels = job->channels;
        }
        job->decoder->cursor.reader = job->reader;
        if (!job->decoder->open && !decoder_open(job->decoder)) return; /* not enough there yet */
    }
    for (int i = 0; i < frames; i++) {
        const uint64_t index = (uint64_t)floor(job->position);
        const float t = (float)(job->position - floor(job->position));
        uint64_t following = index + 1;
        float l0, r0, l1, r1;
        if ((double)following >= job->high) following = job->loop ? (uint64_t)job->low : (uint64_t)job->high - 1;
        if (job->pcm != NULL) {
            pcm_frame(job, index < job->frames ? index : job->frames - 1, &l0, &r0);
            pcm_frame(job, following, &l1, &r1);
        } else if (!decoder_frame(job->decoder, index, &l0, &r0)) {
            if ((double)index >= job->high) { /* the end */
                job->done = !job->loop;
                job->end = job->high;
                if (job->done) return;
                job->position = job->low;
                continue;
            }
            return; /* not arrived: wait, where it is */
        } else if (!decoder_frame(job->decoder, following, &l1, &r1)) {
            l1 = l0;
            r1 = r0;
        }
        out[i * 2] += (l0 + (l1 - l0) * t) * job->gain_l;
        out[i * 2 + 1] += (r0 + (r1 - r0) * t) * job->gain_r;
        job->position += step;
        if (job->position >= job->high) {
            if (!job->loop) {
                job->done = true;
                job->end = job->high;
                return;
            }
            job->position = job->low + fmod(job->position - job->low, length);
        } else if (job->position < job->low) {
            if (!job->loop) {
                job->done = true;
                job->end = job->low;
                return;
            }
            job->position = job->high - fmod(job->low - job->position, length);
        }
    }
}

void wgf_audio_priv_mix(float *out, int frames, int rate)
{
    static snapshot_t snapshot; /* the mixer's alone: one mix at a time */
    memset(out, 0, sizeof(float) * (size_t)frames * 2);
    if (rate <= 0) return;
    wgf_audio_priv_lock();
    snapshot.count = 0;
    snapshot.master = wgf_audio_priv_get_volume();
    if (!wgf_audio_priv_is_paused()) wgf_audio_priv_voice_each(copy_voice, &snapshot);
    wgf_audio_priv_unlock();

    for (int j = 0; j < snapshot.count; j++) mix_job(&snapshot.jobs[j], out, frames, rate);

    wgf_audio_priv_lock();
    for (int j = 0; j < snapshot.count; j++) {
        const job_t *job = &snapshot.jobs[j];
        wgf_audio_priv_voice_t *voice_ptr = wgf_audio_priv_voice_of(job->voice);
        if (voice_ptr == NULL) {
            decoder_close(job->decoder); /* can't be: a busy voice isn't freed; but never leak */
            continue;
        }
        voice_ptr->busy = false;
        voice_ptr->decoder = job->decoder;
        if (voice_ptr->generation != job->generation || voice_ptr->dying) continue; /* changed meanwhile */
        if (job->done) wgf_audio_priv_voice_complete(voice_ptr, job->end / (double)job->rate);
        else voice_ptr->position = job->position / (double)job->rate;
    }
    wgf_audio_priv_unlock();
}

/* ------------------------------------------------------------- the device ---- */

#if !defined(SOKOL_DUMMY_BACKEND)
static int device_rate; /* read once at the start, under the lock: the callback must not
                           call sokol_audio (saudio_shutdown clears its state) */
static float device_stereo[CHUNK_FRAMES * 2];

static void device_callback(float *buffer, int frames, int channels)
{
    int rate;
    wgf_audio_priv_lock();
    rate = device_rate;
    wgf_audio_priv_unlock();
    if (channels == 2) {
        wgf_audio_priv_mix(buffer, frames, rate);
        return;
    }
    for (int done = 0; done < frames;) { /* another layout: mixed in stereo, then spread */
        const int n = frames - done < CHUNK_FRAMES ? frames - done : CHUNK_FRAMES;
        wgf_audio_priv_mix(device_stereo, n, rate);
        for (int i = 0; i < n; i++) {
            for (int c = 0; c < channels; c++) {
                buffer[(done + i) * channels + c] =
                    channels == 1 ? (device_stereo[i * 2] + device_stereo[i * 2 + 1]) * 0.5f
                                  : (c < 2 ? device_stereo[i * 2 + c] : 0.0f);
            }
        }
        done += n;
    }
}
#endif

#if !defined(SOKOL_DUMMY_BACKEND)
/* saudio_setup was called: saudio_shutdown must be too, a device or not, or the next
 * start's setup asserts (a runner with no sound card stops and starts audio again). */
static bool device_set_up;
#endif

void wgf_audio_priv_platform_start(void)
{
#if !defined(SOKOL_DUMMY_BACKEND)
    saudio_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.num_channels = 2;
    desc.stream_cb = device_callback;
    saudio_setup(&desc);
    device_set_up = true;
    if (!saudio_isvalid()) {
        wgf_log_warn("wgf_audio: no audio device; voices play on, unheard");
        return;
    }
    wgf_audio_priv_lock();
    device_rate = saudio_sample_rate();
    wgf_audio_priv_unlock();
#endif
}

void wgf_audio_priv_platform_stop(void)
{
#if !defined(SOKOL_DUMMY_BACKEND)
    if (device_set_up) saudio_shutdown(); /* stops the device's thread before anything is freed */
    device_set_up = false;
    device_rate = 0;
#endif
}

void wgf_audio_priv_platform_apply(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr)
{
    (void)voice;
    (void)voice_ptr; /* the mixer reads the voice itself */
}

double wgf_audio_priv_platform_position(wgf_handle_t voice, const wgf_audio_priv_voice_t *voice_ptr)
{
    (void)voice;
    return voice_ptr->position;
}

void wgf_audio_priv_platform_free_voice(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr)
{
    (void)voice;
    decoder_close(voice_ptr->decoder);
    voice_ptr->decoder = NULL;
}

void wgf_audio_priv_platform_update(void)
{
}

void wgf_audio_priv_platform_master(float volume, bool paused)
{
    (void)volume;
    (void)paused; /* the mixer reads them itself */
}
