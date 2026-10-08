import wgf.*;

/**
	The racer's slice: a box car on a flat track of generated meshes, driven arcade-style
	(Drive: a Jolt wheeled vehicle, physics3d's), a chase camera, checkpoints in
	order and lap times, and a HUD of drawn text.

	What the framework does: the stage draws the track, car, and trees; physics drives the
	car on the track's static body from the intent, the ecs draws it between ticks, and the
	checkpoints' sensors raise their triggers. What
	this does: the race's states, the camera, the HUD.

	The race starts on the throttle, not when loading ends: loading takes real time, which
	differs between runs, and an autopilot's inputs are counted in frames, so a start the
	player makes is one a recorded lap replays at the same point.

	Its probes (Laps.tick): racer.state (0 loading, 1 ready, 2 countdown, 3 racing, 4 results),
	racer.lap, racer.checkpoint (the one wanted next), racer.passed (checkpoints passed in
	order since the start), racer.lap_time, racer.last, racer.best, racer.speed, racer.steer, racer.grass.
**/
class Main {
	public static inline var WIDTH = 1280;
	public static inline var HEIGHT = 720;
	static inline var COUNTDOWN = 3.0;

	public static var stage:Stage3d;
	public static var scene:Scene;
	public static var car:Null<Car> = null;
	public static var state = State.LOADING;

	static var camera:ChaseCamera;
	static var countdown = COUNTDOWN;
	static var showBodies = false;

	static function main() {
		Window.setTitle("Racer");
		Presentation.set(PresentationMode.EXPAND, WIDTH, HEIGHT); // the design at least, the page filled
		Runtime.run(init, tick, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Render.setClearColor(Color.make(150, 196, 236, 255)); // the sky
		Loop.setTickRate(60); // the car's intent and the ecs, at a fixed rate

		Physics.setGravity(0, -9.81, 0); // physics started: before the scene's bodies load
		stage = Stage3d.create();
		stage.setAmbient(Color.make(190, 210, 255, 255), 0.55);
		final sun = Light.create(LightType.DIRECTIONAL);
		sun.setIntensity(2.6);
		sun.setColor(Color.make(255, 244, 228, 255));
		sun.setParent(stage); // a light is an actor on the stage
		sun.lookAt(-0.5, -1, -0.35, 0, 1, 0); // shining down its -z, from the origin

		Track.create(stage);
		Probe.setValue("racer.models", Track.models);
		camera = new ChaseCamera(stage);
		scene = Scene.create("scenes/racer.scene");

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
		Action.bindKey("bodies", KeyboardKey.B); // the physics debug view
		Action.bindPadButton("restart", GamepadButton.START);
	}

	/** The car's behavior is made: the race can start. **/
	public static function carReady(c:Car) {
		car = c;
		grid();
	}

	static function spawn() {
		scene.spawnPrefab("car", stage);
		for (i in 0...Track.CHECKPOINTS) {
			final s = Track.checkpointSample(i);
			final gate = scene.spawnPrefab("checkpoint", stage, Track.xs[s], 2, Track.zs[s], Track.headings[s]); // its sensor 4 m tall
			(gate : BehaviorComponent).setParam(gate.findBehavior("Checkpoint"), "index", '$i');
		}
	}

	/** The car on the grid, behind the line, waiting for the start (or counting down to it). **/
	static function grid(?counting = false) {
		// 12 m back from the line's sensor, so the line is crossed after the start
		final s = (Track.checkpointSample(0) - 6 + Track.xs.length) % Track.xs.length;
		car.reset(Track.xs[s], Track.zs[s], Track.headings[s]);
		camera.snap(car);
		Laps.reset();
		countdown = COUNTDOWN;
		state = counting ? State.COUNTDOWN : State.READY;
	}

	static function tick() {
		final dt = Loop.getTickDelta();
		switch state {
			case State.LOADING:
				if (Resource.getStatus(scene) == ResourceStatus.READY && car == null && Actor.countWithBehavior("Car") == 0)
					spawn(); // the car's behavior starts the countdown once it is made
			case State.READY:
				if (Action.isPressed("throttle"))
					state = State.COUNTDOWN;
			case State.COUNTDOWN:
				countdown -= dt;
				if (countdown <= 0)
					state = State.RACING;
			case State.RACING:
				if (Laps.lap > Laps.LAPS) {
					state = State.RESULTS;
					Ui.setFocus("again");
				}
			case State.RESULTS:
		}
		if (state != State.LOADING && Action.isPressed("restart"))
			grid(true);
		Laps.tick(dt);
	}

	static function frame() {
		#if wgf_record
		telemetry();
		#end
		if (car != null)
			camera.update(car, Loop.getFrameDelta());
		stage.draw();
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

	static function hud() {
		final white = Color.get(ColorStock.WHITE);
		final shade = Color.make(0, 0, 0, 140);
		final v = Presentation.getVisible(visible); // under EXPAND, past the design on a wide page
		final left = v.x + 24, top = v.y + 20, right = v.x + v.z - 24, bottom = v.y + v.w - 24;
		if (state == State.LOADING) {
			Draw.textAligned(0, "loading", v.x + v.z / 2, v.y + v.w / 2, 32, white, TextHalign.CENTER, TextValign.MIDDLE);
			return;
		}
		Draw.rectangle(left - 12, top - 8, 300, 128, shade);
		Draw.text(0, 'LAP ${Std.int(Math.min(Laps.lap, Laps.LAPS))}/${Laps.LAPS}', left, top, 32, white);
		Draw.text(0, 'time  ${Laps.format(Laps.current)}', left, top + 42, 20, white);
		Draw.text(0, 'best  ${Laps.best > 0 ? Laps.format(Laps.best) : "-:--.--"}', left, top + 66, 20, Color.get(ColorStock.GOLD));
		final kmh = Math.round(Math.abs(car.drive.speed) * 3.6);
		Draw.textAligned(0, '$kmh', right, bottom - 22, 56, white, TextHalign.RIGHT, TextValign.BOTTOM);
		Draw.textAligned(0, 'km/h', right, bottom, 18, white, TextHalign.RIGHT, TextValign.BOTTOM);
		Draw.text(0, 'checkpoint ${Laps.passed == 0 ? 0 : (Laps.next == 0 ? Track.CHECKPOINTS : Laps.next)}/${Track.CHECKPOINTS}', left, top + 92, 16, Color.get(ColorStock.LIGHTGRAY));
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
