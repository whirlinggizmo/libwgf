/* Cameras flown by input (wgf_fly.h): raylib's CAMERA_FREE and CAMERA_FIRST_PERSON, a table
 * of up to 64 3D cameras each with its mode, look, and settings, stepped once a frame by a
 * part (installed by the first camera given a mode or a setting) after the ticks and before
 * the frame callback, reading the frame's input through platform's public calls. */
#include "wgf_fly.h"

#include <math.h>
#include <string.h>

#include "actor/wgf_gfx_actor_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_gamepad.h"
#include "wgf_handle.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_mouse.h"
#include "wgf_trig.h"

#define MAX_FLIES 64
#define PADS 4
#define MOST_PITCH 1.5533430f /* 89 degrees */
#define PI 3.14159265f

typedef struct fly_t {
    wgf_actor_t camera;
    int mode;
    float speed, sensitivity, turn_speed;
    float yaw, pitch;
    bool held;
} fly_t;

static struct {
    bool installed;
    bool lock_asked; /* the pointer's lock asked for by a flown camera, not yet let go */
    fly_t flies[MAX_FLIES];
} fy;

static float clamp_pitch(float pitch)
{
    return pitch < -MOST_PITCH ? -MOST_PITCH : pitch > MOST_PITCH ? MOST_PITCH : pitch;
}

static float wrap_yaw(float yaw)
{
    while (yaw > PI) yaw -= 2.0f * PI;
    while (yaw <= -PI) yaw += 2.0f * PI;
    return yaw;
}

/* Where `camera` faces now, as a yaw and a pitch. */
static void facing(wgf_actor_t camera, float *yaw, float *pitch)
{
    const wgf_vec3_t d = wgf_actor_get_world_direction(camera, 0.0f, 0.0f, -1.0f);
    const float y = d.y < -1.0f ? -1.0f : d.y > 1.0f ? 1.0f : d.y;
    *yaw = d.x * d.x + d.z * d.z > 1e-12f ? wgf_trig_atan2(-d.x, -d.z) : 0.0f;
    *pitch = clamp_pitch(wgf_trig_asin(y));
}

/* The camera turned to its yaw and pitch, from where it is. */
static void turn(const fly_t *f)
{
    const wgf_vec3_t at = wgf_actor_get_world_position(f->camera);
    const wgf_vec2_t yaw = wgf_trig_cos_sin(f->yaw), pitch = wgf_trig_cos_sin(f->pitch);
    wgf_actor_look_at(f->camera, at.x - yaw.y * pitch.x, at.y + pitch.y, at.z - yaw.x * pitch.x, 0.0f, 1.0f, 0.0f);
}

/* The camera put at `world` (its tree's root's space) under whatever its parent is. */
static void place(wgf_actor_t camera, float x, float y, float z)
{
    const wgf_actor_t parent = wgf_actor_get_parent(camera);
    wgf_vec3_t at = wgf_vec3_make(x, y, z);
    if (parent != 0 && wgf_actor_get_kind(parent) != WGF_ACTOR_KIND_STAGE3D) {
        at = wgf_mat4_transform_point(wgf_mat4_invert(wgf_gfx_priv_actor_get_world_matrix(parent)), at);
    }
    wgf_actor_set_position(camera, at.x, at.y, at.z);
}

/* -1, 0, or 1 from a pair of keys, each with a second key alike. */
static float keys(wgf_keyboard_key_t negative, wgf_keyboard_key_t negative2, wgf_keyboard_key_t positive,
                  wgf_keyboard_key_t positive2)
{
    float value = 0.0f;
    if (wgf_keyboard_is_down(negative) || wgf_keyboard_is_down(negative2)) value -= 1.0f;
    if (wgf_keyboard_is_down(positive) || wgf_keyboard_is_down(positive2)) value += 1.0f;
    return value;
}

