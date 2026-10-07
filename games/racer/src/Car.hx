import wgf.*;

/**
	The player's car: the input, as named actions (bound once, in Main.bindControls), turned
	into the drive's intent at the tick rate. How that moves the car is ArcadeDrive's alone.

	It also keeps what the chase camera needs, which the actor doesn't give: where the car
	is drawn this frame. A simulated actor's transform is the last tick's; it is drawn
	between the transform kept as that tick began and that one, at the tick fraction, so
	this keeps the same two and blends them the same way.
**/
class Car extends Behavior {
	static inline var STEER_RATE = 4.0; // full lock in a quarter of a second, from keys

	// a var, not a final: `wgf serve`'s hot build refuses an instance final with an initializer (FRICTION.md)
	public var drive = new ArcadeDrive();

	public var steer(default, null) = 0.0;

	// the transform as the last tick began (the ecs's kept one), for drawing between ticks
	var kept = new Vec3();
	var keptHeading = 0.0;
	var now = new Vec3();

	/** Where the car is drawn this frame, and its heading there. **/
	public var drawn = new Vec3();
	public var drawnHeading = 0.0;

	override function onCreate() {
		Main.carReady(this);
	}

	override function onTick(dt:Float) {
		actor.getPosition(kept);
		keptHeading = drive.heading;
		if (Main.state == Main.State.RESULTS) { // past the line: coasting to a stop
			drive.update(actor, 0, 0.4, 0, false, dt);
			return;
		}
		if (Main.state != Main.State.RACING) {
			drive.hold(actor);
			return;
		}
		final want = Action.getAxis("steer"); // -1 to 1
		steer += Math.max(-STEER_RATE * dt, Math.min(STEER_RATE * dt, want - steer)); // keys ease in
		drive.update(actor, Action.getValue("throttle"), Action.getValue("brake"), steer, Action.isDown("handbrake"), dt);
	}

	override function onFrame(dt:Float) {
		final t = Loop.getTickFraction();
		actor.getPosition(now);
		drawn.set(kept.x + (now.x - kept.x) * t, kept.y + (now.y - kept.y) * t, kept.z + (now.z - kept.z) * t);
		var turn = drive.heading - keptHeading;
		if (turn > Math.PI)
			turn -= 2 * Math.PI;
		else if (turn < -Math.PI)
			turn += 2 * Math.PI;
		drawnHeading = keptHeading + turn * t;
	}

	/** Back to `x, z`, turned to `heading`, at rest: the grid. **/
	public function reset(x:Float, z:Float, heading:Float) {
		drive.place(actor, x, z, heading);
		steer = 0;
		actor.getPosition(kept);
		keptHeading = heading;
		drawn.set(kept.x, kept.y, kept.z);
		drawnHeading = heading;
	}
}
