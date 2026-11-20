import wgf.*;

/**
	Wardens, helix's engine trial (its spec.md, unchanged), on libwgf in Haxe: a warden in a
	walled arena holding out against waves of enemies, wearing the gear she finds.

	The screens are this class's states, drawn in immediate mode from the UI art (Menu); the
	play is Game's, run at a fixed 60 ticks a second. The arena, the camera and the lights are
	set up here; the HUD and the warden's health bar are drawn here each frame.

	Run options (`--name=value` natively, `name=value` in the page's address): `enemies=N`
	(N alive from the start, the cap held there), `seed=N` (the spawns' seed), `frames=N` (quit
	after N frames: a headless run's end), `play` (start playing at once, past the title).

	Probes, for its autopilot: wardens.ready, wardens.state (State's numbers), wardens.alive,
	wardens.kills, wardens.level, wardens.hp, wardens.time, wardens.x, wardens.z, wardens.gun.
**/
class Main {
	public static inline var WIDTH = 1280;
	public static inline var HEIGHT = 720;
	static inline var CAMERA_DISTANCE = 14.0;
	static inline var CAMERA_PITCH = 0.95993; // 55 degrees down
	static inline var BAR_HEIGHT = 2.25; // the health bar's height over her feet

	public static var stage:Stage3d;
	public static var camera:Camera3d;
	static var game:Game;
	static var state = State.LOADING;
	static var cards:Array<Card> = [];
	static var touched = false;
	static var frames = 0;
	static var maxFrames = 0;
	static var autoplay = false;
	static var music:Voice = 0;
	static var focusX = 0.0;
	static var focusZ = 0.0;
	static var newBest = false;
	static var stickRegion = new Vec4(-1, -1, -1, -1);
	static final visible = new Vec4();
	static final screen = new Vec3();
	static final stickAt = new Vec2();
	static final knobAt = new Vec2();

