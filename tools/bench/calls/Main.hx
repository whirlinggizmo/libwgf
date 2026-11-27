import wgf.Log;
import wgf.LogLevel;
import wgf.Actor;
import wgf.Runtime;
import wgf.Vec3;

/**
	What a call from Haxe into libwgf costs on the JS target (tools/bench/measure_calls.py
	runs it under node and in a browser): each shape, the median of REPS runs of N calls,
	in ns per call. One line each, `calls: <shape> <ns>`,
	then `calls done: PASS`. The same calls as main.js makes through the JS binding.
**/
class Main {
	static inline final N = 200000;
	static inline final REPS = 7;
	// The chat: a scroll area of LINES lines of 80 characters, as a chat or a log shows them,
	// given again each frame, timed over FRAMES frames after WARM.
	static inline final LINES = 200;
	static inline final WARM = 30;
	static inline final FRAMES = 31;
	static inline final LINE = "player_42: has anyone found the key to the north gate? I looked behind the inn.!";

	static var sink = 0.0;

	static function main()
		Runtime.run(init, null, frame, null);

	static function init() {
		final first = Actor.create();
		first.setParent(Actor.create());
		final kept = new Vec3();
		report("set", N, () -> for (k in 0...N) if (first.setPosition(k, 2, 3)) sink += 1);
		report("get", N, () -> for (_ in 0...N) sink += first.getPosition(kept).x);
		report("transform", N, () -> for (k in 0...N) if (first.setTransform(k, 2, 3, 0, 0, 1, 1, 1, 1)) sink += 1);
		report("string", N, () -> for (_ in 0...N) if (first.setName("first")) sink += 1);
		report("string-80", N, () -> for (_ in 0...N) if (first.setName(LINE)) sink += 1);
	}

	static final frames:Array<Float> = [];
	static final viewFrames:Array<Float> = [];
	static var history:wgf.TextBuffer = 0; // the chat's lines in a text buffer, for the text view

	/**
		One UI frame a frame: the chat's lines given as labels, laid out, and drawn at Ui.end;
		then as many frames of the same lines in a text buffer, shown by a text view.
	**/
	static function frame() {
		if (frames.length < WARM + FRAMES) {
			final start = haxe.Timer.stamp();
			if (wgf.Ui.begin()) {
				wgf.Ui.beginScroll("chat", 600, 400);
				for (_ in 0...LINES)
					if (wgf.Ui.label(LINE, 0))
						sink += 1;
				wgf.Ui.endScroll();
				wgf.Ui.end();
			}
			frames.push((haxe.Timer.stamp() - start) * 1e9);
			return;
		}
		if (history == 0)
			history = wgf.TextBuffer.createText([for (_ in 0...LINES) LINE].join("\n"));
		final start = haxe.Timer.stamp();
		if (wgf.Ui.begin()) {
			if (wgf.Ui.textView("view", history, 600, 400, false))
				sink += 1;
			wgf.Ui.end();
		}
		viewFrames.push((haxe.Timer.stamp() - start) * 1e9);
		if (viewFrames.length < WARM + FRAMES)
			return;
		final timed = frames.slice(WARM);
		timed.sort(Reflect.compare);
		final viewed = viewFrames.slice(WARM);
		viewed.sort(Reflect.compare);
		Log.message(LogLevel.INFO, 'calls: chat ${Math.round(timed[FRAMES >> 1])}');
		Log.message(LogLevel.INFO, 'calls: chat-view ${Math.round(viewed[FRAMES >> 1])}');
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
