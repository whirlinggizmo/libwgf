// The calls the JS binding works itself rather than crossing into the host (LOCAL in
// tools/jsbinding.py): arithmetic on their arguments alone, which a crossing into wasm
// costs more than, for a JS program and Haxe's JS target alike, worked here to the same
// bits C gives -- the colors' (libwgf/gfx/src/color/wgf_gfx_color.c) and the trigonometry's
// (libwgf/math/src/wgf_math_trig.c). Each step rounds where C's does: to a 32-bit float after every
// float operation (Math.fround: a double operation on floats, then rounded, is the float
// operation's result), and nowhere else. tools/gen_binding.py puts each block where its call
// goes in wgf.js, and a release copy keeps a group's helpers only with a call of its group;
// the binding's test (bindings/js/tests/local.mjs) holds every one to the host's C.

// wgf: local color
const LOCAL_STOCK = [
    0x00000000, 0xFFFFFFFF, 0x000000FF, 0xC8C8C8FF, 0x828282FF, 0x505050FF, 0xFFFF00FF, 0xFFCB00FF, 0xFFA100FF,
    0xFF6DC2FF, 0xE62937FF, 0xBE212DFF, 0x00E430FF, 0x009E2FFF, 0x00752CFF, 0x66BFFFFF, 0x0079F1FF, 0x0052ACFF,
    0xC87AFFFF, 0x873CBEFF, 0x701F7EFF, 0xD3B083FF, 0x7F6A4FFF, 0x4C3F2FFF, 0xFF00FFFF, 0xF5F5F5FF,
];

