import wgf.*;

/** A spark: a ring that grows and fades while its lifetime runs out, which ends it. **/
class Spark extends Behavior {
	static inline var LIFE = 0.5;

	var color = 0;

	/** A spark at (x, y), in `color`. **/
	public static function spawn(x:Float, y:Float, color:Int):Actor {
		final spark = Main.shape(x, y, color);
		(spark : Shape2d).setCircle(10);
		(spark : Shape2d).setOutline(3);
		spark.addComponent(Component.LIFETIME);
		(spark : Lifetime).setSeconds(LIFE);
		spark.addBehavior("Spark");
		return spark;
	}

	override function onCreate() {
		color = (actor : Shape2d).getColor();
	}

	// Its scale is its transform, which a simulated actor sets at the tick rate.
	override function onTick(dt:Float) {
		final left = (actor : Lifetime).getSeconds() / LIFE; // 1 down to 0
		final grow = 1 + (1 - left) * 2.5;
		actor.setScale(grow, grow, 1);
		(actor : Shape2d).setColor(Color.withAlpha(color, Std.int(255 * left)));
	}
}
