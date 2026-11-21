import wgf.*;

/**
	The feature test: one program reaching every call of libwgf's public API through the
	binding, on every target (hxcpp natively, headless; JS under node on the headless host;
	JS in a browser on the full host), every component in a scene with them. Each section
	is a group of calls with what it can check where the answer is the same everywhere; a
	call whose answer is the platform's (a window's position headless, a pad that isn't
	there) is made and its answer left alone. Built with -D wgf_reach, the binding counts
	every call, and the run fails naming any it never made (tools/check_features.py).
**/
class Rock extends Behavior {
	public static var made = 0;
	public static var hits = 0;

	override function onCreate()
		made++;

	override function onTriggerEnter(other:Actor, layer:Int)
		hits++;
}

class Title extends Behavior {
	public static var entered = 0;
	public static var exited = 0;

	override function onScreenEnter(from:Actor)
		entered++;

	override function onScreenExit(to:Actor)
		exited++;
}

class Main {
	static var failures = 0;
	static var frames = 0;
	static var world:Stage2d;
	static var hud:Stage2d;
	static var camera:Camera2d;
	static var tiles:Texture;
	static var car:Model = 0; // a section type starts as none, as a handle does
	static var robot:Model = 0; // a skinned, animated glTF file's root
	static var gems:Sheet = 0; // an atlas, TexturePacker's hash
	static var island:Tilemap = 0; // a Tiled map, gfx-tilemap's
	static var font:Font;
	static var pixelFont:Font = 0; // a bitmap font, a BMFont's .fnt and its page
	static var picture:Image = 0; // an image on the CPU, loaded from a file
	static var checks:Image = 0; // an image made in code
	static var checksTexture:Texture = 0; // a texture made from it
	static var tilesRead:TextureRead = 0; // a texture loaded from a file, read back
	static var menu:Document;
	static var menuDone = false;
	static var click:Sound;
	static var music:Sound;
	static var voice:Voice;
	static var scene:Scene;
	static var instantiated = false;
	static var tasks:Array<FsTask> = [];
	static var ensure:AssetTask;
	static var group:AssetTask;
	static var ping:AssetTask;
	static var reported = false;

	static function expect(ok:Bool, what:String, ?pos:haxe.PosInfos):Void {
		if (!ok) {
			failures++;
			Log.message(LogLevel.ERROR, 'FAIL (line ${pos.lineNumber}): $what');
		}
	}

	static function near(a:Float, b:Float):Bool
		return Math.abs(a - b) < 1e-3;

	static function main():Void {
		Window.setTitle("feature-test");
		Window.setSize(640, 360);
		if (!Runtime.run(init, tick, frame, shutdown))
			Log.message(LogLevel.ERROR, "feature test: the run was refused");
	}

	// ---- init: everything that needs no frame ------------------------------------------

	static function init():Void {
		core();
		collide2d();
		tweens();
		platform();
		assets();
		audio();
		gfx();
		images();
		ecs();
		physics();
	}

	static function collide2d():Void {
		final hit:Array<Float> = [0, 0, 0, 0, 0, 0];
		final out:Array<Float> = [0, 0, 0, 0, 0, 0];
		final square:Array<Float> = [0, 0, 10, 0, 10, 10, 0, 10];
		final shifted:Array<Float> = [8, 2, 18, 2, 18, 12, 8, 12];
		expect(Collide2d.rectRect(0, 0, 10, 10, 8, 2, 10, 10, hit) && near(hit[2], 2), "rectangles overlapping");
		expect(Collide2d.circleCircle(0, 0, 5, 8, 0, 5, hit) && Collide2d.circleRect(5, -2, 3, 0, 0, 10, 10, hit)
			&& Collide2d.circleSegment(5, 1, 2, 0, 0, 10, 0, hit) && Collide2d.circlePolygon(5, 12, 3, square, hit)
			&& Collide2d.polygonPolygon(square, shifted, hit), "circles and polygons overlapping");
		expect(Collide2d.pointInRect(1, 1, 0, 0, 10, 10) && Collide2d.pointInCircle(1, 1, 0, 0, 2)
			&& Collide2d.pointInTriangle(2, 1, 0, 0, 10, 0, 0, 10) && Collide2d.pointInPolygon(5, 5, square), "points in shapes");
		expect(Collide2d.segmentSegment(0, 5, 10, 5, 4, 0, 4, 10, hit) && near(hit[5], 0.4), "segments crossing");
		{ // 3D shapes and rays, and splines
			final hit3:Array<Float> = [0, 0, 0, 0, 0, 0, 0, 0];
			expect(Collide3d.pointInBox(1, 1, 1, 0, 0, 0, 2, 2, 2) && Collide3d.pointInSphere(0, 0.5, 0, 0, 0, 0, 1)
				&& Collide3d.boxBox(0, 0, 0, 2, 2, 2, 1, 0, 0, 3, 2, 2, hit3) && Collide3d.sphereSphere(0, 0, 0, 1, 1.5, 0, 0, 1, hit3)
				&& Collide3d.sphereBox(0, 2.5, 0, 1, -1, -1, -1, 1, 2, 1, hit3), "3D points and overlaps");
			expect(Collide3d.raySphere(-5, 0, 0, 1, 0, 0, 0, 0, 0, 1, hit3) && near(hit3[7], 4)
				&& Collide3d.rayBox(0, 5, 0, 0, -1, 0, -1, -1, -1, 1, 1, 1, hit3)
				&& Collide3d.rayTriangle(1, 1, 0, 0, 0, 1, [0, 0, 5, 4, 0, 5, 0, 4, 5], hit3) && near(hit3[7], 5), "rays");
			final path:Array<Float> = [0, 0, 5, 10, 10, 0];
			expect(Spline.getSegmentCount(SplineKind.BEZIER_QUADRATIC, path) == 1
				&& near(Spline.getPoint(SplineKind.BEZIER_QUADRATIC, path, 0.5).y, 5), "a spline's point");
		}
		expect(Collide2d.rayRect(-10, 5, 20, 0, 0, 0, 10, 10, hit) && Collide2d.rayCircle(-10, 0, 20, 0, 0, 0, 2, hit)
			&& Collide2d.sweepRectRect(-20, 0, 10, 10, 20, 0, 0, 0, 10, 10, hit)
			&& Collide2d.sweepCircleCircle(-10, 0, 1, 20, 0, 0, 0, 1, hit), "rays and sweeps");
		expect(Collide2d.moveRects(10, 80, 16, 16, 0, 10, [0, 100, 200, 10], out) && near(out[1], 84) && out[5] == 1,
			"moved onto a floor");
		final cells = [for (i in 0...20) i >= 10 ? 1 : 0];
		expect(Collide2d.moveCells(20, -10, 16, 16, 0, 40, 16, cells, 10, out) && near(out[1], 0) && out[5] == 1,
			"moved onto a row of cells");
	}

	static function tweens():Void {
		expect(near(Ease.value(Easing.QUAD_IN, 0.5), 0.25) && near(Ease.value(Easing.BOUNCE_OUT, 1), 1), "easing");
		final number = Tween.create(0, 10, 1, Easing.LINEAR);
		expect(number.setDelay(0.1) && near(number.getDelay(), 0.1) && number.setRepeat(1) && number.getRepeat() == 1
			&& number.setYoyo(true) && number.isYoyo() && number.setPaused(true) && number.isPaused() && number.setPaused(false),
			"a number's tween, its delay, repeat, yoyo, and pause");
		number.getValue();
		number.destroy();
		final mover = Actor.create();
		final moving = Tween.move(mover, 1, 2, 3, 0.5, Easing.SINE_OUT);
		final turning = Tween.turn(mover, 0, 1, 0, 0.5, Easing.BACK_OUT);
		final growing = Tween.scale(mover, 2, 2, 2, 0.5, Easing.ELASTIC_OUT);
		expect(moving.isAlive() && turning.isAlive() && growing.isAlive(), "an actor moved, turned, and scaled");
		final drawn = Texture.createBlank(2, 2);
		final pixels = haxe.io.Bytes.alloc(16);
		pixels.fill(0, 16, 255);
		expect(drawn.getWidth() == 2 && drawn.write(pixels) && !drawn.write(pixels), "a texture made in code, written once a frame");
		final box = Mesh.createCube(2, 4, 6), boxed:Model = Model.create(box);
		expect(near(box.getBoundsMax().z, 3) && near(box.getBoundsMin().y, -2) && near(boxed.getBoundsMax().y, 2)
			&& near(boxed.getBoundsMin().x, -1), "a mesh's and a model's bounds");
		boxed.destroy(ActorDestroy.DESTROY_CHILDREN);
		mover.destroy(ActorDestroy.DESTROY_CHILDREN);
	}

	static function core():Void {
		expect(Version.get() != "" && Version.getMajor() >= 0 && Version.getMinor() >= 0 && Version.getPatch() >= 0,
			"the version");
		Log.setLevel(LogLevel.INFO);
		expect(Log.getLevel() == LogLevel.INFO, "the log level");
		expect(Log.setHistory(4) && Log.getHistory() == 4, "log lines kept");
		Log.message(LogLevel.WARN, "kept for the game");
		expect(Log.getHistoryCount() == 1 && Log.getHistoryLine(0) == "kept for the game" && Log.getHistoryLevel(0) == LogLevel.WARN,
			"a line kept, read back");
		Log.clearHistory();
		Log.setHistory(0);
		Log.message(LogLevel.INFO, "feature-test begins");
		Log.messageSource(LogLevel.DEBUG, "Main.hx", 1, "a line at its source, below the level shown");
		expect(Identity.setCompany("Whirling Gizmo") && Identity.getCompany() == "Whirling Gizmo", "the company");
		expect(Identity.setProduct("feature-test") && Identity.getProduct() == "feature-test", "the product");
		Random.setSeed(7);
		expect(Random.getSeed() == 7, "the seed");
		final f = Random.getFloat(), r = Random.getRange(2, 3), i = Random.getInt(1, 6);
		expect(f >= 0 && f < 1 && r >= 2 && r <= 3 && i >= 1 && i <= 6, "random numbers in range");
		final deck = [1, 2, 3, 4];
		expect(Random.shuffle(deck) == 4, "a shuffle");
		final own = Random.create(5);
		expect(Random.getGeneratorSeed(own) == 5 && Random.setGeneratorSeed(own, 6) && Random.nextFloat(own) < 1
			&& Random.nextRange(own, 2, 3) >= 2 && Random.nextInt(own, 1, 6) >= 1 && Random.nextShuffle(own, deck) == 4,
			"a game's own generator");
		Random.destroy(own);
		final hued = Color.makeHsv(120, 1, 1, 255);
		expect(near(Color.getHue(hued), 120) && near(Color.getSaturation(hued), 1) && near(Color.getValue(hued), 1)
			&& Color.brighten(hued, 0.5) != hued && Color.multiply(hued, Color.get(ColorStock.WHITE)) == hued, "color math");
		expect(!App.hasOption("wgf-feature-missing") && App.getOption("wgf-feature-missing") == ""
			&& App.getOptionInt("wgf-feature-missing", 3) == 3 && App.getOptionFloat("wgf-feature-missing", 1.5) == 1.5,
			"an option not given");
		expect(Time.getSeconds() >= 0, "the time");
		expect(Probe.setValue("feature.answer", 42) && Probe.hasValue("feature.answer") && Probe.getValue("feature.answer") == 42,
			"a probe");
		expect(Probe.getCount() >= 1 && Probe.getName(0) != "", "the probes listed");
		expect(Probe.setText("feature.screen", "title") && Probe.getText("feature.screen") == "title"
			&& Probe.getText("feature.answer") == "", "a text probe");
		// the root is the program's directory, or "" (the working directory) where that can't be told
		expect(Fs.setRoot(Fs.getRoot()), "the storage's root, set to itself");
		tasks.push(Fs.mkdir("feature"));
		tasks.push(Fs.list("feature"));
		tasks.push(Fs.write("feature/a.txt", haxe.io.Bytes.ofString("feature")));
		expect(Resource.setLoadBudget(4) && near(Resource.getLoadBudget(), 4), "the load budget");
	}