/* The stick pushed furthest across the pads. */
static wgf_vec2_t stick(wgf_gamepad_stick_t which)
{
    wgf_vec2_t best = wgf_vec2_make(0.0f, 0.0f);
    int pad;
    for (pad = 0; pad < PADS; pad++) {
        const wgf_vec2_t s = wgf_gamepad_get_stick(pad, which);
        if (s.x * s.x + s.y * s.y > best.x * best.x + best.y * best.y) best = s;
    }
    return best;
}

/* Whether any pad holds `button`. */
static bool pad_down(wgf_gamepad_button_t button)
{
    int pad;
    for (pad = 0; pad < PADS; pad++) {
        if (wgf_gamepad_is_down(pad, button)) return true;
    }
    return false;
}

static float clamp1(float value)
{
    return value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value;
}

/* The frame's input, read once for every flown camera. */
typedef struct intent_t {
    float forward, right, up; /* -1 to 1 */
    wgf_vec2_t mouse, look;   /* the mouse's movement, logical pixels; the right stick */
    bool mouse_free;          /* the pointer not the UI's */
    bool right_down;          /* the mouse's right button held */
} intent_t;

static intent_t read_intent(void)
{
    intent_t in;
    const wgf_vec2_t move = stick(WGF_GAMEPAD_STICK_LEFT);
    memset(&in, 0, sizeof(in));
    in.forward = -move.y; /* y down */
    in.right = move.x;
    if (pad_down(WGF_GAMEPAD_BUTTON_RIGHT_BUMPER)) in.up += 1.0f;
    if (pad_down(WGF_GAMEPAD_BUTTON_LEFT_BUMPER)) in.up -= 1.0f;
    if (!wgf_input_is_keyboard_captured()) {
        in.forward += keys(WGF_KEY_S, WGF_KEY_DOWN, WGF_KEY_W, WGF_KEY_UP);
        in.right += keys(WGF_KEY_A, WGF_KEY_LEFT, WGF_KEY_D, WGF_KEY_RIGHT);
        in.up += keys(WGF_KEY_LEFT_CONTROL, WGF_KEY_Q, WGF_KEY_SPACE, WGF_KEY_E);
    }
    in.forward = clamp1(in.forward), in.right = clamp1(in.right), in.up = clamp1(in.up);
    in.look = stick(WGF_GAMEPAD_STICK_RIGHT);
    in.mouse_free = !wgf_input_is_pointer_captured();
    in.right_down = wgf_mouse_is_down(WGF_MOUSE_BUTTON_RIGHT);
    in.mouse = wgf_mouse_get_delta();
    return in;
}

/* Whether `f`'s mouse looks this frame, the pointer locked. */
static bool wants_mouse(const fly_t *f, const intent_t *in)
{
    return in->mouse_free && (!f->held || in->right_down);
}

static void step(fly_t *f, const intent_t *in, bool locked, float dt)
{
    float sy, cy, mx = 0.0f, my = 0.0f, mz = 0.0f, length;
    if (wants_mouse(f, in) && locked) {
        f->yaw -= in->mouse.x * f->sensitivity;
        f->pitch -= in->mouse.y * f->sensitivity;
    }
    f->yaw = wrap_yaw(f->yaw - in->look.x * f->turn_speed * dt);
    f->pitch = clamp_pitch(f->pitch - in->look.y * f->turn_speed * dt);
    sy = wgf_trig_sin(f->yaw), cy = wgf_trig_cos(f->yaw);
    /* forward along the ground (-sin yaw, 0, -cos yaw), right (cos yaw, 0, -sin yaw); FREE
       forward where it looks, and up the world's */
    if (f->mode == WGF_FLY_MODE_FREE) {
        const float cp = wgf_trig_cos(f->pitch), sp = wgf_trig_sin(f->pitch);
        mx = -sy * cp * in->forward + cy * in->right;
        my = sp * in->forward + in->up;
        mz = -cy * cp * in->forward - sy * in->right;
    } else {
        mx = -sy * in->forward + cy * in->right;
        my = 0.0f;
        mz = -cy * in->forward - sy * in->right;
    }
    length = sqrtf(mx * mx + my * my + mz * mz);
    if (length > 1.0f) mx /= length, my /= length, mz /= length; /* no faster on a diagonal */
    if (length > 0.0f) {
        const wgf_vec3_t at = wgf_actor_get_world_position(f->camera);
        place(f->camera, at.x + mx * f->speed * dt, f->mode == WGF_FLY_MODE_FREE ? at.y + my * f->speed * dt : at.y,
              at.z + mz * f->speed * dt);
    }
    turn(f);
}

