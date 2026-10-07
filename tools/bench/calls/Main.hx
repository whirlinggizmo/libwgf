import wgf.Log;
import wgf.LogLevel;
import wgf.Actor;
import wgf.Runtime;
import wgf.Vec3;

/**
	What a call from Haxe into libwgf costs on the JS target (tools/bench/measure_calls.py
	runs it under node and in a browser): each shape, the median of REPS runs of N calls,
	in ns per call; the bulk one also per actor. One line each, `calls: <shape> <ns>`,
	then `calls done: PASS`. The same calls as main.js makes through the JS binding.
**/
class Main {
	static inline final N = 200000;
	static inline final REPS = 7;
	static inline final BULK = 1000;

	static var sink = 0.0;

	static function main()
		Runtime.run(init, null, null, null);

	static function init() {
		final root = Actor.create();
		final actors = [for (i in 0...BULK) {
			final actor = Actor.create();
			actor.setParent(root);
			actor;
		}];
		final first = actors[0];
		final kept = new Vec3();
		final out = [for (_ in 0...BULK * 3) 0.0];
		report("set", N, () -> for (k in 0...N) if (first.setPosition(k, 2, 3)) sink += 1);
		report("get", N, () -> for (_ in 0...N) sink += first.getPosition(kept).x);
		report("transform", N, () -> for (k in 0...N) if (first.setTransform(k, 2, 3, 0, 0, 1, 1, 1, 1)) sink += 1);
		report("string", N, () -> for (_ in 0...N) if (first.setName("first")) sink += 1);
		final calls = Std.int(N / BULK) * 10;
		report("bulk", calls, () -> for (_ in 0...calls) sink += Actor.getPositions(actors, out));
		Log.message(LogLevel.INFO, 'calls: bulk-actor ${Math.round(best / BULK * 1000) / 1000}');
		Log.message(LogLevel.INFO, sink != 0 ? "calls done: PASS" : "calls done: FAIL (nothing was called)");
		wgf.App.quit();
	}

	static var best = 0.0;

	static function report(shape:String, calls:Int, body:() -> Void) {
		body(); // warm: the JIT's tiers, the slots grown
		final runs = [for (_ in 0...REPS) {
			final start = haxe.Timer.stamp();
			body();
			(haxe.Timer.stamp() - start) * 1e9 / calls;
		}];
		runs.sort(Reflect.compare);
		best = runs[REPS >> 1];
		Log.message(LogLevel.INFO, 'calls: $shape ${Math.round(best * 1000) / 1000}');
	}
}
