import wgf.*;

/**
	The racer's flow: load behind a screen, count down, race laps, show the results.
	What the framework does: the stage draws the track, car, props, sky, and shadows;
	physics3d drives the car from its intent; the ecs raises the checkpoints' triggers.
	What this does: the states, the camera, the HUD.
**/
class Main {
	public static var stage:Stage;
	public static var scene:Scene;
	public static var car:Entity = 0;
	public static var state = State.LOADING;

	static var camera:ChaseCamera;
	static var mirror:Texture; // a render target
	static var mirrorCamera:Camera3d;
	static var loading:AssetGroup;
	static var countdown = 3.0;

	static function main() {
		Window.setTitle("Racer");
		Window.setSize(1280, 720);
		Runtime.run(init, tick, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Asset.setManifest("manifest.json"); // a second visit reads everything from the cache

		stage = Stage.create();
		final sun = Light.create(LightType.DIRECTIONAL);
		sun.setRotation(-0.9, 0.6, 0);
		sun.setIntensity(3);
		sun.setShadowCasting(true);
		sun.setShadowDistance(80);
		stage.addLight(sun); // or: the light is a node under the stage
		stage.setEnvironment(Environment.create("sky/meadow_1k.hdr"), 1.0, 0);
		stage.setBackground(Environment.create("sky/meadow_1k.hdr"), 0);
		stage.setTonemap(Tonemap.PBR_NEUTRAL, 0);

		// the start: the collision mesh, the car, and the nearest sections; the rest streams
		loading = AssetGroup.create();
		loading.add("track/collision.glb");
		loading.add("track/section_00.glb");
		loading.add("car/car.glb");
		scene = Scene.create("scenes/racer.scene"); // prefabs: car, checkpoint, cone, tree

		camera = new ChaseCamera(stage);
		mirror = Texture.createTarget(320, 120);
		mirrorCamera = Camera3d.create();
		mirrorCamera.setFov(50);

		bindControls();
		Behavior.register("Car", e -> new Car(e));
		Behavior.register("Checkpoint", e -> new Laps.Checkpoint(e));
		Loop.setTickRate(60); // physics and the car's intent, at a fixed rate
		Debug.setFpsOverlay(true, 0, 16, Color.get(ColorStock.LIME)); // development builds only
	}

	/** The controls, as named actions: keys, pad buttons, and axes bound to each. **/
	static function bindControls() {
		Action.bindAxis("steer", KeyboardKey.LEFT, KeyboardKey.RIGHT);
		Action.bindAxis("steer", KeyboardKey.A, KeyboardKey.D);
		Action.bindPadAxis("steer", GamepadAxis.LEFT_X);
		Action.bindKey("throttle", KeyboardKey.UP);
		Action.bindPadAxis("throttle", GamepadAxis.RIGHT_TRIGGER);
		Action.bindKey("brake", KeyboardKey.DOWN);
		Action.bindPadAxis("brake", GamepadAxis.LEFT_TRIGGER);
		Action.bindKey("handbrake", KeyboardKey.SPACE);
		Action.bindPadButton("handbrake", GamepadButton.SOUTH);
	}

	static function tick() {
		switch state {
			case State.LOADING:
				if (loading.getStatus() == AssetTaskStatus.DONE && Resource.getStatus(scene) == ResourceStatus.READY) {
					scene.instantiate(stage); // the track's static parts, checkpoints, props
					car = scene.spawnAt("car", stage, 0, 0.5, 0, 0); // placed, turned, and snapped
					camera.follow(car);
					state = State.COUNTDOWN;
				}
			case State.COUNTDOWN:
				countdown -= Loop.getTickDelta();
				if (countdown <= 0)
					state = State.RACING;
			case State.RACING:
				if (Laps.lap > Laps.LAPS)
					state = State.RESULTS;
			case State.RESULTS:
		}
		Laps.publish();
	}

	static function frame() {
		if (state == State.LOADING) {
			loadingScreen();
			return;
		}
		camera.update(Loop.getFrameDelta());

		// the mirror: the same stage, from the car looking back, into the target
		final at = car.getPosition();
		final heading = car.getRotation().y;
		mirrorCamera.setPosition(at.x, at.y + 1.2, at.z);
		mirrorCamera.setRotation(0, heading + Math.PI, 0);
		Render.beginTarget(mirror);
		stage.drawWith(mirrorCamera);
		Render.endTarget();

		stage.draw();
		speedEffect((car : Vehicle).getSpeed());
		hud();
	}

	static final vignette = Shader.create("shaders/speed_vignette.wgfshader");
	static var effect:Material = 0;

	static function speedEffect(speed:Float) {
		if (effect.isNone()) {
			effect = Material.createCustom(vignette);
			Render.addEffect(effect);
		}
		effect.setFloat("strength", Math.min(speed / 60, 1)); // tightens with speed
	}

	static function hud() {
		final white = Color.get(ColorStock.WHITE);
		Draw.text(0, '${Math.round((car : Vehicle).getSpeed() * 3.6)} km/h', 40, 640, 40, white);
		Draw.text(0, 'lap ${Math.min(Laps.lap, Laps.LAPS)}/${Laps.LAPS}', 40, 30, 24, white);
		Draw.text(0, 'time ${Laps.format(Laps.current)}   best ${Laps.format(Laps.best)}', 40, 60, 20, white);
		Draw.texture(mirror, 480, 16, 320, 120, white);
		if (state == State.COUNTDOWN)
			Draw.text(0, '${Math.ceil(countdown)}', 620, 300, 96, white);
		if (state == State.RESULTS && Ui.begin()) {
			Ui.beginPanel("results");
			Ui.label('best lap ${Laps.format(Laps.best)}', 32);
			if (Ui.button("again", "Race again"))
				restart();
			Ui.endPanel();
			Ui.end();
		}
	}

	static function loadingScreen() {
		if (!Ui.begin())
			return;
		Ui.beginPanel("loading");
		Ui.label("Racer", 64);
		Ui.progress(loading.getProgress()); // a bar: a widget libwgf's UI doesn't have yet
		Ui.endPanel();
		Ui.end();
	}

	static function restart() {
		Laps.reset();
		car.setPosition(0, 0.5, 0);
		car.setRotation(0, 0, 0);
		(car : Vehicle).reset(); // wheels, engine, and velocities to rest
		car.snap();
		countdown = 3;
		state = State.COUNTDOWN;
	}
}

enum abstract State(Int) to Int {
	var LOADING = 0;
	var COUNTDOWN = 1;
	var RACING = 2;
	var RESULTS = 3;
}
