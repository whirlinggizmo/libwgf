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

/* The autopilot's name for a key, a mouse button, a pad button, or a pad axis (its lines'
 * words); NULL for a code it has no name for. */
typedef enum {
    WGF_APP_PRIV_NAME_KEY,
    WGF_APP_PRIV_NAME_MOUSE_BUTTON,
    WGF_APP_PRIV_NAME_PAD_BUTTON,
    WGF_APP_PRIV_NAME_PAD_AXIS
} wgf_app_priv_name_kind_t;
const char *wgf_app_priv_autopilot_name(wgf_app_priv_name_kind_t kind, int code);

/* Recording, natively (wgf_app_record_native.c): when LIBWGF_AUTOPILOT_RECORD names a file
 * and no autopilot flies the run, every input is written there as an autopilot as it
 * reaches the program, frame by frame. Linked only into a program built to record (`wgf
 * autopilot --record` builds the game with it), never a shipped one: a program writing
 * keystrokes to a file is what antivirus calls a keylogger (Windows Defender quarantined
 * a test that did). The runtime reaches it through the hook install sets: started at
 * the runtime's init (true: recording, its time the autopilot's), each event as it
 * arrives for frame `frame`, the pads at each frame's start, and the file finished as
 * the program ends. */
typedef struct wgf_app_priv_recorder_t {
    bool (*start)(void);
    void (*event)(const void *event, long frame);
    void (*frame)(long frame);
    void (*stop)(long frames);
} wgf_app_priv_recorder_t;
void wgf_app_priv_set_recorder(const wgf_app_priv_recorder_t *recorder);
void wgf_app_priv_record_install(void);

#endif
