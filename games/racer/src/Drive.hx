import wgf.*;

/**
	How the car drives: the one place that turns the driver's intent into the vehicle's.
	The car is a Jolt wheeled vehicle (its body and vehicle lines in racer.scene: engine,
	gears, springs, tires); this only sets its input each tick and reads back what it does.
	The driver aids an arcade racer has are here too: steering that narrows with speed, and
	grass that drags. It took the place of the arcade model (motion and a kept heading)
	without Car, Main, or the camera changing more than their reads.
**/
class Drive {
	public var speed(get, never):Float; // along the car's +z, m/s
	public var heading(default, null) = 0.0; // yaw: the car's +z as (sin, cos) on x and z
	public var onGrass(default, null) = false;

	static inline var LOCK_SPEED = 19.0; // m/s: full lock below, less above

	final car:Actor;
	var trackHint = 0;
	final position = new Vec3();
	final ahead = new Vec3();

	public function new(car:Actor) {
		this.car = car;
	}

	inline function get_speed():Float
		return (car : Vehicle).getSpeed();

	/** Put the car at (x, z), turned to `heading`, at rest on its wheels. **/
	public function place(x:Float, z:Float, heading:Float) {
		car.setTransform(x, 0.6, z, 0, heading, 0, 1, 1, 1);
		(car : Vehicle).reset();
		car.snap();
		this.heading = heading;
		trackHint = Track.nearest(x, z, 0);
	}

	/** Held: brakes and the hand brake on, no throttle (the grid, before the start). **/
	public function hold() {
		(car : Vehicle).setInput(0, 1, 0, true);
		observe();
	}

	/**
		One tick of driving: throttle and brake 0 to 1, steer -1 (left) to 1 (right), the
		hand brake held or not. The brake, once stopped, backs up.
	**/
	public function update(throttle:Float, brake:Float, steer:Float, handbrake:Bool) {
		final s = speed;
		// the grass drags: a light brake, and less of the throttle (a soft ground's rolling
		// resistance, which a friction alone doesn't give: it only takes the grip)
		if (onGrass && s > 8) {
			throttle *= 0.5;
			brake = Math.max(brake, 0.12);
		}
		// steering that narrows with speed: past LOCK_SPEED, the lock that turns the car about as
		// hard as its tires hold (wheelbase x grip / speed²), with some to spare, so full lock
		// from a key is a hard corner, not a spin
		final lock = Math.min(1, LOCK_SPEED * LOCK_SPEED / Math.max(s * s, 1));
		steer *= lock;
		if (brake > 0 && s < 0.5 && throttle == 0)
			(car : Vehicle).setInput(-brake, 0, steer, handbrake);
		else
			(car : Vehicle).setInput(throttle, s < 0.5 ? 0 : brake, steer, handbrake);
		observe();
	}

	/** Where it is on the track, and which way it points: the simulation's, this tick. **/
	function observe() {
		final at = car.getWorldPosition(position);
		trackHint = Track.nearest(at.x, at.z, trackHint);
		onGrass = Math.abs(Track.offset(at.x, at.z, trackHint)) > Track.WIDTH / 2 + 1;
		final d = car.getWorldDirection(0, 0, 1, ahead);
		heading = Math.atan2(d.x, d.z);
	}
}
