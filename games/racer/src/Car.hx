import wgf.*;

/**
	The player's car: the input, as named actions (bound once, in Main.bindControls), turned
	into the drive's intent at the tick rate. How that moves the car is Drive's (the
	vehicle's) alone.
**/
class Car extends Behavior {
	static inline var STEER_RATE = 4.0; // full lock in a quarter of a second, from keys

	public var drive:Drive;
	public var steer(default, null) = 0.0;

	/** Where the car is drawn this frame, and its heading there: what the camera follows. **/
	public final drawn = new Vec3();
	public var drawnHeading = 0.0;

	final ahead = new Vec3();

	override function onCreate() {
		drive = new Drive(actor);
		Main.carReady(this);
	}

	override function onTick(dt:Float) {
		if (Main.state == Main.State.RESULTS) { // past the line: coasting to a stop
			drive.update(0, 0.3, 0, false);
			return;
		}
		if (Main.state != Main.State.RACING) {
			drive.hold();
			return;
		}
		final want = Action.getAxis("steer"); // -1 to 1
		steer += Math.max(-STEER_RATE * dt, Math.min(STEER_RATE * dt, want - steer)); // keys ease in
		drive.update(Action.getValue("throttle"), Action.getValue("brake"), steer, Action.isDown("handbrake"));
	}

	override function onFrame(dt:Float) {
		actor.getDrawnPosition(drawn);
		final d = actor.getDrawnDirection(0, 0, 1, ahead);
		drawnHeading = Math.atan2(d.x, d.z);
	}

	/** Back to `x, z`, turned to `heading`, at rest: the grid. **/
	public function reset(x:Float, z:Float, heading:Float) {
		drive.place(x, z, heading);
		steer = 0;
		actor.getPosition(drawn);
		drawnHeading = heading;
	}
}
