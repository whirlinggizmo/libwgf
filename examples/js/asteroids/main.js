// Asteroids in JavaScript: games/asteroids (Haxe) ported call for call to the JS binding,
// on the game's own assets and flying its own playthrough (tools/check_js_binding.py).
//
// The world is the game's scene file: the ship, three sizes of rock, the bullet, and the
// explosion, each a prefab with its components. The ecs moves everything, wraps it, ages
// the bullets and explosions out, and finds the overlaps; the behaviors' objects only set
// intent (behaviors.js does what the Haxe binding's wgf.Behavior does). The title and
// game-over screens are libwgf's UI; the HUD is drawn text. Its probes are the Haxe
// game's: asteroids.state (0 title, 1 playing, 2 game over), score, lives, and wave.
//
// Its differences from the Haxe: the calls are the C names, the vectors kept objects
// the getters fill, and a rock's size a number.
import createWgfHost from "./wgf-host.js";
import * as wgf from "./wgf.js";
import { Behavior, register, of, tickAll, frameAll, endAll } from "./behaviors.js";
import { Sounds } from "./sounds.js";

const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    // libwgf logs every level to stderr: only its errors are the console's
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
    // an autopilot run (tools/check_js_binding.py's), when one was handed the page
    ...(typeof globalThis.wgfAutopilot === "string" ? { wgfAutopilot: globalThis.wgfAutopilot } : {}),
});
wgf.attach(host);

const WIDTH = 960, HEIGHT = 720;
const TITLE = 0, PLAYING = 1, GAME_OVER = 2;
const LARGE = 0, SMALL = 2;

let world = 0, scene = 0, sounds = null;
let state = TITLE, score = 0, lives = 3, wave = 0, best = 0;
let ready = false, waveDelay = 0, respawnDelay = 0, ship = 0;

const color = (stock) => wgf.wgf_color_get(stock);
const keyDown = (...keys) => keys.some((key) => wgf.wgf_keyboard_is_down(key));
const padDown = (...buttons) => buttons.some((button) => wgf.wgf_gamepad_is_down(0, button));

// ---- the ship -----------------------------------------------------------------------

const TURN = 4.2, THRUST = 420, BULLET_SPEED = 560, FIRE_EVERY = 0.16, BULLETS_MAX = 6, SAFE_FOR = 2;

class Ship extends Behavior {
    cooldown = 0;
    safe = SAFE_FOR;
    thrusting = false;
    heading = { x: 0, y: 0, z: 0 };
    velocity = { x: 0, y: 0, z: 0 };
    stick = { x: 0, y: 0 };
    flame = 0;

    onCreate() {
        wgf.wgf_collider_set_enabled(this.actor, false); // safe while it blinks
        this.flame = wgf.wgf_actor_find(this.actor, "emitter2d"); // its part, found once
    }

    onDestroy() {
        sounds.thrust(false);
    }

    onTick(dt) {
        if (state !== PLAYING) return;
        const e = this.actor;
        if (this.safe > 0) {
            this.safe -= dt;
            const shape = e /* the ship is its shape */;
            wgf.wgf_actor_set_visible(shape, this.safe <= 0 || Math.trunc(this.safe * 8) % 2 === 0);
            if (this.safe <= 0) wgf.wgf_collider_set_enabled(e, true); // rocks again
        }
        const stick = wgf.wgf_gamepad_get_stick(0, wgf.WGF_GAMEPAD_STICK_LEFT, this.stick).x;
        let turn = 0;
        const left = keyDown(wgf.WGF_KEY_LEFT, wgf.WGF_KEY_A) || padDown(wgf.WGF_GAMEPAD_BUTTON_DPAD_LEFT);
        const right = keyDown(wgf.WGF_KEY_RIGHT, wgf.WGF_KEY_D) || padDown(wgf.WGF_GAMEPAD_BUTTON_DPAD_RIGHT);
        if (left || stick < -0.4) turn -= 1;
        if (right || stick > 0.4) turn += 1;
        wgf.wgf_motion_set_spin(e, 0, 0, turn * TURN);

        const angle = wgf.wgf_actor_get_rotation(e, this.heading).z;
        const dx = Math.cos(angle), dy = Math.sin(angle);
        const thrust = keyDown(wgf.WGF_KEY_UP, wgf.WGF_KEY_W) || padDown(wgf.WGF_GAMEPAD_BUTTON_SOUTH)
            || wgf.wgf_gamepad_get_trigger(0, wgf.WGF_GAMEPAD_TRIGGER_RIGHT) > 0.3;
        if (thrust) {
            const v = wgf.wgf_motion_get_velocity(e, this.velocity);
            wgf.wgf_motion_set_velocity(e, v.x + dx * THRUST * dt, v.y + dy * THRUST * dt, 0);
        }
        if (thrust !== this.thrusting) {
            this.thrusting = thrust;
            sounds.thrust(thrust);
            wgf.wgf_emitter2d_set_emitting(this.flame, thrust);
        }

        this.cooldown -= dt;
        const fire = keyDown(wgf.WGF_KEY_SPACE)
            || padDown(wgf.WGF_GAMEPAD_BUTTON_EAST, wgf.WGF_GAMEPAD_BUTTON_RIGHT_BUMPER);
        if (fire && this.cooldown <= 0 && wgf.wgf_ecs_count_behavior("Bullet") < BULLETS_MAX) {
            this.cooldown = FIRE_EVERY;
            const at = wgf.wgf_actor_get_position(e, this.heading);
            const v = wgf.wgf_motion_get_velocity(e, this.velocity);
            fireBullet(at.x + dx * 18, at.y + dy * 18, v.x + dx * BULLET_SPEED, v.y + dy * BULLET_SPEED);
        }
    }

