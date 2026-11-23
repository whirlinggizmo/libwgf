import wgf.*;

/**
	The player: a sprite moved by input actions, never by keys by name.

	Its actions are bound once, each to keys and to a pad, and read each frame as one.
	A remap screen would bind them again (Action.clear, then a bind call), and this code
	would not change.
**/
class Player {
	static inline var SPEED = 260.0; // pixels a second
	static inline var MARGIN = 48.0; // how close to the edge it may go, in pixels

	public var x(default, null):Float;
	public var y(default, null):Float;
	public var front(default, null) = true;

	final sprite:Sprite;

	/** Binds the player's three actions, each to keys and to a pad. **/
	public static function bindActions() {
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

	public function new(sprite:Sprite, x:Float, y:Float) {
		this.sprite = sprite;
		this.x = x;
		this.y = y;
		sprite.setTint(Color.make(255, 140, 40, 255)); // a tint multiplies the white texture
		sprite.setPosition(x, y, 0);
	}

	public function update(dt:Float) {
		final dx = Action.getAxis("move_x"), dy = Action.getAxis("move_y");
		x = clamp(x + dx * SPEED * dt, MARGIN, Main.WIDTH - MARGIN);
		y = clamp(y + dy * SPEED * dt, MARGIN, Main.HEIGHT - MARGIN);
		sprite.setPosition(x, y, 0);
		sprite.setRotation(0, 0, dx * 0.35); // leans into the move, in radians
		// Pressed is true for the one frame the action went down.
		if (Action.isPressed("fire")) {
			front = !front;
			// The last of its parent's children is drawn over the rest.
			if (front)
				sprite.moveToFront();
			else
				sprite.moveToBack();
		}
	}

	static inline function clamp(v:Float, lo:Float, hi:Float):Float
		return v < lo ? lo : v > hi ? hi : v;
}
