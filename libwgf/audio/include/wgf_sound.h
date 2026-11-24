#ifndef WGF_SOUND_H
#define WGF_SOUND_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_bus.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A sound is a resource: a handle of kind "audio.sound", or "audio.sound_streamed" for one
 * made with wgf_sound_create_streamed. */
typedef wgf_handle_t wgf_sound_t;

/* Sounds: audio data, loaded when created.
 *
 * A sound is a resource (wgf_resource.h). A voice plays one (wgf_voice.h). A program pulls in
 * audio only by creating a sound.
 *
 * Sounds play from WAV (PCM or float), MP3, and Ogg Vorbis files, mono or stereo.
 * - Natively libwgf decodes them itself.
 * - On the web the browser decodes them, so a page plays what its browser plays. Chromium and
 *   Firefox play all three (tested). Safari and iOS are not tested yet.
 * - A file a platform can't play makes a FAILED sound, logged with its path.
 *
 * A decoded sound is held as float samples, natively and on the web alike. A minute of stereo
 * at 48 kHz is about 23 MB. So create effects with wgf_sound_create, and music with
 * wgf_sound_create_streamed.
 *
 * A sound from a file loads again in place (wgf_asset_reload) when the file is saved:
 * - The sound keeps its handle, its segments, its polyphony, and its bus. A segment that
 *   starts past the new file's end is dropped (logged).
 * - It plays as it was until the new file is decoded. Then a voice PLAYING or PAUSED on it
 *   is at its start again, in the new sound. A voice that is STOPPED or COMPLETE stays so.
 * - A file that won't decode keeps the sound as it was, with one error logged. */

/* Creates the sound at `path`, decoded whole as it loads.
 *
 * The sound is PENDING at once, then READY. It is FAILED in a later update for a file that is
 * missing or won't decode. The same path again gives the same sound, with one more reference.
 *
 * Returns 0 only if there is no room for another sound. */
WGF_API wgf_sound_t wgf_sound_create(const char *path);

/* Creates the sound at `path`, decoded while it plays rather than all at once.
 *
 * Natively the file is held and decoded as it plays. On the web an <audio> element plays it.
 * A local file is READY once its length is known. A file still to be fetched plays as it
 * arrives:
 *
 *   natively  a file the program is downloading (wgf_asset_set_fetching) is READY once
 *             its start has arrived and decodes, and plays while the rest comes. A
 *             voice that catches up with the download waits where the file runs out,
 *             still PLAYING, until more comes. A decoder keeps 128 KB ahead of where it
 *             plays while the file arrives, so a file shorter than that starts once
 *             whole. The sound is FAILED, and its voices stop, if the download fails, or
 *             if the whole file isn't what a manifest lists (wgf_asset_set_manifest). So
 *             is a file that reaches 2 GB while it arrives, logged with its size.
 *   web       a copy here plays as any load's would: a bundled file, or one in the asset
 *             cache that is fresh, trusted, matches its manifest, or is kept by a 304
 *             when asked about. Without a copy, the <audio> element fetches the file
 *             alone, from its URL, as it plays, trying the redirects in turn while one
 *             is missing. It is READY once the browser has its start. What it fetches
 *             is never written to the asset cache. To have the file local (offline,
 *             checked against the manifest), ensure it first (wgf_asset_ensure), and
 *             the next create plays the copy (wgf_asset.h). It needs CORS from another
 *             origin, as any fetch does. If the element fails, the sound is FAILED and
 *             its voices stop.
 *
 * A streamed sound is a sound of its own: the same path made with wgf_sound_create is another
 * sound. Streaming is asked for, never chosen by a file's size, since a streamed sound plays
 * differently: on one voice at a time, with no segments and no negative pitch. */
WGF_API wgf_sound_t wgf_sound_create_streamed(const char *path);

/* Creates a decoded sound from `count` samples, for a sound made in code, such as a tone, a
 * sweep, or a noise burst.
 *
 * - Samples are in -1 to 1, interleaved left then right when `channels` is 2.
 * - `sample_rate` is in frames a second.
 * - The sound plays as a file's does: its plays, polyphony, segments, and voices are the same.
 * - It is READY at once. The samples are copied, so the caller's can go.
 * - It has no path, so another call makes another sound.
 *
 * Returns 0 for any of these:
 * - `samples` is NULL
 * - `count` is below 1, or not a whole number of frames
 * - `channels` is other than 1 or 2
 * - `sample_rate` is outside 8,000 to 192,000 (what every browser's buffers take)
 * - there is no room for another sound
 *
 * For samples made as they play, use a pushed voice (wgf_voice_create_pushed). */
