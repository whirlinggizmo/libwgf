// Behaviors, in JavaScript: a 2D scene whose actors are driven by components and behaviors.
//
// Components are data the ecs runs in C: motion moves an actor, bounds wrap it around the
// screen, a lifetime ends it, and a collider finds what it overlaps. Behaviors are this
// program's classes, one per kind of actor, extending Behavior from behaviors.js, a module
// the program keeps. They only set intent. The scene is the Haxe example `behaviors`'s.
//
// - A spinning square in the middle (Spawner) launches a ball every 0.4 seconds, from its tick.
// - A ball (Ball) flies by its motion, wraps at the edges, and lives 6 seconds. It bounces off
//   a bumper it meets, and takes the bumper's color.
// - A bumper (Bumper) spins by its motion. A ball that meets it counts a hit, flashes it, and
//   leaves a spark (Spark), a ring that grows and fades until its lifetime ends it.
// - Escape quits, where quitting means anything (not in a browser).
//
// The scene draws from libwgf's seeded random numbers (Random), so every run is the same.
// Its probes, for smoke.autopilot: behaviors.hits, behaviors.balls, and behaviors.actors.
import createWgfHost from "./wgf-host.js";
import {
    Actor, App, Bounds, Collider, Color, ColorStock, Component, Draw, Keyboard, KeyboardKey, Lifetime, Loop, Motion,
    Presentation, PresentationMode, Probe, Random, Render, Shape2d, Stage2d, TextHalign, TextValign, Window, attach,
} from "./wgf-typed.js";
import { Behavior, register, of, tickAll, frameAll, endAll } from "./behaviors.js";

const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    // libwgf logs every level to stderr: only its errors are the console's
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
    // an autopilot run (tools/check_js_binding.py's), when one was handed the page
    ...(typeof globalThis.wgfAutopilot === "string" ? { wgfAutopilot: globalThis.wgfAutopilot } : {}),
});
attach(host);

const WIDTH = 800, HEIGHT = 600;
// collider layers, as bits: a ball's mask has bumpers, and a bumper's has balls
const BUMPER_LAYER = 1, BALL_LAYER = 2;
const SPAWN_EVERY = 0.4, BALL_RADIUS = 9, BALL_LIFE = 6, SPARK_LIFE = 0.5;

let world = 0;
let hits = 0;

/** A shape on the world at (x, y), in `color`: what every actor here is. */
function shape(x, y, color) {
    const actor = Shape2d.create();
    Actor.setParent(actor, world);
    Actor.setPosition(actor, x, y, 0);
    Shape2d.setColor(actor, color);
    return actor;
}

// ---- the behaviors ------------------------------------------------------------------

/** The spinning square: launches a ball from where it is, every SPAWN_EVERY seconds, from its tick. */
class Spawner extends Behavior {
    clock = SPAWN_EVERY; // the first ball at once
    at = { x: 0, y: 0, z: 0 };

    onTick(dt) {
        this.clock += dt;
        while (this.clock >= SPAWN_EVERY) {
            this.clock -= SPAWN_EVERY;
            const p = Actor.getPosition(this.actor, this.at); // fills `at`, so no object is made each tick
            spawnBall(p.x, p.y, Random.getRange(0, Math.PI * 2), Random.getRange(140, 220));
        }
    }
}

/** A ball at (x, y), heading `angle` radians at `speed` pixels a second. Its components fly, wrap, and end it. */
function spawnBall(x, y, angle, speed) {
    const ball = shape(x, y, Color.get(ColorStock.RAYWHITE));
    Shape2d.setCircle(ball, BALL_RADIUS);
    Actor.addComponent(ball, Component.MOTION);
    Motion.setVelocity(ball, Math.cos(angle) * speed, Math.sin(angle) * speed, 0);
    Actor.addComponent(ball, Component.BOUNDS); // wraps, by default
    Bounds.setRect(ball, 0, 0, WIDTH, HEIGHT);
    Bounds.setMargin(ball, BALL_RADIUS); // wholly off the screen first
    Actor.addComponent(ball, Component.LIFETIME);
    Lifetime.setSeconds(ball, BALL_LIFE);
    Actor.addComponent(ball, Component.COLLIDER);
    Collider.setRadius(ball, BALL_RADIUS);
    Collider.setLayer(ball, BALL_LAYER);
    Collider.setMask(ball, BUMPER_LAYER);
    Actor.addBehavior(ball, "Ball");
    return ball;
}

/** A ball: bounces off a bumper it meets, and takes the bumper's color. */
class Ball extends Behavior {
    velocity = { x: 0, y: 0, z: 0 };
    position = { x: 0, y: 0, z: 0 };
    push = { x: 0, y: 0 };

    onTriggerEnter(other, layer) {
        if (layer !== BUMPER_LAYER) return;
        // The push parts the two along the way they meet: reflect the velocity across it.
        const p = Collider.getPush(this.actor, other, this.push);
        const length = Math.hypot(p.x, p.y);
        if (length > 0) {
            const nx = p.x / length, ny = p.y / length;
            const v = Motion.getVelocity(this.actor, this.velocity);
            const along = v.x * nx + v.y * ny;
            if (along < 0) Motion.setVelocity(this.actor, v.x - 2 * along * nx, v.y - 2 * along * ny, 0);
            const at = Actor.getPosition(this.actor, this.position);
            Actor.setPosition(this.actor, at.x + p.x, at.y + p.y, 0);
        }
        Shape2d.setColor(this.actor, of(other, "Bumper").color);
    }
}

