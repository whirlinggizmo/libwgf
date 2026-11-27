#include "wgf_app_autopilot_priv.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_log.h"
#include "wgf_mouse.h"
#include "wgf_platform_priv.h"
#include "wgf_core_format_priv.h"

/* Writing the window's events as an autopilot (wgf_app_autopilot_priv.h; BUILDING.md,
 * "Autopilot files"): what a program built to record writes to its file
 * (wgf_app_record_native.c), and what a replay records (wgf_app_replay.c). The window's
 * events as they arrive, each written for the frame it reaches; the pointer's position, and
 * each finger's, once a frame, where it ended up; and the pads' buttons and axes at each
 * frame's start, as they changed. */

#define AXIS_STEP 0.01f /* an axis's change smaller than this isn't written: a stick's noise */
#define LINE_BYTES 512  /* an autopilot's longest line */

void wgf_app_priv_writer_line(wgf_app_priv_writer_t *writer, const char *format, ...)
{
    char line[LINE_BYTES];
    va_list args;
    int n;
    if (writer->failed) return;
    va_start(args, format);
    n = wgf_core_priv_vformat(line, sizeof(line), format, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n >= sizeof(line)) n = (int)sizeof(line) - 1;
    if (writer->size + (size_t)n + 1 > writer->capacity) {
        size_t capacity = writer->capacity > 0 ? writer->capacity : 1024;
        char *grown;
        while (writer->size + (size_t)n + 1 > capacity) capacity *= 2;
        grown = (char *)realloc(writer->text, capacity);
        if (grown == NULL) {
            wgf_log_error("wgf_autopilot: out of memory recording: what follows is lost");
            writer->failed = true;
            return;
        }
        writer->text = grown;
        writer->capacity = capacity;
    }
    memcpy(writer->text + writer->size, line, (size_t)n + 1);
    writer->size += (size_t)n;
}

void wgf_app_priv_writer_flush(wgf_app_priv_writer_t *writer)
{
    if (writer->typed_count == 0) return;
    writer->typed[writer->typed_count] = '\0';
    wgf_app_priv_writer_line(writer, "at %ld text %s\n", writer->typed_frame, writer->typed);
    writer->typed_count = 0;
}

void wgf_app_priv_writer_take(wgf_app_priv_writer_t *writer)
{
    writer->size = 0;
    if (writer->text != NULL) writer->text[0] = '\0';
}

void wgf_app_priv_writer_free(wgf_app_priv_writer_t *writer)
{
    free(writer->text);
    memset(writer, 0, sizeof(*writer));
}

/* A code point onto the frame's typed text, as UTF-8; a full text written first. */
static void add_typed(wgf_app_priv_writer_t *writer, uint32_t code, long frame)
{
    char bytes[4];
    size_t n, i;
    if (code < 0x20 || code == 0x7F || code > 0x10FFFF || code == '#') return; /* a # would start a comment */
    if (writer->typed_count > 0 &&
        (writer->typed_frame != frame || writer->typed_count + 4 >= sizeof(writer->typed))) {
        wgf_app_priv_writer_flush(writer);
    }
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
    if (writer->typed_count == 0 && code == ' ') return; /* a text line's leading blanks are dropped: not recordable */
    for (i = 0; i < n; i++) writer->typed[writer->typed_count++] = bytes[i];
    writer->typed_frame = frame;
}

/* The finger the window system calls `id`: its number in the autopilot, given one if `add`;
 * -1 for none. */
static int finger_of(wgf_app_priv_writer_t *writer, uintptr_t id, bool add)
{
    int i;
    for (i = 0; i < SAPP_MAX_TOUCHPOINTS; i++) {
        if (writer->fingers[i].down && writer->fingers[i].id == id) return i;
    }
    if (!add) return -1;
    for (i = 0; i < SAPP_MAX_TOUCHPOINTS; i++) {
        if (!writer->fingers[i].down) {
            writer->fingers[i].down = true;
            writer->fingers[i].id = id;
            writer->fingers[i].moved = false;
            return i;
        }
    }
    return -1;
}

static void record_touches(wgf_app_priv_writer_t *writer, const sapp_event *event, long frame)
{
    wgf_platform_priv_presentation_t to; /* the framebuffer's pixels to logical ones, as input reads them */
    int i;
    wgf_platform_priv_get_presentation(&to);
    for (i = 0; i < event->num_touches && i < SAPP_MAX_TOUCHPOINTS; i++) {
        const sapp_touchpoint *touch = &event->touches[i];
        const float x = (touch->pos_x - to.offset_x) / (to.scale_x != 0.0f ? to.scale_x : 1.0f);
        const float y = (touch->pos_y - to.offset_y) / (to.scale_y != 0.0f ? to.scale_y : 1.0f);
        const int finger = finger_of(writer, touch->identifier, event->type == SAPP_EVENTTYPE_TOUCHES_BEGAN);
        if (!touch->changed || finger < 0) continue;
        if (event->type == SAPP_EVENTTYPE_TOUCHES_MOVED) { /* where it ended up, once a frame */
            writer->fingers[finger].moved = true;
            writer->fingers[finger].x = x;
            writer->fingers[finger].y = y;
            continue;
        }
        wgf_app_priv_writer_flush(writer);
        if (event->type == SAPP_EVENTTYPE_TOUCHES_BEGAN) {
            wgf_app_priv_writer_line(writer, "at %ld touch down %d %g %g\n", frame, finger, x, y);
        } else { /* ended, or cancelled */
            wgf_app_priv_writer_line(writer, "at %ld touch up %d %g %g\n", frame, finger, x, y);
            writer->fingers[finger].down = false;
        }
        writer->fingers[finger].moved = false;
    }
}

