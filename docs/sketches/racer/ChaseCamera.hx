import wgf.*;

/**
	A camera behind and above the car, pulled toward that point by a critically damped
	spring, looking a little ahead of where the car is going. Per frame, from the car's
	interpolated node, so it is smooth at any frame rate.
**/
class ChaseCamera {
	static inline var BEHIND = 6.5;
	static inline var ABOVE = 2.2;
	static inline var AHEAD = 4.0;
	static inline var STIFFNESS = 8.0;

	final camera:Camera3d;
	var target:Entity = 0;
	final at = new Vec3();
	final velocity = new Vec3();
	final world = new Vec3();

	public function new(stage:Stage) {
		camera = Camera3d.create();
		camera.setFov(65);
		camera.setClip(0.1, 800);
		stage.setCamera(camera);
	}

	public function follow(car:Entity) {
		target = car;
		place(car.getNode().getWorldPosition(world), true);
	}

	public function update(dt:Float) {
		if (target.isNone())
			return;
		place(target.getNode().getWorldPosition(world), false, dt);
	}

	function place(car:Vec3, snap:Bool, dt = 0.0) {
		final heading = target.getNode().getRotation().y;
		final wantX = car.x - Math.sin(heading) * BEHIND;
		final wantY = car.y + ABOVE;
		final wantZ = car.z - Math.cos(heading) * BEHIND;
		if (snap) {
			at.set(wantX, wantY, wantZ);
			velocity.set(0, 0, 0);
		} else {
			spring(dt, wantX, wantY, wantZ); // x, y, and z each a damped spring toward the point
		}
		camera.setPosition(at.x, at.y, at.z);
		camera.lookAt(car.x + Math.sin(heading) * AHEAD, car.y + 0.8, car.z + Math.cos(heading) * AHEAD, 0, 1, 0);
	}

	function spring(dt:Float, x:Float, y:Float, z:Float) {
		final k = STIFFNESS, d = 2 * Math.sqrt(STIFFNESS);
		velocity.x += (k * (x - at.x) - d * velocity.x) * dt;
		velocity.y += (k * (y - at.y) - d * velocity.y) * dt;
		velocity.z += (k * (z - at.z) - d * velocity.z) * dt;
		at.x += velocity.x * dt;
		at.y += velocity.y * dt;
		at.z += velocity.z * dt;
	}
}
