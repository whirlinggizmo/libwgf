import wgf.*;

/**
	Checkpoints in order and lap times. Each checkpoint is a collider (a trigger sphere
	across the track) with a Checkpoint behavior and its index as a parameter; the car
	passing it raises a trigger, and a checkpoint passed out of order counts for nothing.
	The race starts behind checkpoint 0, the line: the first pass of it starts lap 1's
	checkpoints, and each pass after all the others ends a lap.
**/
class Laps {
	public static inline var LAPS = 3;
	public static var lap = 1;
	public static var next = 0; // the checkpoint wanted next
	public static var passed = 0; // checkpoints passed in order since the start, the line's first pass too
	public static var current = 0.0;
	public static var last = 0.0;
	public static var best = 0.0;

	public static function reset() {
		lap = 1;
		next = 0;
		passed = 0;
		current = 0;
		last = 0;
	}

	public static function pass(index:Int) {
		if (index != next)
			return;
		if (index == 0 && passed > 0) { // the line, after a whole lap
			last = current;
			best = best == 0 || current < best ? current : best;
			current = 0;
			lap++;
		}
		passed++;
		next = (next + 1) % Track.CHECKPOINTS;
	}

	/** Each tick: the lap's clock, and the probes an autopilot expects on. **/
	public static function tick(dt:Float) {
		if (Main.state == Main.State.RACING)
			current += dt;
		Probe.setValue("racer.state", Main.state);
		Probe.setValue("racer.lap", lap);
		Probe.setValue("racer.checkpoint", next);
		Probe.setValue("racer.passed", passed);
		Probe.setValue("racer.lap_time", current);
		Probe.setValue("racer.last", last);
		Probe.setValue("racer.best", best);
		Probe.setValue("racer.speed", Main.car == null ? 0 : Main.car.drive.speed);
		Probe.setValue("racer.steer", Main.car == null ? 0 : Main.car.steer);
		Probe.setValue("racer.grass", Main.car != null && Main.car.drive.onGrass ? 1 : 0);
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
	}

	override function onTriggerEnter(other:Actor, layer:Int) {
		if (Main.car != null && other == Main.car.actor)
			Laps.pass(index);
	}
}
