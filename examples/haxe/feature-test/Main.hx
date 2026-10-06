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
class Rock extends Script {
	public static var made = 0;
	public static var hits = 0;

	override function onCreate()
		made++;

	override function onTriggerEnter(other:Entity)
		hits++;
}

class Main {
	static var failures = 0;
	static var frames = 0;
	static var world:Canvas;
	static var hud:Canvas;
	static var camera:Camera2d;
	static var tiles:Texture;
	static var font:Font;
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
		platform();
		assets();
		audio();
		gfx();
		ecs();
	}

	static function core():Void {
		expect(Version.get() != "" && Version.getMajor() >= 0 && Version.getMinor() >= 0 && Version.getPatch() >= 0,
			"the version");
		Log.setLevel(LogLevel.INFO);
		expect(Log.getLevel() == LogLevel.INFO, "the log level");
		Log.message(LogLevel.INFO, "feature-test begins");
		Log.messageSource(LogLevel.DEBUG, "Main.hx", 1, "a line at its source, below the level shown");
		expect(Identity.setCompany("Whirling Gizmo") && Identity.getCompany() == "Whirling Gizmo", "the company");
		expect(Identity.setProduct("feature-test") && Identity.getProduct() == "feature-test", "the product");
		Random.setSeed(7);
		expect(Random.getSeed() == 7, "the seed");
		final f = Random.getFloat(), r = Random.getRange(2, 3), i = Random.getInt(1, 6);
		expect(f >= 0 && f < 1 && r >= 2 && r <= 3 && i >= 1 && i <= 6, "random numbers in range");
		expect(Time.getSeconds() >= 0, "the time");
		expect(Probe.setValue("feature.answer", 42) && Probe.hasValue("feature.answer") && Probe.getValue("feature.answer") == 42,
			"a probe");
		expect(Probe.getCount() >= 1 && Probe.getName(0) != "", "the probes listed");
		// the root is the program's directory, or "" (the working directory) where that can't be told
		expect(Fs.setRoot(Fs.getRoot()), "the storage's root, set to itself");
		tasks.push(Fs.mkdir("feature"));
		tasks.push(Fs.write("feature/a.txt", haxe.io.Bytes.ofString("feature")));
		expect(Resource.setLoadBudget(4) && near(Resource.getLoadBudget(), 4), "the load budget");
	}

	static function platform():Void {
		expect(Window.getTitle() == "feature-test", "the title");
		Window.getWidth();
		Window.getHeight();
		Window.setFullscreen(false);
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
		Gamepad.setDeadzone(0.2);
		expect(near(Gamepad.getDeadzone(), 0.2), "the dead zone");
		Touch.setMouseEmulated(true);
		expect(Touch.isMouseEmulated(), "touch drives the mouse");
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
	}

	static function gfx():Void {
		Render.setClearColor(Color.make(10, 12, 20, 255));
		expect(Render.getClearColor() == Color.make(10, 12, 20, 255), "the clear color");
		Render.getWidth();
		Render.getHeight();
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

		world = Canvas.create();
		hud = Canvas.create();
		camera = Camera2d.create();
		expect(camera.setZoom(1.5) && near(camera.getZoom(), 1.5), "a camera's zoom");
		expect(world.setCamera(camera) && (world.getCamera() : Node) == camera, "a canvas's camera");

		final group = Node.create();
		expect(group.setParent(world) && group.getParent() == world && world.getChildCount() == 1
			&& world.getChild(0) == group, "a node in a canvas");
		expect(group.setName("group") && group.getName() == "group" && world.find("group") == group, "named");
		expect(group.setTransform(5, 6, 0, 0, 0, 0.5, 2, 2, 1), "a transform at once");
		expect(group.setPosition(10, 20, 0) && near(group.getPosition().x, 10), "a position");
		expect(group.setRotation(0, 0, 1) && near(group.getRotation().z, 1), "a rotation");
		expect(group.setScale(2, 2, 1) && near(group.getScale().x, 2), "a scale");
		group.getWorldPosition();
		expect(group.setEnabled(true) && group.isEnabled() && group.setVisible(true) && group.isVisible(), "on and shown");
		expect(group.getType() == NodeType.NODE && group.setIndex(0) && group.getIndex() == 0, "its type and index");

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
		expect(shape.setColor(white) && shape.getColor() == white, "its color");

		final sprite = Sprite.create(tiles);
		sprite.setParent(group);
		expect(sprite.setTexture(tiles) && sprite.getTexture() == tiles, "a sprite's texture");
		expect(sprite.setSource(0, 0, 16, 16) && near(sprite.getSource().z, 16), "its source");
		expect(sprite.setSize(32, 32) && near(sprite.getSize().y, 32), "its size");
		expect(sprite.setPivot(0.5, 0.5) && near(sprite.getPivot().x, 0.5), "its pivot");
		expect(sprite.setTint(white) && sprite.getTint() == white, "its tint");

		final label = Text.create(font);
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
		expect(emitter.setRate(30) && near(emitter.getRate(), 30), "an emitter's rate");
		expect(emitter.setEmitting(true) && emitter.isEmitting(), "emitting");
		expect(emitter.setCapacity(64) && emitter.getCapacity() == 64, "its capacity");
		expect(emitter.setLife(0.2, 0.5) && near(emitter.getLifeMin(), 0.2) && near(emitter.getLifeMax(), 0.5), "its life");
		expect(emitter.setDirection(1, 0.5) && near(emitter.getDirection(), 1) && near(emitter.getSpread(), 0.5), "its direction");
		expect(emitter.setSpeed(10, 20) && near(emitter.getSpeedMin(), 10) && near(emitter.getSpeedMax(), 20), "its speed");
		expect(emitter.setRadius(3) && near(emitter.getRadius(), 3), "its radius");
		expect(emitter.setGravity(0, 10) && near(emitter.getGravity().y, 10), "its gravity");
		expect(emitter.setDrag(0.5) && near(emitter.getDrag(), 0.5), "its drag");
		expect(emitter.setSize(4, 1) && near(emitter.getSizeStart(), 4) && near(emitter.getSizeEnd(), 1), "its sizes");
		expect(emitter.setColor(white, 0) && emitter.getColorStart() == white && emitter.getColorEnd() == 0, "its colors");
		expect(emitter.setStretch(0.1) && near(emitter.getStretch(), 0.1), "its stretch");
		expect(emitter.burst(8) && emitter.getCount() >= 0, "a burst");
		expect(emitter.clear() && emitter.getCount() == 0, "its particles cleared");
		emitter.burst(8);

		final gone = Node.create();
		gone.destroy(NodeDestroy.DESTROY_CHILDREN);
		expect(gone.getType() == NodeType.NONE, "a node destroyed");
		expect(tiles.setSampling(TextureWrap.REPEAT, TextureWrap.CLAMP, TextureFilter.NEAREST)
			&& tiles.getWrapU() == TextureWrap.REPEAT && tiles.getWrapV() == TextureWrap.CLAMP
			&& tiles.getFilter() == TextureFilter.NEAREST, "a texture's sampling");
	}

	static function ecs():Void {
		Script.register("Rock", Rock.new);
		scene = Scene.create("scenes/field.scene");
		final ship = Entity.create(world);
		expect(!ship.isNone() && ship.isAlive() && Entity.getCount() >= 1 && !ship.getNode().isNone(), "an entity");
		expect(ship.setName("probe ship") && ship.getName() == "probe ship" && Entity.find("probe ship") == ship, "named");
		expect(ship.setTransform(100, 100, 0, 0, 0, 0, 1, 1, 1) && ship.snap(), "a transform, snapped");
		expect(ship.setPosition(120, 100, 0) && near(ship.getPosition().x, 120), "a position");
		expect(ship.setRotation(0, 0, 0.5) && near(ship.getRotation().z, 0.5), "a rotation");
		expect(ship.setScale(1, 1, 1) && near(ship.getScale().x, 1), "a scale");
		final out = [0.0, 0, 0];
		expect(Entity.setPositions([ship], [130.0, 100, 0]) && Entity.getPositions([ship], out) == 3 && near(out[0], 130),
			"positions in bulk");
		for (c in [Component.MOTION, Component.BOUNDS, Component.LIFETIME, Component.COLLIDER, Component.BEHAVIOR,
			Component.SHAPE2D, Component.SPRITE, Component.TEXT, Component.EMITTER2D, Component.VOICE])
			expect(ship.addComponent(c) && ship.hasComponent(c), 'a component: $c');
		expect(!ship.getComponentNode(Component.SHAPE2D).isNone() && !ship.getVoice().isNone(), "its nodes and its voice");
		final motion:Motion = ship;
		expect(motion.setVelocity(1, 0, 0) && near(motion.getVelocity().x, 1), "a velocity");
		expect(motion.setSpin(0, 0, 1) && near(motion.getSpin().z, 1), "a spin");
		expect(motion.setDamping(0.1) && near(motion.getDamping(), 0.1), "damping");
		expect(motion.setMaxSpeed(50) && near(motion.getMaxSpeed(), 50), "a top speed");
		final bounds:Bounds = ship;
		expect(bounds.setRect(0, 0, 640, 360) && near(bounds.getRect().z, 640), "bounds");
		expect(bounds.setMode(BoundsMode.WRAP) && bounds.getMode() == BoundsMode.WRAP, "wrapping");
		expect(bounds.setMargin(10) && near(bounds.getMargin(), 10), "a margin");
		final life:Lifetime = ship;
		expect(life.setSeconds(100) && near(life.getSeconds(), 100), "a lifetime");
		final collider:Collider = ship;
		expect(collider.setRadius(8) && near(collider.getRadius(), 8), "a collider");
		expect(collider.setLayer(2) && collider.getLayer() == 2 && collider.setMask(1) && collider.getMask() == 1, "its layers");
		collider.getOverlaps([ship]);
		final behavior:Behavior = ship;
		expect(behavior.setName("Probe") && behavior.getName() == "Probe", "a behavior's name");
		expect(behavior.setParam("speed", "3.5") && behavior.hasParam("speed") && behavior.getParam("speed") == "3.5"
			&& near(behavior.getParamNumber("speed"), 3.5), "a parameter");
		expect(behavior.getParamCount() == 1 && behavior.getParamKey(0) == "speed", "the parameters listed");
		expect(ship.removeComponent(Component.LIFETIME) && !ship.hasComponent(Component.LIFETIME), "a component removed");
		expect(Ecs.countBehavior("Probe") == 1 && Ecs.findBehavior("Probe", [0]) == 1, "behaviors found");
		Ecs.getEventCount();
		final doomed = Entity.create(world);
		expect(doomed.destroy() && !doomed.isAlive(), "an entity destroyed");
	}

	// ---- the frames ----------------------------------------------------------------------

	static function tick():Void {}

	static function frame():Void {
		frames++;
		world.draw();
		hud.draw();
		Draw.rectangle(4, 4, 20, 10, Color.get(ColorStock.RED));
		Draw.rectangleLines(4, 20, 20, 10, 1, Color.get(ColorStock.GREEN));
		Draw.line(0, 0, 30, 30, 2, Color.get(ColorStock.BLUE));
		Draw.circle(50, 50, 5, Color.get(ColorStock.YELLOW));
		Draw.circleLines(70, 50, 5, 1, Color.get(ColorStock.ORANGE));
		Draw.triangle(80, 80, 90, 80, 85, 90, Color.get(ColorStock.PURPLE));
		expect(Draw.polyline([0.0, 100, 20, 110, 40, 100], false, 2, Color.get(ColorStock.WHITE)), "a polyline");
		expect(Draw.polygon([100.0, 100, 120, 100, 110, 115], Color.get(ColorStock.WHITE)), "a polygon drawn");
		Render.pushClip(0, 0, 200, 200);
		Draw.text(font, "feature-test", 4, 120, 14, Color.get(ColorStock.WHITE));
		Draw.texture(tiles, 140, 4, 32, 32, Color.get(ColorStock.WHITE));
		Draw.textureRegion(tiles, 0, 0, 8, 8, 180, 4, 16, 16, Color.get(ColorStock.WHITE));
		Render.popClip();
		ui();
		input();
		if (frames == 2) {
			Loop.getFrameDelta();
			Loop.getTickFraction();
			Loop.getFps();
			expect(App.isRunning(), "running");
		}
		waitOnTasks();
		if (!instantiated && Resource.getStatus(scene) == ResourceStatus.READY) {
			instantiated = true;
			expect(scene.getEntityCount() == 4 && scene.getPrefabCount() == 2 && scene.getPrefabName(0) == "rock"
				&& scene.hasPrefab("spark"), "the scene file");
			expect(scene.instantiate(world) == 4, "the scene's entities made");
			expect(!scene.spawn("spark", world).isNone(), "a prefab spawned");
			expect(Ecs.dump().indexOf("wgf-scene 1") == 0, "the world dumped");
		}
		if (frames >= 30 && instantiated && stage > 3 && !reported)
			finish();
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
			Ui.endPanel();
			expect(Ui.end(), "a UI");
		}
		expect(Ui.setFocus("a") && Ui.getFocus() == "a", "the focus");
	}

	static function input():Void {
		Input.getChars();
		Keyboard.getState(KeyboardKey.SPACE);
		Keyboard.isDown(KeyboardKey.SPACE);
		Keyboard.isPressed(KeyboardKey.SPACE);
		Keyboard.isReleased(KeyboardKey.SPACE);
		Mouse.getPosition();
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

	static function finish():Void {
		reported = true;
		expect(Rock.made == 3, 'the scene\'s rocks given their scripts (${Rock.made})');
		expect(!tiles.isNone() && tiles.getWidth() > 0 && tiles.getHeight() > 0, "a texture loaded");
		expect(Resource.getPath(tiles) == "textures/tiles.png" && Resource.getStatus(tiles) == ResourceStatus.READY, "a resource");
		expect((tiles : Handle).getKindName() == "gfx.texture", "its kind");
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
		Ecs.clear();
		expect(Entity.getCount() == 0, "the world cleared");
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

	static function shutdown():Void {
		#if js
		js.Syntax.code("if (typeof process !== 'undefined') process.exitCode = {0}", reported && failures == 0 ? 0 : 1);
		#elseif sys
		Sys.exit(reported && failures == 0 ? 0 : 1);
		#end
	}
}
