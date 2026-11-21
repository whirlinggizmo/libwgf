package wgf.macro;

#if macro
import haxe.macro.Compiler;
#end

/**
	The binding's defines, registered with Haxe so `haxe --help-user-defines` and an editor's
	hxml completion show them, each with what it does. The binding's `extraParams.hxml` runs
	`register` for a build that takes it as a library (`-lib wgf`); the `wgf` tool, which puts
	it on the class path, passes the same `--macro` itself (tools/binding.py's `MACROS`).
**/
@:noCompletion
class Defines {
	#if macro
	public static final ALL:Array<haxe.macro.Compiler.DefineDescription> = [
		{
			define: "wgf_autopilot",
			doc: "Links the autopilot's runner into a native build (App.enableAutopilot), so an autopilot can fly it: what the wgf tool's own builds pass, and what `wgf export` leaves out unless --keep-autopilot. On the web the host decides: its runner is there when it exports the call."
		},
		{
			define: "wgf_record",
			doc: "A native build made to record a run by hand as an autopilot (`wgf autopilot --record`), the only build with the recorder in it: never a shipped one."
		},
		{
			define: "wgf_out",
			doc: "The staged libwgf variant a native build links (out/<platform>/<variant>): its headers and its archive. Needed for hxcpp.",
			params: ["directory"]
		},
		{
			define: "wgf_binding",
			doc: "Where the binding is (bindings/haxe), for hxcpp's build file: set by the wgf tool.",
			params: ["directory"]
		},
		{
			define: "wgf_headless",
			doc: "The staged variant named by wgf_out is a headless one, which needs no window or audio device."
		},
		{
			define: "wgf_gltf",
			doc: "Links glTF's loading into a native build that never calls it, for a scene's `model path=` lines (the wgf tool sets it when a scene names a file)."
		},
		{
			define: "wgf_emitters",
			doc: "Links the particles into a native build that never makes one, for a scene's emitter lines (the wgf tool sets it when a scene has one)."
		},
		{
			define: "wgf_reach",
			doc: "Each of the binding's C calls counts itself (wgf.impl.Reach), for the feature test's coverage (tools/check_features.py)."
		}
	];

	/** Registers the defines. **/
	public static function register():Void {
		for (define in ALL)
			Compiler.registerCustomDefine(define, "wgf");
	}
	#end
}
