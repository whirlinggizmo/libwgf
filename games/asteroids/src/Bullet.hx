import wgf.*;

/** A bullet: the ecs flies it, wraps it, and ages it out; a rock it meets ends it (Rock). **/
class Bullet extends Behavior {
	public static function fire(x:Float, y:Float, vx:Float, vy:Float) {
		final bullet = Main.scene.spawn("bullet", Main.world);
		bullet.setPosition(x, y, 0);
		bullet.snap();
		(bullet : Motion).setVelocity(vx, vy, 0);
		Main.sounds.play(Main.sounds.fire);
	}
}
