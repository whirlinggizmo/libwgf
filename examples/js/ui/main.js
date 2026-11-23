// A game's settings menu, in libwgf's immediate-mode UI, through the JS binding's typed layer.
//
// Each frame the menu is described again, as it is now. A widget's call takes the current
// value and returns the new one, so the settings live in plain variables:
// `settings.volume = Ui.slider("volume", settings.volume, 0, 100)`. The line under the panel
// shows them changing.
//
// - The mouse works every widget.
// - Tab and the arrow keys move the focus, and a pad's D-pad does too.
// - Enter or Space (or a pad's south button) activates the focused widget.
// - Left and Right change a focused slider, choice, or number.
// - Reset asks first, in a modal. Escape there means No.
// - Escape quits, where quitting means anything (not in a browser).
//
// The UI keeps a text field's text itself, by its id: set it with Ui.setText, read it with
// Ui.getText. The default style is used, so nothing is loaded.
//
// Its probes: ui.volume, ui.sound (1 on), ui.difficulty, ui.lives, ui.clicks (Apply pressed),
// and ui.modal (1 while the modal is open). libwgf publishes ui.focus. The Haxe example `ui`
// is the same menu.
import createWgfHost from "./wgf-host.js";
import {
    App, Color, Keyboard, KeyboardKey, Probe, Render, Ui, UiAlign, UiDirection, Window, attach,
} from "./wgf-typed.js";
import { Presentation, PresentationMode } from "./wgf-typed.js";

const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    // libwgf logs every level to stderr: only its errors are the console's
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
    // an autopilot run (tools/check_js_binding.py's), when one was handed the page
    ...(typeof globalThis.wgfAutopilot === "string" ? { wgfAutopilot: globalThis.wgfAutopilot } : {}),
});
attach(host);

const DIFFICULTIES = "Easy\nNormal\nHard"; // one label a line
const difficultyNames = DIFFICULTIES.split("\n");

// The settings: plain values the widgets show and change.
const defaults = { volume: 50, sound: true, difficulty: 1, lives: 3 };
let settings = { ...defaults };
let clicks = 0;
let confirming = false; // the "Reset?" modal is open

function init() {
    Render.setClearColor(Color.make(24, 28, 38, 255));
    Ui.setText("name", "Ada"); // the field's first text
}

function reset() {
    settings = { ...defaults };
    Ui.setText("name", "Ada");
}

// The panel: a title, a grid of the settings, then the buttons.
function settingsPanel() {
    Ui.beginPanel("settings");
    Ui.setAlign(UiAlign.CENTER, UiAlign.START);
    Ui.label("Settings", 32);

    // A grid of two columns: each setting's name, then its widget. The columns line up.
    Ui.beginGrid("form", 2);
    Ui.setAlign(UiAlign.START, UiAlign.CENTER);
    Ui.label("Volume", 0);
    settings.volume = Ui.slider("volume", settings.volume, 0, 100);
    Ui.label("Level", 0);
    Ui.progress(settings.volume / 100); // driven by the slider
    Ui.label("Sound", 0);
    settings.sound = Ui.toggle("sound", settings.sound ? "On" : "Off", settings.sound);
    Ui.label("Difficulty", 0);
    settings.difficulty = Ui.choice("difficulty", DIFFICULTIES, settings.difficulty);
    Ui.label("Lives", 0);
    settings.lives = Ui.number("lives", settings.lives, 1, 9, 1);
    Ui.label("Name", 0);
    Ui.textField("name", "your name", 16); // its text is the UI's, by its id
    Ui.endGrid();

    Ui.spacer(8);
    Ui.beginBox("buttons", UiDirection.ROW);
    Ui.setGap(12);
    if (Ui.button("apply", "Apply")) clicks++;
    if (Ui.button("reset", "Reset...")) confirming = true;
    Ui.endBox();
    Ui.endPanel();

    // Under the panel: the values as the code holds them now.
    const { volume, sound, difficulty, lives } = settings;
    Ui.label(`volume ${Math.round(volume)}   sound ${sound ? "on" : "off"}   `
        + `${difficultyNames[difficulty]}   lives ${lives}   name "${Ui.getText("name")}"   `
        + `applied ${clicks}   focus: ${Ui.getFocus() || "none"}`, 14);
}

// A modal: drawn over the dimmed screen, and the only thing that answers while it is given.
function resetModal() {
    Ui.beginModal("confirm");
    Ui.setAlign(UiAlign.CENTER, UiAlign.START);
    Ui.label("Reset every setting?", 20);
    Ui.beginBox("answers", UiDirection.ROW);
    Ui.setGap(12);
    if (Ui.button("yes", "Yes")) {
        reset();
        confirming = false;
    }
    if (Ui.button("no", "No")) confirming = false;
    Ui.endBox();
    Ui.endModal();
}

function frame() {
    if (Keyboard.isPressed(KeyboardKey.ESCAPE)) {
        if (confirming) confirming = false;
        else if (App.canQuit()) App.quit();
    }
    if (Ui.begin()) {
        settingsPanel();
        if (confirming) resetModal();
        Ui.end();
    }
    Probe.setValue("ui.volume", Math.round(settings.volume));
    Probe.setValue("ui.sound", settings.sound ? 1 : 0);
    Probe.setValue("ui.difficulty", settings.difficulty);
    Probe.setValue("ui.lives", settings.lives);
    Probe.setValue("ui.clicks", clicks);
    Probe.setValue("ui.modal", confirming ? 1 : 0);
}

Window.setTitle("libwgf ui (JS)");
// The design size, fitted to any window or page. A desktop window opens at this size.
Presentation.set(PresentationMode.FIT, 800, 600);
Window.setMsaa(true);
App.run(init, null, frame, null);