	static function platform():Void {
		expect(Window.getTitle() == "feature-test", "the title");
		Window.getWidth();
		Window.getHeight();
		Window.requestFullscreen(false);
		Window.isFullscreen();
		Window.canFullscreen();
		Window.setPosition(10, 10);
		Window.getX();
		Window.getY();
		final monitors = Window.getMonitorCount();
		Window.setMonitor(0);
		Window.getMonitor();
		Window.getMonitorWidth(0);
		Window.getMonitorHeight(0);
		Window.getMonitorX(0);
		Window.getMonitorY(0);
		Window.getMonitorName(0);
		expect(monitors >= 0, "the monitors");
		Window.setVisible(true);
		Window.isVisible();
		Window.setResizable(true);
		Window.isResizable();
		Window.setDecorated(true);
		Window.isDecorated();
		Window.isFocused();
		Window.isResized();
		expect(Window.setIcon(haxe.io.Bytes.alloc(16 * 16 * 4), 16, 16), "a window icon");
		Window.setTransparent(false);
		Window.isTransparent();
		Window.setHighDpi(true);
		Window.isHighDpi();
		Window.setVsync(true);
		Window.isVsync();
		Window.setMsaa(false);
		Window.isMsaa();
		Loop.setTickRate(60);
		expect(Loop.getTickRate() == 60 && near(Loop.getTickDelta(), 1 / 60), "the tick rate");
		Loop.setTimeScale(1);
		expect(Loop.getTimeScale() == 1, "the time scale");
		Loop.setTargetFps(0);
		expect(Loop.getTargetFps() == 0, "no target fps");
		Loop.setPauseUnfocused(true);
		expect(Loop.isPauseUnfocused(), "held while unfocused");
		Loop.setPauseUnfocused(false);
		Gamepad.setDeadzone(0.2);
		expect(near(Gamepad.getDeadzone(), 0.2), "the dead zone");
		expect(Gamepad.getAxis(0, GamepadAxis.LEFT_X) == 0, "no pad, no axis");
		expect(Action.bindKeys("steer", KeyboardKey.LEFT, KeyboardKey.RIGHT) && Action.bindKey("fire", KeyboardKey.SPACE)
			&& Action.bindPadButton("fire", GamepadButton.SOUTH) && Action.bindPadAxis("steer", GamepadAxis.LEFT_X, 0, 0)
			&& Action.bindTouch("fire", 0, 0, 100, 100)
			&& Action.bindPadButtons("steer", GamepadButton.DPAD_LEFT, GamepadButton.DPAD_RIGHT), "input actions bound");
		expect(Action.getCount() >= 2 && Action.getName(0) == "steer" && Action.getBindingCount("steer") == 3
			&& Action.getBindingText("steer", 0) == "Left / Right", "listed");
		expect(Action.getAxis("steer") == 0 && Action.getValue("fire") == 0 && Action.getState("fire") == InputState.UP
			&& !Action.isDown("fire") && !Action.isPressed("fire") && !Action.isReleased("fire"), "read, at rest");
		expect(Action.clear("fire") && Action.getBindingCount("fire") == 0, "cleared");
		expect(Action.bindTouchStick("move_x", "move_y", 0, 0, 400, 600, 60) && !Action.isStickHeld("move_x")
			&& Action.getStickOrigin("move_x").x == 0 && Action.getStickKnob("move_x").y == 0, "an on-screen stick, not held");
		Touch.setMouseEmulated(true);
		expect(Touch.isMouseEmulated(), "touch drives the mouse");
		Touch.setFromMouse(true);
		expect(Touch.isFromMouse(), "the mouse's left button a finger");
		Touch.setFromMouse(false);
		final tilted = Tilt.request();
		expect(tilted ? Tilt.getStatus() != TiltStatus.OFF : Tilt.getStatus() == TiltStatus.UNAVAILABLE
			|| Tilt.getStatus() == TiltStatus.NEEDS_HTTPS, "the tilt asked for");
		Tilt.setRange(45);
		expect(Tilt.getRange() == 45 && Tilt.getX() == 0 && Tilt.getY() == 0 && !Tilt.isReady() && !Tilt.isCalibrating(),
			"no reading yet, a range");
		Tilt.calibrate();
		Tilt.release();
		expect(Tilt.getStatus() == TiltStatus.OFF, "the tilt released");
		Mouse.setLocked(false);
		expect(!Mouse.isLocked(), "the mouse free");
		Mouse.setCursorVisible(true);
		Mouse.isCursorVisible();
		Input.setPointerCaptured(false);
		expect(!Input.isPointerCaptured(), "the pointer the game's");
		Input.setKeyboardCaptured(false);
		expect(!Input.isKeyboardCaptured(), "the keyboard the game's");
	}

	static function assets():Void {
		expect(Asset.setHost("../assets") && Asset.getHost() != "", "the asset host (beside the program)");
		Log.message(LogLevel.INFO, "feature-test assets from " + Asset.getHost());
		expect(Asset.reload("nothing/made/from.png") == 0, "a reload of a path nothing was made from: none");
		Asset.setCacheDir("feature-cache");
		Asset.getCacheDir();
		Asset.setFetching(false);
		expect(!Asset.isFetching(), "fetching off");
		// the web's fetch has no timeout of its own: there the call stores nothing, and says so
		final timed = Asset.setFetchTimeout(10);
		expect(timed ? near(Asset.getFetchTimeout(), 10) : Asset.getFetchTimeout() >= 0, "the fetch timeout");
		Asset.setCacheMode(AssetCacheMode.REVALIDATE);
		Asset.getCacheMode();
		Asset.setManifest("");
		Asset.getManifest();
		expect(Asset.addRedirect("old/", "textures/"), "a redirect");
		Asset.clearRedirects();
		ensure = Asset.ensure("textures/tiles.png", null, 0);
		group = Asset.groupCreate();
		expect(Asset.groupAdd(group, Asset.ensure("sounds/click_004.ogg", null, 0)), "a group");
		ping = Asset.pingHost("../assets", 1000);
		final request = Asset.fetchNext(); // nothing is fetched natively by a program that didn't ask
		Asset.fetchGetUrl(request);
		Asset.fetchGetDest(request);
		Asset.fetchIsPing(request);
		Asset.fetchDone(request, false);
		Asset.evict("nothing/here.png");
		Asset.clearCache();
	}

	static function audio():Void {
		expect(Audio.setVolume(0.5) && near(Audio.getVolume(), 0.5), "the master volume");
		Audio.setPaused(false);
		expect(!Audio.isPaused(), "audio playing");
		click = Sound.create("sounds/click_004.ogg");
		music = Sound.createStreamed("music/a_hero_is_born.mp3");
		expect(click.addSegment("start", 0, 0.05), "a segment");
		voice = Voice.create(click);
		expect(voice.setSound(click) && voice.getSound() == click, "a voice's sound");
		expect(voice.setLoop(false) && !voice.isLoop(), "not looping");
		expect(voice.setVolume(0.8) && near(voice.getVolume(), 0.8), "a voice's volume");
		expect(voice.setPitch(1.25) && near(voice.getPitch(), 1.25), "its pitch");
		expect(voice.setPan(-0.5) && near(voice.getPan(), -0.5), "its pan");
		expect(voice.setSegment("start") && voice.getSegment() == "start", "its segment");
		voice.setPosition(0);
		voice.getPosition();
		expect(voice.play(), "played");
		voice.getState();
		voice.pause();
		voice.resume();
		expect(click.setPolyphony(3) && click.getPolyphony() == 3, "a sound's polyphony");
		expect(click.play(0.5, 1, 0) && !music.play(1, 1, 0), "a sound played once; a streamed one refused");
		final tone = Sound.createFromSamples([for (i in 0...4410) 0.25 * Math.sin(i * 0.0627)], 1, 44100);
		expect(Resource.getStatus(tone) == ResourceStatus.READY && near(tone.getDuration(), 0.1) && tone.play(0.2, 1, 0),
			"a sound made from samples, READY at once and played");
		Resource.release(tone);
		final master = Bus.getMaster(), effects = Bus.getEffects();
		expect(Bus.getMusic().getParent() == master && effects.getParent() == master, "the buses that always exist");
		final ui = effects.create();
		expect(ui.setVolume(0.5) && near(ui.getVolume(), 0.5) && near(ui.getHeardVolume(), 0.5), "a bus's volume, heard");
		expect(ui.setMuted(true) && ui.isMuted() && ui.getHeardVolume() == 0 && ui.setMuted(false), "muted");
		expect(ui.fade(0, 0.5) && ui.isFading(), "a fade");
		expect(voice.setBus(ui) && voice.getBus() == ui && click.setBus(ui) && click.getBus() == ui, "a voice's and a sound's bus");
		ui.destroy();
		expect(voice.getBus() == effects && click.getBus() == effects, "its bus gone: back to its parent");
		expect(voice.setSpatial(true) && voice.isSpatial() && voice.setPlace(1, 2, 3) && near(voice.getPlace().z, 3)
			&& voice.setRange(2, 30) && near(voice.getRange().y, 30) && voice.setSpatial(false), "a spatial voice");
		expect(Audio.setListenerPosition(0, 1, 0) && near(Audio.getListenerPosition().y, 1) && Audio.setListenerFacing(0, 0, -1)
			&& near(Audio.getListenerFacing().z, -1), "the listener");
		// a pushed voice: STOPPED, nothing is taken from its queue, so its fill is the same everywhere
		final pushed = Voice.createPushed(22050, 2, 100);
		expect(!pushed.isNone() && pushed.push([0.5, -0.5, 0.25, -0.25]) == 2 && pushed.pushInt16([16384, -16384]) == 1
			&& pushed.getQueued() == 3 && pushed.getRoom() == 97 && pushed.getUnderruns() == 0, "a pushed voice's queue");
		expect(pushed.push([0.5]) == 0 && Voice.createPushed(22050, 3, 100).isNone(), "part of a frame, and three channels, refused");
		expect(pushed.play() && pushed.stop() && pushed.getQueued() == 0, "played, and stopped: its queue emptied");
		pushed.destroy();
	}

	static var camera3d:Camera3d;
	static var camera3dMade = false;

	static var stage3d:Stage3d;

	/** A stage, its lights, and its models. **/
	static function makeStage():Void {
		stage3d = Stage3d.create();
		expect(stage3d.setCamera(camera3d) && stage3d.getCamera() == camera3d, "a stage's camera");
		expect(stage3d.setAmbient(Color.get(ColorStock.WHITE), 0.2) && stage3d.getAmbientColor() == Color.get(ColorStock.WHITE)
			&& Math.abs(stage3d.getAmbientIntensity() - 0.2) < 1e-6, "its ambient light");
		expect(stage3d.setTonemap(Stage3dTonemap.ACES, 0.5) && stage3d.getTonemap() == Stage3dTonemap.ACES
			&& stage3d.getExposure() == 0.5 && stage3d.setTonemap(Stage3dTonemap.NEUTRAL, 0), "its tone mapping");
		expect(stage3d.setCulling(false) && !stage3d.isCulling() && stage3d.setCulling(true), "its culling");
		final sky:Environment = Environment.create("textures/tiles.png"); // any image is a panorama
		expect(!sky.isNone() && stage3d.setEnvironment(sky, 0.5, 1) && stage3d.getEnvironment() == sky
			&& stage3d.getEnvironmentIntensity() == 0.5 && stage3d.getEnvironmentRotation() == 1, "its environment");
		expect(stage3d.setBackground(sky, 0.25) && stage3d.getBackground() == sky && stage3d.getBackgroundBlur() == 0.25,
			"its background");
		Resource.release(sky); // the stage holds its own
		final smoke = Emitter3d.create();
		smoke.setParent(stage3d);
		expect(smoke.setTexture(tiles) && smoke.getTexture() == tiles && smoke.setTexture(cast 0), "an emitter3d's texture, and none");
		expect(smoke.setSource(0, 0, 16, 16) && near(smoke.getSource().z, 16), "its source");
		expect(smoke.setFrames(2, 2, 3, 12) && smoke.getFrameColumns() == 2 && smoke.getFrameRows() == 2 && smoke.getFrameCount() == 3
			&& near(smoke.getFrameRate(), 12), "its flipbook");
		expect(smoke.skipTrail(), "its trail skipped");
		expect(smoke.setRate(30) && near(smoke.getRate(), 30), "its rate");
		expect(smoke.setEmitting(true) && smoke.isEmitting(), "emitting");
		expect(smoke.setMax(64) && smoke.getMax() == 64, "its max");
		expect(smoke.setLife(0.2, 0.5) && near(smoke.getLifeMin(), 0.2) && near(smoke.getLifeMax(), 0.5), "its life");
		expect(smoke.setSpawnBox(1, 2, 3) && near(smoke.getSpawnBox().z, 3), "its spawn box");
		expect(smoke.setSpawnSphere(4) && near(smoke.getSpawnSphere(), 4), "its spawn sphere");
		expect(smoke.setVelocity(0, 5, 0, 0.5, 0.25) && near(smoke.getVelocity().y, 5) && near(smoke.getSpread(), 0.5)
			&& near(smoke.getSpeedVariance(), 0.25), "its velocity");
		expect(smoke.setGravity(0, -9.8, 0) && near(smoke.getGravity().y, -9.8), "its gravity");
		expect(smoke.setDrag(0.5) && near(smoke.getDrag(), 0.5), "its drag");
		expect(smoke.setInheritVelocity(0.5) && near(smoke.getInheritVelocity(), 0.5), "its inherited velocity");
		expect(smoke.setStretch(0.1) && near(smoke.getStretch(), 0.1), "its stretch");
		expect(smoke.setSpawnShell(true) && smoke.isSpawnShell() && smoke.setSpawnShell(false), "its spawn shell");
		expect(smoke.setAlignedToMotion(true) && smoke.isAlignedToMotion(), "aligned to its motion");
		expect(smoke.setWander(0.5, 2) && near(smoke.getWanderDistance(), 0.5) && near(smoke.getWanderRate(), 2)
			&& !smoke.setWander(-1, 1), "its wander");
		expect(smoke.setOrbit(1, 3) && near(smoke.getOrbitMin(), 1) && near(smoke.getOrbitMax(), 3), "its orbit");
		expect(smoke.setPulse(2, 1.5) && near(smoke.getPulseRate(), 2) && near(smoke.getPulseDepth(), 1), "its pulse, its depth held to 1");
		expect(smoke.setLocalSpace(true) && smoke.isLocalSpace() && smoke.setLocalSpace(false), "local space");
		expect(smoke.setSize(4, 1, 0.5) && near(smoke.getSizeKeyValue(0), 4) && near(smoke.getSizeVariance(), 0.5), "its sizes");
		expect(smoke.clearSizeKeys() && smoke.addSizeKey(0.5, 2) && smoke.getSizeKeyCount() == 1
			&& near(smoke.getSizeKeyTime(0), 0.5), "a size key");
		expect(smoke.setColor(Color.get(ColorStock.WHITE), 0) && smoke.getColorKeyValue(0) == Color.get(ColorStock.WHITE), "its colors");
		expect(smoke.clearColorKeys() && smoke.addColorKey(0.5, Color.get(ColorStock.WHITE)) && smoke.getColorKeyCount() == 1
			&& near(smoke.getColorKeyTime(0), 0.5), "a color key");
		expect(smoke.addPaletteColor(Color.get(ColorStock.WHITE)) && smoke.getPaletteCount() == 1 && smoke.getPaletteColor(0) == Color.get(ColorStock.WHITE)
			&& smoke.clearPalette(), "its palette");
		expect(smoke.setSpin(1, 2) && near(smoke.getSpinMin(), 1) && near(smoke.getSpinMax(), 2), "its spin");
		expect(smoke.setAlphaMode(AlphaMode.BLEND, 0.25) && smoke.getAlphaMode() == AlphaMode.BLEND
			&& near(smoke.getAlphaCutoff(), 0.25), "its alpha");
		expect(smoke.setSeed(42) && smoke.getSeed() == 42, "its seed");
		expect(smoke.setEnding(false) && !smoke.isEnding() && smoke.setLingering(true) && smoke.isLingering(), "ending, lingering");
		expect(smoke.prewarm(0.5) && smoke.burst(8) && smoke.getCount() >= 8, "prewarmed, and a burst");
		expect(smoke.clear() && smoke.getCount() == 0, "its particles cleared");
		smoke.burst(8);
		final sign = Sprite.create(tiles); // a 3D stage's sprite: the instanced batch
		sign.setParent(stage3d);
		sign.setPosition(0, 2, 0);
		expect(sign.setFacing(SpriteFacing.CAMERA_FIXED_Y) && sign.setSize(1, 1), "a sprite on the 3D stage");
		final banner = Text.create(0); // a 3D stage's text: a block facing as it is told
		banner.setParent(stage3d);
		banner.setPosition(0, 3, 0);
		expect(banner.setString("START") && banner.getFontSize() == 1, "text on the 3D stage, a unit a line");
		expect(banner.setFacing(SpriteFacing.CAMERA_FIXED_Y) && banner.getFacing() == SpriteFacing.CAMERA_FIXED_Y,
			"its facing");
		expect(sign.setDepthOffset(0.01) && near(sign.getDepthOffset(), 0.01) && banner.setDepthOffset(0.02)
			&& near(banner.getDepthOffset(), 0.02), "drawn nearer the camera: depth offsets");
		final sun:Light = Light.create(LightType.SPOT);
		expect(sun.getType() == LightType.SPOT && sun.setColor(Color.get(ColorStock.GOLD))
			&& sun.getColor() == Color.get(ColorStock.GOLD), "a light's color");
		expect(sun.setIntensity(3) && sun.getIntensity() == 3 && sun.setRange(20) && sun.getRange() == 20, "its strength");
		expect(sun.setSpotCone(0.25, 0.5) && sun.getSpotInnerAngle() == 0.25 && sun.getSpotOuterAngle() == 0.5, "its cone");
		sun.setParent(stage3d);
		expect(sun.setName("sun") && stage3d.find("sun") == (sun : Actor), "found on its stage");
		sun.setPosition(0, 5, 0);
		final cube = Mesh.createCube(1, 1, 1);
		final model:Model = Model.create(cube);
		expect(model.getMesh() == cube && model.setMesh(cube) && model.setTint(Color.get(ColorStock.WHITE))
			&& model.getTint() == Color.get(ColorStock.WHITE), "a model");
		final own = Material.create(MaterialShading.UNLIT);
		expect(model.setMaterial(0, own) && model.getMaterial(0) == own && Resource.release(own), "its own material");
		model.setParent(stage3d);
		expect(sun.setShadowCasting(true) && sun.isShadowCasting() && sun.setShadowDistance(30)
			&& sun.getShadowDistance() == 30 && sun.setShadowMapSize(1000) && sun.getShadowMapSize() == 512,
			"a light's shadows: on, their reach, their map (a power of two)");
		expect(sun.setShadowStrength(0.5) && sun.getShadowStrength() == 0.5 && sun.setShadowColor(Color.get(ColorStock.DARKBLUE))
			&& sun.getShadowColor() == Color.get(ColorStock.DARKBLUE), "how dark, and a tint");
		expect(sun.setShadowBias(2, 3) && sun.getShadowBiasConstant() == 2 && sun.getShadowBiasSlope() == 3
			&& !sun.setShadowBias(-1, 0), "its bias, a negative refused");
		expect(model.isShadowCasting() && model.setShadowCasting(false) && !model.isShadowCasting()
			&& model.isShadowReceiving() && model.setShadowReceiving(false) && !model.isShadowReceiving()
			&& model.setShadowCasting(true) && model.setShadowReceiving(true), "a model's shadows, cast and received");
		Resource.release(cube);
		final car:Actor = Model.create(0);
		car.setParent(stage3d);
		expect(car.addComponent(Component.MOTION) && (car : Motion).setVelocity(0, 0, 1), "a model with motion");
		final shape:Shape3d = Shape3d.create();
		expect(shape.getKind() == Shape3dKind.NONE && shape.setCube(1, 2, 3) && shape.getKind() == Shape3dKind.CUBE
			&& shape.getSize().z == 3, "a 3D shape: a cube");
		expect(shape.setSphere(2) && shape.getRadius() == 2 && shape.setRectangle(1, 2) && shape.getSize().y == 2
			&& shape.setCircle(1), "a sphere, a rectangle, a circle");
		expect(shape.setLine(0, 0, 0, 1, 2, 3) && shape.getLineStart().x == 0 && shape.getLineEnd().z == 3, "a line");
		final points = [0.0, 0, 0, 1, 0, 0, 1, 1, 0];
		final back = [for (i in 0...9) 0.0];
		expect(shape.setLineStrip(points) && shape.getPointCount() == 3 && shape.getPoints(back) == 9 && back[7] == 1,
			"a line strip, from an array and back into one");
		expect(shape.setColor(Color.get(ColorStock.LIME)) && shape.getColor() == Color.get(ColorStock.LIME), "its color");
		shape.setParent(stage3d);
	}

