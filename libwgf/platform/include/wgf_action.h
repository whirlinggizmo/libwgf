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

/* Input actions: what the player means, such as "steer", "fire", or "pause".
 *
 * An action is bound by name to keys, pad buttons, pad axes, and touch regions, and they are
 * read as one. A game asks for "fire", never for Space or the south button.
 *
 * - An action is made by its first binding.
 * - A name is 1 to 31 bytes.
 * - There are at most 64 actions, each with at most 16 bindings.
 * - Pads are read across all four. The one pushed furthest wins.
 *
 * Each binding gives a value from -1 to 1:
 *
 *   a key, a pad button   1 while it is down
 *   a pair of keys        -1 while the first is down, 1 while the second is, 0 for both or
 *                         neither
 *   a pair of pad buttons the same, across the pads (the d-pad's left and right as a
 *                         steering axis)
 *   a pad axis            its value (sticks -1 to 1, past the dead zone; triggers 0 to 1),
 *                         either side (direction 0) or one side only (1 or -1, read as
 *                         0 to 1); a value below `threshold` reads as 0
 *   a touch region        1 while a finger is down in it (logical pixels, as the
 *                         pointer's are)
 *   an on-screen stick    its pull across or up, -1 to 1 (see wgf_action_bind_touch_stick)
 *
 * The action takes the value of the binding pushed furthest:
 * - get_axis returns that value.
 * - get_value returns how far it is pushed (0 to 1).
 * - The action is down while that is at least 0.5. For a pad axis, it is down at its
 *   threshold or more, or at 0.5 or more with a threshold of 0.
 *
 * Read actions inside a tick or a frame, as keys are. Pressed and released mean since the
 * previous tick, or the previous frame. */

/* Adds a binding to `action`, making the action with its first binding.
 *
 * Returns false for any of these:
 * - a name that breaks the rule above
 * - a 65th action, or a 17th binding
 * - a key, button, or axis that isn't one
 * - a direction that isn't -1, 0, or 1
 * - a threshold outside 0 to 1
 * - a region with no area */
WGF_API bool wgf_action_bind_key(const char *action, wgf_keyboard_key_t key);
WGF_API bool wgf_action_bind_keys(const char *action, wgf_keyboard_key_t negative, wgf_keyboard_key_t positive);
WGF_API bool wgf_action_bind_pad_button(const char *action, wgf_gamepad_button_t button);
WGF_API bool wgf_action_bind_pad_buttons(const char *action, wgf_gamepad_button_t negative,
                                         wgf_gamepad_button_t positive);
WGF_API bool wgf_action_bind_pad_axis(const char *action, wgf_gamepad_axis_t axis, int direction, float threshold);
WGF_API bool wgf_action_bind_touch(const char *action, float x, float y, float width, float height);
/* Binds an on-screen stick to `action_x` (across) and `action_y` (up).
 *
 * A finger landing in the region (logical pixels) makes a stick where it landed. Its pull, up
 * to `radius` away, is read as `action_x` and `action_y`, each -1 to 1 and held within the
 * circle. That is how a pad's left stick bound to them reads. The stick ends when the finger
 * lifts.
 *
 * - At most 4 sticks are bound at once.
 * - A stick whose actions are cleared (wgf_action_clear) is gone.
 * - Bound again, as a game does as the screen changes, it is the same stick. A finger
 *   holding it keeps holding it.
 *
 * Returns false for two actions alike, a region with no area, a radius not above 0, or a
 * fifth stick. It is also refused for the reasons the other bindings are.
 *
 * For drawing the stick, the getters take its across action:
 * - is_stick_held: whether a finger holds it.
 * - get_stick_origin: where the finger landed.
 * - get_stick_knob: where its knob is.
 * Positions are in logical pixels, and 0, 0 while no finger holds it. */
WGF_API bool wgf_action_bind_touch_stick(const char *action_x, const char *action_y, float x, float y, float width,
                                         float height, float radius);
WGF_API bool wgf_action_is_stick_held(const char *action_x);
WGF_API wgf_vec2_t wgf_action_get_stick_origin(const char *action_x);
WGF_API wgf_vec2_t wgf_action_get_stick_knob(const char *action_x);

/* Removes every binding of `action` and keeps the action, so a remap screen can bind it
 * again.
 *
 * Returns false if there is no such action. */
WGF_API bool wgf_action_clear(const char *action);

/* Returns the action as it is now: its axis (-1 to 1), its value (0 to 1), and its state.
 *
 * Returns 0, 0, and UP for an action that doesn't exist. */
WGF_API float wgf_action_get_axis(const char *action);
WGF_API float wgf_action_get_value(const char *action);
WGF_API wgf_input_state_t wgf_action_get_state(const char *action);
WGF_API bool wgf_action_is_down(const char *action);
WGF_API bool wgf_action_is_pressed(const char *action);
WGF_API bool wgf_action_is_released(const char *action);

/* Lists the actions, in the order they were made, and each one's bindings as text.
 *
 * The text is for a help line or a remap screen, such as "Left / Right", "Space", "Pad south",
 * "Left stick X", "Right trigger", or "Touch". libwgf owns the strings, which are valid until
 * the next call here. Returns "" past the end. */
WGF_API int wgf_action_get_count(void);
WGF_API const char *wgf_action_get_name(int index);
WGF_API int wgf_action_get_binding_count(const char *action);
WGF_API const char *wgf_action_get_binding_text(const char *action, int index);

#ifdef __cplusplus
}
#endif

#endif
