#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_gamepad.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_mouse.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_replay.h"
#include "wgf_touch.h"

/* Replays, headless, through wgf_app_run as a program runs: a run played by events as a
 * window delivers them, recorded by the program, then played back in another run, the input
 * calls reading the same thing frame by frame; real input held back while a replay plays,
 * and noticed; pause and resume; the end; and what is refused: malformed text, playing
 * while recording, recording while playing. Nothing is written to a file: a recording is
 * the program's text. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

#define FRAMES 20       /* frames compared */
#define STOP_AT 17      /* the frame whose callback stops the recording: 18 frames recorded */

/* What the input calls read in a frame. */
typedef struct seen_t {
    int space_down, space_pressed, space_released, typed;
    float mouse_x, mouse_y, wheel_y;
    int left_down, left_pressed;
    int touches;
    float touch_x, touch_y;
    int pad_connected, pad_south;
    float pad_x;
} seen_t;

typedef struct run_t {
    int frames;
    seen_t seen[FRAMES + 8];
    int a_down;             /* frames a real A read down while the replay played */
    int real_press_frame;   /* the first frame a real press was noticed; -1 for none */
    int complete_frame;     /* the first frame the replay read COMPLETE; -1 for none */
    int frame_at_complete, count_at_complete;
} run_t;

static char recording[8192];
static int recorded_frames;

static void look(run_t *run)
{
    seen_t *seen = &run->seen[run->frames];
    seen->space_down = wgf_keyboard_is_down(WGF_KEY_SPACE);
    seen->space_pressed = wgf_keyboard_is_pressed(WGF_KEY_SPACE);
    seen->space_released = wgf_keyboard_is_released(WGF_KEY_SPACE);
    seen->typed = (unsigned char)wgf_input_get_chars()[0];
    seen->mouse_x = wgf_mouse_get_position().x;
    seen->mouse_y = wgf_mouse_get_position().y;
    seen->wheel_y = wgf_mouse_get_wheel().y;
    seen->left_down = wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT);
    seen->left_pressed = wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT);
    seen->touches = wgf_touch_get_count();
    if (seen->touches > 0) {
        seen->touch_x = wgf_touch_get_position(wgf_touch_get_id(0)).x;
        seen->touch_y = wgf_touch_get_position(wgf_touch_get_id(0)).y;
    }
    seen->pad_connected = wgf_gamepad_is_connected(1);
    seen->pad_south = wgf_gamepad_is_down(1, WGF_GAMEPAD_BUTTON_SOUTH);
    seen->pad_x = wgf_gamepad_get_axis(1, WGF_GAMEPAD_AXIS_LEFT_X);
}

static void push_key(sapp_event_type type, sapp_keycode key)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key_code = key;
    wgf_platform_priv_headless_push_event(&event);
}

static void push_mouse(sapp_event_type type, float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.mouse_button = SAPP_MOUSEBUTTON_LEFT;
    event.mouse_x = x;
    event.mouse_y = y;
    if (type == SAPP_EVENTTYPE_MOUSE_SCROLL) event.scroll_y = 3.0f;
    wgf_platform_priv_headless_push_event(&event);
}

static void push_touch(sapp_event_type type, float x, float y)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.num_touches = 1;
    event.touches[0].identifier = 1234;
    event.touches[0].pos_x = x;
    event.touches[0].pos_y = y;
    event.touches[0].changed = true;
    wgf_platform_priv_headless_push_event(&event);
}

static wgf_platform_priv_gamepad_t pad;

/* Played by hand, as a window would deliver it: what is pushed in one frame reaches the next. */
static void played_frame(void *user)
{
    run_t *run = user;
    sapp_event event;
    look(run);
    switch (run->frames) {
        case 1: push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, 20.0f, 40.0f); break;
        case 2: push_key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_SPACE); break;
        case 3:
            memset(&event, 0, sizeof(event));
            event.type = SAPP_EVENTTYPE_CHAR;
            event.char_code = 'Q';
            wgf_platform_priv_headless_push_event(&event);
            break;
        case 4: push_key(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_SPACE); break;
        case 5: push_mouse(SAPP_EVENTTYPE_MOUSE_DOWN, 20.0f, 40.0f); break;
        case 6:
            push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, 30.0f, 44.0f);
            push_mouse(SAPP_EVENTTYPE_MOUSE_UP, 30.0f, 44.0f);
            break;
        case 7: push_mouse(SAPP_EVENTTYPE_MOUSE_SCROLL, 30.0f, 44.0f); break;
        case 8: push_touch(SAPP_EVENTTYPE_TOUCHES_BEGAN, 60.0f, 80.0f); break;
        case 9:
            push_touch(SAPP_EVENTTYPE_TOUCHES_MOVED, 64.0f, 82.0f);
            push_touch(SAPP_EVENTTYPE_TOUCHES_MOVED, 70.0f, 90.0f);
            break;
        case 10: push_touch(SAPP_EVENTTYPE_TOUCHES_ENDED, 70.0f, 90.0f); break;
        case 11:
            pad.connected = true;
            wgf_platform_priv_gamepad_set_test_pad(1, &pad);
            break;
        case 12:
            pad.buttons[WGF_GAMEPAD_BUTTON_SOUTH] = true;
            pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X] = 0.5f;
            wgf_platform_priv_gamepad_set_test_pad(1, &pad);
            break;
        case 14:
            pad.buttons[WGF_GAMEPAD_BUTTON_SOUTH] = false;
            pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X] = 0.0f;
            wgf_platform_priv_gamepad_set_test_pad(1, &pad);
            push_key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_SPACE); /* held through the recording's end */
            break;
        default: break;
    }
    if (run->frames == STOP_AT) {
        expect(wgf_replay_is_recording() && wgf_replay_get_recording_frame_count() == STOP_AT + 1,
               "recording: a frame each frame since it started");
        expect(wgf_replay_stop_recording() && !wgf_replay_is_recording() && !wgf_replay_stop_recording(),
               "stopped once");
        snprintf(recording, sizeof(recording), "%s", wgf_replay_get_recording());
        recorded_frames = wgf_replay_get_recording_frame_count();
    }
    run->frames++;
    if (run->frames == FRAMES + 2) wgf_app_quit();
}

