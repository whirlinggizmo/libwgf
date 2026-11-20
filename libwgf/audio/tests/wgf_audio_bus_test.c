#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wgf_audio_priv.h"
#include "wgf_bus.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"
#include "wgf_sound.h"
#include "wgf_time.h"
#include "wgf_voice.h"

/* Buses natively, headless, the mixer pulled by the test: the three that always exist and
 * their tree, a game's own, volumes and mutes heard through the tree (in the samples the
 * mixer makes), a fade even in decibels over the part's updates, a bus destroyed moving
 * its voices and buses to its parent, a voice's bus and a sound's plays' bus, and what is
 * refused. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-3f;
}

/* A second of a constant sample, 16-bit mono at 44.1 kHz. */
static int make_wav(unsigned char *wav)
{
    const int frames = 44100, data = frames * 2;
    memcpy(wav, "RIFF", 4);
    *(unsigned *)(wav + 4) = (unsigned)(36 + data);
    memcpy(wav + 8, "WAVEfmt ", 8);
    *(unsigned *)(wav + 16) = 16;
    *(unsigned short *)(wav + 20) = 1;
    *(unsigned short *)(wav + 22) = 1;
    *(unsigned *)(wav + 24) = 44100;
    *(unsigned *)(wav + 28) = 44100 * 2;
    *(unsigned short *)(wav + 32) = 2;
    *(unsigned short *)(wav + 34) = 16;
    memcpy(wav + 36, "data", 4);
    *(unsigned *)(wav + 40) = (unsigned)data;
    for (int i = 0; i < frames; i++) *(short *)(wav + 44 + i * 2) = 16384; /* 0.5 */
    return 44 + data;
}

/* What the mixer makes now, on the left, from what is playing. */
static float level(void)
{
    static float out[64 * 2];
    wgf_audio_priv_mix(out, 64, 44100);
    return out[0];
}

