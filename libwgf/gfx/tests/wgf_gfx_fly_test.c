#include <math.h>
#include <stdio.h>
#include <string.h>

#include "render/wgf_gfx_render_priv.h"
#include "wgf_actor.h"
#include "wgf_camera2d.h"
#include "wgf_camera3d.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_fly.h"
#include "wgf_input.h"
#include "wgf_log.h"
#include "wgf_mouse.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_input_priv.h"
#include "wgf_platform_priv.h"
#include "wgf_stage3d.h"

/* Cameras flown by input, headless: the frame's input fed as the window's events (and a
 * test pad), and the part's update stepped by the test as the loop runs it, between a
 * frame's input and its edges' end. Free-fly moving where it looks and up and down, first
 * person on its plane at its height, the mouse looking with the pointer locked, the pitch
 * clamped, a diagonal no faster, the pads, a UI's capture, looking only while held, a
 * camera under a turned parent, the settings and their getters, and what is refused. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int near(float a, float b, float within)
{
    return fabsf(a - b) < within;
}

static void push_key(sapp_event_type type, sapp_keycode key)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key_code = key;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
}

static void push_mouse(sapp_event_type type, sapp_mousebutton button, float dx, float dy)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.mouse_button = button;
    event.mouse_dx = dx;
    event.mouse_dy = dy;
    wgf_platform_priv_input_handle_event(&event, 1.0f);
}

/* `n` frames of 1/60 s, as the loop runs them: the pads read, the parts updated, the
 * frame's edges ended. */
static void frames(int n)
{
    while (n-- > 0) {
        wgf_platform_priv_gamepad_begin_frame();
        wgf_platform_priv_input_set_context(WGF_PLATFORM_PRIV_INPUT_FRAME);
        wgf_core_priv_part_update(1.0f / 60.0f);
        wgf_platform_priv_input_end_frame();
        wgf_platform_priv_gamepad_end_frame();
    }
}

/* `key` held for `n` frames, then let go (and a frame for the release). */
static void hold(sapp_keycode key, int n)
{
    push_key(SAPP_EVENTTYPE_KEY_DOWN, key);
    frames(n);
    push_key(SAPP_EVENTTYPE_KEY_UP, key);
    frames(1);
}

