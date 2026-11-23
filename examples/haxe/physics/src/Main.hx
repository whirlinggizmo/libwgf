import wgf.*;

/**
	3D physics, in Haxe: a pile of boxes and spheres falls onto a floor and settles.

	It shows rigid bodies (Body3d) on generated models:
	- A static floor, and dynamic boxes and spheres with their own mass, friction, and bounce.
	- Layers and masks: two bodies meet when either one's mask has the other's layer.
	- A ray cast from the camera through the pointer (Physics3d.castRay). The body it hits is
	  tinted.
	- A sensor (the green outline) that counts each body falling through it, by a behavior
	  told of its trigger events.

	Controls:
	- Point at a body to highlight it.
	- Space kicks the highlighted body up, or the next one in the pile when none is.
	- R puts the pile back where it started.
	- Escape quits, where quitting means anything (not in a browser).

	This is the JS example `physics`, written the Haxe way. Run it with `wgf serve` or `wgf run`.
**/
class Main {
	// Each body's layer. The pile meets everything; the floor and the sensor meet only the pile.
	static inline var PILE = 1;
	static inline var FLOOR = 2;
	static inline var SENSE = 4;

	static var stage:Stage3d;
	static var camera:Camera3d;
	static var sensor:Actor;
	static final pieces:Array<Piece> = [];
	static var hovered:Null<Piece> = null;
	static var kicks = 0;
	static var nextKick = 0;

	static final mouse = new Vec2(0, 0);
	static final origin = new Vec3(0, 0, 0);
	static final direction = new Vec3(0, 0, 0);
	static final velocity = new Vec3(0, 0, 0);
	static final hit:Array<Actor> = [0];

