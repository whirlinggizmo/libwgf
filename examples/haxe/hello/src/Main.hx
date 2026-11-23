import wgf.*;

/**
	Hello, in Haxe: a window with 2D shapes, text, and input.

	This is the C example `app-hello` and the JS example `hello`, written the Haxe way.
	It shows the smallest whole program: set the window up, then hand libwgf an `init`
	and a `frame` function.

	- A marker follows the mouse.
	- Typing shows what was typed. Backspace removes the last character.
	- Escape quits, where quitting means anything (not in a browser).

	Run it with `wgf serve` or `wgf run` from this folder.
**/
class Main {
	static var typed = "";
	static var frames = 0;
	static final mouse = new Vec2(0, 0);

	static function main() {
		Window.setTitle("libwgf hello (Haxe)");
		// The design size, fitted to any window or page. A desktop window opens at this size.
		Presentation.set(PresentationMode.FIT, 800, 600);
		Window.setFlags(WindowFlag.MSAA);
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Render.setClearColor(Color.get(ColorStock.RAYWHITE));
	}

	static function frame() {
		frames++;
		Mouse.getPosition(mouse); // fills `mouse`, so no vector is made each frame
		if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE))
			App.quit();
		if (Keyboard.isPressed(KeyboardKey.BACKSPACE) && typed.length > 0)
			typed = typed.substr(0, typed.length - 1);
		final chars = Input.getChars();
		if (typed.length + chars.length < 64)
			typed += chars;

		// Immediate-mode drawing: each call draws once, this frame.
		Draw.rectangle(40, 40, 200, 120, Color.get(ColorStock.SKYBLUE));
		Draw.rectangleLines(40, 40, 200, 120, 1, Color.get(ColorStock.DARKBLUE));
		Draw.line(40, 200, 240, 320, 1, Color.get(ColorStock.RED));
		Draw.triangle(320, 60, 280, 180, 360, 180, Color.get(ColorStock.GOLD));
		Draw.circle(440, 120, 60, Color.get(ColorStock.PURPLE));
		Draw.circleLines(440, 120, 60, 1, Color.get(ColorStock.BLACK));
		Draw.circle(mouse.x, mouse.y, 8, Color.get(ColorStock.MAROON));

		// Font 0 is the built-in font.
		Draw.text(0, "libwgf", 40, 360, 32, Color.get(ColorStock.DARKGRAY));
		Draw.text(0, typed == "" ? "type something" : typed, 40, 410, 16,
			Color.get(typed == "" ? ColorStock.GRAY : ColorStock.DARKBLUE));
		if (App.canQuit())
			Draw.text(0, "press Esc to quit", 40, 440, 16, Color.get(ColorStock.GRAY));
		Draw.text(0, '${Math.round(Loop.getFps())} FPS', 40, 12, 16, Color.make(0, 255, 0, 255));

		// Probes publish numbers an autopilot run can check (autopilot/smoke.autopilot).
		Probe.setValue("hello.frames", frames);
		Probe.setValue("hello.typed", typed.length);
	}
}
