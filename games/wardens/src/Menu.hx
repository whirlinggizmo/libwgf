import wgf.*;

/**
	The menus' widgets, drawn in immediate mode from the UI art in assets/ui/: panels and
	buttons as nine-slices (`panel.png`, margins 16; `button.png`, four 190 by 49 states each
	sliced alone, margins 14), so their corners never stretch. Each frame a screen lays out its
	widgets in order; one of them has the focus, moved by the arrow keys or a pad's d-pad, and
	taken by the pointer (a finger's tap is the pointer too: libwgf's touch drives the mouse).
	A widget is chosen by a click, a tap, Enter or Space, or a pad's south button.
**/
class Menu {
	static inline var BUTTON_W = 190;
	static inline var BUTTON_H = 49;
	static inline var BUTTON_MARGIN = 14;
	static inline var PANEL_MARGIN = 16;
	public static inline var NORMAL = 0;
	public static inline var FOCUSED = 1;
	public static inline var PRESSED = 2;
	public static inline var DISABLED = 3;

	public static var focus = 0;
	static var count = 0; // widgets laid out this frame
	static var lastCount = 0;
	static var screen = -1; // which screen: the focus starts again on a new one
	static var dragging = -1; // the slider held by the pointer
	static var mouseX = 0.0;
	static var mouseY = 0.0;
	static var confirm = false;
	static var clicked = false;
	static var mouseDown = false;
	static var pointerMoved = false;
	static var horizontal = false;

	static final mouse = new Vec2();
	static final delta = new Vec2();

	public static var white = 0;
	public static var dark = 0;
	public static var light = 0;

	/** A frame's menu: `id` names the screen, `across` lays its focus out left to right (the cards). **/
	public static function begin(id:Int, across:Bool) {
		white = Color.get(ColorStock.WHITE);
		dark = Color.make(60, 40, 25, 255);
		light = Color.make(250, 240, 220, 255);
		if (id != screen) {
			screen = id;
			focus = 0;
			dragging = -1;
		}
		horizontal = across;
		lastCount = count;
		count = 0;
		Mouse.getPosition(mouse);
		Mouse.getDelta(delta);
		pointerMoved = delta.x != 0 || delta.y != 0;
		mouseX = mouse.x;
		mouseY = mouse.y;
		clicked = Mouse.isPressed(MouseButton.LEFT);
		mouseDown = Mouse.isDown(MouseButton.LEFT);
		if (!mouseDown)
			dragging = -1;
		final back = across ? KeyboardKey.LEFT : KeyboardKey.UP;
		final next = across ? KeyboardKey.RIGHT : KeyboardKey.DOWN;
		if (lastCount > 0) {
			if (Keyboard.isPressed(back) || padPressed(across ? GamepadButton.DPAD_LEFT : GamepadButton.DPAD_UP))
				focus = (focus + lastCount - 1) % lastCount;
			if (Keyboard.isPressed(next) || padPressed(across ? GamepadButton.DPAD_RIGHT : GamepadButton.DPAD_DOWN))
				focus = (focus + 1) % lastCount;
		}
		confirm = Keyboard.isPressed(KeyboardKey.ENTER) || Keyboard.isPressed(KeyboardKey.SPACE)
			|| padPressed(GamepadButton.SOUTH);
	}

	public static function padPressed(button:GamepadButton):Bool {
		for (pad in 0...4)
			if (Gamepad.isPressed(pad, button))
				return true;
		return false;
	}

	static inline function inside(x:Float, y:Float, w:Float, h:Float):Bool
		return mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + h;

	/** The next widget's index; the pointer moving over it takes the focus. **/
	static function next(x:Float, y:Float, w:Float, h:Float):Int {
		final index = count++;
		if (inside(x, y, w, h) && (pointerMoved || clicked))
			focus = index;
		return index;
	}

	public static function panel(x:Float, y:Float, w:Float, h:Float) {
		Draw.textureNineSlice(Assets.panel, 0, 0, 100, 100, PANEL_MARGIN, PANEL_MARGIN, PANEL_MARGIN, PANEL_MARGIN, x, y, w, h,
			white);
	}

	/** A button drawn in `state`, its text on it. **/
	public static function drawButton(text:String, x:Float, y:Float, w:Float, h:Float, state:Int, size:Float = 24) {
		// the pressed art is 45 rows of its cell, drawn 4 lower (Kenney's pressed button sits lower)
		final rows = state == PRESSED ? 45 : BUTTON_H;
		final top = state == PRESSED ? 4.0 : 0.0;
		Draw.textureNineSlice(Assets.button, 0, state * BUTTON_H, BUTTON_W, rows, BUTTON_MARGIN, BUTTON_MARGIN, BUTTON_MARGIN,
			BUTTON_MARGIN, x, y + top, w, h - top, white);
		final color = state == FOCUSED ? dark : state == DISABLED ? Color.make(150, 150, 150, 255) : light;
		Draw.textAligned(Assets.font, text, x + w / 2, y + top + (h - top) / 2, size, color, TextHalign.CENTER, TextValign.MIDDLE);
	}

