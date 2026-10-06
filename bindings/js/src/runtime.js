// The JS binding's runtime: how a call crosses into libwgf's wasm host and back.
// Written by hand; tools/gen_binding.py puts it at the top of wgf.js, whose calls are
// built on it, so it is never loaded on its own. Its rules, each a measured hazard
// (wgrender-c's, where this binding was proven):
//
// - Never hold a heap view. The host grows its memory, which replaces HEAPF32 and the
//   rest; every read goes through host["HEAPF32"] where it is used. A pointer survives
//   growth; a view doesn't.
// - Nothing piles up on the wasm stack. A string goes on the stack for its call alone
//   (the call saves and restores it): an arena kept for a whole frame overflowed the
//   64 KB stack at 5,000 getters. A returned vector comes back through one result slot,
//   and an array through its own slot, each _malloc'd once, grown when too small, and
//   read out before the call returns.
// - Every name into the host, or into an object the caller passed, is a quoted key
//   (host["_wgf_..."], into["x"]): a minifier that mangles properties leaves quoted
//   keys alone, and the host's export names can't change to match. V8 compiles a
//   constant quoted key as it does a dotted one.

let host = notAttached();
let attached = false;

function notAttached() {
    return new Proxy({}, {
        get(_, name) {
            throw new Error(`wgf: ${String(name)} was reached before attach(host): a call at load time `
                + `runs before the host exists; make libwgf objects in init`);
        },
    });
}

/** Hand the binding its host: the module createWgfHost() resolved to. */
export function attach(module) {
    host = module;
    attached = true;
    resultPointer = 0;
    resultBytes = 0;
    slots.length = 0;
    slotBytes.length = 0;
}

/** Whether attach() has been given a host. */
export function isAttached() {
    return attached;
}

/** `s` NUL-terminated on the wasm stack (null as a null pointer), until the call's restore. */
function cstr(s) {
    if (s === null || s === undefined) return 0;
    const length = host["lengthBytesUTF8"](s) + 1;
    const pointer = host["stackAlloc"](length);
    host["stringToUTF8"](s, pointer, length);
    return pointer;
}

/** A C string out, copied ("" for a null pointer). */
function str(pointer) {
    return pointer === 0 ? "" : host["UTF8ToString"](pointer);
}

let resultPointer = 0;
let resultBytes = 0;

/** The slot a returned vector comes back through, at least `bytes` long. */
function result(bytes) {
    if (bytes > resultBytes) {
        if (resultPointer !== 0) host["_free"](resultPointer);
        resultBytes = Math.max(bytes, 64);
        resultPointer = host["_malloc"](resultBytes);
    }
    return resultPointer;
}

/** A vector of `count` floats at `pointer`: into `into` (an array or typed array by
 * index, any other object by x, y, z, w), or a new object when there is none. */
function vector(pointer, count, into) {
    const heap = host["HEAPF32"];
    const at = pointer >> 2;
    if (into === null || into === undefined) {
        return count === 2 ? { "x": heap[at], "y": heap[at + 1] }
            : count === 3 ? { "x": heap[at], "y": heap[at + 1], "z": heap[at + 2] }
            : { "x": heap[at], "y": heap[at + 1], "z": heap[at + 2], "w": heap[at + 3] };
    }
    if (Array.isArray(into) || ArrayBuffer.isView(into)) {
        for (let i = 0; i < count; i++) into[i] = heap[at + i];
        return into;
    }
    into["x"] = heap[at];
    into["y"] = heap[at + 1];
    if (count > 2) into["z"] = heap[at + 2];
    if (count > 3) into["w"] = heap[at + 3];
    return into;
}

const slots = [];
const slotBytes = [];

/** Array slot `index` (a call's first array is 0, its second 1), at least `bytes` long. */
function slot(index, bytes) {
    while (slots.length <= index) {
        slots.push(0);
        slotBytes.push(0);
    }
    if (bytes > slotBytes[index]) {
        if (slots[index] !== 0) host["_free"](slots[index]);
        slotBytes[index] = Math.max(bytes, 256);
        slots[index] = host["_malloc"](slotBytes[index]);
    }
    return slots[index];
}

/** How many elements an array, typed array, or byte buffer holds; 0 for null. */
function lengthOf(values) {
    return values === null || values === undefined ? 0
        : values instanceof ArrayBuffer ? values.byteLength : values.length;
}

/** `values` (an array or typed array) as 32-bit floats, or ints when `heapName` is
 * HEAP32 or HEAPU32, in slot `index`; 0 for null. */
function arrayIn(values, index, heapName) {
    if (values === null || values === undefined) return 0;
    const pointer = slot(index, values.length * 4);
    const heap = host[heapName];
    const at = pointer >> 2;
    if (ArrayBuffer.isView(values)) {
        heap.set(values, at);
    } else {
        for (let i = 0; i < values.length; i++) heap[at + i] = values[i];
    }
    return pointer;
}

/** Room in slot `index` for C to fill `values.length` of them. */
function arrayOut(values, index) {
    return values === null || values === undefined ? 0 : slot(index, values.length * 4);
}

/** What C filled at `pointer`, back into `values`. */
function arrayBack(pointer, values, heapName) {
    if (values === null || values === undefined) return;
    const heap = host[heapName];
    const at = pointer >> 2;
    if (ArrayBuffer.isView(values)) {
        values.set(heap.subarray(at, at + values.length));
    } else {
        for (let i = 0; i < values.length; i++) values[i] = heap[at + i];
    }
}

/** `bytes` (a Uint8Array or an ArrayBuffer) in slot `index`; 0 for null. */
function bytesIn(bytes, index) {
    if (bytes === null || bytes === undefined) return 0;
    const view = bytes instanceof ArrayBuffer ? new Uint8Array(bytes) : bytes;
    const pointer = slot(index, view.length);
    host["HEAPU8"].set(view, pointer);
    return pointer;
}

/** A byte span C owns, copied out: `size` bytes at `pointer`. */
function bytesOut(pointer, size) {
    return pointer === 0 || size <= 0 ? new Uint8Array(0) : host["HEAPU8"].slice(pointer, pointer + size);
}
