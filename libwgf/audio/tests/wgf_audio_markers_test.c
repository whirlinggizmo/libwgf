#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_audio_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_time.h"
#include "wgf_voice.h"

/* MP3's encoder delay, natively (libwgf/audio/tests/data/make_markers.py): markers at 0.5, 1.0,
 * and 1.5 s. The file whose Info frame says its encoder's delay plays them on time, so a
 * segment starting on a marker starts with it; the file without plays them late by the
 * encoder's and decoder's delay (1105 frames, 25 ms), which no decoder can know. The
 * browser's decoders: wgf_audio_markers_web_test. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The first sample past 0.3 from `from` seconds, in seconds; -1 for none. */
static double onset(const wgf_audio_priv_sound_t *sound_ptr, double from)
{
    for (uint64_t i = (uint64_t)(from * sound_ptr->sample_rate); i < sound_ptr->frames; i++) {
        if (fabsf(sound_ptr->pcm[i * (uint64_t)sound_ptr->channels]) > 0.3f) return (double)i / sound_ptr->sample_rate;
    }
    return -1.0;
}

static void check(wgf_handle_t sound, double late, const char *name)
{
    const wgf_audio_priv_sound_t *sound_ptr = wgf_audio_priv_sound_of(sound);
    expect(sound_ptr != NULL && sound_ptr->resource.status == WGF_RESOURCE_STATUS_READY, name);
    if (sound_ptr == NULL || sound_ptr->pcm == NULL) return;
    for (int k = 1; k <= 3; k++) {
        const double at = onset(sound_ptr, 0.5 * k - 0.2), want = 0.5 * k + late;
        char what[160];
        printf("%s: marker %d at %.4f s (%+.1f ms)\n", name, k, at, (at - 0.5 * k) * 1000.0);
        snprintf(what, sizeof(what), "%s: marker %d %.1f ms from %.1f ms late", name, k, (at - want) * 1000.0, late * 1000.0);
        expect(fabs(at - want) < 0.002, what);
    }
}

int main(void)
{
    wgf_handle_t plain, gapless, voice;
    static float out[2048 * 2];
    double start;
    int first = -1;
    wgf_core_priv_init();
    wgf_core_priv_fs_set_root(WGF_TEST_DATA);
    plain = wgf_sound_create("markers.mp3");
    gapless = wgf_sound_create("markers_gapless.mp3");
    start = wgf_time_get_seconds();
    while (wgf_core_priv_load_get_pending_count() > 0 && wgf_time_get_seconds() - start < 30.0) wgf_core_priv_update();
    check(gapless, 0.0, "with its delay written");
    check(plain, 1105.0 / 44100.0, "without");

    /* a segment starting on a marker starts with it */
    expect(wgf_sound_add_segment(gapless, "second", 1.0f, 1.2f), "a segment on the second marker");
    voice = wgf_voice_create(gapless);
    wgf_voice_set_segment(voice, "second");
    wgf_voice_play(voice);
    wgf_audio_priv_mix(out, 2048, 44100);
    for (int i = 0; i < 2048 && first < 0; i++) {
        if (fabsf(out[i * 2]) > 0.3f) first = i;
    }
    printf("the segment's first loud frame: %d (%.2f ms)\n", first, first * 1000.0 / 44100.0);
    expect(first >= 0 && first < 44, "the segment starts with its marker, within a millisecond");
    wgf_voice_destroy(voice);
    wgf_core_priv_part_update(0.0f);
    wgf_resource_release(plain);
    wgf_resource_release(gapless);
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
