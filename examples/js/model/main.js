// A skinned, animated 3D model, in JS: Quaternius's robot, its clips crossfaded one into the next.
//
// This is the C example gfx-skinning made small, and the Haxe example model, on the JS binding's
// typed layer (wgf-typed.js). It shows a glTF file loaded without waiting, and played once it is there:
// - The model is made at once, and draws nothing while its file loads. The text shows the status.
// - Once the file is READY, its clips are found by name, and the idle clip plays.
// - Every few seconds, or on N, the next clip crossfades in.
// - The sun casts shadows on a ground made in code, with a material of its own.
// - The camera circles slowly.
//
// Controls:
// - N: the next clip, crossfaded.
// - Space: pause or resume the clip.
// - Escape: quit, where quitting means anything (not in a browser).
import createWgfHost from "./wgf-host.js";
import {
    Actor, App, Asset, Camera3d, Color, ColorStock, Debug, Draw, Keyboard, KeyboardKey, Light, LightType, Loop,
    Material, MaterialShading, Mesh, Model, Probe, Render, Resource, ResourceStatus, Stage3d, Window, attach,
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

const PATH = "models/robot.glb";
const FADE = 0.3; // seconds of crossfade into each clip
const STEP = 4.0; // seconds of each clip before the next

const statusNames = {
    [ResourceStatus.PENDING]: "pending", [ResourceStatus.READY]: "ready", [ResourceStatus.FAILED]: "failed",
};

const g = { stage: 0, camera: 0, robot: 0, started: false, paused: false, clip: -1, since: 0, angle: 0 };

function init() {
    Asset.setHost("assets");
    Render.setClearColor(Color.make(24, 26, 34, 255));

    g.camera = Camera3d.create();
    g.stage = Stage3d.create();
    Stage3d.setCamera(g.stage, g.camera);
    Stage3d.setAmbient(g.stage, Color.get(ColorStock.WHITE), 0.35);

    const sun = Light.create(LightType.DIRECTIONAL);
    Actor.lookAt(sun, -0.4, -1.0, -0.6, 0, 1, 0); // from the origin, shining that way
    Light.setIntensity(sun, 2.5);
    Light.setShadowCasting(sun, true);
    Light.setShadowDistance(sun, 20);
    Actor.setParent(sun, g.stage);

    // The ground: a generated mesh, with a material of its own.
    const plane = Mesh.createPlane(40, 40, 0);
    const ground = Model.create(plane);
    Resource.release(plane); // the model holds its own reference
    const material = Material.create(MaterialShading.PBR);
    Material.setVec4(material, "base_color", 0.18, 0.2, 0.24, 1.0);
    Material.setFloat(material, "roughness", 0.9);
    Model.setMaterial(ground, -1, material); // -1: every slot
    Resource.release(material);
    Actor.setParent(ground, g.stage);

    // The robot: its file starts loading now, and the model draws once it is READY.
    const mesh = Mesh.create(PATH);
    g.robot = Model.create(mesh);
    Resource.release(mesh);
    Actor.setParent(g.robot, g.stage);
}

// Once the file is READY: its clips are there, found by name.
function start() {
    g.clip = Math.max(Model.findAnimation(g.robot, "Robot_Idle"), 0);
    Model.playAnimation(g.robot, g.clip, 0); // cut straight to it
    g.started = true;
}

function nextClip() {
    g.clip = (g.clip + 1) % Model.getAnimationCount(g.robot);
    Model.playAnimation(g.robot, g.clip, FADE); // plays from any state, so a paused robot plays again
    g.paused = false;
    g.since = 0;
}

function frame() {
    const dt = Loop.getFrameDelta();
    if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE)) App.quit();

    const status = Resource.getStatus(Model.getMesh(g.robot));
    if (!g.started && status === ResourceStatus.READY) start();
    if (g.started) {
        if (Keyboard.isPressed(KeyboardKey.SPACE)) {
            g.paused = !g.paused;
            if (g.paused) Model.pauseAnimation(g.robot);
            else Model.resumeAnimation(g.robot);
        }
        if (!g.paused) g.since += dt;
        if (Keyboard.isPressed(KeyboardKey.N) || g.since >= STEP) nextClip();
    }

    g.angle += dt * 0.15;
    Actor.setPosition(g.camera, 9 * Math.sin(g.angle), 4, 9 * Math.cos(g.angle));
    Actor.lookAt(g.camera, 0, 1.8, 0, 0, 1, 0);

    Stage3d.draw(g.stage);

    const white = Color.get(ColorStock.RAYWHITE);
    const gray = Color.get(ColorStock.LIGHTGRAY);
    if (g.started) {
        const name = Model.getAnimationName(g.robot, g.clip);
        const time = Model.getAnimationTime(g.robot).toFixed(1);
        const duration = Model.getAnimationDuration(g.robot, g.clip).toFixed(1);
        Draw.text(0, `${name}   ${time} / ${duration} s`, 12, 36, 20, white);
        const fading = Model.getAnimationWeight(g.robot) < 1 ? ", crossfading" : "";
        const state = g.paused ? ", paused" : "";
        const count = Model.getAnimationCount(g.robot);
        Draw.text(0, `clip ${g.clip + 1} of ${count}${fading}${state}`, 12, 62, 16, gray);
    } else {
        Draw.text(0, `loading ${PATH}: ${statusNames[status] ?? "none"}`, 12, 36, 20, white);
    }
    Draw.text(0, App.canQuit() ? "N: next clip   SPACE: pause   ESC: quit" : "N: next clip   SPACE: pause",
        12, 86, 16, gray);

    // Probes publish numbers an autopilot run can check (smoke.autopilot).
    Probe.setValue("model.loaded", g.started ? 1 : 0);
    Probe.setValue("model.clips", Model.getAnimationCount(g.robot));
    Probe.setValue("model.clip", g.clip);
    Probe.setValue("model.paused", g.paused ? 1 : 0);
}

Window.setTitle("libwgf model (JS)");
// The design size, fitted to any window or page. Window.setSize would fix the canvas at that
// size on the web, unscaled, so it is left to the presentation.
Presentation.set(PresentationMode.FIT, 1000, 600);
Window.setMsaa(true);
Window.setResizable(true);
Debug.showFps(0, 12, 10, 16, Color.make(0, 255, 0, 255));
App.run(init, null, frame, null);
