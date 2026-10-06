// What a call from JS into libwgf costs through the JS binding: the same calls Main.hx
// makes from Haxe (tools/bench/measure_calls.py runs both), on the same release host.
// Each shape, the median of REPS runs of N calls, in ns per call; one line each,
// `calls: <shape> <ns>`, then `calls done: PASS`.
import createWgfHost from "./wgf-host.js";
import * as wgf from "./wgf.js";

const N = 200000, REPS = 7, BULK = 1000;
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
    const root = wgf.wgf_node_create();
    const entities = [];
    for (let i = 0; i < BULK; i++) entities.push(wgf.wgf_entity_create(root));
    const first = entities[0];
    const kept = { x: 0, y: 0, z: 0 };
    const out = new Array(BULK * 3).fill(0);
    report("set", N, () => {
        for (let k = 0; k < N; k++) if (wgf.wgf_entity_set_position(first, k, 2, 3)) Main.sink += 1;
    });
    report("get", N, () => { for (let k = 0; k < N; k++) Main.sink += wgf.wgf_entity_get_position(first, kept).x; });
    report("transform", N, () => {
        for (let k = 0; k < N; k++) if (wgf.wgf_entity_set_transform(first, k, 2, 3, 0, 0, 1, 1, 1, 1)) Main.sink += 1;
    });
    report("string", N, () => {
        for (let k = 0; k < N; k++) if (wgf.wgf_entity_set_name(first, "first")) Main.sink += 1;
    });
    const calls = Math.trunc(N / BULK) * 10;
    report("bulk", calls, () => {
        for (let k = 0; k < calls; k++) Main.sink += wgf.wgf_entity_get_positions(entities, out);
    });
    console.log(`calls: bulk-entity ${Math.round(best / BULK * 1000) / 1000}`);
    console.log(Main.sink !== 0 ? "calls done: PASS" : "calls done: FAIL (nothing was called)");
    wgf.wgf_app_quit();
}

wgf.wgf_app_run(init, null, null, null);