	/** Generated meshes and materials. **/
	static function meshes():Void {
		// the same bits on every target: a simulation's trigonometry (wgf_trig.h)
		expect(Trig.sin(0) == 0 && Trig.cos(0) == 1 && Math.abs(Trig.tan(0.5) - 0.5463025) < 1e-6
			&& Math.abs(Trig.atan2(1, 1) - Math.PI / 4) < 1e-6 && Math.abs(Trig.asin(1) - Math.PI / 2) < 1e-6
			&& Math.abs(Trig.acos(0) - Math.PI / 2) < 1e-6, "trigonometry alike on every target");
		final cube = Mesh.createCube(1, 2, 3);
		expect(!cube.isNone() && Mesh.createCube(1, 2, 3) == cube && Resource.release(cube), "a cube, shared");
		for (mesh in [Mesh.createPlane(2, 2, 1), Mesh.createSphere(1, 8, 16), Mesh.createCylinder(1, 2, 12),
			Mesh.createCone(1, 2, 12), Mesh.createCapsule(0.5, 2, 8, 12), Mesh.createTorus(1, 0.25, 12, 8)])
			expect(!mesh.isNone() && mesh.getMaterialCount() == 1 && Resource.release(mesh), "a generated shape");
		final strip = Mesh.createTriangles([0.0, 0, 0, 0, 0, 1, 1, 0, 0], [], [], [0, 1, 2]);
		expect(!strip.isNone() && strip.getMaterialCount() == 1 && Resource.release(strip), "the program's own triangles");
		final land = Mesh.createHeightmap([0.0, 1, 0, 0, 1, 0], 3, 2, 2, 4);
		final corners = [for (_ in 0...24) 0.0];
		expect(land.getVertexCount() == 6 && land.getPositions(corners) == 18 && near(corners[4], 2)
			&& land.getNormals(corners) == 18 && land.getUvs(corners) == 12 && near(corners[10], 1),
			"a heightmap: its corners, normals, and texture coordinates");
		final flat = [for (_ in 0...18) 0.0];
		land.getPositions(flat);
		for (i in 0...6)
			flat[i * 3 + 1] = 0.5;
		expect(land.setPositions(flat) && near(land.getBoundsMax().y, 0.5) && land.setNormals([]) && land.setUvs([])
			&& land.getNormals(corners) == 18 && near(corners[1], 1), "changed in place: flattened, its normals made again");
		expect(land.setColors([for (i in 0...24) i % 4 == 1 ? 0.5 : 1.0]) && land.getColors(corners) == 24
			&& Math.abs(corners[1] - 0.5) < 1e-3 && !land.setColors([1.0, 0, 0, 1]),
			"its corners colored; a count that isn't its corners': refused");
		expect(Resource.release(land), "a heightmap let go of");
		final file = Mesh.create("models/toy_car.glb");
		car = Model.create(file);
		expect(!file.isNone() && !car.isNone() && Resource.release(file), "a glTF file's mesh, a model of it");
		final robotFile = Mesh.create("models/robot.glb");
		robot = Model.create(robotFile);
		Resource.release(robotFile);
		expect(robot.getAnimationCount() == 0 && robot.findAnimation("Robot_Dance") == -1 && robot.getAnimation() == -1
			&& robot.getAnimationState() == PlayState.STOPPED, "no animations while a skinned file loads");
		expect(robot.playAnimation(0, 0) && robot.getAnimationState() == PlayState.PLAYING,
			"an animation played while it loads");
		final material = cube.getMaterial(0);
		expect(material.getShading() == MaterialShading.PBR && material.getFloat("metallic") == 0, "the mesh's material");
		final own = Material.create(MaterialShading.UNLIT);
		expect(own.setShading(MaterialShading.PBR) && own.getShading() == MaterialShading.PBR, "a material's shading");
		expect(own.setAlphaMode(AlphaMode.MASK, 0.25) && own.getAlphaMode() == AlphaMode.MASK
			&& own.getAlphaCutoff() == 0.25, "its alpha mode");
		expect(own.setDoubleSided(true) && own.isDoubleSided(), "double sided");
		expect(own.setInt("base_color_texture_texcoord", 1) && own.getInt("base_color_texture_texcoord") == 1, "an int");
		expect(own.setFloat("roughness", 0.5) && own.getFloat("roughness") == 0.5, "a float");
		expect(own.setVec2("normal_texture_scale", 2, 3) && own.getVec2("normal_texture_scale").y == 3, "a vec2");
		expect(own.setVec3("emissive", 1, 2, 3) && own.getVec3("emissive").z == 3, "a vec3");
		expect(own.setVec4("base_color", 1, 0, 0, 1) && own.getVec4("base_color").x == 1, "a vec4");
		expect(own.setColor("base_color", Color.get(ColorStock.WHITE)) && own.getVec4("base_color").w == 1, "a color");
		expect(own.setTexture("base_color_texture", tiles) && own.getTexture("base_color_texture") == tiles, "a texture");
		expect(own.setTextureSampling("base_color_texture", TextureWrap.CLAMP, TextureWrap.MIRROR, TextureFilter.NEAREST)
			&& own.getTextureWrapU("base_color_texture") == TextureWrap.CLAMP
			&& own.getTextureWrapV("base_color_texture") == TextureWrap.MIRROR
			&& own.getTextureFilter("base_color_texture") == TextureFilter.NEAREST, "its sampling");
		expect(Resource.release(own), "a material let go of");
		// a custom shader's material (wgf_shader.h): its values kept by the shader's names while it loads
		final shader = Shader.create("shaders/toon.wgfshader");
		final toon = Material.createCustom(shader);
		expect(!shader.isNone() && toon.getShader() == shader && toon.getShading() == MaterialShading.CUSTOM
			&& Resource.release(shader), "a custom material, holding its shader");
		expect(toon.setFloat("bands", 3) && toon.getFloat("bands") == 3, "a custom material's value");
		expect(Resource.release(toon), "a custom material let go of");
		// a cubemap (wgf_cubemap.h): six faces from one image in a layout or six files, sampled by a custom shader
		final skybox = Cubemap.create("textures/skybox.png", CubemapLayout.AUTO);
		final faces = Cubemap.createFaces("textures/particle.png", "textures/particle.png", "textures/particle.png",
			"textures/particle.png", "textures/particle.png", "textures/particle.png");
		expect(!skybox.isNone() && skybox.getSize() == 0 && faces.getLayout() == CubemapLayout.FACES,
			"cubemaps, loading");
		final skyShader = Shader.create("shaders/skybox.wgfshader");
		final skyMaterial = Material.createCustom(skyShader);
		Resource.release(skyShader);
		expect(skyMaterial.setCubemap("sky", skybox) && skyMaterial.getCubemap("sky") == skybox
			&& !skyMaterial.setCubemap("sky", cast tiles), "a custom material's cubemap, and a texture refused");
		expect(Resource.release(skyMaterial) && Resource.release(skybox) && Resource.release(faces), "cubemaps let go of");
		// a screen effect (wgf_render_add_effect): a custom material over the finished frame
		final vignette = Shader.create("shaders/vignette.wgfshader");
		final effect = Material.createCustom(vignette);
		final unlit = Material.create(MaterialShading.UNLIT);
		Resource.release(vignette);
		expect(!Render.addEffect(unlit) && Render.addEffect(effect) && Render.getEffectCount() == 1
			&& Resource.release(unlit), "an effect, and a built-in material refused");
		Render.clearEffects();
		expect(Render.getEffectCount() == 0 && Resource.release(effect), "the effects cleared");
	}

	/** The 3D camera, look_at, and immediate mode in 3D. **/
	static function threeD():Void {
		if (!camera3dMade) {
			camera3dMade = true;
			camera3d = Camera3d.create();
			expect(camera3d.setFov(0.8) && Math.abs(camera3d.getFov() - 0.8) < 1e-4, "a 3D camera's field of view");
			expect(!camera3d.setClip(2, 1) && camera3d.setClip(0.5, 200) && camera3d.getNear() == 0.5
				&& camera3d.getFar() == 200, "its clip planes");
			expect(camera3d.setOrthoHeight(6) && camera3d.getOrthoHeight() == 6 && camera3d.setOrthographic(true)
				&& camera3d.isOrthographic() && camera3d.setOrthographic(false), "orthographic");
			camera3d.setPosition(6, 4, 6);
			final onScreen = camera3d.worldToScreen(0, 0, 0);
			camera3d.screenToOrigin(onScreen.x, onScreen.y);
			camera3d.screenToDirection(onScreen.x, onScreen.y);
			expect(camera3d.lookAt(0, 0, 0, 0, 1, 0) && !camera3d.lookAt(6, 4, 6, 0, 1, 0), "aimed with lookAt");
		}
		if (frames == 2) meshes();
		if (frames == 2) makeStage();
		if (frames >= 2) stage3d.draw();
		if (frames == 2) renderTarget();
		expect(Draw.begin3d(camera3d), "3D immediate mode");
		Draw.grid(4, 1, Color.get(ColorStock.GRAY));
		Draw.line3d(0, 0, 0, 1, 1, 1, Color.get(ColorStock.RED));
		Draw.cube(0, 0.5, 0, 1, 1, 1, Color.get(ColorStock.SKYBLUE));
		Draw.cubeWires(0, 0.5, 0, 1, 1, 1, Color.get(ColorStock.DARKBLUE));
		{ // a skinned model as it stands, once its file is READY; refused before
			final ready = Resource.getStatus(robot.getMesh()) == ResourceStatus.READY;
			final drawn = Draw.modelWires(robot, Color.get(ColorStock.LIME))
				&& Draw.modelPoints(robot, Color.get(ColorStock.YELLOW))
				&& Draw.modelSkeleton(robot, Color.get(ColorStock.MAGENTA));
			expect(drawn == ready, "a model's wires, points, and skeleton, drawn once it is READY");
			expect(!Draw.modelWires(camera3d, Color.get(ColorStock.LIME)), "a camera's wires refused: not a model");
		}
		Draw.sphere(2, 0.5, 0, 0.5, Color.get(ColorStock.GOLD));
		Draw.triangle3d(0, 0, 0, 1, 0, 0, 0, 1, 0, Color.get(ColorStock.GOLD));
		Draw.cylinder(-2, 0.5, 0, 0.3, 1, Color.get(ColorStock.GOLD));
		Draw.cylinderWires(-2, 0.5, 1, 0.3, 1, Color.get(ColorStock.GOLD));
		Draw.sphereWires(2, 0.5, 1, 0.5, Color.get(ColorStock.GOLD));
		Draw.capsuleWires(0, 0.5, 2, 0.3, 1, Color.get(ColorStock.GOLD));
		Draw.rectangle3d(-2, 0.5, 0, 1, 1, 0, 0.5, 0, Color.get(ColorStock.GREEN));
		Draw.circle3d(0, 0.01, 0, 2, Math.PI / 2, 0, 0, Color.get(ColorStock.WHITE));
		Draw.text3d(font, "3D", 0, 2, 0, 0.5, Color.get(ColorStock.WHITE));
		Draw.end3d();
	}

