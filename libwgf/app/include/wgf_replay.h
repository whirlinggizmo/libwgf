#ifndef WGF_REPLAY_H
#define WGF_REPLAY_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_play_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Input recorded and played back by the program itself, as it runs: a demo, an attract
 * mode, a ghost, a bug report's steps (raylib's automation events). A recording is text,
 * an autopilot (BUILDING.md, "Autopilot files"): every input as it reached the program,
 * frame by frame -- keys, typed text, the mouse's position, buttons, and wheel, fingers,
 * and each gamepad's buttons and axes -- which the program keeps as it likes (saved with
 * wgf_fs_write_text, read back with wgf_fs_read_text), and which an autopilot run can fly
 * too; any autopilot plays as a replay. The same natively, on the web, and headless; a
 * program that never calls one of these links none of it.
 *
 * A replay reproduces the input, frame by frame: a frame of the replay gives the program
 * what that frame of the recording got, before the frame's ticks, read by every input call
 * (wgf_keyboard.h, wgf_mouse.h, wgf_touch.h, wgf_gamepad.h, wgf_input.h, wgf_action.h) as
 * real input is. It reproduces nothing else: the program's randomness is its own (seed
 * wgf_random, wgf_random_set_seed, the same as recording starts and as the replay plays),
 * and ticks run on real time, so a frame's input reaches the same tick only where the
 * frames last as long (wgf_loop_set_target_fps steadies them; an autopilot run's frames
 * last exactly a sixtieth of a second). A finger's tap, double tap, and swipe are timed in
 * real time too. Recording and playing are one at a time. */

/* Recording, from the next frame: each input reaching the program is written, at the
 * frame it reached it, counted from 0, the pointer where it is written first. A recording
 * starts with nothing held: a key down as it starts is not in it. False while recording
 * or while a replay plays or is paused. */
WGF_API bool wgf_replay_start_recording(void);
/* Recording stopped: an `end` written at its last frame. False when not recording. */
WGF_API bool wgf_replay_stop_recording(void);
/* Whether it is recording. */
WGF_API bool wgf_replay_is_recording(void);
/* The recording, as an autopilot's text, so far while recording; "" for none. Valid
 * until the next start or clear. */
WGF_API const char *wgf_replay_get_recording(void);
/* The frames recorded, so far while recording; 0 for none. */
WGF_API int wgf_replay_get_recording_frame_count(void);
/* The recording let go of, stopped first while recording. */
WGF_API void wgf_replay_clear_recording(void);

/* A replay: `text`, an autopilot (a recording), played from the next frame, a frame of it
 * each frame of the program's, in place of whatever plays. Its inputs alone are played: an
 * autopilot's seed, expectations, waits, logs, screenshots, and dumps are an autopilot
 * run's, so a replay passes over them. It lasts until its `end`, or without one its last
 * line's frame, then is COMPLETE.
 *
 * While it plays, and while it is paused, the program's input is the replay's alone: real
 * input (the window's, and an autopilot run's) is held back, never reaching an input call,
 * and every gamepad is the replay's (disconnected unless it connects one). Whether a real
 * key, mouse button, finger, or pad button was pressed meanwhile is
 * wgf_replay_has_real_press, so an attract mode can end on one. Everything held is let go
 * of as a replay starts and as it ends (stopped or complete), so it starts from nothing
 * held and leaves nothing held: the releases are that frame's edges. Paused, it holds what
 * it holds.
 *
 * Its state is wgf_play_state_t (wgf_play_state.h), with play, pause, resume, and stop as
 * that header says. False for text that isn't an autopilot (each bad line logged), with
 * whatever plays left playing, and while recording. */
WGF_API bool wgf_replay_play(const char *text);
/* False unless PLAYING. */
WGF_API bool wgf_replay_pause(void);
/* False unless PAUSED. */
WGF_API bool wgf_replay_resume(void);
/* The replay let go of, its input with it. False when STOPPED. */
WGF_API bool wgf_replay_stop(void);
/* The replay's state: STOPPED when none plays. */
WGF_API wgf_play_state_t wgf_replay_get_state(void);
/* The replay's frames played so far: 0 before its first, its frame count once COMPLETE;
 * 0 when STOPPED. */
WGF_API int wgf_replay_get_frame(void);
/* The replay's length in frames: through its end; 0 when STOPPED. */
WGF_API int wgf_replay_get_frame_count(void);
/* Whether real input held back by a replay pressed something (a key, a mouse button, a
 * finger, or a pad button) for this frame: since the previous frame, read the whole frame
 * and its ticks. False while nothing plays. */
WGF_API bool wgf_replay_has_real_press(void);

#ifdef __cplusplus
}
#endif

#endif
