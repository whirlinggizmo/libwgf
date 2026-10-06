import wgf.*;

/**
	The player's car: the input turned into the vehicle's intent, at the tick rate. The
	vehicle (physics3d, from the scene file's `vehicle` keys) does the engine, gears,
	suspension, and tires; this only says what the driver wants.
**/
class Car extends Behavior {
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
		// the player's controls are named actions (bound once, in Main.bindControls), read
		// across the keyboard and every pad; their bindings make the title's help text
		final want = Action.getAxis("steer"); // -1 to 1
		steer += Math.max(-STEER_RATE * dt, Math.min(STEER_RATE * dt, want - steer)); // keys ease in
		(entity : Vehicle).setInput(Action.getValue("throttle"), Action.getValue("brake"), steer,
			Action.isDown("handbrake"));

		// smoke where the rear tires slide
		for (i in 0...smoke.length) {
			final slip = (entity : Vehicle).getWheelSlip(2 + i);
			(smoke[i] : Emitter3d).setRate(slip > 0.3 ? 120 * slip : 0);
		}
	}
}
