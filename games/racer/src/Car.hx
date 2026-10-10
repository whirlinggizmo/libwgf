import wgf.*;

/**
	The player's car: the input, as named actions (bound once, in Main.bindControls), turned
	into the drive's intent at the tick rate. How that moves the car is Drive's (the
	vehicle's) alone.
**/
class Car extends Behavior {
	static inline var PAINT_ROUGHNESS = 0.22;
	static inline var GLASS_ROUGHNESS = 0.08;
	static inline var STEER_RATE = 4.0; // full lock in a quarter of a second, from keys

	public var drive:Drive;
	public var steer(default, null) = 0.0;

	/** Where the car is drawn this frame, and its heading there: what the camera follows. **/
	public final drawn = new Vec3();
	public var drawnHeading = 0.0;

	final ahead = new Vec3();

	override function onCreate() {
		drive = new Drive(actor);
		paint();
		Main.carReady(this);
	}

	override function onTick(dt:Float) {
		if (Main.state == Main.State.RESULTS) { // past the line: coasting to a stop
			drive.update(0, 0.3, 0, false);
			return;
		}
		if (Main.state != Main.State.RACING) {
			drive.hold();
			return;
		}
		final want = Action.getAxis("steer"); // -1 to 1
		steer += Math.max(-STEER_RATE * dt, Math.min(STEER_RATE * dt, want - steer)); // keys ease in
		drive.update(Action.getValue("throttle"), Action.getValue("brake"), steer, Action.isDown("handbrake"));
	}

	override function onFrame(dt:Float) {
		actor.getDrawnPosition(drawn);
		final d = actor.getDrawnDirection(0, 0, 1, ahead);
		drawnHeading = Math.atan2(d.x, d.z);
	}

	/**
		The body's paint and the cabin's glass: glossy, so they reflect the sky (the scene's generated cube has the
		mesh's own material, rough 0.5, shared by every cube of its size; a scene line can tint
		a model but not set a material's numbers). Its own material, red in the material, the
		model's tint white.
	**/
	function paint() {
		gloss("body", Color.make(200, 40, 44, 255), PAINT_ROUGHNESS);
		gloss("cabin", Color.make(22, 28, 38, 255), GLASS_ROUGHNESS); // dark glass: the sky shows in it
	}

	function gloss(part:String, color:Int, roughness:Float) {
		final model:Model = actor.find(part);
		final m = Material.create(MaterialShading.PBR);
		m.setColor("base_color", color);
		m.setFloat("metallic", 0);
		m.setFloat("roughness", roughness);
		model.setMaterial(-1, m);
		model.setTint(Color.get(ColorStock.WHITE));
		Resource.release(m); // the model holds its own
	}

	/** Back to `x, z`, turned to `heading`, at rest: the grid. **/
	public function reset(x:Float, z:Float, heading:Float) {
		drive.place(x, z, heading);
		steer = 0;
		actor.getPosition(drawn);
		drawnHeading = heading;
	}
}
