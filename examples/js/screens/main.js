// Screens and tweens, in JavaScript: a title, a play screen, and a pause screen over play.
//
// This is the C example ecs-screens and the Haxe example screens, through the JS binding's
// typed layer. The screens are a stack (Screen). Each screen is an actor, with a plain object
// here (screens.js) that builds its 2D stage and hears when its screen comes to the top or
// leaves it. Tweens (Tween) move, turn, scale, and count, each on its own easing.
//
// - Enter, or a click or tap: from the title to play, through an iris.
// - P: pause, and resume. The pause screen opens at once, over play, and freezes it.
// - Escape: back to the title, through a fade. On the title, Escape quits, where quitting
//   means anything (not in a browser).
import createWgfHost from "./wgf-host.js";
import {
    Actor, App, Color, ColorStock, Easing, Keyboard, KeyboardKey, Mouse, MouseButton, Probe, Render, Screen,
    Transition, Tween, Window, World, WorldEvent, attach,
} from "./wgf-typed.js";
import { Presentation, PresentationMode } from "./wgf-typed.js";
import { HEIGHT, WIDTH, makePause, makePlay, makeTitle } from "./screens.js";

const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    // libwgf logs every level to stderr: only its errors are the console's
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
    // an autopilot run (tools/check_js_binding.py's), when one was handed the page
    ...(typeof globalThis.wgfAutopilot === "string" ? { wgfAutopilot: globalThis.wgfAutopilot } : {}),
});
attach(host);

let title, play, pause;
const byActor = new Map(); // a screen's actor -> its object
const events = new Int32Array(4 * 64);

function init() {
    Render.setClearColor(Color.get(ColorStock.BLACK));
    title = makeTitle();
    pause = makePause();
    play = makePlay(pause.actor);
    for (const screen of [title, play, pause]) byActor.set(screen.actor, screen);
    Screen.setOpaque(pause.actor, false); // play shows through it

    Screen.setTransitionColor(Color.make(12, 12, 20, 255));
    Screen.setTransitionEase(Easing.CUBIC_IN_OUT);
    Screen.push(title.actor); // no transition is set yet, so this happens at once
}

// Each change of the stack comes as a world event: the screen, then the other one. They are
// taken each tick and each frame, so a screen hears of a change before the next tick steps
// its tweens.
function tellScreens() {
    let n;
    while ((n = World.takeEvents(events)) > 0) {
        for (let i = 0; i < n; i += 4) {
            const screen = byActor.get(events[i + 1]), other = events[i + 2];
            if (screen === undefined) continue;
            if (events[i] === WorldEvent.SCREEN_ENTER) screen.enter(other);
            else if (events[i] === WorldEvent.SCREEN_EXIT) screen.exit(other);
        }
    }
}

function handleInput() {
    const top = Screen.getTop();
    if (top === title.actor) {
        if (Keyboard.isPressed(KeyboardKey.ENTER) || Mouse.isPressed(MouseButton.LEFT)) {
            Screen.setTransition(Transition.IRIS, 0.8);
            Screen.replace(play.actor);
        } else if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE)) {
            App.quit();
        }
    } else if (Keyboard.isPressed(KeyboardKey.P)) {
        Screen.setTransition(Transition.NONE, 0); // a pause opens and closes at once
        if (top === play.actor) Screen.push(pause.actor);
        else Screen.pop();
    } else if (Keyboard.isPressed(KeyboardKey.ESCAPE)) {
        if (top === pause.actor) {
            Screen.setTransition(Transition.NONE, 0);
            Screen.pop(); // at once, so the replace below is not refused
        }
        Screen.setTransition(Transition.FADE, 0.6);
        Screen.replace(title.actor);
    }
}

// The numbers the autopilot checks (smoke.autopilot).
const frozenAt = { y: 0, charge: 0, was: false };
const at = { x: 0, y: 0, z: 0 };

function publishProbes() {
    const top = Screen.getTop();
    Probe.setValue("screens.current", top === title.actor ? 0 : top === play.actor ? 1 : top === pause.actor ? 2 : -1);
    // While play is frozen, how far its ball and its meter moved since: it should stay 0.
    const y = Actor.getPosition(play.ball, at).y, charge = Tween.getValue(play.charge);
    const frozen = Actor.isFrozen(play.actor);
    if (frozen && !frozenAt.was) Object.assign(frozenAt, { y, charge });
    frozenAt.was = frozen;
    Probe.setValue("screens.charge", charge);
    Probe.setValue("screens.paused", frozen ? 1 : 0);
    Probe.setValue("screens.drift", frozen ? Math.abs(y - frozenAt.y) + Math.abs(charge - frozenAt.charge) : 0);
}

function frame() {
    tellScreens();
    // A screen under the top one is frozen: its own code waits, as its actors do.
    for (const screen of byActor.values()) {
        if (!Actor.isFrozen(screen.actor)) screen.frame();
    }
    if (!Screen.isChanging()) handleInput(); // a change is refused while one runs anyway
    Screen.draw();
    publishProbes();
}

Window.setTitle("libwgf screens (JS)");
// The design size, fitted to any window or page. Window.setSize would fix the canvas at that
// size on the web, unscaled, so it is left to the presentation.
Presentation.set(PresentationMode.FIT, WIDTH, HEIGHT);
Window.setMsaa(true);
App.run(init, tellScreens, frame, null);
