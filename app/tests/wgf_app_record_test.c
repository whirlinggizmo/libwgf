#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* setenv */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_app_autopilot_priv.h"
#include "wgf_input.h"
#include "wgf_keyboard.h"
#include "wgf_platform_priv.h"
#include "wgf_probe.h"

/* Recording an autopilot, headless: a run played by events as a window delivers them,
 * written frame by frame, then flown back with the same frames, ticks, presses, and
 * typing. Its own program, apart from the autopilot test: a program that writes its
 * keystrokes to a file is one antivirus may take for a keylogger, and this keeps that
 * question to the one program that has to. A program that doesn't install the recorder
 * records nothing. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

typedef struct run_t {
    int frames, ticks, presses, typed;
} run_t;

static const char *recording_path = "wgf_app_record_test.autopilot";

static void on_tick(void *user)
{
    run_t *run = user;
    run->ticks++;
    if (wgf_keyboard_is_pressed(WGF_KEY_SPACE)) run->presses++;
    wgf_probe_set_value("presses", run->presses);
}

static void on_frame(void *user)
{
    run_t *run = user;
    run->frames++;
    if (wgf_input_get_chars()[0] != '\0') run->typed = (unsigned char)wgf_input_get_chars()[0];
}

/* Played by hand, as the window would deliver it: events pushed from one frame reach the
 * next, as a real window's arrive between frames. */
static void push(sapp_event_type type, sapp_keycode key, uint32_t code)
{
    sapp_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key_code = key;
    event.char_code = code;
    wgf_platform_priv_headless_push_event(&event);
}

static void played_frame(void *user)
{
    run_t *run = user;
    on_frame(user);
    if (run->frames == 3) push(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_SPACE, 0);
    if (run->frames == 6) push(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_SPACE, 0);
    if (run->frames == 8) push(SAPP_EVENTTYPE_CHAR, SAPP_KEYCODE_INVALID, 'Q');
    if (run->frames == 12) wgf_app_quit();
}

static void set(const char *name, const char *value)
{
#if defined(_WIN32)
    _putenv_s(name, value != NULL ? value : "");
#else
    if (value != NULL) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

static void quit_at_twelve(void *user)
{
    run_t *run = user;
    on_frame(user);
    if (run->frames == 12) wgf_app_quit();
}

int main(void)
{
    run_t played, flown;
    FILE *f;
    char text[2048];
    size_t n;

    set("LIBWGF_AUTOPILOT", NULL);
    set("LIBWGF_AUTOPILOT_RECORD", recording_path);
    remove(recording_path);
    memset(&played, 0, sizeof(played));
    wgf_app_run(NULL, on_tick, quit_at_twelve, NULL, &played);
    f = fopen(recording_path, "rb");
    expect(f == NULL, "a program that doesn't install the recorder records nothing");
    if (f != NULL) fclose(f);

    wgf_app_priv_record_install();
    memset(&played, 0, sizeof(played));
    wgf_app_run(NULL, on_tick, played_frame, NULL, &played);
    f = fopen(recording_path, "rb");
    n = f != NULL ? fread(text, 1, sizeof(text) - 1, f) : 0;
    if (f != NULL) fclose(f);
    text[n] = '\0';
    expect(strstr(text, "wgf-autopilot 1\n") == text && strstr(text, "\nat 3 key down space\n") != NULL &&
               strstr(text, "\nat 6 key up space\n") != NULL && strstr(text, "\nat 8 text Q\n") != NULL &&
               strstr(text, "\nat 11 end\n") != NULL && strstr(text, "#   presses == 1\n") != NULL,
           "recorded: each input at the frame it reached, the end, and the probes' last values");

    set("LIBWGF_AUTOPILOT_RECORD", NULL);
    set("LIBWGF_AUTOPILOT", recording_path);
    memset(&flown, 0, sizeof(flown));
    wgf_app_run(NULL, on_tick, on_frame, NULL, &flown);
    expect(wgf_app_priv_autopilot_has_passed() && flown.frames == played.frames && flown.presses == 1 &&
               flown.ticks == played.ticks && flown.typed == 'Q' && played.typed == 'Q',
           "flown: the same frames, ticks, presses, and typing as were played");
    remove(recording_path);
    return failures == 0 ? 0 : 1;
}
