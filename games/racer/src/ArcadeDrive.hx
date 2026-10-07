import wgf.*;

/**
	How the car drives: the one place that turns the driver's intent into movement, so a
	real vehicle (physics3d, when libwgf has it) replaces this class and nothing else.

	For now it is arcade: a speed along the car's heading, pushed by the throttle and
	pulled by drag, the brake, and the grass; a turn rate from the steering that needs
	some speed and tightens less as speed grows. Its output is the actor's motion
	(wgf_motion.h: the ecs moves the car by its velocity, at the tick rate, drawn smoothly
	between ticks) and its heading, set as the actor's rotation about y. The heading is
	kept here, never read back from the actor: get_rotation's angles past a half turn are
	other angles for the same rotation.
**/
class ArcadeDrive {
	static inline var ACCEL = 14.0; // m/s², at full throttle
	static inline var BRAKE = 30.0;
	static inline var REVERSE = 7.0; // the most speed backwards
	static inline var TOP = 44.0; // on asphalt, m/s (about 160 km/h)
	static inline var TOP_GRASS = 16.0;
	static inline var DRAG = 0.12; // the part of its speed it loses a second, rolling
	static inline var GRASS_DRAG = 1.4;
	static inline var TURN = 1.9; // radians a second at full lock, once moving
	static inline var HANDBRAKE_TURN = 1.5; // the turn's multiple with the handbrake on

	public var heading(default, null) = 0.0;
	public var speed(default, null) = 0.0; // along the heading, m/s
	public var onGrass(default, null) = false;

	var trackHint = 0;

	public function new() {}

	/** Put the car at (x, z), turned to `heading`, at rest. **/
	public function place(actor:Actor, x:Float, z:Float, heading:Float) {
		this.heading = heading;
		speed = 0;
		actor.setTransform(x, 0, z, 0, heading, 0, 1, 1, 1);
		(actor : Motion).setVelocity(0, 0, 0);
		actor.snap();
		trackHint = Track.nearest(x, z, 0);
	}

	/** One tick held still: the grid, before the start. **/
	public function hold(actor:Actor) {
		speed = 0;
		actor.setRotation(0, heading, 0);
		(actor : Motion).setVelocity(0, 0, 0);
	}

	/**
		One tick of driving: throttle and brake 0 to 1, steer -1 (left) to 1 (right), the
		handbrake held or not.
	**/
	public function update(actor:Actor, throttle:Float, brake:Float, steer:Float, handbrake:Bool, dt:Float) {
		final at = actor.getPosition(position);
		trackHint = Track.nearest(at.x, at.z, trackHint);
		onGrass = Math.abs(Track.offset(at.x, at.z, trackHint)) > Track.WIDTH / 2 + 1;

		// along: throttle, brake (then reverse), drag, the grass
		if (brake > 0 && speed > 0.5)
			speed -= BRAKE * brake * dt;
		else if (brake > 0)
			speed -= ACCEL * 0.6 * brake * dt;
		speed += ACCEL * throttle * dt;
		if (handbrake)
			speed -= speed * 1.5 * dt;
		speed -= speed * (onGrass ? GRASS_DRAG : DRAG) * dt;
		final top = onGrass ? TOP_GRASS : TOP;
		if (speed > top)
			speed -= Math.min(speed - top, 30 * dt); // eased down to it, off the asphalt
		if (speed < -REVERSE)
			speed = -REVERSE;
		if (throttle == 0 && brake == 0 && Math.abs(speed) < 0.3)
			speed = 0;

		// across: a turn that needs speed, and tightens less near the top
		final moving = Math.min(Math.abs(speed) / 8, 1);
		final high = 1 - 0.4 * Math.min(Math.abs(speed) / TOP, 1);
		var rate = -steer * TURN * moving * high * (handbrake ? HANDBRAKE_TURN : 1);
		if (speed < 0)
			rate = -rate;
		heading += rate * dt;
		if (heading > Math.PI)
			heading -= 2 * Math.PI;
		else if (heading < -Math.PI)
			heading += 2 * Math.PI;

		actor.setRotation(0, heading, 0);
		(actor : Motion).setVelocity(Math.sin(heading) * speed, 0, Math.cos(heading) * speed);
	}

	var position = new Vec3(); // not final: wgf serve (FRICTION.md)
}
