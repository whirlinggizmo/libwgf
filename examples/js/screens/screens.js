// The three screens. Each is a behavior on its screen's actor: it builds the screen's 2D stage,
// hears when its screen comes to the top (onScreenEnter) or leaves it (onScreenExit), and works
// each frame it isn't frozen (onFrame). Every actor of a screen hangs under its 2D stage, so the
// screen switches, pauses, and draws them together.
import {
    Actor, App, Behavior, Color, ColorStock, Easing, Probe, Screen, Shape2d, Stage2d, Text, TextHalign, TextValign,
    Tween,
} from "./wgf-typed.js";

export const WIDTH = 800;
export const HEIGHT = 450;
const CX = WIDTH / 2;

// A screen's behavior: it makes the screen's 2D stage, and puts shapes and text on it.
class ScreenBehavior extends Behavior {
    constructor(screen, ...rest) {
        super(screen, ...rest);
        this.stage = Stage2d.create();
        Screen.addStage(screen, this.stage);
    }

    rect(x, y, width, height, color) {
        const shape = Shape2d.create();
        Shape2d.setRectangle(shape, width, height);
        Shape2d.setPivot(shape, 0, 0); // placed by its top-left corner
        return this.place(shape, x, y, color);
    }

    circle(x, y, radius, color) {
        const shape = Shape2d.create();
        Shape2d.setCircle(shape, radius);
        return this.place(shape, x, y, color);
    }

    place(shape, x, y, color) {
        Shape2d.setColor(shape, color);
        Actor.setPosition(shape, x, y, 0);
        Actor.setParent(shape, this.stage);
        return shape;
    }

    // A line of text in the built-in font (font 0), centered on x, y.
    label(string, x, y, size, color) {
        const text = Text.create(0);
        Text.setString(text, string);
        Text.setFontSize(text, size);
        Text.setColor(text, color);
        Text.setAlign(text, TextHalign.CENTER, TextValign.MIDDLE);
        Actor.setPosition(text, x, y, 0);
        Actor.setParent(text, this.stage);
        return text;
    }
}

// Lets go of a tween that may have ended already.
const stop = (tween) => {
    if (Tween.isAlive(tween)) Tween.destroy(tween);
};

const HEADING_Y = 120;

// The title screen. Each time it comes to the top, its heading drops in on a bounce and its
// ring springs open. A square turns on a looping tween the whole time.
export class Title extends ScreenBehavior {
    drop = 0;
    spring = 0;

    constructor(screen, ...rest) {
        super(screen, ...rest);
        this.rect(0, 0, WIDTH, HEIGHT, Color.make(30, 45, 100, 255));
        this.heading = this.label("SCREENS", CX, HEADING_Y, 64, Color.get(ColorStock.GOLD));
        this.label("a screen stack, and tweens", CX, 180, 20, Color.get(ColorStock.SKYBLUE));

        this.ring = this.circle(CX, 290, 56, Color.get(ColorStock.SKYBLUE));
        Shape2d.setOutline(this.ring, 6);
        const square = this.rect(CX, 290, 50, 50, Color.get(ColorStock.GOLD));
        Shape2d.setPivot(square, 0.5, 0.5);
        // A quarter turn, again and again: the square looks the same at each end.
        Tween.setRepeat(Tween.turn(square, 0, 0, Math.PI / 2, 0.9, Easing.CUBIC_IN_OUT), -1);

        this.label(`Enter or click: play${App.canQuit() ? "    Esc: quit" : ""}`, CX, 410, 18,
            Color.get(ColorStock.LIGHTGRAY));
    }

    onScreenEnter(from) {
        stop(this.drop); // the last time's, if it still runs
        stop(this.spring);
        // An actor's tween starts from where the actor is, so put each where it starts.
        Actor.setPosition(this.heading, CX, -60, 0);
        Actor.setScale(this.ring, 0.01, 0.01, 1);
        this.drop = Tween.move(this.heading, CX, HEADING_Y, 0, 1.2, Easing.BOUNCE_OUT);
        this.spring = Tween.scale(this.ring, 1, 1, 1, 1.0, Easing.ELASTIC_OUT);
        Tween.setDelay(this.spring, 0.4);
    }

