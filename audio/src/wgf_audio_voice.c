#include "wgf_voice.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_audio_priv.h"
#include "wgf_core_handle_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_resource.h"

/* Voices: one playing of a sound, the same state machine on both platforms
 * (wgf_play_state.h), every change made under the audio lock and carried out by the
 * platform (wgf_audio_priv.h: natively the mixer, on the web Web Audio). Natively the
 * mixer works on a copy of a voice outside the lock, marking it busy: a voice destroyed,
 * or a sound let go of, while busy is freed by the next sweep (the part's update), and a
 * change while busy bumps the voice's generation so the mixer's copy doesn't write back
 * over it. As wgrender's wgr_sound.c, with its states. */

static struct {
    bool ready;
    wgf_core_priv_handle_pool_t pool;
    wgf_audio_priv_voice_t *voices; /* grown by the pool, under the lock */
} vp;

wgf_audio_priv_voice_t *wgf_audio_priv_voice_of(wgf_handle_t voice)
{
    uint16_t index;
    if (!vp.ready || !wgf_core_priv_handle_pool_resolve(&vp.pool, voice, &index)) return NULL;
    return &vp.voices[index];
}

void wgf_audio_priv_voice_each(void (*each)(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr, void *user),
                               void *user)
{
    if (!vp.ready) return;
    for (uint16_t i = 1; i < vp.pool.capacity; i++) {
        const wgf_handle_t voice = wgf_core_priv_handle_pool_handle_from_index(&vp.pool, i);
        if (voice != 0 && !vp.voices[i].dying) each(voice, &vp.voices[i], user);
    }
}

/* Its sound's length: 0 until READY, and without end while a file arriving doesn't say
 * it (the voice plays on until the file ends). */
static double duration_of(const wgf_audio_priv_voice_t *voice_ptr)
{
    const wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(voice_ptr->sound);
    if (sound_ptr == NULL || sound_ptr->resource.status != WGF_RESOURCE_STATUS_READY) return 0.0;
    return sound_ptr->length_known ? sound_ptr->duration : HUGE_VAL;
}

static bool is_ready(const wgf_audio_priv_voice_t *voice_ptr)
{
    const wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(voice_ptr->sound);
    return sound_ptr != NULL && sound_ptr->resource.status == WGF_RESOURCE_STATUS_READY;
}

static bool is_streamed(const wgf_audio_priv_voice_t *voice_ptr)
{
    const wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(voice_ptr->sound);
    return sound_ptr != NULL && sound_ptr->streamed;
}

/* What the voice plays, in seconds of its sound: its segment, its end clamped to the
 * sound's length, or the whole sound. */
static void range_of(const wgf_audio_priv_voice_t *voice_ptr, double *start, double *end)
{
    const double length = duration_of(voice_ptr);
    *start = 0.0;
    *end = length;
    if (voice_ptr->segment != NULL) {
        *start = voice_ptr->segment->start < length ? voice_ptr->segment->start : length;
        *end = voice_ptr->segment->end < length ? voice_ptr->segment->end : length;
    }
}

/* Where play starts: the start, or with a negative pitch the end. */
static double start_of(const wgf_audio_priv_voice_t *voice_ptr)
{
    double start, end;
    range_of(voice_ptr, &start, &end);
    return voice_ptr->pitch < 0.0f ? end : start;
}

/* Where it is, from its segment's start (the sound's when it has none). */
static double relative(const wgf_audio_priv_voice_t *voice_ptr, double position)
{
    double start, end;
    range_of(voice_ptr, &start, &end);
    return position - start;
}

void wgf_audio_priv_voice_range(const wgf_audio_priv_voice_t *voice_ptr, double *start, double *end)
{
    range_of(voice_ptr, start, end);
}

void wgf_audio_priv_voice_complete(wgf_audio_priv_voice_t *voice_ptr, double end)
{
    voice_ptr->state = WGF_PLAY_STATE_COMPLETE;
    voice_ptr->position = end;
    voice_ptr->fresh = false;
    voice_ptr->generation++;
}

/* --------------------------------------------------------------- the public API ---- */

