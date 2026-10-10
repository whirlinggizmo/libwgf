// The calls the JS binding works itself (bindings/js/src/local.js) held to the host's C, to
// the bit, under node on the full headless host (tools/check_js_binding.py, "local"):
//   node local.mjs <the host's folder: wgf-host.js and wgf.js>
// Every stock color, colors made and lerped over a grid (C's float arithmetic included),
// and the trigonometry over the sweep wgf_math_test hashes, to the same hash and call by
// call. Prints PASS or each FAIL; exits 0 when every one agrees.
import { pathToFileURL } from 'node:url';
import { resolve, join } from 'node:path';

const dir = process.argv[2];
const host = await (await import(pathToFileURL(resolve(join(dir, 'wgf-host.js'))).href)).default();
const wgf = await import(pathToFileURL(resolve(join(dir, 'wgf.js'))).href);
wgf.attach(host);
const c = (name, ...args) => host['_' + name](...args);
let failures = 0;
const expect = (ok, what) => { if (!ok) { failures++; console.log(`FAIL: ${what}`); } };

let same = true;
for (let stock = -1; stock < 27; stock++) same &&= wgf.wgf_color_get(stock) === c('wgf_color_get', stock) >>> 0;
for (const v of [-20, 0, 1, 127, 128, 254, 255, 300]) {
    same &&= wgf.wgf_color_make(v, 255 - v, v * 3, 200) === c('wgf_color_make', v, 255 - v, v * 3, 200) >>> 0;
    same &&= wgf.wgf_color_with_alpha(0x11223344, v) === c('wgf_color_with_alpha', 0x11223344, v) >>> 0;
}
const colors = [0, 0xFFFFFFFF, 0x11223344, 0xFF8000C0, 0x00FF7F01];
for (const col of colors) {
    for (const part of ['red', 'green', 'blue', 'alpha']) {
        same &&= wgf[`wgf_color_get_${part}`](col) === c(`wgf_color_get_${part}`, col);
    }
    for (const to of colors) {
        for (let i = -2; i < 13; i++) {
            const t = i / 10 + 0.0137;
            same &&= wgf.wgf_color_lerp(col, to, t) === c('wgf_color_lerp', col, to, t) >>> 0;
        }
    }
}
for (let i = -5; i < 106; i++) {
    const v = i / 100 + 0.00321;
    same &&= wgf.wgf_color_make_float(v, 1 - v, v * 0.5, 0.25) === c('wgf_color_make_float', v, 1 - v, v * 0.5, 0.25) >>> 0;
}
expect(same, "colors worked in JS: C's, to the bit");

const f32 = new Float32Array(1), u32 = new Uint32Array(f32.buffer);
const bits = (x) => { f32[0] = x; return u32[0]; };
let hash = 2166136261 >>> 0, agree = true;
for (let i = -200000; i <= 200000; i += 7) {
    const a = Math.fround(i * Math.fround(1e-4));
    const mine = [wgf.wgf_trig_sin(a), wgf.wgf_trig_cos(a), wgf.wgf_trig_atan2(a, 0.7),
        wgf.wgf_trig_asin(Math.fround(a / 20)), wgf.wgf_trig_acos(Math.fround(a / 20))];
    for (const v of mine) hash = Math.imul(hash ^ bits(v), 16777619) >>> 0;
    if (i % 7001 === 0) {
        agree &&= mine[0] === c('wgf_trig_sin', a) && mine[1] === c('wgf_trig_cos', a)
            && mine[2] === c('wgf_trig_atan2', a, 0.7) && mine[4] === c('wgf_trig_acos', Math.fround(a / 20));
    }
}
expect(hash === 0xFDD87A04, `trigonometry worked in JS: C's hash (${hash.toString(16)})`);
for (const x of [0, -0, 0.5, -3.75, 1e6, Infinity, NaN]) {
    for (const name of ['wgf_trig_sin', 'wgf_trig_cos', 'wgf_trig_tan']) agree &&= Object.is(wgf[name](x), c(name, x));
}
for (const [y, x] of [[0, 0], [0, -0], [-0, -1], [1, 0], [-1, 0], [Infinity, Infinity], [-Infinity, 2], [3, -Infinity]]) {
    agree &&= Object.is(wgf.wgf_trig_atan2(y, x), c('wgf_trig_atan2', y, x));
}
for (const x of [-1, 1, 0, -0, 2, -2, 0.999]) {
    agree &&= Object.is(wgf.wgf_trig_asin(x), c('wgf_trig_asin', x)) && Object.is(wgf.wgf_trig_acos(x), c('wgf_trig_acos', x));
}
expect(agree, "and C's call by call, its edges included");
console.log(failures === 0 ? 'local: PASS' : `local: FAIL (${failures})`);
process.exit(failures === 0 ? 0 : 1);
