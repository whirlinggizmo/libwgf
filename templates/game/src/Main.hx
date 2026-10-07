import wgf.*;

/**
	@NAME@, a libwgf game. `wgf serve` runs it in a browser and reloads it as you save,
	keeping its state; `wgf export` makes its web folder and desktop build.
**/
class Main {
	static var frames = 0;
	static var flips = 0;
	static var world:Stage2d;
	static var ship:Actor;

	static function main() {
		Window.setTitle("@NAME@");
		Presentation.set(PresentationMode.FIT, 800, 600); // the design, fitted to the window or the page
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Asset.setHost("assets"); // the game's files, beside its program everywhere
		Render.setClearColor(Color.make(12, 14, 22, 255));
		world = Stage2d.create();
		// an actor: a shape, which the ecs moves (its motion's spin); the program only sets its intent
		final shape = Shape2d.create();
		shape.setPolygon([18.0, 0, -12, -10, -6, 0, -12, 10]);
		shape.setOutline(2);
		shape.setColor(Color.get(ColorStock.SKYBLUE));
		ship = shape;
		ship.setParent(world);
		ship.setName("ship");
		ship.setPosition(400, 300, 0);
		ship.addComponent(Component.MOTION);
		(ship : Motion).setSpin(0, 0, 1.2);
	}

	static function frame() {
		frames++;
		if (Keyboard.isPressed(KeyboardKey.SPACE)) { // space turns the ship's spin around
			flips++;
			(ship : Motion).setSpin(0, 0, -(ship : Motion).getSpin().z);
		}
		Probe.setValue("frames", frames); // probes: what an autopilot expects on
		Probe.setValue("flips", flips);
		if (frames % 60 == 0) // save a change here while `wgf serve` runs: the next line logged is the new code's
			Log.message(LogLevel.INFO, 'hello, frame $frames');
		world.draw();
		Draw.text(0, "@NAME@", 12, 12, 20, Color.get(ColorStock.WHITE));
		if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE))
			App.quit();
	}
}
