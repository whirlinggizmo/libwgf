#ifndef WGF_REPLAY_H
#define WGF_REPLAY_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_play_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Input that the program records and plays back itself, as it runs: a demo, an attract
 * mode, a ghost, or a bug report's steps.
 *
 * A recording is text, in the form of an autopilot (BUILDING.md, "Autopilot files"). It
 * holds every input as it reached the program, frame by frame:
 * - keys and typed text
 * - the mouse's position, buttons, and wheel
 * - fingers
 * - each gamepad's buttons and axes
 *
 * The program keeps a recording as it likes, such as saving it with wgf_fs_write and reading
 * it back with wgf_fs_read and wgf_fs_task_get_text. An autopilot run can fly a recording too, and any
 * autopilot plays as a replay. Replays work the same natively, on the web, and headless. A
 * program that never calls one of these links none of it.
 *
 * A replay reproduces the input, frame by frame. A frame of the replay gives the program
 * what that frame of the recording got, before the frame's ticks. Every input call reads it
 * as it reads real input (wgf_keyboard.h, wgf_mouse.h, wgf_touch.h, wgf_gamepad.h,
 * wgf_input.h, wgf_action.h).
 *
 * A replay reproduces nothing else:
 * - The program's randomness is its own. Seed wgf_random (wgf_random_set_seed) the same
 *   way as recording starts and as the replay plays.
 * - Ticks run on real time. So a frame's input reaches the same tick only where the
 *   frames last as long. wgf_loop_set_target_fps steadies them, and an autopilot run's
 *   frames last exactly a sixtieth of a second.
 * - A finger's tap, double tap, and swipe are timed in real time too.
 *
 * Recording and playing happen one at a time. */

/* Starts recording, from the next frame.
 *
 * Each input that reaches the program is written at the frame it reached it, counting
 * frames from 0. The pointer is written first, where it is. A recording starts with
 * nothing held, so a key that is down as it starts is not in it.
 *
 * Returns false while recording, or while a replay plays or is paused. */
WGF_API bool wgf_replay_start_recording(void);
/* Stops recording, writing an `end` at its last frame. Returns false if not recording. */
WGF_API bool wgf_replay_stop_recording(void);
/* Returns whether it is recording. */
WGF_API bool wgf_replay_is_recording(void);
/* Returns the recording as an autopilot's text: so far, while recording. Returns "" if there
 * is none. Valid until the next start or clear. */
WGF_API const char *wgf_replay_get_recording(void);
/* Returns the number of frames recorded: so far, while recording. Returns 0 if there is
 * none. */
WGF_API int wgf_replay_get_recording_frame_count(void);
/* Clears the recording, stopping it first if it is recording. */
WGF_API void wgf_replay_clear_recording(void);

/* Plays `text`, an autopilot such as a recording, as a replay from the next frame.
 *
 * - Each frame of the program's plays one frame of the replay.
 * - It plays in place of whatever is playing.
 * - Only its inputs are played. An autopilot's seed, expectations, waits, logs,
 *   screenshots, and dumps belong to an autopilot run, so a replay passes over them.
 * - It lasts until its `end`, or without one, until its last line's frame. Then it is
 *   COMPLETE.
 *
 * While it plays, and while it is paused, the program's input is the replay's alone:
 * - Real input, from the window or an autopilot run, is held back and never reaches an
 *   input call.
 * - Every gamepad is the replay's. A gamepad is disconnected unless the replay connects it.
 * - wgf_replay_has_real_press tells whether a real key, mouse button, finger, or pad button
 *   was pressed meanwhile, so an attract mode can end on one.
 *
 * Everything held is released as a replay starts, and as it ends (stopped or complete). So
 * it starts from nothing held and leaves nothing held, and the releases are that frame's
 * edges. While paused, it keeps held what it holds.
 *
 * Its state is a wgf_play_state_t, with play, pause, resume, and stop as wgf_play_state.h
 * says.
 *
 * Returns false while recording. It also returns false for text that isn't an autopilot,
 * logging each bad line, and leaves whatever plays still playing. */
WGF_API bool wgf_replay_play(const char *text);
/* Pauses the replay. Returns false unless it is PLAYING. */
WGF_API bool wgf_replay_pause(void);
/* Resumes the replay. Returns false unless it is PAUSED. */
WGF_API bool wgf_replay_resume(void);
/* Stops the replay, releasing its input with it. Returns false if it is STOPPED. */
WGF_API bool wgf_replay_stop(void);
/* Returns the replay's state: STOPPED when none plays. */
WGF_API wgf_play_state_t wgf_replay_get_state(void);
/* Returns how many of the replay's frames have played so far.
 *
 * It is 0 before the first, and the replay's frame count once COMPLETE. Returns 0 when
 * STOPPED. */
WGF_API int wgf_replay_get_frame(void);
/* Returns the replay's length in frames, through its end. Returns 0 when STOPPED. */
WGF_API int wgf_replay_get_frame_count(void);
/* Returns whether the real input a replay holds back pressed a key, a mouse button, a
 * finger, or a pad button this frame.
 *
 * It covers the presses since the previous frame, and reads the same through the whole
 * frame and its ticks. Returns false while nothing plays. */
WGF_API bool wgf_replay_has_real_press(void);

#ifdef __cplusplus
}
#endif

#endif
