import wgf.*;

/**
	Checkpoints in order and lap times. Each checkpoint is a sensor (physics3d) on the
	track with its index as a parameter; the car passing it raises a trigger, and a
	checkpoint passed out of order counts for nothing.
**/
class Laps {
	public static inline var LAPS = 3;
	public static var lap = 1;
	public static var next = 0;
	public static var current = 0.0;
	public static var best = 0.0;
	public static var count = 0; // checkpoints on the track, counted as they are made

	public static function reset() {
		lap = 1;
		next = 0;
		current = 0;
	}

	public static function passed(index:Int) {
		if (index != next)
			return;
		next = (next + 1) % count;
		if (next == 1 && index == 0 && current > 0) { // the line, after a whole lap
			best = best == 0 || current < best ? current : best;
			current = 0;
			lap++;
		}
	}

	public static function publish() {
		if (Main.state == Main.State.RACING)
			current += Loop.getTickDelta();
		Probe.setValue("racer.state", Main.state);
		Probe.setValue("racer.lap", lap);
		Probe.setValue("racer.checkpoint", next);
		Probe.setValue("racer.lap_time", current);
		Probe.setValue("racer.best", best);
		Probe.setValue("racer.speed", Main.car.isNone() ? 0 : (Main.car : Vehicle).getSpeed());
	}

	public static function format(seconds:Float):String {
		final whole = Std.int(seconds);
		final hundredths = Std.int((seconds - whole) * 100);
		return '${Std.int(whole / 60)}:${StringTools.lpad('${whole % 60}', "0", 2)}.${StringTools.lpad('$hundredths', "0", 2)}';
	}
}

class Checkpoint extends Behavior {
	var index = 0;

	override function onCreate() {
		index = Std.int(getParamNumber("index"));
		Laps.count++;
	}

	override function onTriggerEnter(other:Entity) {
		if (other == Main.car)
			Laps.passed(index);
	}
}
