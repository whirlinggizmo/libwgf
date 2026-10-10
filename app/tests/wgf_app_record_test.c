#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* setenv */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_app.h"
#include "wgf_app_autopilot_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
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

/* A load in flight from the program's init until its frame `load_frames`, then done: what a
 * recording waits for. */
static int load_frames, loaded_at;
static int load_marker;

static void *load_prepare(const char *path)
{
    (void)path;
    return &load_marker;
}

static wgf_core_priv_load_step_t load_finish(void *prepared, wgf_handle_t resource)
{
    (void)prepared;
    (void)resource;
    return loaded_at >= 0 ? WGF_CORE_PRIV_LOAD_DONE : WGF_CORE_PRIV_LOAD_WAIT;
}

static void load_discard(void *prepared)
{
    (void)prepared;
}

static void load_fail(wgf_handle_t resource)
{
    (void)resource;
}

static const wgf_core_priv_loader_t loader = {"test", load_prepare, load_finish, load_discard, load_fail, NULL, false, NULL};

typedef struct loading_run_t {
    int frames, held_after; /* frames, and ticks with space down once the load ended */
    int done_at;            /* the frame its load was seen done; 0 before */
} loading_run_t;

static void loading_init(void *user)
{
    (void)user;
    loaded_at = -1;
    wgf_core_priv_fs_write("wgf_app_record_test.load", (const unsigned char *)"x", 1);
    wgf_core_priv_load_request(&loader, "wgf_app_record_test.load", 1);
}

static void loading_tick(void *user)
{
    loading_run_t *run = user;
    if (wgf_core_priv_load_get_pending_count() == 0 && wgf_keyboard_is_down(WGF_KEY_SPACE)) run->held_after++;
}

/* Played: space down during the load, up three frames after it ended. */
static void loading_played(void *user)
{
    loading_run_t *run = user;
    run->frames++;
    if (run->frames == 2) push(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_SPACE, 0);
    if (run->frames == load_frames && loaded_at < 0) loaded_at = run->frames;
    if (loaded_at >= 0 && run->done_at == 0 && wgf_core_priv_load_get_pending_count() == 0) run->done_at = run->frames;
    if (run->done_at > 0 && run->frames == run->done_at + 3) push(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_SPACE, 0);
    if (run->done_at > 0 && run->frames == run->done_at + 8) wgf_app_quit();
}

static void loading_flown(void *user)
{
    loading_run_t *run = user;
    run->frames++;
    if (run->frames == load_frames && loaded_at < 0) loaded_at = run->frames;
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

    {
        /* a recording made while the program's init's load is in flight waits for it, and
         * counts its frames from its end: flown back with a load of another length, what
         * was held through the load is held as long after it */
        loading_run_t played_run, flown_run;
        set("LIBWGF_AUTOPILOT", NULL);
        set("LIBWGF_AUTOPILOT_RECORD", recording_path);
        load_frames = 6;
        memset(&played_run, 0, sizeof(played_run));
        wgf_app_run(loading_init, loading_tick, loading_played, NULL, &played_run);
        f = fopen(recording_path, "rb");
        n = f != NULL ? fread(text, 1, sizeof(text) - 1, f) : 0;
        if (f != NULL) fclose(f);
        text[n] = '\0';
        expect(strstr(text, "\nat 0 wait core.loading == 0\n") != NULL && strstr(text, "\nat 0 key down space\n") != NULL &&
                   strstr(text, " key up space\n") != NULL && strstr(text, "\nat 0 key up space\n") == NULL,
               "recorded while loading: a wait for the load, what came during it at 0");

        set("LIBWGF_AUTOPILOT_RECORD", NULL);
        set("LIBWGF_AUTOPILOT", recording_path);
        load_frames = 15; /* a slower load */
        memset(&flown_run, 0, sizeof(flown_run));
        wgf_app_run(loading_init, loading_tick, loading_flown, NULL, &flown_run);
        expect(wgf_app_priv_autopilot_has_passed() && played_run.held_after > 0 &&
                   flown_run.held_after == played_run.held_after,
               "flown with a slower load: held as long after it as it was played");
        remove(recording_path);
        remove("wgf_app_record_test.load");
    }
    return failures == 0 ? 0 : 1;
}
