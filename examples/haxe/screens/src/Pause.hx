import wgf.*;

/**
	The pause screen, laid over play. It isn't opaque (Main sets that), so play shows through
	its dark veil, frozen. Its heading pops in, a little past full size and back.
**/
class Pause extends ScreenBehavior {
	final heading:Text;
	var pop:Tween = 0;

	public function new(screen:Actor) {
		super(screen);
		final cx = Main.WIDTH / 2;
		rect(0, 0, Main.WIDTH, Main.HEIGHT, Color.make(0, 0, 0, 150));
		heading = label("PAUSED", cx, 190, 56, Color.get(ColorStock.RAYWHITE));
		label("P: resume    Esc: title", cx, 250, 18, Color.get(ColorStock.LIGHTGRAY));
	}

	override function onScreenEnter(from:Actor) {
		if (pop.isAlive())
			pop.destroy();
		heading.setScale(0.3, 0.3, 1);
		pop = Tween.scale(heading, 1, 1, 1, 0.35, Easing.BACK_OUT);
	}
}
