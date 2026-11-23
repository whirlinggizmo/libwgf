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

/* A voice is an object: a handle of kind "audio.voice". */
typedef wgf_handle_t wgf_voice_t;

/* Voices: a voice is one playing of a sound (wgf_sound.h).
 *
 * As a sprite shows a texture, a voice plays a sound. Many voices may play one decoded sound
 * at once. Music is a looping voice.
 *
 * Voices work the same on both platforms. Natively libwgf's mixer plays them, on its own
 * thread. On the web the browser does (Web Audio), on its audio thread. So on neither does a
 * slow frame stop the sound.
 *
 * A voice's state is wgf_play_state_t (wgf_play_state.h), with play, pause, resume, and stop
 * as that header says.
 * - COMPLETE: a non-looping voice ran to its end and holds there.
 * - A voice told to play a sound still loading is PLAYING. It starts from the start when the
 *   sound is READY. If the sound fails, the voice is COMPLETE.
 * - On the web the browser keeps sound off until the page's first input. Until then a PLAYING
 *   voice is silent and its position doesn't move. So a one-shot played before the first
 *   click doesn't become COMPLETE until after it. */

/* Creates a voice of `sound`, STOPPED, at volume 1, pitch 1, and pan 0, not looping.
 *
 * Pass 0 for no sound yet, and set one later with wgf_voice_set_sound. The voice holds a
 * reference to the sound.
 *
 * Returns 0 for a handle that isn't a sound, or if there is no room for another voice. */
WGF_API wgf_voice_t wgf_voice_create(wgf_sound_t sound);

/* Destroys `voice` at once, letting go of its sound. Does nothing for a handle that isn't a
 * voice. */
WGF_API void wgf_voice_destroy(wgf_voice_t voice);

/* Sets the sound the voice plays instead (0 for none), keeping its state (see CONVENTIONS.md,
 * "choosing what to play keeps the state").
 *
 * - PLAYING: it plays the new sound from its start (from its end, with a negative pitch).
 * - PAUSED: it waits there.
 * - STOPPED or COMPLETE: it is STOPPED there.
 * - With nothing it can play (0, a sound that FAILED, or a streamed sound another voice
 *   holds), it is STOPPED.
 * - Any segment is cleared.
 *
 * Returns false for a handle that isn't a voice, a `sound` that isn't a sound, or a pushed
 * voice (see wgf_voice_create_pushed). */
WGF_API bool wgf_voice_set_sound(wgf_voice_t voice, wgf_sound_t sound);
/* Returns the voice's sound. The voice holds the reference, so don't release what this
 * returns: that would let go of the voice's own. Returns 0 for none, and for a handle that
 * isn't a voice. */
WGF_API wgf_sound_t wgf_voice_get_sound(wgf_voice_t voice);

/* Plays the voice from the start (the end, with a negative pitch), from any state: PLAYING.
 *
 * Returns false for any of these:
 * - a voice with no sound, unless it is a pushed voice
 * - a voice whose sound FAILED
 * - a streamed sound that another voice is playing or holding paused (one voice at a time) */
WGF_API bool wgf_voice_play(wgf_voice_t voice);
/* Pauses a PLAYING voice, holding it where it is: PAUSED. Returns false in any other state. */
WGF_API bool wgf_voice_pause(wgf_voice_t voice);
/* Resumes a PAUSED voice: PLAYING. Returns false in any other state. */
WGF_API bool wgf_voice_resume(wgf_voice_t voice);
/* Stops the voice from any state, rewound to the start: STOPPED. Returns false only for a
 * handle that isn't a voice. */
WGF_API bool wgf_voice_stop(wgf_voice_t voice);
/* Returns the voice's state: STOPPED, PLAYING, PAUSED, or COMPLETE. Returns STOPPED for a
 * handle that isn't a voice. */
WGF_API wgf_play_state_t wgf_voice_get_state(wgf_voice_t voice);

/* Sets or gets whether the voice loops.
 *
 * A looping voice goes on from the start at its end, and is never COMPLETE. With a negative
 * pitch, it goes from the start back to the end. Turning looping on while COMPLETE leaves the
 * voice COMPLETE until the next play. Returns false only for a handle that isn't a voice. */
WGF_API bool wgf_voice_set_loop(wgf_voice_t voice, bool loop);
WGF_API bool wgf_voice_is_loop(wgf_voice_t voice);

