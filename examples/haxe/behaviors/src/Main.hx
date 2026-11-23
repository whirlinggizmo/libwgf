import wgf.*;

/**
	Behaviors, in Haxe: a 2D scene whose actors are driven by components and behaviors.

	Components are data the ecs runs in C: motion moves an actor, bounds wrap it around the
	screen, a lifetime ends it, and a collider finds what it overlaps. Behaviors are this
	program's classes, one per kind of actor, extending `wgf.Behavior`. They only set intent.

	- A spinning square in the middle (Spawner) launches a ball every 0.4 seconds, from its tick.
	- A ball (Ball) flies by its motion, wraps at the edges, and lives 6 seconds. It bounces off
	  a bumper it meets, and takes the bumper's color.
	- A bumper (Bumper) spins by its motion. A ball that meets it counts a hit, flashes it, and
	  leaves a spark (Spark), a ring that grows and fades until its lifetime ends it.
	- Escape quits, where quitting means anything (not in a browser).

	The scene draws from libwgf's seeded random numbers (wgf.Random), so every run is the same.
	Its probes, for autopilot/smoke.autopilot: behaviors.hits, behaviors.balls, and
	behaviors.actors (every simulated actor).

	Run it with `wgf serve` or `wgf run` from this folder.
**/
class Main {
	public static inline var WIDTH = 800;
	public static inline var HEIGHT = 600;
	// collider layers, as bits: a ball's mask has bumpers, and a bumper's has balls
	public static inline var BUMPER_LAYER = 1;
	public static inline var BALL_LAYER = 2;

	public static var world:Stage2d;
	public static var hits = 0;

	static function main() {
		Window.setTitle("libwgf behaviors (Haxe)");
		Window.setMsaa(true);
		// the scene's 800 by 600, fitted to any window, so the balls wrap at its edges
		Presentation.set(PresentationMode.FIT, WIDTH, HEIGHT);
		Runtime.run(init, tick, frame, null);
	}

	static function init() {
		Asset.setHost("assets");
		Render.setClearColor(Color.make(14, 16, 26, 255));
		Random.setSeed(7);
		world = Stage2d.create();

		// Each name an actor's behavior has is made as this class, at the next tick or frame.
		Behavior.register("Spawner", Spawner.new);
		Behavior.register("Ball", Ball.new);
		Behavior.register("Bumper", Bumper.new);
		Behavior.register("Spark", Spark.new);

		bumper(220, 170, 0.7, ColorStock.ORANGE);
		bumper(580, 170, -0.5, ColorStock.SKYBLUE);
		bumper(220, 430, -0.6, ColorStock.LIME);
		bumper(580, 430, 0.8, ColorStock.PINK);

		final spawner = shape(WIDTH / 2, HEIGHT / 2, Color.get(ColorStock.GOLD));
		(spawner : Shape2d).setRectangle(36, 36);
		(spawner : Shape2d).setOutline(3);
		spawner.addComponent(Component.MOTION);
		(spawner : Motion).setSpin(0, 0, 2);
		spawner.addBehavior("Spawner");
	}

	/**
		A shape on the world at (x, y), in `color`: what every actor here is. It is returned as
		an actor, which casts to each component's calls: `(actor : Motion)`, `(actor : Shape2d)`.
	**/
	public static function shape(x:Float, y:Float, color:Int):Actor {
		final shape = Shape2d.create();
		shape.setParent(world);
		shape.setPosition(x, y, 0);
		shape.setColor(color);
		return shape;
	}

	/** A bar that spins in place, `spin` radians a second, and meets balls. **/
	static function bumper(x:Float, y:Float, spin:Float, stock:ColorStock) {
		final bar = shape(x, y, Color.get(stock));
		(bar : Shape2d).setRectangle(150, 22);
		(bar : Shape2d).setCornerRadius(8);
		bar.addComponent(Component.MOTION);
		(bar : Motion).setSpin(0, 0, spin);
		bar.addComponent(Component.COLLIDER);
		(bar : Collider).setRectangle(150, 22); // it turns with its actor
		(bar : Collider).setLayer(BUMPER_LAYER);
		(bar : Collider).setMask(BALL_LAYER);
		bar.addBehavior("Bumper");
	}

	static function tick() {
		Probe.setValue("behaviors.hits", hits);
		Probe.setValue("behaviors.balls", Actor.countWithBehavior("Ball"));
		Probe.setValue("behaviors.actors", Actor.getCount());
	}

	static function frame() {
		if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE))
			App.quit();
		world.draw();
		Draw.text(0, 'hits: $hits', 16, 14, 28, Color.get(ColorStock.RAYWHITE));
		Draw.text(0, 'balls: ${Actor.countWithBehavior("Ball")}', 16, 48, 18, Color.get(ColorStock.LIGHTGRAY));
		Draw.textAligned(0, "behaviors (Haxe)", WIDTH - 16, 16, 18, Color.get(ColorStock.GRAY), TextHalign.RIGHT,
			TextValign.TOP);
	}
}
