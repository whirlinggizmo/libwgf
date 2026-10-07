import wgf.*;

/**
	A bullet: the ecs flies it, wraps it, and ages it out; a rock it meets ends it (Rock).
	Its behavior name is a tag (Behavior.tag), counted to cap the bullets in flight.
**/
class Bullet {
	public static function fire(x:Float, y:Float, vx:Float, vy:Float) {
		final bullet = Main.scene.spawnAt("bullet", Main.world, x, y, 0, 0);
		(bullet : Motion).setVelocity(vx, vy, 0);
		Main.sounds.play(Main.sounds.fire);
	}
}
