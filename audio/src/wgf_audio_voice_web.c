/* Voices on the web, where the browser does all audio (docs/ROADMAP.md, phase 2): each
 * voice a GainNode and a StereoPannerNode into one master GainNode, playing a decoded
 * sound through an AudioBufferSourceNode (its loop from the buffer's start to its end;
 * backwards from a reversed copy of the buffer, made once) and a streamed one through
 * its <audio> element, with nothing preserving its pitch, so pitch changes speed and
 * tone together, as natively. The browser mixes on its audio thread. Where a voice is
 * comes from the audio clock: where it started, plus the clock since times the pitch.
 * A source's 'ended' queues the voice; the part's update makes it COMPLETE. The context
 * waits, suspended, for the page's first input, which resumes it unless audio is
 * paused. The voices' platform half (wgf_audio_priv.h). */
#include <emscripten.h>

#include "wgf_audio_priv.h"

EM_JS(void, wgf_audio_js_start, (void), {
    var audio = Module.wgf_audio || (Module.wgf_audio = {context: null, sounds: new Map()});
    if (!audio.context) audio.context = new (window.AudioContext || window.webkitAudioContext)();
    audio.voices = new Map();
    audio.ended = [];
    audio.paused = false;
    audio.master = audio.context.createGain();
    audio.master.connect(audio.context.destination);
    audio.unlock = function() {
        if (Module.wgf_audio && !Module.wgf_audio.paused) Module.wgf_audio.context.resume();
    };
    ["pointerdown", "keydown", "touchstart"].forEach(function(name) {
        window.addEventListener(name, audio.unlock, {capture: true});
    });
})

EM_JS(void, wgf_audio_js_master, (double volume, int paused), {
    var audio = Module.wgf_audio;
    if (!audio || !audio.master) return;
    audio.master.gain.value = volume;
    audio.paused = !!paused;
    if (paused) audio.context.suspend();
    else audio.context.resume(); /* before the first input the browser keeps it suspended */
})

/* A voice's record carried into its nodes: a new generation (played, paused, stopped,
 * moved, retargeted, re-pitched) restarts what it plays from `position`; otherwise only
 * its settings change, in place. */
EM_JS(void, wgf_audio_js_voice_apply,
      (int handle, int sound, int state, double position, int loop, double volume, double pitch, double pan,
       int generation, double low, double high, int streamed),
      {
    var audio = Module.wgf_audio;
    if (!audio || !audio.voices) return;
    var context = audio.context;
    var v = audio.voices.get(handle);
    if (!v) {
        v = {gain: context.createGain(), panner: context.createStereoPanner(), node: null, element: null,
             generation: -1, rate: 1, startTime: 0, startPos: 0, low: 0, high: 0, loop: false, playing: false};
        v.gain.connect(v.panner);
        v.panner.connect(audio.master);
        audio.voices.set(handle, v);
    }
    v.gain.gain.value = volume;
    v.panner.pan.value = pan;
    v.loop = !!loop;
    v.low = low; /* what it plays: its segment, or the whole sound */
    v.high = high;
    var entry = audio.sounds.get(sound);
    function halt() {
        if (v.node) {
            v.node.onended = null;
            try { v.node.stop(); } catch (e) {}
            v.node.disconnect();
            v.node = null;
        }
        if (v.element) {
            v.element.onended = null;
            v.element.pause();
            v.element = null;
        }
        v.playing = false;
    }
    if (generation !== v.generation) { /* played, paused, stopped, moved, retargeted, or re-pitched */
        v.generation = generation;
        halt();
        v.startPos = position;
        v.startTime = context.currentTime;
        v.rate = pitch;
        if (state !== 1 || !entry || entry.state !== 1) return; /* 1: PLAYING, and the sound READY */
        v.playing = true;
        if (streamed) {
            var element = entry.element;
            if (!entry.source) entry.source = context.createMediaElementSource(element);
            entry.source.disconnect();
            entry.source.connect(v.gain);
            element.preservesPitch = false;
            element.mozPreservesPitch = false;
            element.webkitPreservesPitch = false;
            /* only a seek that moves it: a seek to where it already is, before play() on a
               file the browser can't seek (streamed from a host without Range), hung the
               pipeline at HAVE_METADATA until the download ended, one visit in eight */
            if (Math.abs(element.currentTime - position) > 0.001) element.currentTime = position;
            element.loop = v.loop;
            element.onended = function() { audio.ended.push(handle); };
            v.element = element;
            if (pitch > 0) {
                element.playbackRate = pitch;
                element.play();
            }
            return;
        }
        if (pitch === 0) return; /* held where it is */
        var buffer = entry.buffer;
        if (pitch < 0) {
            if (!entry.reversed) {
                entry.reversed = context.createBuffer(buffer.numberOfChannels, buffer.length, buffer.sampleRate);
                for (var c = 0; c < buffer.numberOfChannels; c++) {
                    var from = buffer.getChannelData(c), to = entry.reversed.getChannelData(c);
                    for (var i = 0, n = from.length; i < n; i++) to[i] = from[n - 1 - i];
                }
            }
            buffer = entry.reversed;
        }
        var node = context.createBufferSource();
        var length = buffer.duration;
        node.buffer = buffer;
        node.loop = v.loop;
        node.loopStart = pitch < 0 ? length - high : low; /* the reversed copy runs from the end */
        node.loopEnd = pitch < 0 ? length - low : high;
        node.playbackRate.value = Math.abs(pitch);
        node.connect(v.gain);
        node.onended = function() {
            if (v.node === node) audio.ended.push(handle);
        };
        var offset = Math.max(node.loopStart, Math.min(pitch < 0 ? length - position : position, node.loopEnd));
        if (v.loop) node.start(0, offset);
        else node.start(0, offset, node.loopEnd - offset); /* to the segment's end */
        v.node = node;
        return;
    }
    if (v.node) v.node.loop = v.loop; /* a setting changed in place */
    if (v.element) v.element.loop = v.loop;
})

