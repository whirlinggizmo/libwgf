#include "wgf_replay.h"

#include <string.h>

#include "wgf_app_autopilot_priv.h"
#include "wgf_log.h"
#include "wgf_mouse.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_input_priv.h"

/* Replays (wgf_replay.h): a recording written as the autopilot recorder writes a file
 * (wgf_app_record.c), into memory, and an autopilot's inputs played by the autopilot's own
 * parser and player (wgf_app_autopilot.c), straight to the program, with real input held
 * back meanwhile. The runtime reaches it through the hook its first call sets. */

static struct {
    bool recording;
    wgf_app_priv_writer_t writer;
    long recorded; /* frames whose input is recorded */
    wgf_play_state_t state;
    wgf_app_priv_script_t *script; /* the replay, kept until stopped or replaced */
    long played, length;
    bool pending_press; /* a real press held back, for the next frame */
    bool real_press;    /* for this frame */
} replay;

static bool holding(void)
{
    return replay.state == WGF_PLAY_STATE_PLAYING || replay.state == WGF_PLAY_STATE_PAUSED;
}

/* ---- the runtime's hooks --------------------------------------------------- */

static bool hold(const sapp_event *event)
{
    if (!holding()) return false;
    switch (event->type) {
        case SAPP_EVENTTYPE_KEY_DOWN:
            if (!event->key_repeat) replay.pending_press = true;
            return true;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
        case SAPP_EVENTTYPE_TOUCHES_BEGAN: replay.pending_press = true; return true;
        case SAPP_EVENTTYPE_KEY_UP:
        case SAPP_EVENTTYPE_CHAR:
        case SAPP_EVENTTYPE_MOUSE_UP:
        case SAPP_EVENTTYPE_MOUSE_MOVE:
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
        case SAPP_EVENTTYPE_TOUCHES_MOVED:
        case SAPP_EVENTTYPE_TOUCHES_ENDED:
        case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
        case SAPP_EVENTTYPE_UNFOCUSED: /* which lets go of the keys held: the replay's */
            return true;
        default: return false;
    }
}

static void event_reached(const sapp_event *event)
{
    if (replay.recording) wgf_app_priv_writer_event(&replay.writer, event, replay.recorded);
}

/* Let go of what plays, its input with it: everything held released, the real pads back. */
static void let_go(wgf_play_state_t state)
{
    if (holding()) {
        wgf_platform_priv_input_release_all();
        wgf_platform_priv_gamepad_set_played(NULL);
    }
    replay.state = state;
    if (state == WGF_PLAY_STATE_STOPPED) {
        wgf_app_priv_script_destroy(replay.script);
        replay.script = NULL;
        replay.played = replay.length = 0;
    }
}

static void frame_inputs(void)
{
    replay.real_press = replay.pending_press;
    replay.pending_press = false;
    if (replay.state != WGF_PLAY_STATE_PLAYING) return;
    if (replay.played >= replay.length) {
        let_go(WGF_PLAY_STATE_COMPLETE);
        return;
    }
    wgf_app_priv_script_play(replay.script, replay.played);
}

static void frame_read(void)
{
    if (replay.recording) wgf_app_priv_writer_frame(&replay.writer, replay.recorded++);
    if (replay.state == WGF_PLAY_STATE_PLAYING) replay.played++;
    if (holding() && wgf_platform_priv_gamepad_was_held_pressed()) replay.real_press = true;
}

static void program_ended(void)
{
    let_go(WGF_PLAY_STATE_STOPPED);
    wgf_app_priv_writer_free(&replay.writer);
    memset(&replay, 0, sizeof(replay));
}

static void install(void)
{
    static const wgf_app_priv_replay_t hooks = {hold, event_reached, frame_inputs, frame_read, program_ended};
    wgf_app_priv_set_replay(&hooks);
}

/* ---- recording ------------------------------------------------------------- */

bool wgf_replay_start_recording(void)
{
    wgf_vec2_t at;
    install();
    if (replay.recording || holding()) return false;
    wgf_app_priv_writer_free(&replay.writer);
    replay.recording = true;
    replay.recorded = 0;
    at = wgf_mouse_get_position();
    wgf_app_priv_writer_line(&replay.writer,
                             "wgf-autopilot 1\n# recorded by the program (wgf_replay.h): every input as it reached it,"
                             " frame by frame\nat 0 mouse move %g %g\n",
                             at.x, at.y);
    replay.writer.mouse_x = at.x;
    replay.writer.mouse_y = at.y;
    return true;
}

bool wgf_replay_stop_recording(void)
{
    if (!replay.recording) return false;
    wgf_app_priv_writer_flush(&replay.writer);
    if (replay.recorded > 0) wgf_app_priv_writer_line(&replay.writer, "at %ld end\n", replay.recorded - 1);
    replay.recording = false;
    return true;
}

bool wgf_replay_is_recording(void)
{
    return replay.recording;
}

const char *wgf_replay_get_recording(void)
{
    return replay.writer.text != NULL ? replay.writer.text : "";
}

int wgf_replay_get_recording_frame_count(void)
{
    return replay.writer.text != NULL ? (int)replay.recorded : 0;
}

void wgf_replay_clear_recording(void)
{
    replay.recording = false;
    replay.recorded = 0;
    wgf_app_priv_writer_free(&replay.writer);
}

/* ---- playing --------------------------------------------------------------- */

bool wgf_replay_play(const char *text)
{
    wgf_app_priv_script_t *script;
    install();
    if (replay.recording) {
        wgf_log_warn("wgf_replay_play: recording: a replay plays once the recording stops");
        return false;
    }
    script = wgf_app_priv_script_parse(text, "wgf_replay_play");
    if (script == NULL) return false;
    let_go(WGF_PLAY_STATE_STOPPED);
    wgf_platform_priv_input_release_all();
    replay.script = script;
    replay.played = 0;
    replay.length = wgf_app_priv_script_get_length(script);
    replay.state = WGF_PLAY_STATE_PLAYING;
    replay.pending_press = false;
    wgf_platform_priv_gamepad_set_played(wgf_app_priv_script_get_pads(script));
    return true;
}

bool wgf_replay_pause(void)
{
    if (replay.state != WGF_PLAY_STATE_PLAYING) return false;
    replay.state = WGF_PLAY_STATE_PAUSED;
    return true;
}

bool wgf_replay_resume(void)
{
    if (replay.state != WGF_PLAY_STATE_PAUSED) return false;
    replay.state = WGF_PLAY_STATE_PLAYING;
    return true;
}

bool wgf_replay_stop(void)
{
    if (replay.state == WGF_PLAY_STATE_STOPPED) return false;
    let_go(WGF_PLAY_STATE_STOPPED);
    return true;
}

wgf_play_state_t wgf_replay_get_state(void)
{
    return replay.state;
}

int wgf_replay_get_frame(void)
{
    return (int)replay.played;
}

int wgf_replay_get_frame_count(void)
{
    return (int)replay.length;
}

bool wgf_replay_has_real_press(void)
{
    return replay.real_press;
}
