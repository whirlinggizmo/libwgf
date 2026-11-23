// Sprites and input actions, in JS: textured sprites on a 2D stage, and one the player moves.
//
// It shows:
// - sprites made from two textures, each sized and placed, one turned, one scaled, one tinted
// - draw order: a 2D stage draws its children in order, so a later one is drawn on top
// - input actions (player.js): "move_x", "move_y", and "fire", each bound to keys and to a pad
// - a help line made from the actions' own bindings, as a remap screen would list them
//
// Controls:
// - Arrow keys, WASD, the d-pad, or the left stick: move the orange logo.
// - Space or the pad's south button: send it behind the other sprites, or back in front.
// - Escape: quit, where quitting means anything (not in a browser).
//
// Its probes, for smoke.autopilot:
// - sprites.loaded: 1 once both textures are ready.
// - sprites.x, sprites.y: the player's position, in pixels.
// - sprites.front: 1 while the player is drawn on top.
//
// This is the Haxe example `sprites`, written the JS way, on the typed layer (wgf-typed.js).
import createWgfHost from "./wgf-host.js";
import {
    Action, Actor, App, Asset, Color, ColorStock, Draw, Keyboard, KeyboardKey, Loop, Presentation,
    PresentationMode, Probe, Render, Resource, ResourceStatus, Sprite, Stage2d, Texture, Window, attach,
} from "./wgf-typed.js";
import { bindActions, makePlayer, updatePlayer } from "./player.js";

const WIDTH = 800;
const HEIGHT = 600;

const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    // libwgf logs every level to stderr: only its errors are the console's
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
    // an autopilot run (tools/check_js_binding.py's), when one was handed the page
    ...(typeof globalThis.wgfAutopilot === "string" ? { wgfAutopilot: globalThis.wgfAutopilot } : {}),
});
attach(host);

let stage = 0, black = 0, white = 0;
let spinner = 0, pulse = 0, rainbow = 0;
let player = null;
let help = [];
let time = 0;

// A sprite of `texture`, `size` pixels square, centered on (x, y) on the stage.
function addSprite(texture, x, y, size) {
    const sprite = Sprite.create(texture); // it holds its own reference to the texture
    Sprite.setSize(sprite, size, size);
    Actor.setPosition(sprite, x, y, 0);
    Actor.setParent(sprite, stage);
    return sprite;
}

// An action and its bindings as text, such as "fire: Space, Pad south".
function describe(action) {
    const names = [];
    for (let i = 0; i < Action.getBindingCount(action); i++) names.push(Action.getBindingText(action, i));
    return `${action}: ${names.join(", ")}`;
}

function init() {
    Asset.setHost("assets");
    Render.setClearColor(Color.make(232, 234, 240, 255));
    bindActions();
    for (let i = 0; i < Action.getCount(); i++) help.push(describe(Action.getName(i)));

    // Textures load in the background. A sprite draws nothing until its texture is ready.
    black = Texture.create("wg-logo-bw-alpha.png");
    white = Texture.create("wg-logo-white-alpha.png");
    stage = Stage2d.create();
    // Added in this order, so drawn in it: the player, last, starts on top of the two it overlaps.
    spinner = addSprite(black, 250, 330, 220);
    pulse = addSprite(black, 550, 330, 220);
    rainbow = addSprite(white, 400, 190, 150);
    player = makePlayer(addSprite(white, 0, 0, 110), WIDTH / 2, 360, WIDTH, HEIGHT);
}

function frame() {
    if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE)) App.quit();
    const dt = Loop.getFrameDelta();
    time += dt;

    Actor.setRotation(spinner, 0, 0, time); // radians, about its center (the default pivot)
    const scale = 1 + 0.2 * Math.sin(time * 3);
    Actor.setScale(pulse, scale, scale, 1);
    Sprite.setTint(rainbow, Color.makeHsv(time * 90, 0.8, 1, 255)); // round the color wheel every 4 s
    updatePlayer(player, dt);

    Stage2d.draw(stage);
    const gray = Color.get(ColorStock.DARKGRAY);
    Draw.text(0, "Move the orange logo. Fire sends it behind the others, or back in front.", 20, 16, 16, gray);
    help.forEach((line, i) => Draw.text(0, line, 20, 40 + i * 20, 16, Color.get(ColorStock.GRAY)));
    Draw.text(0, player.front ? "player: in front" : "player: behind", 20, 104, 16, gray);

    const loaded = Resource.getStatus(black) === ResourceStatus.READY
        && Resource.getStatus(white) === ResourceStatus.READY;
    Probe.setValue("sprites.loaded", loaded ? 1 : 0);
    Probe.setValue("sprites.x", player.x);
    Probe.setValue("sprites.y", player.y);
    Probe.setValue("sprites.front", player.front ? 1 : 0);
}

Window.setTitle("libwgf sprites (JS)");
Window.setMsaa(true);
// The design size, fitted to any window or page. Natively, the window opens at it.
Presentation.set(PresentationMode.FIT, WIDTH, HEIGHT);
App.run(init, null, frame, null);
