#include "wgf_action.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_platform_input_priv.h"
#include "wgf_touch.h"
#include "wgf_core_format_priv.h"

/* Input actions (wgf_action.h): a table of names, each with its bindings, read through
 * the public input calls when asked. What a discrete binding can't say -- whether an
 * axis past its threshold, or a finger in a region, was down as the last tick or frame
 * ended -- is kept per action as each one ends (the input layer's edges hook, set by the
 * first binding). */

#define ACTIONS_MAX 64
#define BINDINGS_MAX 16
#define NAME_MAX 32
#define PADS 4

typedef enum binding_kind_t { KEY, KEYS, PAD_BUTTON, PAD_BUTTONS, PAD_AXIS, TOUCH, STICK } binding_kind_t;

typedef struct binding_t {
    binding_kind_t kind;
    int a, b;           /* the key, the keys (negative, positive), the button(s), or the axis */
    int direction;      /* a pad axis: -1, 0, or 1 */
    float threshold;    /* a pad axis: below it, 0 */
    float rect[4];      /* a touch region */
    int stick, part;    /* a stick's: which, and which way (0 across, 1 up) */
} binding_t;

typedef struct action_t {
    char name[NAME_MAX];
    binding_t bindings[BINDINGS_MAX];
    int binding_count;
    bool was_down[2]; /* as the last frame (0) and tick (1) ended */
} action_t;

/* An on-screen stick: where a finger may land to make it, how far it pulls, and the finger
 * holding it now (-1 none) with where it landed. */
#define STICKS_MAX 4
typedef struct stick_t {
    float rect[4], radius;
    int finger;
    float origin[2], at[2];
    char action_x[NAME_MAX];
} stick_t;

static struct {
    stick_t sticks[STICKS_MAX];
    int count;
} sticks;

static struct {
    action_t actions[ACTIONS_MAX];
    int count;
    char text[64];
} table;

static action_t *find(const char *name)
{
    int i;
    for (i = 0; name != NULL && i < table.count; i++) {
        if (strcmp(table.actions[i].name, name) == 0) return &table.actions[i];
    }
    return NULL;
}

/* ---- reading ----------------------------------------------------------------------- */