	static function main() {
		Identity.setCompany("WhirlingGizmo"); // where the save is kept: <data>/WhirlingGizmo/Wardens
		Identity.setProduct("Wardens");
		Window.setTitle("Wardens");
		Window.setResizable(true);
		Window.setMsaa(true);
		// the design at least, the window filled: the HUD keeps to the window's edges. The window
		// opens at the design's size natively; on the web the canvas fills the page (no Window.setSize)
		Presentation.set(PresentationMode.EXPAND, WIDTH, HEIGHT);
		Runtime.run(init, tick, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Loop.setTickRate(60);
		maxFrames = App.getOptionInt("frames", 0);
		autoplay = App.hasOption("play");
		final enemies = Std.int(Math.max(0, Math.min(200, App.getOptionInt("enemies", 0))));
		final seed = App.hasOption("seed") ? App.getOptionInt("seed", 1) : Random.getInt(1, 1000000);
		Save.load();
		Assets.load();
		Render.setClearColor(Color.make(22, 20, 26, 255));

		stage = Stage3d.create();
		camera = Camera3d.create();
		camera.setFov(0.87); // 50 degrees
		camera.setClip(0.3, 120);
		stage.setCamera(camera);
		stage.setTonemap(Stage3dTonemap.NEUTRAL, 0);
		// one sun, and a soft fill from above
		final sun = Light.create(LightType.DIRECTIONAL);
		sun.setColor(Color.make(255, 238, 214, 255));
		sun.setIntensity(2.6);
		sun.lookAt(-0.45, -1.0, -0.35, 0, 1, 0);
		sun.setShadowCasting(true);
		sun.setShadowDistance(32);
		sun.setShadowMapSize(2048);
		sun.setShadowStrength(0.7);
		sun.setParent(stage);
		final fill = Light.create(LightType.DIRECTIONAL);
		fill.setColor(Color.make(170, 190, 255, 255));
		fill.setIntensity(0.7);
		fill.lookAt(0.05, -1.0, 0.02, 0, 0, -1);
		fill.setParent(stage);
		stage.setAmbient(Color.make(150, 160, 190, 255), 0.35);

		buildArena();
		game = new Game(stage, enemies, seed);
		game.reset();
		bindControls();
		Probe.setValue("wardens.ready", 0);
	}

	/** The ground (40 by 40, a grid drawn on it each frame), low walls of the dungeon kit's wall around it, pillars at its corners, props outside. **/
	static function buildArena() {
		final groundMesh = Mesh.createPlane(160, 160, 0);
		final ground = Model.create(groundMesh);
		Resource.release(groundMesh);
		final stone = Material.create(MaterialShading.PBR);
		stone.setVec4("base_color", 0.16, 0.15, 0.16, 1);
		stone.setFloat("metallic", 0);
		stone.setFloat("roughness", 0.92);
		ground.setMaterial(-1, stone);
		Resource.release(stone);
		ground.setShadowCasting(false);
		ground.setParent(stage);
		final inner = Mesh.createPlane(Game.HALF * 2, Game.HALF * 2, 0);
		final floor = Model.create(inner);
		Resource.release(inner);
		final slab = Material.create(MaterialShading.PBR);
		slab.setVec4("base_color", 0.30, 0.28, 0.25, 1);
		slab.setFloat("metallic", 0);
		slab.setFloat("roughness", 0.85);
		floor.setMaterial(-1, slab);
		Resource.release(slab);
		floor.setShadowCasting(false);
		floor.setPosition(0, 0.005, 0);
		floor.setParent(stage);

		// the kit's wall is 4 long (x), 1 thick, 4 high: scaled to 1.2 m high and half thick, ten a side, just outside the edge
		final wallY = 0.3, wallT = 0.5;
		for (side in 0...4) {
			for (i in 0...10) {
				final along = -Game.HALF + 2 + 4 * i;
				final out = Game.HALF + wallT / 2;
				final w = Model.create(Assets.wall);
				switch side {
					case 0:
						w.setTransform(along, 0, -out, 0, 0, 0, 1, wallY, wallT);
					case 1:
						w.setTransform(along, 0, out, 0, Math.PI, 0, 1, wallY, wallT);
					case 2:
						w.setTransform(-out, 0, along, 0, Math.PI / 2, 0, 1, wallY, wallT);
					default:
						w.setTransform(out, 0, along, 0, -Math.PI / 2, 0, 1, wallY, wallT);
				}
				w.setParent(stage);
			}
		}
		final corner = Game.HALF + 0.4;
		for (c in [[-1, -1], [1, -1], [-1, 1], [1, 1]]) {
			final p = Model.create(Assets.pillar);
			p.setTransform(c[0] * corner, 0, c[1] * corner, 0, 0, 0, 0.6, 0.45, 0.6);
			p.setParent(stage);
		}
		// props stand about outside the walls: they change nothing
		final spots = [
			[-23.0, -8.0, 0.0], [-23.5, 6.0, 1.0], [23.0, -12.0, 2.0], [23.5, 4.0, 3.0], [-10.0, -23.0, 4.0], [8.0, -23.5, 0.0],
			[-14.0, 23.0, 1.0], [12.0, 23.5, 2.0], [-23.0, 15.0, 3.0], [23.0, 16.0, 0.0], [0.0, -23.5, 2.0], [-2.0, 23.5, 3.0]
		];
		for (s in spots) {
			final m = Model.create(Assets.props[Std.int(s[2])]);
			m.setTransform(s[0], 0, s[1], 0, s[0] * 0.7 + s[1], 0, 0.6, 0.6, 0.6);
			m.setParent(stage);
		}
	}

	/** The controls, as named actions: keys, a pad's left stick and buttons, and touch. **/
	static function bindControls() {
		Action.bindKeys("move_x", KeyboardKey.A, KeyboardKey.D);
		Action.bindKeys("move_x", KeyboardKey.LEFT, KeyboardKey.RIGHT);
		Action.bindPadAxis("move_x", GamepadAxis.LEFT_X, 0, 0.15);
		Action.bindKeys("move_z", KeyboardKey.W, KeyboardKey.S); // up the screen is -z
		Action.bindKeys("move_z", KeyboardKey.UP, KeyboardKey.DOWN);
		Action.bindPadAxis("move_z", GamepadAxis.LEFT_Y, 0, 0.15); // a pad's y is down, as the screen's
		Action.bindKey("pause", KeyboardKey.ESCAPE);
		Action.bindKey("pause", KeyboardKey.P);
		Action.bindPadButton("pause", GamepadButton.START);
		Action.bindKey("switch", KeyboardKey.Q);
		Action.bindPadButton("switch", GamepadButton.LEFT_BUMPER);
		Action.bindPadButton("switch", GamepadButton.RIGHT_BUMPER);
		bindTouch();
	}

	/**
		The touch controls, bound to the visible area as it is now (again when the window
		changes): the pause button at the top right, the switch button at the bottom right,
		and the stick anywhere else, in two regions around those corners, so a finger on a
		button never moves her.
	**/
	static function bindTouch() {
		Presentation.getVisible(visible);
		if (visible.x == stickRegion.x && visible.y == stickRegion.y && visible.z == stickRegion.z && visible.w == stickRegion.w)
			return;
		stickRegion.x = visible.x;
		stickRegion.y = visible.y;
		stickRegion.z = visible.z;
		stickRegion.w = visible.w;
		for (a in ["stick_x", "stick_y", "stick2_x", "stick2_y", "touch_pause", "touch_switch"])
			Action.clear(a);
		final b = touchButtons();
		final column = b.size + 2 * b.margin;
		final right = visible.x + visible.z;
		Action.bindTouchStick("stick_x", "stick_y", visible.x, visible.y, visible.z - column, visible.w, 70);
		Action.bindTouchStick("stick2_x", "stick2_y", right - column, b.pauseY + b.size + b.margin, column,
			b.switchY - b.margin - (b.pauseY + b.size + b.margin), 70);
		Action.bindTouch("touch_pause", right - b.size - b.margin, b.pauseY, b.size, b.size);
		Action.bindTouch("touch_switch", right - b.size - b.margin, b.switchY, b.size, b.size);
	}

	static function touchButtons():{size:Float, margin:Float, pauseY:Float, switchY:Float} {
		final size = 84.0, margin = 18.0;
		return {
			size: size,
			margin: margin,
			pauseY: visible.y + 100,
			switchY: visible.y + visible.w - size - 100
		};
	}

	/** A sound effect, through the effects bus. **/
	public static function play(sound:Sound) {
		sound.play(1, 1, 0);
	}

	/** Whether a point of the ground (about a body's height up) shows on the screen now. **/
	public static function inView(x:Float, z:Float):Bool {
		Presentation.getVisible(visible);
		camera.worldToScreen(x, 0.9, z, screen);
		final margin = 60;
		return screen.z >= 0 && screen.z <= 1 && screen.x > visible.x - margin && screen.x < visible.x + visible.z + margin
			&& screen.y > visible.y - margin && screen.y < visible.y + visible.w + margin;
	}

	// ---- the states -------------------------------------------------------------------------

	static function startPlay() {
		game.reset();
		newBest = false;
		state = State.PLAY;
		game.world.setPaused(false);
		focusX = focusZ = 0;
		if (Window.canFullscreen())
			Window.requestFullscreen(true);
		if (music == 0) {
			music = Voice.create(Assets.music);
			music.setBus(Bus.getMusic());
			music.setLoop(true);
		}
		music.stop();
		music.play();
		Bus.getMusic().fade(1, 0);
	}

	static function toTitle() {
		game.reset();
		state = State.TITLE;
		game.world.setPaused(false);
		if (music != 0)
			music.stop();
	}

	static function pause() {
		state = State.PAUSED;
		game.world.setPaused(true);
		Bus.getMusic().fade(0.5, 0.25); // ducked to half while paused
	}

	static function resume() {
		state = State.PLAY;
		game.world.setPaused(false);
		Bus.getMusic().fade(1, 0.25);
		Save.write(); // the volume, as it was left
	}

	static function gameOver() {
		state = State.GAME_OVER;
		if (game.time > Save.best) {
			Save.best = game.time;
			newBest = true;
			Save.write();
		}
		Bus.getMusic().fade(0.5, 1);
	}

	// ---- the loop ---------------------------------------------------------------------------

	static function tick() {
		final dt = Loop.getTickDelta();
		game.update();
		switch state {
			case State.LOADING:
				if (Assets.progress() >= 1 && game.isReady() && Save.loaded) {
					Bus.getMaster().setVolume(Save.volume);
					Probe.setValue("wardens.ready", 1);
					if (autoplay)
						startPlay();
					else
						state = State.TITLE;
				}
			case State.TITLE:
				game.showcase(dt);
			case State.PLAY:
				final mx = Action.getAxis("move_x") + Action.getAxis("stick_x") + Action.getAxis("stick2_x");
				final mz = Action.getAxis("move_z") - Action.getAxis("stick_y") - Action.getAxis("stick2_y");
				final switching = Action.isPressed("switch") || (touched && Action.isPressed("touch_switch"));
				game.tick(dt, mx, mz, switching);
				if (game.levelUps > 0 && !game.dead) {
					cards = game.drawCards();
					state = State.LEVEL_UP;
					game.world.setPaused(true); // play stops
				} else if (game.dead && game.deadFor > 2.5) {
					gameOver();
				}
			case State.GAME_OVER:
				game.tick(dt, 0, 0, false); // the enemies stand about her
			default:
		}
		publish();
	}

	static function publish() {
		Probe.setValue("wardens.state", state);
		Probe.setValue("wardens.alive", game.alive);
		Probe.setValue("wardens.kills", game.kills);
		Probe.setValue("wardens.level", game.level);
		Probe.setValue("wardens.hp", game.hp);
		Probe.setValue("wardens.time", game.time);
		Probe.setValue("wardens.x", game.x);
		Probe.setValue("wardens.z", game.z);
		Probe.setValue("wardens.gun", game.hasGun ? 1 : 0);
	}

	static function frame() {
		final dt = Loop.getFrameDelta();
		frames++;
		if (maxFrames > 0 && frames >= maxFrames)
			App.quit();
		Save.update();
		if (Touch.getCount() > 0)
			touched = true;
		bindTouch();
		placeCamera(dt);
		stage.draw();
		if (state != State.LOADING)
			drawWorldExtras();
		Presentation.getVisible(visible);
		switch state {
			case State.LOADING:
				loadingScreen();
			case State.TITLE:
				titleScreen();
			case State.PLAY:
				hud();
				touchControls();
				final pausing = Action.isPressed("pause") || (touched && Action.isPressed("touch_pause"));
				if (pausing && !game.dead)
					pause();
			case State.PAUSED:
				hud();
				pauseScreen();
			case State.LEVEL_UP:
				hud();
				levelUpScreen();
			case State.GAME_OVER:
				hud();
				gameOverScreen();
		}
	}

	static function placeCamera(dt:Float) {
		if (state == State.TITLE || state == State.LOADING) {
			// close on the warden, standing in the middle
			camera.setPosition(0, 2.1, 5.2);
			camera.lookAt(0, 1.05, 0, 0, 1, 0);
			return;
		}
		// following her smoothly, looking down at 55 degrees from 14 m
		final k = 1 - Math.exp(-5 * dt);
		focusX += (game.x - focusX) * k;
		focusZ += (game.z - focusZ) * k;
		camera.setPosition(focusX, CAMERA_DISTANCE * Trig.sin(CAMERA_PITCH), focusZ + CAMERA_DISTANCE * Trig.cos(CAMERA_PITCH));
		camera.lookAt(focusX, 0, focusZ, 0, 1, 0);
	}

	/** The grid on the ground, and her health bar above her head, facing the camera. **/
	static function drawWorldExtras() {
		if (!Draw.begin3d(camera))
			return;
		final line = Color.make(120, 110, 95, 255);
		var i = -Game.HALF;
		while (i <= Game.HALF) {
			Draw.line3d(i, 0.012, -Game.HALF, i, 0.012, Game.HALF, line);
			Draw.line3d(-Game.HALF, 0.012, i, Game.HALF, 0.012, i, line);
			i += 2;
		}
		if (state != State.TITLE && !game.dead) {
			// turned up by the camera's pitch: square to its view
			final tilt = -CAMERA_PITCH;
			final ny = Trig.sin(CAMERA_PITCH) * 0.02, nz = Trig.cos(CAMERA_PITCH) * 0.02;
			final w = 1.2, h = 0.14;
			final share = Math.max(0, Math.min(1, game.hp / game.maxHp));
			final bx = game.x, by = BAR_HEIGHT, bz = game.z;
			Draw.rectangle3d(bx, by, bz, w + 0.08, h + 0.08, tilt, 0, 0, Color.make(20, 12, 10, 255));
			final fillW = w * share;
			Draw.rectangle3d(bx - (w - fillW) / 2, by + ny, bz + nz, fillW, h, tilt, 0, 0,
				share > 0.3 ? Color.make(70, 220, 90, 255) : Color.make(240, 70, 50, 255));
		}
		Draw.end3d();
	}

	// ---- the screens ------------------------------------------------------------------------

	static function loadingScreen() {
		final cx = visible.x + visible.z / 2, cy = visible.y + visible.w / 2;
		final white = Color.get(ColorStock.WHITE);
		Draw.textAligned(0, "Wardens", cx, cy - 40, 48, white, TextHalign.CENTER, TextValign.MIDDLE);
		Draw.rectangle(cx - 200, cy + 10, 400, 14, Color.make(50, 44, 40, 255));
		Draw.rectangle(cx - 200, cy + 10, 400 * Assets.progress(), 14, Color.make(230, 180, 90, 255));
	}

	static function titleScreen() {
		Menu.begin(State.TITLE, false);
		final cx = visible.x + visible.z / 2;
		Menu.shadowed("Wardens", cx, visible.y + 60, 84, Color.make(250, 230, 190, 255), TextHalign.CENTER, TextValign.TOP);
		Menu.shadowed('Best: ${Menu.clock(Save.best)}', cx, visible.y + 168, 30, Color.make(230, 215, 190, 255), TextHalign.CENTER,
			TextValign.TOP);
		final bw = 260.0, bh = 62.0;
		final by = visible.y + visible.w - 190;
		if (Menu.button("Play", cx - bw - 20, by, bw, bh))
			startPlay();
		if (Menu.button("Quit", cx + 20, by, bw, bh, App.canQuit()))
			App.quit();
	}

	static function pauseScreen() {
		Menu.begin(State.PAUSED, false);
		final w = 420.0, h = 450.0;
		final x = visible.x + (visible.z - w) / 2, y = visible.y + (visible.w - h) / 2;
		Draw.rectangle(visible.x, visible.y, visible.z, visible.w, Color.make(0, 0, 0, 110));
		Menu.panel(x, y, w, h);
		Draw.textAligned(Assets.font, "Paused", x + w / 2, y + 30, 40, Menu.dark, TextHalign.CENTER, TextValign.TOP);
		final bw = 280.0, bh = 58.0, bx = x + (w - bw) / 2;
		if (Menu.button("Resume", bx, y + 100, bw, bh) || Action.isPressed("pause")) {
			resume();
			return;
		}
		if (Menu.button("Restart", bx, y + 170, bw, bh)) {
			Save.write();
			startPlay();
			return;
		}
		if (Menu.button("Title", bx, y + 240, bw, bh)) {
			Save.write();
			toTitle();
			return;
		}
		final before = Save.volume;
		Save.volume = Menu.slider("Volume", Save.volume, x + 40, y + 330, w - 80, 70);
		if (Save.volume != before)
			Bus.getMaster().setVolume(Save.volume);
	}

	static function levelUpScreen() {
		Menu.begin(State.LEVEL_UP, true);
		Draw.rectangle(visible.x, visible.y, visible.z, visible.w, Color.make(0, 0, 0, 90));
		final cx = visible.x + visible.z / 2;
		Menu.shadowed('Level ${game.level}!', cx, visible.y + 90, 48, Color.make(250, 230, 190, 255), TextHalign.CENTER,
			TextValign.TOP);
		Menu.shadowed("Choose one", cx, visible.y + 148, 22, Color.make(230, 215, 190, 255), TextHalign.CENTER, TextValign.TOP);
		final w = 280.0, h = 330.0, gap = 36.0;
		final x0 = cx - (3 * w + 2 * gap) / 2, y = visible.y + (visible.w - h) / 2 + 40;
		for (i in 0...cards.length) {
			if (Menu.card(cards[i], x0 + i * (w + gap), y, w, h)) {
				game.apply(cards[i]);
				if (game.levelUps > 0) {
					cards = game.drawCards();
				} else {
					state = State.PLAY;
					game.world.setPaused(false);
				}
				return;
			}
		}
	}

	static function gameOverScreen() {
		Menu.begin(State.GAME_OVER, false);
		final w = 480.0, h = 470.0;
		final x = visible.x + (visible.z - w) / 2, y = visible.y + (visible.w - h) / 2;
		Draw.rectangle(visible.x, visible.y, visible.z, visible.w, Color.make(0, 0, 0, 120));
		Menu.panel(x, y, w, h);
		final c = x + w / 2;
		Draw.textAligned(Assets.font, "Game over", c, y + 28, 42, Menu.dark, TextHalign.CENTER, TextValign.TOP);
		final text = Color.make(70, 50, 35, 255);
		Draw.textAligned(Assets.font, 'Time survived: ${Menu.clock(game.time)}', c, y + 96, 26, text, TextHalign.CENTER,
			TextValign.TOP);
		Draw.textAligned(Assets.font, 'Level ${game.level}', c, y + 134, 26, text, TextHalign.CENTER, TextValign.TOP);
		Draw.textAligned(Assets.font, 'Enemies defeated: ${game.kills}', c, y + 172, 26, text, TextHalign.CENTER, TextValign.TOP);
		if (newBest)
			Draw.textAligned(Assets.font, "A new best!", c, y + 218, 30, Color.make(200, 90, 20, 255), TextHalign.CENTER,
				TextValign.TOP);
		else
			Draw.textAligned(Assets.font, 'Best: ${Menu.clock(Save.best)}', c, y + 222, 24, text, TextHalign.CENTER, TextValign.TOP);
		final bw = 280.0, bh = 58.0, bx = x + (w - bw) / 2;
		if (Menu.button("Restart", bx, y + 290, bw, bh)) {
			startPlay();
			return;
		}
		if (Menu.button("Title", bx, y + 360, bw, bh))
			toTitle();
	}

	// ---- the HUD ----------------------------------------------------------------------------

	static function hud() {
		final left = visible.x, top = visible.y, right = visible.x + visible.z, bottom = visible.y + visible.w;
		final light = Color.make(245, 235, 215, 255);
		// the experience bar along the top, the level's number at its left
		final barX = left + 96, barW = visible.z - 96 - 16, barH = 18.0;
		Draw.rectangle(left, top, visible.z, 30, Color.make(16, 12, 10, 200));
		Draw.rectangle(barX, top + 6, barW, barH, Color.make(40, 34, 30, 255));
		Draw.rectangle(barX, top + 6, barW * Math.min(1, game.xp / game.xpToNext()), barH, Color.make(90, 220, 120, 255));
		Draw.rectangleLines(barX, top + 6, barW, barH, 1, Color.make(120, 100, 80, 255));
		Menu.shadowed('Lv ${game.level}', left + 14, top + 15, 20, light, TextHalign.LEFT, TextValign.MIDDLE);
		// the time survived at the top left; enemies defeated and frames a second at the top right
		Menu.shadowed(Menu.clock(game.time), left + 18, top + 44, 34, light, TextHalign.LEFT, TextValign.TOP);
		Menu.shadowed('Defeated: ${game.kills}', right - 18, top + 44, 22, light, TextHalign.RIGHT, TextValign.TOP);
		Menu.shadowed('FPS: ${Std.int(Loop.getFps() + 0.5)}', right - 18, top + 72, 18, Color.make(200, 200, 190, 255),
			TextHalign.RIGHT, TextValign.TOP);
		// her gear at the bottom: head, body, legs, feet, each its outfit's icon with its slot's; then her weapons
		final size = 52.0, gap = 8.0;
		final count = 4 + (game.hasGun ? 2 : 1);
		var x = left + (visible.z - (count * size + (count - 1) * gap + 16)) / 2;
		final y = bottom - size - 18;
		Menu.panel(x - 10, y - 10, count * size + (count - 1) * gap + 36, size + 20);
		for (s in 0...4) {
			Menu.icon(5 + game.warden.outfits[s], x, y, size);
			Menu.icon(15 + s, x + size - 22, y + size - 22, 22);
			x += size + gap;
		}
		x += 16;
		for (w in 0...(game.hasGun ? 2 : 1)) {
			if (game.holding == w)
				Draw.rectangleRounded(x - 3, y - 3, size + 6, size + 6, 6, 0, Color.make(250, 210, 120, 255));
			Menu.icon(19 + w, x, y, size, game.holding == w ? -1 : Color.make(160, 160, 160, 255));
			x += size + gap;
		}
	}

	/** Once the screen has been touched: the stick where a finger holds it, the pause button, and the switch button once she has both weapons. **/
	static function touchControls() {
		if (!touched)
			return;
		final b = touchButtons();
		final right = visible.x + visible.z;
		final bx = right - b.size - b.margin;
		Menu.drawButton("II", bx, b.pauseY, b.size, b.size, Action.isDown("touch_pause") ? Menu.PRESSED : Menu.NORMAL, 28);
		if (game.hasGun) {
			Menu.drawButton("", bx, b.switchY, b.size, b.size, Action.isDown("touch_switch") ? Menu.PRESSED : Menu.NORMAL);
			Menu.icon(game.holding == Game.SWORD ? 20 : 19, bx + 14, b.switchY + 12, b.size - 28);
		}
		for (stick in ["stick_x", "stick2_x"]) {
			if (!Action.isStickHeld(stick))
				continue;
			Action.getStickOrigin(stick, stickAt);
			Action.getStickKnob(stick, knobAt);
			Draw.circle(stickAt.x, stickAt.y, 70, Color.make(255, 255, 255, 50));
			Draw.circleLines(stickAt.x, stickAt.y, 70, 3, Color.make(255, 255, 255, 140));
			Draw.circle(knobAt.x, knobAt.y, 30, Color.make(255, 240, 220, 170));
		}
	}
}

enum abstract State(Int) to Int {
	var LOADING = 0;
	var TITLE = 1;
	var PLAY = 2;
	var PAUSED = 3;
	var LEVEL_UP = 4;
	var GAME_OVER = 5;
}
