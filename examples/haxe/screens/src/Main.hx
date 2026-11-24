import wgf.*;

/**
	Screens and tweens, in Haxe: a title, a play screen, and a pause screen over play.

	This is the C example `ecs-screens` and the JS example `screens`, written the Haxe way.
	The screens are a stack (`Screen`). Each screen is an actor with a behavior on it
	(Title.hx, Play.hx, Pause.hx), which builds its own 2D stage and hears when its screen
	comes to the top or leaves it. Tweens (`Tween`) move, turn, scale, and count, each on
	its own easing.

	- Enter, or a click or tap: from the title to play, through an iris.
	- P: pause, and resume. The pause screen opens at once, over play, and freezes it.
	- Escape: back to the title, through a fade. On the title, Escape quits, where quitting
	  means anything (not in a browser).

	Run it with `wgf serve` or `wgf run` from this folder.
**/
class Main {
	public static inline var WIDTH = 800;
	public static inline var HEIGHT = 450;

	public static var title:Actor;
	public static var play:Actor;
	public static var pause:Actor;

	static function main() {
		Window.setTitle("libwgf screens (Haxe)");
		// The design size, fitted to any window or page. A desktop window opens at this size.
		Presentation.set(PresentationMode.FIT, WIDTH, HEIGHT);
		Window.setFlags(WindowFlag.MSAA);
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Render.setClearColor(Color.get(ColorStock.BLACK));
		Behavior.register("Title", actor -> new Title(actor));
		Behavior.register("Play", actor -> new Play(actor));
		Behavior.register("Pause", actor -> new Pause(actor));
		title = makeScreen("Title");
		play = makeScreen("Play");
		pause = makeScreen("Pause");
		Screen.setOpaque(pause, false); // play shows through it

		Screen.setTransitionColor(Color.make(12, 12, 20, 255));
		Screen.setTransitionEase(Easing.CUBIC_IN_OUT);
		Screen.push(title); // no transition is set yet, so this happens at once
	}

	/** A screen with the behavior `name` on it, which fills it. **/
	static function makeScreen(name:String):Actor {
		final screen = Screen.create();
		screen.addBehavior(name);
		return screen;
	}

	static function frame() {
		if (!Screen.isChanging()) // a change is refused while one runs anyway
			handleInput();
		Screen.draw();

		publishProbes();
	}

	static var wasFrozen = false;
	static var frozenY = 0.0;
	static var frozenCharge = 0.0;

	/** The numbers the autopilot checks (autopilot/smoke.autopilot). **/
	static function publishProbes() {
		final top = Screen.getTop();
		Probe.setValue("screens.current", top == title ? 0 : top == play ? 1 : top == pause ? 2 : -1);
		final game = Behavior.get(play, Play);
		if (game == null)
			return; // made at the runtime's first poll of the ecs's events
		// While play is frozen, how far its ball and its meter moved since: it should stay 0.
		final y = game.ball.getPosition().y, charge = game.charge.getValue();
		final frozen = play.isFrozen();
		if (frozen && !wasFrozen) {
			frozenY = y;
			frozenCharge = charge;
		}
		wasFrozen = frozen;
		Probe.setValue("screens.charge", charge);
		Probe.setValue("screens.paused", frozen ? 1 : 0);
		Probe.setValue("screens.drift", frozen ? Math.abs(y - frozenY) + Math.abs(charge - frozenCharge) : 0);
	}

	static function handleInput() {
		final top = Screen.getTop();
		if (top == title) {
			if (Keyboard.isPressed(KeyboardKey.ENTER) || Mouse.isPressed(MouseButton.LEFT)) {
				Screen.setTransition(Transition.IRIS, 0.8);
				Screen.replace(play);
			} else if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE)) {
				App.quit();
			}
		} else if (Keyboard.isPressed(KeyboardKey.P)) {
			Screen.setTransition(Transition.NONE, 0); // a pause opens and closes at once
			if (top == play)
				Screen.push(pause);
			else
				Screen.pop();
		} else if (Keyboard.isPressed(KeyboardKey.ESCAPE)) {
			if (top == pause) {
				Screen.setTransition(Transition.NONE, 0);
				Screen.pop(); // at once, so the replace below is not refused
			}
			Screen.setTransition(Transition.FADE, 0.6);
			Screen.replace(title);
		}
	}
}