WGF_API wgf_sound_t wgf_sound_create_from_samples(const float *samples, int count, int channels, int sample_rate);

/* Adds a named segment of a decoded sound, from `start` to `end` seconds.
 *
 * A voice selects a segment by name (wgf_voice_set_segment) and plays it as the whole sound.
 * It plays from the segment's start, loops within it, is COMPLETE at its end, and has its
 * position relative to it (see CONVENTIONS.md, "a named segment"). Many voices may play one
 * segment at once. Segments are for one file holding many effects.
 *
 * - Times are given in seconds, and kept as the nearest of the sound's own frames.
 * - The end is clamped to the sound's length.
 * - The same name again changes that segment's range.
 *
 * Returns false for any of these:
 * - a name that is NULL, "", or longer than 31 bytes
 * - a negative or non-finite start or end, or a start at or after the end
 * - a streamed sound (it can't seek freely), or a sound that FAILED
 * - a start at or past a READY sound's end
 *
 * A segment added before the file arrives, and starting past its end, is dropped once the file
 * arrives. That is logged with the segment's name and the file's path.
 *
 * MP3's encoder delay: an MP3 whose encoder wrote its delay plays its segments on time,
 * natively and in Chromium and Firefox (Safari not tested yet). The delay is written in a
 * Xing/Info frame with a LAME extension, as LAME and FFmpeg write. An MP3 without one plays
 * its segments 25 ms late everywhere. That is the encoder's and the decoder's delay, which
 * nothing in the file states. So a file of many effects is best as WAV or Ogg. */
WGF_API bool wgf_sound_add_segment(wgf_sound_t sound, const char *name, float start, float end);

/* Plays `sound` once and forgets it, on a voice the mixer owns and lets go of when it ends.
 *
 * - `volume` is 0 or more.
 * - `pitch` is as a voice's: clamped to -16 to 16, and negative plays from the end.
 * - `pan` is -1 (left) to 1 (right), clamped.
 * - A sound still PENDING plays once it is READY.
 * - The sound's polyphony is how many plays of it sound at once. Past that, its oldest play
 *   starts again with these values.
 * - At most 64 plays sound at once, across every sound. Past that, the oldest is let go of.
 * - A game's own voices (wgf_voice.h) count toward neither limit. A loop, or a sound the game
 *   must stop, needs one of those.
 *
 * Returns false for any of these:
 * - a handle that isn't a decoded sound (a streamed one plays on a voice)
 * - a sound that FAILED
 * - a volume, pitch, or pan that isn't finite
 * - a volume below 0 */
WGF_API bool wgf_sound_play(wgf_sound_t sound, float volume, float pitch, float pan);
/* Sets how many plays of `sound` (wgf_sound_play) sound at once, 1 to 64 (default: 8).
 *
 * A lower count stops none already playing: each play past it takes the oldest. Returns false
 * for a handle that isn't a decoded sound, or a count outside 1 to 64. */
WGF_API bool wgf_sound_set_polyphony(wgf_sound_t sound, int count);
/* Returns the sound's polyphony. Returns 0 for a handle that isn't a decoded sound. */
WGF_API int wgf_sound_get_polyphony(wgf_sound_t sound);
/* Sets or gets the bus the sound's plays (wgf_sound_play) are heard through (wgf_bus.h).
 *
 * It is the effects bus until set, and again once the bus set is destroyed. Returns false for
 * a handle that isn't a decoded sound, or a bus that isn't one. */
WGF_API bool wgf_sound_set_bus(wgf_sound_t sound, wgf_bus_t bus);
WGF_API wgf_bus_t wgf_sound_get_bus(wgf_sound_t sound);

/* Returns the sound's length in seconds.
 *
 * Returns 0 until it is READY, and for a handle that isn't a sound.
 *
 * A streamed sound READY while its file arrives (wgf_sound_create_streamed) has its length if
 * the file states it at its start. Natively that is a WAV's header, or an MP3's Xing or Info
 * frame. On the web it is whatever the browser makes of the file. Otherwise the length is 0
 * until it is known. Natively that is once the file is whole, counted as a whole file's is.
 *
 * The decoders each treat a file's edges their own way. So the same file's length can differ
 * by a few milliseconds between platforms, and between a decoded sound and a streamed one on
 * the web. A 10 ms Ogg reads 10.0 natively, and 8.7 and 12.9 in Chromium. */
WGF_API float wgf_sound_get_duration(wgf_sound_t sound);

#ifdef __cplusplus
}
#endif

#endif /* WGF_SOUND_H */
