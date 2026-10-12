import wgf.*;

/**
	The racer's slice: a box car on a track of Kenney's Racing Kit pieces, streamed in
	(Stream), driven on physics (Drive: a Jolt wheeled vehicle, physics3d's), a chase camera,
	checkpoints in order and lap times, and a HUD of drawn text.

	What the framework does: the stage draws the track, car, and trees; physics drives the
	car on the track's static body from the intent, the ecs draws it between ticks, and the
	checkpoints' sensors raise their triggers. What
	this does: the race's states, the camera, the HUD.

	The race starts on the throttle, not when loading ends: loading takes real time, which
	differs between runs, and an autopilot's inputs are counted in frames, so a start the
	player makes is one a recorded lap replays at the same point. The track streams in from
	then on (Stream): the loading screen holds only what the grid needs, and nothing loads
	between it and the start, so a recording's count, which starts where the loads end, starts
	before the grid's frames on every run.

	The car is put back at the last checkpoint passed, facing along the track, on a key (or
	pad's Back), and by itself when it has been upside down, past the barriers, or stuck
	(Car.onTick).

	Its probes (Laps.tick): racer.state (0 loading, 1 ready, 2 countdown, 3 racing, 4 results),
	racer.lap, racer.checkpoint (the one wanted next), racer.passed (checkpoints passed in
	order since the start), racer.lap_time, racer.last, racer.best, racer.speed, racer.steer, racer.grass,
	racer.recoveries; and Stream's.
**/
class Main {
	public static inline var WIDTH = 1280;
	public static inline var HEIGHT = 720;
	static inline var COUNTDOWN = 3.0;
	static inline var SHADOW_DISTANCE = 35.0; // m of the camera's view the sun's map covers
	// the light, tuned with the sky: how much the panorama lights, the sun beside it, exposure
	static inline var SKY_LIGHT = 0.7;
	static inline var SUN = 3.5;
	static inline var EXPOSURE = 0.0;
	// the sky's sun (examples' sky.hdr, tools/gen_sky.py: 25 degrees up, 40 from -z to +x)
	static inline var SUN_X = 0.5826; // cos 25 sin 40
	static inline var SUN_Y = 0.4226; // sin 25
	static inline var SUN_Z = -0.6943; // -cos 25 cos 40
	// the rear-view mirror and the speed effect, each switched off to measure what it costs
	// (tools/frames.py; FRICTION part 8)
	static inline var MIRROR = true;
	static inline var EFFECT = true;

	public static var stage:Stage3d;
	public static var scene:Scene;
	public static var car:Null<Car> = null;
	public static var state = State.LOADING;

	static var camera:ChaseCamera;
	static var countdown = COUNTDOWN;
	static var showBodies = false;
	static var placed = false;
	static var sky:Environment = 0;
	static var track:Scene = 0;
	static var recoveries = 0;
	static var storage:FsTask = 0;
	static var failed = false; // a scene didn't load: said on the loading screen, which stays
	static var gantry:Text = 0; // the start gantry's lap board (section 0's), once it is placed
	static var gantrySays = "";

