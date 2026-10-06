#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* setenv */
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_app_script_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_gamepad.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_loop.h"
#include "wgf_mouse.h"
#include "wgf_probe.h"
#include "wgf_random.h"

/* Scripted runs, headless, through wgf_app_run as a program runs: a script's inputs
 * reach the program's ticks at their frames, its expectations pass and fail as the
 * probes say, its time is a display frame a frame, its seed is the random numbers',
 * its end quits; a dump asks each part for its state; and a script that can't be read
 * doesn't run. */

static int failures;

/* A part with state to dump, as the ecs is: how often it was asked. */
static int dumps;
static const char *dump_part(void)
{
    dumps++;
    return "first line\nsecond line";
}
static wgf_core_priv_part_t dumping = {.name = "test", .layer = WGF_CORE_PRIV_PART_LAYER_ASSET,
                                       .order = WGF_CORE_PRIV_PART_ASSET, .dump = dump_part};

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

typedef struct run_t {
    int ticks;
    int frames;
    int seed;
    int deltas_exact; /* every frame's delta exactly a sixtieth */
    int presses;
    float first_random;
} run_t;

static void on_init(void *user)
{
    run_t *run = user;
    run->seed = wgf_random_get_seed();
    run->first_random = wgf_random_get_float();
    run->deltas_exact = 1;
}

static void on_tick(void *user)
{
    run_t *run = user;
    run->ticks++;
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) run->presses++;
    wgf_probe_set_value("ticks", run->ticks);
    wgf_probe_set_value("presses", run->presses);
    wgf_probe_set_value("space_down", wgf_keyboard_is_down(WGF_KEY_SPACE));
    wgf_probe_set_value("mouse_x", wgf_mouse_get_position().x);
    wgf_probe_set_value("mouse_left", wgf_mouse_is_down(WGF_MOUSE_BUTTON_LEFT));
    wgf_probe_set_value("pad_connected", wgf_gamepad_is_connected(1));
    wgf_probe_set_value("pad_south", wgf_gamepad_is_down(1, WGF_GAMEPAD_BUTTON_SOUTH));
    wgf_probe_set_value("pad_x", wgf_gamepad_get_stick(1, WGF_GAMEPAD_STICK_LEFT).x);
}

static void on_frame(void *user)
{
    run_t *run = user;
    run->frames++;
    if (wgf_loop_get_frame_delta() != (float)(1.0 / 60.0)) run->deltas_exact = 0;
    if (wgf_input_get_chars()[0] != '\0') wgf_probe_set_value("typed", (double)(unsigned char)wgf_input_get_chars()[0]);
}

static const char *script_path = "wgf_app_script_test.txt";

