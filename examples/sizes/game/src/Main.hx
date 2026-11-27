import wgf.*;

/**
	The game rung: helix-raylib's size ladder's `10-game` (../helix's engines/raylib/tests/sizes/10-game), written on libwgf
	in Haxe, feature for feature, as a libwgf game writes it, so the two pages' sizes can be set side by side
	(docs/COMPARISON.md, "Size"). What a real game uses, a file format of each kind:
	- a skinned glTF model, lit, its idle and run crossfaded by how it moves;
	- PNG textures and a sprite sheet's clip, a TTF font and the built-in one;
	- 2D and 3D, each with a camera;
	- a shader of its own, as a screen effect, over the world drawn into a render target;
	- sound effects (WAV) and music (Ogg, streamed);
	- the keyboard, the mouse, a pad, and touch; a ray from the pointer to the ground;
	- a save kept and read back; collisions; fullscreen and the window's size.
	Measured by tools/measure_sizes.py as a game's web export is (`game:game-rung`); run only by its smoke autopilot.
**/
class Main {
	static var world:Stage3d;
	static var camera:Camera3d;
	static var hero:Model;
	static var overlay:Stage2d;
	static var spark:Sprite;
	static var gem:Sheet;
	static var tile:Sprite;
	static var scoreText:Text;
	static var target:Texture;
	static var effect:Material;
	static var jump:Sound;
	static var coin:Sound;
	static var load:FsTask;
	static var idle = -1;
	static var run = -1;
	static var running = false;
	static var clipAdded = false;
	static var px = 0.0;
	static var pz = 0.0;
	static var coinX = 3.0;
	static var coinZ = 0.0;
	static var score = 0;
	static var best = 0;
	static var time = 0.0;
	static final hit:Array<Float> = [for (_ in 0...6) 0.0];

	static function main() {
		Window.setTitle("libwgf game rung");
		Window.setSize(800, 450);
		Presentation.set(PresentationMode.FIT, 800, 450); // helix's window: in a page, fitted with bars
		Debug.showFps(0, 700, 20, 16, Color.get(ColorStock.LIME));
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Render.setClearColor(Color.get(ColorStock.BLACK));

		// 3D: a lit stage, the hero on it
		camera = Camera3d.create();
		camera.setPosition(6, 4, 6);
		camera.lookAt(0, 1.5, 0, 0, 1, 0);
		world = Stage3d.create();
		world.setCamera(camera);
		camera.setParent(world);
		world.setAmbient(Color.get(ColorStock.WHITE), 0.3);
		final sun = Light.create(LightType.POINT);
		sun.setPosition(2, 4, 2);
		sun.setIntensity(20);
		sun.setParent(world);
		final mesh = Mesh.create("models/woman_casual/woman_casual.glb");
		hero = Model.create(mesh);
		Resource.release(mesh);
		hero.setParent(world);

		// the world drawn into a target, then to the screen through a shader of its own
		target = Texture.createTarget(400, 225);
		Render.setTargetClearColor(target, Color.get(ColorStock.SKYBLUE));
		effect = Material.createCustom(Shader.create("shaders/scanlines.wgfshader"));
		effect.setFloat("lines", 225);
		effect.setFloat("darkness", 0.25);
		effect.setFloat("offset", 0.5);
		Render.addEffect(effect);

		// 2D: tiles and a sprite from a sheet under a 2D camera, then the text
		overlay = Stage2d.create();
		final camera2d = Camera2d.create();
		camera2d.setZoom(1);
		camera2d.setParent(overlay);
		overlay.setCamera(camera2d);
		tile = Sprite.create(Texture.create("textures/hex_tiles.png"));
		tile.setSource(0, 0, 32, 32);
		tile.setPivot(0, 0);
		tile.setPosition(-400, 193, 0);
		tile.setParent(overlay);
		gem = Sheet.create("sheets/gem.json");
		spark = Sprite.create(gem.getTexture());
		spark.setSheet(gem);
		spark.setSize(32, 32);
		spark.setParent(overlay);
		scoreText = Text.create(Font.create("fonts/Komika/KOMIKAH_.ttf"));
		scoreText.setFontSize(32);
		scoreText.setColor(Color.get(ColorStock.WHITE));
		scoreText.setPosition(-380, -205, 0);
		scoreText.setParent(overlay);

		// sound: two effects and music streamed
		jump = Sound.create("sounds/jump.wav");
		coin = Sound.create("sounds/coin.wav");
		final music = Voice.create(Sound.createStreamed("sounds/click_004.ogg"));
		music.setLoop(true);
		music.play();

		load = Fs.read("user:save.txt");
	}