	static function main() {
		Window.setTitle("libwgf physics (Haxe)");
		// The design size, fitted to any window or page. A desktop window opens at this size.
		Presentation.set(PresentationMode.FIT, 960, 600);
		Window.setFlags(WindowFlag.MSAA);
		Runtime.run(init, tick, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Render.setClearColor(Color.make(150, 180, 210, 255));
		Physics3d.setGravity(0, -9.81, 0); // physics starts here, before the first body
		Behavior.register("Sensor", Sensor.new);

		stage = Stage3d.create();
		camera = Camera3d.create();
		camera.setPosition(0, 7, 12);
		camera.lookAt(0, 1.5, 0, 0, 1, 0);
		stage.setCamera(camera);
		stage.setAmbient(Color.make(150, 170, 200, 255), 0.35);
		final sun = Light.create(LightType.DIRECTIONAL);
		sun.lookAt(-0.4, -1.0, -0.3, 0, 1, 0);
		sun.setIntensity(1.2);
		sun.setParent(stage);

		// The floor: a static box, its top at y = 0.
		final slab = Mesh.createCube(20, 1, 20);
		final floor:Body3d = body(slab, Color.make(90, 120, 80, 255), 0, -0.5, 0);
		Resource.release(slab); // the model holds its own reference
		floor.setType(Body3dType.STATIC);
		floor.setShape(Body3dShape.BOX, 20, 1, 20);
		floor.setLayer(FLOOR);
		floor.setMask(PILE);

		// The pile: two layers of three by three, boxes and spheres in turn.
		final cube = Mesh.createCube(0.9, 0.9, 0.9), ball = Mesh.createSphere(0.45, 16, 24);
		for (i in 0...18) {
			final layer = Std.int(i / 9), x = (i % 3 - 1) * 1.2 + layer * 0.3, z = (Std.int(i / 3) % 3 - 1) * 1.2;
			final isBox = i % 2 == 0;
			final tint = isBox ? Color.make(210, 140, 60, 255) : Color.make(70, 120, 220, 255);
			final piece = new Piece(body(isBox ? cube : ball, tint, x, 4 + layer * 1.3, z), tint, i * 0.4);
			final b:Body3d = piece.actor;
			b.setType(Body3dType.DYNAMIC);
			b.setLayer(PILE);
			if (isBox) { // heavy, and grippy
				b.setShape(Body3dShape.BOX, 0.9, 0.9, 0.9);
				b.setMass(20);
				b.setFriction(0.8);
			} else { // light, and bouncy, with damping so it stops rolling
				b.setShape(Body3dShape.SPHERE, 0.45, 0, 0);
				b.setMass(4);
				b.setFriction(0.3);
				b.setBounce(0.6);
				b.setDamping(0.9, 0.95);
			}
			pieces.push(piece);
		}
		Resource.release(cube);
		Resource.release(ball);

		// The sensor: a body with no model, which the pile falls through on its way down.
		sensor = Actor.create();
		sensor.setPosition(0, 2.6, 0);
		sensor.setParent(stage);
		sensor.addComponent(Component.BODY3D);
		final sense:Body3d = sensor;
		sense.setType(Body3dType.SENSOR);
		sense.setShape(Body3dShape.BOX, 5, 0.8, 5);
		sense.setLayer(SENSE);
		sense.setMask(PILE);
		sensor.addBehavior("Sensor");
		reset();
	}

	/** A model of `mesh` on the stage at (x, y, z), tinted, with a body (a dynamic box until set). **/
	static function body(mesh:Mesh, tint:Int, x:Float, y:Float, z:Float):Body3d {
		final model = Model.create(mesh);
		model.setTint(tint);
		model.setPosition(x, y, z);
		model.setParent(stage);
		model.addComponent(Component.BODY3D);
		return model;
	}

	/** Puts every piece back where it started, at rest, and starts the count again. **/
	static function reset() {
		for (piece in pieces) {
			piece.actor.setTransform(piece.x, piece.y, piece.z, 0.2, piece.turn, 0.1, 1, 1, 1);
			(piece.actor : Body3d).setVelocity(0, 0, 0);
			(piece.actor : Body3d).setSpin(0, 0, 0);
		}
		final counter = Behavior.of(sensor, "Sensor");
		if (counter != null)
			(cast counter : Sensor).count = 0;
		kicks = 0;
		nextKick = 0;
	}

	static function tick() {
		if (Keyboard.isPressed(KeyboardKey.R))
			reset();

		// The ray under the pointer: from the camera, into the scene. Its mask sees only the pile.
		Mouse.getPosition(mouse);
		camera.screenToOrigin(mouse.x, mouse.y, origin);
		camera.screenToDirection(mouse.x, mouse.y, direction);
		final n = Physics3d.castRay(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, 100, PILE, hit,
			null);
		final now = n > 0 ? Lambda.find(pieces, p -> p.actor == hit[0]) : null;
		if (now != hovered) {
			if (hovered != null)
				(hovered.actor : Model).setTint(hovered.tint);
			if (now != null)
				(now.actor : Model).setTint(Color.get(ColorStock.YELLOW));
			hovered = now;
		}

		// Space: an impulse up, and a little sideways, sized to the body's mass for a 7 m/s kick.
		if (Keyboard.isPressed(KeyboardKey.SPACE)) {
			final target = hovered != null ? hovered : pieces[nextKick++ % pieces.length];
			final b:Body3d = target.actor;
			final mass = b.getMass();
			b.addImpulse(mass * 1.5, mass * 7, 0);
			kicks++;
		}

		// Settled: every piece nearly still.
		var settled = true;
		for (piece in pieces) {
			(piece.actor : Body3d).getVelocity(velocity);
			if (velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z > 0.1 * 0.1)
				settled = false;
		}
		final counter:Null<Sensor> = cast Behavior.of(sensor, "Sensor");
		Probe.setValue("physics.bodies", pieces.length);
		Probe.setValue("physics.sensed", counter != null ? counter.count : 0);
		Probe.setValue("physics.settled", settled ? 1 : 0);
		Probe.setValue("physics.hovered", hovered != null ? 1 : 0);
		Probe.setValue("physics.kicks", kicks);
	}

	static function frame() {
		if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE))
			App.quit();
		stage.draw();

		// The sensor has no model: its outline is drawn, bright for a moment after each entry.
		final counter:Null<Sensor> = cast Behavior.of(sensor, "Sensor");
		final flash = counter != null && counter.sinceLast < 0.2;
		if (Draw.begin3d(camera)) {
			Draw.cubeWires(0, 2.6, 0, 5, 0.8, 5, flash ? Color.get(ColorStock.LIME) : Color.get(ColorStock.DARKGREEN));
			Draw.end3d();
		}

		final dark = Color.get(ColorStock.DARKGRAY);
		Draw.text(0, "libwgf physics: point at a body, Space kicks it, R resets", 12, 12, 20, dark);
		Draw.text(0, 'through the sensor: ${counter != null ? counter.count : 0}', 12, 40, 20, dark);
		Draw.text(0, 'kicks: $kicks', 12, 64, 20, dark);
		Draw.text(0, '${Math.round(Loop.getFps())} FPS', 12, 576, 16, Color.make(0, 120, 0, 255));
	}
}

/** One body of the pile: its actor, its own tint, and where it starts. **/
class Piece {
	public final actor:Actor;
	public final tint:Int;
	public final x:Float;
	public final y:Float;
	public final z:Float;
	public final turn:Float;

	public function new(actor:Actor, tint:Int, turn:Float) {
		this.actor = actor;
		this.tint = tint;
		final at = actor.getPosition();
		x = at.x;
		y = at.y;
		z = at.z;
		this.turn = turn;
	}
}
