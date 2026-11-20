#include "wgf_platform_priv.h"

#include <emscripten.h>
#include <stdint.h>
#include <string.h>
#include <emscripten/eventloop.h>

#include "sokol_app.h"
#include "wgf_log.h"

/* The window on the web, where it is the page's canvas: what differs from the
 * desktop. */

/* The canvas's size on the page, in CSS pixels (logical); sokol_app reads it again
 * on a resize event, so one is sent. Whether it took: the page's CSS can override
 * it. */
EM_JS(int, canvas_set_size, (int width, int height), {
    const canvas = Module.canvas;
    if (!canvas) return 0;
    canvas.style.width = width + "px";
    canvas.style.height = height + "px";
    window.dispatchEvent(new Event("resize"));
    const rect = canvas.getBoundingClientRect();
    return Math.round(rect.width) === width && Math.round(rect.height) === height ? 1 : 0;
})
EM_JS(int, document_has_focus, (void), { return document.hasFocus() ? 1 : 0; })
EM_JS(int, screen_width, (void), { return window.screen.width | 0; })
EM_JS(int, screen_height, (void), { return window.screen.height | 0; })
/* False in an iframe without allowfullscreen, or under a permissions policy. */
EM_JS(int, document_fullscreen_enabled, (void), { return document.fullscreenEnabled ? 1 : 0; })

void wgf_platform_priv_sokol_hold(bool hold)
{
    if (hold) {
        emscripten_runtime_keepalive_push();
    } else {
        emscripten_runtime_keepalive_pop();
    }
}

void wgf_platform_priv_sokol_open(int width, int height, bool size_set)
{
    if (size_set) canvas_set_size(width, height);
}

bool wgf_platform_priv_set_size(int width, int height)
{
    return canvas_set_size(width, height) != 0;
}

bool wgf_platform_priv_can_quit(void)
{
    return false; /* a page has nothing to quit to */
}

/* The canvas is the window: it has no position, and there is one monitor, the
 * screen. */
bool wgf_platform_priv_set_position(int x, int y)
{
    (void)x, (void)y;
    return false;
}

bool wgf_platform_priv_get_position(int *x, int *y)
{
    *x = *y = 0;
    return false;
}

int wgf_platform_priv_get_monitor_count(void)
{
    return 1;
}

int wgf_platform_priv_get_monitor(void)
{
    return 0;
}

bool wgf_platform_priv_set_monitor(int monitor)
{
    return monitor == 0;
}

bool wgf_platform_priv_get_monitor_rect(int monitor, int *x, int *y, int *width, int *height)
{
    *x = *y = *width = *height = 0;
    if (monitor != 0) return false;
    *width = screen_width();
    *height = screen_height();
    return true;
}

const char *wgf_platform_priv_get_monitor_name(int monitor)
{
    return monitor == 0 ? "screen" : "";
}

bool wgf_platform_priv_has_fullscreen(void)
{
    return document_fullscreen_enabled() != 0;
}

bool wgf_platform_priv_is_focused(void)
{
    return document_has_focus() != 0;
}

/* The browser offers a frame each time the display shows one: run a frame due
 * within half of one, so 30 fps on a 60 Hz display runs every other frame instead
 * of drifting. */
bool wgf_platform_priv_pace(double wait)
{
    return wait <= 0.5 * wgf_platform_priv_get_frame_duration();
}

EM_JS(void, wgf_platform_js_mark, (const char *name), { performance.mark(UTF8ToString(name)); })

void wgf_platform_priv_mark(const char *name)
{
    wgf_platform_js_mark(name);
}

/* ---- the browser's tab, and the device ----------------------------------------------- */

EM_JS(int, web_open_url, (const char *url), {
    var opened = window.open(UTF8ToString(url), "_blank", "noopener");
    return opened !== null ? 1 : 0;
})
EM_JS(int, web_has_touch, (void), { return (navigator.maxTouchPoints | 0) > 0 ? 1 : 0; })
EM_JS(int, web_has_fine_pointer, (void), {
    return window.matchMedia && window.matchMedia("(any-pointer: fine)").matches ? 1 : 0;
})
EM_JS(int, web_os, (void), {
    var agent = (navigator.userAgent || "") + " " + (navigator.platform || "");
    if (/Android/i.test(agent)) return 5;
    if (/iPhone|iPad|iPod/i.test(agent) || (/Mac/i.test(agent) && (navigator.maxTouchPoints | 0) > 1)) return 6;
    if (/Win/i.test(agent)) return 2;
    if (/Mac/i.test(agent)) return 3;
    if (/Linux|X11|CrOS/i.test(agent)) return 1;
    return 0;
})

bool wgf_platform_priv_open_url(const char *url)
{
    return web_open_url(url) != 0;
}

