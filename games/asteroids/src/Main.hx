import wgf.*;

/**
	Asteroids, on libwgf: the first milestone's game.

	The world is a scene file (assets/scenes/asteroids.scene): the ship, three sizes of
	rock, the bullet, and the explosion, each a prefab with its components. The ecs moves
	everything, wraps it around the screen, ages the bullets and explosions out, and
	finds the overlaps; the behaviors only set intent -- the ship's turn, thrust, and fire
	from the input, a rock's split when a bullet meets it -- and keep the score. The title
	and game-over screens are libwgf's UI; the HUD is drawn text. Sounds are generated
	(tools/gen_sounds.py) and committed.

	Its probes, for its autopilot playthrough (autopilot/playthrough.autopilot):
	asteroids.state (0 title, 1 playing, 2 game over), asteroids.score, asteroids.lives,
	asteroids.wave, and the ecs's own (ecs.behavior.Rock: the rocks left).
**/
class Main {
	public static inline var WIDTH = 960;
	public static inline var HEIGHT = 720;

	public static var world:Canvas;
	public static var scene:Scene;
	public static var sounds:Sounds;
	public static var state = State.TITLE;
	public static var score = 0;
	public static var lives = 3;
	public static var wave = 0;
	public static var best = 0;

	static var ready = false;
	static var waveDelay = 0.0;
	static var respawnDelay = 0.0;
	static var ship:Entity = 0;

	static function main() {
		Window.setTitle("Asteroids");
		Window.setSize(WIDTH, HEIGHT);
		Runtime.run(init, tick, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Render.setClearColor(Color.make(6, 8, 14, 255));
		world = Canvas.create();
		scene = Scene.create("scenes/asteroids.scene");
		sounds = new Sounds();
		Behavior.register("Ship", e -> new Ship(e));
		Behavior.register("Rock", e -> new Rock(e));
		Behavior.register("Bullet", e -> new Bullet(e));
		Ui.setStyleColor(UiColor.PANEL, Color.make(14, 18, 30, 230));
		Ui.setStyleColor(UiColor.BUTTON, Color.make(32, 40, 62, 255));
		Ui.setStyleColor(UiColor.BUTTON_HOVERED, Color.make(48, 60, 92, 255));
		Ui.setStyleColor(UiColor.FOCUS, Color.make(120, 220, 255, 255));
		Ui.setStyleValue(UiValue.CORNER_RADIUS, 6);
		publish();
	}

	// ---- the game's states -------------------------------------------------------------

	/** A new game: the rocks of the title cleared, the ship made, the first wave. **/
	public static function start() {
		Ecs.clear();
		score = 0;
		lives = 3;
		wave = 0;
		waveDelay = 0; // a delay left from the last game would cut this one's first wait short
		respawnDelay = 0;
		state = State.PLAYING;
		spawnShip();
		nextWave();
		sounds.play(sounds.start);
	}

	static function spawnShip() {
		ship = scene.spawn("ship", world);
		ship.setPosition(WIDTH / 2, HEIGHT / 2, 0);
		ship.snap();
	}

	/** The ship was hit: a life lost, and the next ship in a while, or the game over. **/
	public static function shipLost() {
		ship = 0;
		lives--;
		if (lives > 0) {
			respawnDelay = 2.0;
		} else {
			state = State.GAME_OVER;
			best = score > best ? score : best;
			Ui.setFocus("again");
			sounds.play(sounds.gameOver);
		}
	}

	static function nextWave() {
		wave++;
		final count = 3 + wave;
		for (_ in 0...count) {
			// at an edge, away from the ship in the middle
			final side = Random.getInt(0, 3);
			final x = side == 0 ? 0 : side == 1 ? WIDTH : Random.getRange(0, WIDTH);
			final y = side == 2 ? 0 : side == 3 ? HEIGHT : Random.getRange(0, HEIGHT);
			Rock.spawn(RockSize.LARGE, x, y);
		}
	}

	/** The title's drifting rocks, behind its menu. **/
	static function titleField() {
		Ecs.clear();
		for (_ in 0...6)
			Rock.spawn(RockSize.LARGE, Random.getRange(0, WIDTH), Random.getRange(0, HEIGHT));
	}

	public static function addScore(points:Int) {
		final before = Std.int(score / 10000);
		score += points;
		if (Std.int(score / 10000) > before) { // a ship every 10,000 points
			lives++;
			sounds.play(sounds.extraLife);
		}
	}

	// ---- the loop ------------------------------------------------------------------------

	static function tick() {
		final dt = Loop.getTickDelta();
		if (state == State.PLAYING) {
			if (ship.isNone() && respawnDelay > 0) {
				respawnDelay -= dt;
				if (respawnDelay <= 0)
					spawnShip();
			}
			if (Ecs.countBehavior("Rock") == 0) {
				waveDelay += dt;
				if (waveDelay > 1.5) {
					waveDelay = 0;
					nextWave();
				}
			}
		}
		publish();
	}

	static function publish() {
		Probe.setValue("asteroids.state", state);
		Probe.setValue("asteroids.score", score);
		Probe.setValue("asteroids.lives", lives);
		Probe.setValue("asteroids.wave", wave);
	}

	static function frame() {
		if (!ready && Resource.getStatus(scene) == ResourceStatus.READY) {
			ready = true;
			titleField();
			Ui.setFocus("play");
		}
		world.draw();
		switch state {
			case State.TITLE:
				titleScreen();
			case State.PLAYING:
				hud();
			case State.GAME_OVER:
				hud();
				gameOverScreen();
		}
		if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE))
			App.quit();
	}

	static function hud() {
		final white = Color.get(ColorStock.WHITE);
		Draw.text(0, '$score', 24, 16, 32, white);
		for (i in 0...lives)
			Ship.drawIcon(32 + i * 26, 72);
		Draw.text(0, 'wave $wave', WIDTH - 120, 20, 18, Color.get(ColorStock.LIGHTGRAY));
	}

	static function titleScreen() {
		if (!Ui.begin())
			return;
		Ui.beginPanel("title");
		Ui.setAlign(UiAlign.CENTER, UiAlign.START);
		Ui.label("ASTEROIDS", 64);
		Ui.label("turn: left and right   thrust: up   fire: space", 16);
		Ui.spacer(16);
		if (Ui.button("play", "Play") && ready)
			start();
		if (App.canQuit() && Ui.button("quit", "Quit"))
			App.quit();
		if (best > 0)
			Ui.label('best: $best', 18);
		Ui.endPanel();
		Ui.end();
	}

	static function gameOverScreen() {
		if (!Ui.begin())
			return;
		Ui.beginPanel("over");
		Ui.setAlign(UiAlign.CENTER, UiAlign.START);
		Ui.label("GAME OVER", 56);
		Ui.label('score: $score   best: $best', 20);
		Ui.spacer(12);
		if (Ui.button("again", "Play again"))
			start();
		if (Ui.button("title", "Title")) {
			state = State.TITLE;
			titleField();
			Ui.setFocus("play");
		}
		Ui.endPanel();
		Ui.end();
	}
}

enum abstract State(Int) to Int {
	var TITLE = 0;
	var PLAYING = 1;
	var GAME_OVER = 2;
}
