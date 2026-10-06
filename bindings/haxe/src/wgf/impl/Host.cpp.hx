package wgf.impl;

/**
	The hxcpp target's help for Raw.cpp.hx: a C call takes floats where a Haxe array holds
	doubles, so floats are copied into a buffer of floats for the call and, for one C
	fills, back. An array of ints or handles is passed as it is: hxcpp keeps it as ints.
**/
class Host {
	public static function isAttached():Bool
		return true;

	/** `values` as floats, for the call: a copy, or room (`copy` false) for C to fill. **/
	public static function floats(values:Array<Float>, copy:Bool):Array<cpp.Float32> {
		if (values == null)
			return null;
		final out = new Array<cpp.Float32>();
		out.resize(values.length);
		if (copy)
			for (i in 0...values.length)
				out[i] = values[i];
		return out;
	}

	/** The floats C filled, back into `values`. **/
	public static function floatsBack(floats:Array<cpp.Float32>, values:Array<Float>):Void {
		if (values == null)
			return;
		for (i in 0...values.length)
			values[i] = floats[i];
	}
}
