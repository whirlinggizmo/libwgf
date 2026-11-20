import wgf.*;

/**
	The effects: death bursts of sparks, the target ring and the spawn warnings (the game's
	own shaders on quads laid over the ground), the hit flash's materials, and the meshes and
	materials of shots and gems.
**/
class Fx {
	static inline var BURSTS = 50; // emitters taken in turn, 40 sparks each: 2,000 alive at most
	public static inline var SPARKS = 40;
	public static inline var SPARK_LIFE = 0.6;

	static var bursts:Array<Emitter3d> = [];
	static var nextBurst = 0;

	public static var ring:Model = 0;
	static var ringMaterial:Material = 0;

	static var flashMaterials = new Map<String, Material>();

	public static var shotMesh:Mesh = 0;
	public static var shotMaterial:Material = 0;
	public static var gemMesh:Mesh = 0;
	public static var gemMaterial:Material = 0;
	static var quad:Mesh = 0;

	public static function create(parent:Actor) {
		for (_ in 0...BURSTS) {
			final e = Emitter3d.create();
			e.setMax(SPARKS);
			e.setLife(SPARK_LIFE, SPARK_LIFE);
			e.setAlphaMode(AlphaMode.ADD, 0); // added to what is behind: they glow where they cross
			e.setSpawnSphere(0.25);
			e.setVelocity(0, 4.5, 0, 1.3, 0.6); // out and up, every way within 75 degrees of up
			e.setGravity(0, -12, 0); // and falling
			e.setDrag(0.6);
			e.setSize(0.16, 0.06, 0.3);
			e.setStretch(0.03);
			e.setParent(parent);
			bursts.push(e);
		}

		quad = Mesh.createPlane(1, 1, 0);
		ringMaterial = Material.createCustom(Assets.ringShader);
		ringMaterial.setAlphaMode(AlphaMode.BLEND, 0);
		ringMaterial.setColor("color", Color.make(120, 230, 255, 200));
		ringMaterial.setFloat("width", 0.08);
		ring = Model.create(quad);
		ring.setMaterial(-1, ringMaterial);
		ring.setShadowCasting(false);
		ring.setParent(parent);

		shotMesh = Mesh.createSphere(0.12, 8, 12);
		shotMaterial = Material.create(MaterialShading.UNLIT);
		shotMaterial.setVec4("base_color", 4.0, 3.2, 1.0, 1.0); // past white: it glows

		gemMesh = Mesh.createSphere(0.16, 2, 4); // two rings of four: an octahedron, a crystal
		gemMaterial = Material.create(MaterialShading.PBR);
		gemMaterial.setVec4("base_color", 0.1, 0.9, 0.3, 1.0);
		gemMaterial.setVec3("emissive", 0.05, 0.9, 0.25);
		gemMaterial.setFloat("metallic", 0.1);
		gemMaterial.setFloat("roughness", 0.2);
	}

	/** A death burst of 40 sparks of `color` at (x, y, z), fading out over 0.6 s. **/
	public static function burst(x:Float, y:Float, z:Float, color:Int) {
		final e = bursts[nextBurst];
		nextBurst = (nextBurst + 1) % BURSTS;
		e.skipTrail(); // put there, not moved there
		e.setPosition(x, y, z);
		e.setColor(color, Color.withAlpha(color, 0));
		e.burst(SPARKS);
	}

	public static function clearBursts() {
		for (e in bursts)
			e.clear();
	}

	/** The ring around the warden, `radius` her sword's reach. **/
	public static function placeRing(x:Float, z:Float, radius:Float, visible:Bool) {
		final size = 2 * radius + 0.6;
		ring.setTransform(x, 0.025, z, 0, 0, 0, size, 1, size);
		ring.setVisible(visible);
		ringMaterial.setVec2("center", x, z);
		ringMaterial.setFloat("radius", radius);
	}

	/** A spawn warning: a quad with a material of its own, whose fill the game sets each tick. **/
	public static function makeWarning(parent:Actor, x:Float, z:Float, radius:Float):Model {
		final material = Material.createCustom(Assets.warningShader);
		material.setAlphaMode(AlphaMode.BLEND, 0);
		material.setColor("color", Color.make(255, 40, 30, 230));
		material.setVec2("center", x, z);
		material.setFloat("radius", radius);
		material.setFloat("fill", 0);
		final m = Model.create(quad);
		m.setMaterial(-1, material);
		Resource.release(material); // the model holds it
		m.setShadowCasting(false);
		m.setTransform(x, 0.03, z, 0, 0, 0, 2 * radius + 0.3, 1, 2 * radius + 0.3);
		m.setParent(parent);
		return m;
	}

	public static function setWarningFill(warning:Model, fill:Float)
		warning.getMaterial(0).setFloat("fill", fill);

	/** The flash material for slot `slot` of a part's mesh: the game's shader, its base the slot's own color. **/
	public static function flashMaterial(mesh:Mesh, slot:Int):Material {
		final key = '${(mesh : Int)}:$slot';
		var material = flashMaterials.get(key);
		if (material == null) {
			material = Material.createCustom(Assets.flashShader);
			final own = mesh.getMaterial(slot);
			final base = own.isNone() ? new Vec4(1, 1, 1, 1) : own.getVec4("base_color");
			material.setVec4("base", base.x, base.y, base.z, base.w);
			material.setFloat("flash", 0.85);
			flashMaterials.set(key, material);
		}
		return material;
	}
}
