#ifndef WGF_APP_AUTOPILOT_PRIV_H
#define WGF_APP_AUTOPILOT_PRIV_H

#include <stdbool.h>

/* An autopilot run: inputs at frames, and assertions on probes, read from an autopilot --
 * natively the file LIBWGF_AUTOPILOT names, on the web the text the page hands the
 * module as Module["wgfAutopilot"] -- so a program can be driven and checked with no
 * one at it (the `wgf` tool's autopilot, a game's playthrough in CI). The format, a line
 * each, `#` starting a comment:
 *
 *   wgf-autopilot 1                 the first line: the format and its version
 *   seed <int>                   wgf_random's seed, set before the program's init
 *   at <frame> <command>         a command at a frame, frames counted from 0
 *
 * where a command is one of
 *
 *   key down|up|tap <key>        a key by name (wgf_keyboard.h's, lower case: a, 1,
 *                                space, enter, left, left_shift, f1...); tap is down
 *                                at this frame and up at the next
 *   text <characters>            typed characters (UTF-8, the rest of the line)
 *   mouse move <x> <y>           the pointer, in logical pixels
 *   mouse down|up|click <button> left, right, or middle; click is down, then up the
 *                                next frame
 *   mouse scroll <dx> <dy>
 *   pad <n> connect|disconnect   pad 0 to 3 (an autopilot's pads replace the real ones)
 *   pad <n> down|up|tap <button> wgf_gamepad.h's, lower case: south, dpad_up, start...
 *   pad <n> axis <axis> <value>  left_x, left_y, right_x, right_y (-1 to 1),
 *                                left_trigger, right_trigger (0 to 1)
 *   expect <probe> <op> <number> after the frame: ==, !=, <, <=, >, or >= against a
 *                                probe (wgf_probe.h); a probe not set fails
 *   log <text>                   the text, logged, to mark a point in the run
 *   screenshot <name>            "wgf_autopilot: SCREENSHOT <name>" logged, for a tool
 *                                watching the run to save the frame
 *   dump                         after the frame, each optional part's state as text
 *                                (the ecs's world, as a scene), logged a line at a
 *                                time: "wgf_autopilot: DUMP <part>| <line>", between
 *                                "wgf_autopilot: DUMP <part> BEGIN at frame <n>" and "... END"
 *   end                          after the frame: the run's result logged, and quit
 *
 * Inputs at a frame are delivered before its ticks; expectations are checked after
 * it. While an autopilot runs, time is the autopilot's: every frame lasts one sixtieth of a
 * second, whatever the display does, so ticks, and the random numbers a seed gives,
 * make the same run everywhere; a headless run doesn't wait for a display.
 *
 * The run's result is logged: "wgf_autopilot: PASS (<n> expectations, <frames> frames)"
 * at info, or "wgf_autopilot: FAIL ..." at error, with an error for each expectation
 * that failed as it fails; an autopilot that can't be read, or a program that quits
 * before the autopilot's end, is an error too. Errors are what the tools judge a run by
 * (on the web there is no exit code). */

/* Load the autopilot the environment names, if any, at the runtime's init; an autopilot that
 * can't be read is logged and ends the program. True when an autopilot is running. */
bool wgf_app_priv_autopilot_start(void);
/* For tests: run `text` as the autopilot, in place of the environment's. */
bool wgf_app_priv_autopilot_start_text(const char *text);
/* Whether an autopilot is running: the runtime gives it its time. */
bool wgf_app_priv_autopilot_is_running(void);
/* Frame `frame`'s inputs, before its ticks; its expectations and end, after it. */
void wgf_app_priv_autopilot_begin_frame(long frame);
void wgf_app_priv_autopilot_end_frame(long frame);
/* The program is quitting: an autopilot not yet at its end fails. */
void wgf_app_priv_autopilot_stop(long frames);
/* For tests: the expectations failed so far, and whether the autopilot has ended
 * (reached `end`) and passed. */
int wgf_app_priv_autopilot_get_failures(void);
bool wgf_app_priv_autopilot_has_passed(void);

#endif
