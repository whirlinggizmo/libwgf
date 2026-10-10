import wgf.*;
import wgf.impl.BuiltVersion;

/**
	The binding's own test, the same program on both targets (JS against a headless wasm
	host under node or a page's host in a browser; hxcpp against a staged headless
	archive): every way a value crosses -- handles, enums, numbers, bools, text both ways
	(UTF-8, and null), a returned vector new and filled, arrays in and filled, a byte span
	in and out -- the runtime's trampolines, behaviors made, told of their triggers, and
	ended from the ecs's events, and a getter called 100,000 times in one frame, which a
	binding piling things on the wasm stack would fault at.
**/
class Rock extends Behavior {
	public static var made = 0;
	public static var entered = 0;
	public static var layers = 0;
	public static var ended = 0;
	public static var ticks = 0;

	override function onCreate()
		made++;

	override function onTick(dt:Float)
		ticks++;

	override function onTriggerEnter(other:Actor, layer:Int) {
		entered++;
		layers += layer;
	}

	override function onDestroy()
		ended++;
}

/** Destroys what it meets: the other's own trigger, later in the batch, must not arrive. **/
class Doomer extends Behavior {
	public static var entered = 0;

	override function onTriggerEnter(other:Actor, layer:Int) {
		entered++;
		other.destroy(ActorDestroy.DESTROY_CHILDREN);
	}
}

class Main {
	static var failures = 0;
	static var frames = 0;
	static var world:Stage2d;
	static var a:Actor;
	static var b:Actor;
	static var write:FsTask;

	static function expect(ok:Bool, what:String, ?pos:haxe.PosInfos):Void {
		if (!ok) {
			failures++;
			Log.message(LogLevel.ERROR, 'FAIL (line ${pos.lineNumber}): $what');
		}
	}

	static function near(a:Float, b:Float):Bool
		return Math.abs(a - b) < 1e-4;

	static function init():Void {
		expect(Version.get() == '${BuiltVersion.MAJOR}.${BuiltVersion.MINOR}.${BuiltVersion.PATCH}',
			"the version");
		world = Stage2d.create();
		expect(!world.isNone() && world.getKind() == ActorKind.STAGE2D, "a handle, and an enum back");

		// text both ways, UTF-8 included; null where C takes NULL
		a = Actor.create();
		a.setParent(world);
		expect(a.setName("rock ünïcødé ✓") && a.getName() == "rock ünïcødé ✓", "text in and out, UTF-8");
		expect(world.find("rock ünïcødé ✓") == a && world.find(null).isNone(), "text in, and null");
		expect((a : Handle).getKindName() == "gfx.actor", "any handle");

		// vectors: new, and filled
		expect(a.setPosition(1.5, -2, 3), "numbers in");
		final p = a.getPosition();
		expect(near(p.x, 1.5) && near(p.y, -2) && near(p.z, 3), "a vector back");
		final into = new Vec3();
		expect(a.getPosition(into) == into && near(into.y, -2), "a vector filled");

		// arrays: in, and filled
		b = Actor.create();
		b.setParent(world);
		expect(Actor.setPositions([a, b], [10.0, 11, 12, 20, 21, 22]), "an array of handles and one of floats in");
		final out = [for (_ in 0...6) 0.0];
		expect(Actor.getPositions([a, b], out) == 6 && near(out[3], 20) && near(out[5], 22), "an array filled");
		final shape = Shape2d.create();
		expect(shape.setPolygon([0.0, 0, 10, 0, 10, 10]) && shape.getPointCount() == 3, "a section over a kind");
		final points = [for (_ in 0...6) 0.0];
		expect(shape.getPoints(points) == 6 && near(points[4], 10), "floats filled");
		expect(shape.setParent(world) && shape.getParent() == world, "a Shape2d is an Actor");

		// numbers: a double, a bool, a color
		expect(Probe.setValue("binding.test", 0.125) && Probe.getValue("binding.test") == 0.125, "a double both ways");
		expect(Color.getRed(Color.make(200, 10, 20, 255)) == 200 && Color.getAlpha(Color.make(1, 2, 3, 255)) == 255,
			"a color, all 32 bits");

		// the calls the JS binding works itself, and C natively: the same bits on every target
		expect(Color.make(300, -5, 127, 128) == Color.make(255, 0, 127, 128) && Color.getRed(Color.make(300, -5, 127, 128)) == 255
			&& Color.get(ColorStock.SKYBLUE) | 0 == 0x66BFFFFF && Color.makeFloat(0.5, 1, 0, 0.25) | 0 == 0x80FF0040
			&& Color.lerp(0x000000FF, 0xFFFFFFFF, 0.5) | 0 == 0x808080FF, "colors, as C makes them");
		trigonometry();

		// a byte span in, then out
		final bytes = haxe.io.Bytes.ofString("bytes\x00and more");
		write = Fs.write("binding/test.bin", bytes);
		expect(!write.isNone(), "a byte span in");

		// behaviors, from the ecs's events
		Behavior.register("Rock", Rock.new);
		for (e in [a, b]) {
			expect(e.addBehavior("Rock") == 1, "a behavior");
			expect(e.addComponent(Component.COLLIDER) && (e : Collider).setRadius(5), "a collider");
		}
		b.setPosition(13, 11, 12); // within a's reach
		Behavior.register("Doomer", Doomer.new);
		for (x in [500.0, 503.0]) { // two that meet, far from a and b
			final d = Actor.create();
			d.setParent(world);
			d.setPosition(x, 0, 0);
			d.addBehavior("Doomer");
			d.addComponent(Component.COLLIDER);
			(d : Collider).setRadius(5);
		}
		(a : Motion).getVelocity(); // a component it hasn't: still answers
	}