	/** A button: true the frame it is chosen. **/
	public static function button(text:String, x:Float, y:Float, w:Float, h:Float, enabled:Bool = true):Bool {
		final index = next(x, y, w, h);
		final over = inside(x, y, w, h);
		final state = !enabled ? DISABLED : over && mouseDown ? PRESSED : focus == index ? FOCUSED : NORMAL;
		drawButton(text, x, y, w, h, state);
		if (!enabled)
			return false;
		return (clicked && over) || (confirm && focus == index);
	}

	/** A slider from 0 to 1, dragged, tapped, or moved by left and right while it has the focus; the value after. **/
	public static function slider(label:String, value:Float, x:Float, y:Float, w:Float, h:Float):Float {
		final index = next(x, y, w, h);
		final trackX = x + 24, trackW = w - 48;
		if (clicked && inside(x, y, w, h))
			dragging = index;
		if (dragging == index && mouseDown)
			value = (mouseX - trackX) / trackW;
		if (focus == index) {
			if (Keyboard.isPressed(KeyboardKey.LEFT) || Keyboard.isRepeated(KeyboardKey.LEFT) || padPressed(GamepadButton.DPAD_LEFT))
				value -= 0.05;
			if (Keyboard.isPressed(KeyboardKey.RIGHT) || Keyboard.isRepeated(KeyboardKey.RIGHT)
				|| padPressed(GamepadButton.DPAD_RIGHT))
				value += 0.05;
		}
		value = Math.round(Math.max(0, Math.min(1, value)) * 100) / 100;
		final state = focus == index ? FOCUSED : NORMAL;
		Draw.textAligned(Assets.font, '$label ${Std.int(Math.round(value * 100))}%', x + w / 2, y, 22,
			state == FOCUSED ? light : Color.make(230, 215, 190, 255), TextHalign.CENTER, TextValign.TOP);
		final barY = y + 34, barH = 16.0;
		Draw.rectangleRounded(trackX, barY, trackW, barH, 8, 0, Color.make(40, 26, 16, 255));
		Draw.rectangleRounded(trackX, barY, trackW * value, barH, 8, 0, Color.make(230, 180, 90, 255));
		final knobX = trackX + trackW * value;
		final knob = 30.0;
		drawButton("", knobX - knob / 2, barY + barH / 2 - knob / 2, knob, knob, state);
		return value;
	}

	/** A level-up card: a panel with its icon, name and text; true the frame it is picked. **/
	public static function card(card:Card, x:Float, y:Float, w:Float, h:Float):Bool {
		final index = next(x, y, w, h);
		final over = inside(x, y, w, h);
		final focused = focus == index;
		final lift = focused ? -10.0 : 0.0;
		panel(x, y + lift, w, h);
		if (focused)
			Draw.textureNineSlice(Assets.button, 0, FOCUSED * BUTTON_H, BUTTON_W, BUTTON_H, BUTTON_MARGIN, BUTTON_MARGIN,
				BUTTON_MARGIN, BUTTON_MARGIN, x + 14, y + lift + h - 64, w - 28, 50, white);
		icon(card.icon, x + w / 2 - 48, y + lift + 28, 96);
		Draw.textAligned(Assets.font, card.name, x + w / 2, y + lift + 142, 24, dark, TextHalign.CENTER, TextValign.TOP);
		wrapped(card.text, x + 24, y + lift + 184, w - 48, 18, Color.make(70, 50, 35, 255));
		if (focused)
			Draw.textAligned(Assets.font, "Pick", x + w / 2, y + lift + h - 39, 22, dark, TextHalign.CENTER, TextValign.MIDDLE);
		return (clicked && over) || (confirm && focused);
	}

	/** Icon `index` of icons.png (48 by 48, 8 a row), drawn `size` across. **/
	public static function icon(index:Int, x:Float, y:Float, size:Float, tint:Int = -1) {
		Draw.textureRegion(Assets.icons, (index % 8) * 48, Std.int(index / 8) * 48, 48, 48, x, y, size, size,
			tint == -1 ? Color.get(ColorStock.WHITE) : tint);
	}

	/** Text broken into lines no wider than `w`, centered. **/
	public static function wrapped(text:String, x:Float, y:Float, w:Float, size:Float, color:Int) {
		final words = text.split(" ");
		var line = "";
		var lineY = y;
		final measure = new Vec2();
		for (word in words) {
			final tryLine = line == "" ? word : '$line $word';
			Assets.font.measure(tryLine, size, measure);
			if (measure.x > w && line != "") {
				Draw.textAligned(Assets.font, line, x + w / 2, lineY, size, color, TextHalign.CENTER, TextValign.TOP);
				lineY += size * 1.35;
				line = word;
			} else {
				line = tryLine;
			}
		}
		if (line != "")
			Draw.textAligned(Assets.font, line, x + w / 2, lineY, size, color, TextHalign.CENTER, TextValign.TOP);
	}

	/** Text with a dark shadow under it, for the HUD over the world. **/
	public static function shadowed(text:String, x:Float, y:Float, size:Float, color:Int, h:TextHalign, v:TextValign) {
		Draw.textAligned(Assets.font, text, x + 2, y + 2, size, Color.make(0, 0, 0, 180), h, v);
		Draw.textAligned(Assets.font, text, x, y, size, color, h, v);
	}

	public static function clock(seconds:Float):String {
		final s = Std.int(seconds);
		final m = Std.int(s / 60);
		final r = s % 60;
		return '$m:${r < 10 ? "0" : ""}$r';
	}
}