static void played_init(void *user)
{
    (void)user;
    expect(wgf_replay_start_recording() && wgf_replay_is_recording(), "recording from the first frame");
    expect(!wgf_replay_start_recording(), "a second start refused while recording");
    expect(!wgf_replay_play("wgf-autopilot 1\nat 0 key down space\n") &&
               wgf_replay_get_state() == WGF_PLAY_STATE_STOPPED,
           "playing refused while recording");
}

/* Played back: the replay's input, with real input pushed meanwhile, held back. */
static void replayed_frame(void *user)
{
    run_t *run = user;
    const wgf_play_state_t state = wgf_replay_get_state();
    look(run);
    if (state == WGF_PLAY_STATE_PLAYING && wgf_keyboard_is_down(WGF_KEY_A)) run->a_down++;
    if (wgf_replay_has_real_press() && run->real_press_frame < 0) run->real_press_frame = run->frames;
    if (state == WGF_PLAY_STATE_COMPLETE && run->complete_frame < 0) {
        run->complete_frame = run->frames;
        run->frame_at_complete = wgf_replay_get_frame();
        run->count_at_complete = wgf_replay_get_frame_count();
    }
    if (run->frames == 5) expect(!wgf_replay_start_recording(), "recording refused while a replay plays");
    if (run->frames == 9) push_key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_A); /* real, held back */
    if (run->frames == 12) push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, 500.0f, 500.0f);
    run->frames++;
    if (run->frames == FRAMES + 2) wgf_app_quit();
}

static void replayed_init(void *user)
{
    (void)user;
    expect(wgf_replay_play(recording) && wgf_replay_get_state() == WGF_PLAY_STATE_PLAYING &&
               wgf_replay_get_frame() == 0 && wgf_replay_get_frame_count() == recorded_frames,
           "the recording plays from the next frame, its length the frames recorded");
}

/* Paused: the frame held, what it holds held. */
typedef struct paused_run_t {
    int frames;
    int down_while_paused, frame_while_paused;
    int complete_frame;
} paused_run_t;

static void paused_frame(void *user)
{
    paused_run_t *run = user;
    if (run->frames == 3) expect(wgf_replay_pause() && !wgf_replay_pause(), "paused once");
    if (run->frames > 3 && run->frames < 8 && wgf_replay_get_state() == WGF_PLAY_STATE_PAUSED) {
        run->down_while_paused += wgf_keyboard_is_down(WGF_KEY_SPACE);
        run->frame_while_paused = wgf_replay_get_frame();
    }
    if (run->frames == 7) expect(wgf_replay_resume() && !wgf_replay_resume(), "resumed once");
    if (wgf_replay_get_state() == WGF_PLAY_STATE_COMPLETE && run->complete_frame < 0) {
        run->complete_frame = run->frames;
        expect(!wgf_keyboard_is_down(WGF_KEY_SPACE) && wgf_keyboard_is_released(WGF_KEY_SPACE) &&
                   wgf_touch_get_count() == 1 && wgf_touch_get_state(wgf_touch_get_id(0)) == WGF_INPUT_STATE_RELEASED,
               "complete: what it held let go of, released that frame");
    }
    run->frames++;
    if (run->frames == 20) {
        expect(wgf_replay_stop() && wgf_replay_get_state() == WGF_PLAY_STATE_STOPPED && !wgf_replay_stop() &&
                   wgf_replay_get_frame_count() == 0,
               "stopped: let go of");
        wgf_app_quit();
    }
}