static void update(float dt)
{
    intent_t in;
    bool want = false, locked;
    int i;
    for (i = 0; i < MAX_FLIES; i++) {
        fly_t *f = &fy.flies[i];
        if (f->camera != 0 && !wgf_handle_is_alive(f->camera)) memset(f, 0, sizeof(*f));
    }
    in = read_intent();
    for (i = 0; i < MAX_FLIES; i++) {
        const fly_t *f = &fy.flies[i];
        if (f->camera != 0 && f->mode != WGF_FLY_MODE_NONE && wants_mouse(f, &in)) want = true;
    }
    /* the pointer locked while a camera looks by it, asked again by a click once the
       platform let it go */
    if (want && (!fy.lock_asked || (!wgf_mouse_is_locked() && wgf_mouse_is_pressed(WGF_MOUSE_BUTTON_LEFT)))) {
        wgf_mouse_set_locked(true);
        fy.lock_asked = true;
    } else if (!want && fy.lock_asked) {
        wgf_mouse_set_locked(false);
        fy.lock_asked = false;
    }
    locked = wgf_mouse_is_locked();
    for (i = 0; i < MAX_FLIES; i++) {
        fly_t *f = &fy.flies[i];
        if (f->camera != 0 && f->mode != WGF_FLY_MODE_NONE) step(f, &in, locked, dt);
    }
}

static void stop(void)
{
    memset(&fy, 0, sizeof(fy));
}

static wgf_core_priv_part_t part = {.name = "fly",
                                    .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_FLY,
                                    .update = update,
                                    .stop = stop};

/* Whether `camera` is a live 3D camera, a dead one said (wgf_core_priv_handle_stale). */
static bool is_camera_at(wgf_actor_t camera, const char *caller)
{
    const wgf_gfx_priv_actor_t *camera_ptr = wgf_gfx_priv_actor_of_at(camera, caller);
    return camera_ptr != NULL && camera_ptr->type == WGF_ACTOR_KIND_CAMERA3D;
}

/* The entry of a live 3D camera; NULL for anything else, or one with none. */
static fly_t *fly_of_at(wgf_actor_t camera, const char *caller)
{
    int i;
    if (camera == 0 || !is_camera_at(camera, caller)) return NULL;
    for (i = 0; i < MAX_FLIES; i++) {
        if (fy.flies[i].camera == camera) return &fy.flies[i];
    }
    return NULL;
}

/* The entry of a live 3D camera, made with the defaults when it has none; NULL for anything
 * else, or 64 taken. */
static fly_t *fly_made_at(wgf_actor_t camera, const char *caller)
{
    fly_t *f = fly_of_at(camera, caller);
    int i;
    if (f != NULL || !is_camera_at(camera, NULL)) return f;
    for (i = 0; i < MAX_FLIES && f == NULL; i++) {
        if (fy.flies[i].camera == 0 || !wgf_handle_is_alive(fy.flies[i].camera)) f = &fy.flies[i];
    }
    if (f == NULL) return NULL;
    memset(f, 0, sizeof(*f));
    f->camera = camera;
    f->speed = 5.4f;
    f->sensitivity = 0.003f;
    f->turn_speed = 2.0f;
    facing(camera, &f->yaw, &f->pitch);
    if (!fy.installed) {
        wgf_core_priv_part_install(&part);
        fy.installed = true;
    }
    return f;
}
#define fly_of(camera) fly_of_at(camera, WGF_CORE_PRIV_CALLER)
#define fly_made(camera) fly_made_at(camera, WGF_CORE_PRIV_CALLER)