    /** Hit by a rock: an explosion where it was, and a life lost. */
    explode() {
        const at = wgf.wgf_actor_get_position(this.actor, this.heading);
        explosion(at.x, at.y, 2);
        sounds.play(sounds.bangLarge);
        wgf.wgf_actor_destroy(this.actor, wgf.WGF_ACTOR_DESTROY_CHILDREN);
        shipLost();
    }
}

/** The ship's outline at (x, y), pointing up: the HUD's lives. */
function drawShipIcon(x, y) {
    wgf.wgf_draw_polyline([x, y - 12, x + 8, y + 10, x, y + 5, x - 8, y + 10], true, 2,
                          color(wgf.WGF_COLOR_STOCK_SKYBLUE));
}

// ---- rocks, bullets, explosions -----------------------------------------------------

const PREFABS = ["rock_large", "rock_medium", "rock_small"];
const RADII = [44, 24, 12];
const SPEEDS = [50, 90, 140];
const POINTS = [20, 50, 100];

function spawnRock(size, x, y, direction) {
    const rock = wgf.wgf_scene_spawn(scene, PREFABS[size], world);
    wgf.wgf_actor_set_position(rock, x, y, 0);
    wgf.wgf_actor_snap(rock);
    const angle = direction !== undefined ? direction : wgf.wgf_random_get_range(0, Math.PI * 2);
    const speed = SPEEDS[size] * wgf.wgf_random_get_range(0.7, 1.3);
    wgf.wgf_motion_set_velocity(rock, Math.cos(angle) * speed, Math.sin(angle) * speed, 0);
    wgf.wgf_motion_set_spin(rock, 0, 0, wgf.wgf_random_get_range(-1.5, 1.5));
    wgf.wgf_behavior_set_param(rock, wgf.wgf_actor_find_behavior(rock, "Rock"), "size", String(size));
    return rock;
}

class Rock extends Behavior {
    size = LARGE;

    onCreate() {
        this.size = Math.trunc(wgf.wgf_behavior_get_param_number(this.actor, this.id, "size"));
        const radius = RADII[this.size], points = [];
        const corners = 9 + wgf.wgf_random_get_int(0, 3);
        for (let i = 0; i < corners; i++) {
            const a = i / corners * Math.PI * 2;
            const r = radius * wgf.wgf_random_get_range(0.72, 1.08);
            points.push(Math.cos(a) * r, Math.sin(a) * r);
        }
        wgf.wgf_shape2d_set_polygon(this.actor /* a rock is a shape */, points);
    }

    onTriggerEnter(other) {
        if (wgf.wgf_actor_get_kind(this.actor) === wgf.WGF_ACTOR_KIND_NONE || wgf.wgf_actor_get_kind(other) === wgf.WGF_ACTOR_KIND_NONE) return; // one gone this tick
        const object = of(other);
        if (object instanceof Bullet) {
            wgf.wgf_actor_destroy(other, wgf.WGF_ACTOR_DESTROY_CHILDREN);
            this.split();
        } else if (object instanceof Ship) {
            object.explode();
            this.split();
        }
    }