/* Sets or gets the voice's volume.
 *
 * - 1 plays the sound as the file is.
 * - 0 is silent.
 * - Above 1 is louder, and may clip.
 *
 * Returns false for a negative or non-finite volume. */
WGF_API bool wgf_voice_set_volume(wgf_voice_t voice, float volume);
WGF_API float wgf_voice_get_volume(wgf_voice_t voice);

/* Sets or gets the voice's pitch: its speed and tone together, clamped to [-16, 16].
 *
 * - 1 plays as recorded.
 * - 2 plays an octave up, at twice the speed.
 * - 0 holds it where it is. It is still PLAYING: that is not a pause.
 * - A negative pitch plays backwards.
 *
 * Returns false for a non-finite pitch. Also returns false for a negative pitch on a streamed
 * sound (its decoders run forwards only) or on a pushed voice. */
WGF_API bool wgf_voice_set_pitch(wgf_voice_t voice, float pitch);
WGF_API float wgf_voice_get_pitch(wgf_voice_t voice);

/* Sets or gets the voice's pan, its balance, clamped to [-1, 1] (default: 0).
 *
 * - -1 plays the left only.
 * - 0 plays both.
 * - 1 plays the right only.
 * - Between, the other side fades.
 *
 * Returns false for a non-finite pan. */
WGF_API bool wgf_voice_set_pan(wgf_voice_t voice, float pan);
WGF_API float wgf_voice_get_pan(wgf_voice_t voice);
/* Sets or gets the bus the voice is heard through (wgf_bus.h).
 *
 * It is the effects bus until set. Once the bus set is destroyed, it is that bus's parent. A
 * play (wgf_sound_play) is on its sound's bus instead. Returns false for a handle that isn't a
 * voice, or a bus that isn't one. */
WGF_API bool wgf_voice_set_bus(wgf_voice_t voice, wgf_bus_t bus);
WGF_API wgf_bus_t wgf_voice_get_bus(wgf_voice_t voice);
/* Sets or gets whether the voice is spatial, and its place and range.
 *
 * A spatial voice is heard from its place by the listener (wgf_audio.h), on top of its own
 * volume and pan.
 * - It is at full volume within its range's near distance (default: 1).
 * - It falls off evenly to silent at its far distance (default: 50).
 * - It is panned by which side of the listener it is on.
 * - An actor's voice component that is spatial is placed at its actor each frame.
 *
 * The place reads back as x, y, z, and the range as near, far.
 *
 * Returns false only for a handle that isn't a voice. Also, set_place returns false for a
 * non-finite place, and set_range for a near below 0 or a far not above the near. */
WGF_API bool wgf_voice_set_spatial(wgf_voice_t voice, bool spatial);
WGF_API bool wgf_voice_is_spatial(wgf_voice_t voice);
WGF_API bool wgf_voice_set_place(wgf_voice_t voice, float x, float y, float z);
WGF_API wgf_vec3_t wgf_voice_get_place(wgf_voice_t voice);
WGF_API bool wgf_voice_set_range(wgf_voice_t voice, float near_distance, float far_distance);
WGF_API wgf_vec2_t wgf_voice_get_range(wgf_voice_t voice);

/* Sets or gets where the voice is, in seconds from the sound's start, in any state.
 *
 * When it plays a segment, the position is from the segment's start.
 * - On a looping voice, the position wraps into the sound (or the segment).
 * - On a voice that isn't looping, it is clamped to [0, its length].
 * - Set while PLAYING, the voice plays on from there. A non-looping voice set to its end is
 *   COMPLETE at the next step.
 * - Set while STOPPED, PAUSED, or COMPLETE, it moves and the state stays.
 * - A streamed sound whose file is still arriving, and doesn't state its length
 *   (wgf_sound_get_duration returns 0), bounds the position only below, by 0. Set past what
 *   has arrived, the voice waits there for it.
 *
 * Setting returns false for a non-finite position, for any position while the sound isn't
 * READY, and for any on a pushed voice. Getting returns 0 for a handle that isn't a voice. */
WGF_API bool wgf_voice_set_position(wgf_voice_t voice, float seconds);
WGF_API float wgf_voice_get_position(wgf_voice_t voice);