bool wgf_platform_priv_has_touch(void)
{
    return web_has_touch() != 0;
}

/* ---- the on-screen keyboard: a hidden input element of the page's ------------------- */

/* The input made the first time one is shown: transparent, a pixel, at the caret, so the
 * page doesn't scroll away from it. It keeps one space, so a phone's Backspace has
 * something to delete: what it is given past the space is typed, the space gone is a
 * Backspace, each queued for the next frame. A desktop's keys reach the window's handlers
 * as before, which keep them from the input. Phones show a keyboard only for a focus
 * given while the user touches the page, so a keyboard asked for is focused again at
 * every touch's end or click until it isn't wanted. */
EM_JS(void, web_show_keyboard, (int shown, float x, float y, float height), {
    var k = Module.wgfKeyboard;
    if (!k) {
        if (!shown) return;
        var input = document.createElement("input");
        input.type = "text";
        input.setAttribute("autocomplete", "off");
        input.setAttribute("autocorrect", "off");
        input.setAttribute("autocapitalize", "off");
        input.setAttribute("spellcheck", "false");
        input.setAttribute("aria-hidden", "true");
        input.style.cssText = "position:fixed;left:0;top:0;width:1px;height:1px;opacity:0;padding:0;border:0;" +
                              "margin:0;outline:none;font-size:16px;pointer-events:none;z-index:-1";
        input.value = " ";
        k = Module.wgfKeyboard = {input : input, typed : "", wanted : false};
        input.addEventListener("input", function() {
            var value = input.value;
            if (value.length === 0) k.typed += "\b";
            else if (value.length > 1) k.typed += value.charAt(0) === " " ? value.substring(1) : value;
            input.value = " ";
            input.setSelectionRange(1, 1);
        });
        var refocus = function() {
            if (k.wanted && document.activeElement !== input) input.focus({preventScroll : true});
        };
        window.addEventListener("touchend", refocus, true);
        window.addEventListener("click", refocus, true);
        document.body.appendChild(input);
    }
    k.wanted = shown !== 0;
    if (k.wanted) {
        var rect = Module.canvas ? Module.canvas.getBoundingClientRect() : {left : 0, top : 0};
        k.input.style.left = Math.max(0, Math.round(rect.left + x)) + "px";
        k.input.style.top = Math.max(0, Math.round(rect.top + y + height)) + "px";
        if (document.activeElement !== k.input) k.input.focus({preventScroll : true});
    } else if (document.activeElement === k.input) {
        k.input.blur();
    }
})

EM_JS_DEPS(wgf_platform_keyboard, "$stringToUTF8,$lengthBytesUTF8")

/* What the input was given since, as UTF-8 into `out` (as much as fits whole), and taken. */
EM_JS(int, web_take_keyboard, (char *out, int size), {
    var k = Module.wgfKeyboard;
    if (!k || k.typed.length === 0) return 0;
    var text = k.typed;
    while (text.length > 0 && lengthBytesUTF8(text) >= size) text = text.substring(0, text.length - 1);
    k.typed = k.typed.substring(text.length);
    stringToUTF8(text, out, size);
    return lengthBytesUTF8(text);
})

static bool keyboard_shown;

void wgf_platform_priv_show_keyboard(bool shown, float x, float y, float height)
{
    keyboard_shown = shown;
    web_show_keyboard(shown ? 1 : 0, x, y, height);
}

bool wgf_platform_priv_is_keyboard_shown(void)
{
    return keyboard_shown;
}

static void deliver_key(void (*deliver)(const sapp_event *event), sapp_keycode code)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = SAPP_EVENTTYPE_KEY_DOWN;
    event.key_code = code;
    deliver(&event);
    event.type = SAPP_EVENTTYPE_KEY_UP;
    deliver(&event);
}

static void poll_orientation(void);

void wgf_platform_priv_sokol_poll(void (*deliver)(const sapp_event *event))
{
    char text[256];
    const int n = web_take_keyboard(text, (int)sizeof(text));
    int i = 0;
    poll_orientation();
    while (i < n) {
        const unsigned char c = (unsigned char)text[i];
        uint32_t code = c;
        int length = 1, k;
        if (c == '\b') {
            deliver_key(deliver, SAPP_KEYCODE_BACKSPACE);
            i++;
            continue;
        }
        if (c == '\n' || c == '\r') {
            deliver_key(deliver, SAPP_KEYCODE_ENTER);
            i++;
            continue;
        }
        if (c >= 0xF0) {
            code = c & 0x07u;
            length = 4;
        } else if (c >= 0xE0) {
            code = c & 0x0Fu;
            length = 3;
        } else if (c >= 0xC0) {
            code = c & 0x1Fu;
            length = 2;
        }
        for (k = 1; k < length && i + k < n; k++) code = (code << 6) | ((unsigned char)text[i + k] & 0x3Fu);
        i += length;
        if (code >= 32) {
            sapp_event event;
            memset(&event, 0, sizeof(event));
            event.type = SAPP_EVENTTYPE_CHAR;
            event.char_code = code;
            deliver(&event);
        }
    }
}

