// 3D physics, in JS: a pile of boxes and spheres falls onto a floor and settles.
//
// It shows rigid bodies (Body3d) on generated models:
// - A static floor, and dynamic boxes and spheres with their own mass, friction, and bounce.
// - Layers and masks: two bodies meet when either one's mask has the other's layer.
// - A ray cast from the camera through the pointer (Physics3d.castRay). The body it hits is tinted.
// - A sensor (the green outline) that counts each body falling through it, in a behavior's
//   onTriggerEnter.
//
// Controls:
// - Point at a body to highlight it.
// - Space kicks the highlighted body up, or the next one in the pile when none is.
// - R puts the pile back where it started.
// - Escape quits, where quitting means anything (not in a browser).
//
// This is the Haxe example `physics`, on the typed layer (wgf-typed.js).
import createWgfHost from "./wgf-host.js";
import {
    Actor, App, Behavior, Body3d, Body3dShape, Body3dType, Camera3d, Color, ColorStock, Component, Draw, Keyboard,
    KeyboardKey, Light, LightType, Loop, Mesh, Model, Mouse, Physics3d, Presentation, PresentationMode, Probe,
    Render, Resource, Stage3d, Window, attach,
    WindowFlag,
} from "./wgf-typed.js";

const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    // libwgf logs every level to stderr: only its errors are the console's
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
    // an autopilot run (tools/check_js_binding.py's), when one was handed the page
    ...(typeof globalThis.wgfAutopilot === "string" ? { wgfAutopilot: globalThis.wgfAutopilot } : {}),
});
attach(host);

// Each body's layer. The pile meets everything; the floor and the sensor meet only the pile.
const PILE = 1, FLOOR = 2, SENSE = 4;

let stage = 0, camera = 0, sensor = 0, yellow = 0;
const pieces = []; // { actor, tint, x, y, z, turn }: each body of the pile, and where it starts
let hovered = null;
let kicks = 0, nextKick = 0;

const mouse = { x: 0, y: 0 };
const origin = { x: 0, y: 0, z: 0 }, direction = { x: 0, y: 0, z: 0 };
const hit = new Uint32Array(1);

// A model of `mesh` on the stage at (x, y, z), tinted, with a body (a dynamic box until set).
function body(mesh, tint, x, y, z) {
    const model = Model.create(mesh);
    Model.setTint(model, tint);
    Actor.setPosition(model, x, y, z);
    Actor.setParent(model, stage);
    Actor.addComponent(model, Component.BODY3D);
    return model;
}

// The sensor's behavior: it counts the bodies that enter it. A sensor body meets no body, so
// the pile falls straight through it.
class Sensor extends Behavior {
    count = 0; // how many bodies have entered, since the last reset
    sinceLast = 99; // seconds since the last one, to flash the sensor's outline

    onTick(dt) {
        this.sinceLast += dt;
    }

    onTriggerEnter(other, layer) {
        this.count++;
        this.sinceLast = 0;
    }
}

// The sensor's behavior, or null before it is made (at the first tick or frame after init).
const counter = () => Behavior.of(sensor, "Sensor");

function init() {
    Behavior.register("Sensor", Sensor);
    Render.setClearColor(Color.make(150, 180, 210, 255));
    Physics3d.setGravity(0, -9.81, 0); // physics starts here, before the first body
    yellow = Color.get(ColorStock.YELLOW);

    stage = Stage3d.create();
    camera = Camera3d.create();
    Actor.setPosition(camera, 0, 7, 12);
    Actor.lookAt(camera, 0, 1.5, 0, 0, 1, 0);
    Stage3d.setCamera(stage, camera);
    Stage3d.setAmbient(stage, Color.make(150, 170, 200, 255), 0.35);
    const sun = Light.create(LightType.DIRECTIONAL);
    Actor.lookAt(sun, -0.4, -1.0, -0.3, 0, 1, 0);
    Light.setIntensity(sun, 1.2);
    Actor.setParent(sun, stage);

    // The floor: a static box, its top at y = 0.
    const slab = Mesh.createCube(20, 1, 20);
    const floor = body(slab, Color.make(90, 120, 80, 255), 0, -0.5, 0);
    Resource.release(slab); // the model holds its own reference
    Body3d.setType(floor, Body3dType.STATIC);
    Body3d.setShape(floor, Body3dShape.BOX, 20, 1, 20);
    Body3d.setLayer(floor, FLOOR);
    Body3d.setMask(floor, PILE);

    // The pile: two layers of three by three, boxes and spheres in turn.
    const cube = Mesh.createCube(0.9, 0.9, 0.9), ball = Mesh.createSphere(0.45, 16, 24);
    for (let i = 0; i < 18; i++) {
        const layer = Math.floor(i / 9);
        const x = ((i % 3) - 1) * 1.2 + layer * 0.3, y = 4 + layer * 1.3, z = ((Math.floor(i / 3) % 3) - 1) * 1.2;
        const isBox = i % 2 === 0;
        const tint = isBox ? Color.make(210, 140, 60, 255) : Color.make(70, 120, 220, 255);
        const actor = body(isBox ? cube : ball, tint, x, y, z);
        Body3d.setType(actor, Body3dType.DYNAMIC);
        Body3d.setLayer(actor, PILE);
        if (isBox) { // heavy, and grippy
            Body3d.setShape(actor, Body3dShape.BOX, 0.9, 0.9, 0.9);
            Body3d.setMass(actor, 20);
            Body3d.setFriction(actor, 0.8);
        } else { // light, and bouncy, with damping so it stops rolling
            Body3d.setShape(actor, Body3dShape.SPHERE, 0.45, 0, 0);
            Body3d.setMass(actor, 4);
            Body3d.setFriction(actor, 0.3);
            Body3d.setBounce(actor, 0.6);
            Body3d.setDamping(actor, 0.9, 0.95);
        }
        pieces.push({ actor, tint, x, y, z, turn: i * 0.4 });
    }
    Resource.release(cube);
    Resource.release(ball);

    // The sensor: a body with no model, which the pile falls through on its way down.
    sensor = Actor.create();
    Actor.setPosition(sensor, 0, 2.6, 0);
    Actor.setParent(sensor, stage);
    Actor.addComponent(sensor, Component.BODY3D);
    Body3d.setType(sensor, Body3dType.SENSOR);
    Body3d.setShape(sensor, Body3dShape.BOX, 5, 0.8, 5);
    Body3d.setLayer(sensor, SENSE);
    Body3d.setMask(sensor, PILE);
    Actor.addBehavior(sensor, "Sensor");
    reset();
}

