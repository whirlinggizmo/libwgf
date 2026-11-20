#include "wgf_platform_priv.h"

#include <emscripten.h>
#include <stdint.h>
#include <string.h>
#include <emscripten/eventloop.h>

#include "sokol_app.h"

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

void wgf_platform_priv_sokol_poll(void (*deliver)(const sapp_event *event))
{
    char text[256];
    const int n = web_take_keyboard(text, (int)sizeof(text));
    int i = 0;
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
