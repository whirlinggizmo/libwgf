#ifndef WGF_SOUND_H
#define WGF_SOUND_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A sound, a resource: a handle of kind "audio.sound", or "audio.sound_streamed" for one
 * made with wgf_sound_create_streamed. */
typedef wgf_handle_t wgf_sound_t;

/* Sounds: audio's data, a resource (wgf_resource.h), loaded on create, as wgrender's
 * Audio; a voice is to play one (phase 2, docs/ROADMAP.md). A program pulls in audio
 * only by creating a sound.
 *
 * What plays: WAV (PCM or float), MP3, and Ogg Vorbis, mono or stereo. Natively libwgf
 * decodes them (dr_wav, dr_mp3, Xiph's libvorbis); on the web the browser does, so a
 * page plays what its browser plays: Chromium and Firefox play all three (tested);
 * Safari and iOS are not tested yet. A file a platform can't play makes a FAILED sound,
 * logged with its path.
 *
 * Memory: a decoded sound is float samples, natively and on the web alike: a minute of
 * stereo at 48 kHz is about 23 MB. So: effects, create; music, create_streamed. */

/* The sound at `path`, decoded whole as it loads: PENDING at once, then READY, or FAILED
 * in a later update for a file that is missing or won't decode. The same path again
 * gives the same sound, with one more reference. 0 only when there's no room for
 * another. */
WGF_API wgf_sound_t wgf_sound_create(const char *path);

/* The sound at `path`, decoded while it plays rather than all at once (natively the file
 * is held and decoded as it plays; on the web an <audio> element plays it). READY once
 * its length is known, for a file that is local; one still to be fetched plays as it
 * arrives:
 *
 *   natively  a file the program is downloading (wgf_asset_set_fetching) is READY once
 *             its start has arrived and decodes, and plays while the rest comes, a
 *             voice that catches up with the download waiting where the file runs out,
 *             PLAYING, until more comes. A decoder keeps 128 KB ahead of where it plays
 *             while the file arrives, so a file shorter than that starts once whole. If
 *             the download fails, or once whole the file isn't what a manifest lists
 *             (wgf_asset_set_manifest), the sound is FAILED and its voices stop; so too
 *             a file that reaches 2 GB while it arrives, logged with its size.
 *   web       a copy here plays as any load's would (a bundled file; one in the asset
 *             cache that is fresh, trusted, matches its manifest, or is kept by a 304
 *             when asked about); without one, the <audio> element fetches the file
 *             alone, from its URL, as it plays (the redirects' tried in turn while one
 *             is missing), READY once the browser has its start. What it fetches is
 *             never written to the asset cache: to have the file local (offline,
 *             checked against the manifest), ensure it first (wgf_asset_ensure), and
 *             the next create plays the copy (wgf_asset.h). It needs CORS from another
 *             origin, as any fetch does. If the element fails, the sound is FAILED and
 *             its voices stop.
 *
 * A sound of its
 * own: the same path made with wgf_sound_create is another sound. Asked for, never
 * chosen by a file's size, since it plays differently: on one voice at a time, with no
 * segments and no negative pitch. */
WGF_API wgf_sound_t wgf_sound_create_streamed(const char *path);

/* A named segment of a decoded sound, from `start` to `end` seconds, which a voice
 * selects by name (wgf_voice_set_segment) and plays as the whole sound: play from its
 * start, loop within it, COMPLETE at its end, position relative to it (CONVENTIONS.md, a
 * named segment). Many voices may play one segment at once; for one file of many
 * effects. Seconds in, the sound's own frames inside, the nearest. The end is clamped to
 * the sound's length; the same name again changes that segment's range. Refuses a name
 * that is NULL, "", or longer than 31 bytes; a negative or non-finite start or end, or a
 * start at or after the end; a streamed sound (it can't seek freely) and one that
 * FAILED; and a start at or past a READY sound's end. Added before the file comes, a
 * segment starting past its end is dropped then, logged with its name and the file's
 * path. MP3's encoder delay: an MP3 whose encoder wrote its delay (a Xing/Info frame with
 * a LAME extension, as LAME and FFmpeg write) plays its segments on time natively and in
 * Chromium and Firefox (Safari not tested yet); one without plays them 25 ms late
 * everywhere (the encoder's and the decoder's delay, which nothing in the file says), so
 * a file of many effects is best WAV or Ogg. */
WGF_API bool wgf_sound_add_segment(wgf_sound_t sound, const char *name, float start, float end);

/* Its length in seconds; 0 until it is READY, and for a handle that isn't a sound. A
 * streamed sound READY while its file arrives (wgf_sound_create_streamed) has its length
 * when the file says it at its start (natively a WAV's header, an MP3's Xing or Info
 * frame; on the web whatever the browser makes of it), and otherwise 0 until it is
 * known: natively once the file is whole, counted as a whole file's. The
 * decoders treat a file's edges each their own way, so the same file's length can
 * differ by a few milliseconds between platforms, and between a decoded sound and a
 * streamed one on the web (a 10 ms Ogg: 10.0 natively, 8.7 and 12.9 in Chromium). */
WGF_API float wgf_sound_get_duration(wgf_sound_t sound);

#ifdef __cplusplus
}
#endif

#endif /* WGF_SOUND_H */
