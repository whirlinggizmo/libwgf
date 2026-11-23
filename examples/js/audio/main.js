// Audio, in JS: sounds, voices, and buses, through the JS binding's typed layer (wgf-typed.js).
//
// It shows each way a sound plays, with its state drawn as it changes:
// - Music: a streamed sound on its own looping voice, heard through the music bus.
// - A click: a sound played once with Sound.play, on a voice the mixer owns.
// - A tone: a sound made in code from samples, on its own voice.
// - The music voice's volume, pitch, and pan, the music and effects buses' volumes, and the
//   master volume, each changed while it plays.
//
// The controls:
// - Space plays the music from the start. P pauses or resumes it. S stops it.
// - Enter, or a click, plays the click. A click is panned to where the mouse is.
// - T plays the tone.
// - Up and Down choose a setting. Left and Right, held, change it.
// - Escape quits, where quitting means anything (not in a browser).
//
// In a browser, sound stays off until the page's first key or click. Until then a voice told
// to play reads PLAYING, but is silent and its position doesn't move.
//
// This is the Haxe example `audio` (examples/haxe/audio), written the JS way.
import createWgfHost from "./wgf-host.js";
import {
    App, Asset, Audio, Bus, Color, ColorStock, Draw, Keyboard, KeyboardKey, Loop, Mouse, MouseButton,
    PlayState, Probe, Render, Resource, ResourceStatus, Sound, Voice, Window, attach,
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

let music = 0, click = 0, tone = 0;
let settings = [];
let selected = 0;
let plays = 0;
let flash = 0; // the click's ring, fading after each play
const mouse = { x: 0, y: 0 };

// A falling chirp a third of a second long, made a sample at a time: 22050 a second, mono.
function makeTone() {
    const rate = 22050;
    const samples = new Float32Array(Math.floor(rate * 0.35));
    let phase = 0;
    for (let i = 0; i < samples.length; i++) {
        const t = i / rate;
        phase += (880 - 1200 * t) / rate; // the pitch falls from 880 Hz
        samples[i] = Math.sin(phase * Math.PI * 2) * Math.exp(-t * 9) * 0.5;
    }
    return Sound.createFromSamples(samples, 1, rate);
}

const busSetting = (name, bus) =>
    ({ name, min: 0, max: 1.5, get: () => Bus.getVolume(bus), set: (v) => Bus.setVolume(bus, v) });

function init() {
    Asset.setHost("assets");
    Render.setClearColor(Color.make(18, 20, 28, 255));

    // Music is streamed: decoded as it plays, rather than held whole.
    const song = Sound.createStreamed("music/a_hero_is_born.mp3");
    music = Voice.create(song);
    Resource.release(song); // the voice holds its own reference
    Voice.setLoop(music, true);
    Voice.setVolume(music, 0.6);
    Voice.setBus(music, Bus.getMusic()); // a voice is on the effects bus until set

    click = Sound.create("sounds/click_004.ogg"); // its plays are on the effects bus

    tone = Voice.create(makeTone());

    settings = [
        { name: "music volume", min: 0, max: 1.5,
            get: () => Voice.getVolume(music), set: (v) => Voice.setVolume(music, v) },
        { name: "music pitch", min: 0.25, max: 2,
            get: () => Voice.getPitch(music), set: (v) => Voice.setPitch(music, v) },
        { name: "music pan", min: -1, max: 1, get: () => Voice.getPan(music), set: (v) => Voice.setPan(music, v) },
        busSetting("music bus", Bus.getMusic()),
        busSetting("effects bus", Bus.getEffects()),
        { name: "master volume", min: 0, max: 1.5, get: () => Audio.getVolume(), set: (v) => Audio.setVolume(v) },
    ];
}

const loaded = () => Resource.getStatus(Voice.getSound(music)) === ResourceStatus.READY
    && Resource.getStatus(click) === ResourceStatus.READY;

// Plays the click once, at `pan` (-1 left to 1 right), and forgets it.
function playClick(pan) {
    if (Sound.play(click, 0.8, 1, pan)) {
        plays++;
        flash = 1;
    }
}

function frame() {
    Mouse.getPosition(mouse);
    if (App.canQuit() && Keyboard.isPressed(KeyboardKey.ESCAPE)) App.quit();

    if (Keyboard.isPressed(KeyboardKey.SPACE)) Voice.play(music);
    if (Keyboard.isPressed(KeyboardKey.P) && !Voice.pause(music)) Voice.resume(music);
    if (Keyboard.isPressed(KeyboardKey.S)) Voice.stop(music);
    if (Keyboard.isPressed(KeyboardKey.T)) Voice.play(tone);
    if (Keyboard.isPressed(KeyboardKey.ENTER)) playClick(0);
    if (Mouse.isPressed(MouseButton.LEFT)) playClick(mouse.x / 400 - 1);

    // Up and Down choose a setting. Left and Right move it, half its range a second.
    const count = settings.length;
    if (Keyboard.isRepeated(KeyboardKey.UP)) selected = (selected + count - 1) % count;
    if (Keyboard.isRepeated(KeyboardKey.DOWN)) selected = (selected + 1) % count;
    const dir = (Keyboard.isDown(KeyboardKey.RIGHT) ? 1 : 0) - (Keyboard.isDown(KeyboardKey.LEFT) ? 1 : 0);
    if (dir !== 0) {
        const s = settings[selected];
        const v = s.get() + dir * (s.max - s.min) * 0.5 * Loop.getFrameDelta();
        s.set(Math.min(s.max, Math.max(s.min, v)));
    }
    flash = Math.max(0, flash - Loop.getFrameDelta() * 3);

    draw();

    // Probes publish numbers an autopilot run can check (smoke.autopilot).
    Probe.setValue("audio.loaded", loaded() ? 1 : 0);
    Probe.setValue("audio.music_state", Voice.getState(music));
    Probe.setValue("audio.tone_state", Voice.getState(tone));
    Probe.setValue("audio.plays", plays);
    Probe.setValue("audio.music_volume", Voice.getVolume(music));
}

const round = (value, places) => Math.round(value * 10 ** places) / 10 ** places;
const stateNames = {
    [PlayState.PLAYING]: "playing", [PlayState.PAUSED]: "paused", [PlayState.COMPLETE]: "complete",
};

// A voice's line: its name, its state, and its position along its sound.
function drawVoice(name, voice, y, color) {
    const length = Sound.getDuration(Voice.getSound(voice));
    const at = Voice.getPosition(voice);
    Draw.text(0, `${name}: ${stateNames[Voice.getState(voice)] ?? "stopped"}`, 40, y, 18, color);
    Draw.rectangle(240, y + 4, 300, 10, Color.make(50, 54, 66, 255));
    if (length > 0) Draw.rectangle(240, y + 4, 300 * at / length, 10, color);
    Draw.text(0, `${round(at, 1)} / ${round(length, 1)} s`, 560, y, 18, color);
}

function draw() {
    const white = Color.get(ColorStock.RAYWHITE);
    const gray = Color.get(ColorStock.GRAY);
    Draw.text(0, "libwgf audio", 40, 30, 32, white);
    Draw.text(0, loaded() ? "sounds loaded" : "loading sounds...", 40, 70, 16, gray);

    drawVoice("music", music, 120, Color.get(ColorStock.SKYBLUE));
    drawVoice("tone", tone, 180, Color.get(ColorStock.GOLD));
    Draw.text(0, `click: played ${plays} times`, 40, 240, 18, Color.get(ColorStock.LIME));
    Draw.circleLines(700, 160, 30 + (1 - flash) * 40, 3, Color.make(0, 228, 48, Math.floor(flash * 255)));

    settings.forEach((s, i) => {
        const y = 280 + i * 30;
        const on = i === selected;
        Draw.text(0, (on ? "> " : "  ") + s.name, 40, y, 18, on ? white : gray);
        Draw.rectangle(240, y + 4, 300, 10, Color.make(50, 54, 66, 255));
        Draw.rectangle(240, y + 4, 300 * (s.get() - s.min) / (s.max - s.min), 10,
            Color.get(on ? ColorStock.ORANGE : ColorStock.DARKGRAY));
        Draw.text(0, `${round(s.get(), 2)}`, 560, y, 18, on ? white : gray);
    });

    Draw.text(0, "[Space] play  [P] pause/resume  [S] stop  [Enter]/click: click  [T] tone", 40, 480, 16, gray);
    Draw.text(0, "[Up/Down] choose a setting  [Left/Right] change it" + (App.canQuit() ? "  [Esc] quit" : ""),
        40, 504, 16, gray);
    Draw.text(0, "In a browser, sound starts after your first key or click.", 40, 528, 16, gray);
}

Window.setTitle("libwgf audio (JS)");
// The design size, fitted to any window or page. Window.setSize would fix the canvas at that
// size on the web, unscaled, so it is left to the presentation.
Presentation.set(PresentationMode.FIT, 800, 600);
Window.setMsaa(true);
App.run(init, null, frame, null);
