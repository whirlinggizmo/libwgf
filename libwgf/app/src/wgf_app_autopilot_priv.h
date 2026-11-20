#ifndef WGF_APP_AUTOPILOT_PRIV_H
#define WGF_APP_AUTOPILOT_PRIV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sokol_app.h"
#include "wgf_platform_gamepad_priv.h"

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

/* A real input event, the window's or an autopilot run's, as it reaches the program
 * (wgf_app.c): held back while a replay plays, recorded while one records. */
void wgf_app_priv_input_event(const sapp_event *event);

/* An autopilot parsed, whole and checked (BUILDING.md's format), to be played: what a
 * replay (wgf_replay.h) plays. Its bad lines are logged after `who`, and refused: NULL.
 * Its inputs alone are played (an autopilot's `seed`, expectations, waits, logs,
 * screenshots, and dumps are an autopilot run's, not a replay's), straight to the
 * program, never as real input; its pads are kept in it, to be read in place of the
 * real ones (wgf_platform_priv_gamepad_set_played). */
typedef struct wgf_app_priv_script_t wgf_app_priv_script_t;
wgf_app_priv_script_t *wgf_app_priv_script_parse(const char *text, const char *who);
void wgf_app_priv_script_destroy(wgf_app_priv_script_t *script);
/* Its frames: through its end, or without one through its last command's frame. */
long wgf_app_priv_script_get_length(const wgf_app_priv_script_t *script);
/* The inputs at frames up to `frame` not yet played. */
void wgf_app_priv_script_play(wgf_app_priv_script_t *script, long frame);
const wgf_platform_priv_gamepad_t *wgf_app_priv_script_get_pads(const wgf_app_priv_script_t *script);

/* Writing the window's events as an autopilot (wgf_app_record.c): the file a program built
 * to record writes, and a replay's recording. Its lines are kept in `text` as they come;
 * the inputs for frame `frame` written as they arrive (after the program has them), and
 * at each frame's start (after the pads are read) where the pointer and each finger
 * ended up, and each pad's buttons and axes, as they changed. */
typedef struct wgf_app_priv_writer_t {
    char *text;              /* NUL-terminated; NULL before the first line */
    size_t size, capacity;
    bool failed;             /* out of memory: what came after is lost, logged once */
    bool moved;              /* the pointer moved since the last frame's line */
    float mouse_x, mouse_y;  /* as last written */
    char typed[64];          /* the frame's typed characters, written together */
    size_t typed_count;
    long typed_frame;
    struct {
        bool down;
        uintptr_t id;        /* the window system's */
        bool moved;          /* since its last line */
        float x, y;
    } fingers[SAPP_MAX_TOUCHPOINTS];
    wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS]; /* as last written */
} wgf_app_priv_writer_t;
void wgf_app_priv_writer_line(wgf_app_priv_writer_t *writer, const char *format, ...);
void wgf_app_priv_writer_event(wgf_app_priv_writer_t *writer, const sapp_event *event, long frame);
void wgf_app_priv_writer_frame(wgf_app_priv_writer_t *writer, long frame);
/* The typed characters kept, written. */
void wgf_app_priv_writer_flush(wgf_app_priv_writer_t *writer);
/* Its lines let go of (written elsewhere, or done with), its state kept. */
void wgf_app_priv_writer_take(wgf_app_priv_writer_t *writer);
void wgf_app_priv_writer_free(wgf_app_priv_writer_t *writer);

/* A replay (wgf_app_replay.c), reached by the runtime through the hook its first call sets,
 * so a program that never replays links none of it: whether a real event is held back
 * from the program (true), noting a press; a real event the program had (to record it);
 * the frame's inputs, before the pads are read; the frame's input done, after; and the
 * program ending. */
typedef struct wgf_app_priv_replay_t {
    bool (*hold)(const sapp_event *event);
    void (*event)(const sapp_event *event);
    void (*inputs)(void);
    void (*frame)(void);
    void (*stop)(void);
} wgf_app_priv_replay_t;
void wgf_app_priv_set_replay(const wgf_app_priv_replay_t *replay);

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