	/**
		The trigonometry (wgf_trig.h) over the sweep wgf_math_test hashes, to the same hash: C's
		natively, the JS binding's own on the web (bindings/js/src/local.js), the same bits.
	**/
	static function trigonometry():Void {
		var hash:haxe.Int32 = cast 0x811C9DC5;
		var i = -200000;
		while (i <= 200000) {
			final a = haxe.io.FPHelper.i32ToFloat(haxe.io.FPHelper.floatToI32(i * haxe.io.FPHelper.i32ToFloat(haxe.io.FPHelper.floatToI32(1e-4))));
			final twentieth = haxe.io.FPHelper.i32ToFloat(haxe.io.FPHelper.floatToI32(a / 20));
			for (v in [Trig.sin(a), Trig.cos(a), Trig.atan2(a, 0.7), Trig.asin(twentieth), Trig.acos(twentieth)])
				hash = (hash ^ haxe.io.FPHelper.floatToI32(v)) * 16777619;
			i += 7;
		}
		expect(StringTools.hex(hash, 8) == "FDD87A04", 'trigonometry: C\'s hash on every target (${StringTools.hex(hash, 8)})');
	}

	static function tick():Void {}

	static function frame():Void {
		frames++;
		if (frames == 1) {
			expect(Rock.made == 2 && Behavior.count() == 4, "the behaviors made from CREATED");
			expect(Behavior.of(a) != null && Behavior.of(a).name == "Rock", "a's behavior");
			// a getter 100,000 times in one frame: nothing may pile up on the wasm stack
			final v = new Vec3();
			var sum = 0.0;
			for (_ in 0...100000)
				sum += a.getPosition(v).x;
			expect(near(sum, 1000000), "100,000 vector getters in a frame");
			var names = 0;
			for (_ in 0...20000)
				names += world.find("rock ünïcødé ✓") == a ? 1 : 0;
			expect(names == 20000, "20,000 calls passing text in a frame");
		}
		// the triggers and ticks come with the fixed-rate ticks, so with the time the frames
		// carry: waited for, as a browser's first frames may carry little
		if (destroyedAt == 0 && frames >= 3 && (Rock.entered == 2 && Rock.ticks > 0 && Doomer.entered >= 1 || frames > 300)) {
			expect(Rock.entered == 2 && Rock.layers == 2, "both told of their trigger, and the other's layer (1)");
			expect(Rock.ticks > 0, "the behaviors ticked");
			expect(Doomer.entered == 1, "a trigger whose actor was destroyed earlier in the batch: dropped");
			a.destroy(ActorDestroy.DESTROY_CHILDREN);
			expect(a.getKind() == ActorKind.NONE, "destroyed");
			destroyedAt = frames;
		} else if (destroyedAt > 0 && frames == destroyedAt + 2) {
			expect(Rock.ended == 1 && Behavior.count() == 2 && Behavior.of(a) == null, "its behavior ended from DESTROYED");
			if (Ui.begin()) {
				Ui.beginPanel("p");
				Ui.label("ünïcødé", 0);
				expect(!Ui.button("b", "Button"), "a UI");
				Ui.endPanel();
				expect(Ui.end(), "ended");
			}
		}
		// the byte span's write, then its read: each waited on, as a page's storage takes a while
		if (frames > 300 && stage < 3) {
			expect(false, "the write and the read done in 300 frames");
			stage = 3;
		}
		if (stage == 0 && write.getStatus() != FsTaskStatus.PENDING) {
			expect(write.getStatus() == FsTaskStatus.DONE, "the write done");
			write.destroy();
			write = Fs.read("binding/test.bin");
			stage = 1;
		} else if (stage == 1 && write.getStatus() != FsTaskStatus.PENDING) {
			expect(write.getStatus() == FsTaskStatus.DONE, "the read done");
			final data = write.getData();
			expect(data.length == 14 && data.get(5) == 0 && data.getString(6, 8) == "and more", "a byte span out");
			write.destroy();
			stage = 2;
		}
		if (destroyedAt > 0 && frames >= destroyedAt + 6 && stage >= 2 && !reported) {
			reported = true;
			if (App.canQuit())
				App.quit();
			else
				report(); // a page: no quitting, so no shutdown
		}
	}

	static var stage = 0;
	static var reported = false;
	static var destroyedAt = 0;

	static function shutdown():Void {
		expect(Rock.ended == 2, "the last behavior ended at shutdown");
		report();
	}

	static function report():Void {
		expect(frames >= 9, "the frames ran");
		final verdict = failures == 0 ? "PASS" : 'FAIL ($failures)';
		Log.message(failures == 0 ? LogLevel.INFO : LogLevel.ERROR, 'binding test: $verdict');
		#if js
		js.Syntax.code("if (typeof process !== 'undefined') process.exitCode = {0}", failures == 0 ? 0 : 1);
		#elseif sys
		Sys.exit(failures == 0 ? 0 : 1);
		#end
	}

	static function main():Void {
		if (!Runtime.run(init, tick, frame, shutdown)) {
			Log.message(LogLevel.ERROR, "binding test: the run was refused");
			#if sys
			Sys.exit(1);
			#end
		}
	}
}
