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
	var warming = 0; // frames left of the smoke's first puffs, made on the grid and then cleared
	var upsideDown = 0.0;
	var offTrack = 0.0;
	var stuck = 0.0;

	override function onCreate() {
		drive = new Drive(actor);
		paint();
		for (name in ["smoke_rl", "smoke_rr"])
			smoke.push(curves(actor.find(name)));
		// a puff each now, behind the loading screen, cleared two frames on: an emitter's first
		// particles cost a frame of about 25 ms (the stream check's worst frame, at the
		// race's wheelspinning start), so that frame is the grid's, not the race's
		for (e in smoke)
			e.burst(1);
		warming = 2;
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

	/**
		A puff's life, beyond the scene line's two keys: it fades in as it leaves the tire,
		grows fast and then slowly, and greys as it thins.
	**/
	static function curves(e:Emitter3d):Emitter3d {
		e.clearSizeKeys();
		e.addSizeKey(0, 0.4);
		e.addSizeKey(0.25, 1.8);
		e.addSizeKey(1, 3.6);
		e.clearColorKeys();
		e.addColorKey(0, Color.make(235, 235, 235, 0));
		e.addColorKey(0.08, Color.make(235, 235, 235, 110));
		e.addColorKey(0.5, Color.make(205, 205, 205, 60));
		e.addColorKey(1, Color.make(180, 180, 180, 0));
		return e;
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
		if (warming > 0 && --warming == 0)
			for (e in smoke)
				e.clear();
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
