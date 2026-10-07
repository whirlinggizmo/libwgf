import wgf.*;

/**
	The player's ship: its behavior turns the input into intent -- a spin to turn, thrust
	added to its velocity, a bullet fired -- and the ecs does the rest (its motion's
	damping and top speed, wrapping at the screen's edge). A new ship can't be hit for its
	first two seconds (its collider switched off, so it meets nothing from either side),
	and blinks meanwhile.
**/
class Ship extends Behavior {
	static inline var TURN = 4.2; // radians a second
	static inline var THRUST = 420.0; // units a second, a second
	static inline var BULLET_SPEED = 560.0;
	static inline var FIRE_EVERY = 0.16;
	static inline var BULLETS_MAX = 6;
	static inline var SAFE_FOR = 2.0;

	var cooldown = 0.0;
	var safe = SAFE_FOR;
	var thrusting = false;
	final heading = new Vec3();
	final velocity = new Vec3();
	var flame:Emitter2d;

	override function onCreate() {
		(actor : Collider).setEnabled(false); // safe while it blinks
		flame = actor.find("emitter2d"); // its part, found once
	}

	override function onDestroy() {
		Main.sounds.thrust(false);
	}

	override function onTick(dt:Float) {
		if (Main.state != Main.State.PLAYING)
			return;
		if (safe > 0) {
			safe -= dt;
			actor.setVisible(safe <= 0 || Std.int(safe * 8) % 2 == 0); // the ship is its shape
			if (safe <= 0)
				(actor : Collider).setEnabled(true); // rocks again
		}
		final stick = Gamepad.getStick(0, GamepadStick.LEFT).x;
		var turn = 0.0;
		if (Keyboard.isDown(KeyboardKey.LEFT) || Keyboard.isDown(KeyboardKey.A) || Gamepad.isDown(0, GamepadButton.DPAD_LEFT) || stick < -0.4)
			turn -= 1;
		if (Keyboard.isDown(KeyboardKey.RIGHT) || Keyboard.isDown(KeyboardKey.D) || Gamepad.isDown(0, GamepadButton.DPAD_RIGHT) || stick > 0.4)
			turn += 1;
		(actor : Motion).setSpin(0, 0, turn * TURN);

		final angle = actor.getRotation(heading).z;
		final dx = Math.cos(angle), dy = Math.sin(angle);
		final thrust = Keyboard.isDown(KeyboardKey.UP) || Keyboard.isDown(KeyboardKey.W) || Gamepad.isDown(0, GamepadButton.SOUTH)
			|| Gamepad.getTrigger(0, GamepadTrigger.RIGHT) > 0.3;
		if (thrust) {
			final v = (actor : Motion).getVelocity(velocity);
			(actor : Motion).setVelocity(v.x + dx * THRUST * dt, v.y + dy * THRUST * dt, 0);
		}
		if (thrust != thrusting) {
			thrusting = thrust;
			Main.sounds.thrust(thrust);
			flame.setEmitting(thrust);
		}

		cooldown -= dt;
		final fire = Keyboard.isDown(KeyboardKey.SPACE) || Gamepad.isDown(0, GamepadButton.EAST)
			|| Gamepad.isDown(0, GamepadButton.RIGHT_BUMPER);
		if (fire && cooldown <= 0 && Ecs.countBehavior("Bullet") < BULLETS_MAX) {
			cooldown = FIRE_EVERY;
			final at = actor.getPosition(heading);
			final v = (actor : Motion).getVelocity(velocity);
			Bullet.fire(at.x + dx * 18, at.y + dy * 18, v.x + dx * BULLET_SPEED, v.y + dy * BULLET_SPEED);
		}
	}

	/** Hit by a rock: an explosion where it was, and a life lost. **/
	public function explode() {
		final at = actor.getPosition(heading);
		Rock.explosion(at.x, at.y, 2);
		Main.sounds.play(Main.sounds.bangLarge);
		actor.destroy(ActorDestroy.DESTROY_CHILDREN);
		Main.shipLost();
	}

	/** The ship's outline at (x, y), pointing up: the HUD's lives. **/
	public static function drawIcon(x:Float, y:Float) {
		Draw.polyline([x, y - 12, x + 8, y + 10, x, y + 5, x - 8, y + 10], true, 2, Color.get(ColorStock.SKYBLUE));
	}
}