bool wgf_fly_set_mode(wgf_actor_t camera, wgf_fly_mode_t mode)
{
    fly_t *f;
    if ((int)mode < WGF_FLY_MODE_NONE || mode > WGF_FLY_MODE_FIRST_PERSON) return false;
    if (mode == WGF_FLY_MODE_NONE && fly_of(camera) == NULL) return is_camera_at(camera, NULL);
    f = fly_made(camera);
    if (f == NULL) return false;
    if (f->mode == WGF_FLY_MODE_NONE && mode != WGF_FLY_MODE_NONE) facing(camera, &f->yaw, &f->pitch);
    f->mode = (int)mode;
    return true;
}

wgf_fly_mode_t wgf_fly_get_mode(wgf_actor_t camera)
{
    const fly_t *f = fly_of(camera);
    return f != NULL ? (wgf_fly_mode_t)f->mode : WGF_FLY_MODE_NONE;
}

bool wgf_fly_set_speed(wgf_actor_t camera, float speed)
{
    fly_t *f = !(speed >= 0.0f) || !isfinite(speed) ? NULL : fly_made(camera);
    if (f == NULL) return false;
    f->speed = speed;
    return true;
}

float wgf_fly_get_speed(wgf_actor_t camera)
{
    const fly_t *f = fly_of(camera);
    return f != NULL ? f->speed : is_camera_at(camera, NULL) ? 5.4f : 0.0f;
}

bool wgf_fly_set_sensitivity(wgf_actor_t camera, float radians)
{
    fly_t *f = !(radians >= 0.0f) || !isfinite(radians) ? NULL : fly_made(camera);
    if (f == NULL) return false;
    f->sensitivity = radians;
    return true;
}

float wgf_fly_get_sensitivity(wgf_actor_t camera)
{
    const fly_t *f = fly_of(camera);
    return f != NULL ? f->sensitivity : is_camera_at(camera, NULL) ? 0.003f : 0.0f;
}

bool wgf_fly_set_turn_speed(wgf_actor_t camera, float radians)
{
    fly_t *f = !(radians >= 0.0f) || !isfinite(radians) ? NULL : fly_made(camera);
    if (f == NULL) return false;
    f->turn_speed = radians;
    return true;
}

float wgf_fly_get_turn_speed(wgf_actor_t camera)
{
    const fly_t *f = fly_of(camera);
    return f != NULL ? f->turn_speed : is_camera_at(camera, NULL) ? 2.0f : 0.0f;
}

bool wgf_fly_set_look(wgf_actor_t camera, float yaw, float pitch)
{
    fly_t *f = !isfinite(yaw) || !isfinite(pitch) ? NULL : fly_made(camera);
    if (f == NULL) return false;
    f->yaw = wrap_yaw(yaw);
    f->pitch = clamp_pitch(pitch);
    turn(f);
    return true;
}

wgf_vec2_t wgf_fly_get_look(wgf_actor_t camera)
{
    const fly_t *f = fly_of(camera);
    float yaw = 0.0f, pitch = 0.0f;
    if (f != NULL && f->mode != WGF_FLY_MODE_NONE) return wgf_vec2_make(f->yaw, f->pitch);
    if (f == NULL && !is_camera_at(camera, NULL)) return wgf_vec2_make(0.0f, 0.0f);
    facing(camera, &yaw, &pitch);
    return wgf_vec2_make(yaw, pitch);
}

bool wgf_fly_set_look_held(wgf_actor_t camera, bool held)
{
    fly_t *f = fly_made(camera);
    if (f == NULL) return false;
    f->held = held;
    return true;
}

bool wgf_fly_is_look_held(wgf_actor_t camera)
{
    const fly_t *f = fly_of(camera);
    return f != NULL && f->held;
}