    split() {
        const at = wgf.wgf_actor_get_position(this.actor);
        addScore(POINTS[this.size]);
        explosion(at.x, at.y, this.size);
        sounds.play([sounds.bangLarge, sounds.bangMedium, sounds.bangSmall][this.size]);
        wgf.wgf_actor_destroy(this.actor, wgf.WGF_ACTOR_DESTROY_CHILDREN);
        if (this.size !== SMALL) {
            const heading = wgf.wgf_random_get_range(0, Math.PI * 2);
            spawnRock(this.size + 1, at.x, at.y, heading);
            spawnRock(this.size + 1, at.x, at.y, heading + Math.PI * wgf.wgf_random_get_range(0.6, 1.4));
        }
    }
}

/** A bullet: the ecs flies it, wraps it, and ages it out; a rock it meets ends it. */
class Bullet extends Behavior {}

function fireBullet(x, y, vx, vy) {
    const bullet = wgf.wgf_scene_spawn(scene, "bullet", world);
    wgf.wgf_actor_set_position(bullet, x, y, 0);
    wgf.wgf_actor_snap(bullet);
    wgf.wgf_motion_set_velocity(bullet, vx, vy, 0);
    sounds.play(sounds.fire);
}

/** Sparks at (x, y), as many as the size calls for: an emitter's burst that ages out. */
function explosion(x, y, size) {
    const sparks = wgf.wgf_scene_spawn(scene, "explosion", world);
    wgf.wgf_actor_set_position(sparks, x, y, 0);
    wgf.wgf_actor_snap(sparks);
    wgf.wgf_emitter2d_burst(sparks,
                            [40, 24, 14][Math.min(Math.max(size, 0), 2)]);
}

// ---- the game's states --------------------------------------------------------------

function start() {
    wgf.wgf_ecs_clear();
    score = 0;
    lives = 3;
    wave = 0;
    waveDelay = 0; // a delay left from the last game would cut this one's first wait short
    respawnDelay = 0;
    state = PLAYING;
    spawnShip();
    nextWave();
    sounds.play(sounds.start);
}

function spawnShip() {
    ship = wgf.wgf_scene_spawn(scene, "ship", world);
    wgf.wgf_actor_set_position(ship, WIDTH / 2, HEIGHT / 2, 0);
    wgf.wgf_actor_snap(ship);
}

function shipLost() {
    ship = 0;
    lives--;
    if (lives > 0) {
        respawnDelay = 2;
    } else {
        state = GAME_OVER;
        best = Math.max(score, best);
        wgf.wgf_ui_set_focus("again");
        sounds.play(sounds.gameOver);
    }
}

function nextWave() {
    wave++;
    for (let i = 0; i < 3 + wave; i++) {
        // at an edge, away from the ship in the middle
        const side = wgf.wgf_random_get_int(0, 3);
        const x = side === 0 ? 0 : side === 1 ? WIDTH : wgf.wgf_random_get_range(0, WIDTH);
        const y = side === 2 ? 0 : side === 3 ? HEIGHT : wgf.wgf_random_get_range(0, HEIGHT);
        spawnRock(LARGE, x, y);
    }
}

/** The title's drifting rocks, behind its menu. */
function titleField() {
    wgf.wgf_ecs_clear();
    for (let i = 0; i < 6; i++) {
        spawnRock(LARGE, wgf.wgf_random_get_range(0, WIDTH), wgf.wgf_random_get_range(0, HEIGHT));
    }
}

function addScore(points) {
    const before = Math.trunc(score / 10000);
    score += points;
    if (Math.trunc(score / 10000) > before) { // a ship every 10,000 points
        lives++;
        sounds.play(sounds.extraLife);
    }
}

// ---- the loop -----------------------------------------------------------------------

function init() {
    wgf.wgf_asset_set_host("assets");
    wgf.wgf_render_set_clear_color(wgf.wgf_color_make(6, 8, 14, 255));
    world = wgf.wgf_stage2d_create();
    scene = wgf.wgf_scene_create("scenes/asteroids.scene");
    sounds = new Sounds();
    register("Ship", (e) => new Ship(e));
    register("Rock", (e) => new Rock(e));
    register("Bullet", (e) => new Bullet(e));
    wgf.wgf_ui_set_style_color(wgf.WGF_UI_COLOR_PANEL, wgf.wgf_color_make(14, 18, 30, 230));
    wgf.wgf_ui_set_style_color(wgf.WGF_UI_COLOR_BUTTON, wgf.wgf_color_make(32, 40, 62, 255));
    wgf.wgf_ui_set_style_color(wgf.WGF_UI_COLOR_BUTTON_HOVERED, wgf.wgf_color_make(48, 60, 92, 255));
    wgf.wgf_ui_set_style_color(wgf.WGF_UI_COLOR_FOCUS, wgf.wgf_color_make(120, 220, 255, 255));
    wgf.wgf_ui_set_style_value(wgf.WGF_UI_VALUE_CORNER_RADIUS, 6);
    publish();
}

