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

class Main {
	static var failures = 0;
	static var frames = 0;
	static var world:Stage2d;
	static var hud:Stage2d;
	static var camera:Camera2d;
	static var tiles:Texture;
	static var car:Model = 0; // a section type starts as none, as a handle does
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
		physics();
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
		expect(Probe.setText("feature.screen", "title") && Probe.getText("feature.screen") == "title"
			&& Probe.getText("feature.answer") == "", "a text probe");
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
		final file = Mesh.create("models/toy_car.glb");
		car = Model.create(file);
		expect(!file.isNone() && !car.isNone() && Resource.release(file), "a glTF file's mesh, a model of it");
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
			expect(camera3d.lookAt(0, 0, 0, 0, 1, 0) && !camera3d.lookAt(6, 4, 6, 0, 1, 0), "aimed with lookAt");
		}
		if (frames == 2) meshes();
		if (frames == 2) makeStage();
		if (frames >= 2) stage3d.draw();
		expect(Draw.begin3d(camera3d), "3D immediate mode");
		Draw.grid(4, 1, Color.get(ColorStock.GRAY));
		Draw.line3d(0, 0, 0, 1, 1, 1, Color.get(ColorStock.RED));
		Draw.cube(0, 0.5, 0, 1, 1, 1, Color.get(ColorStock.SKYBLUE));
		Draw.cubeWires(0, 0.5, 0, 1, 1, 1, Color.get(ColorStock.DARKBLUE));
		Draw.sphere(2, 0.5, 0, 0.5, Color.get(ColorStock.GOLD));
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

		world = Stage2d.create();
		hud = Stage2d.create();
		camera = Camera2d.create();
		expect(camera.setZoom(1.5) && near(camera.getZoom(), 1.5), "a camera's zoom");
		expect(world.setCamera(camera) && (world.getCamera() : Actor) == camera, "a 2D stage's camera");

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

		final gone = Actor.create();
		gone.destroy(ActorDestroy.DESTROY_CHILDREN);
		expect(gone.getKind() == ActorKind.NONE, "an actor destroyed");
		expect(tiles.setSampling(TextureWrap.REPEAT, TextureWrap.CLAMP, TextureFilter.NEAREST)
			&& tiles.getWrapU() == TextureWrap.REPEAT && tiles.getWrapV() == TextureWrap.CLAMP
			&& tiles.getFilter() == TextureFilter.NEAREST, "a texture's sampling");
	}

	static function ecs():Void {
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
	}

	// ---- the frames ----------------------------------------------------------------------

	static function tick():Void {}

	static function frame():Void {
		frames++;
		Physics.drawBodies(Color.get(ColorStock.YELLOW)); // outside a 3D drawing: nothing, but reached
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
		Draw.textAligned(font, "right", 200, 140, 14, Color.get(ColorStock.WHITE), TextHalign.RIGHT, TextValign.MIDDLE);
		Draw.texture(tiles, 140, 4, 32, 32, Color.get(ColorStock.WHITE));
		Draw.textureRegion(tiles, 0, 0, 8, 8, 180, 4, 16, 16, Color.get(ColorStock.WHITE));
		Render.popClip();
		threeD();
		ui();
		input();
		if (frames == 2) {
			Loop.getFrameDelta();
			Loop.getTickFraction();
			Loop.getFps();
			expect(App.isRunning(), "running");
			expect(!Debug.isFpsShown(), "the frame-rate overlay off by default");
			Debug.showFps(0, 8, 8, 14, Color.get(ColorStock.LIME));
			expect(Debug.isFpsShown(), "and shown");
		}
		if (frames == 3) {
			expect(Loop.getFrameCost() > 0, "a frame's cost, measured");
			Debug.hideFps();
			expect(!Debug.isFpsShown(), "the overlay hidden");
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
		expect(Rock.made == 3, 'the scene\'s rocks given their behaviors (${Rock.made})');
		expect(!tiles.isNone() && tiles.getWidth() > 0 && tiles.getHeight() > 0, "a texture loaded");
		expect(Resource.getPath(tiles) == "textures/tiles.png" && Resource.getStatus(tiles) == ResourceStatus.READY, "a resource");
		expect((tiles : Handle).getKindName() == "gfx.texture", "its kind");
		expect(Resource.getStatus(car.getMesh()) == ResourceStatus.READY && !car.find("toy_car/body/wheel_fl").isNone(),
			"a glTF file loaded, its nodes under its model");
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

	static function shutdown():Void {
		#if js
		js.Syntax.code("if (typeof process !== 'undefined') process.exitCode = {0}", reported && failures == 0 ? 0 : 1);
		#elseif sys
		Sys.exit(reported && failures == 0 ? 0 : 1);
		#end
	}
}