	static function gfx():Void {
		Render.setClearColor(Color.make(10, 12, 20, 255));
		expect(Render.getClearColor() == Color.make(10, 12, 20, 255), "the clear color");
		Render.getWidth();
		Render.getHeight();
		expect(Presentation.set(PresentationMode.EXPAND, 320, 180) && Presentation.getMode() == PresentationMode.EXPAND
			&& Presentation.getWidth() == 320 && Presentation.getHeight() == 180, "a presentation");
		expect(Presentation.getVisible().z >= 320 && Presentation.getScale() > 0, "what it shows, and its scale");
		expect(!Presentation.set(PresentationMode.FIT, 0, 180) && Presentation.set(PresentationMode.NONE, 0, 0),
			"a design under 1 refused; none again");
		Presentation.setBarColor(Color.make(1, 2, 3, 255));
		expect(Presentation.getBarColor() == Color.make(1, 2, 3, 255), "the bars' color");
		expect(Render.getDpiScale() >= 1, "the dpi scale");
		final white = Color.get(ColorStock.WHITE);
		expect(Color.getRed(white) == 255 && Color.getGreen(white) == 255 && Color.getBlue(white) == 255
			&& Color.getAlpha(Color.withAlpha(white, 9)) == 9, "colors");
		expect(Color.getRed(Color.makeFloat(1, 0, 0, 1)) == 255 && Color.getRed(Color.lerp(0xFF0000FF, 0x000000FF, 0.5)) > 100,
			"colors from floats, and between two");
		tiles = Texture.create("textures/tiles.png");
		font = Font.create("fonts/JetBrainsMono/JetBrainsMono-Regular.ttf");
		expect(Font.getDefault().isNone() && font.setDefault() && Font.getDefault() == font, "the default font");
		expect(font.measure("feature", 16).x > 0, "text measured");
		pixelFont = Font.create("fonts/gizmo5/gizmo5.fnt");
		expect(pixelFont.getFilter() == TextureFilter.NEAREST && pixelFont.setFilter(TextureFilter.LINEAR)
			&& pixelFont.getFilter() == TextureFilter.LINEAR && pixelFont.setFilter(TextureFilter.NEAREST)
			&& !font.setFilter(TextureFilter.LINEAR), "a bitmap font's filter; a TrueType font has none");
		expect(font.getSize() == 0, "a TrueType font has no size of its own");
		expect(Ui.setDocumentFont("Mono", font) && Ui.getDocumentFont("mono") == font, "a document's font family");
		expect(Ui.setDocumentFontFace("Mono", 700, false, font) && Ui.getDocumentFontFace("mono", 700, false) == font
			&& Ui.setDocumentFontFace("Mono", 700, false, 0) && Ui.getDocumentFontFace("mono", 700, false).isNone(),
			"a family's bold face, set and unset");
		menu = Document.create("ui/options.rml");
		expect(menu.setVisible(true) && menu.isVisible(), "a document, shown once loaded");

		world = Stage2d.create();
		hud = Stage2d.create();
		camera = Camera2d.create();
		expect(camera.setZoom(1.5) && near(camera.getZoom(), 1.5), "a camera's zoom");
		expect(world.setCamera(camera) && (world.getCamera() : Actor) == camera, "a 2D stage's camera");
		final shown = world.worldToScreen(10, 20), under = world.screenToWorld(shown.x, shown.y);
		expect(near(under.x, 10) && near(under.y, 20), "a 2D stage's point on the screen and back");
		final follower:Follow = (Camera2d.create() : Actor), followed = Actor.create();
		expect(follower.setTarget(followed) && follower.getTarget() == followed, "a camera following");
		expect(follower.setSmoothing(4) && near(follower.getSmoothing(), 4) && follower.setDeadZone(10, 8)
			&& near(follower.getDeadZone().x, 10) && follower.setBounds(0, 0, 100, 100) && near(follower.getBounds().z, 100),
			"its smoothing, dead zone, and bounds");
		expect(follower.setOffset(0, 2, 5) && near(follower.getOffset().z, 5) && follower.setMode(FollowMode.ORBIT)
			&& follower.getMode() == FollowMode.ORBIT && follower.setOrbit(0.5, 0.2, 7) && near(follower.getOrbit().z, 7),
			"its offset, mode, and orbit");
		expect(World.setListener(followed) && World.getListener() == followed && World.setListener(cast 0), "a listener actor");
		expect(follower.shake(0.5) && near(follower.getTrauma(), 0.5) && follower.snap() && follower.setTarget(cast 0),
			"shaken, snapped, and let go");
		final flier:Fly = (Camera3d.create() : Actor);
		expect(flier.getMode() == FlyMode.NONE && flier.setSpeed(3) && near(flier.getSpeed(), 3) && flier.setSensitivity(0.002)
			&& near(flier.getSensitivity(), 0.002) && flier.setTurnSpeed(1.5) && near(flier.getTurnSpeed(), 1.5),
			"a flown camera's speed, sensitivity, and turn speed");
		expect(flier.setLook(0.5, 2) && near(flier.getLook().x, 0.5) && near(flier.getLook().y, 1.5533430)
			&& flier.setLookHeld(true) && flier.isLookHeld() && !flier.setSpeed(-1),
			"its look, the pitch clamped; looking while held; a negative speed refused");
		expect(flier.setMode(FlyMode.FREE) && flier.getMode() == FlyMode.FREE && flier.setMode(FlyMode.FIRST_PERSON)
			&& flier.setMode(FlyMode.NONE) && flier.getMode() == FlyMode.NONE && !((follower : Actor) : Fly).setMode(FlyMode.FREE),
			"flown free, first person, and stopped; a 2D camera refused");
		(flier : Actor).destroy(ActorDestroy.DESTROY_CHILDREN);

		final group = Actor.create();
		expect(group.setParent(world) && group.getParent() == world && world.getChildCount() == 1
			&& world.getChild(0) == group, "an actor on a 2D stage");
		expect(group.setName("group") && group.getName() == "group" && world.find("group") == group
			&& (world : Actor).find("group") == group, "named: found on its stage, and by its path");
		expect(group.setTransform(5, 6, 0, 0, 0, 0.5, 2, 2, 1), "a transform at once");
		expect(group.setPosition(10, 20, 0) && near(group.getPosition().x, 10), "a position");
		expect(group.setRotation(0, 0, 1) && near(group.getRotation().z, 1), "a rotation");
		expect(group.setScale(2, 2, 1) && near(group.getScale().x, 2), "a scale");
		final at = group.getWorldPosition(), heading = group.getWorldDirection(1, 0, 0);
		expect(near(group.getDrawnPosition().x, at.x) && near(group.getDrawnDirection(1, 0, 0).y, heading.y)
			&& near(heading.x * heading.x + heading.y * heading.y, 1), "where it is, drawn there, and its heading");
		expect(group.setEnabled(true) && group.isEnabled() && group.setVisible(true) && group.isVisible(), "on and shown");
		expect(group.setPaused(true) && group.isPaused() && group.isFrozen() && group.setPaused(false) && !group.isFrozen(),
			"paused and resumed");
		expect(group.getKind() == ActorKind.PLAIN && group.setIndex(0) && group.getIndex() == 0, "its type and index");

		final shape = Shape2d.create();
		shape.setParent(group);
		expect(shape.setRectangle(20, 10) && shape.getKind() == Shape2dKind.RECTANGLE && near(shape.getSize().x, 20), "a rectangle");
		expect(shape.setCircle(4) && near(shape.getRadius(), 4), "a circle");
		expect(shape.setLine(0, 0, 3, 4) && near(shape.getLineStart().x, 0) && near(shape.getLineEnd().y, 4), "a line");
		expect(shape.setPolygon([0.0, 0, 10, 0, 5, 8]) && shape.getPointCount() == 3, "a polygon");
		final points = [for (_ in 0...6) 0.0];
		expect(shape.getPoints(points) == 6 && near(points[2], 10), "its points");
		expect(shape.setPivot(0.5, 0.5) && near(shape.getPivot().y, 0.5), "a pivot");
		expect(shape.setOutline(2) && near(shape.getOutline(), 2), "an outline");
		expect(shape.setCornerRadius(3) && near(shape.getCornerRadius(), 3) && shape.setCornerRadius(0), "a corner radius");
		expect(shape.setColor(white) && shape.getColor() == white, "its color");

		final sprite = Sprite.create(tiles);
		sprite.setParent(group);
		expect(sprite.setTexture(tiles) && sprite.getTexture() == tiles, "a sprite's texture");
		expect(sprite.setSource(0, 0, 16, 16) && near(sprite.getSource().z, 16), "its source");
		expect(sprite.setSize(32, 32) && near(sprite.getSize().y, 32), "its size");
		expect(sprite.setPivot(0.5, 0.5) && near(sprite.getPivot().x, 0.5), "its pivot");
		expect(sprite.setTint(white) && sprite.getTint() == white, "its tint");
		expect(sprite.setAlphaMode(AlphaMode.MASK, 0.25) && sprite.getAlphaMode() == AlphaMode.MASK
			&& near(sprite.getAlphaCutoff(), 0.25) && sprite.setAlphaMode(AlphaMode.BLEND, 0.5), "its alpha mode");
		expect(sprite.setPickAlpha(0.5) && near(sprite.getPickAlpha(), 0.5) && !sprite.setPickAlpha(2) && sprite.setPickAlpha(0),
			"its pick alpha");
		expect(sprite.setPickable(false) && !sprite.isPickable() && sprite.setPickable(true) && sprite.isPickable(), "pickable");
		expect(sprite.setNineSlice(4, 4, 4, 4) && sprite.getNineSlice().x == 4 && sprite.setNineSlice(0, 0, 0, 0), "a nine-slice");
		expect(sprite.setFacing(SpriteFacing.CAMERA_FIXED_Y) && sprite.getFacing() == SpriteFacing.CAMERA_FIXED_Y
			&& sprite.setFacing(SpriteFacing.CAMERA), "its facing on a 3D stage");
		final sheet = Sheet.createGrid(tiles, 16, 16, 0, 0, 0);
		expect(sheet.getTexture() == tiles && sheet.addClip("spin", [0, 1, 2], 0.1, SheetDirection.PING_PONG)
			&& sheet.getClipCount() == 1, "a sprite sheet and a clip");
		sheet.getFrameCount();
		sheet.getFrame(0);
		final flipbook = Sprite.create(tiles);
		expect(flipbook.setSheet(sheet) && flipbook.getSheet() == sheet && flipbook.playClip("spin", true)
			&& flipbook.getClip() == "spin" && flipbook.getClipFrame() == 0 && flipbook.getClipState() == PlayState.PLAYING,
			"a sprite playing it");
		expect(flipbook.setClipSpeed(2) && near(flipbook.getClipSpeed(), 2) && flipbook.pauseClip() && flipbook.resumeClip()
			&& flipbook.stopClip(), "its speed, paused, resumed, stopped");
		flipbook.destroy(ActorDestroy.DESTROY_CHILDREN);
		sheet.destroy();
		gems = Sheet.create("sheets/gem.json");
		expect(Resource.getStatus(gems) == ResourceStatus.PENDING, "an atlas loading");
		island = Tilemap.create("maps/island.tmj");
		expect(Resource.getStatus(island) == ResourceStatus.PENDING && island.getColumns() == 0, "a Tiled map loading");
		final quads = SpriteBatch.create(tiles);
		quads.setParent(group);
		final floats:Array<Float> = [0, 0, 0, 0, 0, 0, 0, 0];
		final ints:Array<Int> = [0, 0];
		expect(quads.getKind() == ActorKind.SPRITE_BATCH && quads.setTexture(tiles) && quads.getTexture() == tiles,
			"a sprite batch's texture");
		expect(quads.setCount(2) && quads.getCount() == 2, "its count");
		expect(quads.setPositions(0, [40, 200, 60, 200]) == 2 && quads.getPositions(0, floats) == 4 && near(floats[2], 60),
			"its positions");
		expect(quads.setSizes(0, [16, 16, 8, 8]) == 2 && quads.getSizes(1, floats) == 2 && near(floats[0], 8), "its sizes");
		expect(quads.setRotations(0, [0, 0.5]) == 2 && quads.getRotations(1, floats) == 1 && near(floats[0], 0.5),
			"its rotations");
		expect(quads.setRegions(0, [0, 0, 16, 16]) == 1 && quads.getRegions(0, floats) == 8 && near(floats[2], 16),
			"its regions");
		expect(quads.setTints(0, [cast white, cast white]) == 2 && quads.getTints(0, ints) == 2 && ints[1] == cast white,
			"its tints");
		expect(quads.setPivot(0, 0) && near(quads.getPivot().y, 0), "its pivot");
		expect(quads.setAlphaMode(AlphaMode.ADD, 0.5) && quads.getAlphaMode() == AlphaMode.ADD
			&& near(quads.getAlphaCutoff(), 0.5), "its alpha mode");

		final label = Text.create(font);
		expect(label.setSpacing(1, 4) && label.getLetterSpacing() == 1 && label.getLineSpacing() == 4 && label.setSpacing(0, 0),
			"a text's letter and line spacing");
		Draw.setTextSpacing(0, 0);
		expect(Draw.getTextSpacing().x == 0, "immediate mode's");
		label.setParent(hud);
		expect(label.setFont(font) && label.getFont() == font, "a text's font");
		expect(label.setString("feature-test") && label.getString() == "feature-test", "its string");
		expect(label.setFontSize(18) && near(label.getFontSize(), 18), "its size");
		expect(label.setColor(white) && label.getColor() == white, "its color");
		expect(label.setWrapWidth(200) && near(label.getWrapWidth(), 200), "its wrap");
		expect(label.setAlign(TextHalign.CENTER, TextValign.MIDDLE) && label.getHalign() == TextHalign.CENTER
			&& label.getValign() == TextValign.MIDDLE, "its alignment");

		final emitter = Emitter2d.create();
		emitter.setParent(world);
		expect(emitter.setTexture(tiles) && emitter.getTexture() == tiles && emitter.setTexture(cast 0), "an emitter2d's texture, and none");
		expect(emitter.setSource(0, 0, 16, 16) && near(emitter.getSource().z, 16), "its source");
		expect(emitter.setFrames(2, 2, 3, 12) && emitter.getFrameColumns() == 2 && emitter.getFrameRows() == 2 && emitter.getFrameCount() == 3
			&& near(emitter.getFrameRate(), 12), "its flipbook");
		expect(emitter.skipTrail(), "its trail skipped");
		expect(emitter.setRate(30) && near(emitter.getRate(), 30), "its rate");
		expect(emitter.setEmitting(true) && emitter.isEmitting(), "emitting");
		expect(emitter.setMax(64) && emitter.getMax() == 64, "its max");
		expect(emitter.setLife(0.2, 0.5) && near(emitter.getLifeMin(), 0.2) && near(emitter.getLifeMax(), 0.5), "its life");
		expect(emitter.setSpawnBox(2, 3) && near(emitter.getSpawnBox().y, 3), "its spawn box");
		expect(emitter.setSpawnCircle(4) && near(emitter.getSpawnCircle(), 4), "its spawn circle");
		expect(emitter.setVelocity(10, 0, 0.5, 0.25) && near(emitter.getVelocity().x, 10) && near(emitter.getSpread(), 0.5)
			&& near(emitter.getSpeedVariance(), 0.25), "its velocity");
		expect(emitter.setGravity(0, 10) && near(emitter.getGravity().y, 10), "its gravity");
		expect(emitter.setDrag(0.5) && near(emitter.getDrag(), 0.5), "its drag");
		expect(emitter.setInheritVelocity(0.5) && near(emitter.getInheritVelocity(), 0.5), "its inherited velocity");
		expect(emitter.setStretch(0.1) && near(emitter.getStretch(), 0.1), "its stretch");
		expect(emitter.setSpawnShell(true) && emitter.isSpawnShell() && emitter.setSpawnShell(false), "its spawn shell");
		expect(emitter.setAlignedToMotion(true) && emitter.isAlignedToMotion(), "aligned to its motion");
		expect(emitter.setWander(0.5, 2) && near(emitter.getWanderDistance(), 0.5) && near(emitter.getWanderRate(), 2)
			&& !emitter.setWander(-1, 1), "its wander");
		expect(emitter.setOrbit(1, 3) && near(emitter.getOrbitMin(), 1) && near(emitter.getOrbitMax(), 3), "its orbit");
		expect(emitter.setPulse(2, 1.5) && near(emitter.getPulseRate(), 2) && near(emitter.getPulseDepth(), 1), "its pulse, its depth held to 1");
		expect(emitter.setLocalSpace(true) && emitter.isLocalSpace() && emitter.setLocalSpace(false), "local space");
		expect(emitter.setSize(4, 1, 0.5) && near(emitter.getSizeKeyValue(0), 4) && near(emitter.getSizeVariance(), 0.5), "its sizes");
		expect(emitter.clearSizeKeys() && emitter.addSizeKey(0.5, 2) && emitter.getSizeKeyCount() == 1
			&& near(emitter.getSizeKeyTime(0), 0.5), "a size key");
		expect(emitter.setColor(white, 0) && emitter.getColorKeyValue(0) == white, "its colors");
		expect(emitter.clearColorKeys() && emitter.addColorKey(0.5, white) && emitter.getColorKeyCount() == 1
			&& near(emitter.getColorKeyTime(0), 0.5), "a color key");
		expect(emitter.addPaletteColor(white) && emitter.getPaletteCount() == 1 && emitter.getPaletteColor(0) == white
			&& emitter.clearPalette(), "its palette");
		expect(emitter.setSpin(1, 2) && near(emitter.getSpinMin(), 1) && near(emitter.getSpinMax(), 2), "its spin");
		expect(emitter.setAlphaMode(AlphaMode.BLEND, 0.25) && emitter.getAlphaMode() == AlphaMode.BLEND
			&& near(emitter.getAlphaCutoff(), 0.25), "its alpha");
		expect(emitter.setSeed(42) && emitter.getSeed() == 42, "its seed");
		expect(emitter.setEnding(false) && !emitter.isEnding() && emitter.setLingering(true) && emitter.isLingering(), "ending, lingering");
		expect(emitter.prewarm(0.5) && emitter.burst(8) && emitter.getCount() >= 8, "prewarmed, and a burst");
		expect(emitter.clear() && emitter.getCount() == 0, "its particles cleared");
		emitter.burst(8);

		final gone = Actor.create();
		gone.destroy(ActorDestroy.DESTROY_CHILDREN);
		expect(gone.getKind() == ActorKind.NONE, "an actor destroyed");
		expect(tiles.setSampling(TextureWrap.REPEAT, TextureWrap.CLAMP, TextureFilter.NEAREST)
			&& tiles.getWrapU() == TextureWrap.REPEAT && tiles.getWrapV() == TextureWrap.CLAMP
			&& tiles.getFilter() == TextureFilter.NEAREST, "a texture's sampling");
	}

