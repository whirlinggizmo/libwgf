/* Voices on the web, where the browser does all audio (docs/ROADMAP.md, phase 2): each
 * voice a GainNode and a StereoPannerNode into one master GainNode, playing a decoded
 * sound through an AudioBufferSourceNode (its loop from the buffer's start to its end;
 * backwards from a reversed copy of the buffer, made once) and a streamed one through
 * its <audio> element, with nothing preserving its pitch, so pitch changes speed and
 * tone together, as natively. The browser mixes on its audio thread. Where a voice is
 * comes from the audio clock: where it started, plus the clock since times the pitch.
 * A source's 'ended' queues the voice; the part's update makes it COMPLETE. The context
 * waits, suspended, for the page's first input, which resumes it unless audio is
 * paused. A pushed voice's queue is a ring in JS, taken from by a ScriptProcessorNode of
 * its own into its gain, resampled as the native mixer does (wgf_audio_mix.c's
 * pushed_pull, line for line): it runs on the page's thread, between frames, so nothing
 * here calls back into C. The voices' platform half (wgf_audio_priv.h). */
#include <emscripten.h>

#include "wgf_audio_priv.h"

EM_JS(void, wgf_audio_js_start, (void), {
    var audio = Module.wgf_audio || (Module.wgf_audio = {context: null, sounds: new Map()});
    if (!audio.context) audio.context = new (window.AudioContext || window.webkitAudioContext)();
    audio.voices = new Map();
    audio.pushed = new Map();
    audio.ended = [];
    audio.paused = false;
    audio.master = audio.context.createGain();
    audio.master.connect(audio.context.destination);
    audio.unlock = function() {
        if (!Module.wgf_audio) return;
        Module.wgf_audio.unlocked = true;
        if (!Module.wgf_audio.paused) Module.wgf_audio.context.resume();
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
        var p = audio.pushed.get(handle);
        if (p) { /* a pushed voice: its processor takes from the queue while PLAYING */
            if (!p.node) {
                p.node = context.createScriptProcessor(2048, 1, 2);
                p.node.onaudioprocess = function(e) {
                    Module.wgf_audio_pushed_pull(p, e.outputBuffer, context.sampleRate);
                };
                p.node.connect(v.gain);
            }
            p.active = state === 1;
            p.pitch = pitch;
            p.played = 0;
            return;
        }
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
                /* refused before the page's first tap, click, or key (the browser's autoplay
                   rule): played again inside the next one, if the voice still plays it then;
                   another refusal (a pause before it started) is the voice's own doing */
                element.play().catch(function(e) {
                    if (!e || e.name !== "NotAllowedError") return;
                    var retry = function() {
                        ["pointerup", "touchend", "click", "keydown"].forEach(function(name) {
                            window.removeEventListener(name, retry, {capture: true});
                        });
                        if (v.element === element && v.playing && element.paused) element.play().catch(function() {});
                    };
                    ["pointerup", "touchend", "click", "keydown"].forEach(function(name) {
                        window.addEventListener(name, retry, {capture: true});
                    });
                });
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
    var p = audio.pushed.get(handle);
    if (p) return v.startPos + p.played / p.rate;
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
    var p = audio.pushed.get(handle);
    if (p && p.node) {
        p.node.onaudioprocess = null;
        p.node.disconnect();
    }
    audio.pushed.delete(handle);
    v.gain.disconnect();
    v.panner.disconnect();
    audio.voices.delete(handle);
})

/* A pushed voice's queue: made, filled from the wasm heap, asked about, emptied. */
EM_JS(void, wgf_audio_js_pushed_start, (int handle, int rate, int channels, int capacity), {
    var audio = Module.wgf_audio;
    if (!audio || !audio.pushed) return;
    audio.pushed.set(handle, {rate: rate, channels: channels, capacity: capacity,
                              ring: new Float32Array(capacity * channels), head: 0, count: 0,
                              a: [0, 0], b: [0, 0], hasB: false, t: 1, starved: true, underruns: 0,
                              active: false, pitch: 1, played: 0, node: null});
    /* Up to an output buffer's frames taken from the queue at `step` frames each,
       linearly between two, silence once it runs out: the native mixer's pushed_pull. */
    Module.wgf_audio_pushed_pull = Module.wgf_audio_pushed_pull || function(p, buffer, outRate) {
        var left = buffer.getChannelData(0), right = buffer.getChannelData(1), n = left.length;
        var step = p.pitch * p.rate / outRate;
        function pop(to) {
            if (p.count === 0) return false;
            var at = p.head * p.channels;
            to[0] = p.ring[at];
            to[1] = p.channels > 1 ? p.ring[at + 1] : p.ring[at];
            p.head = (p.head + 1) % p.capacity;
            p.count--;
            return true;
        }
        function ready() {
            while (p.t >= 1) {
                if (p.hasB) {
                    p.a[0] = p.b[0];
                    p.a[1] = p.b[1];
                    p.hasB = false;
                } else if (!pop(p.a)) {
                    return false;
                }
                p.t -= 1;
                p.played += 1;
            }
            if (p.t > 0 && !p.hasB) {
                if (!pop(p.b)) return false;
                p.hasB = true;
            }
            return true;
        }
        var i = 0;
        if (p.active && p.pitch > 0) {
            for (; i < n; i++) {
                if (!ready()) {
                    if (!p.starved) p.underruns++;
                    p.starved = true;
                    break;
                }
                left[i] = p.a[0] + (p.t > 0 ? (p.b[0] - p.a[0]) * p.t : 0);
                right[i] = p.a[1] + (p.t > 0 ? (p.b[1] - p.a[1]) * p.t : 0);
                p.t += step;
            }
        }
        for (; i < n; i++) left[i] = right[i] = 0;
    };
})

EM_JS(int, wgf_audio_js_pushed_push, (int handle, const float *samples, int frames), {
    var audio = Module.wgf_audio;
    var p = audio && audio.pushed && audio.pushed.get(handle);
    if (!p) return 0;
    var taken = Math.min(frames, p.capacity - p.count);
    var from = samples >> 2;
    for (var done = 0; done < taken;) { /* in at most two runs, around the ring's end */
        var at = (p.head + p.count) % p.capacity;
        var run = Math.min(taken - done, p.capacity - at);
        p.ring.set(HEAPF32.subarray(from + done * p.channels, from + (done + run) * p.channels), at * p.channels);
        p.count += run;
        done += run;
    }
    if (taken > 0) p.starved = false;
    return taken;
})

EM_JS(int, wgf_audio_js_pushed_queued, (int handle), {
    var audio = Module.wgf_audio;
    var p = audio && audio.pushed && audio.pushed.get(handle);
    return p ? p.count : 0;
})

EM_JS(int, wgf_audio_js_pushed_underruns, (int handle), {
    var audio = Module.wgf_audio;
    var p = audio && audio.pushed && audio.pushed.get(handle);
    return p ? p.underruns : 0;
})

EM_JS(void, wgf_audio_js_pushed_clear, (int handle), {
    var audio = Module.wgf_audio;
    var p = audio && audio.pushed && audio.pushed.get(handle);
    if (!p) return;
    p.head = p.count = 0;
    p.hasB = false;
    p.t = 1;
    p.starved = true;
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
    float gain, pan;
    wgf_audio_priv_voice_range(voice_ptr, &low, &high);
    wgf_audio_priv_spatial(voice_ptr, &gain, &pan);
    wgf_audio_js_voice_apply((int)voice, (int)voice_ptr->sound, voice_ptr->fresh ? 0 : (int)voice_ptr->state,
                             voice_ptr->position, voice_ptr->loop,
                             voice_ptr->volume * gain * wgf_audio_priv_bus_heard(voice_ptr->bus),
                             voice_ptr->pitch, pan,
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

/* Not blocked once an input reached the page (ours seen, or the browser's own word), or the
 * context runs: a paused context is suspended, and not blocked. */
EM_JS(int, wgf_audio_js_is_blocked, (void), {
    var audio = Module.wgf_audio;
    if (audio && (audio.unlocked || (audio.context && audio.context.state === "running"))) return 0;
    return navigator.userActivation && navigator.userActivation.hasBeenActive ? 0 : 1;
})

bool wgf_audio_priv_platform_is_blocked(void)
{
    return wgf_audio_js_is_blocked() != 0;
}

bool wgf_audio_priv_platform_pushed_start(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr)
{
    wgf_audio_js_pushed_start((int)voice, voice_ptr->pushed_rate, voice_ptr->pushed_channels,
                              voice_ptr->pushed_capacity);
    return true;
}

int wgf_audio_priv_platform_pushed_push(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr, const float *samples,
                                        int frames)
{
    (void)voice_ptr;
    return wgf_audio_js_pushed_push((int)voice, samples, frames);
}

int wgf_audio_priv_platform_pushed_queued(wgf_handle_t voice, const wgf_audio_priv_voice_t *voice_ptr)
{
    (void)voice_ptr;
    return wgf_audio_js_pushed_queued((int)voice);
}

int wgf_audio_priv_platform_pushed_underruns(wgf_handle_t voice, const wgf_audio_priv_voice_t *voice_ptr)
{
    (void)voice_ptr;
    return wgf_audio_js_pushed_underruns((int)voice);
}

void wgf_audio_priv_platform_pushed_clear(wgf_handle_t voice, wgf_audio_priv_voice_t *voice_ptr)
{
    (void)voice_ptr;
    wgf_audio_js_pushed_clear((int)voice);
}
