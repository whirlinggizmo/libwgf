import wgf.*;

/**
	Sprites and input actions, in Haxe: textured sprites on a 2D stage, and one the player moves.

	It shows:
	- sprites made from two textures, each sized and placed, one turned, one scaled, one tinted
	- draw order: a 2D stage draws its children in order, so a later one is drawn on top
	- input actions (Player.hx): "move_x", "move_y", and "fire", each bound to keys and to a pad
	- a help line made from the actions' own bindings, as a remap screen would list them

	Controls:
	- Arrow keys, WASD, the d-pad, or the left stick: move the orange logo.
	- Space or the pad's south button: send it behind the other sprites, or back in front.
	- Escape: quit, where quitting means anything (not in a browser).

	Its probes, for autopilot/smoke.autopilot:
	- sprites.loaded: 1 once both textures are ready.
	- sprites.x, sprites.y: the player's position, in pixels.
	- sprites.front: 1 while the player is drawn on top.

	This is the JS example `sprites`, written the Haxe way. Run it with `wgf serve` or
	`wgf run` from this folder.
**/
class Main {
	public static inline var WIDTH = 800;
	public static inline var HEIGHT = 600;

	static var stage:Stage2d;
	static var black:Texture;
	static var white:Texture;
	static var spinner:Sprite;
	static var pulse:Sprite;
	static var rainbow:Sprite;
	static var player:Player;
	static var help:Array<String> = [];
	static var time = 0.0;

	static function main() {
		Window.setTitle("libwgf sprites (Haxe)");
		Window.setFlags(WindowFlag.MSAA);
		// The design size, fitted to any window or page. Natively, the window opens at it.
		Presentation.set(PresentationMode.FIT, WIDTH, HEIGHT);
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Render.setClearColor(Color.make(232, 234, 240, 255));
		Player.bindActions();
		help = [for (i in 0...Action.getCount()) describe(Action.getName(i))];

		// Textures load in the background. A sprite draws nothing until its texture is ready.
		black = Texture.create("wg-logo-bw-alpha.png");
		white = Texture.create("wg-logo-white-alpha.png");
		stage = Stage2d.create();
		// Added in this order, so drawn in it: the player, last, starts on top of the two it overlaps.
		spinner = addSprite(black, 250, 330, 220);
		pulse = addSprite(black, 550, 330, 220);
		rainbow = addSprite(white, 400, 190, 150);
		player = new Player(addSprite(white, 0, 0, 110), WIDTH / 2, 360);
	}

	/** A sprite of `texture`, `size` pixels square, centered on (x, y) on the stage. **/
	static function addSprite(texture:Texture, x:Float, y:Float, size:Float):Sprite {
		final sprite = Sprite.create(texture); // it holds its own reference to the texture
		sprite.setSize(size, size);
		sprite.setPosition(x, y, 0);
		sprite.setParent(stage);
		return sprite;
	}

	/** An action and its bindings as text, such as "fire: Space, Pad south". **/
	static function describe(action:String):String {
		final names = [for (i in 0...Action.getBindingCount(action)) Action.getBindingText(action, i)];
		return '$action: ${names.join(", ")}';
	}

	static function frame() {
		if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE))
			App.quit();
		final dt = Loop.getFrameDelta();
		time += dt;

		spinner.setRotation(0, 0, time); // radians, about its center (the default pivot)
		final scale = 1 + 0.2 * Math.sin(time * 3);
		pulse.setScale(scale, scale, 1);
		rainbow.setTint(Color.makeHsv(time * 90, 0.8, 1, 255)); // round the color wheel every 4 s
		player.update(dt);

		stage.draw();
		final gray = Color.get(ColorStock.DARKGRAY);
		Draw.text(0, "Move the orange logo. Fire sends it behind the others, or back in front.", 20, 16, 16, gray);
		for (i in 0...help.length)
			Draw.text(0, help[i], 20, 40 + i * 20, 16, Color.get(ColorStock.GRAY));
		Draw.text(0, player.front ? "player: in front" : "player: behind", 20, 104, 16, gray);

		final loaded = Resource.getStatus(black) == ResourceStatus.READY
			&& Resource.getStatus(white) == ResourceStatus.READY;
		Probe.setValue("sprites.loaded", loaded ? 1 : 0);
		Probe.setValue("sprites.x", player.x);
		Probe.setValue("sprites.y", player.y);
		Probe.setValue("sprites.front", player.front ? 1 : 0);
	}
}
