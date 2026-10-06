import wgf.*;

/**
	The player's car: the input turned into the vehicle's intent, at the tick rate. The
	vehicle (physics3d, from the scene file's `vehicle` keys) does the engine, gears,
	suspension, and tires; this only says what the driver wants.
**/
class Car extends Script {
	static inline var STEER_RATE = 3.0; // full lock in a third of a second

	var steer = 0.0;
	final smoke:Array<Node> = [];

	override function onCreate() {
		for (wheel in ["wheel_rl", "wheel_rr"])
			smoke.push(entity.getNode().find('$wheel/smoke')); // an emitter3d under each rear wheel
	}

	override function onTick(dt:Float) {
		if (Main.state != Main.State.RACING) {
			(entity : Vehicle).setInput(0, 1, 0, true);
			return;
		}
		final pad = Gamepad.getStick(0, GamepadStick.LEFT).x;
		var want = pad;
		if (Keyboard.isDown(KeyboardKey.LEFT) || Keyboard.isDown(KeyboardKey.A))
			want -= 1;
		if (Keyboard.isDown(KeyboardKey.RIGHT) || Keyboard.isDown(KeyboardKey.D))
			want += 1;
		want = Math.max(-1, Math.min(1, want));
		steer += Math.max(-STEER_RATE * dt, Math.min(STEER_RATE * dt, want - steer)); // keys ease in

		final throttle = Keyboard.isDown(KeyboardKey.UP) ? 1.0 : Gamepad.getTrigger(0, GamepadTrigger.RIGHT);
		final brake = Keyboard.isDown(KeyboardKey.DOWN) ? 1.0 : Gamepad.getTrigger(0, GamepadTrigger.LEFT);
		final handbrake = Keyboard.isDown(KeyboardKey.SPACE) || Gamepad.isDown(0, GamepadButton.SOUTH);
		(entity : Vehicle).setInput(throttle, brake, steer, handbrake);

		// smoke where the rear tires slide
		for (i in 0...smoke.length) {
			final slip = (entity : Vehicle).getWheelSlip(2 + i);
			(smoke[i] : Emitter3d).setRate(slip > 0.3 ? 120 * slip : 0);
		}
	}
}