wgf_handle_t wgf_voice_create(wgf_handle_t sound)
{
    wgf_handle_t voice = 0;
    wgf_audio_priv_voice_t *voice_ptr;
    if (sound != 0 && wgf_audio_priv_sound_of(sound) == NULL) return 0;
    wgf_audio_priv_install();
    wgf_audio_priv_lock();
    if (!vp.ready) {
        vp.ready = wgf_core_priv_handle_pool_init(&vp.pool, WGF_CORE_PRIV_HANDLE_KIND_AUDIO_VOICE, (void **)&vp.voices,
                                                  sizeof(wgf_audio_priv_voice_t), 16, 4096);
    }
    if (vp.ready) voice = wgf_core_priv_handle_pool_alloc(&vp.pool); /* may grow, and move */
    voice_ptr = wgf_audio_priv_voice_of(voice);
    if (voice_ptr != NULL) {
        memset(voice_ptr, 0, sizeof(*voice_ptr));
        voice_ptr->sound = sound;
        voice_ptr->state = WGF_PLAY_STATE_STOPPED;
        voice_ptr->volume = voice_ptr->pitch = 1.0f;
        voice_ptr->fresh = true;
        wgf_audio_priv_platform_apply(voice, voice_ptr);
    }
    wgf_audio_priv_unlock();
    if (voice_ptr != NULL && sound != 0) wgf_core_priv_resource_retain(sound);
    return voice_ptr != NULL ? voice : 0;
}

/* `voice` gone: its platform state let go of, its slot freed, its sound released. Main
 * thread, the voice not busy. */
static void free_voice(wgf_handle_t voice)
{
    wgf_audio_priv_voice_t *voice_ptr;
    wgf_handle_t sound, let_go;
    wgf_audio_priv_lock();
    voice_ptr = wgf_audio_priv_voice_of(voice);
    if (voice_ptr == NULL) {
        wgf_audio_priv_unlock();
        return;
    }
    voice_ptr->dying = true; /* the mixer skips it from now */
    wgf_audio_priv_unlock();
    wgf_audio_priv_platform_free_voice(voice, voice_ptr); /* the main thread's alone: the pool doesn't move */
    wgf_audio_priv_lock();
    sound = voice_ptr->sound;
    let_go = voice_ptr->let_go;
    wgf_core_priv_handle_pool_free(&vp.pool, voice);
    wgf_audio_priv_unlock();
    if (sound != 0) wgf_resource_release(sound);
    if (let_go != 0) wgf_resource_release(let_go);
}

void wgf_voice_destroy(wgf_handle_t voice)
{
    wgf_audio_priv_voice_t *voice_ptr;
    bool busy;
    wgf_audio_priv_lock();
    voice_ptr = wgf_audio_priv_voice_of(voice);
    busy = voice_ptr != NULL && voice_ptr->busy;
    if (voice_ptr != NULL && busy) {
        voice_ptr->dying = true; /* freed by the next sweep */
        voice_ptr->state = WGF_PLAY_STATE_STOPPED;
    }
    wgf_audio_priv_unlock();
    if (voice_ptr != NULL && !busy) free_voice(voice);
}

static void keep_state(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr);

bool wgf_voice_set_sound(wgf_handle_t voice, wgf_handle_t sound)
{
    wgf_audio_priv_voice_t *voice_ptr;
    wgf_handle_t release = 0;
    if (sound != 0 && wgf_audio_priv_sound_of(sound) == NULL) return false;
    wgf_audio_priv_lock();
    voice_ptr = wgf_audio_priv_voice_of(voice);
    if (voice_ptr == NULL || voice_ptr->dying) {
        wgf_audio_priv_unlock();
        return false;
    }
    if (voice_ptr->sound != sound) {
        /* the sound the mixer's copy may be playing waits for the sweep; one set since
           then the mixer never saw, and goes now */
        if (voice_ptr->busy && voice_ptr->let_go == 0) voice_ptr->let_go = voice_ptr->sound;
        else release = voice_ptr->sound;
        voice_ptr->sound = sound;
        voice_ptr->segment = NULL; /* the old sound's */
        if (sound != 0) wgf_core_priv_resource_retain(sound);
    }
    keep_state(voice, voice_ptr);
    wgf_audio_priv_unlock();
    if (release != 0) wgf_resource_release(release);
    return true;
}

