package wgf;

import wgf.impl.BuiltVersion;
import wgf.impl.Raw;

/**
	Running a program: `Runtime.run(init, tick, frame, shutdown)` is libwgf's `wgf_app_run`
	for Haxe, the one call that takes callbacks (docs/BINDINGS.md). It checks the library
	is the version the binding was made for, then hands C four trampolines, installed once:
	each reads the handler statics below when it fires, so a handler changed later -- by
	the program, or by a hot reload swapping the classes -- is the one that runs, and on
	the web no second function enters the wasm table.

	Each trampoline also runs the scripts (wgf.Script): before the program's tick, the
	ecs's events are taken and each script made, told of its triggers, or ended; then
	every script's tick, then the program's. The frame does the same with frames. A throw
	out of a handler is caught at the trampoline -- it would otherwise unwind through C --
	logged, and ends the run, so it is seen rather than repeated every frame.
**/
@:keep
#if cpp
@:cppFileCode("
#include \"wgf_app.h\"
static void wgf_hx_init(void *user) { (void)user; ::wgf::Runtime_obj::dispatch(0); }
static void wgf_hx_tick(void *user) { (void)user; ::wgf::Runtime_obj::dispatch(1); }
static void wgf_hx_frame(void *user) { (void)user; ::wgf::Runtime_obj::dispatch(2); }
static void wgf_hx_shutdown(void *user) { (void)user; ::wgf::Runtime_obj::dispatch(3); }
")
#end
class Runtime {
	public static var onInit:Null<() -> Void>;
	public static var onTick:Null<() -> Void>;
	public static var onFrame:Null<() -> Void>;
	public static var onShutdown:Null<() -> Void>;

	static var faulted = false;

	/**
		Open the window and run, calling `init` once, `tick` at the tick rate (wgf.Loop),
		`frame` every frame, and `shutdown` after the last; any may be null. False when
		the library is another major or minor version than the binding, or C refuses
		(a run already going). On the web it returns at once and the browser runs the
		frames; natively, when the program has quit.
	**/
	public static function run(init:Null<() -> Void>, tick:Null<() -> Void>, frame:Null<() -> Void>,
			shutdown:Null<() -> Void>):Bool {
		onInit = init;
		onTick = tick;
		onFrame = frame;
		onShutdown = shutdown;
		#if js
		// the host the page (or node's runner) made: globalThis.wgfHost, unless attached already
		if (!wgf.impl.Host.isAttached() && js.Syntax.code("typeof globalThis.wgfHost") != "undefined")
			wgf.impl.Host.attach(js.Syntax.code("globalThis.wgfHost"));
		#end
		if (!versionMatches())
			return false;
		#if js
		final host = Raw.host;
		if (trampolines == null)
			trampolines = [for (which in 0...4) host["addFunction"](() -> dispatch(which), "vi")];
		return (host["_wgf_app_run"](trampolines[0], trampolines[1], trampolines[2], trampolines[3], 0) : Int) != 0;
		#elseif cpp
		return untyped __cpp__("::wgf_app_run(wgf_hx_init, wgf_hx_tick, wgf_hx_frame, wgf_hx_shutdown, (void *)0)");
		#end
	}

	#if js
	static var trampolines:Null<Array<Int>>;
	#end

	/**
		Whether the library is the version the binding was made from, major and minor; a
		patch apart is compatible. On the web the host and the program are fetched and
		cached apart, so a page can pair an old one with a new one: this says so, rather
		than calls landing on the wrong functions.
	**/
	public static function versionMatches():Bool {
		final major = Version.getMajor(), minor = Version.getMinor();
		if (major == BuiltVersion.MAJOR && minor == BuiltVersion.MINOR)
			return true;
		Log.message(LogLevel.ERROR,
			'wgf: the binding is for libwgf ${BuiltVersion.MAJOR}.${BuiltVersion.MINOR}, the library is ${Version.get()}: '
			+ 'rebuild the program against this library (or load the host it was built with)');
		return false;
	}

	/** A trampoline fired: 0 init, 1 tick, 2 frame, 3 shutdown. **/
	@:keep public static function dispatch(which:Int):Void {
		if (faulted && which != 3)
			return;
		#if js
		final mark = wgf.impl.Host.stackSave();
		#end
		try {
			switch which {
				case 0:
					if (onInit != null) onInit();
				case 1:
					Script.tickAll(Loop.getTickDelta());
					if (onTick != null) onTick();
				case 2:
					Script.frameAll(Loop.getFrameDelta());
					if (onFrame != null) onFrame();
				default:
					Script.endAll();
					if (onShutdown != null) onShutdown();
			}
		} catch (e:haxe.Exception) {
			faulted = true;
			Log.message(LogLevel.ERROR, 'wgf: uncaught exception in ${["init", "tick", "frame", "shutdown"][which]}: '
				+ e.message + '\n' + e.stack.toString());
			App.quit();
		}
		#if js
		wgf.impl.Host.stackRestore(mark);
		#end
	}
}
