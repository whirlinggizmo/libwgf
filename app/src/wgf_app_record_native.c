#include "wgf_app_autopilot_priv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sokol_app.h"
#include "wgf_log.h"
#include "wgf_mouse.h"
#include "wgf_platform_gamepad_priv.h"
#include "wgf_probe.h"
#include "wgf_random.h"

/* Recording an autopilot, natively (wgf_app_autopilot_priv.h; BUILDING.md, "Autopilot
 * files"), in a program built to record alone: the window's events as they arrive, each
 * written for the frame it reaches; the pointer's position once a frame, where it ended
 * up; and the pads' buttons and axes at each frame's start, as they changed. Lines go to the file as they come, so a crash
 * keeps what was played; the end and the probes' last values are written as it stops. */

#define AXIS_STEP 0.01f /* an axis's change smaller than this isn't written: a stick's noise */

static struct {
    FILE *file;
    bool moved;      /* the pointer moved since the last frame's line */
    float mouse_x, mouse_y;
    char typed[64];  /* the frame's typed characters, written together */
    size_t typed_count;
    long typed_frame;
    wgf_platform_priv_gamepad_t pads[WGF_PLATFORM_PRIV_GAMEPADS]; /* as last written */
} rec;

static void flush_typed(void)
{
    if (rec.typed_count == 0) return;
    rec.typed[rec.typed_count] = '\0';
    fprintf(rec.file, "at %ld text %s\n", rec.typed_frame, rec.typed);
    rec.typed_count = 0;
}

static bool record_start(void)
{
    const char *path = getenv("LIBWGF_AUTOPILOT_RECORD");
    if (path == NULL || path[0] == '\0') return false;
    rec.file = fopen(path, "wb");
    if (rec.file == NULL) {
        wgf_log_error("wgf_autopilot: LIBWGF_AUTOPILOT_RECORD names %s, which can't be written", path);
        return false;
    }
    fprintf(rec.file, "wgf-autopilot 1\n# recorded by hand: every input as it reached the program, frame by frame\n"
                      "seed %d\n",
            wgf_random_get_seed());
    wgf_log_info("wgf_autopilot: recording to %s", path);
    return true;
}

