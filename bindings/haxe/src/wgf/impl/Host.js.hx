package wgf.impl;

/**
	How a call crosses into the wasm host and back, for the JS target: Raw.js.hx is built
	on these. Three rules, each a measured hazard (wgrender-c's, docs/BINDINGS.md):

	- Never hold a heap view. The host grows its memory, which replaces `HEAPF32` and the
	  rest; every read goes through `host["HEAPF32"]` where it is used.
	- Nothing piles up on the wasm stack. A string goes on the stack for its call alone
	  (the call saves and restores it); a returned vector comes back through one result
	  slot, and an array through its slot, each `_malloc`'d once, grown when too small,
	  and read out before the call returns.
	- Every name into the host is a quoted key, so a minifier can't rename it.
**/
class Host {
	/** Hand the binding its host: the module the page's createWgfHost() resolved to. **/
	public static function attach(module:Dynamic):Void {
		Raw.host = module;
		attached = true;
		resultPointer = 0;
		resultBytes = 0;
		slots = [];
		slotBytes = [];
	}

	static var attached = false;

	public static inline function isAttached():Bool
		return attached;

	/** A stand-in until attach: any call through it says what went wrong. **/
	public static function notAttached():haxe.DynamicAccess<Dynamic>
		return js.Syntax.code("new Proxy({}, {get(_, name) { throw new Error(\"wgf: \" + String(name) + \" was reached before the host was attached: a call at load time runs before the host exists; make libwgf objects in init\"); }})");

	public static inline function stackSave():Int
		return Raw.host["stackSave"]();

	public static inline function stackRestore(mark:Int):Void
		Raw.host["stackRestore"](mark);

	/** `s` NUL-terminated on the wasm stack (null as a null pointer), until the call's restore. **/
	public static function cstr(s:String):Int {
		if (s == null)
			return 0;
		final length:Int = Raw.host["lengthBytesUTF8"](s) + 1;
		final pointer:Int = Raw.host["stackAlloc"](length);
		Raw.host["stringToUTF8"](s, pointer, length);
		return pointer;
	}

	/** A C string out, copied ("" for a null pointer). **/
	public static inline function str(pointer:Int):String
		return pointer == 0 ? "" : Raw.host["UTF8ToString"](pointer);

	static var resultPointer = 0;
	static var resultBytes = 0;

	/** The slot a returned vector comes back through, at least `bytes` long. **/
	public static function result(bytes:Int):Int {
		if (bytes > resultBytes) {
			if (resultPointer != 0)
				Raw.host["_free"](resultPointer);
			resultBytes = bytes < 64 ? 64 : bytes;
			resultPointer = Raw.host["_malloc"](resultBytes);
		}
		return resultPointer;
	}

	public static inline function vec2(pointer:Int, into:Null<wgf.Vec2>):wgf.Vec2 {
		final heap:Dynamic = Raw.host["HEAPF32"];
		final out = into != null ? into : new wgf.Vec2();
		out.x = heap[pointer >> 2];
		out.y = heap[(pointer >> 2) + 1];
		return out;
	}

	public static inline function vec3(pointer:Int, into:Null<wgf.Vec3>):wgf.Vec3 {
		final heap:Dynamic = Raw.host["HEAPF32"];
		final out = into != null ? into : new wgf.Vec3();
		out.x = heap[pointer >> 2];
		out.y = heap[(pointer >> 2) + 1];
		out.z = heap[(pointer >> 2) + 2];
		return out;
	}

	public static inline function vec4(pointer:Int, into:Null<wgf.Vec4>):wgf.Vec4 {
		final heap:Dynamic = Raw.host["HEAPF32"];
		final out = into != null ? into : new wgf.Vec4();
		out.x = heap[pointer >> 2];
		out.y = heap[(pointer >> 2) + 1];
		out.z = heap[(pointer >> 2) + 2];
		out.w = heap[(pointer >> 2) + 3];
		return out;
	}

	static var slots:Array<Int> = [];
	static var slotBytes:Array<Int> = [];

	/** Array slot `index` (a call's first array is 0, its second 1), at least `bytes` long. **/
	static function slot(index:Int, bytes:Int):Int {
		while (slots.length <= index) {
			slots.push(0);
			slotBytes.push(0);
		}
		if (bytes > slotBytes[index]) {
			if (slots[index] != 0)
				Raw.host["_free"](slots[index]);
			slotBytes[index] = bytes < 256 ? 256 : bytes;
			slots[index] = Raw.host["_malloc"](slotBytes[index]);
		}
		return slots[index];
	}

	/** `values` as floats in slot `index`; 0 for null. **/
	public static function floatsIn(values:Array<Float>, index:Int):Int {
		if (values == null)
			return 0;
		final pointer = slot(index, values.length * 4);
		final heap:Dynamic = Raw.host["HEAPF32"];
		final at = pointer >> 2;
		for (i in 0...values.length)
			heap[at + i] = values[i];
		return pointer;
	}

	/** `values` as 32-bit ints (handles, numbers) in slot `index`; 0 for null. **/
	public static function intsIn(values:Array<Int>, index:Int):Int {
		if (values == null)
			return 0;
		final pointer = slot(index, values.length * 4);
		final heap:Dynamic = Raw.host["HEAP32"];
		final at = pointer >> 2;
		for (i in 0...values.length)
			heap[at + i] = values[i];
		return pointer;
	}

	/** Room in slot `index` for C to fill `values.length` of them. **/
	public static inline function arrayOut<T>(values:Array<T>, index:Int):Int
		return values == null ? 0 : slot(index, values.length * 4);

	/** The floats C filled, back into `values`. **/
	public static function floatsOut(pointer:Int, values:Array<Float>):Void {
		if (values == null)
			return;
		final heap:Dynamic = Raw.host["HEAPF32"];
		final at = pointer >> 2;
		for (i in 0...values.length)
			values[i] = heap[at + i];
	}

	/** The ints C filled, back into `values`. **/
	public static function intsOut(pointer:Int, values:Array<Int>):Void {
		if (values == null)
			return;
		final heap:Dynamic = Raw.host["HEAP32"];
		final at = pointer >> 2;
		for (i in 0...values.length)
			values[i] = heap[at + i];
	}

	/** `bytes` in slot `index`; 0 for null. **/
	public static function bytesIn(bytes:haxe.io.Bytes, index:Int):Int {
		if (bytes == null)
			return 0;
		final pointer = slot(index, bytes.length);
		final heap:Dynamic = Raw.host["HEAPU8"];
		heap.set(js.Syntax.code("new Uint8Array({0})", bytes.getData()), pointer);
		return pointer;
	}

	/** A byte span C owns, copied out: `size` bytes at `pointer`. **/
	public static function bytesOut(pointer:Int, size:Int):haxe.io.Bytes {
		if (pointer == 0 || size <= 0)
			return haxe.io.Bytes.alloc(0);
		final heap:Dynamic = Raw.host["HEAPU8"];
		final copy:js.lib.Uint8Array = heap.slice(pointer, pointer + size);
		return haxe.io.Bytes.ofData(copy.buffer);
	}
}
