// What a call from JS into libwgf costs through the JS binding: the same calls Main.hx
// makes from Haxe (tools/bench/measure_calls.py runs both), on the same release host.
// Each shape, the median of REPS runs of N calls, in ns per call; one line each,
// `calls: <shape> <ns>`, then `calls done: PASS`.
import createWgfHost from "./wgf-host.js";
import * as wgf from "./wgf.js";

const N = 200000, REPS = 7, BULK = 1000;
// The chat: a scroll area of LINES lines of 80 characters, as a chat or a log shows them,
// given again each frame, timed over FRAMES frames after WARM.
const LINES = 200, WARM = 30, FRAMES = 31;
const LINE = "player_42: has anyone found the key to the north gate? I looked behind the inn.!";
const host = await createWgfHost({
    canvas: document.getElementById("canvas"),
    printErr: (text) => (/^\[(ERROR|FATAL)/.test(text) ? console.error : console.log)(text),
});
wgf.attach(host);
// Main.hx adds each result into a static (Main.sink), a property write each call; so does
// this, or the two would differ by that write and not by the calls (about 4 ns, measured).
class Main {
    static sink = 0;
}
let best = 0;

function report(shape, calls, body) {
    body(); // warm: the JIT's tiers, the slots grown
    const runs = [];
    for (let r = 0; r < REPS; r++) {
        const start = performance.now();
        body();
        runs.push((performance.now() - start) * 1e6 / calls);
    }
    runs.sort((a, b) => a - b);
    best = runs[REPS >> 1];
    console.log(`calls: ${shape} ${Math.round(best * 1000) / 1000}`);
}

function init() {
    const root = wgf.wgf_actor_create();
    const actors = [];
    for (let i = 0; i < BULK; i++) {
        actors.push(wgf.wgf_actor_create());
        wgf.wgf_actor_set_parent(actors[i], root);
    }
    const first = actors[0];
    const kept = { x: 0, y: 0, z: 0 };
    const out = new Array(BULK * 3).fill(0);
    report("set", N, () => {
        for (let k = 0; k < N; k++) if (wgf.wgf_actor_set_position(first, k, 2, 3)) Main.sink += 1;
    });
    report("get", N, () => { for (let k = 0; k < N; k++) Main.sink += wgf.wgf_actor_get_position(first, kept).x; });
    report("transform", N, () => {
        for (let k = 0; k < N; k++) if (wgf.wgf_actor_set_transform(first, k, 2, 3, 0, 0, 1, 1, 1, 1)) Main.sink += 1;
    });
    report("string", N, () => {
        for (let k = 0; k < N; k++) if (wgf.wgf_actor_set_name(first, "first")) Main.sink += 1;
    });
    report("string-80", N, () => {
        for (let k = 0; k < N; k++) if (wgf.wgf_actor_set_name(first, LINE)) Main.sink += 1;
    });
    const calls = Math.trunc(N / BULK) * 10;
    report("bulk", calls, () => {
        for (let k = 0; k < calls; k++) Main.sink += wgf.wgf_actor_get_transforms(actors, out, null, null);
    });
    console.log(`calls: bulk-actor ${Math.round(best / BULK * 1000) / 1000}`);
    report("bulk-set", calls, () => {
        for (let k = 0; k < calls; k++) if (wgf.wgf_actor_set_transforms(actors, out, null, null)) Main.sink += 1;
    });
    console.log(`calls: bulk-set-actor ${Math.round(best / BULK * 1000) / 1000}`);
}

const frames = [], viewFrames = [];
let history = 0; // the chat's lines in a text buffer, for the text view

// One UI frame a frame: the chat's lines given as labels, laid out, and drawn at wgf_ui_end;
// then as many frames of the same lines in a text buffer, shown by a text view.
function frame() {
    if (frames.length < WARM + FRAMES) {
        const start = performance.now();
        if (wgf.wgf_ui_begin()) {
            wgf.wgf_ui_begin_scroll("chat", 600, 400);
            for (let i = 0; i < LINES; i++) if (wgf.wgf_ui_label(LINE, 0)) Main.sink += 1;
            wgf.wgf_ui_end_scroll();
            wgf.wgf_ui_end();
        }
        frames.push((performance.now() - start) * 1e6);
        return;
    }
    if (history === 0) history = wgf.wgf_text_buffer_create_text(new Array(LINES).fill(LINE).join("\n"));
    const start = performance.now();
    if (wgf.wgf_ui_begin()) {
        if (wgf.wgf_ui_text_view("view", history, 600, 400, false)) Main.sink += 1;
        wgf.wgf_ui_end();
    }
    viewFrames.push((performance.now() - start) * 1e6);
    if (viewFrames.length < WARM + FRAMES) return;
    const timed = frames.slice(WARM).sort((a, b) => a - b);
    const viewed = viewFrames.slice(WARM).sort((a, b) => a - b);
    console.log(`calls: chat ${Math.round(timed[FRAMES >> 1])}`);
    console.log(`calls: chat-view ${Math.round(viewed[FRAMES >> 1])}`);
    console.log(Main.sink !== 0 ? "calls done: PASS" : "calls done: FAIL (nothing was called)");
    wgf.wgf_app_quit();
}

wgf.wgf_app_run(init, null, frame, null);
