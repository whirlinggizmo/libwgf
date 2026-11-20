#include <emscripten.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_gamepad.h"
#include "wgf_time.h"
#include "wgf_window.h"

/* The device in a browser (tools/run_in_browser.py), its APIs stood in for by ones that
 * record what libwgf asks: three pads from navigator.getGamepads, one with a vibration
 * actuator, one with none, one with only a haptic actuator, rumbled and stopped (by a call,
 * and once its time is up), the actuators' calls checked; the safe area's insets as the
 * page's CSS gives them (window.getComputedStyle answering for libwgf's probe element as
 * env(safe-area-inset-*) would), less what lies outside the canvas, and held to the
 * window's size; screen.orientation.lock granting landscape and refusing portrait, the lock
 * read at the next frame, kept through a refusal, let go outside fullscreen and by ANY.
 * wgf_platform_gamepad_test and wgf_platform_window_test are the logic's. */

static int failures;
static int phase;
static double since;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

EM_JS_DEPS(wgf_platform_device_web_test, "$UTF8ToString")

/* Whether a line of JS is true, against the stubs' log. */
EM_JS(int, js, (const char *code), { return eval(UTF8ToString(code)) ? 1 : 0; })

EM_JS(void, stub_device, (void), {
    const log = Module.deviceLog = {effects : [], pulses : [], locks : [], unlocks : 0};
    const buttons = () => Array.from({length : 17}, () => ({pressed : false, value : 0}));
    const pads = [
        {
            connected : true, id : "dual rumble pad", index : 0, axes : [ 0, 0, 0, 0 ], buttons : buttons(),
            vibrationActuator : {
                playEffect : (type, params) => {
                    log.effects.push([ type, params ]);
                    return Promise.resolve("complete");
                },
                reset : () => {
                    log.effects.push([ "reset" ]);
                    return Promise.resolve("complete");
                }
            }
        },
        {connected : true, id : "still pad", index : 1, axes : [ 0, 0, 0, 0 ], buttons : buttons()},
        {
            connected : true, id : "haptic pad", index : 2, axes : [ 0, 0, 0, 0 ], buttons : buttons(),
            hapticActuators : [ {
                pulse : (value, ms) => {
                    log.pulses.push([ value, ms ]);
                    return Promise.resolve(true);
                }
            } ]
        }
    ];
    Object.defineProperty(navigator, "getGamepads", {value : () => pads, configurable : true});

    Module.insets =
        {"padding-top" : "24px", "padding-right" : "10px", "padding-bottom" : "4.2px", "padding-left" : "16px"};
    const computed = window.getComputedStyle;
    window.getComputedStyle = function(element, pseudo) {
        if (element === Module.wgfSafeArea) return {getPropertyValue : (name) => Module.insets[name] || "0px"};
        return computed.call(window, element, pseudo);
    };
    Module.canvas.style.cssText = "position:fixed;left:0;top:0;width:100vw;height:100vh;margin:0";

    Module.fullscreen = false;
    Object.defineProperty(document, "fullscreenElement",
                          {get : () => Module.fullscreen ? Module.canvas : null, configurable : true});
    Object.defineProperty(navigator, "maxTouchPoints", {get : () => 5, configurable : true});
    Object.defineProperty(window.screen, "orientation", {
        value : {
            type : "landscape-primary",
            angle : 0,
            lock : (orientation) => {
                log.locks.push(orientation);
                return orientation === "portrait" ? Promise.reject(new DOMException("not now", "NotSupportedError"))
                                                  : Promise.resolve();
            },
            unlock : () => { log.unlocks++; }
        },
        configurable : true
    });
})

