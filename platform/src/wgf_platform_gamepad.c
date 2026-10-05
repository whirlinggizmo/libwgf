#include "wgf_gamepad.h"

#include <math.h>
#include <string.h>

#include "wgf_platform_gamepad_priv.h"
#include "wgf_platform_input_priv.h"
#include "wgf_log.h"

/* Gamepads, the same on every platform: each frame the platform's pads are read, held
 * values are clamped, the triggers count as buttons past halfway, and what changed
 * since the previous read is recorded twice, for the frame and for the ticks, as
 * input's edges are. */

#define DEFAULT_DEADZONE 0.15f
#define MAX_DEADZONE 0.9f

typedef struct edges_t {
    bool pressed[WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS];
    bool released[WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS];
} edges_t;

static struct {
    bool open;
    wgf_platform_priv_gamepad_t raw[WGF_PLATFORM_PRIV_GAMEPADS];  /* as the platform keeps them */
    wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS]; /* as the program sees them */
    edges_t frame[WGF_PLATFORM_PRIV_GAMEPADS];
    edges_t tick[WGF_PLATFORM_PRIV_GAMEPADS];
    bool testing;
    wgf_platform_priv_gamepad_t test[WGF_PLATFORM_PRIV_GAMEPADS];
    float deadzone;
} pads = {.deadzone = DEFAULT_DEADZONE};

static float clamp(float value, float lo, float hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

void wgf_platform_priv_gamepad_open(void)
{
    if (pads.open) return;
    memset(pads.raw, 0, sizeof(pads.raw));
    memset(pads.pads, 0, sizeof(pads.pads));
    memset(pads.frame, 0, sizeof(pads.frame));
    memset(pads.tick, 0, sizeof(pads.tick));
    if (!pads.testing) wgf_platform_priv_gamepad_platform_open();
    pads.open = true;
}

void wgf_platform_priv_gamepad_close(void)
{
    if (!pads.open) return;
    if (!pads.testing) wgf_platform_priv_gamepad_platform_close();
    memset(pads.pads, 0, sizeof(pads.pads));
    pads.open = false;
}

void wgf_platform_priv_gamepad_set_test_pad(int pad, const wgf_platform_priv_gamepad_t *state)
{
    if (pad < 0 || pad >= WGF_PLATFORM_PRIV_GAMEPADS) return;
    if (!pads.testing && pads.open) wgf_platform_priv_gamepad_platform_close();
    pads.testing = true;
    if (state != NULL) {
        pads.test[pad] = *state;
    } else {
        memset(&pads.test[pad], 0, sizeof(pads.test[pad]));
    }
}

void wgf_platform_priv_gamepad_begin_frame(void)
{
    wgf_platform_priv_gamepad_t next[WGF_PLATFORM_PRIV_GAMEPADS];
    int i, b, a;
    if (!pads.testing) wgf_platform_priv_gamepad_platform_poll(pads.raw);
    memcpy(next, pads.testing ? pads.test : pads.raw, sizeof(next));
    for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPADS; i++) {
        wgf_platform_priv_gamepad_t *pad = &next[i];
        if (!pad->connected) {
            memset(pad, 0, sizeof(*pad)); /* what it held is released */
        } else {
            for (a = 0; a < WGF_PLATFORM_PRIV_GAMEPAD_AXES; a++) {
                pad->axes[a] = clamp(pad->axes[a], a >= WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER ? 0.0f : -1.0f, 1.0f);
            }
            pad->name[WGF_PLATFORM_PRIV_GAMEPAD_NAME_SIZE - 1] = '\0';
            /* the triggers are buttons too, past halfway, whatever the platform said */
            pad->buttons[WGF_GAMEPAD_BUTTON_LEFT_TRIGGER] |= pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER] > 0.5f;
            pad->buttons[WGF_GAMEPAD_BUTTON_RIGHT_TRIGGER] |=
                pad->axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER] > 0.5f;
        }
        if (pad->connected != pads.pads[i].connected) {
            if (pad->connected) {
                wgf_log_info("wgf_platform: gamepad %d: %s", i, pad->name);
            } else {
                wgf_log_info("wgf_platform: gamepad %d: disconnected", i);
            }
        }
        for (b = 0; b < WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS; b++) {
            const bool was = pads.pads[i].buttons[b], is = pad->buttons[b];
            if (is && !was) pads.frame[i].pressed[b] = pads.tick[i].pressed[b] = true;
            if (!is && was) pads.frame[i].released[b] = pads.tick[i].released[b] = true;
        }
    }
    memcpy(pads.pads, next, sizeof(next));
}

void wgf_platform_priv_gamepad_end_tick(void)
{
    memset(pads.tick, 0, sizeof(pads.tick));
}

