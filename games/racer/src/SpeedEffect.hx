import wgf.*;

/**
	The speed effect: a screen effect of the game's own (shaders/speed.glsl, packed into
	assets/shaders/speed.wgfshader by tools/pack_shaders.py) over the finished frame. Toward
	its edges the world blurs toward the middle and darkens, and everything warms a little,
	from none below FROM to all of it at FULL, set from the car's speed every frame.

	An effect redraws everything drawn, the HUD too, so the HUD's panels and the mirror are
	given to it as rectangles it leaves as they are (`keep0`..`keep2`, in the frame's 0..1).

	Probe: racer.effect, its strength (0..1).
**/
class SpeedEffect {
	static inline var FROM = 8.0; // m/s (29 km/h): none below
	static inline var FULL = 32.0; // m/s (115 km/h): all of it, about the lap's top speed
	static inline var RADIUS = 0.45; // where it starts, a fraction of the half-diagonal
	static inline var BLUR = 0.12; // how far toward the middle an edge pixel reaches, of its distance
	static inline var DARK = 0.45; // the corners' darkening at full strength

	public static var strength(default, null) = 0.0;
	static var material:Material = 0;

	public static function create() {
		final shader = Shader.create("shaders/speed.wgfshader");
		material = Material.createCustom(shader);
		Resource.release(shader); // the material holds its own
		material.setFloat("radius", RADIUS);
		material.setFloat("blur", BLUR);
		material.setFloat("dark", DARK);
		material.setVec4("warm", 1.0, 0.93, 0.82, 1.0);
		Render.addEffect(material); // skipped until its shader is READY
		set(0);
	}

	/** Its strength from the car's speed (m/s), shown or not (0 off: the menus, the grid). **/
	public static function update(speed:Float, on:Bool) {
		set(on ? Math.max(0, Math.min(1, (Math.abs(speed) - FROM) / (FULL - FROM))) : 0);
	}

	/** The rectangles left as they are, in the design's pixels, inside the visible area `v`. **/
	public static function keep(v:Vec4, i:Int, r:Vec4) {
		if (material != 0)
			material.setVec4('keep$i', (r.x - v.x) / v.z, (r.y - v.y) / v.w, r.z / v.z, r.w / v.w);
	}

	public static function clearKeep(i:Int) {
		if (material != 0)
			material.setVec4('keep$i', 0, 0, 0, 0);
	}

	static function set(s:Float) {
		strength = s;
		if (material != 0)
			material.setFloat("strength", s);
		Probe.setValue("racer.effect", s);
	}
}
