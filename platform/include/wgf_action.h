#ifndef WGF_ACTION_H
#define WGF_ACTION_H

#include <stdbool.h>

#include "wgf_api.h"
#include "wgf_gamepad.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Input actions: what the player means ("steer", "fire", "pause"), bound by name to
 * keys, pad buttons, pad axes, and touch regions, and read as one, so a game asks for
 * "fire" and never for Space or the south button. An action is made by its first
 * binding; a name is 1 to 31 bytes; at most 64 actions, each with at most 16 bindings.
 * Pads are read across all four: the one pushed furthest wins.
 *
 * Each binding gives a value from -1 to 1:
 *
 *   a key, a pad button   1 while it is down
 *   a pair of keys        -1 while the first is down, 1 the second, 0 both or neither
 *   a pad axis            its value (sticks -1 to 1, past the dead zone; triggers 0 to 1),
 *                         either side (direction 0) or one side only (1 or -1, read as
 *                         0 to 1), below `threshold` as 0
 *   a touch region        1 while a finger is down in it (logical pixels, as the
 *                         pointer's)
 *
 * and the action is the binding pushed furthest: get_axis its value, get_value how far
 * (0 to 1), and down while that is at least 0.5 (a pad axis: at least its threshold, or
 * 0.5 with a threshold of 0). Read inside a tick or a frame, as keys are: pressed and
 * released are since the previous tick, or the previous frame. */

/* Bindings added to `action` (made by the first); false for a name breaking the rule, a
 * 65th action, a 17th binding, a key, button, or axis that isn't one, a direction that
 * isn't -1, 0, or 1, a threshold outside 0 to 1, or a region with no area. */
WGF_API bool wgf_action_bind_key(const char *action, wgf_keyboard_key_t key);
WGF_API bool wgf_action_bind_keys(const char *action, wgf_keyboard_key_t negative, wgf_keyboard_key_t positive);
WGF_API bool wgf_action_bind_pad_button(const char *action, wgf_gamepad_button_t button);
WGF_API bool wgf_action_bind_pad_axis(const char *action, wgf_gamepad_axis_t axis, int direction, float threshold);
WGF_API bool wgf_action_bind_touch(const char *action, float x, float y, float width, float height);

/* Every binding of `action` taken off, the action kept (a remap screen binds it again);
 * false for an action there isn't. */
WGF_API bool wgf_action_clear(const char *action);

/* The action as it is: its axis (-1 to 1), its value (0 to 1), and its state. 0, 0, and
 * UP for an action there isn't. */
WGF_API float wgf_action_get_axis(const char *action);
WGF_API float wgf_action_get_value(const char *action);
WGF_API wgf_input_state_t wgf_action_get_state(const char *action);
WGF_API bool wgf_action_is_down(const char *action);
WGF_API bool wgf_action_is_pressed(const char *action);
WGF_API bool wgf_action_is_released(const char *action);

/* The actions, in the order made, and each one's bindings as text for a help line or a
 * remap screen ("Left / Right", "Space", "Pad south", "Left stick X", "Right trigger",
 * "Touch"): libwgf's, valid until the next call here. "" past the end. */
WGF_API int wgf_action_get_count(void);
WGF_API const char *wgf_action_get_name(int index);
WGF_API int wgf_action_get_binding_count(const char *action);
WGF_API const char *wgf_action_get_binding_text(const char *action, int index);

#ifdef __cplusplus
}
#endif

#endif