wgf_handle_t wgf_voice_get_sound(wgf_handle_t voice)
{
    wgf_audio_priv_voice_t *voice_ptr;
    wgf_handle_t sound;
    wgf_audio_priv_lock();
    voice_ptr = wgf_audio_priv_voice_of(voice);
    sound = voice_ptr != NULL && !voice_ptr->dying ? voice_ptr->sound : 0;
    wgf_audio_priv_unlock();
    return sound;
}

/* Whether another voice than `self` plays or holds paused the streamed `sound`. */
typedef struct taken_t {
    wgf_handle_t self, sound;
    bool taken;
} taken_t;

static void find_taker(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr, void *user)
{
    taken_t *taken = (taken_t *)user;
    if (voice != taken->self && voice_ptr->sound == taken->sound &&
        (voice_ptr->state == WGF_PLAY_STATE_PLAYING || voice_ptr->state == WGF_PLAY_STATE_PAUSED)) {
        taken->taken = true;
    }
}

/* The voice to play what it was just given (a sound, or a segment) in the state it is in
 * (CONVENTIONS.md, choosing what to play keeps the state): PLAYING, from the new start;
 * PAUSED, waiting at it; STOPPED or COMPLETE, STOPPED at it. With nothing it can play
 * (no sound, one that FAILED, a streamed one another voice holds), STOPPED. Under the
 * lock. */
static void keep_state(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr)
{
    const wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(voice_ptr->sound);
    bool playable = sound_ptr != NULL && sound_ptr->resource.status != WGF_RESOURCE_STATUS_FAILED;
    if (playable && sound_ptr->streamed) {
        taken_t taken = {voice, voice_ptr->sound, false};
        wgf_audio_priv_voice_each(find_taker, &taken);
        playable = !taken.taken;
    }
    if (!playable || voice_ptr->state == WGF_PLAY_STATE_STOPPED || voice_ptr->state == WGF_PLAY_STATE_COMPLETE) {
        voice_ptr->state = WGF_PLAY_STATE_STOPPED;
        voice_ptr->fresh = true;
    } else {
        voice_ptr->fresh = !is_ready(voice_ptr); /* PLAYING or PAUSED: from the start once it can */
    }
    voice_ptr->position = start_of(voice_ptr);
    voice_ptr->generation++;
    wgf_audio_priv_platform_apply(voice, voice_ptr);
}

bool wgf_voice_play(wgf_handle_t voice)
{
    wgf_audio_priv_voice_t *voice_ptr;
    const wgf_audio_priv_sound_t *sound_ptr;
    bool played = false;
    wgf_audio_priv_lock();
    voice_ptr = wgf_audio_priv_voice_of(voice);
    sound_ptr = voice_ptr != NULL && !voice_ptr->dying ? wgf_audio_priv_sound_of(voice_ptr->sound) : NULL;
    if (sound_ptr != NULL && sound_ptr->resource.status != WGF_RESOURCE_STATUS_FAILED) {
        taken_t taken = {voice, voice_ptr->sound, false};
        if (sound_ptr->streamed) wgf_audio_priv_voice_each(find_taker, &taken);
        if (!taken.taken) {
            voice_ptr->state = WGF_PLAY_STATE_PLAYING;
            voice_ptr->fresh = !is_ready(voice_ptr);
            voice_ptr->position = start_of(voice_ptr);
            voice_ptr->generation++;
            wgf_audio_priv_platform_apply(voice, voice_ptr);
            played = true;
        }
    }
    wgf_audio_priv_unlock();
    return played;
}

/* `voice`'s record when it is one and its state is `from`, or NULL; under the lock. */
static wgf_audio_priv_voice_t *in_state(wgf_handle_t voice, wgf_play_state_t from)
{
    wgf_audio_priv_voice_t *voice_ptr = wgf_audio_priv_voice_of(voice);
    return voice_ptr != NULL && !voice_ptr->dying && voice_ptr->state == from ? voice_ptr : NULL;
}

