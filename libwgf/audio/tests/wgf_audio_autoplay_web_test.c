#include <emscripten.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_audio.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_time.h"
#include "wgf_voice.h"
#include "wgf_window.h"

/* A streamed voice before the page's first tap, in a browser (tools/run_in_browser.py): the
 * media element's play() stood in for by one refusing as the autoplay rule does
 * (NotAllowedError) outside a tap's event. A voice played at the title, before any tap,
 * isn't an unhandled rejection: it is played again inside the next tap's own event; one
 * stopped before that tap stays stopped. And whether sound is held back
 * (wgf_audio_is_blocked): the browser that runs the tests lets sound start untapped, so the
 * audio context's state and the page's user activation are stood in for too, suspended and
 * none until the first tap, which is held back until then, and not after. */

static int failures;
static int phase;
static double since;
static wgf_sound_t music;
static wgf_voice_t voice;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

EM_JS_DEPS(wgf_audio_autoplay_web_test, "$UTF8ToString")

/* Whether a line of JS is true, against the stand-in's log. */
EM_JS(int, js, (const char *code), { return eval(UTF8ToString(code)) ? 1 : 0; })

EM_JS(void, stub_autoplay, (void), {
    const log = Module.autoplayLog = {refused : 0, played : 0, unhandled : 0};
    const play = HTMLMediaElement.prototype.play;
    let tapped = false;
    HTMLMediaElement.prototype.play = function() {
        if (!tapped) {
            log.refused++;
            return Promise.reject(new DOMException("play() failed because the user didn't interact",
                                                   "NotAllowedError"));
        }
        log.played++;
        return play.call(this);
    };
    window.addEventListener("unhandledrejection", () => { log.unhandled++; });
    /* as a browser that holds sound back has it: no activation, the context suspended, until
       the first tap */
    Module.activated = false;
    Object.defineProperty(Navigator.prototype, "userActivation", {
        configurable: true,
        get: () => ({hasBeenActive: Module.activated, isActive: false})
    });
    Object.defineProperty(AudioContext.prototype, "state", {
        configurable: true,
        get: () => Module.activated ? "running" : "suspended"
    });
    /* a tap: the page's listeners run inside its event, the only time play() is let through */
    Module.tap = () => {
        tapped = true;
        Module.activated = true;
        window.dispatchEvent(new Event("pointerdown"));
        window.dispatchEvent(new Event("pointerup"));
        tapped = false;
    };
})

static void done(void)
{
    if (voice != 0) wgf_voice_destroy(voice);
    if (failures == 0) printf("wgf_audio_autoplay_web_test: passed\n");
    emscripten_force_exit(failures == 0 ? 0 : 1);
}

static void on_frame(void *user)
{
    (void)user;
    const double now = wgf_time_get_seconds();
    if (phase > 0 && now - since > 10.0) {
        printf("FAIL: phase %d timed out\n", phase);
        failures++;
        done();
        return;
    }
    switch (phase) {
        case 0:
            expect(wgf_audio_is_blocked(), "before any input: sound held back");
            music = wgf_sound_create_streamed("music/a_hero_is_born.mp3");
            since = now;
            phase = 1;
            break;
        case 1: /* ready: played before any tap */
            if (wgf_resource_get_status(music) == WGF_RESOURCE_STATUS_READY) {
                voice = wgf_voice_create(music);
                expect(voice != 0 && wgf_voice_play(voice), "a voice of the music, played before a tap");
                since = now;
                phase = 2;
            }
            break;
        case 2: /* refused, answered, and played inside the next tap's own event */
            if (now - since > 0.3) {
                expect(js("Module.autoplayLog.refused === 1 && Module.autoplayLog.unhandled === 0"),
                       "refused by the autoplay rule, and answered: no unhandled rejection");
                expect(js("Module.autoplayLog.played === 0"), "not played before a tap");
                expect(wgf_audio_is_blocked(), "audio running, the context suspended: still held back");
                js("Module.tap()");
                expect(js("Module.autoplayLog.played === 1"), "played inside the tap");
                expect(!wgf_audio_is_blocked(), "after a tap: not held back");
                wgf_voice_stop(voice);
                since = now;
                phase = 3;
            }
            break;
        case 3: /* played again outside a tap, refused, then stopped: the next tap leaves it */
            if (now - since > 0.1) {
                expect(wgf_voice_play(voice), "played again, outside a tap");
                since = now;
                phase = 4;
            }
            break;
        case 4:
            if (now - since > 0.3) {
                expect(js("Module.autoplayLog.refused === 2"), "refused again");
                wgf_voice_stop(voice);
                js("Module.tap()");
                expect(js("Module.autoplayLog.played === 1 && Module.autoplayLog.unhandled === 0"),
                       "stopped before the tap: not played by it");
                done();
            }
            break;
        default: break;
    }
}

int main(void)
{
    stub_autoplay();
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
