/* A sound's load, on the web, where the browser does all audio (docs/ROADMAP.md, phase
 * 2): the file's bytes read through fs; a decoded sound handed to decodeAudioData, its
 * AudioBuffer kept in a JS table by the sound's handle; a streamed one made a blob URL,
 * which an <audio> element plays (the same element reads its length now). The browser
 * works outside the frame, so finish asks in each update until it's done
 * (WGF_CORE_PRIV_LOAD_WAIT). No decoder is in the wasm. A streamed sound whose file
 * isn't in the cache (core's arrival, phase 3) is fetched by its element alone, from
 * its URL, playing as it arrives: the redirects' URLs tried in turn while one fails
 * before it starts, READY at its metadata, its length 0 until the browser knows it,
 * FAILED on the element's error. */
#include <emscripten.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_audio_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_log.h"

/* The browser's side: one AudioContext, made by the first sound (it waits, suspended,
 * for the page's first input), and the sounds by handle: {state: 0 decoding, 1 ready,
 * 2 failed; buffer, the decoded AudioBuffer; url and element, a streamed sound's}. */
EM_JS(void, wgf_audio_js_sound_start, (int handle, int streamed, const unsigned char *bytes, int size, int type), {
    var audio = Module.wgf_audio || (Module.wgf_audio = {context: null, sounds: new Map()});
    var entry = {state: 0, buffer: null, url: null, element: null, duration: 0};
    var data = HEAPU8.slice(bytes, bytes + size); /* a copy: decodeAudioData takes it over */
    audio.sounds.set(handle, entry);
    if (!audio.context) audio.context = new (window.AudioContext || window.webkitAudioContext)();
    if (streamed) {
        var mime = ["", "audio/wav", "audio/mpeg", "audio/ogg"][type] || "";
        entry.url = URL.createObjectURL(new Blob([data], mime ? {type: mime} : {}));
        entry.element = new Audio();
        entry.element.preload = "metadata";
        entry.element.addEventListener("loadedmetadata", function() {
            entry.duration = entry.element.duration;
            entry.state = isFinite(entry.duration) && entry.duration > 0 ? 1 : 2;
        });
        entry.element.addEventListener("error", function() { entry.state = 2; });
        entry.element.src = entry.url;
    } else {
        audio.context.decodeAudioData(data.buffer).then(
            function(buffer) {
                entry.buffer = buffer;
                entry.duration = buffer.duration;
                entry.state = 1;
            },
            function() { entry.state = 2; });
    }
})

/* A streamed sound from the network: `urls`, one a line, tried in turn while the
 * element fails before its metadata; crossOrigin so createMediaElementSource hears a
 * file from another origin (which must allow it, as asset's fetch needs too). */
EM_JS(void, wgf_audio_js_sound_stream, (int handle, const char *urls_c), {
    var audio = Module.wgf_audio || (Module.wgf_audio = {context: null, sounds: new Map()});
    var urls = UTF8ToString(urls_c).split("\n");
    var entry = {state: 0, buffer: null, url: null, element: new Audio(), duration: 0};
    var at = 0;
    audio.sounds.set(handle, entry);
    if (!audio.context) audio.context = new (window.AudioContext || window.webkitAudioContext)();
    entry.element.crossOrigin = "anonymous";
    entry.element.preload = "metadata";
    entry.element.addEventListener("loadedmetadata", function() { entry.state = 1; });
    entry.element.addEventListener("durationchange", function() { entry.duration = entry.element.duration; });
    entry.element.addEventListener("error", function() {
        if (entry.state === 0 && ++at < urls.length) {
            entry.element.src = urls[at]; /* the next redirect's */
        } else {
            entry.state = 2;
        }
    });
    entry.element.src = urls[0];
})

EM_JS(int, wgf_audio_js_sound_state, (int handle), {
    var entry = Module.wgf_audio && Module.wgf_audio.sounds.get(handle);
    return entry ? entry.state : 2;
})