/* A code point onto the frame's typed text, as UTF-8; a full text written first. */
static void add_typed(uint32_t code, long frame)
{
    char bytes[4];
    size_t n, i;
    if (code < 0x20 || code == 0x7F || code > 0x10FFFF || code == '#') return; /* a # would start a comment */
    if (rec.typed_count > 0 && (rec.typed_frame != frame || rec.typed_count + 4 >= sizeof(rec.typed))) flush_typed();
    if (code < 0x80) {
        bytes[0] = (char)code;
        n = 1;
    } else if (code < 0x800) {
        bytes[0] = (char)(0xC0 | (code >> 6));
        bytes[1] = (char)(0x80 | (code & 0x3F));
        n = 2;
    } else if (code < 0x10000) {
        bytes[0] = (char)(0xE0 | (code >> 12));
        bytes[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (code & 0x3F));
        n = 3;
    } else {
        bytes[0] = (char)(0xF0 | (code >> 18));
        bytes[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        bytes[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        bytes[3] = (char)(0x80 | (code & 0x3F));
        n = 4;
    }
    if (rec.typed_count == 0 && code == ' ') return; /* a text line's leading blanks are dropped: not recordable */
    for (i = 0; i < n; i++) rec.typed[rec.typed_count++] = bytes[i];
    rec.typed_frame = frame;
}

static void record_event(const void *event_ptr, long frame)
{
    const sapp_event *event = (const sapp_event *)event_ptr;
    const char *name;
    if (rec.file == NULL) return;
    switch (event->type) {
        case SAPP_EVENTTYPE_KEY_DOWN:
        case SAPP_EVENTTYPE_KEY_UP:
            if (event->key_repeat) break;
            name = wgf_app_priv_autopilot_name(WGF_APP_PRIV_NAME_KEY, (int)event->key_code);
            if (name != NULL) {
                flush_typed();
                fprintf(rec.file, "at %ld key %s %s\n", frame, event->type == SAPP_EVENTTYPE_KEY_DOWN ? "down" : "up",
                        name);
            }
            break;
        case SAPP_EVENTTYPE_CHAR: add_typed(event->char_code, frame); break;
        case SAPP_EVENTTYPE_MOUSE_MOVE: rec.moved = true; break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
        case SAPP_EVENTTYPE_MOUSE_UP:
            name = wgf_app_priv_autopilot_name(WGF_APP_PRIV_NAME_MOUSE_BUTTON, (int)event->mouse_button);
            if (name != NULL) {
                const wgf_vec2_t at = wgf_mouse_get_position(); /* pressed where the pointer is */
                flush_typed();
                if (at.x != rec.mouse_x || at.y != rec.mouse_y) {
                    fprintf(rec.file, "at %ld mouse move %g %g\n", frame, at.x, at.y);
                    rec.mouse_x = at.x;
                    rec.mouse_y = at.y;
                }
                fprintf(rec.file, "at %ld mouse %s %s\n", frame, event->type == SAPP_EVENTTYPE_MOUSE_DOWN ? "down" : "up",
                        name);
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            flush_typed();
            fprintf(rec.file, "at %ld mouse scroll %g %g\n", frame, event->scroll_x, event->scroll_y);
            break;
        default: break;
    }
}

static void record_frame(long frame)
{
    int pad, i;
    if (rec.file == NULL) return;
    if (rec.typed_count > 0 && rec.typed_frame < frame) flush_typed();
    if (rec.moved) { /* where the pointer ended up, once a frame */
        const wgf_vec2_t at = wgf_mouse_get_position();
        rec.moved = false;
        if (at.x != rec.mouse_x || at.y != rec.mouse_y) {
            fprintf(rec.file, "at %ld mouse move %g %g\n", frame, at.x, at.y);
            rec.mouse_x = at.x;
            rec.mouse_y = at.y;
        }
    }
    for (pad = 0; pad < WGF_PLATFORM_PRIV_GAMEPADS; pad++) {
        const wgf_platform_priv_gamepad_t *now = wgf_platform_priv_gamepad_get(pad);
        wgf_platform_priv_gamepad_t *was = &rec.pads[pad];
        if (now == NULL) continue;
        if (now->connected != was->connected) {
            fprintf(rec.file, "at %ld pad %d %s\n", frame, pad, now->connected ? "connect" : "disconnect");
            memset(was, 0, sizeof(*was));
            was->connected = now->connected;
        }
        if (!now->connected) continue;
        for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS; i++) {
            const char *name = wgf_app_priv_autopilot_name(WGF_APP_PRIV_NAME_PAD_BUTTON, i);
            if (now->buttons[i] == was->buttons[i] || name == NULL) continue;
            fprintf(rec.file, "at %ld pad %d %s %s\n", frame, pad, now->buttons[i] ? "down" : "up", name);
            was->buttons[i] = now->buttons[i];
        }
        for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPAD_AXES; i++) {
            const char *name = wgf_app_priv_autopilot_name(WGF_APP_PRIV_NAME_PAD_AXIS, i);
            const float value = roundf(now->axes[i] / AXIS_STEP) * AXIS_STEP;
            if (name == NULL || fabsf(value - was->axes[i]) < AXIS_STEP * 0.5f) continue;
            fprintf(rec.file, "at %ld pad %d axis %s %g\n", frame, pad, name, value);
            was->axes[i] = value;
        }
    }
    fflush(rec.file); /* a crash keeps what was played */
}

static void record_stop(long frames)
{
    int i;
    if (rec.file == NULL) return;
    flush_typed();
    fprintf(rec.file, "at %ld end\n# the probes as the recording ended, to start its expectations from:\n",
            frames > 0 ? frames - 1 : 0);
    for (i = 0; i < wgf_probe_get_count(); i++) {
        const char *name = wgf_probe_get_name(i);
        const char *text = wgf_probe_get_text(name);
        if (text[0] != '\0') {
            fprintf(rec.file, "#   %s == \"%s\"\n", name, text);
        } else {
            fprintf(rec.file, "#   %s == %g\n", name, wgf_probe_get_value(name));
        }
    }
    fclose(rec.file);
    wgf_log_info("wgf_autopilot: recorded %ld frames", frames);
    memset(&rec, 0, sizeof(rec));
}

void wgf_app_priv_record_install(void)
{
    static const wgf_app_priv_recorder_t recorder = {record_start, record_event, record_frame, record_stop};
    wgf_app_priv_set_recorder(&recorder);
}
