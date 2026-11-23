import wgf.*;

/**
	A ball: its components fly it, wrap it, and end it after `LIFE` seconds. This behavior
	only bounces it off a bumper it meets, and gives it the bumper's color.
**/
class Ball extends Behavior {
	static inline var RADIUS = 9.0;
	static inline var LIFE = 6.0;

	final velocity = new Vec3();
	final position = new Vec3();
	final push = new Vec2();

	/** A ball at (x, y), heading `angle` radians at `speed` pixels a second. **/
	public static function spawn(x:Float, y:Float, angle:Float, speed:Float):Actor {
		final ball = Main.shape(x, y, Color.get(ColorStock.RAYWHITE));
		(ball : Shape2d).setCircle(RADIUS);
		ball.addComponent(Component.MOTION);
		(ball : Motion).setVelocity(Math.cos(angle) * speed, Math.sin(angle) * speed, 0);
		ball.addComponent(Component.BOUNDS); // wraps, by default
		(ball : Bounds).setRect(0, 0, Main.WIDTH, Main.HEIGHT);
		(ball : Bounds).setMargin(RADIUS); // wholly off the screen first
		ball.addComponent(Component.LIFETIME);
		(ball : Lifetime).setSeconds(LIFE);
		ball.addComponent(Component.COLLIDER);
		(ball : Collider).setRadius(RADIUS);
		(ball : Collider).setLayer(Main.BALL_LAYER);
		(ball : Collider).setMask(Main.BUMPER_LAYER);
		ball.addBehavior("Ball");
		return ball;
	}

	override function onTriggerEnter(other:Actor, layer:Int) {
		if (layer != Main.BUMPER_LAYER)
			return;
		// The push parts the two along the way they meet: reflect the velocity across it.
		final p = (actor : Collider).getPush(other, push);
		final length = Math.sqrt(p.x * p.x + p.y * p.y);
		if (length > 0) {
			final nx = p.x / length, ny = p.y / length;
			final v = (actor : Motion).getVelocity(velocity);
			final along = v.x * nx + v.y * ny;
			if (along < 0)
				(actor : Motion).setVelocity(v.x - 2 * along * nx, v.y - 2 * along * ny, 0);
			final at = actor.getPosition(position);
			actor.setPosition(at.x + p.x, at.y + p.y, 0);
		}
		final bumper:Bumper = cast Behavior.of(other, "Bumper");
		(actor : Shape2d).setColor(bumper.color);
	}
}
