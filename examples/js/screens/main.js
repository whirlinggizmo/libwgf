// Screens and tweens, in JavaScript: a title, a play screen, and a pause screen over play.
//
// This is the C example ecs-screens and the Haxe example screens, through the JS binding's
// typed layer. The screens are a stack (Screen). Each screen is an actor with a behavior on it
// (screens.js: Title, Play, Pause), which builds its own 2D stage and hears when its screen
// comes to the top or leaves it. Tweens (Tween) move, turn, scale, and count, each on its own
// easing.
//
// - Enter, or a click or tap: from the title to play, through an iris.
// - P: pause, and resume. The pause screen opens at once, over play, and freezes it.
// - Escape: back to the title, through a fade. On the title, Escape quits, where quitting
//   means anything (not in a browser).
import createWgfHost from "./wgf-host.js";
import {
    Actor, App, Behavior, Color, ColorStock, Easing, Keyboard, KeyboardKey, Mouse, MouseButton, Presentation,
    PresentationMode, Probe, Render, Screen, Transition, Tween, Window, attach,
} from "./wgf-typed.js";
import { HEIGHT, Pause, Play, Title, WIDTH } from "./screens.js";

const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    // libwgf logs every level to stderr: only its errors are the console's
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
    // an autopilot run (tools/check_js_binding.py's), when one was handed the page
    ...(typeof globalThis.wgfAutopilot === "string" ? { wgfAutopilot: globalThis.wgfAutopilot } : {}),
});
attach(host);

let title = 0, play = 0, pause = 0;

function init() {
    Render.setClearColor(Color.get(ColorStock.BLACK));
    Behavior.register("Title", Title);
    Behavior.register("Play", Play);
    Behavior.register("Pause", Pause);
    title = makeScreen("Title");
    play = makeScreen("Play");
    pause = makeScreen("Pause");
    Screen.setOpaque(pause, false); // play shows through it

    Screen.setTransitionColor(Color.make(12, 12, 20, 255));
    Screen.setTransitionEase(Easing.CUBIC_IN_OUT);
    Screen.push(title); // no transition is set yet, so this happens at once
}

// A screen with the behavior `name` on it, which fills it.
function makeScreen(name) {
    const screen = Screen.create();
    Actor.addBehavior(screen, name);
    return screen;
}

function frame() {
    if (!Screen.isChanging()) handleInput(); // a change is refused while one runs anyway
    Screen.draw();
    publishProbes();
}

// The numbers the autopilot checks (smoke.autopilot). They are set here, not in a behavior,
// as play's behavior gets no onFrame while it is frozen.
const frozenAt = { y: 0, charge: 0, was: false };
const at = { x: 0, y: 0, z: 0 };

function publishProbes() {
    const top = Screen.getTop();
    Probe.setValue("screens.current", top === title ? 0 : top === play ? 1 : top === pause ? 2 : -1);
    const game = Behavior.of(play, "Play");
    if (game === null) return; // made at the run's first poll of the ecs's events
    // While play is frozen, how far its ball and its meter moved since: it should stay 0.
    const y = Actor.getPosition(game.ball, at).y, charge = Tween.getValue(game.charge);
    const frozen = Actor.isFrozen(play);
    if (frozen && !frozenAt.was) Object.assign(frozenAt, { y, charge });
    frozenAt.was = frozen;
    Probe.setValue("screens.charge", charge);
    Probe.setValue("screens.paused", frozen ? 1 : 0);
    Probe.setValue("screens.drift", frozen ? Math.abs(y - frozenAt.y) + Math.abs(charge - frozenAt.charge) : 0);
}

function handleInput() {
    const top = Screen.getTop();
    if (top === title) {
        if (Keyboard.isPressed(KeyboardKey.ENTER) || Mouse.isPressed(MouseButton.LEFT)) {
            Screen.setTransition(Transition.IRIS, 0.8);
            Screen.replace(play);
        } else if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE)) {
            App.quit();
        }
    } else if (Keyboard.isPressed(KeyboardKey.P)) {
        Screen.setTransition(Transition.NONE, 0); // a pause opens and closes at once
        if (top === play) Screen.push(pause);
        else Screen.pop();
    } else if (Keyboard.isPressed(KeyboardKey.ESCAPE)) {
        if (top === pause) {
            Screen.setTransition(Transition.NONE, 0);
            Screen.pop(); // at once, so the replace below is not refused
        }
        Screen.setTransition(Transition.FADE, 0.6);
        Screen.replace(title);
    }
}

Window.setTitle("libwgf screens (JS)");
// The design size, fitted to any window or page. A desktop window opens at this size.
Presentation.set(PresentationMode.FIT, WIDTH, HEIGHT);
Window.setMsaa(true);
App.run(init, null, frame, null);