static void paused_init(void *user)
{
    (void)user;
    /* an autopilot's own lines: a tap, a finger, a pad; its expectations and logs passed over */
    expect(wgf_replay_play("wgf-autopilot 1\nseed 7\n"
                           "at 1 key down space\n"
                           "at 1 touch down 0 5 6\n"
                           "at 2 expect nothing == 1   # an autopilot run's, not a replay's\n"
                           "at 2 log hello\n"
                           "at 3 pad 0 connect\n"
                           "at 5 end\n"),
           "an autopilot plays");
    expect(wgf_replay_get_frame_count() == 6, "its length: through its end");
}

int main(void)
{
    run_t played, replayed;
    paused_run_t paused;
    int i, same = 1;

    wgf_platform_priv_headless_set_dpi_scale(2.0f); /* the window's pixels are two of a logical one */

    expect(!wgf_replay_play(NULL) && !wgf_replay_play("") && !wgf_replay_play("not an autopilot\n") &&
               !wgf_replay_play("wgf-autopilot 1\nat x key down space\n") &&
               !wgf_replay_play("wgf-autopilot 1\nat 1 touch down 9 1 1\n") &&
               !wgf_replay_play("wgf-autopilot 1\nat 1 touch press 0 1 1\n") &&
               !wgf_replay_play("wgf-autopilot 1\nat 1 key down nokey\n") &&
               wgf_replay_get_state() == WGF_PLAY_STATE_STOPPED,
           "malformed text refused, nothing playing");
    expect(!wgf_replay_pause() && !wgf_replay_resume() && !wgf_replay_stop() && wgf_replay_get_frame() == 0 &&
               !wgf_replay_has_real_press() && wgf_replay_get_recording()[0] == '\0' &&
               wgf_replay_get_recording_frame_count() == 0 && !wgf_replay_stop_recording(),
           "nothing to pause, resume, stop, or read before anything plays or records");

    memset(&played, 0, sizeof(played));
    wgf_app_run(played_init, NULL, played_frame, NULL, &played);
    wgf_platform_priv_gamepad_set_test_pad(1, NULL); /* the real pad gone: the replay's alone */
    expect(recorded_frames == STOP_AT + 1 && strstr(recording, "wgf-autopilot 1\n") == recording &&
               strstr(recording, "\nat 3 key down space\n") != NULL && strstr(recording, "\nat 4 text Q\n") != NULL &&
               strstr(recording, "\nat 9 touch down 0 30 40\n") != NULL &&
               strstr(recording, "\nat 10 touch move 0 35 45\n") != NULL &&
               strstr(recording, "\nat 13 pad 1 down south\n") != NULL &&
               strstr(recording, "\nat 17 end\n") != NULL,
           "recorded as an autopilot: each input at the frame it reached, a finger's moves once a frame, the end");

    memset(&replayed, 0, sizeof(replayed));
    replayed.real_press_frame = replayed.complete_frame = -1;
    wgf_app_run(replayed_init, NULL, replayed_frame, NULL, &replayed);
    for (i = 0; i < recorded_frames; i++) {
        if (memcmp(&played.seen[i], &replayed.seen[i], sizeof(seen_t)) != 0) {
            printf("frame %d differs: space %d/%d typed %d/%d mouse %g,%g/%g,%g touches %d/%d pad %d/%d\n", i,
                   played.seen[i].space_down, replayed.seen[i].space_down, played.seen[i].typed,
                   replayed.seen[i].typed, played.seen[i].mouse_x, played.seen[i].mouse_y, replayed.seen[i].mouse_x,
                   replayed.seen[i].mouse_y, played.seen[i].touches, replayed.seen[i].touches,
                   played.seen[i].pad_south, replayed.seen[i].pad_south);
            same = 0;
        }
    }
    expect(same, "played back: every input call reads what it read as recorded, frame by frame");
    expect(played.seen[5].space_released && played.seen[13].pad_south && played.seen[9].touches == 1 &&
               fabsf(played.seen[3].mouse_x - 10.0f) < 0.001f && played.seen[8].wheel_y != 0.0f,
           "what was compared was there: keys, the pad, a finger, the pointer, the wheel");
    expect(replayed.a_down == 0 && replayed.real_press_frame == 10,
           "real input held back while it plays, its press noticed");
    expect(replayed.complete_frame == recorded_frames && replayed.frame_at_complete == recorded_frames &&
               replayed.count_at_complete == recorded_frames,
           "complete after its last frame");
    expect(replayed.seen[recorded_frames].space_released && !replayed.seen[recorded_frames].space_down,
           "complete: the key it held let go of");
    expect(replayed.seen[recorded_frames].mouse_x != 250.0f, "a real move held back is not caught up later");
    expect(wgf_replay_get_state() == WGF_PLAY_STATE_STOPPED && wgf_replay_get_recording()[0] == '\0',
           "the program's end lets go of the replay and the recording");

    memset(&paused, 0, sizeof(paused));
    paused.complete_frame = -1;
    wgf_app_run(paused_init, NULL, paused_frame, NULL, &paused);
    expect(paused.down_while_paused == 4 && paused.frame_while_paused == 4, "paused: its frame and its keys held");
    expect(paused.complete_frame == 6 + 4, "resumed: complete four frames later than it would have been");
    return failures == 0 ? 0 : 1;
}
