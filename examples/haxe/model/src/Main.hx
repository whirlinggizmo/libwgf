import wgf.*;

/**
	A skinned, animated 3D model, in Haxe: Quaternius's robot, its clips crossfaded one into the next.

	This is the C example `gfx-skinning` made small, and the JS example `model`, written the Haxe way.
	It shows a glTF file loaded without waiting, and played once it is there:
	- The model is made at once, and draws nothing while its file loads. The text shows the status.
	- Once the file is READY, its clips are found by name, and the idle clip plays.
	- Every few seconds, or on N, the next clip crossfades in.
	- The sun casts shadows on a ground made in code, with a material of its own.
	- The camera circles slowly.

	Controls:
	- N: the next clip, crossfaded.
	- Space: pause or resume the clip.
	- Escape: quit, where quitting means anything (not in a browser).

	Run it with `wgf serve` or `wgf run` from this folder.
**/
class Main {
	static inline final PATH = "models/robot.glb";
	static inline final FADE = 0.3; // seconds of crossfade into each clip
	static inline final STEP = 4.0; // seconds of each clip before the next

	static var stage:Stage3d;
	static var camera:Camera3d;
	static var robot:Model;
	static var started = false;
	static var paused = false;
	static var clip = -1;
	static var since = 0.0;
	static var angle = 0.0;

	static function main() {
		Window.setTitle("libwgf model (Haxe)");
		// The design size, fitted to any window or page. A desktop window opens at this size.
		Presentation.set(PresentationMode.FIT, 1000, 600);
		Window.setMsaa(true);
		Window.setResizable(true);
		Debug.showFps(0, 12, 10, 16, Color.make(0, 255, 0, 255));
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Render.setClearColor(Color.make(24, 26, 34, 255));

		camera = Camera3d.create();
		stage = Stage3d.create();
		stage.setCamera(camera);
		stage.setAmbient(Color.get(ColorStock.WHITE), 0.35);

		final sun = Light.create(LightType.DIRECTIONAL);
		sun.lookAt(-0.4, -1.0, -0.6, 0, 1, 0); // from the origin, shining that way
		sun.setIntensity(2.5);
		sun.setShadowCasting(true);
		sun.setShadowDistance(20);
		sun.setParent(stage);

		// The ground: a generated mesh, with a material of its own.
		final plane = Mesh.createPlane(40, 40, 0);
		final ground = Model.create(plane);
		Resource.release(plane); // the model holds its own reference
		final material = Material.create(MaterialShading.PBR);
		material.setVec4("base_color", 0.18, 0.2, 0.24, 1.0);
		material.setFloat("roughness", 0.9);
		ground.setMaterial(-1, material); // -1: every slot
		Resource.release(material);
		ground.setParent(stage);

		// The robot: its file starts loading now, and the model draws once it is READY.
		final mesh = Mesh.create(PATH);
		robot = Model.create(mesh);
		Resource.release(mesh);
		robot.setParent(stage);
	}

	/** Once the file is READY: its clips are there, found by name. **/
	static function start() {
		clip = robot.findAnimation("Robot_Idle");
		if (clip < 0)
			clip = 0;
		robot.playAnimation(clip, 0); // cut straight to it
		started = true;
	}

	static function nextClip() {
		clip = (clip + 1) % robot.getAnimationCount();
		robot.playAnimation(clip, FADE); // plays from any state, so a paused robot plays again
		paused = false;
		since = 0;
	}

	static function frame() {
		final dt = Loop.getFrameDelta();
		if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE))
			App.quit();

		final status = Resource.getStatus(robot.getMesh());
		if (!started && status == ResourceStatus.READY)
			start();
		if (started) {
			if (Keyboard.isPressed(KeyboardKey.SPACE)) {
				paused = !paused;
				if (paused)
					robot.pauseAnimation();
				else
					robot.resumeAnimation();
			}
			if (!paused)
				since += dt;
			if (Keyboard.isPressed(KeyboardKey.N) || since >= STEP)
				nextClip();
		}

		angle += dt * 0.15;
		camera.setPosition(9 * Math.sin(angle), 4, 9 * Math.cos(angle));
		camera.lookAt(0, 1.8, 0, 0, 1, 0);

		stage.draw();

		final white = Color.get(ColorStock.RAYWHITE);
		final gray = Color.get(ColorStock.LIGHTGRAY);
		if (started) {
			final name = robot.getAnimationName(clip);
			final time = tenths(robot.getAnimationTime());
			final duration = tenths(robot.getAnimationDuration(clip));
			Draw.text(0, '$name   $time / $duration s', 12, 36, 20, white);
			final fading = robot.getAnimationWeight() < 1 ? ", crossfading" : "";
			final state = paused ? ", paused" : "";
			Draw.text(0, 'clip ${clip + 1} of ${robot.getAnimationCount()}$fading$state', 12, 62, 16, gray);
		} else {
			Draw.text(0, 'loading $PATH: ${statusName(status)}', 12, 36, 20, white);
		}
		Draw.text(0, App.canQuit() ? "N: next clip   SPACE: pause   ESC: quit" : "N: next clip   SPACE: pause",
			12, 86, 16, gray);

		// Probes publish numbers an autopilot run can check (autopilot/smoke.autopilot).
		Probe.setValue("model.loaded", started ? 1 : 0);
		Probe.setValue("model.clips", robot.getAnimationCount());
		Probe.setValue("model.clip", clip);
		Probe.setValue("model.paused", paused ? 1 : 0);
	}

	/** `seconds` with one decimal, as "1.5". **/
	static function tenths(seconds:Float):String {
		final t = Math.round(seconds * 10);
		return '${Std.int(t / 10)}.${t % 10}';
	}

	static function statusName(status:ResourceStatus):String {
		return switch status {
			case PENDING: "pending";
			case READY: "ready";
			case FAILED: "failed";
			default: "none";
		}
	}
}