EM_JS(double, wgf_audio_js_sound_duration, (int handle), {
    var entry = Module.wgf_audio && Module.wgf_audio.sounds.get(handle);
    return entry ? entry.duration : 0;
})

EM_JS(int, wgf_audio_js_sound_channels, (int handle), {
    var entry = Module.wgf_audio && Module.wgf_audio.sounds.get(handle);
    return entry && entry.buffer ? entry.buffer.numberOfChannels : 2;
})

EM_JS(void, wgf_audio_js_sound_free, (int handle), {
    var audio = Module.wgf_audio;
    var entry = audio && audio.sounds.get(handle);
    if (!entry) return;
    if (entry.element) entry.element.removeAttribute("src");
    if (entry.url) URL.revokeObjectURL(entry.url);
    audio.sounds.delete(handle);
})

EM_JS(void, wgf_audio_js_stop, (void), {
    var audio = Module.wgf_audio;
    if (!audio) return;
    audio.sounds.forEach(function(entry) { if (entry.url) URL.revokeObjectURL(entry.url); });
    audio.sounds.clear();
    if (audio.context) audio.context.close();
    Module.wgf_audio = null;
})

typedef struct prepared_t {
    unsigned char *bytes; /* fs's (wgf_core_priv_fs_read_free) */
    int size;
    wgf_audio_priv_format_t type; /* by the name, for a streamed sound's blob */
    bool started;
    char path[WGF_CORE_PRIV_FS_PATH_MAX];
} prepared_t;

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

void *wgf_audio_priv_sound_prepare(const char *path, bool streamed)
{
    prepared_t *prepared = (prepared_t *)calloc(1, sizeof(prepared_t));
    (void)streamed;
    if (prepared == NULL) return NULL;
    if (!wgf_core_priv_fs_read(path, &prepared->bytes, &prepared->size)) {
        free(prepared);
        return NULL;
    }
    prepared->type = ends_with(path, ".wav")   ? WGF_AUDIO_PRIV_FORMAT_WAV
                     : ends_with(path, ".ogg") ? WGF_AUDIO_PRIV_FORMAT_OGG
                     : ends_with(path, ".mp3") ? WGF_AUDIO_PRIV_FORMAT_MP3
                                               : WGF_AUDIO_PRIV_FORMAT_NONE;
    strncpy(prepared->path, path, sizeof(prepared->path) - 1);
    return prepared;
}

