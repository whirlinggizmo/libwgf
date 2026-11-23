import wgf.*;

/** The spinning square: launches a ball from where it is, every `EVERY` seconds, from its tick. **/
class Spawner extends Behavior {
	static inline var EVERY = 0.4;

	var clock = EVERY; // the first ball at once
	final at = new Vec3();

	override function onTick(dt:Float) {
		clock += dt;
		while (clock >= EVERY) {
			clock -= EVERY;
			final p = actor.getPosition(at); // fills `at`, so no vector is made each tick
			Ball.spawn(p.x, p.y, Random.getRange(0, Math.PI * 2), Random.getRange(140, 220));
		}
	}
}
