#ifndef WGF_APP_AUTOPILOT_PRIV_H
#define WGF_APP_AUTOPILOT_PRIV_H

#include <stdbool.h>

/* An autopilot run: inputs at frames, and assertions on probes, read from an autopilot --
 * natively the file LIBWGF_AUTOPILOT names, on the web the text the page hands the
 * module as Module["wgfAutopilot"] -- so a program can be driven and checked with no
 * one at it (the `wgf` tool's autopilot, a game's playthrough in CI). The format, its
 * commands, its time, and how its result is logged are BUILDING.md's ("Autopilot
 * files"), a game developer's doc, stated only there; wgf_app_autopilot.c parses it. */

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