void wgf_app_priv_writer_event(wgf_app_priv_writer_t *writer, const sapp_event *event, long frame)
{
    const char *name;
    switch (event->type) {
        case SAPP_EVENTTYPE_KEY_DOWN:
        case SAPP_EVENTTYPE_KEY_UP:
            if (event->key_repeat) break;
            name = wgf_app_priv_autopilot_name(WGF_APP_PRIV_NAME_KEY, (int)event->key_code);
            if (name != NULL) {
                wgf_app_priv_writer_flush(writer);
                wgf_app_priv_writer_line(writer, "at %ld key %s %s\n", frame,
                                         event->type == SAPP_EVENTTYPE_KEY_DOWN ? "down" : "up", name);
            }
            break;
        case SAPP_EVENTTYPE_CHAR: add_typed(writer, event->char_code, frame); break;
        case SAPP_EVENTTYPE_MOUSE_MOVE: writer->moved = true; break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
        case SAPP_EVENTTYPE_MOUSE_UP:
            name = wgf_app_priv_autopilot_name(WGF_APP_PRIV_NAME_MOUSE_BUTTON, (int)event->mouse_button);
            if (name != NULL) {
                const wgf_vec2_t at = wgf_mouse_get_position(); /* pressed where the pointer is */
                wgf_app_priv_writer_flush(writer);
                if (at.x != writer->mouse_x || at.y != writer->mouse_y) {
                    wgf_app_priv_writer_line(writer, "at %ld mouse move %g %g\n", frame, at.x, at.y);
                    writer->mouse_x = at.x;
                    writer->mouse_y = at.y;
                }
                wgf_app_priv_writer_line(writer, "at %ld mouse %s %s\n", frame,
                                         event->type == SAPP_EVENTTYPE_MOUSE_DOWN ? "down" : "up", name);
            }
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            wgf_app_priv_writer_flush(writer);
            wgf_app_priv_writer_line(writer, "at %ld mouse scroll %g %g\n", frame, event->scroll_x, event->scroll_y);
            break;
        case SAPP_EVENTTYPE_TOUCHES_BEGAN:
        case SAPP_EVENTTYPE_TOUCHES_MOVED:
        case SAPP_EVENTTYPE_TOUCHES_ENDED:
        case SAPP_EVENTTYPE_TOUCHES_CANCELLED: record_touches(writer, event, frame); break;
        default: break;
    }
}

void wgf_app_priv_writer_frame(wgf_app_priv_writer_t *writer, long frame)
{
    int pad, i;
    if (writer->typed_count > 0 && writer->typed_frame < frame) wgf_app_priv_writer_flush(writer);
    if (writer->moved) { /* where the pointer ended up, once a frame */
        const wgf_vec2_t at = wgf_mouse_get_position();
        writer->moved = false;
        if (at.x != writer->mouse_x || at.y != writer->mouse_y) {
            wgf_app_priv_writer_line(writer, "at %ld mouse move %g %g\n", frame, at.x, at.y);
            writer->mouse_x = at.x;
            writer->mouse_y = at.y;
        }
    }
    for (i = 0; i < SAPP_MAX_TOUCHPOINTS; i++) { /* and each finger */
        if (!writer->fingers[i].down || !writer->fingers[i].moved) continue;
        writer->fingers[i].moved = false;
        wgf_app_priv_writer_line(writer, "at %ld touch move %d %g %g\n", frame, i, writer->fingers[i].x,
                                 writer->fingers[i].y);
    }
    for (pad = 0; pad < WGF_PLATFORM_PRIV_GAMEPADS; pad++) {
        const wgf_platform_priv_gamepad_t *now = wgf_platform_priv_gamepad_get(pad);
        wgf_platform_priv_gamepad_t *was = &writer->pads[pad];
        if (now == NULL) continue;
        if (now->connected != was->connected) {
            wgf_app_priv_writer_line(writer, "at %ld pad %d %s\n", frame, pad,
                                     now->connected ? "connect" : "disconnect");
            memset(was, 0, sizeof(*was));
            was->connected = now->connected;
        }
        if (!now->connected) continue;
        for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPAD_BUTTONS; i++) {
            const char *name = wgf_app_priv_autopilot_name(WGF_APP_PRIV_NAME_PAD_BUTTON, i);
            if (now->buttons[i] == was->buttons[i] || name == NULL) continue;
            wgf_app_priv_writer_line(writer, "at %ld pad %d %s %s\n", frame, pad, now->buttons[i] ? "down" : "up",
                                     name);
            was->buttons[i] = now->buttons[i];
        }
        for (i = 0; i < WGF_PLATFORM_PRIV_GAMEPAD_AXES; i++) {
            const char *name = wgf_app_priv_autopilot_name(WGF_APP_PRIV_NAME_PAD_AXIS, i);
            const float value = roundf(now->axes[i] / AXIS_STEP) * AXIS_STEP;
            if (name == NULL || fabsf(value - was->axes[i]) < AXIS_STEP * 0.5f) continue;
            wgf_app_priv_writer_line(writer, "at %ld pad %d axis %s %g\n", frame, pad, name, value);
            was->axes[i] = value;
        }
    }
}
