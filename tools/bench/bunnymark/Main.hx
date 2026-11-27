import wgf.Actor;
import wgf.App;
import wgf.Asset;
import wgf.Log;
import wgf.LogLevel;
import wgf.Render;
import wgf.Resource;
import wgf.ResourceStatus;
import wgf.Runtime;
import wgf.Sprite;
import wgf.Stage2d;
import wgf.Texture;
import wgf.Window;
import wgf.WindowFlag;

/** A bunny, as raylib's bunnymark has it. */
class Bunny {
	public var x:Float;
	public var y:Float;
	public var vx:Float;
	public var vy:Float;

	public function new(x:Float, y:Float, vx:Float, vy:Float) {
		this.x = x;
		this.y = y;
		this.vx = vx;
		this.vy = vy;
	}
}

/**
	The bunnymark in Haxe, on the JS target through the Haxe binding, as a libwgf game runs
	in a page: main.c's program (its header says what it draws and how it is timed), each
	call crossing from JS into libwgf's wasm. helix-raylib's row is Haxe too
	(../helix's tools/speed/contenders/helix-raylib/bunnymark/Main.hx), its Bunny the same.

	Each bunny is a sprite actor, moved with a setPosition each. Options from the page's
	query: count=N, or incrementCount=N with startCount=M. It prints main.c's JSON line, with
	"lang":"haxe", then logs "bunnymark-hx: done" (measure_frames.py's bench:bunnymark-hx).
**/
class Main {
	static inline final SCREEN_W = 800;
	static inline final SCREEN_H = 450;
	static inline final BUNNY = 32;
	static inline final WARMUP = 120;
	static inline final TIMED = 600;
	static inline final MAX_SPRITES = 65536;

	static var seed = 0x92D68CA2; // 2463534242
	static var stage:Stage2d;
	static var texture:Texture;
	static final bunnies:Array<Bunny> = [];
	static final sprites:Array<Actor> = [];
	static var limit = false;
	static var asked = 1000;
	static var increment = 0;
	static var frameIndex = -1;
	static var last = 0.0;
	static var windowStart = 0.0;
	static var maxCount = 0;
	static final deltas:Array<Float> = [];
	static var done = false;

	// xorshift32, main.c's and helix's, so every bunny starts the same
	static function rnd(min:Int, max:Int):Int {
		var x = seed;
		x ^= x << 13;
		x ^= x >>> 17;
		x ^= x << 5;
		seed = x;
		return min + (x >>> 8) % (max - min + 1);
	}

	static function spawn(n:Int) {
		for (_ in 0...n) {
			if (sprites.length == MAX_SPRITES) {
				limit = true;
				return;
			}
			final sprite = Sprite.create(texture);
			if (sprite.isNone()) {
				limit = true;
				return;
			}
			final x = rnd(0, SCREEN_W - BUNNY);
			final y = rnd(40, SCREEN_H - BUNNY);
			final vx = rnd(-250, 250) / 60.0;
			final vy = rnd(-250, 250) / 60.0;
			final red = rnd(50, 240), green = rnd(80, 240), blue = rnd(100, 240);
			bunnies.push(new Bunny(x, y, vx, vy));
			sprite.setSize(BUNNY, BUNNY);
			sprite.setPivot(0, 0); // raylib's DrawTexture places the top-left
			sprite.setTint((red << 24) | (green << 16) | (blue << 8) | 0xFF);
			sprite.setParent(stage);
			sprites.push(sprite);
		}
	}

	static function now():Float
		return js.Browser.window.performance.now();

	static function finish() {
		done = true;
		Log.message(LogLevel.INFO, "bunnymark-hx: done");
		if (App.canQuit())
			App.quit();
	}

	static function main() {
		asked = App.getOptionInt("count", 1000);
		increment = App.getOptionInt("incrementCount", 0);
		final start = App.getOptionInt("startCount", 0);
		if (increment != 0)
			asked = start > 0 ? start : increment;
		Window.setTitle("libwgf bunnymark (Haxe)");
		Window.setSize(SCREEN_W, SCREEN_H);
		Window.setFlags(WindowFlag.VSYNC_OFF | WindowFlag.LOW_DPI);
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Render.setClearColor(0xF5F5F5FF);
		stage = Stage2d.create();
		texture = Texture.create("textures/wabbit_alpha.png");
	}

	static function frame() {
		if (done || Resource.getStatus(texture) == ResourceStatus.PENDING)
			return;
		final time = now();
		if (frameIndex < 0)
			spawn(asked);
		else
			deltas.push(time - last);
		last = time;
		frameIndex++;

		for (i in 0...bunnies.length) {
			final b = bunnies[i];
			b.x += b.vx;
			b.y += b.vy;
			if (b.x + BUNNY / 2 > SCREEN_W || b.x + BUNNY / 2 < 0)
				b.vx *= -1;
			if (b.y + BUNNY / 2 > SCREEN_H || b.y + BUNNY / 2 - 40 < 0)
				b.vy *= -1;
			sprites[i].setPosition(Std.int(b.x), Std.int(b.y), 0);
		}
		stage.draw();

		if (increment == 0) {
			if (deltas.length == WARMUP + TIMED) {
				final timed = deltas.slice(WARMUP);
				final sorted = timed.copy();
				sorted.sort((a, b) -> a < b ? -1 : a > b ? 1 : 0);
				var sum = 0.0;
				for (d in timed)
					sum += d;
				final mean = sum / TIMED;
				js.Browser.console.log('{"contender":"libwgf","lang":"haxe","draw":"actors","target":"page","mode":"count",'
					+ '"count":${sprites.length},"asked":$asked,"limit":$limit,"mean_ms":$mean,'
					+ '"median_ms":${sorted[TIMED >> 1]},"p95_ms":${sorted[Std.int(TIMED * 95 / 100)]},"fps":${1000 / mean}}');
				finish();
			}
		} else if (deltas.length >= WARMUP && (deltas.length - WARMUP) % 60 == 0) {
			if (deltas.length > WARMUP) {
				final fps = 60 * 1000 / (time - windowStart);
				if (fps >= 59 && !limit) {
					maxCount = sprites.length;
					spawn(increment);
				} else {
					js.Browser.console.log('{"contender":"libwgf","lang":"haxe","draw":"actors","target":"page","mode":"ramp",'
						+ '"max_count":$maxCount,"limit":$limit,"last_fps":$fps}');
					finish();
				}
			}
			windowStart = time;
		}
	}
}
