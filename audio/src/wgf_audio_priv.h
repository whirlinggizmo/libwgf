#ifndef WGF_AUDIO_PRIV_H
#define WGF_AUDIO_PRIV_H

#include "wgf_core_handle_priv.h" /* WGF_CORE_PRIV_CALLER */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wgf_core_resource_priv.h"
#include "wgf_handle.h"
#include "wgf_play_state.h"

/* audio's private parts, shared by its files: the lock, the sounds' records, and each
 * platform's half of a sound's load (wgf_audio_sound_native.c, wgf_audio_sound_web.c). */

/* The audio lock: what the mixer shares with the game thread (voices, sounds, the
 * reader's count, the mixer's settings, the pools) changes only under it. Not
 * recursive: no audio call takes it inside another. Set up by the first sound. */
void wgf_audio_priv_lock(void);
void wgf_audio_priv_unlock(void);

/* Audio joins core's part list and its lock is made (wgf_audio.c): by the first sound.
 * Its stop frees every sound (wgf_audio_priv_sound_stop_all), then the lock. */
void wgf_audio_priv_install(void);
void wgf_audio_priv_sound_stop_all(void);

typedef enum wgf_audio_priv_format_t {
    WGF_AUDIO_PRIV_FORMAT_NONE = 0,
    WGF_AUDIO_PRIV_FORMAT_WAV,
    WGF_AUDIO_PRIV_FORMAT_MP3,
    WGF_AUDIO_PRIV_FORMAT_OGG
} wgf_audio_priv_format_t;

/* A streamed sound's file, natively: its bytes, and how many have arrived. A file that
 * is local has them all, held at once (bytes); one still arriving (core's arrival) is
 * read into blocks as it comes, behind a table made once, so nothing the mixer reads
 * ever moves. Copied under the lock; the bytes below `available` never change. */
#define WGF_AUDIO_PRIV_BLOCK (256 * 1024)
#define WGF_AUDIO_PRIV_BLOCKS 8192 /* 2 GB: the most a file arriving can be */
typedef struct wgf_audio_priv_reader_t {
    unsigned char *bytes;   /* a whole file */
    unsigned char **blocks; /* or one arriving: WGF_AUDIO_PRIV_BLOCK bytes each */
    size_t size;            /* the whole file's, once whole */
    size_t available;       /* arrived so far, from the start */
    bool whole;             /* all of it there */
} wgf_audio_priv_reader_t;

/* A place in a reader's bytes, read as a decoder reads a file: up to what has arrived,
 * which reads as the end for now. seek's origin is 0 from the start, 1 from here, 2 from
 * the end (only once whole); false, and nothing moved, past what has arrived. */
typedef struct wgf_audio_priv_cursor_t {
    wgf_audio_priv_reader_t reader;
    size_t at;
} wgf_audio_priv_cursor_t;
size_t wgf_audio_priv_cursor_read(wgf_audio_priv_cursor_t *cursor, void *out, size_t want);
bool wgf_audio_priv_cursor_seek(wgf_audio_priv_cursor_t *cursor, long long offset, int origin);

/* A named segment of a decoded sound (CONVENTIONS.md: a named segment is added to the
 * resource and selected by name on the object). Each allocated on its own and never
 * moved until its sound is freed, so a voice may point at it and its name be handed out;
 * adding the same name again changes its range in place. */
typedef struct wgf_audio_priv_segment_t {
    char name[32];
    double start, end; /* seconds, as asked; the end clamped to the sound's length when used */
} wgf_audio_priv_segment_t;

typedef struct wgf_audio_priv_sound_t {
    wgf_core_priv_resource_t resource; /* first: the resource core's (status, path, references) */
    bool streamed;
    wgf_audio_priv_format_t format;
    int channels;    /* 1 or 2 */
    int sample_rate; /* natively; on the web the browser's */
    uint64_t frames; /* natively: sample frames, a channel's samples */
    float duration;  /* seconds */
    bool length_known; /* frames and duration: false for a file arriving that doesn't say it */
    float *pcm;      /* natively, decoded: interleaved */
    wgf_audio_priv_reader_t reader; /* natively, streamed */
    struct wgf_audio_priv_arriving_t *arriving; /* natively, its file arriving: the main thread's */
    wgf_audio_priv_segment_t **segments; /* decoded only; changed under the lock */
    int segment_count, segment_capacity;
} wgf_audio_priv_sound_t;

/* The segment of `sound_ptr` named `name`; NULL for none. Under the lock. */
wgf_audio_priv_segment_t *wgf_audio_priv_sound_segment(const wgf_audio_priv_sound_t *sound_ptr, const char *name);

/* The sound `sound` is, decoded or streamed; NULL for anything else. Under the lock when
 * another thread may read it. */
wgf_audio_priv_sound_t *wgf_audio_priv_sound_of_at(wgf_handle_t sound, const char *caller);
#define wgf_audio_priv_sound_of(sound) wgf_audio_priv_sound_of_at((sound), WGF_CORE_PRIV_CALLER)