// Puts every piece back where it started, at rest, and starts the count again.
function reset() {
    for (const piece of pieces) {
        Actor.setTransform(piece.actor, piece.x, piece.y, piece.z, 0.2, piece.turn, 0.1, 1, 1, 1);
        Body3d.setVelocity(piece.actor, 0, 0, 0);
        Body3d.setSpin(piece.actor, 0, 0, 0);
    }
    if (counter()) counter().count = 0;
    kicks = 0;
    nextKick = 0;
}

function tick() {
    if (Keyboard.isPressed(KeyboardKey.R)) reset();

    // The ray under the pointer: from the camera, into the scene. Its mask sees only the pile.
    Mouse.getPosition(mouse);
    Camera3d.screenToOrigin(camera, mouse.x, mouse.y, origin);
    Camera3d.screenToDirection(camera, mouse.x, mouse.y, direction);
    const n = Physics3d.castRay(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, 100, PILE, hit,
        null);
    const now = n > 0 ? pieces.find((piece) => piece.actor === hit[0]) ?? null : null;
    if (now !== hovered) {
        if (hovered) Model.setTint(hovered.actor, hovered.tint);
        if (now) Model.setTint(now.actor, yellow);
        hovered = now;
    }

    // Space: an impulse up, and a little sideways, sized to the body's mass for a 7 m/s kick.
    if (Keyboard.isPressed(KeyboardKey.SPACE)) {
        const target = hovered ?? pieces[nextKick++ % pieces.length];
        const mass = Body3d.getMass(target.actor);
        Body3d.addImpulse(target.actor, mass * 1.5, mass * 7, 0);
        kicks++;
    }

    // Settled: the world has put every piece to sleep.
    const settled = pieces.every((piece) => Body3d.isSleeping(piece.actor));
    Probe.setValue("physics.bodies", pieces.length);
    Probe.setValue("physics.sensed", counter() ? counter().count : 0);
    Probe.setValue("physics.settled", settled ? 1 : 0);
    Probe.setValue("physics.hovered", hovered ? 1 : 0);
    Probe.setValue("physics.kicks", kicks);
}

function frame() {
    if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE)) App.quit();
    Stage3d.draw(stage);

    // The sensor has no model: its outline is drawn, bright for a moment after each entry.
    if (Draw.begin3d(camera)) {
        const lit = counter() && counter().sinceLast < 0.2;
        Draw.cubeWires(0, 2.6, 0, 5, 0.8, 5, Color.get(lit ? ColorStock.LIME : ColorStock.DARKGREEN));
        Draw.end3d();
    }

    const dark = Color.get(ColorStock.DARKGRAY);
    Draw.text(0, "libwgf physics: point at a body, Space kicks it, R resets", 12, 12, 20, dark);
    Draw.text(0, `through the sensor: ${counter() ? counter().count : 0}`, 12, 40, 20, dark);
    Draw.text(0, `kicks: ${kicks}`, 12, 64, 20, dark);
    Draw.text(0, `${Math.round(Loop.getFps())} FPS`, 12, 576, 16, Color.make(0, 120, 0, 255));
}

Window.setTitle("libwgf physics (JS)");
// The design size, fitted to any window or page. A desktop window opens at this size.
Presentation.set(PresentationMode.FIT, 960, 600);
Window.setFlags(WindowFlag.MSAA);
App.run(init, tick, frame, null);
