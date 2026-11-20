#include <stdbool.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_color.h"
#include "wgf_debug.h"
#include "wgf_draw.h"
#include "wgf_keyboard.h"
#include "wgf_render.h"
#include "wgf_trig.h"
#include "wgf_voice.h"
#include "wgf_window.h"

/* A small synth: tones made here, in C, a sample at a time, and pushed into a pushed
 * voice (wgf_voice_create_pushed, raylib's AudioStream), which plays them. Each frame it
 * tops the voice's queue up to a tenth of a second (wgf_voice_get_queued; raylib's
 * IsAudioStreamProcessed loop), so what is held down is heard within about that. The
 * samples are made at 22050 Hz, mono, and played at the mixer's rate, resampled. The
 * scope draws the last samples made, and the readout the queue and its underruns (each a
 * gap: the queue ran out, as a long frame on the web can make it).
 *
 *   A S D F G H J K   play C D E F G A B C, while held
 *   Tab               the next waveform: sine, square, saw, triangle
 *   Space             an arpeggio, on or off
 *   Esc               quit, where quitting means anything */

#define RATE 22050
#define NOTES 8
#define SCOPE 512

static const wgf_keyboard_key_t keys[NOTES] = {WGF_KEY_A, WGF_KEY_S, WGF_KEY_D, WGF_KEY_F,
                                                WGF_KEY_G, WGF_KEY_H, WGF_KEY_J, WGF_KEY_K};
static const char *const names[NOTES] = {"C", "D", "E", "F", "G", "A", "B", "C"};
static const float pitches[NOTES] = {261.63f, 293.66f, 329.63f, 349.23f, 392.00f, 440.00f, 493.88f, 523.25f};
static const char *const waves[4] = {"sine", "square", "saw", "triangle"};

static wgf_voice_t synth;
static int wave;
static bool arpeggio;
static long long made; /* samples made: the arpeggio's clock */
static float phase[NOTES], level[NOTES];
static bool held[NOTES];
static float scope[SCOPE];
static int scope_at;

/* One cycle of the waveform at `phase` (0 to 1). */
static float shape(float at)
{
    switch (wave) {
    case 0: return wgf_trig_sin(at * 6.2831853f);
    case 1: return at < 0.5f ? 1.0f : -1.0f;
    case 2: return at * 2.0f - 1.0f;
    default: return at < 0.5f ? at * 4.0f - 1.0f : 3.0f - at * 4.0f;
    }
}

/* The next sample: every sounding note, each with an envelope (a fast attack while held,
 * a slower release after). */
static float next_sample(void)
{
    float sum = 0.0f;
    if (arpeggio) { /* up the scale and back, an eighth of a second a note */
        const int step = (int)(made / (RATE / 8)) % (NOTES * 2 - 2);
        const int note = step < NOTES ? step : NOTES * 2 - 2 - step;
        for (int n = 0; n < NOTES; n++) held[n] = held[n] || n == note;
    }
    for (int n = 0; n < NOTES; n++) {
        level[n] = held[n] ? level[n] + (1.0f - level[n]) * 0.01f : level[n] * 0.9995f;
        if (level[n] < 1e-4f) {
            level[n] = 0.0f;
            continue;
        }
        sum += shape(phase[n]) * level[n] * 0.2f;
        phase[n] += pitches[n] / (float)RATE;
        if (phase[n] >= 1.0f) phase[n] -= 1.0f;
    }
    made++;
    return sum;
}

static void init(void *user)
{
    (void)user;
    wgf_render_set_clear_color(wgf_color_make(18, 20, 28, 255));
    synth = wgf_voice_create_pushed(RATE, 1, RATE / 2); /* room for half a second */
    wgf_voice_set_volume(synth, 0.8f);
    wgf_voice_play(synth); /* silent until something is pushed */
}

static void frame(void *user)
{
    static float samples[RATE / 2];
    int want;
    char line[128];
    (void)user;
    if (wgf_keyboard_is_pressed(WGF_KEY_TAB)) wave = (wave + 1) % 4;
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) arpeggio = !arpeggio;
    if (wgf_app_can_quit() && wgf_keyboard_is_pressed(WGF_KEY_ESCAPE)) wgf_app_quit();

    /* the queue topped up to a tenth of a second, made now with what is held now */
    want = RATE / 10 - wgf_voice_get_queued(synth);
    if (want > wgf_voice_get_room(synth)) want = wgf_voice_get_room(synth);
    for (int i = 0; i < want; i++) {
        if (i % 64 == 0) { /* the keys, between short runs, so a key's start isn't late */
            for (int n = 0; n < NOTES; n++) held[n] = wgf_keyboard_is_down(keys[n]);
        }
        samples[i] = next_sample();
        scope[scope_at] = samples[i];
        scope_at = (scope_at + 1) % SCOPE;
    }
    if (want > 0) wgf_voice_push(synth, samples, want);

    wgf_draw_text(0, "libwgf synth: a pushed voice", 24, 30, 28, WGF_COLOR_RAYWHITE);
    for (int n = 0; n < NOTES; n++) { /* the keys, lit as they sound */
        const float x = 24.0f + (float)n * 64.0f;
        const unsigned char lit = (unsigned char)(60.0f + level[n] * 195.0f);
        wgf_draw_rectangle(x, 80.0f, 56.0f, 120.0f, wgf_color_make(lit, lit, (unsigned char)(lit / 2 + 120), 255));
        wgf_draw_text(0, names[n], x + 22.0f, 170.0f, 20, WGF_COLOR_BLACK);
    }
    wgf_draw_rectangle_lines(24.0f, 220.0f, 504.0f, 120.0f, 1.0f, WGF_COLOR_DARKGRAY);
    for (int i = 1; i < SCOPE; i++) { /* the last samples made, oldest first */
        const float a = scope[(scope_at + i - 1) % SCOPE], b = scope[(scope_at + i) % SCOPE];
        const float x = 24.0f + (float)i * (504.0f / (float)SCOPE);
        wgf_draw_line(x - 504.0f / (float)SCOPE, 280.0f - a * 55.0f, x, 280.0f - b * 55.0f, 1.5f, WGF_COLOR_LIME);
    }
    snprintf(line, sizeof(line), "%s   queued %4d frames (%3d ms)   underruns %d   arpeggio %s", waves[wave],
             wgf_voice_get_queued(synth), wgf_voice_get_queued(synth) * 1000 / RATE, wgf_voice_get_underruns(synth),
             arpeggio ? "on" : "off");
    wgf_draw_text(0, line, 24, 356, 18, WGF_COLOR_SKYBLUE);
    wgf_draw_text(0,
                  wgf_app_can_quit() ? "[A..K] play   [TAB] waveform   [SPACE] arpeggio   [ESC] quit"
                                     : "[A..K] play   [TAB] waveform   [SPACE] arpeggio",
                  24, 390, 16, WGF_COLOR_LIGHTGRAY);
}

int main(void)
{
    wgf_window_set_title("libwgf synth");
    wgf_window_set_size(560, 430);
    wgf_debug_show_fps(0, 400, 12, 16.0f, wgf_color_make(0, 255, 0, 255));
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
