import wgf.*;

/**
	A camera behind and above the car, looking a little ahead of where the car is going.
	Its yaw follows the car's heading through a critically damped spring, so it swings
	wide into a corner and settles behind on a straight; its distance stays fixed, so it
	never falls behind at speed (a spring on its position would trail by twice the speed
	over its frequency). Per frame, from where the car is drawn (Car.drawn), so it is
	smooth at any frame rate.
**/
class ChaseCamera {
	static inline var BEHIND = 8.5;
	static inline var ABOVE = 3.4;
	static inline var AHEAD = 6.0;
	static inline var STIFFNESS = 36.0;

	public final camera:Camera3d;

	var yaw = 0.0;
	var yawVelocity = 0.0;

	public function new(stage:Stage3d) {
		camera = Camera3d.create();
		camera.setFov(65 * Math.PI / 180);
		camera.setClip(0.1, 800);
		stage.setCamera(camera);
	}

	/** Behind the car at once, no spring: the race's start. **/
	public function snap(car:Car) {
		yaw = car.drawnHeading;
		yawVelocity = 0;
		place(car);
	}

	public function update(car:Car, dt:Float) {
		// the spring stepped exactly (critically damped), on the shortest way round
		var d = yaw - car.drawnHeading;
		while (d > Math.PI)
			d -= 2 * Math.PI;
		while (d < -Math.PI)
			d += 2 * Math.PI;
		final w = Math.sqrt(STIFFNESS), t = Math.min(dt, 0.1);
		final e = Math.exp(-w * t), c = yawVelocity + w * d;
		yaw = car.drawnHeading + (d + c * t) * e;
		yawVelocity = (c - w * (d + c * t)) * e;
		place(car);
	}

	function place(car:Car) {
		final p = car.drawn;
		final bx = Math.sin(yaw), bz = Math.cos(yaw);
		camera.setPosition(p.x - bx * BEHIND, p.y + ABOVE, p.z - bz * BEHIND);
		final fx = Math.sin(car.drawnHeading), fz = Math.cos(car.drawnHeading);
		camera.lookAt(p.x + fx * AHEAD, p.y + 0.8, p.z + fz * AHEAD, 0, 1, 0);
	}
}