bool wgf_platform_priv_has_fine_pointer(void)
{
    return web_has_fine_pointer() != 0;
}

int wgf_platform_priv_get_os(void)
{
    return web_os();
}

/* ---- the safe area, and the screen's orientation -------------------------------------- */

/* The page's safe-area insets, read through an element out of sight whose padding is them (a
 * script reads env() only through a style, as helix's DomWindowAdapter does), made at the
 * first read; less what of each lies outside the canvas, in CSS pixels (logical), rounded
 * out. */
EM_JS(void, web_safe_insets, (int *out), {
    let probe = Module.wgfSafeArea;
    if (!probe) {
        probe = Module.wgfSafeArea = document.createElement("div");
        probe.style.cssText = "position:fixed;left:0;top:0;width:0;height:0;visibility:hidden;pointer-events:none;" +
                              "padding:env(safe-area-inset-top,0px) env(safe-area-inset-right,0px) " +
                              "env(safe-area-inset-bottom,0px) env(safe-area-inset-left,0px)";
        document.body.appendChild(probe);
    }
    const style = window.getComputedStyle(probe);
    const inset = (side) => {
        const value = parseFloat(style.getPropertyValue("padding-" + side));
        return value > 0 ? value : 0;
    };
    const root = document.documentElement;
    const width = root.clientWidth || window.innerWidth, height = root.clientHeight || window.innerHeight;
    const rect = Module.canvas ? Module.canvas.getBoundingClientRect()
                               : {left : 0, top : 0, right : width, bottom : height};
    HEAP32[out >> 2] = Math.ceil(Math.max(0, inset("top") - rect.top));
    HEAP32[(out >> 2) + 1] = Math.ceil(Math.max(0, inset("right") - (width - rect.right)));
    HEAP32[(out >> 2) + 2] = Math.ceil(Math.max(0, inset("bottom") - (height - rect.bottom)));
    HEAP32[(out >> 2) + 3] = Math.ceil(Math.max(0, inset("left") - rect.left));
})

void wgf_platform_priv_get_safe_insets(int insets[4])
{
    web_safe_insets(insets);
}

/* Fullscreen on the web: the canvas sokol_app draws to, asked at once; refused (no tap
 * this frame: the program asks in its frame, after the tap's event has ended, and only
 * Chrome lets that through, for a few seconds after a tap), the request is held, and asked
 * again inside the next pointer up, touch end, click, or key press, caught first (the
 * capture phase, on the window), so a game's Play tap, or its first tap anywhere when it
 * asked at start, takes the page fullscreen. Granted, the request is no longer held, so a
 * player who leaves fullscreen stays out. */
EM_JS(void, web_request_fullscreen, (int on), {
    let state = Module.wgfFullscreen;
    if (!state) {
        state = Module.wgfFullscreen = {wanted : false};
        const retry = () => {
            if (!state.wanted || document.fullscreenElement) return;
            const canvas = Module.sapp_emsc_target || Module.canvas;
            if (!canvas || typeof canvas.requestFullscreen !== "function") return;
            /* granted, the hold ends: a player leaving fullscreen (Escape, the back gesture)
               stays out until the program asks again */
            canvas.requestFullscreen().then(() => { state.wanted = false; }, () => {});
        };
        for (const type of [ "pointerup", "touchend", "click", "keydown" ]) {
            window.addEventListener(type, retry, true);
        }
        state.retry = retry;
    }
    state.wanted = on !== 0;
    if (!state.wanted) {
        if (document.fullscreenElement && typeof document.exitFullscreen === "function") {
            document.exitFullscreen().then(() => {}, () => {});
        }
        return;
    }
    state.retry();
})

void wgf_platform_priv_web_request_fullscreen(bool fullscreen)
{
    web_request_fullscreen(fullscreen ? 1 : 0);
}

EM_JS(int, document_is_fullscreen, (void), { return document.fullscreenElement ? 1 : 0; })

bool wgf_platform_priv_web_is_fullscreen(void)
{
    return document_is_fullscreen() != 0;
}

/* The lock: asked of screen.orientation, granted when its promise resolves, kept until let
 * go or the page leaves fullscreen (the browser lets go then); a refusal's reason kept for
 * the next frame to warn of. 1 landscape, 2 portrait, 0 none, as wgf_window_orientation_t. */