wgf_core_priv_load_step_t wgf_audio_priv_sound_finish(void *data, wgf_handle_t sound, wgf_audio_priv_sound_t *record)
{
    prepared_t *prepared = (prepared_t *)data;
    int state;
    if (!prepared->started) {
        wgf_audio_js_sound_start((int)sound, record->streamed, prepared->bytes, prepared->size, (int)prepared->type);
        prepared->started = true;
        wgf_core_priv_fs_read_free(prepared->bytes); /* the browser has its copy */
        prepared->bytes = NULL;
        return WGF_CORE_PRIV_LOAD_WAIT;
    }
    state = wgf_audio_js_sound_state((int)sound);
    if (state == 0) return WGF_CORE_PRIV_LOAD_WAIT;
    if (state != 1) {
        wgf_log_warn("wgf_sound: %s: this browser can't play it", prepared->path);
        return WGF_CORE_PRIV_LOAD_FAILED;
    }
    wgf_audio_priv_lock();
    record->format = prepared->type;
    record->channels = wgf_audio_js_sound_channels((int)sound);
    record->duration = (float)wgf_audio_js_sound_duration((int)sound);
    record->length_known = true;
    wgf_audio_priv_unlock();
    wgf_core_priv_resource_loaded(sound, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

/* Streamed sounds whose elements fetch their files: READY at their metadata, their
 * lengths as the browser learns them, FAILED on an error, asked each update. */
static wgf_handle_t *streaming;
static int streaming_count, streaming_capacity;

void wgf_audio_priv_sound_arrive(wgf_handle_t sound, wgf_audio_priv_sound_t *record, const char *path,
                                 wgf_handle_t arrival)
{
    char url[WGF_CORE_PRIV_FS_PATH_MAX + 512];
    char *urls = NULL;
    size_t length = 0;
    for (int i = 0; wgf_core_priv_load_hooks.arrival_source != NULL &&
                    wgf_core_priv_load_hooks.arrival_source(arrival, i, url, sizeof(url));
         i++) {
        const size_t n = strlen(url);
        char *grown = (char *)realloc(urls, length + n + 2);
        if (grown == NULL) break;
        urls = grown;
        if (length > 0) urls[length++] = '\n';
        memcpy(urls + length, url, n + 1);
        length += n;
    }
    if (wgf_core_priv_load_hooks.arrival_end != NULL) wgf_core_priv_load_hooks.arrival_end(arrival);
    if (streaming_count == streaming_capacity) {
        const int capacity = streaming_capacity > 0 ? streaming_capacity * 2 : 8;
        wgf_handle_t *grown = (wgf_handle_t *)realloc(streaming, sizeof(wgf_handle_t) * (size_t)capacity);
        if (grown != NULL) {
            streaming = grown;
            streaming_capacity = capacity;
        }
    }
    if (urls == NULL || streaming_count == streaming_capacity) {
        free(urls);
        wgf_core_priv_resource_failed(sound);
        return;
    }
    record->format = ends_with(path, ".wav")   ? WGF_AUDIO_PRIV_FORMAT_WAV
                     : ends_with(path, ".ogg") ? WGF_AUDIO_PRIV_FORMAT_OGG
                                               : WGF_AUDIO_PRIV_FORMAT_MP3;
    wgf_audio_js_sound_stream((int)sound, urls);
    free(urls);
    streaming[streaming_count++] = sound;
}

void wgf_audio_priv_sound_feed(void)
{
    for (int i = 0; i < streaming_count;) {
        const wgf_handle_t sound = streaming[i];
        wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(sound);
        const int state = sound_ptr != NULL ? wgf_audio_js_sound_state((int)sound) : 2;
        const double duration = wgf_audio_js_sound_duration((int)sound);
        const bool known = isfinite(duration) && duration > 0.0;
        if (sound_ptr == NULL || state == 2) {
            if (sound_ptr != NULL) {
                wgf_log_warn("wgf_sound: %s: its stream failed", sound_ptr->resource.path);
                wgf_audio_priv_lock();
                wgf_core_priv_resource_failed(sound);
                wgf_audio_priv_unlock();
            }
            streaming[i] = streaming[--streaming_count];
            continue;
        }
        if (state == 1 && (sound_ptr->resource.status != WGF_RESOURCE_STATUS_READY ||
                           (known && (!sound_ptr->length_known || (float)duration != sound_ptr->duration)))) {
            wgf_audio_priv_lock();
            sound_ptr->channels = 2;
            sound_ptr->duration = known ? (float)duration : 0.0f;
            sound_ptr->length_known = known;
            if (sound_ptr->resource.status != WGF_RESOURCE_STATUS_READY) wgf_core_priv_resource_loaded(sound, NULL);
            wgf_audio_priv_unlock();
        }
        i++;
    }
}

void wgf_audio_priv_sound_discard(void *data)
{
    prepared_t *prepared = (prepared_t *)data;
    if (prepared == NULL) return;
    if (prepared->bytes != NULL) wgf_core_priv_fs_read_free(prepared->bytes);
    free(prepared);
}

void wgf_audio_priv_sound_free(wgf_handle_t sound, wgf_audio_priv_sound_t *record)
{
    (void)record;
    wgf_audio_js_sound_free((int)sound);
}

void wgf_audio_priv_sound_stop(void)
{
    wgf_audio_js_stop();
    free(streaming);
    streaming = NULL;
    streaming_count = streaming_capacity = 0;
}