function tick() {
    const dt = wgf.wgf_loop_get_tick_delta();
    tickAll(dt);
    if (state === PLAYING) {
        if (ship === 0 && respawnDelay > 0) {
            respawnDelay -= dt;
            if (respawnDelay <= 0) spawnShip();
        }
        if (wgf.wgf_ecs_count_behavior("Rock") === 0) {
            waveDelay += dt;
            if (waveDelay > 1.5) {
                waveDelay = 0;
                nextWave();
            }
        }
    }
    publish();
}

function publish() {
    wgf.wgf_probe_set_value("asteroids.state", state);
    wgf.wgf_probe_set_value("asteroids.score", score);
    wgf.wgf_probe_set_value("asteroids.lives", lives);
    wgf.wgf_probe_set_value("asteroids.wave", wave);
    wgf.wgf_probe_set_value("asteroids.ready", ready ? 1 : 0); // its scene loaded: what an autopilot waits for
}

function frame() {
    frameAll(wgf.wgf_loop_get_frame_delta());
    if (!ready && wgf.wgf_resource_get_status(scene) === wgf.WGF_RESOURCE_STATUS_READY) {
        ready = true;
        titleField();
        wgf.wgf_ui_set_focus("play");
    }
    wgf.wgf_stage2d_draw(world);
    if (state === TITLE) {
        titleScreen();
    } else {
        hud();
        if (state === GAME_OVER) gameOverScreen();
    }
    if (wgf.wgf_app_can_quit() && wgf.wgf_keyboard_is_pressed(wgf.WGF_KEY_ESCAPE)) wgf.wgf_app_quit();
}

function hud() {
    wgf.wgf_draw_text(0, `${score}`, 24, 16, 32, color(wgf.WGF_COLOR_STOCK_WHITE));
    for (let i = 0; i < lives; i++) drawShipIcon(32 + i * 26, 72);
    wgf.wgf_draw_text_aligned(0, `wave ${wave}`, WIDTH - 24, 20, 18, color(wgf.WGF_COLOR_STOCK_LIGHTGRAY),
                              wgf.WGF_TEXT_HALIGN_RIGHT, wgf.WGF_TEXT_VALIGN_TOP);
}

function titleScreen() {
    if (!wgf.wgf_ui_begin()) return;
    wgf.wgf_ui_begin_panel("title");
    wgf.wgf_ui_set_align(wgf.WGF_UI_ALIGN_CENTER, wgf.WGF_UI_ALIGN_START);
    wgf.wgf_ui_label("ASTEROIDS", 64);
    wgf.wgf_ui_label("turn: left and right   thrust: up   fire: space", 16);
    wgf.wgf_ui_spacer(16);
    if (wgf.wgf_ui_button("play", "Play") && ready) start();
    if (wgf.wgf_app_can_quit() && wgf.wgf_ui_button("quit", "Quit")) wgf.wgf_app_quit();
    if (best > 0) wgf.wgf_ui_label(`best: ${best}`, 18);
    wgf.wgf_ui_end_panel();
    wgf.wgf_ui_end();
}

function gameOverScreen() {
    if (!wgf.wgf_ui_begin()) return;
    wgf.wgf_ui_begin_panel("over");
    wgf.wgf_ui_set_align(wgf.WGF_UI_ALIGN_CENTER, wgf.WGF_UI_ALIGN_START);
    wgf.wgf_ui_label("GAME OVER", 56);
    wgf.wgf_ui_label(`score: ${score}   best: ${best}`, 20);
    wgf.wgf_ui_spacer(12);
    if (wgf.wgf_ui_button("again", "Play again")) start();
    if (wgf.wgf_ui_button("title", "Title")) {
        state = TITLE;
        titleField();
        wgf.wgf_ui_set_focus("play");
    }
    wgf.wgf_ui_end_panel();
    wgf.wgf_ui_end();
}

wgf.wgf_window_set_title("Asteroids (JS)");
// the design the game is written in, fitted to any window or screen, bars around it
wgf.wgf_presentation_set(wgf.WGF_PRESENTATION_MODE_FIT, WIDTH, HEIGHT);
wgf.wgf_render_set_bar_color(wgf.wgf_color_make(2, 3, 6, 255));
wgf.wgf_app_run(init, tick, frame, endAll);
