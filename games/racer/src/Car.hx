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
	// put back on the track by itself after this long: on its roof or side, past the
	// barriers (or fallen through the world), or not moving with the throttle or brake held
	static inline var UPSIDE_DOWN = 1.0;
	static inline var OFF_TRACK = 1.0;
	static inline var STUCK = 2.5;
	// tire smoke: none below this slip (the vehicle's: 1 the tire's limit, smoke from about
	// 1.5), then this many puffs a second for each unit above it, up to SMOKE_MAX
	static inline var SMOKE_FROM = 2.0;
	static inline var SMOKE_RATE = 20.0;
	static inline var SMOKE_MAX = 160.0;

	public var drive:Drive;
	public var steer(default, null) = 0.0;

	/** Where the car is drawn this frame, and its heading there: what the camera follows. **/
	public final drawn = new Vec3();
	public var drawnHeading = 0.0;

	final ahead = new Vec3();
	final smoke:Array<Emitter3d> = [];
	var upsideDown = 0.0;
	var offTrack = 0.0;
	var stuck = 0.0;

	override function onCreate() {
		drive = new Drive(actor);
		paint();
		for (name in ["smoke_rl", "smoke_rr"])
			smoke.push(actor.find(name));
		Main.carReady(this);
	}

	override function onTick(dt:Float) {
		if (Main.state == Main.State.RESULTS) { // past the line: coasting to a stop
			drive.update(0, 0.3, 0, false);
			smokeRates();
			return;
		}
		if (Main.state != Main.State.RACING) {
			drive.hold();
			smokeRates();
			return;
		}
		final want = Action.getAxis("steer"); // -1 to 1
		steer += Math.max(-STEER_RATE * dt, Math.min(STEER_RATE * dt, want - steer)); // keys ease in
		final throttle = Action.getValue("throttle"), brake = Action.getValue("brake");
		drive.update(throttle, brake, steer, Action.isDown("handbrake"));
		if (lost(dt, throttle > 0.1 || brake > 0.1))
			Main.recover();
		smokeRates();
	}

	/** Each rear tire's smoke from its wheel's slip (wheels 2 and 3, as the vehicle lists them). **/
	function smokeRates() {
		var total = 0.0;
		for (i in 0...smoke.length) {
			final slip = (actor : Vehicle).getWheelSlip(2 + i);
			final rate = slip > SMOKE_FROM ? Math.min(SMOKE_MAX, SMOKE_RATE * (slip - SMOKE_FROM)) : 0;
			smoke[i].setRate(rate);
			total += rate;
		}
		Probe.setValue("racer.smoke_rate", total);
		Probe.setValue("racer.smoke", smoke[0].getCount() + smoke[1].getCount());
	}

	/** Whether the car has been upside down, off the track, or stuck long enough to be put back. **/
	function lost(dt:Float, driving:Bool):Bool {
		upsideDown = drive.upright < 0.3 ? upsideDown + dt : 0;
		final off = Math.abs(drive.across) > Track.BARRIER_OFFSET + 1.5 || drive.height < -3;
		offTrack = off ? offTrack + dt : 0;
		stuck = driving && Math.abs(drive.speed) < 1 ? stuck + dt : 0;
		return upsideDown >= UPSIDE_DOWN || offTrack >= OFF_TRACK || stuck >= STUCK;
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
		for (e in smoke) {
			e.setRate(0);
			e.skipTrail(); // a jump, not a slide: nothing spawned along the way
		}
		steer = 0;
		upsideDown = offTrack = stuck = 0;
		actor.getPosition(drawn);
		drawnHeading = heading;
	}
}