bool wgf_voice_pause(wgf_handle_t voice)
{
    wgf_audio_priv_voice_t *voice_ptr;
    wgf_audio_priv_lock();
    voice_ptr = in_state(voice, WGF_PLAY_STATE_PLAYING);
    if (voice_ptr != NULL) {
        voice_ptr->position = voice_ptr->fresh ? start_of(voice_ptr) : wgf_audio_priv_platform_position(voice, voice_ptr);
        voice_ptr->state = WGF_PLAY_STATE_PAUSED;
        voice_ptr->generation++;
        wgf_audio_priv_platform_apply(voice, voice_ptr);
    }
    wgf_audio_priv_unlock();
    return voice_ptr != NULL;
}

bool wgf_voice_resume(wgf_handle_t voice)
{
    wgf_audio_priv_voice_t *voice_ptr;
    wgf_audio_priv_lock();
    voice_ptr = in_state(voice, WGF_PLAY_STATE_PAUSED);
    if (voice_ptr != NULL) {
        voice_ptr->state = WGF_PLAY_STATE_PLAYING;
        voice_ptr->generation++;
        wgf_audio_priv_platform_apply(voice, voice_ptr);
    }
    wgf_audio_priv_unlock();
    return voice_ptr != NULL;
}

bool wgf_voice_stop(wgf_handle_t voice)
{
    wgf_audio_priv_voice_t *voice_ptr;
    wgf_audio_priv_lock();
    voice_ptr = wgf_audio_priv_voice_of(voice);
    if (voice_ptr != NULL && !voice_ptr->dying) {
        voice_ptr->state = WGF_PLAY_STATE_STOPPED;
        voice_ptr->fresh = true;
        voice_ptr->position = start_of(voice_ptr);
        voice_ptr->generation++;
        wgf_audio_priv_platform_apply(voice, voice_ptr);
    }
    wgf_audio_priv_unlock();
    return voice_ptr != NULL && !voice_ptr->dying;
}

wgf_play_state_t wgf_voice_get_state(wgf_handle_t voice)
{
    wgf_audio_priv_voice_t *voice_ptr;
    wgf_play_state_t state;
    wgf_audio_priv_lock();
    voice_ptr = wgf_audio_priv_voice_of(voice);
    state = voice_ptr != NULL && !voice_ptr->dying ? voice_ptr->state : WGF_PLAY_STATE_STOPPED;
    wgf_audio_priv_unlock();
    return state;
}

/* A setting changed in place: the platform carries it, the mixer reads it next copy. */
#define SET(voice, assign)                                                              \
    do {                                                                                \
        wgf_audio_priv_voice_t *voice_ptr;                                              \
        wgf_audio_priv_lock();                                                          \
        voice_ptr = wgf_audio_priv_voice_of(voice);                                     \
        if (voice_ptr != NULL && !voice_ptr->dying) {                                   \
            assign;                                                                     \
            wgf_audio_priv_platform_apply(voice, voice_ptr);                            \
        }                                                                               \
        wgf_audio_priv_unlock();                                                        \
        return voice_ptr != NULL && !voice_ptr->dying;                                  \
    } while (0)

#define GET(voice, type, value, otherwise)                                              \
    do {                                                                                \
        wgf_audio_priv_voice_t *voice_ptr;                                              \
        type got;                                                                       \
        wgf_audio_priv_lock();                                                          \
        voice_ptr = wgf_audio_priv_voice_of(voice);                                     \
        got = voice_ptr != NULL && !voice_ptr->dying ? (value) : (otherwise);           \
        wgf_audio_priv_unlock();                                                        \
        return got;                                                                     \
    } while (0)

bool wgf_voice_set_loop(wgf_handle_t voice, bool loop)
{
    SET(voice, voice_ptr->loop = loop);
}

bool wgf_voice_is_loop(wgf_handle_t voice)
{
    GET(voice, bool, voice_ptr->loop, false);
}