/* Sets the named segment of the voice's sound (wgf_sound_add_segment) to play as the whole
 * sound. Pass "" for the whole sound again.
 *
 * The segment plays from its start (its end, with a negative pitch), loops within it, is
 * COMPLETE at its end, and has its position relative to it (see CONVENTIONS.md, "a named
 * segment").
 *
 * The voice's state is kept, as wgf_voice_set_sound keeps it:
 * - PLAYING plays the segment from its start.
 * - PAUSED waits there.
 * - STOPPED or COMPLETE is STOPPED there.
 *
 * Returns false for a name the sound has no segment of (a streamed sound has none), NULL, a
 * pushed voice, or a handle that isn't a voice. */
WGF_API bool wgf_voice_set_segment(wgf_voice_t voice, const char *name);
/* Returns the name of the voice's segment. Returns "" for none, and for a handle that isn't a
 * voice. The string is valid while the voice plays that segment of that sound. */
WGF_API const char *wgf_voice_get_segment(wgf_voice_t voice);

/* Pushed voices: a pushed voice plays samples the game makes as it goes.
 *
 * Use one for a synth, a tracker, an emulator, procedural sound, or a voice chat. The game
 * pushes samples into the voice's queue, and the voice plays from it. Nothing calls back for
 * more: each frame the game asks how much room the queue has (wgf_voice_get_room) and fills it.
 *
 * - Samples are at the voice's own rate, any rate, played at the mixer's rate. They are
 *   resampled with linear interpolation, by pitch too.
 * - Samples are mono or stereo, interleaved, left first.
 *
 * A pushed voice is a voice: play, pause, resume, stop, volume, pan, its bus, and spatial work
 * as any voice's do. Its pitch is 0 to 16: a negative one is refused, since a queue runs
 * forwards only.
 * - It has no sound (set_sound refuses), no segments, and no position to set.
 * - Its position is the seconds it has played since its last play or stop.
 * - Looping means nothing to it.
 * - PLAYING, it takes what is queued as the mixer needs it. When the queue runs out it plays
 *   silence, still PLAYING, and goes on once more is pushed.
 * - Each time the queue ran out, after having had samples to play, is an underrun
 *   (wgf_voice_get_underruns).
 * - Paused, or with audio paused, it takes nothing and the queue waits.
 * - Stop empties the queue. Play from STOPPED plays what was pushed meanwhile, so a queue
 *   filled before play starts without a gap.
 *
 * Natively libwgf's mixer takes from the queue on its own thread. On the web a
 * ScriptProcessorNode on the page's own thread does, so a long frame there can starve it. Keep
 * a tenth of a second or more queued. On the web, too, nothing is taken before the page's
 * first input (see the top of this header). */

/* Creates a pushed voice, STOPPED, with its queue empty, at volume 1, pitch 1, and pan 0.
 *
 * - `capacity` is the queue's room, in frames. A frame is a sample of each channel.
 * - `channels` is 1 or 2.
 * - `sample_rate` is in frames a second.
 *
 * Returns 0 for any of these:
 * - a rate outside 1000 to 384000
 * - channels other than 1 or 2
 * - a capacity outside 1 to 1048576
 * - no room for another voice */
WGF_API wgf_voice_t wgf_voice_create_pushed(int sample_rate, int channels, int capacity);
/* Queues `count` samples from `samples`, as many frames as there is room for.
 *
 * Samples are interleaved, -1 to 1 full scale, and `count` is a whole number of frames.
 *
 * Returns how many frames it took. Returns 0 for a voice that isn't a pushed one, or a count
 * below 0 or not a whole number of frames. */
WGF_API int wgf_voice_push(wgf_voice_t voice, const float *samples, int count);
/* Queues 16-bit samples, as wgf_voice_push does: -32768 to 32767 (clamped), one to an int.
 *
 * Returns how many frames it took. */
WGF_API int wgf_voice_push_int16(wgf_voice_t voice, const int *samples, int count);
/* Returns the frames the queue has room for now (get_room), and the frames queued and not yet
 * played (get_queued). Each returns 0 for a voice that isn't a pushed one. */
WGF_API int wgf_voice_get_room(wgf_voice_t voice);
WGF_API int wgf_voice_get_queued(wgf_voice_t voice);
/* Returns how many times the queue ran out while the voice played, after having had samples.
 * Each is a gap in the sound.
 *
 * Returns 0 for a voice that isn't a pushed one. */
WGF_API int wgf_voice_get_underruns(wgf_voice_t voice);

#ifdef __cplusplus
}
#endif

#endif /* WGF_VOICE_H */