    onFrame(dt) {
        Probe.setValue("screens.heading_y", Actor.getPosition(this.heading).y);
    }
}

const METER_WIDTH = 300;

// The play screen: a ball bouncing and a block gliding, each on a tween that loops for ever,
// and a charge meter filled by a number's tween read each frame.
//
// When the pause screen covers play, the screen stack pauses play and its stage. The tweens
// of actors on it freeze with them, and so does this behavior: its onFrame isn't called.
// A number's tween belongs to no actor, so this pauses it.
export class Play extends ScreenBehavior {
    charge = 0;

    constructor(screen, ...rest) {
        super(screen, ...rest);
        this.rect(0, 0, WIDTH, HEIGHT, Color.make(25, 80, 45, 255));
        this.rect(0, 354, WIDTH, 4, Color.get(ColorStock.DARKGREEN));

        // A bounce: falling faster (QUAD_IN), then back up the way it came (yoyo), slowing.
        this.ball = this.circle(220, 100, 24, Color.get(ColorStock.ORANGE));
        const bounce = Tween.move(this.ball, 220, 330, 0, 0.55, Easing.QUAD_IN);
        Tween.setRepeat(bounce, -1);
        Tween.setYoyo(bounce, true);

        const block = this.rect(440, 260, 60, 60, Color.get(ColorStock.SKYBLUE));
        Shape2d.setPivot(block, 0.5, 0.5);
        const glide = Tween.move(block, 700, 260, 0, 1.4, Easing.SINE_IN_OUT);
        Tween.setRepeat(glide, -1);
        Tween.setYoyo(glide, true);

        this.rect(40, 30, METER_WIDTH, 18, Color.make(0, 0, 0, 120));
        this.meter = this.rect(40, 30, 0, 18, Color.get(ColorStock.GOLD));
        this.readout = this.label("", 40 + METER_WIDTH + 70, 39, 18, Color.get(ColorStock.RAYWHITE));
        this.label("P: pause    Esc: title", CX, 410, 18, Color.get(ColorStock.LIGHTGRAY));
    }

    onScreenEnter(from) {
        if (!(Behavior.of(from) instanceof Pause)) { // a new game: the meter starts empty
            Tween.destroy(this.charge);
            this.charge = Tween.create(0, 1, 2.0, Easing.QUAD_IN_OUT);
            Tween.setRepeat(this.charge, -1);
            Tween.setYoyo(this.charge, true);
        }
        Tween.setPaused(this.charge, false);
    }

    onScreenExit(to) {
        Tween.setPaused(this.charge, true);
    }

    onFrame(dt) {
        const level = Tween.getValue(this.charge);
        Shape2d.setRectangle(this.meter, METER_WIDTH * level, 18);
        Text.setString(this.readout, `charge ${Math.round(level * 100)}%`);
    }
}

// The pause screen, laid over play. It isn't opaque (main.js sets that), so play shows
// through its dark veil, frozen. Its heading pops in, a little past full size and back.
export class Pause extends ScreenBehavior {
    pop = 0;

    constructor(screen, ...rest) {
        super(screen, ...rest);
        this.rect(0, 0, WIDTH, HEIGHT, Color.make(0, 0, 0, 150));
        this.heading = this.label("PAUSED", CX, 190, 56, Color.get(ColorStock.RAYWHITE));
        this.label("P: resume    Esc: title", CX, 250, 18, Color.get(ColorStock.LIGHTGRAY));
    }

    onScreenEnter(from) {
        stop(this.pop);
        Actor.setScale(this.heading, 0.3, 0.3, 1);
        this.pop = Tween.scale(this.heading, 1, 1, 1, 0.35, Easing.BACK_OUT);
    }
}
