#include <emscripten.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_time.h"
#include "wgf_window.h"

/* Fullscreen held for a tap, in a browser (tools/run_in_browser.py), the browser's calls
 * stood in for by ones that answer as a phone's browser does: the canvas's requestFullscreen
 * refused outside a tap's event and granted inside one (document.fullscreenElement and
 * fullscreenchange as the browser sets them), screen.orientation.lock refused outside
 * fullscreen and granted in it. Fullscreen asked before the window opens, and an
 * orientation asked at start, are held: no warning, nothing granted, until a tap; the tap
 * (a pointer up on the window) is answered inside its own event, and the lock follows as
 * fullscreen begins. Leaving needs no tap, and a request let go of is no longer held. */

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

EM_JS_DEPS(wgf_platform_fullscreen_web_test, "$UTF8ToString")

/* Whether a line of JS is true, against the stubs' log. */
EM_JS(int, js, (const char *code), { return eval(UTF8ToString(code)) ? 1 : 0; })

EM_JS(void, stub_browser, (void), {
    const log = Module.fullLog = {asked : 0, granted : 0, exits : 0, locks : [], refusedLocks : 0};
    let element = null;
    let inTap = false;
    const changed = () => document.dispatchEvent(new Event("fullscreenchange"));
    Object.defineProperty(document, "fullscreenEnabled", {get : () => true, configurable : true});
    Object.defineProperty(document, "fullscreenElement", {get : () => element, configurable : true});
    Module.canvas.requestFullscreen = function() {
        log.asked++;
        if (!inTap) return Promise.reject(new TypeError("Permissions check failed"));
        log.granted++;
        element = this;
        setTimeout(changed, 0);
        return Promise.resolve();
    };
    document.exitFullscreen = () => {
        log.exits++;
        element = null;
        setTimeout(changed, 0);
        return Promise.resolve();
    };
    /* a tap: its event dispatched as the browser does, the page's listeners run inside it */
    Module.tap = () => {
        inTap = true;
        window.dispatchEvent(new Event("pointerup"));
        inTap = false;
    };
    Object.defineProperty(navigator, "maxTouchPoints", {get : () => 5, configurable : true});
    Object.defineProperty(window.screen, "orientation", {
        value : {
            type : "portrait-primary",
            angle : 0,
            lock : (orientation) => {
                if (!element) {
                    log.refusedLocks++;
                    return Promise.reject(new DOMException("not in fullscreen", "NotSupportedError"));
                }
                log.locks.push(orientation);
                return Promise.resolve();
            },
            unlock : () => {}
        },
        configurable : true
    });
})

static void done(void)
{
    if (failures == 0) printf("wgf_platform_fullscreen_web_test: passed\n");
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void on_frame(void *user)
{
    (void)user;
    const double now = wgf_time_get_seconds();
    if (phase > 0 && now - since > 5.0) {
        printf("FAIL: phase %d timed out (fullscreen %d, lock %d)\n", phase, wgf_window_is_fullscreen(),
               (int)wgf_window_get_orientation_lock());
        js("console.log(JSON.stringify(Module.fullLog), !!document.fullscreenElement)");
        failures++;
        done();
        return;
    }
    switch (phase) {
        case 0: /* asked before the window opened: refused (no tap), held */
            expect(js("Module.fullLog.asked >= 1 && Module.fullLog.granted === 0"),
                   "asked as the window opened, refused outside a tap");
            expect(!wgf_window_is_fullscreen(), "not fullscreen before a tap");
            expect(wgf_window_request_orientation(WGF_WINDOW_ORIENTATION_LANDSCAPE), "an orientation asked for");
            since = now;
            phase = 1;
            break;
        case 1: /* a few frames on: still held, the lock's refusal outside fullscreen kept quiet */
            if (now - since > 0.2) {
                expect(!wgf_window_is_fullscreen(), "still not fullscreen with no tap");
                expect(wgf_window_get_orientation_lock() == WGF_WINDOW_ORIENTATION_ANY, "no lock outside fullscreen");
                expect(js("Module.fullLog.refusedLocks === 1"), "the lock asked once, refused");
                js("Module.tap()");
                expect(js("Module.fullLog.granted === 1"), "granted inside the tap's own event");
                since = now;
                phase = 2;
            }
            break;
        case 2: /* fullscreen read at a frame's start, and the lock asked again as it began */
            if (wgf_window_is_fullscreen() && wgf_window_get_orientation_lock() == WGF_WINDOW_ORIENTATION_LANDSCAPE) {
                expect(js("Module.fullLog.locks.length === 1 && Module.fullLog.locks[0] === 'landscape'"),
                       "the held lock asked as fullscreen began");
                expect(wgf_window_request_fullscreen(false), "leaving asked for");
                expect(js("Module.fullLog.exits === 1"), "left at once, needing no tap");
                since = now;
                phase = 3;
            }
            break;
        case 3: /* out, and a tap now asks for nothing: let go of, it isn't held */
            if (!wgf_window_is_fullscreen()) {
                expect(wgf_window_get_orientation_lock() == WGF_WINDOW_ORIENTATION_ANY, "the lock gone with fullscreen");
                js("Module.before = Module.fullLog.asked; Module.tap()");
                expect(js("Module.fullLog.asked === Module.before"), "a tap asks for nothing let go of");
                expect(wgf_window_request_fullscreen(true), "asked again, at once");
                js("Module.tap()");
                expect(js("Module.fullLog.granted === 2"), "granted by the next tap");
                since = now;
                phase = 4;
            }
            break;
        case 4:
            if (wgf_window_is_fullscreen()) done();
            break;
        default: break;
    }
}

int main(void)
{
    stub_browser();
    wgf_window_request_fullscreen(true); /* before the window opens */
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