void wgf_platform_priv_gamepad_end_frame(void)
{
    memset(pads.frame, 0, sizeof(pads.frame));
}

/* ----------------------------------------------------------- public API ---- */

static const wgf_platform_priv_gamepad_t *connected(int pad)
{
    return pad >= 0 && pad < WGF_PLATFORM_PRIV_GAMEPADS && pads.pads[pad].connected ? &pads.pads[pad] : NULL;
}

static bool valid_button(int pad, wgf_gamepad_button_t button)
{
    return pad >= 0 && pad < WGF_PLATFORM_PRIV_GAMEPADS && (int)button >= 0 && (int)button < WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS;
}

static const edges_t *edges_of(int pad)
{
    return wgf_platform_priv_input_get_context() == WGF_PLATFORM_PRIV_INPUT_TICK ? &pads.tick[pad] : &pads.frame[pad];
}

bool wgf_gamepad_is_connected(int pad)
{
    return connected(pad) != NULL;
}

const char *wgf_gamepad_get_name(int pad)
{
    const wgf_platform_priv_gamepad_t *pad_ptr = connected(pad);
    return pad_ptr != NULL ? pad_ptr->name : "";
}

/* edges outlive a pad by a read (its releases), so they are read connected or not */
wgf_input_state_t wgf_gamepad_get_button_state(int pad, wgf_gamepad_button_t button)
{
    const edges_t *edges;
    if (!valid_button(pad, button)) return WGF_INPUT_STATE_UP;
    edges = edges_of(pad);
    if (edges->pressed[button]) return WGF_INPUT_STATE_PRESSED;
    if (edges->released[button]) return WGF_INPUT_STATE_RELEASED;
    return pads.pads[pad].buttons[button] ? WGF_INPUT_STATE_DOWN : WGF_INPUT_STATE_UP;
}

bool wgf_gamepad_is_down(int pad, wgf_gamepad_button_t button)
{
    const wgf_input_state_t state = wgf_gamepad_get_button_state(pad, button);
    return state == WGF_INPUT_STATE_PRESSED || state == WGF_INPUT_STATE_DOWN;
}

bool wgf_gamepad_is_pressed(int pad, wgf_gamepad_button_t button)
{
    return valid_button(pad, button) && edges_of(pad)->pressed[button];
}

bool wgf_gamepad_is_released(int pad, wgf_gamepad_button_t button)
{
    return valid_button(pad, button) && edges_of(pad)->released[button];
}

wgf_vec2_t wgf_gamepad_get_stick(int pad, wgf_gamepad_stick_t stick)
{
    const wgf_platform_priv_gamepad_t *pad_ptr = connected(pad);
    wgf_vec2_t value = {0.0f, 0.0f};
    float x, y, length, scale;
    int axis;
    if (pad_ptr == NULL || ((int)stick != WGF_GAMEPAD_STICK_LEFT && (int)stick != WGF_GAMEPAD_STICK_RIGHT)) {
        return value;
    }
    axis = stick == WGF_GAMEPAD_STICK_LEFT ? WGF_PLATFORM_PRIV_GAMEPAD_LEFT_X : WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_X;
    x = pad_ptr->axes[axis];
    y = pad_ptr->axes[axis + 1];
    length = sqrtf(x * x + y * y);
    if (length <= pads.deadzone) return value;
    /* the part past the dead zone, rescaled to reach 1 at the edge; a square-ish
       stick's corners reach past 1, so the length stops there */
    scale = (length - pads.deadzone) / (1.0f - pads.deadzone) / length;
    if (length * scale > 1.0f) scale = 1.0f / length;
    value.x = x * scale;
    value.y = y * scale;
    return value;
}

float wgf_gamepad_get_trigger(int pad, wgf_gamepad_trigger_t trigger)
{
    const wgf_platform_priv_gamepad_t *pad_ptr = connected(pad);
    if (pad_ptr == NULL) return 0.0f;
    if (trigger == WGF_GAMEPAD_TRIGGER_LEFT) return pad_ptr->axes[WGF_PLATFORM_PRIV_GAMEPAD_LEFT_TRIGGER];
    if (trigger == WGF_GAMEPAD_TRIGGER_RIGHT) return pad_ptr->axes[WGF_PLATFORM_PRIV_GAMEPAD_RIGHT_TRIGGER];
    return 0.0f;
}

void wgf_gamepad_set_deadzone(float radius)
{
    pads.deadzone = radius >= 0.0f ? (radius <= MAX_DEADZONE ? radius : MAX_DEADZONE) : 0.0f;
}

float wgf_gamepad_get_deadzone(void)
{
    return pads.deadzone;
}
