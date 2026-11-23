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
	- Reset asks first, in a modal. Escape there, or a pad's east button, means No.
	- Escape quits, where quitting means anything (not in a browser).

	A text field's text is a text buffer, made once: the field writes what is typed into it,
	and the game reads or sets it when it wants. The default style is used, so nothing is
	loaded.

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
	static var name:TextBuffer; // the name field's text
	static var log:TextBuffer; // what happened, shown in a text view under the buttons

	static function main() {
		Window.setTitle("libwgf ui (Haxe)");
		// The design size, fitted to any window or page. A desktop window opens at this size.
		Presentation.set(PresentationMode.FIT, 800, 600);
		Window.setFlags(WindowFlag.MSAA);
		Runtime.run(init, null, frame, null);
	}

	static function init() {
		Render.setClearColor(Color.make(24, 28, 38, 255));
		name = TextBuffer.createText("Ada"); // the field's first text
		log = TextBuffer.createText("Settings opened.");
	}

	static function reset() {
		volume = 50;
		sound = true;
		difficulty = 1;
		lives = 3;
		name.setText("Ada");
	}

	static function frame() {
		if (Keyboard.isPressed(KeyboardKey.ESCAPE) && !confirming && App.canQuit())
			App.quit();
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

	// The panel: a title, a grid of the settings, then the buttons.
	static function settings() {
		Ui.beginPanel("settings");
		Ui.setAlign(UiAlign.CENTER, UiAlign.START);
		Ui.label("Settings", 32);

		// A grid of two columns: each setting's name, then its widget. The columns line up.
		Ui.beginGrid("form", 2);
		Ui.setAlign(UiAlign.START, UiAlign.CENTER);
		Ui.label("Volume", 0);
		volume = Ui.slider("volume", volume, 0, 100);
		Ui.label("Level", 0);
		Ui.progress(volume / 100); // driven by the slider
		Ui.label("Sound", 0);
		sound = Ui.toggle("sound", sound ? "On" : "Off", sound);
		Ui.label("Difficulty", 0);
		difficulty = Ui.choice("difficulty", DIFFICULTIES, difficulty);
		Ui.label("Lives", 0);
		lives = Std.int(Ui.number("lives", lives, 1, 9, 1)); // a number is a Float
		Ui.label("Name", 0);
		Ui.textField("name", name, "your name", 16); // writes what is typed into `name`
		Ui.endGrid();

		Ui.spacer(8);
		Ui.beginBox("buttons", UiDirection.ROW);
		Ui.setGap(12);
		if (Ui.button("apply", "Apply")) {
			clicks++;
			log.append('\nApplied: volume ${Math.round(volume)}, ${difficultyNames[difficulty]}, $lives lives.');
		}
		if (Ui.button("reset", "Reset..."))
			confirming = true;
		Ui.endBox();
		// The log: its text is the buffer's, given as a handle, so a frame passes no text. It
		// follows its end as lines are added.
		Ui.textView("log", log, 0, 72, true);
		Ui.endPanel();

		// Under the panel: the values as the code holds them now.
		final focus = Ui.getFocus();
		Ui.label('volume ${Math.round(volume)}   sound ${sound ? "on" : "off"}   '
			+ '${difficultyNames[difficulty]}   lives $lives   name "${name.getText()}"   '
			+ 'applied $clicks   focus: ${focus == "" ? "none" : focus}', 14);
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
			log.append("\nReset.");
			confirming = false;
		}
		if (Ui.button("no", "No"))
			confirming = false;
		Ui.endBox();
		if (Ui.isModalCancelled()) // Escape, or a pad's east button
			confirming = false;
		Ui.endModal();
	}
}