function localComponent(v) {
    v |= 0;
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

// 0..1 to 0..255, clamped and rounded to the nearest step, in C's float arithmetic.
function localComponentFloat(v) {
    const scaled = Math.fround(Math.fround(Math.fround(v) * 255) + 0.5);
    return Math.trunc(scaled < 0 ? 0 : scaled > 255 ? 255 : scaled);
}

// wgf: call wgf_color_get
export function wgf_color_get(stock) {
    return stock >= 0 && stock < LOCAL_STOCK.length ? LOCAL_STOCK[stock | 0] : 0;
}

// wgf: call wgf_color_make
export function wgf_color_make(r, g, b, a) {
    return ((localComponent(r) << 24) | (localComponent(g) << 16) | (localComponent(b) << 8) | localComponent(a)) >>> 0;
}

// wgf: call wgf_color_make_float
export function wgf_color_make_float(r, g, b, a) {
    return ((localComponentFloat(r) << 24) | (localComponentFloat(g) << 16) | (localComponentFloat(b) << 8)
        | localComponentFloat(a)) >>> 0;
}

// wgf: call wgf_color_with_alpha
export function wgf_color_with_alpha(color, a) {
    return ((color & 0xFFFFFF00) | localComponent(a)) >>> 0;
}

// wgf: call wgf_color_get_red
export function wgf_color_get_red(color) {
    return (color >>> 24) & 0xFF;
}

// wgf: call wgf_color_get_green
export function wgf_color_get_green(color) {
    return (color >>> 16) & 0xFF;
}

// wgf: call wgf_color_get_blue
export function wgf_color_get_blue(color) {
    return (color >>> 8) & 0xFF;
}

// wgf: call wgf_color_get_alpha
export function wgf_color_get_alpha(color) {
    return color & 0xFF;
}

// wgf: call wgf_color_lerp
export function wgf_color_lerp(from, to, t) {
    const t32 = Math.fround(t);
    const k = t32 < 0 ? 0 : t32 > 1 ? 1 : t32;
    let out = 0;
    for (let shift = 24; shift >= 0; shift -= 8) {
        const a = (from >>> shift) & 0xFF, b = (to >>> shift) & 0xFF;
        const step = Math.fround(Math.fround(b - a) * k);
        out |= (Math.trunc(Math.fround(Math.fround(a + step) + 0.5)) & 0xFF) << shift;
    }
    return out >>> 0;
}

// wgf: local trig
const LOCAL_PI = 3.14159265358979323846;

function localSinReduced(r) {
    const r2 = r * r;
    let p = 2.8114572543455206e-15;
    p = p * r2; p = p - 7.6471637318198164e-13;
    p = p * r2; p = p + 1.6059043836821613e-10;
    p = p * r2; p = p - 2.5052108385441720e-08;
    p = p * r2; p = p + 2.7557319223985893e-06;
    p = p * r2; p = p - 1.9841269841269841e-04;
    p = p * r2; p = p + 8.3333333333333333e-03;
    p = p * r2; p = p - 1.6666666666666667e-01;
    p = p * r2; p = p * r;
    return r + p;
}

function localCosReduced(r) {
    const r2 = r * r;
    let p = -1.5619206968586225e-16;
    p = p * r2; p = p + 4.7794773323873853e-14;
    p = p * r2; p = p - 1.1470745597729725e-11;
    p = p * r2; p = p + 2.0876756987868099e-09;
    p = p * r2; p = p - 2.7557319223985888e-07;
    p = p * r2; p = p + 2.4801587301587302e-05;
    p = p * r2; p = p - 1.3888888888888889e-03;
    p = p * r2; p = p + 4.1666666666666667e-02;
    p = p * r2; p = p - 0.5;
    p = p * r2;
    return 1 + p;
}

// sin (cosine false) or cos of x, in double.
function localSinOrCos(x, cosine) {
    const k = Math.floor(x * 0.63661977236758134308 + 0.5);
    const quadrant = k - 4 * Math.floor(k * 0.25);
    let r = x - k * 1.57079632673412561417e+00;
    r = r - k * 6.07710050630396597660e-11;
    r = r - k * 2.02226624879595063154e-21;
    const s = localSinReduced(r), c = localCosReduced(r);
    switch ((quadrant + (cosine ? 1 : 0)) & 3) {
        case 0: return s;
        case 1: return c;
        case 2: return -s;
        default: return -c;
    }
}

function localAtanReduced(x) {
    const x2 = x * x;
    let p = 0;
    for (let n = 43; n >= 3; n -= 2) {
        p = p * x2;
        p = p + ((((n - 1) >> 1) % 2 === 0) ? 1 : -1) / n;
    }
    p = p * x2;
    p = p * x;
    return x + p;
}

function localAtan(x) {
    const t = x < 0 ? -x : x;
    const a = t <= 0.41421356237309504880 ? localAtanReduced(t)
        : t <= 2.41421356237309504880 ? LOCAL_PI / 4 + localAtanReduced((t - 1) / (t + 1))
            : LOCAL_PI / 2 - localAtanReduced(1 / t);
    return x < 0 ? -a : a;
}

function localNegative(x) {
    return x < 0 || Object.is(x, -0);
}

// wgf: call wgf_trig_sin
export function wgf_trig_sin(radians) {
    const x = Math.fround(radians);
    return Number.isFinite(x) ? Math.fround(localSinOrCos(x, false)) : NaN;
}

// wgf: call wgf_trig_cos
export function wgf_trig_cos(radians) {
    const x = Math.fround(radians);
    return Number.isFinite(x) ? Math.fround(localSinOrCos(x, true)) : NaN;
}

// wgf: call wgf_trig_tan
export function wgf_trig_tan(radians) {
    const x = Math.fround(radians);
    return Number.isFinite(x) ? Math.fround(localSinOrCos(x, false) / localSinOrCos(x, true)) : NaN;
}

// wgf: call wgf_trig_atan2
export function wgf_trig_atan2(yIn, xIn) {
    const y = Math.fround(yIn), x = Math.fround(xIn);
    if (Number.isNaN(y) || Number.isNaN(x)) return NaN;
    if (x === 0 && y === 0) return localNegative(x) ? (localNegative(y) ? Math.fround(-LOCAL_PI) : Math.fround(LOCAL_PI)) : y;
    if (!Number.isFinite(y) || !Number.isFinite(x)) return Math.fround(Math.atan2(y, x)); // the infinities' fixed angles
    if (x > 0) return Math.fround(localAtan(y / x));
    if (x < 0) return Math.fround(localAtan(y / x) + (localNegative(y) ? -LOCAL_PI : LOCAL_PI));
    return y > 0 ? Math.fround(LOCAL_PI / 2) : Math.fround(-LOCAL_PI / 2);
}

// wgf: call wgf_trig_asin
export function wgf_trig_asin(xIn) {
    const d = Math.fround(xIn);
    if (!(d >= -1 && d <= 1)) return NaN;
    if (d === 1 || d === -1) return Math.fround(d * LOCAL_PI / 2);
    return Math.fround(localAtan(d / Math.sqrt((1 - d) * (1 + d))));
}

// wgf: call wgf_trig_acos
export function wgf_trig_acos(xIn) {
    const d = Math.fround(xIn);
    if (!(d >= -1 && d <= 1)) return NaN;
    if (d === 0) return Math.fround(LOCAL_PI / 2);
    const a = localAtan(Math.sqrt((1 - d) * (1 + d)) / d);
    return Math.fround(d > 0 ? a : a + LOCAL_PI);
}
