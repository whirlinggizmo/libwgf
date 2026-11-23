#ifndef WGF_PLAY_STATE_H
#define WGF_PLAY_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* The state of whatever plays over time: for now, an audio voice and a model's animation.
 * See docs/CONVENTIONS.md, "What plays over time has one state and four calls".
 *
 * Each of these has play, pause, resume, and stop, with the same transitions:
 *
 *   play     to PLAYING from the start, from any state. COMPLETE is included: that is
 *            how a one-shot plays again
 *   pause    from PLAYING only, to PAUSED, held where it was
 *   resume   from PAUSED only, back to PLAYING
 *   stop     to STOPPED, rewound to the start, from any state
 *
 * Each returns false where it doesn't apply, and its header names those cases.
 *
 * - A speed or pitch of 0 holds where it is, still PLAYING. That is not a pause.
 * - A negative speed or pitch runs backwards: play starts at the end, and COMPLETE comes
 *   at the start.
 * - The state changes only in an update, as a resource's status does (wgf_resource.h). */

typedef enum wgf_play_state_t {
    WGF_PLAY_STATE_STOPPED = 0,  /* never played, or stopped and rewound; also read for a handle that isn't one */
    WGF_PLAY_STATE_PLAYING = 1,  /* advancing */
    WGF_PLAY_STATE_PAUSED = 2,   /* held where it was */
    WGF_PLAY_STATE_COMPLETE = 3  /* reached its end without looping, and holds there; looping never completes */
} wgf_play_state_t;

#ifdef __cplusplus
}
#endif

#endif /* WGF_PLAY_STATE_H */
