#include "wgf_platform_input_priv.h"

#include <math.h>
#include <string.h>

#include "wgf_platform_priv.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_mouse.h"
#include "wgf_touch.h"
#include "wgf_platform_window_priv.h"

#define MAX_KEYS 512 /* sokol's key codes are under this */
#define MOUSE_BUTTONS 3
#define MAX_TYPED 256 /* bytes of UTF-8 typed between two reads; past this, dropped */
#define MAX_FINGERS 8
#define PI 3.14159265358979f

typedef struct edges_t {
    float dx, dy;
    float wheel_x, wheel_y;
    bool pressed[MOUSE_BUTTONS];
    bool released[MOUSE_BUTTONS];
    bool key_pressed[MAX_KEYS];
    bool key_released[MAX_KEYS];
    char typed[MAX_TYPED];
    int typed_size;
    bool touch_pressed[MAX_FINGERS];
    bool touch_released[MAX_FINGERS];
    float touch_dx[MAX_FINGERS], touch_dy[MAX_FINGERS];
    float gesture_dx, gesture_dy;
    float gesture_log_scale; /* summed, so cleared edges mean a scale of 1 */
    float gesture_rotation;
} edges_t;

/* A finger, in the slot its id names. A lifted finger keeps its slot, and where it
 * lifted, until both edge sets have seen it lift. */
typedef struct finger_t {
    bool down;
    uintptr_t system_id; /* the window system's */
    float x, y;
    unsigned order; /* when it touched down: fingers are listed oldest first */
} finger_t;

static struct {
    float x, y;
    bool down[MOUSE_BUTTONS];
    bool key_down[MAX_KEYS];
    edges_t frame;
    edges_t tick;
    wgf_platform_priv_input_context_t context;
    float dpi_scale; /* the event being handled's */
    finger_t fingers[MAX_FINGERS];
    unsigned finger_order;
    /* the first finger drives the mouse until a second cancels it; then the mouse
       stays up until every finger has lifted */
    bool touching;
    uintptr_t touch_id;
    bool touch_cancelled;
    bool cursor_visible;
    bool mouse_emulated;
    bool pointer_held;        /* the pointer section's press on a node */
    bool ui_pointer_captured; /* the game's UI's say, sticky */
    bool ui_keyboard_captured;
} input = {.cursor_visible = true, .mouse_emulated = true};

void wgf_platform_priv_input_reset(void)
{
    const bool cursor_visible = input.cursor_visible, mouse_emulated = input.mouse_emulated;
    memset(&input, 0, sizeof(input));
    input.cursor_visible = cursor_visible;
    input.mouse_emulated = mouse_emulated;
}

void wgf_platform_priv_input_set_pointer_held(bool held)
{
    input.pointer_held = held;
}

void wgf_input_set_pointer_captured(bool captured)
{
    input.ui_pointer_captured = captured;
}

bool wgf_input_is_pointer_captured(void)
{
    return input.pointer_held || input.ui_pointer_captured;
}

void wgf_input_set_keyboard_captured(bool captured)
{
    input.ui_keyboard_captured = captured;
}

bool wgf_input_is_keyboard_captured(void)
{
    return input.ui_keyboard_captured;
}

void wgf_platform_priv_input_set_context(wgf_platform_priv_input_context_t context)
{
    input.context = context;
}

wgf_platform_priv_input_context_t wgf_platform_priv_input_get_context(void)
{
    return input.context;
}

void wgf_platform_priv_input_end_tick(void)
{
    memset(&input.tick, 0, sizeof(input.tick));
}

void wgf_platform_priv_input_end_frame(void)
{
    memset(&input.frame, 0, sizeof(input.frame));
}

static const edges_t *current(void)
{
    return input.context == WGF_PLATFORM_PRIV_INPUT_TICK ? &input.tick : &input.frame;
}

