#ifndef WGF_PLATFORM_GAMEPAD_PRIV_H
#define WGF_PLATFORM_GAMEPAD_PRIV_H

#include <stdbool.h>

/* Gamepads: each platform reports its pads in one shape, and wgf_app_gamepad.c turns
 * that into edges, the dead zone, and the public calls. Cribbed from wgrender's
 * wgr_gamepad. */

enum {
    WGF_PLATFORM_PRIV_GAMEPADS = 4,
    WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS = 17, /* wgf_gamepad_button_t */
    WGF_PLATFORM_PRIV_GAMEPAD_NAME_SIZE = 64
};

typedef enum wgf_platform_priv_gamepad_axis_t {
    WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X = 0, /* -1 to 1, y down */
    WGF_PLATFORM_PRIV_GAMEPAD_LEFT_Y = 1,
    WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X = 2,
    WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_Y = 3,
    WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER = 4, /* 0 to 1 */
    WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER = 5,
    WGF_PLATFORM_PRIV_GAMEPAD_AXES = 6
} wgf_platform_priv_gamepad_axis_t;

/* One pad, as a platform reports it. */
typedef struct wgf_platform_priv_gamepad_t {
    bool connected;
    char name[WGF_PLATFORM_PRIV_GAMEPAD_NAME_SIZE];
    bool buttons[WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS];
    float axes[WGF_PLATFORM_PRIV_GAMEPAD_AXES];
    bool rumble; /* it has motors the platform can reach */
} wgf_platform_priv_gamepad_t;

/* The platform's side (wgf_app_gamepad_<platform>.c): start, stop, and update the
 * pads in place, slot by slot -- a pad keeps its slot while connected. Polled on the
 * main thread, once a frame. */
void wgf_platform_priv_gamepad_platform_open(void);
void wgf_platform_priv_gamepad_platform_close(void);
void wgf_platform_priv_gamepad_platform_poll(wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS]);
/* Rumble pad `pad`'s motors, `low` and `high` 0 to 1, for `seconds` (at most 2), in place of
 * what it was doing; `seconds` 0 stops it. Only for a connected pad whose `rumble` is set;
 * a platform with no timing of its own (XInput) rumbles until it is stopped, which the
 * shared code does once the time is up. */
void wgf_platform_priv_gamepad_platform_rumble(int pad, float low, float high, float seconds);

/* The loop's: started and stopped with the window; read the pads before a frame's
 * ticks; edges cleared as input's are (wgf_app_input_priv.h). */
void wgf_platform_priv_gamepad_open(void);
void wgf_platform_priv_gamepad_close(void);
void wgf_platform_priv_gamepad_begin_frame(void);
void wgf_platform_priv_gamepad_end_tick(void);
void wgf_platform_priv_gamepad_end_frame(void);

/* Pad `pad` as the program sees it, before the dead zone: what an autopilot's `pad axis`
 * lines set, so a recording of it plays back the same; NULL for no such pad. */
const wgf_platform_priv_gamepad_t *wgf_platform_priv_gamepad_get(int pad);

/* A replay's pads (wgf_replay.h), `played` (WGF_PLATFORM_PRIV_GAMEPADS of them, kept, not
 * copied, and read at each frame's start) in place of the real ones (or a test's) while
 * set; NULL gives the real ones back. The real ones are still read meanwhile: whether a
 * button of theirs went down at the last frame's start, held back from the program, is
 * wgf_platform_priv_gamepad_was_held_pressed. */
void wgf_platform_priv_gamepad_set_played(const wgf_platform_priv_gamepad_t *played);
bool wgf_platform_priv_gamepad_was_held_pressed(void);

/* Tests: from now on, pads come from here, never the platform. */
void wgf_platform_priv_gamepad_set_test_pad(int pad, const wgf_platform_priv_gamepad_t *state);
/* Tests: what test pad `pad` was last asked to rumble, in place of the platform: its low and
 * high motors and its seconds (0 for a stop) into `out` (3 floats); how many asks it has had,
 * 0 for none or a pad out of range. */
int wgf_platform_priv_gamepad_get_test_rumble(int pad, float out[3]);

#endif