	static function screens():Void {
		Behavior.register("Title", Title.new);
		final title = Screen.create(), play = Screen.create(), menu = Screen.create(), loading = Screen.create();
		final playStage = Stage2d.create();
		expect(!title.isNone() && Screen.addStage(play, playStage) && Screen.getStageCount(play) == 1
			&& Screen.getStage(play, 0) == playStage, "screens, one with a stage");
		title.addBehavior("Title");
		expect(Screen.setTransition(Transition.NONE, 0) && Screen.getTransition() == Transition.NONE
			&& Screen.getTransitionSeconds() == 0, "changes at once");
		Screen.setTransitionColor(Color.make(0, 0, 0, 255));
		expect(Screen.getTransitionColor() == Color.make(0, 0, 0, 255) && Screen.setTransitionEase(Easing.QUAD_OUT)
			&& Screen.getTransitionEase() == Easing.QUAD_OUT, "the transition's color and ease");
		expect(Screen.push(title) && Screen.getTop() == title && Screen.getCount() == 1 && Screen.get(0) == title, "a title pushed");
		expect(Screen.replace(play) && Screen.getTop() == play, "replaced by play");
		expect(Screen.setOpaque(menu, false) && !Screen.isOpaque(menu) && Screen.setPausing(menu, true) && Screen.isPausing(menu),
			"a pause menu over it");
		expect(Screen.push(menu) && playStage.isFrozen() && Screen.pop() && !playStage.isFrozen(), "pushed, play paused; popped");
		expect(Screen.setLoadingScreen(loading) && Screen.getLoadingScreen() == loading && Screen.getLoadProgress() == 1
			&& !Screen.isChanging() && Screen.getChangeProgress() == 0, "a loading screen; nothing changing");
		expect(!Screen.isWaitLoads(play) && Screen.setWaitLoads(play, true) && Screen.isWaitLoads(play)
			&& Screen.setWaitLoads(play, false), "waiting for every load, or not");
		expect(Screen.setAssets(play, cast 0) && Screen.getAssets(play).isNone() && Screen.removeStage(play, playStage),
			"no assets; a stage removed");
		Screen.draw();
		Screen.setLoadingScreen(cast 0);
	}

	static function ecs():Void {
		screens();
		Behavior.register("Rock", Rock.new);
		scene = Scene.create("scenes/field.scene");
		final ship:Actor = Shape2d.create();
		ship.setParent(world);
		expect(ship.setName("probe ship") && world.find("probe ship") == ship, "named");
		expect(ship.setTransform(100, 100, 0, 0, 0, 0, 1, 1, 1) && ship.snap(), "a transform, snapped");
		final out = [0.0, 0, 0];
		expect(Actor.setPositions([ship], [130.0, 100, 0]) && Actor.getPositions([ship], out) == 3 && near(out[0], 130),
			"positions in bulk");
		for (c in [Component.MOTION, Component.BOUNDS, Component.LIFETIME, Component.COLLIDER, Component.VOICE])
			expect(ship.addComponent(c) && ship.hasComponent(c), 'a component: $c');
		expect(Actor.getCount() >= 1 && !ship.getVoice().isNone(), "counted, and its voice");
		final motion:Motion = ship;
		expect(motion.setVelocity(1, 0, 0) && near(motion.getVelocity().x, 1), "a velocity");
		expect(motion.setSpin(0, 0, 1) && near(motion.getSpin().z, 1), "a spin");
		expect(motion.setDamping(0.1) && near(motion.getDamping(), 0.1), "damping");
		expect(motion.setMaxSpeed(50) && near(motion.getMaxSpeed(), 50), "a top speed");
		final bounds:Bounds = ship;
		expect(bounds.setRect(0, 0, 640, 360) && near(bounds.getRect().z, 640), "bounds");
		expect(bounds.setMode(BoundsMode.WRAP) && bounds.getMode() == BoundsMode.WRAP, "wrapping");
		expect(bounds.setMargin(10) && near(bounds.getMargin(), 10), "a margin");
		expect(bounds.setVisible(true) && bounds.isVisible(), "bounds the visible area");
		expect(bounds.setRect(0, 0, 640, 360) && !bounds.isVisible(), "until a rectangle is set");
		final life:Lifetime = ship;
		expect(life.setSeconds(100) && near(life.getSeconds(), 100), "a lifetime");
		final collider:Collider = ship;
		expect(collider.setRadius(8) && near(collider.getRadius(), 8), "a collider");
		expect(collider.setLayer(2) && collider.getLayer() == 2 && collider.setMask(1) && collider.getMask() == 1, "its layers");
		collider.getOverlaps([ship]);
		expect(collider.setRectangle(16, 8) && collider.getShape() == ColliderShape.RECTANGLE && near(collider.getSize().x, 16),
			"a rectangle collider");
		expect(collider.setPolygon([0, 0, 8, 0, 8, 8]) && collider.getShape() == ColliderShape.POLYGON
			&& collider.getPoints([0, 0, 0, 0, 0, 0]) == 6, "a polygon collider");
		expect(collider.setSegment(0, 0, 0, 8) && collider.getShape() == ColliderShape.SEGMENT, "a segment collider");
		expect(collider.setOffset(1, 2) && near(collider.getOffset().y, 2) && collider.setOffset(0, 0), "an offset");
		expect(collider.getPush(ship).x == 0 && collider.setRadius(8), "no push from itself; a circle again");
		{
			final hero = Actor.create();
			expect(hero.addComponent(Component.BODY2D), "a 2D body");
			final body:Body2d = hero;
			expect(body.setSize(12, 20) && near(body.getSize().y, 20) && body.setVelocity(3, 0) && near(body.getVelocity().x, 3),
				"its size and velocity");
			expect(body.setGravity(900) && near(body.getGravity(), 900) && body.setMaxFallSpeed(600)
				&& near(body.getMaxFallSpeed(), 600) && body.setSolidMask(1) && body.getSolidMask() == 1, "its gravity, fall, mask");
			expect(!body.isGrounded() && !body.isBlockedLeft() && !body.isBlockedRight() && !body.isBlockedAbove()
				&& body.getAirTime() == 0, "touching nothing yet");
			final ground:Body2d = Actor.create();
			expect(ground.setGrid([0, 1, 1, 1], 2, 16, 0, 0) && ground.getGridCell(1, 0) == 1 && ground.setGridCell(0, 0, 1)
				&& ground.clearGrid(), "a grid of solid cells");
			(ground : Actor).destroy(ActorDestroy.DESTROY_CHILDREN);
			hero.destroy(ActorDestroy.DESTROY_CHILDREN);
		}
		expect(collider.isEnabled() && collider.setEnabled(false) && !collider.isEnabled() && collider.setEnabled(true),
			"a collider switched off and on");
		final id = ship.addBehavior("Probe");
		final second = ship.addBehavior("Probe");
		expect(id == 1 && second == 2 && ship.getBehaviorCount() == 2 && ship.getBehavior(1) == second
			&& ship.findBehavior("Probe") == id, "two behaviors of a name");
		final behavior:BehaviorComponent = ship;
		expect(behavior.getName(id) == "Probe", "a behavior's name");
		expect(behavior.setParam(id, "speed", "3.5") && behavior.hasParam(id, "speed") && behavior.getParam(id, "speed") == "3.5"
			&& near(behavior.getParamNumber(id, "speed"), 3.5) && !behavior.hasParam(second, "speed"), "a parameter, its own");
		expect(behavior.getParamCount(id) == 1 && behavior.getParamKey(id, 0) == "speed", "the parameters listed");
		expect(behavior.setParam(second, "self", "@.") && behavior.getParamActor(second, "self") == ship
			&& behavior.getParamActor(id, "speed").isNone(), "a parameter referring to an actor");
		expect(ship.removeBehavior(second) && ship.getBehaviorCount() == 1, "a behavior removed");
		expect(ship.removeComponent(Component.LIFETIME) && !ship.hasComponent(Component.LIFETIME), "a component removed");
		expect(Actor.countWithBehavior("Probe") == 1 && Actor.findWithBehavior("Probe", [0]) == 1, "behaviors found");
		final found = [ship];
		expect(Actor.countWithComponent(Component.COLLIDER) >= 1 && Actor.findWithComponent(Component.COLLIDER, found) == 1,
			"components found");
		World.getEventCount();
		final doomed = Actor.create();
		doomed.addComponent(Component.LIFETIME);
		doomed.destroy(ActorDestroy.DESTROY_CHILDREN);
		expect(doomed.getKind() == ActorKind.NONE && !doomed.hasComponent(Component.LIFETIME), "an actor destroyed");
	}

