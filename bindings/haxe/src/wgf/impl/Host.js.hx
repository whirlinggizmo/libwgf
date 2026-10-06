package wgf.impl;

/**
	What Raw.js.hx needs besides the JS binding (bindings/js/wgf.js), which does the
	crossing into the wasm host for every JS program: Haxe's JS target never reaches the
	host itself. What is left is Haxe's own: finding the binding, a vector read into a Vec
	through one kept array (by index, which no minifier can rename), Bytes to and from the
	binding's Uint8Arrays, and handles made Ints again where the binding gives them back
	unsigned.
**/
class Host {
	// no initializer: Raw's, which may set it, can run before Host's would
	static var attached:Null<Bool>;

	public static inline function isAttached():Bool
		return attached == true;

	/**
		The JS binding as the program loads: the one the page (or node's runner) attached to
		the host and left at globalThis.wgfJs before loading the program, so a call before
		the run (a window's title) has it; otherwise a stand-in that says what went wrong at
		any call.
	**/
	public static function initial():haxe.DynamicAccess<Dynamic> {
		if (js.Syntax.code("typeof globalThis.wgfJs") != "undefined") {
			attached = true;
			return js.Syntax.code("globalThis.wgfJs");
		}
		return notAttached();
	}

	static function notAttached():haxe.DynamicAccess<Dynamic>
		return js.Syntax.code("new Proxy({}, {get(_, name) { throw new Error(\"wgf: \" + String(name) + \" called before the JS binding was attached (globalThis.wgfJs)\"); }})");

	/** The array every vector getter fills, read at once. **/
	public static final vector:Array<Float> = [0.0, 0.0, 0.0, 0.0];

	public static inline function vec2(into:Null<wgf.Vec2>):wgf.Vec2 {
		final out = into != null ? into : new wgf.Vec2();
		out.x = vector[0];
		out.y = vector[1];
		return out;
	}

	public static inline function vec3(into:Null<wgf.Vec3>):wgf.Vec3 {
		final out = into != null ? into : new wgf.Vec3();
		out.x = vector[0];
		out.y = vector[1];
		out.z = vector[2];
		return out;
	}

	public static inline function vec4(into:Null<wgf.Vec4>):wgf.Vec4 {
		final out = into != null ? into : new wgf.Vec4();
		out.x = vector[0];
		out.y = vector[1];
		out.z = vector[2];
		out.w = vector[3];
		return out;
	}

	/** `bytes` as the binding takes them: a view of its data, not a copy; null for null. **/
	public static inline function bytesIn(bytes:haxe.io.Bytes):Dynamic
		return bytes == null ? null : js.Syntax.code("new Uint8Array({0}, 0, {1})", bytes.getData(), bytes.length);

	/** The binding's copy of a byte span, as Bytes over it. **/
	public static inline function bytesOut(copy:js.lib.Uint8Array):haxe.io.Bytes
		return haxe.io.Bytes.ofData(copy.buffer);

	/** Handles the binding filled in unsigned, made Haxe's 32-bit Ints again. **/
	public static function signed(values:Array<Int>):Void {
		if (values == null)
			return;
		for (i in 0...values.length)
			values[i] = values[i] | 0;
	}
}
