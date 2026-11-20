#ifndef WGF_VOICE_H
#define WGF_VOICE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_bus.h"
#include "wgf_handle.h"
#include "wgf_play_state.h"
#include "wgf_sound.h"
#include "wgf_vec2.h"
#include "wgf_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A voice, an object: a handle of kind "audio.voice". */
typedef wgf_handle_t wgf_voice_t;

/* Voices: one playing of a sound (wgf_sound.h), an object, as wgrender's Sound: as a
 * sprite shows a texture, a voice plays a sound. Many voices may play one decoded sound
 * at once. Music is a looping voice. The same on both platforms: natively libwgf's
 * mixer plays it, on its own thread; on the web the browser does (Web Audio), on its
 * audio thread, so on neither does a slow frame stop the sound.
 *
 * Its state is wgf_play_state_t (wgf_play_state.h), with play, pause, resume, and stop
 * as that header says. COMPLETE: a non-looping voice ran to its end and holds there.
 * A voice told to play a sound still loading is PLAYING, and starts from the start when
 * the sound is READY; if the sound fails, it is COMPLETE. On the web the browser keeps
 * sound off until the page's first input: until then a PLAYING voice is silent and its
 * position doesn't move, so a one-shot played before the first click doesn't become
 * COMPLETE until after it. */

/* A voice of `sound` (0 for none yet: set_sound later), STOPPED, at volume 1, pitch 1,
 * pan 0, not looping; it holds a reference to the sound. 0 for a handle that isn't a
 * sound, or when there's no room for another. */
WGF_API wgf_voice_t wgf_voice_create(wgf_sound_t sound);

/* The voice ended at once, its sound let go of. Nothing for a handle that isn't one. */
WGF_API void wgf_voice_destroy(wgf_voice_t voice);

/* Play `sound` (0 for none) instead, its state kept (CONVENTIONS.md, choosing what to
 * play keeps the state): PLAYING, it plays the new sound from its start (its end, with a
 * negative pitch); PAUSED, it waits there; STOPPED or COMPLETE, it is STOPPED there. With
 * nothing it can play (0, a sound that FAILED, a streamed sound another voice holds), it
 * is STOPPED. Any segment is cleared. False for a handle that isn't a voice, one that
 * isn't a sound, and a pushed voice (below). */
WGF_API bool wgf_voice_set_sound(wgf_voice_t voice, wgf_sound_t sound);
/* Its sound (the voice holds the reference); 0 for none, and for a handle that isn't a
 * voice. */
WGF_API wgf_sound_t wgf_voice_get_sound(wgf_voice_t voice);

/* To PLAYING from the start (the end, with a negative pitch), from any state. False for
 * a voice with no sound (but a pushed one), one whose sound FAILED, and a streamed sound another voice is
 * playing or holding paused (one voice at a time). */
WGF_API bool wgf_voice_play(wgf_voice_t voice);
/* PLAYING to PAUSED, held where it is. False in any other state. */
WGF_API bool wgf_voice_pause(wgf_voice_t voice);
/* PAUSED back to PLAYING. False in any other state. */
WGF_API bool wgf_voice_resume(wgf_voice_t voice);
/* To STOPPED, rewound to the start, from any state. False only for a handle that isn't
 * a voice. */
WGF_API bool wgf_voice_stop(wgf_voice_t voice);
/* STOPPED, PLAYING, PAUSED, or COMPLETE; STOPPED for a handle that isn't a voice. */
WGF_API wgf_play_state_t wgf_voice_get_state(wgf_voice_t voice);

/* Loop: at the end, on from the start (with a negative pitch, from the start back to
 * the end), never COMPLETE. Turning it on while COMPLETE leaves it COMPLETE until the
 * next play. False only for a handle that isn't a voice. */
WGF_API bool wgf_voice_set_loop(wgf_voice_t voice, bool loop);
WGF_API bool wgf_voice_is_loop(wgf_voice_t voice);

/* Loudness: 1 as the file is, 0 silent, above 1 louder (and may clip). Refuses a
 * negative or non-finite one. */
WGF_API bool wgf_voice_set_volume(wgf_voice_t voice, float volume);
WGF_API float wgf_voice_get_volume(wgf_voice_t voice);

/* Speed and tone together: 1 as recorded, 2 an octave up at twice the speed, 0 held
 * where it is (still PLAYING: that is not pause), negative backwards. Clamped to
 * [-16, 16]. Refuses a non-finite one, and a negative one on a streamed sound (its
 * decoders run forwards only) or a pushed voice. */
WGF_API bool wgf_voice_set_pitch(wgf_voice_t voice, float pitch);
WGF_API float wgf_voice_get_pitch(wgf_voice_t voice);

/* Balance: -1 the left only, 0 both (the default), 1 the right only, the other side
 * fading. Clamped to [-1, 1]; refuses a non-finite one. */
WGF_API bool wgf_voice_set_pan(wgf_voice_t voice, float pan);
WGF_API float wgf_voice_get_pan(wgf_voice_t voice);
/* The bus it is heard through (wgf_bus.h): effects until set, its parent's once the one
 * set is destroyed. A play (wgf_sound_play) is on its sound's. False for a handle that
 * isn't a voice, or a bus that isn't one. */
WGF_API bool wgf_voice_set_bus(wgf_voice_t voice, wgf_bus_t bus);
WGF_API wgf_bus_t wgf_voice_get_bus(wgf_voice_t voice);
/* Spatial: heard from its place by the listener (wgf_audio.h): full within its range's near
 * distance, falling off evenly to silent at its far one (1 and 50 by default), and panned
 * by its side of the listener, on top of its own volume and pan. An actor's voice component
 * that is spatial is placed at its actor each frame. False only for a handle that isn't a
 * voice; set_place refuses a non-finite place, set_range a near below 0 or a far not above
 * it. Read back as x, y, z and as near, far. */