/* Each platform's half of a sound's load and life: prepare on any thread (the file at
 * `path`), finish on the main thread, discard what was prepared, free what a record
 * holds. finish publishes into the record under the lock. */
void *wgf_audio_priv_sound_prepare(const char *path, bool streamed);
/* Natively, a streamed sound's file still arriving (the loader's arrive, core's
 * arrival): read as it comes, READY once its start decodes, its length set once known.
 * On the web never called. feed, once an update (audio's part), reads what came. */
void wgf_audio_priv_sound_arrive(wgf_handle_t sound, wgf_audio_priv_sound_t *record, const char *path,
                                 wgf_handle_t arrival);
void wgf_audio_priv_sound_feed(void);
wgf_core_priv_load_step_t wgf_audio_priv_sound_finish(void *prepared, wgf_handle_t sound, wgf_audio_priv_sound_t *record);
void wgf_audio_priv_sound_discard(void *prepared);
void wgf_audio_priv_sound_free(wgf_handle_t sound, wgf_audio_priv_sound_t *record);
/* At audio's stop, after every sound is freed: what the platform holds for them all. */
void wgf_audio_priv_sound_stop(void);

/* ------------------------------------------------------------------ voices ---- */

struct wgf_audio_priv_decoder_t; /* natively, a streamed sound's decoder (wgf_audio_mix.c) */

/* A voice's record: the game thread sets it under the lock; natively the mixer reads it
 * under the lock, and works on a copy outside it. */
typedef struct wgf_audio_priv_voice_t {
    wgf_handle_t sound; /* a reference held; 0 none */
    const wgf_audio_priv_segment_t *segment; /* its sound's, played as the whole; NULL the whole sound */
    wgf_play_state_t state;
    bool loop;
    float volume, pitch, pan;
    double position;     /* seconds from the sound's start */
    bool fresh;          /* played and not yet started: from the start in pitch's direction */
    unsigned generation; /* bumped by every change the mixer's copy mustn't write over (play,
                            stop, a position, a sound) */
    bool busy;           /* natively: the mixer is working on a copy, outside the lock */
    bool dying;          /* destroyed while busy: freed by the next sweep */
    wgf_handle_t let_go; /* natively: a sound let go of while busy: released by the next sweep */
    struct wgf_audio_priv_decoder_t *decoder; /* natively, streamed: the mixer's */
} wgf_audio_priv_voice_t;

/* What a voice plays, in seconds of its sound: its segment's range (the end clamped to
 * the sound's length), or the whole sound. Under the lock. */
void wgf_audio_priv_voice_range(const wgf_audio_priv_voice_t *voice_ptr, double *start, double *end);

/* The voice `voice` is; NULL for anything else. Under the lock. */
wgf_audio_priv_voice_t *wgf_audio_priv_voice_of_at(wgf_handle_t voice, const char *caller);
#define wgf_audio_priv_voice_of(voice) wgf_audio_priv_voice_of_at((voice), WGF_CORE_PRIV_CALLER)

/* Every voice, for the mixer, under the lock: calls `each` with each live one. */
void wgf_audio_priv_voice_each(void (*each)(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr, void *user),
                               void *user);

/* The mixer's settings (wgf_audio.h), read under the lock. */
float wgf_audio_priv_get_volume(void);
bool wgf_audio_priv_is_paused(void);

/* Each platform's half of voices (wgf_audio_mix.c natively, wgf_audio_voice_web.c on the
 * web). All on the main thread, under the lock but free_voice:
 *   start      audio's install: the device (natively), the context (the web)
 *   stop       audio's stop, every voice already freed
 *   apply      after any change to `voice_ptr`: the web carries it into its nodes
 *   position   where it is now, in seconds (the web asks the audio clock)
 *   free_voice its platform state let go of (the lock not held; never while busy)
 *   update     once a frame (the part's update): the web's ended voices to COMPLETE */
void wgf_audio_priv_platform_start(void);
void wgf_audio_priv_platform_stop(void);
void wgf_audio_priv_platform_apply(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr);
double wgf_audio_priv_platform_position(wgf_handle_t voice, const wgf_audio_priv_voice_t *voice_ptr);
void wgf_audio_priv_platform_free_voice(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr);
void wgf_audio_priv_platform_update(void);
/* The master volume or pause changed: the web carries it into its master gain. */
void wgf_audio_priv_platform_master(float volume, bool paused);

/* A voice reached its end (natively, the mixer's write-back; on the web, 'ended'): to
 * COMPLETE at its end, under the lock. */
void wgf_audio_priv_voice_complete(wgf_audio_priv_voice_t *voice_ptr, double end);

/* Voices' sweep, once a frame and at stop: what was destroyed or let go of while busy,
 * and a voice whose sound failed, to COMPLETE. */
void wgf_audio_priv_voice_sweep(void);
/* audio's stop: every voice freed. */
void wgf_audio_priv_voice_stop_all(void);

/* Natively: mix `frames` frames of every playing voice into `out`, interleaved stereo,
 * at `rate` frames a second (the device's callback, or a test pulling the mixer). */
void wgf_audio_priv_mix(float *out, int frames, int rate);

#endif /* WGF_AUDIO_PRIV_H */
