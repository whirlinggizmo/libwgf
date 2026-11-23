import wgf.*;

/** A bumper: counts each ball that meets it, flashes, and leaves a spark where the ball was. **/
class Bumper extends Behavior {
	/** Its own color, the one it flashes from. **/
	public var color(default, null) = 0;

	var flash = 0.0; // 1 as a ball meets it, down to 0
	final at = new Vec3();

	override function onCreate() {
		color = (actor : Shape2d).getColor();
	}

	override function onTriggerEnter(other:Actor, layer:Int) {
		if (layer != Main.BALL_LAYER)
			return;
		Main.hits++;
		flash = 1;
		final p = other.getPosition(at);
		Spark.spawn(p.x, p.y, color);
	}

	// Each frame, the flash fades back to its color: a look, so the frame's and not the tick's.
	override function onFrame(dt:Float) {
		if (flash <= 0)
			return;
		flash = Math.max(0, flash - dt * 3);
		(actor : Shape2d).setColor(Color.lerp(color, Color.get(ColorStock.WHITE), flash));
	}
}