	static function physics():Void {
		expect(Physics.setGravity(0, -9.81, 0) && near(Physics.getGravity().y, -9.81), "physics started, its gravity");
		final floor = Actor.create();
		floor.setParent(stage3d);
		expect(floor.addComponent(Component.BODY) && floor.hasComponent(Component.BODY), "a body");
		final body:Body = floor;
		expect(body.setType(BodyType.STATIC) && body.getType() == BodyType.STATIC, "static");
		expect(body.setShape(BodyShape.BOX, 50, 1, 50) && body.getShape() == BodyShape.BOX && near(body.getSize().x, 50),
			"a box");
		expect(body.setMass(0) && body.getMass() == 0 && body.setFriction(0.8) && near(body.getFriction(), 0.8), "mass, friction");
		expect(body.setBounce(0.1) && near(body.getBounce(), 0.1) && body.setDamping(0.1, 0.2) && near(body.getDamping().y, 0.2),
			"bounce, damping");
		expect(body.setLayer(4) && body.getLayer() == 4 && body.setMask(3) && body.getMask() == 3, "its layer and mask");
		expect(body.setOffset(0, 0.5, 0) && near(body.getOffset().y, 0.5) && body.setMassOffset(0, -0.2, 0)
			&& near(body.getMassOffset().y, -0.2), "its offsets");
		final car = Actor.create();
		car.setParent(stage3d);
		car.setPosition(0, 1, 0);
		car.addComponent(Component.BODY);
		final carBody:Body = car;
		expect(carBody.setVelocity(0, 0, 1) && near(carBody.getVelocity().z, 1) && carBody.setSpin(0, 1, 0)
			&& near(carBody.getSpin().y, 1) && carBody.addImpulse(0, 0, 10), "moving");
		final wheels = [for (i in 0...4) Actor.create()];
		for (i in 0...4) {
			wheels[i].setParent(car);
			wheels[i].setPosition(i % 2 == 0 ? -0.8 : 0.8, -0.2, i < 2 ? 1.4 : -1.4);
		}
		expect(car.addComponent(Component.VEHICLE), "a vehicle");
		final vehicle:Vehicle = car;
		expect(vehicle.setWheels(wheels) && vehicle.getWheelCount() == 4 && vehicle.getWheel(0) == wheels[0], "its wheels");
		expect(vehicle.setWheelSize(0.34, 0.22) && vehicle.setSuspension(0.3, 1.6, 0.5) && vehicle.setSteering(0.5)
			&& vehicle.setGrip(1.2), "its wheels' settings");
		expect(vehicle.setAntiRoll(4000) && near(vehicle.getAntiRoll(), 4000), "its anti-roll bars");
		expect(near(vehicle.getWheelSize().x, 0.34) && near(vehicle.getSuspension().y, 1.6) && near(vehicle.getSteering(), 0.5)
			&& near(vehicle.getGrip(), 1.2), "read back");
		expect(vehicle.setEngine(420, 7000) && vehicle.setGears([3.2, 2.1, 1.5]) && vehicle.setDrive(VehicleDrive.REAR),
			"its drive train");
		expect(near(vehicle.getEngine().x, 420) && vehicle.getGearCount() == 3 && near(vehicle.getGearRatio(1), 2.1)
			&& vehicle.getDrive() == VehicleDrive.REAR, "its drive train read back");
		expect(vehicle.setInput(1, 0, 0, false), "its intent");
		vehicle.getSpeed();
		vehicle.getRpm();
		vehicle.getGear();
		vehicle.getWheelSlip(0);
		expect(vehicle.reset(), "reset");
		final found:Array<Actor> = [for (i in 0...4) cast 0];
		final hits:Array<Float> = [for (i in 0...28) 0];
		Physics.castRay(0, 10, 0, 0, -1, 0, 20, 0x7FFF, found, hits);
		Physics.castSphere(0, 10, 0, 0.5, 0, -1, 0, 20, 0x7FFF, found, hits);
		Physics.overlapSphere(0, 0, 0, 1, 0x7FFF, found);
		Physics.overlapBox(0, 0, 0, 1, 1, 1, 0x7FFF, found);
		final walker = Actor.create();
		walker.setParent(stage3d);
		walker.setPosition(10, 3, 0);
		walker.addComponent(Component.BODY);
		final character:Body = walker;
		expect(character.setType(BodyType.CHARACTER) && character.setShape(BodyShape.CAPSULE, 0.4, 1.8, 0), "a character");
		expect(character.setStepHeight(0.25) && near(character.getStepHeight(), 0.25) && character.setMaxSlope(0.7)
			&& near(character.getMaxSlope(), 0.7), "its step and slope");
		expect(!character.isGrounded() && character.getGroundNormal().y == 0, "not yet standing");
	}

	static var target:Texture;
	static var targetRead:TextureRead;
	static var screenRead:TextureRead;

	/** A render target: drawn into with the stage through another camera, then on the screen. **/
	static function renderTarget():Void {
		target = Texture.createTarget(64, 48);
		expect(!target.isNone() && target.getWidth() == 64, "a render target");
		expect(Render.setTargetClearColor(target, Color.get(ColorStock.DARKGRAY))
			&& Render.getTargetClearColor(target) == Color.get(ColorStock.DARKGRAY), "its clear color");
		expect(!Render.beginTarget(tiles) && Render.beginTarget(target), "a texture that isn't a target refused, a target begun");
		expect(Render.getWidth() == 64 && Render.getHeight() == 48 && Render.getDpiScale() == 1, "inside it, its size");
		stage3d.drawWith(camera3d);
		Render.endTarget();
		Draw.texture(target, 220, 4, 64, 48, Color.get(ColorStock.WHITE));
		targetRead = target.read();
		screenRead = Render.readScreen();
		tilesRead = tiles.read();
		expect(targetRead.getStatus() == TextureReadStatus.PENDING && tilesRead.getStatus() == TextureReadStatus.PENDING,
			"a target read back, PENDING, and a texture of a file");
	}

	/** Images on the CPU: made in code (each generator), a pixel read and set, their bytes, a
		texture made from one, saved as a PNG, and one loaded from a file (checked once READY). **/
	static function images():Void {
		final red = Color.get(ColorStock.RED), blue = Color.get(ColorStock.BLUE), green = Color.get(ColorStock.GREEN);
		final black = Color.get(ColorStock.BLACK), white = Color.get(ColorStock.WHITE);
		checks = Image.createChecked(8, 4, 2, 2, red, blue);
		expect(!checks.isNone() && Resource.getStatus(checks) == ResourceStatus.READY && (checks : Handle).getKindName() == "gfx.image",
			"an image made in code: READY at once");
		expect(checks.getWidth() == 8 && checks.getHeight() == 4 && checks.getSize() == 128 && checks.getData().length == 128,
			"its size, and its bytes");
		expect(checks.getPixel(0, 0) == red && checks.getPixel(2, 0) == blue && checks.getPixel(2, 2) == red, "checks");
		expect(checks.setPixel(1, 1, green) && checks.getPixel(1, 1) == green && !checks.setPixel(8, 0, green)
			&& checks.getPixel(-1, 0) == 0, "a pixel set and read; outside it refused");
		final ramp = Image.createGradientLinear(4, 2, 0, black, white);
		expect(ramp.getPixel(0, 1) == black && ramp.getPixel(3, 0) == white, "a linear gradient's ends");
		final radial = Image.createGradientRadial(5, 5, 0, white, black), square = Image.createGradientSquare(5, 5, 0, white, black);
		expect(radial.getPixel(2, 2) == white && radial.getPixel(0, 0) == black && square.getPixel(2, 2) == white
			&& square.getPixel(4, 2) == black, "radial and square gradients: the middle inner, the edges outer");
		final flat = Image.createColor(2, 2, green);
		expect(flat.getPixel(1, 1) == green, "a color");
		final noise = Image.createWhiteNoise(16, 16, 0.5, 7), perlin = Image.createPerlinNoise(16, 16, 0, 0, 2, 7);
		final cells = Image.createCellular(16, 16, 4, 7);
		expect(noise.getData().compare(Image.createWhiteNoise(16, 16, 0.5, 7).getData()) == 0
			&& perlin.getData().compare(Image.createPerlinNoise(16, 16, 0, 0, 2, 7).getData()) == 0
			&& cells.getData().compare(Image.createCellular(16, 16, 4, 7).getData()) == 0,
			"noise: the same for a seed");
		checksTexture = Texture.createFromImage(checks);
		expect(!checksTexture.isNone() && checksTexture.getWidth() == 8 && !checksTexture.writeImage(checks),
			"a texture from an image; written once in its frame");
		final saved = checks.save("feature-image.png");
		expect(!saved.isNone(), "an image saved as a PNG");
		saved.destroy();
		Fs.remove("feature-image.png").destroy();
		picture = Image.create("textures/tiles.png");
		expect(Resource.getStatus(picture) == ResourceStatus.PENDING && picture.getWidth() == 0, "an image from a file, PENDING");
		for (image in [ramp, radial, square, flat, noise, perlin, cells])
			Resource.release(image);
	}

	/** The read, the frame after: DONE with the target's pixels, or FAILED with no GPU (headless). **/
	static function targetReadBack():Void {
		final status = targetRead.getStatus();
		final done = status == TextureReadStatus.DONE;
		final data = targetRead.getData();
		final width = targetRead.getWidth(), height = targetRead.getHeight(), size = targetRead.getSize();
		expect(done || status == TextureReadStatus.FAILED, "read in the next frame");
		expect(done ? width == 64 && height == 48 && size == 64 * 48 * 4 && data.length == size
			: width == 0 && height == 0 && size == 0, "DONE: its pixels, 4 bytes each; FAILED: none");
		expect(targetRead.destroy() && !targetRead.isAlive(), "the read destroyed");
		{ // a file's texture read: DONE where there is a GPU and it was READY in time, and made an image
			final tilesDone = tilesRead.getStatus() == TextureReadStatus.DONE;
			expect(tilesDone || tilesRead.getStatus() == TextureReadStatus.FAILED, "a file's texture read");
			final copy = Image.createFromRead(tilesRead);
			expect(tilesDone ? copy.getWidth() == tilesRead.getWidth() && copy.getSize() == tilesRead.getSize() : copy.isNone(),
				"an image of the read: its pixels; none of a FAILED read");
			if (tilesDone)
				Resource.release(copy);
			tilesRead.destroy();
		}
		expect(checksTexture.writeImage(checks) && !checksTexture.writeImage(checks),
			"a texture written with an image the frames after it was made; once a frame");
		final screenDone = screenRead.getStatus() == TextureReadStatus.DONE;
		expect(screenDone || screenRead.getStatus() == TextureReadStatus.FAILED, "the screen read as its frame ended");
		final shot = screenRead.save("feature-shot.png");
		expect(screenDone ? !shot.isNone() : shot.isNone(), "a screenshot saved; none from a FAILED read");
		if (screenDone) {
			shot.destroy();
			Fs.remove("feature-shot.png").destroy();
		}
		screenRead.destroy();
	}

	// ---- the frames ----------------------------------------------------------------------

	/** Picking in 3D and 2D, and the pointer over a stage's actors. **/
	static function picking():Void {
		final hit = [for (_ in 0...8) 0.0];
		final cube = Mesh.createCube(1, 1, 1);
		final box:Model = Model.create(cube);
		Resource.release(cube); // the model holds its own
		box.setParent(stage3d);
		box.setPosition(100, 0, 0);
		expect(stage3d.pickRay(100, 0, 10, 0, 0, -1, hit) == (box : Actor) && near(hit[2], 1) && near(hit[6], 0.5)
			&& near(hit[7], 9.5), "a ray picks the nearest model: its normal, point, and distance");
		expect(box.pickRay(100, 0, 10, 0, 0, -1, hit) && near(hit[6], 0.5), "a model tested alone");
		expect(stage3d.pickRay(100, 10, 10, 0, 0, -1, hit) == 0 && hit[7] == 0, "a ray past it: nothing");
		stage3d.pick(320, 180, hit); // the screen's middle: whatever the stage has there
		box.destroy(ActorDestroy.DESTROY_CHILDREN);

		final target = Shape2d.create();
		target.setRectangle(20, 20);
		target.setPosition(600, 300, 0);
		target.setParent(hud);
		expect(hud.pick(605, 307, hit) == (target : Actor) && near(hit[2], 5) && near(hit[3], 7), "a 2D pick, its own point");
		expect(hud.pick(500, 300, hit) == 0, "a 2D miss");
		final pointer:Pointer = (hud : Actor);
		expect(pointer.setInteractive(true) && pointer.isInteractive(), "the HUD under the pointer");
		Pointer.getHovered();
		Pointer.getHeld();
		final over:Pointer = (target : Actor);
		expect(over.getHoverState() != InputState.PRESSED || Pointer.getHovered() == (target : Actor), "its hover state");
		expect(over.getPressState() == InputState.UP && !over.isClicked(), "no press, no click without input");
		expect(pointer.setInteractive(false) && !pointer.isInteractive(), "and taken off");
		target.destroy(ActorDestroy.DESTROY_CHILDREN);
	}

	static function tick():Void {}