EM_JS(int, web_request_orientation, (int orientation), {
    const screen_orientation = window.screen && window.screen.orientation;
    if (!screen_orientation || typeof screen_orientation.lock !== "function") return 0;
    let state = Module.wgfOrientation;
    if (!state) {
        state = Module.wgfOrientation = {locked : 0, refused : "", wanted : 0};
        state.lock = () => {
            const wanted = state.wanted;
            const refused = (e) => {
                /* outside fullscreen a phone's browser refuses any lock: held, asked again as
                   fullscreen begins, so no warning */
                if (!document.fullscreenElement) return;
                state.refused = (e && e.name ? e.name + ": " : "") + (e && e.message ? e.message : String(e));
            };
            try {
                screen_orientation.lock(wanted === 1 ? "landscape" : "portrait")
                    .then(() => { if (state.wanted === wanted) state.locked = wanted; }, refused);
            } catch (e) {
                refused(e);
            }
        };
        document.addEventListener("fullscreenchange", () => {
            if (!document.fullscreenElement) state.locked = 0;
            else if (state.wanted && !state.locked) state.lock();
        });
    }
    state.wanted = orientation;
    if (orientation === 0) {
        try {
            screen_orientation.unlock();
        } catch (e) {
        }
        state.locked = 0;
        return 1;
    }
    state.lock();
    return 1;
})
EM_JS_DEPS(wgf_platform_orientation, "$stringToUTF8")
/* The lock held now (none outside fullscreen), and a refusal's reason since into `reason`,
 * taken: the lock, plus 4 when there is a reason. */
EM_JS(int, web_poll_orientation, (char *reason, int size), {
    const state = Module.wgfOrientation;
    if (!state) return 0;
    let answer = document.fullscreenElement ? state.locked : 0;
    if (state.refused) {
        stringToUTF8(state.refused, reason, size);
        state.refused = "";
        answer |= 4;
    }
    return answer;
})
EM_JS(int, web_can_lock_orientation, (void), {
    const screen_orientation = window.screen && window.screen.orientation;
    const lock = screen_orientation && typeof screen_orientation.lock === "function";
    return lock && (navigator.maxTouchPoints | 0) > 0 ? 1 : 0;
})

static int orientation_lock;

static void poll_orientation(void)
{
    char reason[256];
    const int answer = web_poll_orientation(reason, (int)sizeof(reason));
    orientation_lock = answer & 3;
    if (answer & 4) wgf_log_warn("wgf_window_request_orientation: the browser refused the lock: %s", reason);
}

bool wgf_platform_priv_request_orientation(int orientation)
{
    return web_request_orientation(orientation) != 0;
}

int wgf_platform_priv_get_orientation_lock(void)
{
    return orientation_lock;
}

bool wgf_platform_priv_can_lock_orientation(void)
{
    return web_can_lock_orientation() != 0;
}

/* The canvas's WebGL context: lost now, asked of the context itself (Emscripten's GLctx, the
 * one sokol_app made current), and the restores counted by a listener of the canvas's,
 * added at the first ask; sokol_app's own listener cancels the loss, so the browser gives
 * it back. Under quoted keys of Module, as the asset part's state is, so nothing is
 * exported. */
EM_JS(int, gpu_state, (void), {
    if (typeof GLctx === "undefined" || !GLctx) return 0;
    if (!Module["wgfGpuWatched"]) {
        Module["wgfGpuWatched"] = true;
        Module["wgfGpuRestores"] = 0;
        GLctx.canvas.addEventListener("webglcontextrestored", function() { Module["wgfGpuRestores"]++; });
    }
    return GLctx.isContextLost() ? 1 : 0;
})
EM_JS(int, gpu_restores, (void), { return Module["wgfGpuRestores"] | 0; })

/* Emscripten's side of a context given back: its tables' objects, the lost context's, let
 * go of (never deleted: they belong to no context now, and sokol makes its own again), and
 * the extensions it enables at a context's creation enabled again on the new one. */
EM_JS(void, gpu_reset, (void), {
    /* each table by name in the code, not as a string, so a closure build's renaming reaches it */
    var tables = [ GL.buffers, GL.programs, GL.framebuffers, GL.renderbuffers, GL.textures, GL.shaders, GL.vaos,
                   GL.samplers, GL.queries, GL.syncs, GL.transformFeedbacks ];
    for (var t = 0; t < tables.length; t++) {
        var table = tables[t];
        if (table) {
            for (var i = 0; i < table.length; i++) table[i] = null;
        }
    }
    var context = GL.currentContext;
    if (context) {
        context.initExtensionsDone = false;
        GL.initExtensions(context);
    }
})

bool wgf_platform_priv_gpu_is_lost(void)
{
    return gpu_state() != 0;
}

unsigned wgf_platform_priv_gpu_get_restores(void)
{
    return (unsigned)gpu_restores();
}

void wgf_platform_priv_gpu_reset(void)
{
    gpu_reset();
}
