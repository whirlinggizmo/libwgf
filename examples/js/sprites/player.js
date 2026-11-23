// The player: a sprite moved by input actions, never by keys by name.
//
// Its actions are bound once, each to keys and to a pad, and read each frame as one. A remap
// screen would bind them again (Action.clear, then a bind call), and this code would not change.
import { Action, Actor, Color, GamepadAxis, GamepadButton, KeyboardKey, Sprite } from "./wgf-typed.js";

const SPEED = 260; // pixels a second
const MARGIN = 48; // how close to the edge it may go, in pixels

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

// Binds the player's three actions, each to keys and to a pad.
export function bindActions() {
    // An axis from -1 to 1: the first key or button of a pair is -1, the second is 1.
    Action.bindKeys("move_x", KeyboardKey.LEFT, KeyboardKey.RIGHT);
    Action.bindKeys("move_x", KeyboardKey.A, KeyboardKey.D);
    Action.bindPadButtons("move_x", GamepadButton.DPAD_LEFT, GamepadButton.DPAD_RIGHT);
    Action.bindPadAxis("move_x", GamepadAxis.LEFT_X, 0, 0.2); // both sides, a little dead zone
    // y grows down the screen, so up is the negative side.
    Action.bindKeys("move_y", KeyboardKey.UP, KeyboardKey.DOWN);
    Action.bindKeys("move_y", KeyboardKey.W, KeyboardKey.S);
    Action.bindPadButtons("move_y", GamepadButton.DPAD_UP, GamepadButton.DPAD_DOWN);
    Action.bindPadAxis("move_y", GamepadAxis.LEFT_Y, 0, 0.2);
    Action.bindKey("fire", KeyboardKey.SPACE);
    Action.bindPadButton("fire", GamepadButton.SOUTH);
}

// The player as a plain object: its sprite, where it is, whether it is drawn on top, and the
// area it stays in (width by height, in pixels).
export function makePlayer(sprite, x, y, width, height) {
    Sprite.setTint(sprite, Color.make(255, 140, 40, 255)); // a tint multiplies the white texture
    Actor.setPosition(sprite, x, y, 0);
    return { sprite, x, y, width, height, front: true };
}

export function updatePlayer(player, dt) {
    const dx = Action.getAxis("move_x"), dy = Action.getAxis("move_y");
    player.x = clamp(player.x + dx * SPEED * dt, MARGIN, player.width - MARGIN);
    player.y = clamp(player.y + dy * SPEED * dt, MARGIN, player.height - MARGIN);
    Actor.setPosition(player.sprite, player.x, player.y, 0);
    Actor.setRotation(player.sprite, 0, 0, dx * 0.35); // leans into the move, in radians
    // Pressed is true for the one frame the action went down.
    if (Action.isPressed("fire")) {
        player.front = !player.front;
        // The last of its parent's children is drawn over the rest.
        if (player.front) Actor.moveToFront(player.sprite);
        else Actor.moveToBack(player.sprite);
    }
}
