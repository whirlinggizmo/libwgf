package wgf;

import wgf.impl.BuiltVersion;
import wgf.impl.Raw;

/**
	Running a program: `Runtime.run(init, tick, frame, shutdown)` is libwgf's `wgf_app_run`
	for Haxe, the one call that takes callbacks (docs/BINDINGS.md). It checks the library
	is the version the binding was made for, then hands C four trampolines, installed once
	(on the web, the JS binding's run installs them): each reads the handler statics below
	when it fires, so a handler changed later -- by the program, or by a hot reload
	swapping the classes -- is the one that runs, and on the web no second function enters
	the wasm table.

	Each trampoline also runs the behaviors (wgf.Behavior): before the program's tick, the
	ecs's events are taken and each behavior made, told of its triggers, or ended; then
	every behavior's tick, then the program's. The frame does the same with frames. A throw
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
extern \"C\" void wgf_app_priv_record_install(void); /* linked only when called: -D wgf_record */
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
		#if hotreload
		// a hot reload's bundle runs its main again as it loads: the run is going already,
		// and the swap that follows gives the trampolines this bundle's handlers
		if (hotreload.Reloader.isReload())
			return true;
		#end
		if (!versionMatches())
			return false;
		#if wgf_reach
		wgf.impl.Reach.hit(wgf.impl.Reach.NAMES.indexOf("wgf_app_run"));
		#end
		#if js
		#if hotreload
		pollAssets(-1);
		#end
		// the JS binding's run: its trampolines, installed once, call these, which read the
		// handlers when they fire; it restores the wasm stack after each
		return Raw.binding["wgf_app_run"](() -> dispatch(0), () -> dispatch(1), () -> dispatch(2), () -> dispatch(3));
		#elseif cpp
		#if wgf_record
		// a build made to record a run by hand (`wgf autopilot --record`): never a shipped one
		untyped __cpp__("wgf_app_priv_record_install()");
		#end
		return untyped __cpp__("::wgf_app_run(wgf_hx_init, wgf_hx_tick, wgf_hx_frame, wgf_hx_shutdown, (void *)0)");
		#end
	}

	#if (js && hotreload)
	/**
		`wgf serve`'s assets: asks the server for the files saved after save `since` (a long
		poll; -1 first, for the latest number alone) and loads each again in place
		(Asset.reload), then asks again; a server gone is asked again a second later.
	**/
	static function pollAssets(since:Int):Void {
		js.Browser.window.fetch('/__hotreload_assets?since=$since', {cache: js.html.RequestCache.NO_STORE})
			.then(response -> response.json())
			.then(function(answer:Dynamic) {
				final files:Array<String> = answer.files;
				for (file in files)
					Asset.reload(file);
				pollAssets(answer.version);
			}, function(_) {
				haxe.Timer.delay(() -> pollAssets(since), 1000);
			});
	}
	#end

	/**
		Whether the library is the version the binding was made from, major and minor; a
		patch apart is compatible. On the web the host and the program are fetched and
		cached apart, so a page can pair an old one with a new one: this says so, rather
		than calls landing on the wrong functions.
	**/
	public static function versionMatches():Bool {
		#if js
		// the JS binding beside the program: made from the same headers as this binding,
		// or the calls this one makes may not be the ones it has
		final built:haxe.DynamicAccess<Dynamic> = Raw.binding["BUILT_VERSION"];
		final stamp:String = built["headers"];
		if (stamp != BuiltVersion.HEADERS) {
			Log.message(LogLevel.ERROR,
				'wgf: the JS binding beside the program (wgf.js) was made from other headers (${stamp}) than '
				+ 'the program\'s binding (${BuiltVersion.HEADERS}): build the program and its page again');
			return false;
		}
		#end
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
		try {
			switch which {
				case 0:
					if (onInit != null) onInit();
				case 1:
					Behavior.tickAll(Loop.getTickDelta());
					if (onTick != null) onTick();
				case 2:
					Behavior.frameAll(Loop.getFrameDelta());
					if (onFrame != null) onFrame();
				default:
					Behavior.endAll();
					if (onShutdown != null) onShutdown();
			}
		} catch (e:haxe.Exception) {
			faulted = true;
			Log.message(LogLevel.ERROR, 'wgf: uncaught exception in ${["init", "tick", "frame", "shutdown"][which]}: '
				+ e.message + '\n' + e.stack.toString());
			App.quit();
		}
	}
}