static float clamp1(float v)
{
    return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

/* A pad axis's furthest pad, its direction applied: -1 to 1 for both sides, 0 to 1 for
 * one, 0 below the threshold. */
static float pad_axis(const binding_t *b)
{
    float best = 0.0f;
    int pad;
    for (pad = 0; pad < PADS; pad++) {
        float v = wgf_gamepad_get_axis(pad, (wgf_gamepad_axis_t)b->a);
        if (b->direction != 0) v = v * (float)b->direction > 0.0f ? v * (float)b->direction : 0.0f;
        if ((v < 0.0f ? -v : v) > (best < 0.0f ? -best : best)) best = v;
    }
    return (best < 0.0f ? -best : best) < b->threshold ? 0.0f : best;
}

static bool touched(const binding_t *b)
{
    int i;
    for (i = 0; i < wgf_touch_get_count(); i++) {
        const int id = wgf_touch_get_id(i);
        const wgf_input_state_t state = wgf_touch_get_state(id);
        const wgf_vec2_t at = wgf_touch_get_position(id);
        if ((state == WGF_INPUT_STATE_PRESSED || state == WGF_INPUT_STATE_DOWN) && at.x >= b->rect[0] &&
            at.y >= b->rect[1] && at.x < b->rect[0] + b->rect[2] && at.y < b->rect[1] + b->rect[3]) {
            return true;
        }
    }
    return false;
}

/* The stick as its finger has it now: a finger lifted lets it go; a finger landing in its
 * region, not another stick's, takes it; its pull read as -1 to 1 each way. */
static stick_t *stick_now(int index)
{
    stick_t *s = &sticks.sticks[index];
    int i;
    if (s->finger >= 0) {
        const wgf_input_state_t state = wgf_touch_get_state(s->finger);
        if (state == WGF_INPUT_STATE_PRESSED || state == WGF_INPUT_STATE_DOWN) {
            const wgf_vec2_t at = wgf_touch_get_position(s->finger);
            s->at[0] = at.x, s->at[1] = at.y;
            return s;
        }
        s->finger = -1;
    }
    for (i = 0; i < wgf_touch_get_count(); i++) {
        const int id = wgf_touch_get_id(i);
        const wgf_vec2_t at = wgf_touch_get_position(id);
        int k;
        bool taken = false;
        if (wgf_touch_get_state(id) != WGF_INPUT_STATE_PRESSED) continue;
        for (k = 0; k < sticks.count; k++) taken = taken || sticks.sticks[k].finger == id;
        if (taken || at.x < s->rect[0] || at.y < s->rect[1] || at.x >= s->rect[0] + s->rect[2] ||
            at.y >= s->rect[1] + s->rect[3]) {
            continue;
        }
        s->finger = id;
        s->origin[0] = s->at[0] = at.x;
        s->origin[1] = s->at[1] = at.y;
        break;
    }
    return s;
}

/* A stick's pull each way, -1 to 1, held within its circle; y up as a pad's is (the
 * screen's y runs down). */
static void stick_pull(const stick_t *s, float *x, float *y)
{
    float dx = (s->at[0] - s->origin[0]) / s->radius, dy = -(s->at[1] - s->origin[1]) / s->radius;
    const float len = sqrtf(dx * dx + dy * dy);
    if (s->finger < 0) dx = dy = 0.0f;
    else if (len > 1.0f) dx /= len, dy /= len;
    *x = dx;
    *y = dy;
}

static bool pad_button_down(int button)
{
    int pad;
    for (pad = 0; pad < PADS; pad++) {
        if (wgf_gamepad_is_down(pad, (wgf_gamepad_button_t)button)) return true;
    }
    return false;
}

static float value_of(const binding_t *b)
{
    switch (b->kind) {
        case KEY: return wgf_keyboard_is_down((wgf_keyboard_key_t)b->a) ? 1.0f : 0.0f;
        case KEYS:
            return (wgf_keyboard_is_down((wgf_keyboard_key_t)b->b) ? 1.0f : 0.0f) -
                   (wgf_keyboard_is_down((wgf_keyboard_key_t)b->a) ? 1.0f : 0.0f);
        case PAD_BUTTON: return pad_button_down(b->a) ? 1.0f : 0.0f;
        case PAD_BUTTONS: return (pad_button_down(b->b) ? 1.0f : 0.0f) - (pad_button_down(b->a) ? 1.0f : 0.0f);
        case PAD_AXIS: return pad_axis(b);
        case STICK: {
            float x, y;
            stick_pull(stick_now(b->stick), &x, &y);
            return b->part == 0 ? x : y;
        }
        default: return touched(b) ? 1.0f : 0.0f;
    }
}

static bool binding_down(const binding_t *b)
{
    const float v = value_of(b);
    const float limit = b->kind == PAD_AXIS && b->threshold > 0.0f ? b->threshold : 0.5f;
    return (v < 0.0f ? -v : v) >= limit;
}

/* Whether a key or button binding went down since the last tick or frame: a tap shorter
 * than a tick is still a press. */
static bool binding_pressed(const binding_t *b)
{
    int pad;
    switch (b->kind) {
        case KEY: return wgf_keyboard_is_pressed((wgf_keyboard_key_t)b->a);
        case KEYS:
            return wgf_keyboard_is_pressed((wgf_keyboard_key_t)b->a) ||
                   wgf_keyboard_is_pressed((wgf_keyboard_key_t)b->b);
        case PAD_BUTTON:
        case PAD_BUTTONS:
            for (pad = 0; pad < PADS; pad++) {
                if (wgf_gamepad_is_pressed(pad, (wgf_gamepad_button_t)b->a)) return true;
                if (b->kind == PAD_BUTTONS && wgf_gamepad_is_pressed(pad, (wgf_gamepad_button_t)b->b)) return true;
            }
            return false;
        default: return false;
    }
}

static float axis_of(const action_t *action)
{
    float best = 0.0f;
    int i;
    for (i = 0; action != NULL && i < action->binding_count; i++) {
        const float v = value_of(&action->bindings[i]);
        if ((v < 0.0f ? -v : v) > (best < 0.0f ? -best : best)) best = v;
    }
    return clamp1(best);
}

static bool down_of(const action_t *action)
{
    int i;
    for (i = 0; action != NULL && i < action->binding_count; i++) {
        if (binding_down(&action->bindings[i])) return true;
    }
    return false;
}

static void edges_ended(wgf_platform_priv_input_context_t context)
{
    int i;
    for (i = 0; i < table.count; i++) table.actions[i].was_down[context] = down_of(&table.actions[i]);
}

float wgf_action_get_axis(const char *action)
{
    return axis_of(find(action));
}

float wgf_action_get_value(const char *action)
{
    const float v = axis_of(find(action));
    return v < 0.0f ? -v : v;
}

wgf_input_state_t wgf_action_get_state(const char *action)
{
    const action_t *a = find(action);
    const bool was = a != NULL && a->was_down[wgf_platform_priv_input_get_context()];
    int i;
    if (a == NULL) return WGF_INPUT_STATE_UP;
    if (down_of(a)) {
        if (!was) return WGF_INPUT_STATE_PRESSED;
        for (i = 0; i < a->binding_count; i++) {
            if (binding_pressed(&a->bindings[i])) return WGF_INPUT_STATE_PRESSED;
        }
        return WGF_INPUT_STATE_DOWN;
    }
    if (was) return WGF_INPUT_STATE_RELEASED;
    for (i = 0; i < a->binding_count; i++) { /* down and up again since: a press, seen once */
        if (binding_pressed(&a->bindings[i])) return WGF_INPUT_STATE_PRESSED;
    }
    return WGF_INPUT_STATE_UP;
}

bool wgf_action_is_down(const char *action)
{
    const wgf_input_state_t state = wgf_action_get_state(action);
    return state == WGF_INPUT_STATE_PRESSED || state == WGF_INPUT_STATE_DOWN;
}

bool wgf_action_is_pressed(const char *action)
{
    return wgf_action_get_state(action) == WGF_INPUT_STATE_PRESSED;
}

bool wgf_action_is_released(const char *action)
{
    return wgf_action_get_state(action) == WGF_INPUT_STATE_RELEASED;
}

/* ---- binding ----------------------------------------------------------------------- */

static bool is_key(int key)
{
    return key >= WGF_KEY_SPACE && key <= WGF_KEY_MENU;
}

/* `action`'s next binding, the action made the first time; NULL when there is no room or
 * the name breaks the rule. */
static binding_t *add(const char *name)
{
    action_t *action = find(name);
    binding_t *b;
    const size_t n = name != NULL ? strlen(name) : 0;
    if (action == NULL) {
        if (n == 0 || n >= NAME_MAX || table.count == ACTIONS_MAX) return NULL;
        action = &table.actions[table.count++];
        memset(action, 0, sizeof(*action));
        memcpy(action->name, name, n + 1);
        wgf_platform_priv_input_set_edges_hook(edges_ended);
    }
    if (action->binding_count == BINDINGS_MAX) return NULL;
    b = &action->bindings[action->binding_count++];
    memset(b, 0, sizeof(*b));
    return b;
}

bool wgf_action_bind_key(const char *action, wgf_keyboard_key_t key)
{
    binding_t *b;
    if (!is_key((int)key) || (b = add(action)) == NULL) return false;
    b->kind = KEY;
    b->a = (int)key;
    return true;
}

bool wgf_action_bind_keys(const char *action, wgf_keyboard_key_t negative, wgf_keyboard_key_t positive)
{
    binding_t *b;
    if (!is_key((int)negative) || !is_key((int)positive) || (b = add(action)) == NULL) return false;
    b->kind = KEYS;
    b->a = (int)negative;
    b->b = (int)positive;
    return true;
}

bool wgf_action_bind_pad_button(const char *action, wgf_gamepad_button_t button)
{
    binding_t *b;
    if ((int)button < WGF_GAMEPAD_BUTTON_SOUTH || (int)button > WGF_GAMEPAD_BUTTON_DPAD_RIGHT ||
        (b = add(action)) == NULL) {
        return false;
    }
    b->kind = PAD_BUTTON;
    b->a = (int)button;
    return true;
}

bool wgf_action_bind_pad_buttons(const char *action, wgf_gamepad_button_t negative, wgf_gamepad_button_t positive)
{
    binding_t *b;
    if ((int)negative < WGF_GAMEPAD_BUTTON_SOUTH || (int)negative > WGF_GAMEPAD_BUTTON_DPAD_RIGHT ||
        (int)positive < WGF_GAMEPAD_BUTTON_SOUTH || (int)positive > WGF_GAMEPAD_BUTTON_DPAD_RIGHT ||
        (b = add(action)) == NULL) {
        return false;
    }
    b->kind = PAD_BUTTONS;
    b->a = (int)negative;
    b->b = (int)positive;
    return true;
}

bool wgf_action_bind_pad_axis(const char *action, wgf_gamepad_axis_t axis, int direction, float threshold)
{
    binding_t *b;
    if ((int)axis < WGF_GAMEPAD_AXIS_LEFT_X || (int)axis > WGF_GAMEPAD_AXIS_RIGHT_TRIGGER || direction < -1 ||
        direction > 1 || !(threshold >= 0.0f && threshold <= 1.0f) || (b = add(action)) == NULL) {
        return false;
    }
    b->kind = PAD_AXIS;
    b->a = (int)axis;
    b->direction = direction;
    b->threshold = threshold;
    return true;
}

bool wgf_action_bind_touch(const char *action, float x, float y, float width, float height)
{
    binding_t *b;
    if (!(width > 0.0f && height > 0.0f) || (b = add(action)) == NULL) return false;
    b->kind = TOUCH;
    b->rect[0] = x;
    b->rect[1] = y;
    b->rect[2] = width;
    b->rect[3] = height;
    return true;
}

/* Whether any action's binding reads stick `index`: one whose actions were all cleared is
 * free for the next stick bound. */
static bool stick_bound(int index)
{
    int i, k;
    for (i = 0; i < table.count; i++) {
        for (k = 0; k < table.actions[i].binding_count; k++) {
            const binding_t *b = &table.actions[i].bindings[k];
            if (b->kind == STICK && b->stick == index) return true;
        }
    }
    return false;
}

/* The slot a stick for `action_x` takes: the free one that was its own (cleared and bound
 * again, as a game does as the screen changes: a finger holding it keeps it), else any free
 * one, else a new one; -1 when all are bound. */
static int stick_slot(const char *action_x)
{
    int i, free_slot = -1;
    for (i = 0; i < sticks.count; i++) {
        if (stick_bound(i)) continue;
        if (strcmp(sticks.sticks[i].action_x, action_x) == 0) return i;
        if (free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        sticks.sticks[free_slot].finger = -1;
        return free_slot;
    }
    if (sticks.count >= STICKS_MAX) return -1;
    sticks.sticks[sticks.count].finger = -1;
    return sticks.count++;
}

bool wgf_action_bind_touch_stick(const char *action_x, const char *action_y, float x, float y, float width,
                                 float height, float radius)
{
    binding_t *bx, *by;
    stick_t *s;
    int slot;
    if (!(width > 0.0f && height > 0.0f && radius > 0.0f) || action_x == NULL || action_y == NULL ||
        strcmp(action_x, action_y) == 0 || (slot = stick_slot(action_x)) < 0 || (bx = add(action_x)) == NULL) {
        return false;
    }
    if ((by = add(action_y)) == NULL) {
        find(action_x)->binding_count--;
        return false;
    }
    s = &sticks.sticks[slot];
    s->rect[0] = x, s->rect[1] = y, s->rect[2] = width, s->rect[3] = height;
    s->radius = radius;
    memset(s->action_x, 0, sizeof(s->action_x));
    strncpy(s->action_x, action_x, NAME_MAX - 1);
    bx->kind = by->kind = STICK;
    bx->stick = by->stick = slot;
    bx->part = 0;
    by->part = 1;
    return true;
}

/* The bound stick whose across action is `action_x`, as its finger has it now; NULL for
 * none. */
static stick_t *stick_of(const char *action_x)
{
    int i;
    for (i = 0; action_x != NULL && i < sticks.count; i++) {
        if (strcmp(sticks.sticks[i].action_x, action_x) == 0 && stick_bound(i)) return stick_now(i);
    }
    return NULL;
}

bool wgf_action_is_stick_held(const char *action_x)
{
    const stick_t *s = stick_of(action_x);
    return s != NULL && s->finger >= 0;
}

wgf_vec2_t wgf_action_get_stick_origin(const char *action_x)
{
    const stick_t *s = stick_of(action_x);
    return s != NULL && s->finger >= 0 ? wgf_vec2_make(s->origin[0], s->origin[1]) : wgf_vec2_make(0.0f, 0.0f);
}

wgf_vec2_t wgf_action_get_stick_knob(const char *action_x)
{
    const stick_t *s = stick_of(action_x);
    float x, y;
    if (s == NULL || s->finger < 0) return wgf_vec2_make(0.0f, 0.0f);
    stick_pull(s, &x, &y);
    return wgf_vec2_make(s->origin[0] + x * s->radius, s->origin[1] - y * s->radius);
}

bool wgf_action_clear(const char *action)
{
    action_t *a = find(action);
    if (a == NULL) return false;
    a->binding_count = 0;
    return true;
}

/* ---- listing ----------------------------------------------------------------------- */

int wgf_action_get_count(void)
{
    return table.count;
}

const char *wgf_action_get_name(int index)
{
    return index >= 0 && index < table.count ? table.actions[index].name : "";
}

int wgf_action_get_binding_count(const char *action)
{
    const action_t *a = find(action);
    return a != NULL ? a->binding_count : 0;
}

static const char *key_text(int key, char *out, size_t size)
{
    static const struct {
        int key;
        const char *text;
    } names[] = {{WGF_KEY_SPACE, "Space"},      {WGF_KEY_ESCAPE, "Escape"},   {WGF_KEY_ENTER, "Enter"},
                 {WGF_KEY_TAB, "Tab"},          {WGF_KEY_BACKSPACE, "Backspace"}, {WGF_KEY_INSERT, "Insert"},
                 {WGF_KEY_DELETE, "Delete"},    {WGF_KEY_RIGHT, "Right"},     {WGF_KEY_LEFT, "Left"},
                 {WGF_KEY_DOWN, "Down"},        {WGF_KEY_UP, "Up"},           {WGF_KEY_PAGE_UP, "Page up"},
                 {WGF_KEY_PAGE_DOWN, "Page down"}, {WGF_KEY_HOME, "Home"},    {WGF_KEY_END, "End"},
                 {WGF_KEY_LEFT_SHIFT, "Left shift"}, {WGF_KEY_RIGHT_SHIFT, "Right shift"},
                 {WGF_KEY_LEFT_CONTROL, "Left ctrl"}, {WGF_KEY_RIGHT_CONTROL, "Right ctrl"},
                 {WGF_KEY_LEFT_ALT, "Left alt"}, {WGF_KEY_RIGHT_ALT, "Right alt"}};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (names[i].key == key) return names[i].text;
    }
    if (key >= WGF_KEY_F1 && key <= WGF_KEY_F25) {
        wgf_core_priv_format(out, size, "F%d", key - WGF_KEY_F1 + 1);
    } else if (key >= WGF_KEY_KP_0 && key <= WGF_KEY_KP_9) {
        wgf_core_priv_format(out, size, "Keypad %d", key - WGF_KEY_KP_0);
    } else if (key > 32 && key < 127) {
        wgf_core_priv_format(out, size, "%c", (char)key); /* letters, digits, and the marks: as printed on the key */
    } else {
        wgf_core_priv_format(out, size, "Key %d", key);
    }
    return out;
}

const char *wgf_action_get_binding_text(const char *action, int index)
{
    static const char *const buttons[] = {"Pad south",      "Pad east",       "Pad west",     "Pad north",
                                          "Left bumper",    "Right bumper",   "Left trigger", "Right trigger",
                                          "Pad back",       "Pad start",      "Pad guide",    "Left stick press",
                                          "Right stick press", "D-pad up",    "D-pad down",   "D-pad left",
                                          "D-pad right"};
    static const char *const axes[] = {"Left stick X",  "Left stick Y",  "Right stick X",
                                       "Right stick Y", "Left trigger", "Right trigger"};
    const action_t *a = find(action);
    const binding_t *b;
    char one[24], two[24];
    if (a == NULL || index < 0 || index >= a->binding_count) return "";
    b = &a->bindings[index];
    switch (b->kind) {
        case KEY: wgf_core_priv_format(table.text, sizeof(table.text), "%s", key_text(b->a, one, sizeof(one))); break;
        case KEYS:
            wgf_core_priv_format(table.text, sizeof(table.text), "%s / %s", key_text(b->a, one, sizeof(one)),
                     key_text(b->b, two, sizeof(two)));
            break;
        case PAD_BUTTON: wgf_core_priv_format(table.text, sizeof(table.text), "%s", buttons[b->a]); break;
        case PAD_BUTTONS: wgf_core_priv_format(table.text, sizeof(table.text), "%s / %s", buttons[b->a],
                                               buttons[b->b]); break;
        case PAD_AXIS:
            wgf_core_priv_format(table.text, sizeof(table.text), "%s%s", axes[b->a],
                     b->direction < 0 ? " -" : (b->direction > 0 && b->a < WGF_GAMEPAD_AXIS_LEFT_TRIGGER ? " +" : ""));
            break;
        case STICK: wgf_core_priv_format(table.text, sizeof(table.text), "Touch stick %s",
                                         b->part == 0 ? "X" : "Y"); break;
        default: wgf_core_priv_format(table.text, sizeof(table.text), "Touch"); break;
    }
    return table.text;
}