int main(void)
{
    static unsigned char wav[44 + 44100 * 2];
    const int size = make_wav(wav);
    wgf_bus_t master, music, effects, mine, under;
    wgf_handle_t sound, voice;
    double start;

    wgf_core_priv_init();
    wgf_core_priv_fs_set_root("audio_bus_test_root");
    master = wgf_bus_get_master();
    music = wgf_bus_get_music();
    effects = wgf_bus_get_effects();
    expect(master != 0 && music != 0 && effects != 0 && wgf_bus_get_parent(master) == 0 &&
               wgf_bus_get_parent(music) == master && wgf_bus_get_parent(effects) == master,
           "master, music, and effects, before audio starts, music and effects under the master");
    expect(near(wgf_bus_get_volume(music), 1.0f) && !wgf_bus_is_muted(music) && near(wgf_bus_get_heard_volume(music), 1.0f),
           "at volume 1, unmuted");

    expect(wgf_core_priv_fs_write("sounds/half.wav", wav, size), "the WAV written");
    sound = wgf_sound_create("sounds/half.wav");
    start = wgf_time_get_seconds();
    while (wgf_core_priv_load_get_pending_count() > 0 && wgf_time_get_seconds() - start < 30.0) wgf_core_priv_update();
    wgf_core_priv_part_update(0.0f);

    voice = wgf_voice_create(sound);
    expect(wgf_voice_get_bus(voice) == effects, "a voice on effects until set");
    wgf_voice_set_loop(voice, true);
    wgf_voice_play(voice);
    expect(near(level(), 0.5f), "heard at its own volume through buses at 1");
    expect(wgf_bus_set_volume(effects, 0.5f) && near(level(), 0.25f), "its bus at a half: a half as loud");
    expect(wgf_bus_set_volume(master, 0.5f) && near(wgf_bus_get_heard_volume(effects), 0.25f) && near(level(), 0.125f),
           "and the master at a half: its parents' too");
    expect(wgf_bus_set_muted(master, true) && wgf_bus_is_muted(master) && level() == 0.0f &&
               wgf_bus_get_heard_volume(effects) == 0.0f && near(wgf_bus_get_volume(master), 0.5f),
           "the master muted: silent, its volume kept");
    wgf_bus_set_muted(master, false);
    wgf_bus_set_volume(master, 1.0f);
    expect(wgf_voice_set_bus(voice, music) && wgf_voice_get_bus(voice) == music && near(level(), 0.5f),
           "moved to music: effects' volume no longer counts");

    mine = wgf_bus_create(music);
    under = wgf_bus_create(mine);
    expect(mine != 0 && under != 0 && wgf_bus_get_parent(under) == mine, "a game's own buses, one under another");
    wgf_bus_set_volume(mine, 0.5f);
    wgf_voice_set_bus(voice, under);
    expect(near(level(), 0.25f), "heard through its own bus");

    expect(wgf_bus_fade(mine, 0.0f, 1.0f) && wgf_bus_is_fading(mine), "a fade to silence over a second");
    wgf_core_priv_part_update(0.5f);
    expect(wgf_bus_get_volume(mine) < 0.05f && wgf_bus_get_volume(mine) > 0.0f,
           "halfway in decibels: from -6 dB to -60, -33 dB, about 0.02");
    wgf_core_priv_part_update(0.6f);
    expect(wgf_bus_get_volume(mine) == 0.0f && !wgf_bus_is_fading(mine) && level() == 0.0f, "ended: silent");
    expect(wgf_bus_fade(mine, 1.0f, 2.0f) && wgf_bus_set_volume(mine, 0.5f) && !wgf_bus_is_fading(mine),
           "a volume set ends a fade");
    expect(wgf_bus_fade(mine, 0.8f, 0.0f) && near(wgf_bus_get_volume(mine), 0.8f) && !wgf_bus_is_fading(mine),
           "a fade of no time: at once");

    wgf_bus_destroy(mine);
    expect(wgf_bus_get_volume(mine) == 0.0f && wgf_bus_get_parent(under) == music, "destroyed: its buses moved up");
    wgf_bus_destroy(under);
    expect(wgf_voice_get_bus(voice) == music && near(level(), 0.5f), "and its voices");
    wgf_bus_destroy(master);
    wgf_bus_destroy(effects);
    expect(wgf_bus_get_parent(effects) == master, "the three that always exist aren't destroyed");

    {
        const wgf_bus_t quiet = wgf_bus_create(master);
        wgf_voice_stop(voice);
        wgf_bus_set_volume(quiet, 0.5f);
        expect(wgf_sound_get_bus(sound) == effects && wgf_sound_set_bus(sound, quiet) && wgf_sound_get_bus(sound) == quiet,
               "a sound's plays on effects until set");
        expect(wgf_sound_play(sound, 1.0f, 1.0f, 0.0f) && near(level(), 0.25f), "a play heard through its sound's bus");
        wgf_bus_destroy(quiet);
        expect(wgf_sound_get_bus(sound) == effects, "its bus gone: effects again");
    }

    expect(!wgf_bus_set_volume(master, -1.0f) && !wgf_bus_set_volume(master, NAN) &&
               !wgf_bus_fade(master, -1.0f, 1.0f) && !wgf_bus_fade(master, 1.0f, NAN),
           "a negative or non-finite volume or time refused");
    expect(wgf_bus_create(voice) == 0 && !wgf_bus_set_volume(voice, 1.0f) && wgf_bus_get_volume(voice) == 0.0f &&
               !wgf_voice_set_bus(voice, voice) && !wgf_sound_set_bus(sound, voice),
           "a handle that isn't a bus refused");
    {
        int made = 0;
        while (wgf_bus_create(master) != 0 && made < 100) made++;
        expect(made == 61, "64 buses at most, the three among them");
    }

    wgf_voice_destroy(voice);
    wgf_resource_release(sound);
    wgf_core_priv_fs_rmdir("sounds");
    wgf_core_priv_shutdown();
    return failures == 0 ? 0 : 1;
}