	static function frame():Void {
		frames++;
		Physics.drawBodies(Color.get(ColorStock.YELLOW)); // outside a 3D drawing: nothing, but reached
		world.draw();
		hud.draw();
		Draw.rectangle(4, 4, 20, 10, Color.get(ColorStock.RED));
		Draw.rectangleLines(4, 20, 20, 10, 1, Color.get(ColorStock.GREEN));
		Draw.rectangleRounded(30, 4, 30, 16, 6, 0, Color.get(ColorStock.RED));
		Draw.rectangleRoundedLines(30, 24, 30, 16, 6, 4, 2, Color.get(ColorStock.GREEN));
		Draw.line(0, 0, 30, 30, 2, Color.get(ColorStock.BLUE));
		Draw.circle(50, 50, 5, Color.get(ColorStock.YELLOW));
		Draw.circleLines(70, 50, 5, 1, Color.get(ColorStock.ORANGE));
		Draw.triangle(80, 80, 90, 80, 85, 90, Color.get(ColorStock.PURPLE));
		expect(Draw.polyline([0.0, 100, 20, 110, 40, 100], false, 2, Color.get(ColorStock.WHITE)), "a polyline");
		expect(Draw.spline(SplineKind.CATMULL_ROM, [0.0, 100, 20, 110, 40, 100, 60, 110], 8, 2, Color.get(ColorStock.WHITE)),
			"a spline drawn");
		Draw.textureNineSlice(tiles, 0, 0, 16, 16, 4, 4, 4, 4, 10, 10, 80, 40, Color.get(ColorStock.WHITE));
		Draw.textureTiled(tiles, 0, 0, 16, 16, 10, 60, 50, 20, 1, Color.get(ColorStock.WHITE));
		expect(Draw.texturePolygon(tiles, [0, 0, 10, 0, 0, 10], [0, 0, 1, 0, 0, 1], Color.get(ColorStock.WHITE)), "a textured polygon");
		expect(Draw.setBlendMode(AlphaMode.MULTIPLY) && Draw.getBlendMode() == AlphaMode.MULTIPLY && Draw.setBlendMode(AlphaMode.BLEND),
			"immediate mode's blend mode");
		expect(Render.getDrawCalls() >= 0 && Render.getTriangleCount() >= 0 && Render.getGpuBytes() >= 0, "frame stats");
		expect(!Render.isContextLost() && !Render.isContextRestored(), "the GPU's context neither lost nor given back");
		expect(Draw.polygon([100.0, 100, 120, 100, 110, 115], Color.get(ColorStock.WHITE)), "a polygon drawn");
		Render.pushClip(0, 0, 200, 200);
		Draw.text(font, "feature-test", 4, 120, 14, Color.get(ColorStock.WHITE));
		Draw.text(pixelFont, "pixels", 4, 160, 20, Color.get(ColorStock.WHITE));
		Draw.textAligned(font, "right", 200, 140, 14, Color.get(ColorStock.WHITE), TextHalign.RIGHT, TextValign.MIDDLE);
		Draw.texture(tiles, 140, 4, 32, 32, Color.get(ColorStock.WHITE));
		Draw.textureRegion(tiles, 0, 0, 8, 8, 180, 4, 16, 16, Color.get(ColorStock.WHITE));
		final white = Color.get(ColorStock.WHITE);
		Draw.ring(40, 300, 10, 20, 0, 3, white);
		Draw.arc(90, 300, 15, 0, 3, 2, white);
		Draw.ellipse(140, 300, 20, 10, white);
		Draw.ellipseLines(190, 300, 20, 10, 2, white);
		Draw.rectangleGradient(220, 290, 30, 20, white, white, Color.get(ColorStock.RED), Color.get(ColorStock.RED));
		Draw.circleGradient(280, 300, 12, white, Color.get(ColorStock.BLANK));
		Draw.textureRotated(tiles, 0, 0, 8, 8, 320, 300, 16, 16, 0.5, 0.5, 0.3, white);
		Draw.textureFill(tiles, 350, 290, 32, 8, 0.5, DrawFill.LEFT_TO_RIGHT, white);
		Draw.textureFill(tiles, 390, 290, 16, 16, 0.75, DrawFill.CLOCKWISE, white);
		Render.popClip();
		threeD();
		ui();
		document();
		input();
		replay();
		if (frames == 2) {
			Loop.getFrameDelta();
			Loop.getTickFraction();
			Loop.getFps();
			expect(App.isRunning(), "running");
			expect(!App.enableAutopilot(), "the autopilot enabled only before the run");
			App.isAutopilotEnabled();
			expect(!Debug.isFpsShown(), "the frame-rate overlay off by default");
			Debug.showFps(0, 8, 8, 14, Color.get(ColorStock.LIME));
			expect(Debug.isFpsShown(), "and shown");
			expect(Debug.getFrameStat(FrameStat.FRAMES) == 0, "frames counted from the first ask");
			Debug.startFrameRecording();
			expect(Debug.isFrameRecording(), "the frames recorded");
		}
		if (frames == 3) {
			picking();
			targetReadBack();
			expect(Loop.getFrameCost() > 0, "a frame's cost, measured");
			Debug.hideFps();
			expect(!Debug.isFpsShown(), "the overlay hidden");
			expect(Debug.getFrameStat(FrameStat.FRAMES) >= 1 && Debug.getFrameStat(FrameStat.TIME_P95) > 0, "the last frames' stats");
			expect(Debug.stopFrameRecording() && Debug.getRecordedFrameStat(FrameStat.FRAMES) == 0
				&& Debug.getRecordedFrameTimes([0.0, 0.0], false) == 0, "a recording stopped before its first frame ended");
		}
		waitOnTasks();
		if (!instantiated && Resource.getStatus(scene) == ResourceStatus.READY) {
			instantiated = true;
			expect(scene.getActorCount() == 4 && scene.getPrefabCount() == 2 && scene.getPrefabName(0) == "rock"
				&& scene.hasPrefab("spark"), "the scene file");
			expect(scene.instantiate(world) == 4, "the scene's actors made");
			final spark = scene.prefab("spark");
			expect(!spark.isNone() && spark.isAlive() && !spark.spawn(world).isNone(), "a prefab found once, spawned");
			final placed = spark.spawnAt(world, 10, 20, 0, 0.5);
			expect(!placed.isNone() && near(placed.getPosition().x, 10) && near(placed.getRotation().z, 0.5),
				"a prefab spawned at a place, turned");
			expect(!scene.spawnPrefab("spark", world).isNone()
				&& near(scene.spawnPrefab("spark", world, 5, 6, 0, 0).getPosition().y, 6),
				"spawned by name, at its own transform or placed");
			expect(placed.isAlive() && !(0 : Actor).isAlive() && (placed : Handle).isAlive(), "alive");
			Behavior.tag("Spark");
			expect(World.dump().indexOf("wgf-scene 2") == 0, "the world dumped");
		}
		if (frames >= 30 && instantiated && stage > 3 && menuDone && !reported
			&& Resource.getStatus(robot.getMesh()) != ResourceStatus.PENDING
			&& Resource.getStatus(gems) != ResourceStatus.PENDING && Resource.getStatus(island) != ResourceStatus.PENDING
			&& Resource.getStatus(pixelFont) != ResourceStatus.PENDING && Resource.getStatus(picture) != ResourceStatus.PENDING) {
			expect(Resource.getStatus(picture) == ResourceStatus.READY && picture.getWidth() == tiles.getWidth()
				&& picture.getSize() == picture.getWidth() * picture.getHeight() * 4, "an image from a file READY, at its size");
			expect(pixelFont.getSize() == 10 && pixelFont.measure("AV", 10).x == 11,
				"a bitmap font READY: its own size, A and V kerned a pixel closer");
			finish();
		}
		if (frames > 600 && !reported) {
			expect(false, 'still waiting at frame 600 (scene ${Resource.getStatus(scene)}, storage stage $stage)');
			finish();
		}
	}

	static function ui():Void {
		Ui.resetStyle();
		expect(Ui.setStyleColor(UiColor.FOCUS, Color.get(ColorStock.GOLD)) && Ui.getStyleColor(UiColor.FOCUS) == Color.get(ColorStock.GOLD),
			"a style color");
		expect(Ui.setStyleValue(UiValue.GAP, 10) && near(Ui.getStyleValue(UiValue.GAP), 10), "a style value");
		expect(Ui.setStyleFont(font) && Ui.getStyleFont() == font, "the style's font");
		expect(Ui.setStyleTexture(UiColor.BAR, tiles, 2, 2, 2, 2) && Ui.getStyleTexture(UiColor.BAR) == tiles
			&& !Ui.setStyleTexture(UiColor.TEXT, tiles, 0, 0, 0, 0), "a part drawn from art");
		if (Ui.begin()) {
			Ui.beginPanel("menu");
			Ui.setPadding(10, 10);
			Ui.label("feature-test", 0);
			Ui.beginBox("row", UiDirection.ROW);
			Ui.setWidth(UiSizing.FIT, 0);
			Ui.setHeight(UiSizing.FIT, 0);
			Ui.setGap(4);
			Ui.setAlign(UiAlign.CENTER, UiAlign.CENTER);
			Ui.setColor(0);
			Ui.button("a", "A");
			Ui.spacer(8);
			Ui.button("b", "B");
			Ui.endBox();
			expect(Ui.progress(0.5), "a progress bar");
			Ui.endPanel();
			expect(Ui.end(), "a UI");
		}
		expect(Ui.setFocus("a") && Ui.getFocus() == "a", "the focus");
	}

	static function document():Void {
		Ui.drawDocuments();
		if (menuDone || Resource.getStatus(menu) != ResourceStatus.READY)
			return;
		menuDone = true;
		final play = menu.find("play"), name = menu.find("name"), volume = menu.find("volume"), sound = menu.find("sound");
		final status = menu.find("status");
		expect(!play.isNone() && play.getDocument() == menu && play.getId() == "play" && menu.find("nothing").isNone(),
			"a document's elements, found by id");
		expect(status.setText("<feature>") && status.getText() == "<feature>" && status.setRml("<b>bold</b>")
			&& status.getRml().indexOf("<b>") >= 0, "an element's text and RML");
		expect(status.setAttribute("data-x", "1") && status.getAttribute("data-x") == "1" && status.hasAttribute("data-x")
			&& status.removeAttribute("data-x"), "an attribute");
		expect(status.setClass("hot", true) && status.hasClass("hot"), "a class");
		expect(status.setProperty("color", "#ff0000") && status.getProperty("color") != "", "a property");
		expect(name.setValue("Feature") && name.getValue() == "Feature", "a text field's value");
		expect(volume.setNumber(3) && volume.getNumber() == 3, "a slider's number");
		expect(sound.setChecked(false) && !sound.isChecked(), "a check box");
		expect(name.setDisabled(true) && name.isDisabled() && name.setDisabled(false), "a form control disabled");
		expect(menu.setFocus(play) && menu.getFocus() == play, "the focus");
		final events:Array<Int> = [for (_ in 0...16) 0];
		menu.getEventCount();
		expect(menu.takeEvents(events) % 2 == 0, "events, two ints each");
	}

	// Input recorded and played back: two frames recorded, then a replay holding space for three.
	static function replay():Void {
		switch (frames) {
			case 4:
				expect(Replay.startRecording() && Replay.isRecording(), "input recorded");
			case 6:
				expect(Replay.stopRecording() && !Replay.isRecording() && Replay.getRecordingFrameCount() == 2
					&& StringTools.startsWith(Replay.getRecording(), "wgf-autopilot 1"), "two frames recorded, as an autopilot");
				Replay.clearRecording();
				expect(Replay.getRecording() == "" && Replay.getRecordingFrameCount() == 0, "the recording let go of");
				expect(!Replay.play("not an autopilot"), "a replay that isn't an autopilot refused");
				expect(Replay.play("wgf-autopilot 1\nat 0 key down space\nat 2 end\n") && Replay.getState() == PlayState.PLAYING
					&& Replay.getFrameCount() == 3 && Replay.getFrame() == 0, "a replay played");
			case 7:
				expect(Keyboard.isDown(KeyboardKey.SPACE) && Replay.getFrame() == 1 && !Replay.hasRealPress(),
					"the replay's key down, the real input held back");
				expect(Replay.pause() && Replay.getState() == PlayState.PAUSED && Replay.resume(), "a replay paused and resumed");
			case 10:
				expect(Replay.getState() == PlayState.COMPLETE && Replay.getFrame() == 3 && !Keyboard.isDown(KeyboardKey.SPACE),
					"the replay complete, its key let go of");
				expect(Replay.stop() && Replay.getState() == PlayState.STOPPED, "the replay stopped");
			default:
		}
	}

	static function input():Void {
		Input.getChars();
		Keyboard.getState(KeyboardKey.SPACE);
		Keyboard.isDown(KeyboardKey.SPACE);
		Keyboard.isPressed(KeyboardKey.SPACE);
		Keyboard.isReleased(KeyboardKey.SPACE);
		Keyboard.isRepeated(KeyboardKey.SPACE);
		Keyboard.getLastPressed();
		expect(Gamepad.getLastPressed(0) == GamepadButton.NONE, "no pad button pressed");
		Touch.isTapped();
		Touch.isDoubleTapped();
		Touch.getTapPosition();
		Touch.getSwipe();
		Touch.isHolding();
		expect(Mouse.setCursor(MouseCursor.HAND) && Mouse.getCursor() == MouseCursor.HAND && Mouse.setCursor(MouseCursor.DEFAULT),
			"the cursor's shape");
		expect(Input.setClipboard("wgf") && Input.getClipboard() != null, "the clipboard");
		expect(!Window.openUrl(""), "an empty URL refused (a real one would open a browser)");
		Window.hasTouch();
		Window.hasFinePointer();
		Window.getOs();
		Window.isWeb();
		Mouse.getPosition();
		Mouse.isOverWindow();
		Mouse.getDelta();
		Mouse.getWheel();
		Mouse.getButtonState(MouseButton.LEFT);
		Mouse.isDown(MouseButton.LEFT);
		Mouse.isPressed(MouseButton.LEFT);
		Mouse.isReleased(MouseButton.LEFT);
		Touch.getCount();
		Touch.getId(0);
		Touch.getState(0);
		Touch.getPosition(0);
		Touch.getDelta(0);
		Touch.isGesture();
		Touch.getGestureCenter();
		Touch.getGesturePan();
		Touch.getGestureScale();
		Touch.getGestureRotation();
		Gamepad.isConnected(0);
		Gamepad.getName(0);
		Gamepad.getButtonState(0, GamepadButton.SOUTH);
		Gamepad.isDown(0, GamepadButton.SOUTH);
		Gamepad.isPressed(0, GamepadButton.SOUTH);
		Gamepad.isReleased(0, GamepadButton.SOUTH);
		Gamepad.getStick(0, GamepadStick.LEFT);
		Gamepad.getTrigger(0, GamepadTrigger.LEFT);
		expect(!Gamepad.canRumble(0) && !Gamepad.rumble(0, 1, 1, 0.5) && !Gamepad.isRumbling(0) && !Gamepad.stopRumble(0),
			"no pad: no rumble");
		expect(Gamepad.addMappings("# a pad of no one's\n05000000111100002222000000000000,Nobody's Pad,a:b1,b:b0,leftx:a0,") == 1,
			"a pad's mapping, SDL's line");
		Gamepad.getGuid(0);
		Gamepad.isMapped(0);
		expect(Window.getSafeInsetTop() >= 0 && Window.getSafeInsetRight() >= 0 && Window.getSafeInsetBottom() >= 0
			&& Window.getSafeInsetLeft() >= 0, "the safe area's insets");
		expect(Window.getOrientation() == (Window.getHeight() > Window.getWidth() ? WindowOrientation.PORTRAIT : WindowOrientation.LANDSCAPE),
			"the orientation is the window's shape");
		Window.canLockOrientation();
		Window.requestOrientation(WindowOrientation.ANY); // natively refused; a page lets go of what it never held
		expect(Window.getOrientationLock() == WindowOrientation.ANY, "no orientation locked");
	}

