import wgf.*;

/**
	A level described as data: a scene file, loaded, instantiated, and added to at run time.

	The level is `assets/level.scene`: a floor, two ledges, two clouds, and a dropper, each an
	actor with its components set in the file. The file also holds two prefabs, a cloud and a
	crate. Change the level by editing the file: this code only does these things.

	- It loads the scene, a resource, and waits for it to be READY.
	- It instantiates the scene under a 2D stage.
	- It finds the dropper by name on the stage, and the dropper's label by its path.
	- It spawns the crate prefab under the dropper, on a key press and every second.
	- It registers `Patrol`, the behavior the file names on the dropper.

	The controls:
	- Space drops a crate.
	- Escape quits, where quitting means anything (not in a browser).

	Run it with `wgf serve` or `wgf run` from this folder.
**/
class Main {
	static inline var DROP_EVERY = 1.0; // seconds between the crates that drop on their own

	static var stage:Stage2d;
	static var scene:Scene;
	static var loaded = false;
	static var crate:Prefab = 0; // found once the scene is READY, and kept
	static var dropper:Actor = 0;
	static var label:Actor = 0;
	static var spawned = 0;
	static var clock = 0.0;

	static function main() {
		Window.setTitle("libwgf scene (Haxe)");
		// The design size, fitted to any window or page. Window.setSize would fix the canvas at that
		// size on the web, unscaled, so it is left to the presentation.
		Presentation.set(PresentationMode.FIT, 800, 450);
		Window.setMsaa(true);
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Render.setClearColor(Color.make(150, 196, 230, 255));
		stage = Stage2d.create();
		Behavior.register("Patrol", actor -> new Patrol(actor));
		scene = Scene.create("level.scene"); // PENDING now: it loads while the frames go on
		publish();
	}

	static function frame() {
		if (!loaded && Resource.getStatus(scene) == ResourceStatus.READY)
			start();
		if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE))
			App.quit();
		if (loaded) {
			clock += Loop.getFrameDelta();
			if (Keyboard.isPressed(KeyboardKey.SPACE) || clock >= DROP_EVERY)
				drop();
		}
		stage.draw();
		hud();
		publish();
	}

	/** The scene is READY: its actors made, and what the program needs from it found once. **/
	static function start() {
		loaded = true;
		scene.instantiate(stage);
		dropper = stage.find("dropper"); // by name, anywhere on the stage
		label = dropper.find("label"); // by path, from the dropper
		crate = scene.prefab("crate");
	}

	static function drop() {
		clock = 0;
		final at = dropper.getPosition();
		crate.spawnAt(stage, at.x, at.y + 24, 0, 0);
		spawned++;
		(label : Text).setString('dropped $spawned');
	}

	static function hud() {
		final ink = Color.make(30, 34, 44, 255);
		Draw.text(0, "level.scene: a level as data", 16, 12, 20, ink);
		if (Resource.getStatus(scene) == ResourceStatus.FAILED) {
			Draw.text(0, "level.scene failed to load (see the log)", 16, 40, 16, Color.get(ColorStock.MAROON));
			return;
		}
		if (!loaded) {
			Draw.text(0, "loading...", 16, 40, 16, ink);
			return;
		}
		Draw.text(0, 'the file: ${scene.getActorCount()} actors, ${scene.getPrefabCount()} prefabs', 16, 40, 16, ink);
		Draw.text(0, 'simulated actors now: ${Actor.getCount()}', 16, 60, 16, ink);
		Draw.text(0, "space: drop a crate (one drops each second)", 16, 80, 16, ink);
	}

	// Probes, for autopilot/smoke.autopilot.
	static function publish() {
		Probe.setValue("scene.loaded", loaded ? 1 : 0);
		Probe.setValue("scene.actors", Actor.getCount());
		Probe.setValue("scene.spawned", spawned);
	}
}

/**
	The behavior `level.scene` names on the dropper: it turns its actor back at the parameters
	`left` and `right`. Its speed is the actor's motion, also set in the file.
**/
class Patrol extends Behavior {
	var left = 0.0;
	var right = 0.0;
	final at = new Vec3(0, 0, 0);
	final velocity = new Vec3(0, 0, 0);

	override function onCreate() {
		left = getParamNumber("left");
		right = getParamNumber("right");
	}

	override function onTick(dt:Float) {
		actor.getPosition(at);
		(actor : Motion).getVelocity(velocity);
		if ((at.x > right && velocity.x > 0) || (at.x < left && velocity.x < 0))
			(actor : Motion).setVelocity(-velocity.x, velocity.y, 0);
	}
}
