package wgf;

/**
    2 floats: what a getter of a wgf_vec2_t gives back. A getter takes one to fill
    (`node.getPosition(into)`), so a loop calling it makes none; without one, it makes one.
    The binding's math is its own: C's math is header-only, and nothing in C takes one.
**/
class Vec2 {
	public var x:Float;
	public var y:Float;

	public inline function new(x:Float = 0, y:Float = 0) {
		this.x = x;
		this.y = y;
	}

	public inline function set(x:Float, y:Float):Vec2 {
		this.x = x;
		this.y = y;
		return this;
	}

	public function toString():String
		return '($x, $y)';
}