static void done(void)
{
    if (failures == 0) printf("wgf_platform_device_web_test: passed\n");
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void on_frame(void *user)
{
    const double now = wgf_time_get_seconds();
    (void)user;
    if (phase > 0 && now - since > 5.0) {
        printf("FAIL: phase %d timed out\n", phase);
        failures++;
        done();
    }
    switch (phase) {
        case 0:
            expect(wgf_gamepad_is_connected(0) && wgf_gamepad_is_connected(1) && wgf_gamepad_is_connected(2),
                   "three pads");
            expect(wgf_gamepad_can_rumble(0) && !wgf_gamepad_can_rumble(1) && wgf_gamepad_can_rumble(2),
                   "a vibration actuator or a haptic one can rumble; none can't");
            expect(!wgf_gamepad_rumble(1, 1.0f, 1.0f, 0.5f) && js("Module.deviceLog.effects.length === 0"),
                   "the pad without one refused");
            expect(wgf_gamepad_rumble(0, 0.25f, 0.75f, 0.1f) && wgf_gamepad_is_rumbling(0), "pad 0 rumbling");
            expect(js("const e = Module.deviceLog.effects; e.length === 1 && e[0][0] === 'dual-rumble' && "
                      "e[0][1].duration === 100 && e[0][1].strongMagnitude === 0.25 && e[0][1].weakMagnitude === 0.75"),
                   "a dual-rumble effect: 100 ms, strong 0.25, weak 0.75");
            expect(wgf_gamepad_rumble(2, 0.5f, 0.2f, 3.0f) && wgf_gamepad_stop_rumble(2) && !wgf_gamepad_is_rumbling(2),
                   "pad 2 rumbled and stopped");
            expect(js("const p = Module.deviceLog.pulses; p.length === 2 && p[0][0] === 0.5 && p[0][1] === 2000 && "
                      "p[1][0] === 0 && p[1][1] === 0"),
                   "a pulse at the stronger motor's strength for 2 seconds (clamped), then a stop");

            expect(wgf_window_get_safe_inset_top() == 24 && wgf_window_get_safe_inset_right() == 10 &&
                       wgf_window_get_safe_inset_bottom() == 5 && wgf_window_get_safe_inset_left() == 16,
                   "the page's insets, rounded out, over a canvas filling the page");

            expect(wgf_window_get_orientation() == (wgf_window_get_height() > wgf_window_get_width()
                                                        ? WGF_WINDOW_ORIENTATION_PORTRAIT
                                                        : WGF_WINDOW_ORIENTATION_LANDSCAPE),
                   "the orientation is the window's shape");
            expect(wgf_window_can_lock_orientation(), "a lock offered: a lock function, and touch");
            expect(!wgf_window_request_orientation((wgf_window_orientation_t)9) &&
                       js("Module.deviceLog.locks.length === 0"),
                   "a value that isn't one refused, nothing asked");
            js("Module.fullscreen = true");
            expect(wgf_window_request_orientation(WGF_WINDOW_ORIENTATION_LANDSCAPE) &&
                       js("Module.deviceLog.locks[0] === 'landscape'"),
                   "landscape asked of the browser");
            expect(wgf_window_get_orientation_lock() == WGF_WINDOW_ORIENTATION_ANY, "not held before a frame's start");
            phase = 1;
            since = now;
            break;
        case 1: /* the promise resolves, and the next frame reads it */
            if (wgf_window_get_orientation_lock() == WGF_WINDOW_ORIENTATION_LANDSCAPE) {
                expect(wgf_window_request_orientation(WGF_WINDOW_ORIENTATION_PORTRAIT) &&
                           js("Module.deviceLog.locks[1] === 'portrait'"),
                       "portrait asked of the browser");
                phase = 2;
                since = now;
            }
            break;
        case 2: /* refused: warned of, the lock kept */
            if (now - since > 0.1 &&
                js("Module.wgfOrientation.refused === '' && Module.deviceLog.locks.length === 2")) {
                expect(wgf_window_get_orientation_lock() == WGF_WINDOW_ORIENTATION_LANDSCAPE,
                       "a refusal keeps the lock held");
                js("Module.fullscreen = false");
                phase = 3;
                since = now;
            }
            break;
        case 3: /* out of fullscreen: none */
            if (wgf_window_get_orientation_lock() == WGF_WINDOW_ORIENTATION_ANY) {
                js("Module.fullscreen = true");
                expect(wgf_window_request_orientation(WGF_WINDOW_ORIENTATION_ANY) &&
                           js("Module.deviceLog.unlocks === 1"),
                       "let go: unlocked");
                phase = 4;
                since = now;
            }
            break;
        case 4: /* let go, though back in fullscreen; and pad 0's time up */
            if (now - since > 0.1 && !wgf_gamepad_is_rumbling(0)) {
                expect(wgf_window_get_orientation_lock() == WGF_WINDOW_ORIENTATION_ANY, "none held once let go");
                expect(js("const e = Module.deviceLog.effects; e.length === 2 && e[1][0] === 'reset'"),
                       "pad 0 stopped once its time was up: the actuator reset");
                expect(!wgf_gamepad_stop_rumble(0), "nothing left to stop");

                js("Module.canvas.style.top = '10px'");
                expect(wgf_window_get_safe_inset_top() == 14, "less what lies above the canvas");
                js("Module.insets['padding-top'] = '5000px'");
                expect(wgf_window_get_safe_inset_top() == wgf_window_get_height(), "held to the window's height");
                done();
            }
            break;
        default: break;
    }
}

int main(void)
{
    stub_device();
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
