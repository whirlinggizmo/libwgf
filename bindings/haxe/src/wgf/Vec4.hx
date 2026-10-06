package wgf;

/**
    4 floats: what a getter of a wgf_vec4_t gives back. A getter takes one to fill
    (`node.getPosition(into)`), so a loop calling it makes none; without one, it makes one.
    The binding's math is its own: C's math is header-only, and nothing in C takes one.
**/
class Vec4 {
	public var x:Float;
	public var y:Float;
	public var z:Float;
	public var w:Float;

	public inline function new(x:Float = 0, y:Float = 0, z:Float = 0, w:Float = 0) {
		this.x = x;
		this.y = y;
		this.z = z;
		this.w = w;
	}

	public inline function set(x:Float, y:Float, z:Float, w:Float):Vec4 {
		this.x = x;
		this.y = y;
		this.z = z;
		this.w = w;
		return this;
	}

	public function toString():String
		return '($x, $y, $z, $w)';
}
