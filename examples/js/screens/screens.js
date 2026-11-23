// The three screens. Each is a plain object: its screen's actor, and what it does when its
// screen comes to the top (enter), leaves it (exit), and each frame it isn't frozen (frame).
// main.js calls them. Every actor of a screen hangs under its 2D stage, so the screen
// switches, pauses, and draws them together.
import {
    Actor, App, Color, ColorStock, Easing, Probe, Screen, Shape2d, Stage2d, Text, TextHalign, TextValign, Tween,
} from "./wgf-typed.js";

export const WIDTH = 800;
export const HEIGHT = 450;
const CX = WIDTH / 2;

// A screen with its own 2D stage, and calls that put shapes and text on that stage.
function makeScreen() {
    const actor = Screen.create(), stage = Stage2d.create();
    Screen.addStage(actor, stage);
    const place = (shape, x, y, color) => {
        Shape2d.setColor(shape, color);
        Actor.setPosition(shape, x, y, 0);
        Actor.setParent(shape, stage);
        return shape;
    };
    return {
        actor,
        rect(x, y, width, height, color) {
            const shape = Shape2d.create();
            Shape2d.setRectangle(shape, width, height);
            return place(shape, x, y, color);
        },
        circle(x, y, radius, color) {
            const shape = Shape2d.create();
            Shape2d.setCircle(shape, radius);
            return place(shape, x, y, color);
        },
        // A line of text in the built-in font (font 0), centered on x, y.
        label(string, x, y, size, color) {
            const text = Text.create(0);
            Text.setString(text, string);
            Text.setFontSize(text, size);
            Text.setColor(text, color);
            Text.setAlign(text, TextHalign.CENTER, TextValign.MIDDLE);
            Actor.setPosition(text, x, y, 0);
            Actor.setParent(text, stage);
            return text;
        },
        enter(from) {},
        exit(to) {},
        frame() {},
    };
}

// Lets go of a tween that may have ended already.
const stop = (tween) => {
    if (Tween.isAlive(tween)) Tween.destroy(tween);
};

// The title. Each time it comes to the top, its heading drops in on a bounce and its ring
// springs open. A square turns on a looping tween the whole time.
export function makeTitle() {
    const HEADING_Y = 120;
    const screen = makeScreen();
    screen.rect(0, 0, WIDTH, HEIGHT, Color.make(30, 45, 100, 255));
    const heading = screen.label("SCREENS", CX, HEADING_Y, 64, Color.get(ColorStock.GOLD));
    screen.label("a screen stack, and tweens", CX, 180, 20, Color.get(ColorStock.SKYBLUE));

    const ring = screen.circle(CX, 290, 56, Color.get(ColorStock.SKYBLUE));
    Shape2d.setOutline(ring, 6);
    const square = screen.rect(CX, 290, 50, 50, Color.get(ColorStock.GOLD));
    Shape2d.setPivot(square, 0.5, 0.5);
    // A quarter turn, again and again: the square looks the same at each end.
    Tween.setRepeat(Tween.turn(square, 0, 0, Math.PI / 2, 0.9, Easing.CUBIC_IN_OUT), -1);

    screen.label(`Enter or click: play${App.canQuit() ? "    Esc: quit" : ""}`, CX, 410, 18,
        Color.get(ColorStock.LIGHTGRAY));

    let drop = 0, spring = 0;
    screen.enter = () => {
        stop(drop); // the last time's, if it still runs
        stop(spring);
        // An actor's tween starts from where the actor is, so put each where it starts.
        Actor.setPosition(heading, CX, -60, 0);
        Actor.setScale(ring, 0.01, 0.01, 1);
        drop = Tween.move(heading, CX, HEADING_Y, 0, 1.2, Easing.BOUNCE_OUT);
        spring = Tween.scale(ring, 1, 1, 1, 1.0, Easing.ELASTIC_OUT);
        Tween.setDelay(spring, 0.4);
    };
    screen.frame = () => Probe.setValue("screens.heading_y", Actor.getPosition(heading).y);
    return screen;
}

// The play screen: a ball bouncing and a block gliding, each on a tween that loops for ever,
// and a charge meter filled by a number's tween read each frame.
//
// When the pause screen covers play, the screen stack pauses play and its stage. The tweens
// of actors on it freeze with them, and main.js doesn't call its frame. A number's tween
// belongs to no actor, so this pauses it. `pause` is the pause screen's actor.
export function makePlay(pause) {
    const METER_WIDTH = 300;
    const screen = makeScreen();
    screen.rect(0, 0, WIDTH, HEIGHT, Color.make(25, 80, 45, 255));
    screen.rect(0, 354, WIDTH, 4, Color.get(ColorStock.DARKGREEN));

    // A bounce: falling faster (QUAD_IN), then back up the way it came (yoyo), slowing.
    screen.ball = screen.circle(220, 100, 24, Color.get(ColorStock.ORANGE));
    const bounce = Tween.move(screen.ball, 220, 330, 0, 0.55, Easing.QUAD_IN);
    Tween.setRepeat(bounce, -1);
    Tween.setYoyo(bounce, true);

    const block = screen.rect(440, 260, 60, 60, Color.get(ColorStock.SKYBLUE));
    Shape2d.setPivot(block, 0.5, 0.5);
    const glide = Tween.move(block, 700, 260, 0, 1.4, Easing.SINE_IN_OUT);
    Tween.setRepeat(glide, -1);
    Tween.setYoyo(glide, true);

    screen.rect(40, 30, METER_WIDTH, 18, Color.make(0, 0, 0, 120));
    const meter = screen.rect(40, 30, 0, 18, Color.get(ColorStock.GOLD));
    const readout = screen.label("", 40 + METER_WIDTH + 70, 39, 18, Color.get(ColorStock.RAYWHITE));
    screen.label("P: pause    Esc: title", CX, 410, 18, Color.get(ColorStock.LIGHTGRAY));

    screen.charge = 0;
    screen.enter = (from) => {
        if (from !== pause) { // a new game: the meter starts empty
            Tween.destroy(screen.charge);
            screen.charge = Tween.create(0, 1, 2.0, Easing.QUAD_IN_OUT);
            Tween.setRepeat(screen.charge, -1);
            Tween.setYoyo(screen.charge, true);
        }
        Tween.setPaused(screen.charge, false);
    };
    screen.exit = () => Tween.setPaused(screen.charge, true);
    screen.frame = () => {
        const level = Tween.getValue(screen.charge);
        Shape2d.setRectangle(meter, METER_WIDTH * level, 18);
        Text.setString(readout, `charge ${Math.round(level * 100)}%`);
    };
    return screen;
}

// The pause screen, laid over play. It isn't opaque (main.js sets that), so play shows
// through its dark veil, frozen. Its heading pops in, a little past full size and back.
export function makePause() {
    const screen = makeScreen();
    screen.rect(0, 0, WIDTH, HEIGHT, Color.make(0, 0, 0, 150));
    const heading = screen.label("PAUSED", CX, 190, 56, Color.get(ColorStock.RAYWHITE));
    screen.label("P: resume    Esc: title", CX, 250, 18, Color.get(ColorStock.LIGHTGRAY));

    let pop = 0;
    screen.enter = () => {
        stop(pop);
        Actor.setScale(heading, 0.3, 0.3, 1);
        pop = Tween.scale(heading, 1, 1, 1, 0.35, Easing.BACK_OUT);
    };
    return screen;
}