static void write_script(const char *text)
{
    FILE *f = fopen(script_path, "wb");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void run_script(const char *text, run_t *run)
{
    write_script(text);
    memset(run, 0, sizeof(*run));
    wgf_app_run(on_init, on_tick, on_frame, NULL, run);
}

static const char *passing =
    "wgf-script 1\n"
    "# every kind of input, and checks after each\n"
    "seed 1234\n"
    "at 2 key tap space\n"
    "at 2 expect presses == 1\n"
    "at 2 expect space_down == 1     # down during frame 2's tick\n"
    "at 3 expect space_down == 0     # up at frame 3\n"
    "at 4 key down space\n"
    "at 6 key up space\n"
    "at 7 expect presses == 2\n"
    "at 8 mouse move 120 45\n"
    "at 8 mouse down left\n"
    "at 8 expect mouse_x == 120\n"
    "at 8 expect mouse_left == 1\n"
    "at 9 mouse up left\n"
    "at 9 expect mouse_left == 0\n"
    "at 10 text A\n"
    "at 10 expect typed == 65\n"
    "at 11 pad 1 connect\n"
    "at 12 pad 1 down south\n"
    "at 12 pad 1 axis left_x 1\n"
    "at 12 expect pad_connected == 1\n"
    "at 12 expect pad_south == 1\n"
    "at 12 expect pad_x > 0.9\n"
    "at 13 pad 1 up south\n"
    "at 13 expect pad_south == 0\n"
    "at 29 expect ticks == 30        # one tick a frame: 60 a second, a sixtieth a frame\n"
    "at 29 log thirty frames\n"
    "at 29 screenshot thirty\n"
    "at 29 end\n";

int main(void)
{
    run_t run;
    float seeded_first;

#if defined(_WIN32)
    _putenv_s("LIBWGF_SCRIPT", script_path);
#else
    setenv("LIBWGF_SCRIPT", script_path, 1);
#endif

    run_script(passing, &run);
    expect(wgf_app_priv_script_has_passed() && wgf_app_priv_script_get_failures() == 0, "the script passes");
    expect(run.frames == 30, "it ended after its end's frame");
    expect(run.ticks == 30, "a tick a frame");
    expect(run.deltas_exact, "every frame lasted a sixtieth of a second");
    expect(run.seed == 1234, "the script's seed, set before init");
    seeded_first = run.first_random;
    expect(run.presses == 2, "a tap and a hold, each pressed once");

    run_script(passing, &run);
    expect(run.first_random == seeded_first, "the same seed, the same numbers");

    run_script("wgf-script 1\n"
               "at 3 expect presses == 1\n" /* nothing was pressed */
               "at 3 expect never_set > 0\n"
               "at 4 expect ticks == 5\n"
               "at 5 end\n",
               &run);
    expect(!wgf_app_priv_script_has_passed() && wgf_app_priv_script_get_failures() == 2,
           "a wrong value and a probe never set fail, the right one doesn't");
    expect(run.frames == 6, "a failing script still runs to its end");

    wgf_core_priv_part_install(&dumping);
    run_script("wgf-script 1\nat 2 dump\nat 3 dump\nat 4 end\n", &run);
    expect(wgf_app_priv_script_has_passed() && dumps == 2, "each dump asks the part for its state");

    /* the numbers a script reads: decimals with a point and an exponent, either sign */
    run_script("wgf-script 1\n"
               "at 3 expect ticks == 4e0\n"
               "at 3 expect ticks == +4.000\n"
               "at 3 expect ticks > .35e1\n"
               "at 3 expect ticks < 450E-2\n"
               "at 3 expect ticks > -1.5\n"
               "at 4 end\n",
               &run);
    expect(wgf_app_priv_script_has_passed() && wgf_app_priv_script_get_failures() == 0,
           "numbers with points, exponents, and signs read as they say");

    run_script("wgf-script 1\nat 2 key tap nosuchkey\nat 4 end\n", &run);
    expect(run.frames == 0, "a script that can't be read ends the program before its first frame");

    /* what the parser refuses, each alone */
    {
        static const char *const bad[] = {
            "",                                       /* no header */
            "wgf-script 2\n",                         /* another version */
            "wgf-script 1\nat -1 end\n",              /* a frame before 0 */
            "wgf-script 1\nat x end\n",               /* not a number */
            "wgf-script 1\nat 1\n",                   /* no command */
            "wgf-script 1\nat 1 jump\n",              /* no such command */
            "wgf-script 1\nat 1 key press a\n",       /* no such verb */
            "wgf-script 1\nat 1 mouse click thumb\n", /* no such button */
            "wgf-script 1\nat 1 pad 4 connect\n",     /* pads are 0 to 3 */
            "wgf-script 1\nat 1 pad 0 axis left_x 2\n",
            "wgf-script 1\nat 1 expect score ~ 3\n",  /* no such operator */
            "wgf-script 1\nat 1 expect score == nan\n",
            "wgf-script 1\nat 1 expect score == 1e\n",   /* an exponent with no digits */
            "wgf-script 1\nat 1 expect score == .\n",    /* a point with no digits */
            "wgf-script 1\nat 1 expect score == 0x10\n", /* hex: a script's numbers are decimal */
            "wgf-script 1\nat 1 expect score == 1e999\n", /* not finite */
            "wgf-script 1\nat 1 expect score == 2,5\n",
            "wgf-script 1\nat 1 end\nat 2 end\n",     /* two ends */
            "wgf-script 1\nat 1 end extra\n",         /* more than the command takes */
            "wgf-script 1\nat 1 key tap a\nseed 3\n", /* a seed after the commands */
            "wgf-script 1\nwait 3\n",                 /* not a line the format has */
            "wgf-script 1\nat 1 text\n",              /* nothing to type */
            "wgf-script 1\nat 1 log \x01\n",          /* a control character */
        };
        size_t i;
        for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            if (wgf_app_priv_script_start_text(bad[i])) printf("FAIL: accepted bad script %u\n", (unsigned)i);
            expect(!wgf_app_priv_script_is_running(), "a bad script doesn't run");
        }
        expect(wgf_app_priv_script_start_text("wgf-script 1 # the header\n\n  # nothing else\n"),
               "comments, blank lines, and no commands are a script");
        wgf_app_priv_script_stop(0);
    }
    remove(script_path);
    return failures == 0 ? 0 : 1;
}