int main(void)
{
    wgf_actor_t stage, eye, flat, plain, holder, under;
    wgf_platform_priv_gamepad_t pad;
    wgf_vec3_t at, d;
    wgf_vec2_t look;
    int i, made;

    wgf_core_priv_init();
    wgf_log_set_level(WGF_LOG_LEVEL_ERROR);
    expect(wgf_gfx_priv_start(), "setup");
    wgf_platform_priv_set_size(800, 600);
    wgf_platform_priv_input_reset();

    stage = wgf_stage3d_create();
    eye = wgf_camera3d_create();
    wgf_actor_set_parent(eye, stage);
    wgf_actor_set_position(eye, 0, 2, 0);

    /* the settings, before it flies, and their getters */
    expect(wgf_fly_get_mode(eye) == WGF_FLY_MODE_NONE && near(wgf_fly_get_speed(eye), 5.4f, 1e-6f) &&
               near(wgf_fly_get_sensitivity(eye), 0.003f, 1e-7f) && near(wgf_fly_get_turn_speed(eye), 2, 1e-6f) &&
               !wgf_fly_is_look_held(eye),
           "the defaults: not flown, raylib's speed and sensitivity");
    look = wgf_fly_get_look(eye);
    expect(near(look.x, 0, 1e-5f) && near(look.y, 0, 1e-5f), "its look where it faces: -z, level");

    /* free-fly: forward where it looks */
    expect(wgf_fly_set_mode(eye, WGF_FLY_MODE_FREE) && wgf_fly_get_mode(eye) == WGF_FLY_MODE_FREE, "flying free");
    frames(1);
    expect(wgf_mouse_is_locked(), "the pointer locked while it looks");
    hold(SAPP_KEYCODE_W, 60);
    at = wgf_actor_get_position(eye);
    expect(near(at.x, 0, 1e-3f) && near(at.y, 2, 1e-3f) && near(at.z, -5.4f, 1e-2f), "W: 5.4 forward in a second");
    hold(SAPP_KEYCODE_SPACE, 30);
    expect(near(wgf_actor_get_position(eye).y, 2 + 2.7f, 1e-2f), "Space: up");
    hold(SAPP_KEYCODE_LEFT_CONTROL, 30);
    expect(near(wgf_actor_get_position(eye).y, 2, 1e-2f), "Left Control: down");
    push_key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_W);
    push_key(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_D);
    at = wgf_actor_get_position(eye);
    frames(60);
    push_key(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_W);
    push_key(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_D);
    frames(1);
    d = wgf_vec3_sub(wgf_actor_get_position(eye), at);
    expect(near(wgf_vec3_length(d), 5.4f, 1e-2f) && near(d.x, -d.z, 1e-3f) && d.x > 0, "W and D: forward right, no faster");

    /* the mouse looks: right turns right, up looks up, the pitch clamped */
    wgf_fly_set_look(eye, 0, 0);
    push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 100, 0);
    frames(1);
    look = wgf_fly_get_look(eye);
    d = wgf_actor_get_world_direction(eye, 0, 0, -1);
    expect(near(look.x, -0.3f, 1e-5f) && near(look.y, 0, 1e-6f) && near(d.x, sinf(0.3f), 1e-4f) &&
               near(d.z, -cosf(0.3f), 1e-4f),
           "100 pixels right: turned 0.3 right");
    push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 0, -100);
    frames(1);
    expect(near(wgf_fly_get_look(eye).y, 0.3f, 1e-5f) && wgf_actor_get_world_direction(eye, 0, 0, -1).y > 0.29f,
           "100 pixels up: looking 0.3 up");
    push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 0, -10000);
    frames(1);
    expect(near(wgf_fly_get_look(eye).y, 1.5533430f, 1e-5f), "far up: held short of straight up");
    push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 0, 20000);
    frames(1);
    expect(near(wgf_fly_get_look(eye).y, -1.5533430f, 1e-5f), "far down: short of straight down");
    expect(wgf_fly_set_look(eye, 0, 0.5f), "looking 0.5 up");
    at = wgf_actor_get_position(eye);
    hold(SAPP_KEYCODE_UP, 60);
    d = wgf_vec3_sub(wgf_actor_get_position(eye), at);
    expect(near(d.y, 5.4f * sinf(0.5f), 1e-2f) && near(d.z, -5.4f * cosf(0.5f), 1e-2f), "Up: forward where it looks, rising");
    expect(wgf_fly_set_look(eye, 7.0f, 4.0f) && near(wgf_fly_get_look(eye).x, 7.0f - 6.2831853f, 1e-4f) &&
               near(wgf_fly_get_look(eye).y, 1.5533430f, 1e-5f),
           "a look set: the yaw wrapped, the pitch clamped");

    /* first person: on its plane, its height kept */
    wgf_actor_set_position(eye, 0, 1.7f, 0);
    expect(wgf_fly_set_mode(eye, WGF_FLY_MODE_FIRST_PERSON) && wgf_fly_set_look(eye, 0, -0.8f), "first person, looking down");
    hold(SAPP_KEYCODE_W, 60);
    at = wgf_actor_get_position(eye);
    expect(at.y == 1.7f && near(at.z, -5.4f, 1e-2f), "W: 5.4 along the ground, at its height");
    hold(SAPP_KEYCODE_SPACE, 30);
    hold(SAPP_KEYCODE_E, 30);
    expect(wgf_actor_get_position(eye).y == 1.7f, "no up or down");
    wgf_actor_set_position(eye, 0, 3.0f, 0); /* the program moves it: a stair */
    hold(SAPP_KEYCODE_A, 30);
    at = wgf_actor_get_position(eye);
    expect(at.y == 3.0f && near(at.x, -2.7f, 1e-2f), "A: left, at the height the program set");

    /* the pads: the left stick moves, the right stick looks */
    memset(&pad, 0, sizeof(pad));
    pad.connected = true;
    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_Y] = -1.0f; /* pushed up */
    wgf_platform_priv_gamepad_set_test_pad(2, &pad);
    wgf_fly_set_look(eye, 0, 0);
    at = wgf_actor_get_position(eye);
    frames(60);
    expect(near(wgf_actor_get_position(eye).z - at.z, -5.4f, 1e-2f), "a pad's left stick: forward");
    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_Y] = 0.0f;
    pad.axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X] = 1.0f;
    wgf_platform_priv_gamepad_set_test_pad(2, &pad);
    expect(wgf_fly_set_turn_speed(eye, 1.5f) && near(wgf_fly_get_turn_speed(eye), 1.5f, 1e-6f), "a turn speed");
    frames(30);
    expect(near(wgf_fly_get_look(eye).x, -0.75f, 1e-4f), "its right stick: turned right at the turn speed");
    wgf_platform_priv_gamepad_set_test_pad(2, NULL);

    /* a UI's: the keyboard captured leaves the keys alone, the pointer captured the mouse */
    wgf_fly_set_look(eye, 0, 0);
    at = wgf_actor_get_position(eye);
    wgf_input_set_keyboard_captured(true);
    hold(SAPP_KEYCODE_W, 30);
    expect(wgf_vec3_length(wgf_vec3_sub(wgf_actor_get_position(eye), at)) < 1e-5f, "the keyboard captured: still");
    wgf_input_set_keyboard_captured(false);
    wgf_input_set_pointer_captured(true);
    push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 100, 0);
    frames(1);
    expect(near(wgf_fly_get_look(eye).x, 0, 1e-6f) && !wgf_mouse_is_locked(), "the pointer captured: not looking, let go");
    wgf_input_set_pointer_captured(false);
    frames(1);
    expect(wgf_mouse_is_locked(), "and locked again");

    /* looking only while the right button is held */
    expect(wgf_fly_set_look_held(eye, true) && wgf_fly_is_look_held(eye), "looking while held");
    push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 100, 0);
    frames(1);
    expect(near(wgf_fly_get_look(eye).x, 0, 1e-6f) && !wgf_mouse_is_locked(), "not held: not looking, the pointer free");
    push_mouse(SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_MOUSEBUTTON_RIGHT, 0, 0);
    frames(1);
    expect(wgf_mouse_is_locked(), "held: locked");
    push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 100, 0);
    frames(1);
    expect(near(wgf_fly_get_look(eye).x, -0.3f, 1e-5f), "held: looking");
    push_mouse(SAPP_EVENTTYPE_MOUSE_UP, SAPP_MOUSEBUTTON_RIGHT, 0, 0);
    frames(1);
    expect(!wgf_mouse_is_locked(), "let go: the pointer free");
    wgf_fly_set_look_held(eye, false);
    expect(wgf_fly_set_sensitivity(eye, 0) && wgf_fly_get_sensitivity(eye) == 0.0f, "a sensitivity of 0");
    push_mouse(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_MOUSEBUTTON_INVALID, 100, 0);
    frames(1);
    expect(near(wgf_fly_get_look(eye).x, -0.3f, 1e-5f), "0: the mouse doesn't look");

    /* stopped: staying put, the pointer let go, its settings kept */
    at = wgf_actor_get_position(eye);
    expect(wgf_fly_set_mode(eye, WGF_FLY_MODE_NONE) && wgf_fly_get_mode(eye) == WGF_FLY_MODE_NONE, "stopped");
    hold(SAPP_KEYCODE_W, 30);
    expect(wgf_vec3_length(wgf_vec3_sub(wgf_actor_get_position(eye), at)) < 1e-5f && !wgf_mouse_is_locked() &&
               near(wgf_fly_get_turn_speed(eye), 1.5f, 1e-6f),
           "staying put, the pointer free, its settings kept");
    expect(wgf_fly_set_speed(eye, 2) && near(wgf_fly_get_speed(eye), 2, 1e-6f), "a speed");
    wgf_actor_look_at(eye, at.x + 1, at.y, at.z, 0, 1, 0); /* the program turns it: facing +x */
    expect(wgf_fly_set_mode(eye, WGF_FLY_MODE_FREE) && near(wgf_fly_get_look(eye).x, -1.5707963f, 1e-4f),
           "flying again: its look taken from where it faces");
    at = wgf_actor_get_position(eye);
    hold(SAPP_KEYCODE_W, 60);
    expect(near(wgf_actor_get_position(eye).x - at.x, 2, 1e-2f), "at its own speed");
    wgf_fly_set_mode(eye, WGF_FLY_MODE_NONE);

    /* under a turned parent: moved in the world */
    holder = wgf_actor_create();
    under = wgf_camera3d_create();
    wgf_actor_set_parent(holder, stage);
    wgf_actor_set_parent(under, holder);
    wgf_actor_set_position(holder, 10, 0, 0);
    wgf_actor_set_rotation(holder, 0, 1.5707963f, 0); /* facing -x */
    wgf_actor_set_position(under, 0, 2, 0);
    expect(wgf_fly_set_mode(under, WGF_FLY_MODE_FIRST_PERSON) && near(wgf_fly_get_look(under).x, 1.5707963f, 1e-4f),
           "a camera under a turned parent, facing -x");
    hold(SAPP_KEYCODE_W, 60);
    at = wgf_actor_get_world_position(under);
    expect(near(at.x, 10 - 5.4f, 1e-2f) && near(at.y, 2, 1e-4f) && near(at.z, 0, 1e-3f), "moved along -x in the world");
    wgf_fly_set_mode(under, WGF_FLY_MODE_NONE);
    frames(1);

    /* what is refused */
    flat = wgf_camera2d_create();
    plain = wgf_actor_create();
    expect(!wgf_fly_set_mode(flat, WGF_FLY_MODE_FREE) && !wgf_fly_set_mode(plain, WGF_FLY_MODE_FREE) &&
               !wgf_fly_set_mode(0, WGF_FLY_MODE_FREE) && !wgf_fly_set_speed(flat, 1) && !wgf_fly_set_look_held(plain, true),
           "only a 3D camera flies");
    expect(!wgf_fly_set_mode(eye, (wgf_fly_mode_t)3) && !wgf_fly_set_mode(eye, (wgf_fly_mode_t)-1), "a mode that isn't one");
    expect(!wgf_fly_set_speed(eye, -1) && !wgf_fly_set_speed(eye, NAN) && !wgf_fly_set_sensitivity(eye, -0.1f) &&
               !wgf_fly_set_turn_speed(eye, INFINITY) && !wgf_fly_set_look(eye, NAN, 0) &&
               !wgf_fly_set_look(eye, 0, INFINITY) && near(wgf_fly_get_speed(eye), 2, 1e-6f),
           "a negative or not finite setting, the one before kept");
    expect(wgf_fly_get_speed(flat) == 0.0f && wgf_fly_get_mode(plain) == WGF_FLY_MODE_NONE && wgf_fly_get_look(0).x == 0.0f,
           "getters of what isn't a 3D camera: 0");
    made = 0;
    {
        wgf_actor_t cameras[64];
        for (i = 0; i < 64; i++) {
            cameras[i] = wgf_camera3d_create();
            made += wgf_fly_set_speed(cameras[i], 1) ? 1 : 0;
        }
        expect(made == 62, "64 cameras with a setting (two had one)");
        wgf_actor_destroy(cameras[0], WGF_ACTOR_DESTROY_CHILDREN);
        expect(wgf_fly_set_speed(cameras[63], 1) && wgf_fly_get_speed(cameras[0]) == 0.0f &&
                   !wgf_fly_set_mode(cameras[0], WGF_FLY_MODE_FREE),
               "a destroyed camera's refused, its place taken");
        for (i = 1; i < 64; i++) wgf_actor_destroy(cameras[i], WGF_ACTOR_DESTROY_CHILDREN);
    }
    wgf_actor_destroy(eye, WGF_ACTOR_DESTROY_CHILDREN);
    expect(!wgf_fly_set_mode(eye, WGF_FLY_MODE_FREE) && wgf_fly_get_speed(eye) == 0.0f, "a destroyed camera: refused, 0");
    frames(1);

    wgf_actor_destroy(flat, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(plain, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_actor_destroy(stage, WGF_ACTOR_DESTROY_CHILDREN);
    wgf_gfx_priv_stop();
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
