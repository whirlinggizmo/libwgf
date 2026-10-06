#include <math.h>
#include <stdio.h>

#include <emscripten.h>

#include "wgf_app.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_window.h"

/* MP3's encoder delay in a browser (tools/run_in_browser.py): the markers of
 * audio/tests/data/make_markers.py, at 0.5, 1.0, and 1.5 s, found in the AudioBuffer the
 * browser decoded each file into. Whether the browser reads the delay the Info frame
 * writes decides whether an MP3's segments land on time there. Chromium reads it, as the
 * native decoder does (wgf_audio_markers_test): on time within 2 ms with it, the
 * encoder's and decoder's 1105 frames late without. */

static int failures, frames;
static wgf_handle_t sounds[2]; /* [delay written, without] */

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The first sample past 0.3 from `from` seconds in the buffer the browser decoded for
 * `sound`, in seconds; -1 for none. */
EM_JS(double, onset, (int sound, double from), {
    var entry = Module.wgf_audio && Module.wgf_audio.sounds.get(sound);
    if (!entry || !entry.buffer) return -1;
    var data = entry.buffer.getChannelData(0), rate = entry.buffer.sampleRate;
    for (var i = Math.floor(from * rate); i < data.length; i++) {
        if (Math.abs(data[i]) > 0.3) return i / rate;
    }
    return -1;
})

static void check(int which, double late, double within, const char *name)
{
    for (int k = 1; k <= 3; k++) {
        const double at = onset((int)sounds[which], 0.5 * k - 0.2), want = 0.5 * k + late;
        char what[160];
        printf("%s: marker %d at %.4f s (%+.1f ms)\n", name, k, at, (at - 0.5 * k) * 1000.0);
        snprintf(what, sizeof(what), "%s: marker %d %.1f ms from %.1f ms late", name, k, (at - want) * 1000.0, late * 1000.0);
        expect(fabs(at - want) < within, what);
    }
}

static void on_frame(void *user)
{
    (void)user;
    if (++frames > 60 * 30) {
        printf("FAIL: the sounds never loaded\n");
        emscripten_force_exit(1);
    } else if (frames == 1) {
        sounds[0] = wgf_sound_create("markers_gapless.mp3");
        sounds[1] = wgf_sound_create("markers.mp3");
    } else if (wgf_resource_get_status(sounds[0]) != WGF_RESOURCE_STATUS_PENDING &&
               wgf_resource_get_status(sounds[1]) != WGF_RESOURCE_STATUS_PENDING) {
        expect(wgf_resource_get_status(sounds[0]) == WGF_RESOURCE_STATUS_READY &&
                   wgf_resource_get_status(sounds[1]) == WGF_RESOURCE_STATUS_READY,
               "both decoded");
        check(0, 0.0, 0.002, "with its delay written");
        check(1, 1105.0 / 44100.0, 0.002, "without");
        emscripten_force_exit(failures == 0 ? 0 : 1);
    }
}

int main(void)
{
    wgf_window_set_size(64, 64);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
