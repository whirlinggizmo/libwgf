import wgf.*;

/**
	The title screen. Each time it comes to the top, its heading drops in on a bounce and
	its ring springs open. A square turns on a looping tween the whole time.
**/
class Title extends ScreenBehavior {
	static inline var HEADING_Y = 120;

	final heading:Text;
	final ring:Shape2d;
	var drop:Tween = 0;
	var spring:Tween = 0;

	public function new(screen:Actor) {
		super(screen);
		final cx = Main.WIDTH / 2;
		rect(0, 0, Main.WIDTH, Main.HEIGHT, Color.make(30, 45, 100, 255));
		heading = label("SCREENS", cx, HEADING_Y, 64, Color.get(ColorStock.GOLD));
		label("a screen stack, and tweens", cx, 180, 20, Color.get(ColorStock.SKYBLUE));

		ring = circle(cx, 290, 56, Color.get(ColorStock.SKYBLUE));
		ring.setOutline(6);
		final square = rect(cx, 290, 50, 50, Color.get(ColorStock.GOLD));
		square.setPivot(0.5, 0.5);
		// A quarter turn, again and again: the square looks the same at each end.
		Tween.turn(square, 0, 0, Math.PI / 2, 0.9, Easing.CUBIC_IN_OUT).setRepeat(-1);

		label("Enter or click: play" + (App.canQuit() ? "    Esc: quit" : ""), cx, 410, 18,
			Color.get(ColorStock.LIGHTGRAY));
	}

	override function onScreenEnter(from:Actor) {
		if (drop.isAlive()) // the last time's, if it still runs
			drop.destroy();
		if (spring.isAlive())
			spring.destroy();
		// An actor's tween starts from where the actor is, so put each where it starts.
		heading.setPosition(Main.WIDTH / 2, -60, 0);
		ring.setScale(0.01, 0.01, 1);
		drop = Tween.move(heading, Main.WIDTH / 2, HEADING_Y, 0, 1.2, Easing.BOUNCE_OUT);
		spring = Tween.scale(ring, 1, 1, 1, 1.0, Easing.ELASTIC_OUT);
		spring.setDelay(0.4);
	}

	override function onFrame(dt:Float) {
		Probe.setValue("screens.heading_y", heading.getPosition().y);
	}
}