WGF_API bool wgf_voice_set_spatial(wgf_voice_t voice, bool spatial);
WGF_API bool wgf_voice_is_spatial(wgf_voice_t voice);
WGF_API bool wgf_voice_set_place(wgf_voice_t voice, float x, float y, float z);
WGF_API wgf_vec3_t wgf_voice_get_place(wgf_voice_t voice);
WGF_API bool wgf_voice_set_range(wgf_voice_t voice, float near_distance, float far_distance);
WGF_API wgf_vec2_t wgf_voice_get_range(wgf_voice_t voice);

/* Where it is, in seconds from the sound's start (its segment's, when it plays one), in
 * any state: it wraps into the sound (the segment) when looping and is clamped to [0, its
 * length] otherwise. Setting it while PLAYING plays on from there (a non-looping voice
 * set to its end is COMPLETE at the next step); while STOPPED, PAUSED, or COMPLETE it
 * moves and the state stays. Refuses a non-finite one, and any while the sound isn't
 * READY. A streamed sound whose file is still arriving and doesn't say its length
 * (wgf_sound_get_duration 0) bounds it only below, by 0: set past what has arrived, the
 * voice waits there for it. Refuses any on a pushed voice. 0 for a handle that isn't a
 * voice. */
WGF_API bool wgf_voice_set_position(wgf_voice_t voice, float seconds);
WGF_API float wgf_voice_get_position(wgf_voice_t voice);

/* Play the named segment of its sound (wgf_sound_add_segment) as the whole sound: from
 * its start (its end, with a negative pitch), looping within it, COMPLETE at its end, its
 * position relative to it (CONVENTIONS.md, a named segment); "" for the whole sound
 * again. Its state is kept, as set_sound keeps it: PLAYING plays the segment from its
 * start, PAUSED waits there, STOPPED or COMPLETE is STOPPED there. Refuses a name its
 * sound has no segment of (a streamed sound has none), NULL, a pushed voice, and a handle
 * that isn't a voice. */
WGF_API bool wgf_voice_set_segment(wgf_voice_t voice, const char *name);
/* Its segment's name; "" for none, and for a handle that isn't a voice. Valid while the
 * voice plays that segment of that sound. */
WGF_API const char *wgf_voice_get_segment(wgf_voice_t voice);

/* A pushed voice: samples the game makes as it goes (a synth, a tracker, an emulator,
 * procedural sound, a voice chat), pushed into the voice's queue and played from it, as
 * raylib's AudioStream. raylib's stream callback, which its mixer calls for more, is the
 * push here: nothing calls back, so each frame the game asks how much room the queue has
 * (wgf_voice_get_room, raylib's IsAudioStreamProcessed) and fills it. Samples are at
 * the voice's own rate, any rate, played at the mixer's (resampled, linearly
 * interpolated, by pitch too); mono or stereo; interleaved, left first.
 *
 * It is a voice: play, pause, resume, and stop; volume, pitch (0 to 16: a negative one is
 * refused, a queue runs forwards only), pan, its bus, and spatial, as any voice's. It has
 * no sound (set_sound refuses), no segments, and no position to set; its position is the
 * seconds it has played since its last play or stop. Loop means nothing to it. PLAYING, it
 * takes what is queued as the mixer needs it; when the queue runs out it plays silence,
 * still PLAYING, and goes on once more is pushed: each time it ran out, after having had
 * something to play, is an underrun (wgf_voice_get_underruns). Paused, or audio paused, it
 * takes nothing and the queue waits. Stop empties the queue; play from STOPPED plays what
 * was pushed meanwhile, so a queue filled before play starts without a gap.
 *
 * Natively libwgf's mixer takes from the queue on its own thread; on the web a
 * ScriptProcessorNode on the page's own thread does, so a long frame there can starve it:
 * keep a tenth of a second or more queued. On the web, too, nothing is taken before the
 * page's first input (the top of this header). */

/* A pushed voice with room for `capacity` frames (a frame is a sample of each channel) of
 * `channels` (1 or 2) at `sample_rate` frames a second, STOPPED, its queue empty, at
 * volume 1, pitch 1, pan 0. 0 for a rate outside 1000 to 384000, channels other than 1 or
 * 2, a capacity outside 1 to 1048576, or when there's no room for another voice. */
WGF_API wgf_voice_t wgf_voice_create_pushed(int sample_rate, int channels, int capacity);
/* `samples` (interleaved, `count` of them: a whole number of frames) queued, -1 to 1 full
 * scale, as many frames as there is room for: how many frames it took. 0 for a voice that
 * isn't a pushed one, and a count below 0 or not a whole number of frames. */
WGF_API int wgf_voice_push(wgf_voice_t voice, const float *samples, int count);
/* The same with 16-bit samples, -32768 to 32767 (clamped), one to an int: how many frames
 * it took. */
WGF_API int wgf_voice_push_int16(wgf_voice_t voice, const int *samples, int count);
/* Frames the queue has room for now, and frames queued and not yet played; 0 for a voice
 * that isn't a pushed one. */
WGF_API int wgf_voice_get_room(wgf_voice_t voice);
WGF_API int wgf_voice_get_queued(wgf_voice_t voice);
/* How many times its queue ran out while it played, after having had samples: each a gap
 * in the sound. 0 for a voice that isn't a pushed one. */
WGF_API int wgf_voice_get_underruns(wgf_voice_t voice);

#ifdef __cplusplus
}
#endif

#endif /* WGF_VOICE_H */