	static function frame() {
		final dt = Loop.getFrameDelta();
		time += dt;
		if (load != 0 && load.getStatus() != FsTaskStatus.PENDING) {
			if (load.getStatus() == FsTaskStatus.DONE)
				best = Std.parseInt(load.getData().toString()) ?? 0;
			load.destroy();
			load = 0;
		}
		if (Keyboard.isPressed(KeyboardKey.F))
			Window.requestFullscreen(!Window.isFullscreen());
		if (Keyboard.isPressed(KeyboardKey.R))
			Window.setSize(1280, 720);

		// the keyboard, a pad's stick, and the mouse or a finger move the hero
		var mx = 0.0, mz = 0.0;
		if (Keyboard.isDown(KeyboardKey.RIGHT))
			mx += 1;
		if (Keyboard.isDown(KeyboardKey.LEFT))
			mx -= 1;
		if (Gamepad.isConnected(0)) {
			mx += Gamepad.getAxis(0, GamepadAxis.LEFT_X);
			mz += Gamepad.getAxis(0, GamepadAxis.LEFT_Y);
		}
		final pointer = Touch.getCount() > 0 ? Touch.getPosition(Touch.getId(0)) : Mouse.getPosition();
		if (Mouse.isDown(MouseButton.LEFT) || Touch.getCount() > 0) {
			final from = camera.screenToOrigin(pointer.x, pointer.y);
			final way = camera.screenToDirection(pointer.x, pointer.y);
			if (Collide3d.rayBox(from.x, from.y, from.z, way.x, way.y, way.z, -10, -0.1, -10, 10, 0, 10, hit)) {
				final dx = hit[0] - px, dz = hit[2] - pz, length = Math.sqrt(dx * dx + dz * dz);
				if (length > 0.01) {
					mx = dx / length;
					mz = dz / length;
				}
			}
		}
		if (Keyboard.isPressed(KeyboardKey.SPACE) || Gamepad.isPressed(0, GamepadButton.SOUTH))
			jump.play(1, 1, 0);
		px += mx * 4 * dt;
		pz += mz * 4 * dt;
		hero.setPosition(px, 0, pz);
		if (Collide3d.sphereSphere(px, 0.5, pz, 0.5, coinX, 0.5, coinZ, 0.5, hit)) {
			score++;
			coinX = Random.getInt(-5, 5);
			coinZ = Random.getInt(-5, 5);
			coin.play(1, 1, 0);
			if (score > best) {
				best = score;
				Fs.write("user:save.txt", haxe.io.Bytes.ofString(Std.string(best))).destroy();
			}
		}

		// the hero's idle and run, crossfaded as it starts and stops
		if (idle < 0 && hero.getAnimationCount() > 0) {
			idle = hero.findAnimation("idle-loop");
			run = hero.findAnimation("run-loop");
			hero.playAnimation(idle, 0);
		}
		final moving = mx * mx + mz * mz > 0.01;
		if (idle >= 0 && moving != running) {
			running = moving;
			hero.playAnimation(running ? run : idle, 0.2);
		}
		if (!clipAdded && gem.getFrameCount() > 0) {
			clipAdded = spark.playClip("spin", true) || gem.addClip("spin", [for (i in 0...gem.getFrameCount()) i], 0.8,
				SheetDirection.FORWARD) && spark.playClip("spin", true);
		}

		// the world into the target; the target to the screen, through the effect
		effect.setFloat("flicker", 0.05 + 0.05 * Math.sin(time * 6)); // driven by time, as helix's wave shader is
		if (Render.beginTarget(target)) {
			world.draw();
			Draw.begin3d(camera);
			Draw.grid(20, 1, Color.get(ColorStock.GRAY));
			Draw.sphere(coinX, 0.5, coinZ, 0.5, Color.get(ColorStock.GOLD));
			Draw.end3d();
			Render.endTarget();
		}
		Draw.texture(target, 0, 0, 800, 450, Color.get(ColorStock.WHITE));

		// 2D over it: the sprite at the pointer, outlined where it meets the tile
		spark.setPosition(pointer.x - 400, pointer.y - 225, 0);
		if (Collide2d.rectRect(pointer.x - 416, pointer.y - 241, 32, 32, -400, 193, 32, 32, hit))
			Draw.rectangleLines(pointer.x - 16, pointer.y - 16, 32, 32, 2, Color.get(ColorStock.RED));
		scoreText.setString('Score $score');
		overlay.draw();
		Draw.text(0, 'Best $best', 20, 60, 20, Color.get(ColorStock.LIGHTGRAY));
	}
}