	/** The storage's tasks, a stage at a time: each stage's done before the next is asked. **/
	static var stage = 0;

	static function waitOnTasks():Void {
		if (stage > 3)
			return;
		for (task in tasks)
			if (task.getStatus() == FsTaskStatus.PENDING)
				return;
		for (task in tasks) {
			final status = task.getStatus();
			switch stage {
				case 0: // mkdir, write
					expect(status == FsTaskStatus.DONE, 'made and written: ${task.getPath()}');
				case 1: // read, exists
					expect(status == FsTaskStatus.DONE, 'read and found: ${task.getPath()}');
					if (task.getSize() > 0)
						expect(task.getText() == "feature" && task.getData().length == 7, "read back");
				default: // remove, then rmdir
					expect(status == FsTaskStatus.DONE, 'removed: ${task.getPath()}');
			}
			task.destroy();
		}
		tasks = switch stage++ {
			case 0: [Fs.read("feature/a.txt"), Fs.exists("feature/a.txt")];
			case 1: [Fs.remove("feature/a.txt")];
			case 2: [Fs.rmdir("feature")];
			default: [];
		}
	}

	/** A skinned glTF file's animations, played on its root, once it has loaded. **/
	static function skinning():Void {
		expect(Resource.getStatus(robot.getMesh()) == ResourceStatus.READY && robot.getAnimationCount() == 14,
			"a skinned file loaded: its 14 animations");
		final dance = robot.findAnimation("Robot_Dance"), wave = robot.findAnimation("Robot_Wave");
		expect(dance >= 0 && robot.getAnimationName(dance) == "Robot_Dance" && robot.findAnimation("nope") == -1,
			"animations found by name");
		expect(near(robot.getAnimationDuration(dance), 3.3333333), "an animation's length");
		expect(robot.playAnimation(dance, 0) && robot.getAnimation() == dance && robot.getAnimationWeight() == 1,
			"played, cut to");
		expect(robot.setAnimationTime(1.5) && near(robot.getAnimationTime(), 1.5), "its time set");
		expect(robot.setAnimationSpeed(0.5) && robot.getAnimationSpeed() == 0.5, "its speed");
		expect(robot.setAnimationLoop(false) && !robot.isAnimationLoop() && robot.setAnimationLoop(true), "looping");
		expect(robot.playAnimation(wave, 0.25) && robot.getAnimationWeight() == 0, "crossfaded into another");
		expect(robot.playLayerAnimation(0, dance, "Torso", 0, true) && robot.getLayerAnimation(0) == dance
			&& robot.getLayerState(0) == PlayState.PLAYING && robot.getLayerWeight(0) == 1 && robot.stopLayerAnimation(0, 0),
			"a layer from the torso up, and stopped");
		{
			final twin:Model = Model.create(robot.getMesh());
			expect(twin.syncAnimation(robot) && twin.getAnimation() == robot.getAnimation(), "a model synced to another");
			expect(!robot.findNode("Hand.R").isNone() && robot.findNode("nope").isNone(), "a socket found by name");
			(twin : Actor).destroy(ActorDestroy.DESTROY_CHILDREN);
		}
		expect(robot.pauseAnimation() && robot.getAnimationState() == PlayState.PAUSED && robot.resumeAnimation()
			&& robot.stopAnimation() && robot.getAnimationState() == PlayState.STOPPED, "paused, resumed, stopped");
		expect(robot.setAnimations(robot.getMesh()) && robot.getAnimations() == robot.getMesh() && robot.getAnimationCount() == 14
			&& robot.setAnimations(cast 0), "clips from a file by its nodes' names, and its own again");
	}

	static function finish():Void {
		reported = true;
		expect(Rock.made == 3, 'the scene\'s rocks given their behaviors (${Rock.made})');
		expect(Title.entered == 1 && Title.exited == 1, 'a screen\'s behavior told it came and went (${Title.entered}, ${Title.exited})');
		expect(!tiles.isNone() && tiles.getWidth() > 0 && tiles.getHeight() > 0, "a texture loaded");
		expect(Resource.getPath(tiles) == "textures/tiles.png" && Resource.getStatus(tiles) == ResourceStatus.READY, "a resource");
		expect((tiles : Handle).getKindName() == "gfx.texture", "its kind");
		expect(Resource.getStatus(car.getMesh()) == ResourceStatus.READY && !car.find("toy_car/body/wheel_fl").isNone(),
			"a glTF file loaded, its nodes under its model");
		skinning();
		tilemap();
		{ // the atlas, loaded
			expect(Resource.getStatus(gems) == ResourceStatus.READY && gems.getFrameCount() == 4
				&& gems.getFrameName(0) == "gem_1.png" && gems.findFrame("gem_3.png") == 2, "an atlas read: its frames by name");
			expect(gems.getClipCount() == 1 && gems.getClipName(0) == "gem" && gems.findClip("gem") == 0,
				"its numbered run, a clip");
			expect(gems.setClipEvent("gem", 0, 5) && gems.getClipEvent("gem", 0) == 5, "an event on its first frame");
			final gem = Sprite.create(cast 0);
			final taken:Array<Int> = [0, 0];
			expect(gem.setSheet(gems) && gem.playClip("gem", true) && gem.takeClipEvents(taken) == 1 && taken[0] == 5,
				"played: its first frame's event");
			expect(gem.setClipFacing("left") && gem.getClipFacing() == "left" && gem.getClip() == "gem", "a facing it has no clip for");
			gem.destroy(ActorDestroy.DESTROY_CHILDREN);
			Resource.release(gems);
		}
		expect(music.getDuration() >= 0 && click.getDuration() > 0, "the sounds' lengths");
		ensure.getStatus();
		ensure.getPath();
		ensure.getProgress();
		ensure.destroy();
		group.destroy();
		ping.getStatus();
		Asset.pingGetMilliseconds(ping);
		ping.destroy();
		voice.stop();
		voice.destroy();
		World.clear();
		expect(Actor.getCount() == 0, "the world cleared");
		expect(Resource.release(scene) && Resource.release(music), "resources released");
		// the run ends after this frame, which finishes first: the verdict is still given
		App.canQuit();
		App.quit();
		final verdict = failures == 0 ? "PASS" : 'FAIL ($failures)';
		#if wgf_reach
		final missing = wgf.impl.Reach.missing();
		if (missing.length > 0)
			Log.message(LogLevel.ERROR, 'feature test: ${missing.length} call(s) never reached: ${missing.join(", ")}');
		final reach = missing.length == 0;
		#else
		final reach = true;
		#end
		Log.message(failures == 0 && reach ? LogLevel.INFO : LogLevel.ERROR,
			'feature test: ${failures == 0 && reach ? "PASS" : verdict + (reach ? "" : ", calls unreached")}');
	}

	static function tilemap():Void {
		expect(Resource.getStatus(island) == ResourceStatus.READY && island.getColumns() == 24 && island.getRows() == 16
			&& island.getTileWidth() == 16 && island.getTileHeight() == 16 && island.getBackgroundColor() == 0x18202cff,
			"a Tiled map read: its size, tile size, background");
		expect(island.getLayerCount() == 5 && island.getLayerName(0) == "ground" && island.findLayer("props") == 2
			&& island.getLayerKind(3) == TilemapLayerKind.OBJECTS && island.getLayerKind(4) == TilemapLayerKind.IMAGE
			&& !island.getLayerTexture(4).isNone() && island.getLayerTexture(0).isNone(), "its layers, an image layer's image");
		expect(island.getTilesetCount() == 3 && island.getTilesetName(0) == "tiles" && island.getTilesetFirstGid(1) == 9
			&& island.getTilesetTileCount(0) == 8 && !island.getTilesetTexture(1).isNone(), "its tilesets");
		expect(island.getOrientation() == TilemapOrientation.ORTHOGONAL && !island.isInfinite() && island.getOriginColumn() == 0
			&& island.getOriginRow() == 0 && island.getPixelWidth() == 384 && island.getPixelHeight() == 256
			&& island.getCellMiddle(1, 1).x == 24 && island.getColumnAt(40, 40) == 2 && island.getRowAt(40, 40) == 2,
			"its grid: orthogonal, finite, its size in pixels, cells and points");
		expect(island.getTerrainsetCount() == 1 && island.getTerrainsetName(0) == "trail"
			&& island.getTerrainsetKind(0) == TilemapTerrainKind.CORNER && island.getTerrainsetTileset(0) == 2
			&& island.findTerrainset("trail") == 0 && island.getTerrainCount(0) == 1 && island.getTerrainName(0, 1) == "dirt"
			&& island.findTerrain(0, "dirt") == 1 && island.getTileTerrain(0, 13 + 15, 1) == 1, "its terrain set");
		var hero = 0;
		while (hero < island.getObjectCount(3) && island.getObjectName(3, hero) != "hero")
			hero++;
		expect(island.getObjectClass(3, hero) == "hero" && island.getObjectShape(3, hero) == TilemapShape.POINT
			&& island.getObjectId(3, hero) == hero + 1 && near(island.getObjectPosition(3, hero).x, 72)
			&& island.getObjectSize(3, hero).x == 0 && island.getObjectRotation(3, hero) == 0 && island.isObjectVisible(3, hero)
			&& island.getObjectTile(3, hero) == 0 && island.getObjectPointCount(3, hero) == 0
			&& island.getObjectPoints(3, hero, [0.0, 0]) == 0 && near(island.getObjectMiddle(3, hero).y, 152),
			"an object: its name, class, shape, place");
		expect(island.getObjectTile(3, 20) == 8 && island.getObjectTileFlips(3, 20) == TilemapFlip.X
			&& near(island.getObjectMiddle(3, 20).x, 48) && near(island.getObjectMiddle(3, 20).y, 56),
			"a tile object: its tile, flips, and middle");
		expect(island.getPropertyCount(-1, -1) == 1 && island.getPropertyName(-1, -1, 0) == "title"
			&& island.getProperty(-1, -1, "title") == "Coin island"
			&& island.getPropertyType(3, hero, "speed") == TilemapPropertyType.FLOAT
			&& near(island.getPropertyNumber(3, hero, "speed"), 40), "the map's and an object's properties");
		expect(island.getTileClass(8) == "rock" && island.getTilePropertyCount(8) == 1 && island.getTilePropertyName(8, 0) == "solid"
			&& island.getTilePropertyType(8, "solid") == TilemapPropertyType.BOOL && island.getTileProperty(8, "solid") == "true"
			&& island.getTilePropertyNumber(8, "solid") == 1, "a tile's class and properties");
		final root = island.instantiate(world);
		final ground:TilemapLayer = root.find("ground");
		final props:TilemapLayer = root.find("props");
		final things:TilemapLayer = root.find("things");
		final trail:TilemapLayer = root.find("trail");
		expect(!root.isNone() && root.getChildCount() == 5 && ground.getMap() == island && props.getIndex() == 2
			&& (ground : Actor).getKind() == ActorKind.TILEMAP_LAYER, "placed: a layer actor each");
		expect(!things.getObjectActor(20).isNone() && things.getObjectActor(0).isNone(), "a tile object drawn as its tile");
		expect(trail.paintTerrain(5, 5, 0, 1) && trail.getCell(5, 5) == 13 + 15 && trail.getCell(4, 4) == 13 + 4
			&& trail.getCellMiddle(5, 5).x == 88, "terrain painted, the cells around fitted");
		expect(ground.getCell(0, 0) == 1 && props.getCell(21, 9) == 8 && props.getCellFlips(21, 9) == TilemapFlip.X
			&& ground.setCell(0, 0, 2, TilemapFlip.NONE) && ground.getCell(0, 0) == 2, "a cell read and set");
		final cells = [for (_ in 0...24 * 16) 0];
		expect(ground.getCells(cells) == 24 * 16 && cells[0] == 2 && ground.setCells(cells), "every cell, in bulk");
		expect(ground.getColumnAt(40, 0) == 2 && ground.getRowAt(0, 40) == 2, "a cell from a point");
		final walls:Body2d = (things : Actor);
		expect(walls.setGridFromLayer(props) && walls.getGridCell(21, 9) == 1 && walls.getGridCell(4, 9) == 0,
			"a layer's cells a 2D body's solid grid");
		expect(scene.spawnObjects(things) == 0, "the field's scene has no prefab of the map's classes");
		root.destroy(ActorDestroy.DESTROY_CHILDREN);
		Resource.release(island);
	}

	static function shutdown():Void {
		#if js
		js.Syntax.code("if (typeof process !== 'undefined') process.exitCode = {0}", reported && failures == 0 ? 0 : 1);
		#elseif sys
		Sys.exit(reported && failures == 0 ? 0 : 1);
		#end
	}
}
