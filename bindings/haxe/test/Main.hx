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
	public static var ended = 0;
	public static var ticks = 0;

	override function onCreate()
		made++;

	override function onTick(dt:Float)
		ticks++;

	override function onTriggerEnter(other:Entity)
		entered++;

	override function onDestroy()
		ended++;
}

class Main {
	static var failures = 0;
	static var frames = 0;
	static var canvas:Canvas;
	static var a:Entity;
	static var b:Entity;
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
		canvas = Canvas.create();
		expect(!canvas.isNone() && canvas.getType() == NodeType.CANVAS, "a handle, and an enum back");

		// text both ways, UTF-8 included; null where C takes NULL
		a = Entity.create(canvas);
		expect(a.setName("rock ünïcødé ✓") && a.getName() == "rock ünïcødé ✓", "text in and out, UTF-8");
		expect(Entity.find("rock ünïcødé ✓") == a && Entity.find(null).isNone(), "text in, and null");
		expect((a : Handle).getKindName() == "ecs.entity", "any handle");

		// vectors: new, and filled
		expect(a.setPosition(1.5, -2, 3), "numbers in");
		final p = a.getPosition();
		expect(near(p.x, 1.5) && near(p.y, -2) && near(p.z, 3), "a vector back");
		final into = new Vec3();
		expect(a.getPosition(into) == into && near(into.y, -2), "a vector filled");

		// arrays: in, and filled
		b = Entity.create(canvas);
		expect(Entity.setPositions([a, b], [10.0, 11, 12, 20, 21, 22]), "an array of handles and one of floats in");
		final out = [for (_ in 0...6) 0.0];
		expect(Entity.getPositions([a, b], out) == 6 && near(out[3], 20) && near(out[5], 22), "an array filled");
		final shape = Shape2d.create();
		expect(shape.setPolygon([0.0, 0, 10, 0, 10, 10]) && shape.getPointCount() == 3, "a section over a kind");
		final points = [for (_ in 0...6) 0.0];
		expect(shape.getPoints(points) == 6 && near(points[4], 10), "floats filled");
		expect(shape.setParent(canvas) && shape.getParent() == canvas, "a Shape2d is a Node");

		// numbers: a double, a bool, a color
		expect(Probe.setValue("binding.test", 0.125) && Probe.getValue("binding.test") == 0.125, "a double both ways");
		expect(Color.getRed(Color.make(200, 10, 20, 255)) == 200 && Color.getAlpha(Color.make(1, 2, 3, 255)) == 255,
			"a color, all 32 bits");

		// a byte span in, then out
		final bytes = haxe.io.Bytes.ofString("bytes\x00and more");
		write = Fs.write("binding/test.bin", bytes);
		expect(!write.isNone(), "a byte span in");

		// behaviors, from the ecs's events
		Behavior.register("Rock", Rock.new);
		for (e in [a, b]) {
			expect(e.addComponent(Component.BEHAVIOR) && (e : BehaviorComponent).setName("Rock"), "a behavior");
			expect(e.addComponent(Component.COLLIDER) && (e : Collider).setRadius(5), "a collider");
		}
		(b : Entity).setPosition(13, 11, 12); // within a's reach
		(a : Motion).getVelocity(); // a component it hasn't: still answers
	}

	static function tick():Void {}

	static function frame():Void {
		frames++;
		switch frames {
			case 1:
				expect(Rock.made == 2 && Behavior.count() == 2, "the behaviors made from CREATED");
				expect(Behavior.of(a) != null && Behavior.of(a).name == "Rock", "a's behavior");
				// a getter 100,000 times in one frame: nothing may pile up on the wasm stack
				final v = new Vec3();
				var sum = 0.0;
				for (_ in 0...100000)
					sum += a.getPosition(v).x;
				expect(near(sum, 1000000), "100,000 vector getters in a frame");
				var names = 0;
				for (_ in 0...20000)
					names += Entity.find("rock ünïcødé ✓") == a ? 1 : 0;
				expect(names == 20000, "20,000 calls passing text in a frame");
			case 3:
				expect(Rock.entered == 2, "both told of their trigger");
				expect(Rock.ticks > 0, "the behaviors ticked");
				expect(a.destroy(), "destroyed");
			case 5:
				expect(Rock.ended == 1 && Behavior.count() == 1 && Behavior.of(a) == null, "its behavior ended from DESTROYED");
				if (Ui.begin()) {
					Ui.beginPanel("p");
					Ui.label("ünïcødé", 0);
					expect(!Ui.button("b", "Button"), "a UI");
					Ui.endPanel();
					expect(Ui.end(), "ended");
				}
			default:
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
		if (frames >= 9 && stage >= 2 && !reported) {
			reported = true;
			if (App.canQuit())
				App.quit();
			else
				report(); // a page: no quitting, so no shutdown
		}
	}

	static var stage = 0;
	static var reported = false;

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