bool wgf_voice_set_volume(wgf_handle_t voice, float volume)
{
    if (!isfinite(volume) || volume < 0.0f) return false;
    SET(voice, voice_ptr->volume = volume);
}

float wgf_voice_get_volume(wgf_handle_t voice)
{
    GET(voice, float, voice_ptr->volume, 0.0f);
}

bool wgf_voice_set_pitch(wgf_handle_t voice, float pitch)
{
    wgf_audio_priv_voice_t *voice_ptr;
    bool set = false;
    if (!isfinite(pitch)) return false;
    pitch = pitch < -16.0f ? -16.0f : pitch > 16.0f ? 16.0f : pitch;
    wgf_audio_priv_lock();
    voice_ptr = wgf_audio_priv_voice_of(voice);
    if (voice_ptr != NULL && !voice_ptr->dying && !(pitch < 0.0f && is_streamed(voice_ptr))) {
        if (voice_ptr->state == WGF_PLAY_STATE_PLAYING && !voice_ptr->fresh) {
            /* where it is, before the speed changes: the web counts from here on */
            voice_ptr->position = wgf_audio_priv_platform_position(voice, voice_ptr);
        }
        if (voice_ptr->fresh && (voice_ptr->pitch < 0.0f) != (pitch < 0.0f)) {
            voice_ptr->pitch = pitch;
            voice_ptr->position = start_of(voice_ptr); /* not started: from the other end */
        }
        voice_ptr->pitch = pitch;
        voice_ptr->generation++;
        wgf_audio_priv_platform_apply(voice, voice_ptr);
        set = true;
    }
    wgf_audio_priv_unlock();
    return set;
}

float wgf_voice_get_pitch(wgf_handle_t voice)
{
    GET(voice, float, voice_ptr->pitch, 0.0f);
}

bool wgf_voice_set_pan(wgf_handle_t voice, float pan)
{
    if (!isfinite(pan)) return false;
    pan = pan < -1.0f ? -1.0f : pan > 1.0f ? 1.0f : pan;
    SET(voice, voice_ptr->pan = pan);
}

float wgf_voice_get_pan(wgf_handle_t voice)
{
    GET(voice, float, voice_ptr->pan, 0.0f);
}

bool wgf_voice_set_position(wgf_handle_t voice, float seconds)
{
    wgf_audio_priv_voice_t *voice_ptr;
    bool set = false;
    if (!isfinite(seconds)) return false;
    wgf_audio_priv_lock();
    voice_ptr = wgf_audio_priv_voice_of(voice);
    if (voice_ptr != NULL && !voice_ptr->dying && is_ready(voice_ptr)) {
        double start, end, at = seconds;
        range_of(voice_ptr, &start, &end);
        if (voice_ptr->loop && end > start) {
            at = fmod(at, end - start);
            if (at < 0.0) at += end - start;
        } else {
            at = at < 0.0 ? 0.0 : at > end - start ? end - start : at;
        }
        voice_ptr->position = start + at;
        voice_ptr->fresh = false;
        voice_ptr->generation++;
        wgf_audio_priv_platform_apply(voice, voice_ptr);
        set = true;
    }
    wgf_audio_priv_unlock();
    return set;
}

float wgf_voice_get_position(wgf_handle_t voice)
{
    GET(voice, float,
        (float)relative(voice_ptr, voice_ptr->fresh ? start_of(voice_ptr)
                                   : voice_ptr->state == WGF_PLAY_STATE_PLAYING
                                       ? wgf_audio_priv_platform_position(voice, voice_ptr)
                                       : voice_ptr->position),
        0.0f);
}

bool wgf_voice_set_segment(wgf_handle_t voice, const char *name)
{
    wgf_audio_priv_voice_t *voice_ptr;
    const wgf_audio_priv_segment_t *segment = NULL;
    bool set = false;
    if (name == NULL) return false;
    wgf_audio_priv_lock();
    voice_ptr = wgf_audio_priv_voice_of(voice);
    if (voice_ptr != NULL && !voice_ptr->dying) {
        segment = name[0] != '\0' ? wgf_audio_priv_sound_segment(wgf_audio_priv_sound_of(voice_ptr->sound), name) : NULL;
        if (name[0] == '\0' || segment != NULL) {
            voice_ptr->segment = segment;
            keep_state(voice, voice_ptr);
            set = true;
        }
    }
    wgf_audio_priv_unlock();
    return set;
}