EM_JS(double, wgf_audio_js_voice_position, (int handle), {
    var audio = Module.wgf_audio;
    var v = audio && audio.voices && audio.voices.get(handle);
    if (!v) return 0;
    if (v.element) return v.element.currentTime;
    var at = v.startPos + (audio.context.currentTime - v.startTime) * (v.playing ? v.rate : 0);
    var length = v.high - v.low;
    if (length > 0) {
        if (v.loop) {
            at = (at - v.low) % length;
            if (at < 0) at += length;
            at += v.low;
        } else {
            at = Math.max(v.low, Math.min(at, v.high));
        }
    }
    return at;
})

EM_JS(int, wgf_audio_js_next_ended, (void), {
    var audio = Module.wgf_audio;
    return audio && audio.ended && audio.ended.length > 0 ? audio.ended.shift() : 0;
})

EM_JS(void, wgf_audio_js_voice_free, (int handle), {
    var audio = Module.wgf_audio;
    var v = audio && audio.voices && audio.voices.get(handle);
    if (!v) return;
    if (v.node) {
        v.node.onended = null;
        try { v.node.stop(); } catch (e) {}
        v.node.disconnect();
    }
    if (v.element) {
        v.element.onended = null;
        v.element.pause();
    }
    v.gain.disconnect();
    v.panner.disconnect();
    audio.voices.delete(handle);
})

EM_JS(void, wgf_audio_js_voices_stop, (void), {
    var audio = Module.wgf_audio;
    if (!audio) return;
    if (audio.unlock) {
        ["pointerdown", "keydown", "touchstart"].forEach(function(name) {
            window.removeEventListener(name, audio.unlock, {capture: true});
        });
    }
})

void wgf_audio_priv_platform_start(void)
{
    wgf_audio_js_start();
}

void wgf_audio_priv_platform_stop(void)
{
    wgf_audio_js_voices_stop(); /* each voice's nodes go as it is freed; the context with the sounds */
}

void wgf_audio_priv_platform_apply(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr)
{
    const wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(voice_ptr->sound);
    double low, high;
    wgf_audio_priv_voice_range(voice_ptr, &low, &high);
    wgf_audio_js_voice_apply((int)voice, (int)voice_ptr->sound, voice_ptr->fresh ? 0 : (int)voice_ptr->state,
                             voice_ptr->position, voice_ptr->loop, voice_ptr->volume, voice_ptr->pitch, voice_ptr->pan,
                             (int)voice_ptr->generation, low, high, sound_ptr != NULL && sound_ptr->streamed);
}

double wgf_audio_priv_platform_position(wgf_handle_t voice, const wgf_audio_priv_voice_t *voice_ptr)
{
    (void)voice_ptr;
    return wgf_audio_js_voice_position((int)voice);
}

void wgf_audio_priv_platform_free_voice(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr)
{
    (void)voice_ptr;
    wgf_audio_js_voice_free((int)voice);
}

void wgf_audio_priv_platform_update(void)
{
    int ended;
    while ((ended = wgf_audio_js_next_ended()) != 0) {
        wgf_audio_priv_voice_t *voice_ptr;
        wgf_audio_priv_lock();
        voice_ptr = wgf_audio_priv_voice_of((wgf_handle_t)ended);
        if (voice_ptr != NULL && voice_ptr->state == WGF_PLAY_STATE_PLAYING && !voice_ptr->loop) {
            double low, high;
            wgf_audio_priv_voice_range(voice_ptr, &low, &high);
            wgf_audio_priv_voice_complete(voice_ptr, voice_ptr->pitch < 0.0f ? low : high);
        }
        wgf_audio_priv_unlock();
    }
}

void wgf_audio_priv_platform_master(float volume, bool paused)
{
    wgf_audio_js_master(volume, paused);
}
