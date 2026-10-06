#ifndef WGF_VOICE_H
#define WGF_VOICE_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_handle.h"
#include "wgf_play_state.h"
#include "wgf_sound.h"

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
 * is STOPPED. Any segment is cleared. False for a handle that isn't a voice, or one that
 * isn't a sound. */
WGF_API bool wgf_voice_set_sound(wgf_voice_t voice, wgf_sound_t sound);
/* Its sound (the voice holds the reference); 0 for none, and for a handle that isn't a
 * voice. */
WGF_API wgf_sound_t wgf_voice_get_sound(wgf_voice_t voice);

/* To PLAYING from the start (the end, with a negative pitch), from any state. False for
 * a voice with no sound, one whose sound FAILED, and a streamed sound another voice is
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
 * decoders run forwards only). */
WGF_API bool wgf_voice_set_pitch(wgf_voice_t voice, float pitch);
WGF_API float wgf_voice_get_pitch(wgf_voice_t voice);

/* Balance: -1 the left only, 0 both (the default), 1 the right only, the other side
 * fading. Clamped to [-1, 1]; refuses a non-finite one. */
WGF_API bool wgf_voice_set_pan(wgf_voice_t voice, float pan);
WGF_API float wgf_voice_get_pan(wgf_voice_t voice);

/* Where it is, in seconds from the sound's start (its segment's, when it plays one), in
 * any state: it wraps into the sound (the segment) when looping and is clamped to [0, its
 * length] otherwise. Setting it while PLAYING plays on from there (a non-looping voice
 * set to its end is COMPLETE at the next step); while STOPPED, PAUSED, or COMPLETE it
 * moves and the state stays. Refuses a non-finite one, and any while the sound isn't
 * READY. A streamed sound whose file is still arriving and doesn't say its length
 * (wgf_sound_get_duration 0) bounds it only below, by 0: set past what has arrived, the
 * voice waits there for it. 0 for a handle that isn't a voice. */
WGF_API bool wgf_voice_set_position(wgf_voice_t voice, float seconds);
WGF_API float wgf_voice_get_position(wgf_voice_t voice);

/* Play the named segment of its sound (wgf_sound_add_segment) as the whole sound: from
 * its start (its end, with a negative pitch), looping within it, COMPLETE at its end, its
 * position relative to it (CONVENTIONS.md, a named segment); "" for the whole sound
 * again. Its state is kept, as set_sound keeps it: PLAYING plays the segment from its
 * start, PAUSED waits there, STOPPED or COMPLETE is STOPPED there. Refuses a name its
 * sound has no segment of (a streamed sound has none), NULL, and a handle that isn't a
 * voice. */
WGF_API bool wgf_voice_set_segment(wgf_voice_t voice, const char *name);
/* Its segment's name; "" for none, and for a handle that isn't a voice. Valid while the
 * voice plays that segment of that sound. */
WGF_API const char *wgf_voice_get_segment(wgf_voice_t voice);

#ifdef __cplusplus
}
#endif

#endif /* WGF_VOICE_H */