const char *wgf_voice_get_segment(wgf_handle_t voice)
{
    GET(voice, const char *, voice_ptr->segment != NULL ? voice_ptr->segment->name : "", "");
}

/* ------------------------------------------------------------------ the sweep ---- */

typedef struct sweep_t {
    wgf_handle_t *dead;      /* voices to free */
    wgf_handle_t *let_go;    /* sounds to release */
    int dead_count, let_go_count, capacity;
} sweep_t;

static void sweep_one(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr, void *user)
{
    sweep_t *sweep = (sweep_t *)user;
    const wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(voice_ptr->sound);
    if (voice_ptr->state == WGF_PLAY_STATE_PLAYING && sound_ptr != NULL &&
        sound_ptr->resource.status == WGF_RESOURCE_STATUS_FAILED) {
        wgf_audio_priv_voice_complete(voice_ptr, 0.0); /* nothing to play */
    } else if (voice_ptr->state == WGF_PLAY_STATE_PLAYING && voice_ptr->fresh && sound_ptr != NULL &&
               sound_ptr->resource.status == WGF_RESOURCE_STATUS_READY && !voice_ptr->busy) {
        voice_ptr->fresh = false; /* its sound came: from the start in its direction */
        voice_ptr->position = start_of(voice_ptr);
        voice_ptr->generation++;
        wgf_audio_priv_platform_apply(voice, voice_ptr);
    }
    if (!voice_ptr->busy && voice_ptr->let_go != 0 && sweep->let_go_count < sweep->capacity) {
        sweep->let_go[sweep->let_go_count++] = voice_ptr->let_go;
        voice_ptr->let_go = 0;
    }
}

static void find_dead(wgf_audio_priv_voice_t *voices, uint16_t capacity, sweep_t *sweep)
{
    for (uint16_t i = 1; i < capacity; i++) {
        const wgf_handle_t voice = wgf_core_priv_handle_pool_handle_from_index(&vp.pool, i);
        if (voice != 0 && voices[i].dying && !voices[i].busy && sweep->dead_count < sweep->capacity) {
            sweep->dead[sweep->dead_count++] = voice;
        }
    }
}

void wgf_audio_priv_voice_sweep(void)
{
    sweep_t sweep;
    if (!vp.ready) return;
    wgf_audio_priv_lock();
    memset(&sweep, 0, sizeof(sweep));
    sweep.capacity = vp.pool.capacity;
    sweep.dead = (wgf_handle_t *)malloc(sizeof(wgf_handle_t) * (size_t)sweep.capacity);
    sweep.let_go = (wgf_handle_t *)malloc(sizeof(wgf_handle_t) * (size_t)sweep.capacity);
    if (sweep.dead == NULL || sweep.let_go == NULL) sweep.capacity = 0;
    wgf_audio_priv_voice_each(sweep_one, &sweep);
    find_dead(vp.voices, vp.pool.capacity, &sweep);
    wgf_audio_priv_unlock();
    for (int i = 0; i < sweep.let_go_count; i++) wgf_resource_release(sweep.let_go[i]);
    for (int i = 0; i < sweep.dead_count; i++) free_voice(sweep.dead[i]);
    free(sweep.dead);
    free(sweep.let_go);
}

void wgf_audio_priv_voice_stop_all(void)
{
    if (!vp.ready) return;
    for (uint16_t i = 1; i < vp.pool.capacity; i++) {
        const wgf_handle_t voice = wgf_core_priv_handle_pool_handle_from_index(&vp.pool, i);
        if (voice != 0) {
            vp.voices[i].busy = false; /* the device is stopped: nothing mixes */
            free_voice(voice);
        }
    }
    wgf_audio_priv_lock();
    wgf_core_priv_handle_pool_destroy(&vp.pool);
    memset(&vp, 0, sizeof(vp));
    wgf_audio_priv_unlock();
}
