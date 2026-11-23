import wgf.*;

/**
	A game's settings menu, in libwgf's immediate-mode UI.

	Each frame the menu is described again, as it is now. A widget's call takes the
	current value and returns the new one, so the settings live in plain variables:
	`volume = Ui.slider("volume", volume, 0, 100)`. The line under the panel shows them
	changing.

	- The mouse works every widget.
	- Tab and the arrow keys move the focus, and a pad's D-pad does too.
	- Enter or Space (or a pad's south button) activates the focused widget.
	- Left and Right change a focused slider, choice, or number.
	- Reset asks first, in a modal. Escape there means No.
	- Escape quits, where quitting means anything (not in a browser).

	The UI keeps a text field's text itself, by its id: set it with `Ui.setText`, read it
	with `Ui.getText`. The default style is used, so nothing is loaded.

	Its probes: ui.volume, ui.sound (1 on), ui.difficulty, ui.lives, ui.clicks (Apply
	pressed), and ui.modal (1 while the modal is open). libwgf publishes ui.focus.
**/
class Main {
	static inline var DIFFICULTIES = "Easy\nNormal\nHard"; // one label a line
	static final difficultyNames = DIFFICULTIES.split("\n");

	// The settings: plain variables the widgets show and change.
	static var volume = 50.0;
	static var sound = true;
	static var difficulty = 1;
	static var lives = 3;
	static var clicks = 0;
	static var confirming = false; // the "Reset?" modal is open

	static function main() {
		Window.setTitle("libwgf ui (Haxe)");
		// The design size, fitted to any window or page. Window.setSize would fix the canvas at that
		// size on the web, unscaled, so it is left to the presentation.
		Presentation.set(PresentationMode.FIT, 800, 600);
		Window.setMsaa(true);
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Render.setClearColor(Color.make(24, 28, 38, 255));
		Ui.setText("name", "Ada"); // the field's first text
	}

	static function reset() {
		volume = 50;
		sound = true;
		difficulty = 1;
		lives = 3;
		Ui.setText("name", "Ada");
	}

	static function frame() {
		if (Keyboard.isPressed(KeyboardKey.ESCAPE)) {
			if (confirming)
				confirming = false;
			else if (App.canQuit())
				App.quit();
		}
		if (Ui.begin()) {
			settings();
			if (confirming)
				resetModal();
			Ui.end();
		}
		Probe.setValue("ui.volume", Math.round(volume));
		Probe.setValue("ui.sound", sound ? 1 : 0);
		Probe.setValue("ui.difficulty", difficulty);
		Probe.setValue("ui.lives", lives);
		Probe.setValue("ui.clicks", clicks);
		Probe.setValue("ui.modal", confirming ? 1 : 0);
	}

	// The panel: a title, a row a setting, then the buttons.
	static function settings() {
		Ui.beginPanel("settings");
		Ui.setAlign(UiAlign.CENTER, UiAlign.START);
		Ui.label("Settings", 32);

		row("Volume");
		volume = Ui.slider("volume", volume, 0, 100);
		Ui.endBox();
		row("Level");
		Ui.progress(volume / 100); // driven by the slider
		Ui.endBox();
		row("Sound");
		sound = Ui.toggle("sound", sound ? "On" : "Off", sound);
		Ui.endBox();
		row("Difficulty");
		difficulty = Ui.choice("difficulty", DIFFICULTIES, difficulty);
		Ui.endBox();
		row("Lives");
		lives = Std.int(Ui.number("lives", lives, 1, 9, 1)); // a number is a Float
		Ui.endBox();
		row("Name");
		Ui.textField("name", "your name", 16); // its text is the UI's, by its id
		Ui.endBox();

		Ui.spacer(8);
		Ui.beginBox("buttons", UiDirection.ROW);
		Ui.setGap(12);
		if (Ui.button("apply", "Apply"))
			clicks++;
		if (Ui.button("reset", "Reset..."))
			confirming = true;
		Ui.endBox();
		Ui.endPanel();

		// Under the panel: the values as the code holds them now.
		final focus = Ui.getFocus();
		Ui.label('volume ${Math.round(volume)}   sound ${sound ? "on" : "off"}   '
			+ '${difficultyNames[difficulty]}   lives $lives   name "${Ui.getText("name")}"   '
			+ 'applied $clicks   focus: ${focus == "" ? "none" : focus}', 14);
	}

	// Begins a row: the setting's name in a column of fixed width, its widget beside it.
	// The caller adds the widget, then closes the row with Ui.endBox.
	static function row(name:String) {
		Ui.beginBox("", UiDirection.ROW);
		Ui.setWidth(UiSizing.GROW, 0);
		Ui.setGap(16);
		Ui.setAlign(UiAlign.START, UiAlign.CENTER);
		Ui.beginBox("", UiDirection.COLUMN);
		Ui.setWidth(UiSizing.FIXED, 100);
		Ui.label(name, 0);
		Ui.endBox();
	}

	// A modal: drawn over the dimmed screen, and the only thing that answers while it is given.
	static function resetModal() {
		Ui.beginModal("confirm");
		Ui.setAlign(UiAlign.CENTER, UiAlign.START);
		Ui.label("Reset every setting?", 20);
		Ui.beginBox("answers", UiDirection.ROW);
		Ui.setGap(12);
		if (Ui.button("yes", "Yes")) {
			reset();
			confirming = false;
		}
		if (Ui.button("no", "No"))
			confirming = false;
		Ui.endBox();
		Ui.endModal();
	}
}