	static function main() {
		Window.setTitle("Racer");
		Presentation.set(PresentationMode.EXPAND, WIDTH, HEIGHT); // the design at least, the page filled
		Runtime.run(init, tick, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		// the browser's file store, open (and cleared of an older build's files) before the
		// manifest and the start's files are asked for: a request waits for it (wgf_fs.h)
		storage = Fs.exists("manifest.json");
		Render.setClearColor(Color.make(150, 196, 236, 255)); // behind the sky until it has loaded
		Loop.setTickRate(60); // the car's intent and the ecs, at a fixed rate

		Physics.setGravity(0, -9.81, 0); // physics started: before the scene's bodies load
		stage = Stage3d.create();
		stage.setTonemap(Stage3dTonemap.NEUTRAL, EXPOSURE);
		// the sun: aimed along the sky's own sun (25 degrees up, 40 from -z toward +x, the
		// panorama unturned), so the light, the shadows, and the bright disc in the sky agree
		final sun = Light.create(LightType.DIRECTIONAL);
		sun.setIntensity(SUN);
		sun.setColor(Color.make(255, 240, 220, 255));
		sun.setParent(stage); // a light is an actor on the stage
		sun.lookAt(-SUN_X, -SUN_Y, -SUN_Z, 0, 1, 0); // shining down its -z, from the origin, away from the sky's sun
		// the sun casts: one map fitted around what the chase camera sees, SHADOW_DISTANCE out
		sun.setShadowCasting(true);
		sun.setShadowDistance(SHADOW_DISTANCE);
		sun.setShadowMapSize(2048);
		sun.setShadowStrength(0.85);

		camera = new ChaseCamera(stage);
		if (EFFECT)
			SpeedEffect.create();

		bindControls();
		Behavior.register("Car", e -> new Car(e));
		Behavior.register("Checkpoint", e -> new Laps.Checkpoint(e));
		#if debug
		Debug.showFps(0, 8, 8, 14, Color.get(ColorStock.LIME)); // development builds only
		#end
		Laps.tick(0);
	}

	/** The controls, as named actions: keys, pad buttons, and axes bound to each. **/
	static function bindControls() {
		Action.bindKeys("steer", KeyboardKey.LEFT, KeyboardKey.RIGHT);
		Action.bindKeys("steer", KeyboardKey.A, KeyboardKey.D);
		Action.bindPadAxis("steer", GamepadAxis.LEFT_X, 0, 0.2); // past a worn stick's drift
		Action.bindKey("throttle", KeyboardKey.UP);
		Action.bindKey("throttle", KeyboardKey.W);
		Action.bindPadAxis("throttle", GamepadAxis.RIGHT_TRIGGER, 1, 0.05);
		Action.bindPadButton("throttle", GamepadButton.SOUTH);
		Action.bindKey("brake", KeyboardKey.DOWN);
		Action.bindKey("brake", KeyboardKey.S);
		Action.bindPadAxis("brake", GamepadAxis.LEFT_TRIGGER, 1, 0.05);
		Action.bindPadButton("brake", GamepadButton.WEST);
		Action.bindKey("handbrake", KeyboardKey.SPACE);
		Action.bindPadButton("handbrake", GamepadButton.EAST);
		Action.bindKey("restart", KeyboardKey.R);
		Action.bindKey("recover", KeyboardKey.BACKSPACE); // back on the track at the last checkpoint
		Action.bindKey("recover", KeyboardKey.T);
		Action.bindPadButton("recover", GamepadButton.BACK);
		Action.bindKey("bodies", KeyboardKey.B); // the physics debug view
		Action.bindPadButton("restart", GamepadButton.START);
	}

	/** The car's behavior is made: the race can start. **/
	public static function carReady(c:Car) {
		car = c;
		Mirror.create(c.actor);
		grid();
	}

	static function spawn() {
		scene.spawnPrefab("car", stage);
	}

	/** The car on the grid, behind the line, waiting for the start (or counting down to it). **/
	static function grid(?counting = false) {
		car.reset(Track.xs[GRID], Track.zs[GRID], Track.headings[GRID]);
		camera.snap(car);
		Laps.reset();
		countdown = COUNTDOWN;
		state = counting ? State.COUNTDOWN : State.READY;
	}

	// 12 m back from the line's sensor, so the line is crossed after the start
	static final GRID = (Track.checkpointSample(0) - 6 + Track.xs.length) % Track.xs.length;

	/**
		The car back on the track: on the last checkpoint passed (the grid before the first),
		a few metres on, facing along the track, at rest.
	**/
	public static function recover() {
		final s = Laps.passed == 0 ? GRID : (Track.checkpointSample((Laps.next + Track.CHECKPOINTS - 1) % Track.CHECKPOINTS) + 2) % Track.xs.length;
		car.reset(Track.xs[s], Track.zs[s], Track.headings[s]);
		camera.snap(car);
		recoveries++;
	}

	static function tick() {
		final dt = Loop.getTickDelta();
		switch state {
			case State.LOADING:
				load();
			case State.READY:
				if (Action.isPressed("throttle")) {
					state = State.COUNTDOWN;
					Stream.start(); // the rest of the track, from now on
				}
			case State.COUNTDOWN:
				countdown -= dt;
				if (countdown <= 0) {
					state = State.RACING;
					Stream.raced();
				}
			case State.RACING:
				if (Laps.lap > Laps.LAPS) {
					state = State.RESULTS;
					Ui.setFocus("again");
				}
			case State.RESULTS:
		}
		if (state != State.LOADING && Action.isPressed("restart"))
			grid(true);
		if (state == State.RACING && Action.isPressed("recover"))
			recover();
		if (state != State.LOADING)
			Stream.update(car.drive.sample);
		Laps.tick(dt);
		Probe.setValue("racer.recoveries", recoveries);
		gantryBoard();
	}

	/** The start gantry's board: the lap the car is on, the last one called, then the finish. **/
	static function gantryBoard() {
		if (gantry == 0 && (gantry = (stage : Actor).find("gantry_board/lap")) == 0)
			return;
		final lap = Std.int(Math.min(Laps.lap, Laps.LAPS));
		final says = state == State.RESULTS ? "FINISH" : (lap == Laps.LAPS && Laps.LAPS > 1 ? "FINAL LAP" : 'LAP $lap / ${Laps.LAPS}');
		if (says != gantrySays) {
			gantry.setString(says);
			gantrySays = says;
			Probe.setText("racer.gantry", says);
		}
	}

	/**
		The start: once its files are local, the sky, the scenes (the car's, and the track's
		collision and checkpoints, made once its file has loaded), and the grid's sections;
		then the car on the grid once all of them are in, the sky prepared on a worker (or
		failed), so the start a player makes is after every load a recording counts from.
	**/
	static function load() {
		if (storage != 0) {
			if (storage.getStatus() == FsTaskStatus.PENDING)
				return;
			storage.destroy();
			storage = 0;
			Asset.setManifest("manifest.json"); // a second visit reads everything from the cache
			Stream.begin(stage); // the start's files, behind the loading screen
		}
		if (!Stream.filesLocal())
			return;
		if (sky == 0) {
			// the sky: one panorama lighting the stage (soft light all around, and what glossy
			// surfaces reflect) and drawn behind it; it takes the place of a flat ambient light
			sky = Environment.create("sky/sky.hdr");
			stage.setEnvironment(sky, SKY_LIGHT, 0);
			stage.setBackground(sky, 0);
			scene = Scene.create("scenes/racer.scene");
			track = Scene.create("scenes/track.scene");
		}
		failed = Resource.getStatus(scene) == ResourceStatus.FAILED || Resource.getStatus(track) == ResourceStatus.FAILED;
		if (!placed && Resource.getStatus(scene) == ResourceStatus.READY && Resource.getStatus(track) == ResourceStatus.READY) {
			track.instantiate(stage);
			placed = true;
		}
		Stream.update(GRID);
		if (placed && Track.collisionPlaced(stage) && Actor.countWithBehavior("Checkpoint") == Track.CHECKPOINTS
			&& Stream.gridReady() && car == null && Resource.getStatus(sky) != ResourceStatus.PENDING
			&& Actor.countWithBehavior("Car") == 0)
			spawn();
	}

	static function frame() {
		#if wgf_record
		telemetry();
		#end
		if (car != null)
			camera.update(car, Loop.getFrameDelta());
		Mirror.draw(stage, MIRROR && (state == State.COUNTDOWN || state == State.RACING)); // into its target, drawn before the screen
		stage.draw();
		SpeedEffect.update(car == null ? 0 : car.drive.speed, state == State.RACING);
		if (Action.isPressed("bodies"))
			showBodies = !showBodies;
		if (showBodies && Draw.begin3d(camera.camera)) {
			Physics.drawBodies(Color.get(ColorStock.YELLOW));
			Draw.end3d();
		}
		hud();
		if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE))
			App.quit();
	}

	#if wgf_record
	/**
		The build that records a lap by hand also says, each frame, where the car is, and once
		the track's centerline: what tools/drive.py steers by when no hand is at the keys.
	**/
	static var frames = 0;

	static function telemetry() {
		if (frames == 0)
			for (i in 0...Std.int((Track.xs.length + 15) / 16))
				Log.message(LogLevel.INFO, 'racer.track ' + [for (k in i * 16...Std.int(Math.min(i * 16 + 16, Track.xs.length))) '${Track.xs[k]},${Track.zs[k]}'].join(" "));
		frames++;
		if (car != null)
			Log.message(LogLevel.INFO, 'racer.tel $frames $state ${car.drawn.x} ${car.drawn.z} ${car.drive.heading} ${car.drive.speed} ${Laps.passed} ${Laps.lap}');
	}
	#end

	static final visible = new Vec4();
	static final panel = new Vec4();

	static function hud() {
		final white = Color.get(ColorStock.WHITE);
		final shade = Color.make(0, 0, 0, 140);
		final v = Presentation.getVisible(visible); // under EXPAND, past the design on a wide page
		final left = v.x + 24, top = v.y + 20, right = v.x + v.z - 24, bottom = v.y + v.w - 24;
		if (state == State.LOADING) {
			loadingScreen();
			return;
		}
		Draw.rectangle(left - 12, top - 8, 300, 128, shade);
		SpeedEffect.keep(v, 0, panel.set(left - 12, top - 8, 300, 128));
		// the speed's panel: the effect leaves both panels as they are, so each is a shaded box
		Draw.rectangle(right - 138, bottom - 88, 150, 100, shade);
		SpeedEffect.keep(v, 1, panel.set(right - 138, bottom - 88, 150, 100));
		Draw.text(0, 'LAP ${Std.int(Math.min(Laps.lap, Laps.LAPS))}/${Laps.LAPS}', left, top, 32, white);
		Draw.text(0, 'time  ${Laps.format(Laps.current)}', left, top + 42, 20, white);
		Draw.text(0, 'best  ${Laps.best > 0 ? Laps.format(Laps.best) : "-:--.--"}', left, top + 66, 20, Color.get(ColorStock.GOLD));
		final kmh = Math.round(Math.abs(car.drive.speed) * 3.6);
		Draw.textAligned(0, '$kmh', right, bottom - 22, 56, white, TextHalign.RIGHT, TextValign.BOTTOM);
		Draw.textAligned(0, 'km/h', right, bottom, 18, white, TextHalign.RIGHT, TextValign.BOTTOM);
		Draw.text(0, 'checkpoint ${Laps.passed == 0 ? 0 : (Laps.next == 0 ? Track.CHECKPOINTS : Laps.next)}/${Track.CHECKPOINTS}', left, top + 92, 16, Color.get(ColorStock.LIGHTGRAY));
		Mirror.show(v);
		if (Mirror.shown)
			SpeedEffect.keep(v, 2, Mirror.rect);
		else
			SpeedEffect.clearKeep(2);
		if (state == State.READY)
			Draw.textAligned(0, 'press ${Action.getBindingText("throttle", 0)} to start', v.x + v.z / 2, v.y + v.w * 0.35, 40, white,
				TextHalign.CENTER, TextValign.MIDDLE);
		if (state == State.COUNTDOWN)
			Draw.textAligned(0, '${Math.ceil(countdown)}', v.x + v.z / 2, v.y + v.w * 0.35, 120, white, TextHalign.CENTER, TextValign.MIDDLE);
		else if (state == State.RACING && Laps.passed <= 1 && Laps.current < 1.5)
			Draw.textAligned(0, "GO", v.x + v.z / 2, v.y + v.w * 0.35, 120, Color.get(ColorStock.LIME), TextHalign.CENTER, TextValign.MIDDLE);
		if (state == State.RESULTS)
			results();
	}

	static function loadingScreen() {
		if (!Ui.begin())
			return;
		Ui.beginPanel("loading");
		Ui.setAlign(UiAlign.CENTER, UiAlign.START);
		Ui.label("RACER", 64);
		Ui.spacer(8);
		Ui.progress(Stream.progress()); // the start's files, then the pieces loading
		Ui.label(failed ? "the track didn't load: see the log" : "loading the track", 18);
		Ui.endPanel();
		Ui.end();
	}

	static function results() {
		if (!Ui.begin())
			return;
		Ui.beginPanel("results");
		Ui.setAlign(UiAlign.CENTER, UiAlign.START);
		Ui.label("FINISHED", 48);
		Ui.label('best lap ${Laps.format(Laps.best)}', 24);
		Ui.spacer(12);
		if (Ui.button("again", "Race again"))
			grid(true);
		Ui.endPanel();
		Ui.end();
	}
}

enum abstract State(Int) to Int {
	var LOADING = 0;
	var READY = 1; // on the grid, waiting for the throttle to start the countdown
	var COUNTDOWN = 2;
	var RACING = 3;
	var RESULTS = 4;
}
