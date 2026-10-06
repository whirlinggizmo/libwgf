import wgf.*;

/**
	A rock: a prefab of its size, its outline made jagged at random as it is made; the ecs
	drifts, spins, and wraps it. A bullet that meets it splits it in two of the next size
	down (a small one is gone), for points; the ship that meets it is lost.
**/
class Rock extends Script {
	static final PREFABS = ["rock_large", "rock_medium", "rock_small"];
	static final RADII = [44.0, 24.0, 12.0];
	static final SPEEDS = [50.0, 90.0, 140.0];
	static final POINTS = [20, 50, 100];

	public var size:RockSize = RockSize.LARGE;

	public static function spawn(size:RockSize, x:Float, y:Float, ?direction:Float):Entity {
		final rock = Main.scene.spawn(PREFABS[size], Main.world);
		rock.setPosition(x, y, 0);
		rock.snap();
		final angle = direction != null ? direction : Random.getRange(0, Math.PI * 2);
		final speed = SPEEDS[size] * Random.getRange(0.7, 1.3);
		(rock : Motion).setVelocity(Math.cos(angle) * speed, Math.sin(angle) * speed, 0);
		(rock : Motion).setSpin(0, 0, Random.getRange(-1.5, 1.5));
		(rock : Behavior).setParam("size", Std.string(size));
		return rock;
	}

	override function onCreate() {
		size = Std.int((entity : Behavior).getParamNumber("size"));
		final radius = RADII[size], points = [];
		final corners = 9 + Random.getInt(0, 3);
		for (i in 0...corners) {
			final a = i / corners * Math.PI * 2;
			final r = radius * Random.getRange(0.72, 1.08);
			points.push(Math.cos(a) * r);
			points.push(Math.sin(a) * r);
		}
		final shape:Shape2d = entity.getComponentNode(Component.SHAPE2D);
		shape.setPolygon(points);
	}

	override function onTriggerEnter(other:Entity) {
		if (!entity.isAlive() || !other.isAlive())
			return; // one already gone this tick: a bullet meeting two rocks at once
		final script = Script.of(other);
		if (Std.isOfType(script, Bullet)) {
			other.destroy();
			split();
		} else if (Std.isOfType(script, Ship)) {
			(cast script : Ship).explode();
			split();
		}
	}

	function split() {
		final at = entity.getPosition();
		Main.addScore(POINTS[size]);
		explosion(at.x, at.y, size);
		Main.sounds.play([Main.sounds.bangLarge, Main.sounds.bangMedium, Main.sounds.bangSmall][size]);
		entity.destroy();
		if (size != RockSize.SMALL) {
			final heading = Random.getRange(0, Math.PI * 2);
			spawn(size + 1, at.x, at.y, heading);
			spawn(size + 1, at.x, at.y, heading + Math.PI * Random.getRange(0.6, 1.4));
		}
	}

	/** Sparks at (x, y), as many as the size calls for: an emitter's burst that ages out. **/
	public static function explosion(x:Float, y:Float, size:Int) {
		final sparks = Main.scene.spawn("explosion", Main.world);
		sparks.setPosition(x, y, 0);
		sparks.snap();
		final emitter:Emitter2d = sparks.getComponentNode(Component.EMITTER2D);
		emitter.burst([40, 24, 14][size < 0 ? 0 : size > 2 ? 2 : size]);
	}
}