/* `code` as UTF-8 onto what was typed, whole or not at all. */
static void add_typed(edges_t *edges, uint32_t code)
{
    char bytes[4];
    int n;
    if (code < 0x20 || code == 0x7F || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) return;
    if (code < 0x80) {
        bytes[0] = (char)code;
        n = 1;
    } else if (code < 0x800) {
        bytes[0] = (char)(0xC0 | (code >> 6));
        bytes[1] = (char)(0x80 | (code & 0x3F));
        n = 2;
    } else if (code < 0x10000) {
        bytes[0] = (char)(0xE0 | (code >> 12));
        bytes[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (code & 0x3F));
        n = 3;
    } else {
        bytes[0] = (char)(0xF0 | (code >> 18));
        bytes[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        bytes[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        bytes[3] = (char)(0x80 | (code & 0x3F));
        n = 4;
    }
    if (edges->typed_size + n >= MAX_TYPED) return; /* room for the NUL */
    memcpy(edges->typed + edges->typed_size, bytes, (size_t)n);
    edges->typed_size += n;
}

/* One event's edges, into one set. */
static void add_edges(edges_t *edges, const sapp_event *event, float dpi_scale, bool key_was_down)
{
    switch (event->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            edges->dx += event->mouse_dx / dpi_scale;
            edges->dy += event->mouse_dy / dpi_scale;
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            edges->pressed[event->mouse_button] = true;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            edges->released[event->mouse_button] = true;
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            edges->wheel_x += event->scroll_x;
            edges->wheel_y += event->scroll_y;
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
            if (!event->key_repeat && !key_was_down) edges->key_pressed[event->key_code] = true;
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            edges->key_released[event->key_code] = true;
            break;
        case SAPP_EVENTTYPE_CHAR:
            add_typed(edges, event->char_code);
            break;
        default:
            break;
    }
}

/* ---------------------------------------------------------------- touch ---- */

static int find_finger(uintptr_t system_id)
{
    int i;
    for (i = 0; i < MAX_FINGERS; i++) {
        if (input.fingers[i].down && input.fingers[i].system_id == system_id) return i;
    }
    return -1;
}

/* A slot for a new finger: one with no edges left to report, else any that's up. */
static int free_finger(void)
{
    const edges_t *f = &input.frame, *t = &input.tick;
    int i;
    for (i = 0; i < MAX_FINGERS; i++) {
        if (!input.fingers[i].down && !f->touch_pressed[i] && !f->touch_released[i] && !t->touch_pressed[i] &&
            !t->touch_released[i]) {
            return i;
        }
    }
    for (i = 0; i < MAX_FINGERS; i++) {
        if (!input.fingers[i].down) return i;
    }
    return -1;
}

static int fingers_down(void)
{
    int count = 0, i;
    for (i = 0; i < MAX_FINGERS; i++) count += input.fingers[i].down ? 1 : 0;
    return count;
}

/* The first two fingers down, the gesture's; false with fewer. */
static bool gesture_pair(int *a, int *b)
{
    int i;
    *a = *b = -1;
    for (i = 0; i < MAX_FINGERS; i++) {
        const finger_t *finger = &input.fingers[i];
        if (!finger->down) continue;
        if (*a < 0 || finger->order < input.fingers[*a].order) {
            *b = *a;
            *a = i;
        } else if (*b < 0 || finger->order < input.fingers[*b].order) {
            *b = i;
        }
    }
    return *b >= 0;
}

/* Move the mouse to logical (x, y), and press or release its left button. */
static void pointer_event(sapp_event_type type, float x, float y)
{
    const float scale = input.dpi_scale;
    sapp_event mouse;
    memset(&mouse, 0, sizeof(mouse));
    mouse.type = SAPP_EVENTTYPE_MOUSE_MOVE;
    mouse.mouse_x = x * scale;
    mouse.mouse_y = y * scale;
    mouse.mouse_dx = (x - input.x) * scale;
    mouse.mouse_dy = (y - input.y) * scale;
    wgf_platform_priv_input_handle_event(&mouse, scale);
    if (type != SAPP_EVENTTYPE_MOUSE_MOVE) {
        mouse.type = type;
        mouse.mouse_button = SAPP_MOUSEBUTTON_LEFT;
        mouse.mouse_dx = mouse.mouse_dy = 0.0f;
        wgf_platform_priv_input_handle_event(&mouse, scale);
    }
}

/* The mouse follows the first finger; a second one cancels its press. */
static void touch_pointer(sapp_event_type type, uintptr_t system_id, const finger_t *finger)
{
    if (type == SAPP_EVENTTYPE_TOUCHES_BEGAN) {
        if (!input.touching && !input.touch_cancelled && fingers_down() == 1) {
            input.touching = true;
            input.touch_id = system_id;
            pointer_event(SAPP_EVENTTYPE_MOUSE_DOWN, finger->x, finger->y);
        } else if (input.touching) {
            input.touching = false; /* let go off the window: nothing under it is clicked */
            input.touch_cancelled = true;
            pointer_event(SAPP_EVENTTYPE_MOUSE_UP, -1.0f, -1.0f);
        }
    } else if (input.touching && system_id == input.touch_id) {
        if (type == SAPP_EVENTTYPE_TOUCHES_MOVED) {
            pointer_event(SAPP_EVENTTYPE_MOUSE_MOVE, finger->x, finger->y);
        } else {
            input.touching = false;
            pointer_event(SAPP_EVENTTYPE_MOUSE_UP, finger->x, finger->y);
        }
    }
}

/* Fingers, their edges, the mouse, and the gesture, from one touch event. */
static void handle_touch(const sapp_event *event)
{
    const float scale = input.dpi_scale;
    const sapp_event_type type =
        event->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED ? SAPP_EVENTTYPE_TOUCHES_ENDED : event->type;
    float ax = 0, ay = 0, bx = 0, by = 0;
    int a, b, i;
    const bool had_pair = gesture_pair(&a, &b);
    const int pair_a = a, pair_b = b;

    if (had_pair) {
        ax = input.fingers[a].x, ay = input.fingers[a].y;
        bx = input.fingers[b].x, by = input.fingers[b].y;
    }
    for (i = 0; i < event->num_touches && i < SAPP_MAX_TOUCHPOINTS; i++) {
        const sapp_touchpoint *touch = &event->touches[i];
        const float x = touch->pos_x / scale, y = touch->pos_y / scale;
        int slot = find_finger(touch->identifier);
        finger_t *finger;

        if (!touch->changed) continue;
        if (type == SAPP_EVENTTYPE_TOUCHES_BEGAN && slot < 0) {
            slot = free_finger();
            if (slot < 0) continue; /* more fingers than there are slots */
            input.frame.touch_released[slot] = input.tick.touch_released[slot] = false;
            input.frame.touch_dx[slot] = input.tick.touch_dx[slot] = 0.0f;
            input.frame.touch_dy[slot] = input.tick.touch_dy[slot] = 0.0f;
            input.fingers[slot].down = true;
            input.fingers[slot].system_id = touch->identifier;
            input.fingers[slot].x = x;
            input.fingers[slot].y = y;
            input.fingers[slot].order = ++input.finger_order;
            input.frame.touch_pressed[slot] = input.tick.touch_pressed[slot] = true;
        } else if (slot < 0) {
            continue;
        }
        finger = &input.fingers[slot];
        input.frame.touch_dx[slot] += x - finger->x;
        input.tick.touch_dx[slot] += x - finger->x;
        input.frame.touch_dy[slot] += y - finger->y;
        input.tick.touch_dy[slot] += y - finger->y;
        finger->x = x;
        finger->y = y;
        if (type == SAPP_EVENTTYPE_TOUCHES_ENDED) {
            finger->down = false;
            input.frame.touch_released[slot] = input.tick.touch_released[slot] = true;
        }
        if (input.mouse_emulated) touch_pointer(type, touch->identifier, finger);
    }
    if (fingers_down() == 0) input.touch_cancelled = false;

    /* the same two fingers before and after: they panned, pinched, and twisted */
    if (had_pair && gesture_pair(&a, &b) && a == pair_a && b == pair_b) {
        const float nax = input.fingers[a].x, nay = input.fingers[a].y;
        const float nbx = input.fingers[b].x, nby = input.fingers[b].y;
        const float before = hypotf(bx - ax, by - ay), after = hypotf(nbx - nax, nby - nay);
        const float pan_x = (nax + nbx - ax - bx) * 0.5f, pan_y = (nay + nby - ay - by) * 0.5f;
        float turn = atan2f(nby - nay, nbx - nax) - atan2f(by - ay, bx - ax);
        edges_t *sets[2];
        int set;
        if (turn > PI) turn -= 2.0f * PI;
        if (turn < -PI) turn += 2.0f * PI;
        sets[0] = &input.frame;
        sets[1] = &input.tick;
        for (set = 0; set < 2; set++) {
            sets[set]->gesture_dx += pan_x;
            sets[set]->gesture_dy += pan_y;
            if (before > 0.0f && after > 0.0f) {
                sets[set]->gesture_log_scale += logf(after / before);
                sets[set]->gesture_rotation += turn;
            }
        }
    }
}

void wgf_platform_priv_input_handle_event(const sapp_event *event, float dpi_scale)
{
    bool key_was_down = false;
    if (event == NULL) return;
    input.dpi_scale = dpi_scale >= 1.0f ? dpi_scale : 1.0f;
    dpi_scale = input.dpi_scale;
    switch (event->type) {
        case SAPP_EVENTTYPE_TOUCHES_BEGAN:
        case SAPP_EVENTTYPE_TOUCHES_MOVED:
        case SAPP_EVENTTYPE_TOUCHES_ENDED:
        case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
            handle_touch(event);
            return;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
        case SAPP_EVENTTYPE_MOUSE_UP:
            if ((int)event->mouse_button < 0 || (int)event->mouse_button >= MOUSE_BUTTONS) return;
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
        case SAPP_EVENTTYPE_KEY_UP:
            if ((int)event->key_code <= 0 || (int)event->key_code >= MAX_KEYS) return;
            key_was_down = input.key_down[event->key_code];
            break;
        case SAPP_EVENTTYPE_UNFOCUSED:
            /* the keys held go up with the focus: their releases go elsewhere */
            {
                int key;
                for (key = 0; key < MAX_KEYS; key++) {
                    if (!input.key_down[key]) continue;
                    input.key_down[key] = false;
                    input.frame.key_released[key] = input.tick.key_released[key] = true;
                }
            }
            return;
        default:
            break;
    }
    add_edges(&input.frame, event, dpi_scale, key_was_down);
    add_edges(&input.tick, event, dpi_scale, key_was_down);
    switch (event->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            input.x = event->mouse_x / dpi_scale;
            input.y = event->mouse_y / dpi_scale;
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            input.down[event->mouse_button] = true;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            input.down[event->mouse_button] = false;
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
            input.key_down[event->key_code] = true;
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            input.key_down[event->key_code] = false;
            break;
        default:
            break;
    }
}

static wgf_input_state_t state_of(bool down, bool pressed, bool released)
{
    if (pressed) return WGF_INPUT_STATE_PRESSED;
    if (released) return WGF_INPUT_STATE_RELEASED;
    return down ? WGF_INPUT_STATE_DOWN : WGF_INPUT_STATE_UP;
}

/* ---------------------------------------------------------------- keys ---- */

wgf_input_state_t wgf_keyboard_get_state(wgf_keyboard_key_t key)
{
    const edges_t *edges = current();
    if ((int)key <= 0 || (int)key >= MAX_KEYS) return WGF_INPUT_STATE_UP;
    return state_of(input.key_down[key], edges->key_pressed[key], edges->key_released[key]);
}

bool wgf_keyboard_is_down(wgf_keyboard_key_t key)
{
    const wgf_input_state_t state = wgf_keyboard_get_state(key);
    return state == WGF_INPUT_STATE_PRESSED || state == WGF_INPUT_STATE_DOWN;
}

bool wgf_keyboard_is_pressed(wgf_keyboard_key_t key)
{
    return (int)key > 0 && (int)key < MAX_KEYS && current()->key_pressed[key];
}

bool wgf_keyboard_is_released(wgf_keyboard_key_t key)
{
    return (int)key > 0 && (int)key < MAX_KEYS && current()->key_released[key];
}

const char *wgf_input_get_chars(void)
{
    edges_t *edges = input.context == WGF_PLATFORM_PRIV_INPUT_TICK ? &input.tick : &input.frame;
    edges->typed[edges->typed_size] = '\0';
    return edges->typed;
}

/* --------------------------------------------------------------- mouse ---- */

wgf_vec2_t wgf_mouse_get_position(void)
{
    const wgf_vec2_t position = {input.x, input.y};
    return position;
}

wgf_vec2_t wgf_mouse_get_delta(void)
{
    const wgf_vec2_t delta = {current()->dx, current()->dy};
    return delta;
}

wgf_vec2_t wgf_mouse_get_wheel(void)
{
    const wgf_vec2_t wheel = {current()->wheel_x, current()->wheel_y};
    return wheel;
}

wgf_input_state_t wgf_mouse_get_button_state(wgf_mouse_button_t button)
{
    const edges_t *edges = current();
    if ((int)button < 0 || (int)button >= MOUSE_BUTTONS) return WGF_INPUT_STATE_UP;
    return state_of(input.down[button], edges->pressed[button], edges->released[button]);
}

bool wgf_mouse_is_down(wgf_mouse_button_t button)
{
    const wgf_input_state_t state = wgf_mouse_get_button_state(button);
    return state == WGF_INPUT_STATE_PRESSED || state == WGF_INPUT_STATE_DOWN;
}

bool wgf_mouse_is_pressed(wgf_mouse_button_t button)
{
    return (int)button >= 0 && (int)button < MOUSE_BUTTONS && current()->pressed[button];
}

bool wgf_mouse_is_released(wgf_mouse_button_t button)
{
    return (int)button >= 0 && (int)button < MOUSE_BUTTONS && current()->released[button];
}

void wgf_mouse_set_cursor_visible(bool visible)
{
    input.cursor_visible = visible;
    if (wgf_platform_priv_window_is_open()) wgf_platform_priv_show_mouse(visible);
}

bool wgf_mouse_is_cursor_visible(void)
{
    return input.cursor_visible;
}

/* ---------------------------------------------------------------- touch ---- */

/* The fingers to list in the current context, down or with an edge in it, oldest
 * first. */
static int list_fingers(int out[MAX_FINGERS])
{
    const edges_t *edges = current();
    int count = 0, i;
    for (i = 0; i < MAX_FINGERS; i++) {
        int j;
        if (!input.fingers[i].down && !edges->touch_pressed[i] && !edges->touch_released[i]) continue;
        j = count++;
        while (j > 0 && input.fingers[out[j - 1]].order > input.fingers[i].order) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = i;
    }
    return count;
}

/* Whether `id` is a finger to report in the current context. */
static bool listed(int id)
{
    const edges_t *edges = current();
    return id >= 0 && id < MAX_FINGERS &&
           (input.fingers[id].down || edges->touch_pressed[id] || edges->touch_released[id]);
}

int wgf_touch_get_count(void)
{
    int ids[MAX_FINGERS];
    return list_fingers(ids);
}

int wgf_touch_get_id(int index)
{
    int ids[MAX_FINGERS];
    const int count = list_fingers(ids);
    return index >= 0 && index < count ? ids[index] : -1;
}

wgf_input_state_t wgf_touch_get_state(int id)
{
    const edges_t *edges = current();
    if (!listed(id)) return WGF_INPUT_STATE_UP;
    return state_of(input.fingers[id].down, edges->touch_pressed[id], edges->touch_released[id]);
}

wgf_vec2_t wgf_touch_get_position(int id)
{
    wgf_vec2_t position = {0.0f, 0.0f};
    if (listed(id)) {
        position.x = input.fingers[id].x;
        position.y = input.fingers[id].y;
    }
    return position;
}

wgf_vec2_t wgf_touch_get_delta(int id)
{
    wgf_vec2_t delta = {0.0f, 0.0f};
    if (listed(id)) {
        delta.x = current()->touch_dx[id];
        delta.y = current()->touch_dy[id];
    }
    return delta;
}

void wgf_touch_set_mouse_emulated(bool emulated)
{
    input.mouse_emulated = emulated;
}

bool wgf_touch_is_mouse_emulated(void)
{
    return input.mouse_emulated;
}

/* -------------------------------------------------------------- gesture ---- */

bool wgf_touch_is_gesture(void)
{
    int a, b;
    return gesture_pair(&a, &b);
}

wgf_vec2_t wgf_touch_get_gesture_center(void)
{
    wgf_vec2_t center = {0.0f, 0.0f};
    int a, b;
    if (gesture_pair(&a, &b)) {
        center.x = (input.fingers[a].x + input.fingers[b].x) * 0.5f;
        center.y = (input.fingers[a].y + input.fingers[b].y) * 0.5f;
    }
    return center;
}

wgf_vec2_t wgf_touch_get_gesture_pan(void)
{
    const wgf_vec2_t pan = {current()->gesture_dx, current()->gesture_dy};
    return pan;
}

float wgf_touch_get_gesture_scale(void)
{
    return expf(current()->gesture_log_scale);
}

float wgf_touch_get_gesture_rotation(void)
{
    return current()->gesture_rotation;
}
