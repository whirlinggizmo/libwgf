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
    const z = r * r, zz = z * z;
    let low = z * 0.008333331873837089, high = z * 2.7249864655139585e-06;
    low = low - 0.16666666663853044;
    high = high - 0.00019840086263755443;
    high = high * zz;
    let p = low + high;
    low = r * z;
    p = p * low;
    return r + p;
}

function localCosReduced(r) {
    const z = r * r, zz = z * z;
    let low = z * 0.041666666637372694, high = z * 2.479986150250612e-05;
    const top = zz * -2.7237076731325097e-07;
    low = low - 0.49999999999963857;
    high = high - 0.0013888885089126252;
    high = high + top;
    high = high * zz;
    let p = low + high;
    p = p * z;
    return 1 + p;
}

// sin (cosine false) or cos of x, in double.
function localSinOrCos(x, cosine) {
    if (x === 0) return cosine ? 1 : x; // a zero's sine keeps its sign, as C's
    // past 1.6e6, x brought within a turn first, as C's fmod (% is exact too)
    let v = x * 0.63661977236758134308;
    if (!(v > -1048576 && v < 1048576)) {
        x = x % 6.28318530717958647693;
        v = x * 0.63661977236758134308;
    }
    // x * 2/pi rounded to the nearest integer, a tie to even, as C's: by 1.5 * 2^52
    const k = (v + 6755399441055744) - 6755399441055744;
    const quadrant = k - 4 * Math.floor(k * 0.25);
    let r = x - k * 1.57079632673412561417e+00;
    r = r - k * 6.07710050630396597660e-11;
    r = r - k * 2.02226624879595063154e-21;
    const q = (quadrant + (cosine ? 1 : 0)) & 3; // the one polynomial its quadrant needs
    const value = (q & 1) ? localCosReduced(r) : localSinReduced(r);
    return (q & 2) ? -value : value;
}

function localAtanReduced(x) {
    const x2 = x * x;
    let p = -6.6666666666666667e-02;
    p = p * x2;
    p = p + 7.6923076923076927e-02;
    p = p * x2;
    p = p - 9.0909090909090912e-02;
    p = p * x2;
    p = p + 1.1111111111111111e-01;
    p = p * x2;
    p = p - 1.4285714285714285e-01;
    p = p * x2;
    p = p + 2.0000000000000001e-01;
    p = p * x2;
    p = p - 3.3333333333333331e-01;
    p = p * x2;
    p = p * x;
    return x + p;
}

// each multiple of pi/16's tangent, its angle, and the tangent halfway to the next
const LOCAL_ATAN_TANGENT = [0.0, 0.198912367379658, 0.41421356237309503, 0.6681786379192989,
    1.0, 1.496605762665489, 2.414213562373095, 5.027339492125848];
const LOCAL_ATAN_ANGLE = [0.0, 0.19634954084936207, 0.39269908169872414, 0.5890486225480862,
    0.7853981633974483, 0.9817477042468103, 1.1780972450961724, 1.3744467859455345];
const LOCAL_ATAN_UPTO = [0.09849140335716425, 0.3033466836073424, 0.5345111359507917, 0.8206787908286604,
    1.2185035255879764, 1.8708684117893895, 3.2965582089383205, 10.15317038760886];

function localAtan(x) {
    const t = x < 0 ? -x : x;
    let a;
    if (t <= LOCAL_ATAN_UPTO[0]) {
        a = localAtanReduced(t);
    } else if (t > LOCAL_ATAN_UPTO[7]) {
        a = LOCAL_PI / 2 - localAtanReduced(1 / t);
    } else {
        let i = 1;
        while (t > LOCAL_ATAN_UPTO[i]) i++;
        a = LOCAL_ATAN_ANGLE[i] + localAtanReduced((t - LOCAL_ATAN_TANGENT[i]) / (t * LOCAL_ATAN_TANGENT[i] + 1));
    }
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

// wgf: call wgf_trig_cos_sin
export function wgf_trig_cos_sin(radians, into) {
    const x = Math.fround(radians);
    const c = Number.isFinite(x) ? Math.fround(localSinOrCos(x, true)) : NaN;
    const s = Number.isFinite(x) ? Math.fround(localSinOrCos(x, false)) : NaN;
    if (into === null || into === undefined) return { "x": c, "y": s };
    if (Array.isArray(into) || ArrayBuffer.isView(into)) {
        into[0] = c;
        into[1] = s;
    } else {
        into["x"] = c;
        into["y"] = s;
    }
    return into;
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
