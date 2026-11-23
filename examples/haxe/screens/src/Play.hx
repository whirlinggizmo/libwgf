import wgf.*;

/**
	The play screen: a ball bouncing and a block gliding, each on a tween that loops for ever,
	and a charge meter filled by a number's tween read each frame.

	When the pause screen covers play, the screen stack pauses play and its stage. The tweens
	of actors on it freeze with them, and so does this behavior: its onFrame isn't called.
	A number's tween belongs to no actor, so this pauses it.
**/
class Play extends ScreenBehavior {
	static inline var METER_WIDTH = 300;

	public final ball:Shape2d;
	public var charge(default, null):Tween = 0;
	final meter:Shape2d;
	final readout:Text;

	public function new(screen:Actor) {
		super(screen);
		rect(0, 0, Main.WIDTH, Main.HEIGHT, Color.make(25, 80, 45, 255));
		rect(0, 354, Main.WIDTH, 4, Color.get(ColorStock.DARKGREEN));

		// A bounce: falling faster (QUAD_IN), then back up the way it came (yoyo), slowing.
		ball = circle(220, 100, 24, Color.get(ColorStock.ORANGE));
		final bounce = Tween.move(ball, 220, 330, 0, 0.55, Easing.QUAD_IN);
		bounce.setRepeat(-1);
		bounce.setYoyo(true);

		final block = rect(440, 260, 60, 60, Color.get(ColorStock.SKYBLUE));
		block.setPivot(0.5, 0.5);
		final glide = Tween.move(block, 700, 260, 0, 1.4, Easing.SINE_IN_OUT);
		glide.setRepeat(-1);
		glide.setYoyo(true);

		rect(40, 30, METER_WIDTH, 18, Color.make(0, 0, 0, 120));
		meter = rect(40, 30, 0, 18, Color.get(ColorStock.GOLD));
		readout = label("", 40 + METER_WIDTH + 70, 39, 18, Color.get(ColorStock.RAYWHITE));
		label("P: pause    Esc: title", Main.WIDTH / 2, 410, 18, Color.get(ColorStock.LIGHTGRAY));
	}

	override function onScreenEnter(from:Actor) {
		if (from != Main.pause) { // a new game: the meter starts empty
			charge.destroy();
			charge = Tween.create(0, 1, 2.0, Easing.QUAD_IN_OUT);
			charge.setRepeat(-1);
			charge.setYoyo(true);
		}
		charge.setPaused(false);
	}

	override function onScreenExit(to:Actor) {
		charge.setPaused(true);
	}

	override function onFrame(dt:Float) {
		final level = charge.getValue();
		meter.setRectangle(METER_WIDTH * level, 18);
		readout.setString('charge ${Math.round(level * 100)}%');
	}
}
