import wgf.*;

/**
	The player's ship: its behavior turns the input into intent -- a spin to turn, thrust
	added to its velocity, a bullet fired -- and the ecs does the rest (its motion's
	damping and top speed, wrapping at the screen's edge). A new ship can't be hit for its
	first two seconds (its collider meets nothing), and blinks meanwhile.
**/
class Ship extends Behavior {
	static inline var TURN = 4.2; // radians a second
	static inline var THRUST = 420.0; // units a second, a second
	static inline var BULLET_SPEED = 560.0;
	static inline var FIRE_EVERY = 0.16;
	static inline var BULLETS_MAX = 6;
	static inline var SAFE_FOR = 2.0;

	public static var current:Null<Ship>;

	var cooldown = 0.0;
	var safe = SAFE_FOR;
	var thrusting = false;
	final heading = new Vec3();
	final velocity = new Vec3();

	override function onCreate() {
		current = this;
		(entity : Collider).setMask(0); // safe while it blinks
	}

	override function onDestroy() {
		if (current == this)
			current = null;
		Main.sounds.thrust(false);
	}

	override function onTick(dt:Float) {
		if (Main.state != Main.State.PLAYING)
			return;
		if (safe > 0) {
			safe -= dt;
			final shape:Node = entity.getComponentNode(Component.SHAPE2D);
			shape.setVisible(safe <= 0 || Std.int(safe * 8) % 2 == 0);
			if (safe <= 0)
				(entity : Collider).setMask(2); // rocks again
		}
		final stick = Gamepad.getStick(0, GamepadStick.LEFT).x;
		var turn = 0.0;
		if (Keyboard.isDown(KeyboardKey.LEFT) || Keyboard.isDown(KeyboardKey.A) || Gamepad.isDown(0, GamepadButton.DPAD_LEFT) || stick < -0.4)
			turn -= 1;
		if (Keyboard.isDown(KeyboardKey.RIGHT) || Keyboard.isDown(KeyboardKey.D) || Gamepad.isDown(0, GamepadButton.DPAD_RIGHT) || stick > 0.4)
			turn += 1;
		(entity : Motion).setSpin(0, 0, turn * TURN);

		final angle = entity.getRotation(heading).z;
		final dx = Math.cos(angle), dy = Math.sin(angle);
		final thrust = Keyboard.isDown(KeyboardKey.UP) || Keyboard.isDown(KeyboardKey.W) || Gamepad.isDown(0, GamepadButton.SOUTH)
			|| Gamepad.getTrigger(0, GamepadTrigger.RIGHT) > 0.3;
		if (thrust) {
			final v = (entity : Motion).getVelocity(velocity);
			(entity : Motion).setVelocity(v.x + dx * THRUST * dt, v.y + dy * THRUST * dt, 0);
		}
		if (thrust != thrusting) {
			thrusting = thrust;
			Main.sounds.thrust(thrust);
			final flame:Node = entity.getComponentNode(Component.EMITTER2D);
			(flame : Emitter2d).setEmitting(thrust);
		}

		cooldown -= dt;
		final fire = Keyboard.isDown(KeyboardKey.SPACE) || Gamepad.isDown(0, GamepadButton.EAST)
			|| Gamepad.isDown(0, GamepadButton.RIGHT_BUMPER);
		if (fire && cooldown <= 0 && Ecs.countBehavior("Bullet") < BULLETS_MAX) {
			cooldown = FIRE_EVERY;
			final at = entity.getPosition(heading);
			final v = (entity : Motion).getVelocity(velocity);
			Bullet.fire(at.x + dx * 18, at.y + dy * 18, v.x + dx * BULLET_SPEED, v.y + dy * BULLET_SPEED);
		}
	}

	/** Hit by a rock: an explosion where it was, and a life lost. **/
	public function explode() {
		final at = entity.getPosition(heading);
		Rock.explosion(at.x, at.y, 2);
		Main.sounds.play(Main.sounds.bangLarge);
		entity.destroy();
		Main.shipLost();
	}

	/** The ship's outline at (x, y), pointing up: the HUD's lives. **/
	public static function drawIcon(x:Float, y:Float) {
		Draw.polyline([x, y - 12, x + 8, y + 10, x, y + 5, x - 8, y + 10], true, 2, Color.get(ColorStock.SKYBLUE));
	}
}
