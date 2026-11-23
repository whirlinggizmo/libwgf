// A level described as data: a scene file, loaded, instantiated, and added to at run time.
//
// The level is assets/level.scene: a floor, two ledges, two clouds, and a dropper, each an actor
// with its components set in the file. The file also holds two prefabs, a cloud and a crate.
// Change the level by editing the file: this code only does these things.
//
// - It loads the scene, a resource, and waits for it to be READY.
// - It instantiates the scene under a 2D stage.
// - It finds the dropper by name on the stage, and the dropper's label by its path.
// - It spawns the crate prefab under the dropper, on a key press and every second.
// - It registers Patrol, the behavior the file names on the dropper.
//
// The controls:
// - Space drops a crate.
// - Escape quits, where quitting means anything (not in a browser).
//
// The Haxe example `scene` is the same program.
import createWgfHost from "./wgf-host.js";
import {
    Actor, App, Asset, Behavior, Color, ColorStock, Draw, Keyboard, KeyboardKey, Loop, Motion, Prefab, Presentation,
    PresentationMode, Probe, Render, Resource, ResourceStatus, Scene, Stage2d, Text, Window, attach,
} from "./wgf-typed.js";

const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    // libwgf logs every level to stderr: only its errors are the console's
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
    // an autopilot run (tools/check_js_binding.py's), when one was handed the page
    ...(typeof globalThis.wgfAutopilot === "string" ? { wgfAutopilot: globalThis.wgfAutopilot } : {}),
});
attach(host);

const DROP_EVERY = 1.0; // seconds between the crates that drop on their own

let stage = 0;
let scene = 0;
let loaded = false;
let crate = 0; // found once the scene is READY, and kept
let dropper = 0;
let label = 0;
let spawned = 0;
let clock = 0;
const at = { x: 0, y: 0, z: 0 };

// Patrol: its actor turns back at `left` and `right`, the parameters the scene file gives it.
// Its speed is its motion, set in the file too.
class Patrol extends Behavior {
    at = { x: 0, y: 0, z: 0 };
    velocity = { x: 0, y: 0, z: 0 };

    onCreate() {
        this.left = this.getParamNumber("left");
        this.right = this.getParamNumber("right");
    }

    onTick(dt) {
        const at = Actor.getPosition(this.actor, this.at), velocity = Motion.getVelocity(this.actor, this.velocity);
        if ((at.x > this.right && velocity.x > 0) || (at.x < this.left && velocity.x < 0)) {
            Motion.setVelocity(this.actor, -velocity.x, velocity.y, 0);
        }
    }
}

function init() {
    Asset.setHost("assets");
    Render.setClearColor(Color.make(150, 196, 230, 255));
    stage = Stage2d.create();
    Behavior.register("Patrol", Patrol);
    scene = Scene.create("level.scene"); // PENDING now: it loads while the frames go on
    publish();
}

// The scene is READY: its actors made, and what the program needs from it found once.
function start() {
    loaded = true;
    Scene.instantiate(scene, stage);
    dropper = Stage2d.find(stage, "dropper"); // by name, anywhere on the stage
    label = Actor.find(dropper, "label"); // by path, from the dropper
    crate = Scene.prefab(scene, "crate");
}

function frame() {
    if (!loaded && Resource.getStatus(scene) === ResourceStatus.READY) start();
    if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE)) App.quit();
    if (loaded) {
        clock += Loop.getFrameDelta();
        if (Keyboard.isPressed(KeyboardKey.SPACE) || clock >= DROP_EVERY) drop();
    }
    Stage2d.draw(stage);
    hud();
    publish();
}

function drop() {
    clock = 0;
    Actor.getPosition(dropper, at);
    Prefab.spawnAt(crate, stage, at.x, at.y + 24, 0, 0);
    spawned++;
    Text.setString(label, `dropped ${spawned}`);
}

function hud() {
    const ink = Color.make(30, 34, 44, 255);
    Draw.text(0, "level.scene: a level as data", 16, 12, 20, ink);
    if (Resource.getStatus(scene) === ResourceStatus.FAILED) {
        Draw.text(0, "level.scene failed to load (see the log)", 16, 40, 16, Color.get(ColorStock.MAROON));
        return;
    }
    if (!loaded) {
        Draw.text(0, "loading...", 16, 40, 16, ink);
        return;
    }
    const prefabs = Scene.getPrefabCount(scene);
    Draw.text(0, `the file: ${Scene.getActorCount(scene)} actors, ${prefabs} prefabs`, 16, 40, 16, ink);
    Draw.text(0, `simulated actors now: ${Actor.getCount()}`, 16, 60, 16, ink);
    Draw.text(0, "space: drop a crate (one drops each second)", 16, 80, 16, ink);
}

// Probes, for smoke.autopilot.
function publish() {
    Probe.setValue("scene.loaded", loaded ? 1 : 0);
    Probe.setValue("scene.actors", Actor.getCount());
    Probe.setValue("scene.spawned", spawned);
}

Window.setTitle("libwgf scene (JS)");
// The design size, fitted to any window or page. Window.setSize would fix the canvas at that
// size on the web, unscaled, so it is left to the presentation.
Presentation.set(PresentationMode.FIT, 800, 450);
Window.setMsaa(true);
App.run(init, null, frame, null);