/** A bumper: counts each ball that meets it, flashes, and leaves a spark where the ball was. */
class Bumper extends Behavior {
    color = 0; // its own color, the one it flashes from
    flash = 0; // 1 as a ball meets it, down to 0
    at = { x: 0, y: 0, z: 0 };

    onCreate() {
        this.color = Shape2d.getColor(this.actor);
    }

    onTriggerEnter(other, layer) {
        if (layer !== BALL_LAYER) return;
        hits++;
        this.flash = 1;
        const p = Actor.getPosition(other, this.at);
        spawnSpark(p.x, p.y, this.color);
    }

    // Each frame, the flash fades back to its color: a look, so the frame's and not the tick's.
    onFrame(dt) {
        if (this.flash <= 0) return;
        this.flash = Math.max(0, this.flash - dt * 3);
        Shape2d.setColor(this.actor, Color.lerp(this.color, Color.get(ColorStock.WHITE), this.flash));
    }
}

/** A spark at (x, y), in `color`: a ring its lifetime ends. */
function spawnSpark(x, y, color) {
    const spark = shape(x, y, color);
    Shape2d.setCircle(spark, 10);
    Shape2d.setOutline(spark, 3);
    Actor.addComponent(spark, Component.LIFETIME);
    Lifetime.setSeconds(spark, SPARK_LIFE);
    Actor.addBehavior(spark, "Spark");
    return spark;
}

/** A spark: grows and fades while its lifetime runs out. */
class Spark extends Behavior {
    color = 0;

    onCreate() {
        this.color = Shape2d.getColor(this.actor);
    }

    // Its scale is its transform, which a simulated actor sets at the tick rate.
    onTick(dt) {
        const left = Lifetime.getSeconds(this.actor) / SPARK_LIFE; // 1 down to 0
        const grow = 1 + (1 - left) * 2.5;
        Actor.setScale(this.actor, grow, grow, 1);
        Shape2d.setColor(this.actor, Color.withAlpha(this.color, Math.trunc(255 * left)));
    }
}

// ---- the scene and the loop ---------------------------------------------------------

/** A bar that spins in place, `spin` radians a second, and meets balls. */
function bumper(x, y, spin, stock) {
    const bar = shape(x, y, Color.get(stock));
    Shape2d.setRectangle(bar, 150, 22);
    Shape2d.setCornerRadius(bar, 8);
    Actor.addComponent(bar, Component.MOTION);
    Motion.setSpin(bar, 0, 0, spin);
    Actor.addComponent(bar, Component.COLLIDER);
    Collider.setRectangle(bar, 150, 22); // it turns with its actor
    Collider.setLayer(bar, BUMPER_LAYER);
    Collider.setMask(bar, BALL_LAYER);
    Actor.addBehavior(bar, "Bumper");
}

function init() {
    Render.setClearColor(Color.make(14, 16, 26, 255));
    Random.setSeed(7);
    world = Stage2d.create();

    // Each name an actor's behavior has is made as this class, at the next tick or frame.
    register("Spawner", (actor) => new Spawner(actor));
    register("Ball", (actor) => new Ball(actor));
    register("Bumper", (actor) => new Bumper(actor));
    register("Spark", (actor) => new Spark(actor));

    bumper(220, 170, 0.7, ColorStock.ORANGE);
    bumper(580, 170, -0.5, ColorStock.SKYBLUE);
    bumper(220, 430, -0.6, ColorStock.LIME);
    bumper(580, 430, 0.8, ColorStock.PINK);

    const spawner = shape(WIDTH / 2, HEIGHT / 2, Color.get(ColorStock.GOLD));
    Shape2d.setRectangle(spawner, 36, 36);
    Shape2d.setOutline(spawner, 3);
    Actor.addComponent(spawner, Component.MOTION);
    Motion.setSpin(spawner, 0, 0, 2);
    Actor.addBehavior(spawner, "Spawner");
}

function tick() {
    tickAll(Loop.getTickDelta()); // the behaviors first, as the Haxe binding's run does
    Probe.setValue("behaviors.hits", hits);
    Probe.setValue("behaviors.balls", Actor.countWithBehavior("Ball"));
    Probe.setValue("behaviors.actors", Actor.getCount());
}

function frame() {
    frameAll(Loop.getFrameDelta());
    if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE)) App.quit();
    Stage2d.draw(world);
    Draw.text(0, `hits: ${hits}`, 16, 14, 28, Color.get(ColorStock.RAYWHITE));
    Draw.text(0, `balls: ${Actor.countWithBehavior("Ball")}`, 16, 48, 18, Color.get(ColorStock.LIGHTGRAY));
    Draw.textAligned(0, "behaviors (JS)", WIDTH - 16, 16, 18, Color.get(ColorStock.GRAY), TextHalign.RIGHT,
                     TextValign.TOP);
}

Window.setTitle("libwgf behaviors (JS)");
Window.setMsaa(true);
// the scene's 800 by 600, fitted to any window, so the balls wrap at its edges
Presentation.set(PresentationMode.FIT, WIDTH, HEIGHT);
App.run(init, tick, frame, endAll);
