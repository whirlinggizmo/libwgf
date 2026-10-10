#ifndef WGF_PLAY_STATE_H
#define WGF_PLAY_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* The state of anything that plays over time: an audio voice and a model's animation
 * now (docs/CONVENTIONS.md, "What plays over time has one state and four calls"). Each
 * such object has play, pause, resume, and stop, with the same transitions:
 *
 *   play     to PLAYING from the start, from any state (COMPLETE included: that is how
 *            a one-shot plays again)
 *   pause    from PLAYING only, to PAUSED, held where it was
 *   resume   from PAUSED only, back to PLAYING
 *   stop     to STOPPED, rewound to the start, from any state
 *
 * and each returns false where it doesn't apply, its header naming those cases. A speed
 * or pitch of 0 holds where it is, still PLAYING: that is not pause. A negative one runs
 * backwards: play starts at the end and COMPLETE comes at the start. Read it as a
 * resource's status is read (wgf_resource.h): it changes only in an update. */

typedef enum wgf_play_state_t {
    WGF_PLAY_STATE_STOPPED = 0,  /* never played, or stopped: rewound to the start; also what a
                                    handle that isn't one reads */
    WGF_PLAY_STATE_PLAYING = 1,  /* advancing */
    WGF_PLAY_STATE_PAUSED = 2,   /* paused: held where it was */
    WGF_PLAY_STATE_COMPLETE = 3  /* ran out: a non-looping one reached its end, and holds there;
                                    looping never does */
} wgf_play_state_t;

#ifdef __cplusplus
}
#endif

#endif /* WGF_PLAY_STATE_H */
