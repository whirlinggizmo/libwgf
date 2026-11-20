#include "wgf_app_autopilot_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sokol_app.h"
#include "wgf_core_load_priv.h"
#include "wgf_log.h"
#include "wgf_probe.h"
#include "wgf_random.h"

/* Recording an autopilot, natively (wgf_app_autopilot_priv.h; BUILDING.md, "Autopilot
 * files"), in a program built to record alone: the window's events written as an
 * autopilot is (wgf_app_record.c), each frame's lines going to the file as the frame
 * starts, so a crash keeps what was played; the end and the probes' last values are
 * written as it stops.
 *
 * A program whose loads are in flight as recording starts (its init's) is recorded from
 * their end: the file waits for them (`wait core.loading == 0`), its frames count from
 * the program's frame they ended in, and what was given during them is written at 0, so
 * a replay holds it through its own load, however long that takes. */

static struct {
    FILE *file;
    wgf_app_priv_writer_t writer;
    bool decided;    /* whether it waits, decided at the first event or frame (the init's loads made) */
    bool waits;      /* the start's loads were in flight: frames count from their end */
    long start;      /* waiting: the program's frame they ended in; -1 until they have */
} rec;

/* The writer's lines so far, to the file. */
static void write_out(void)
{
    if (rec.writer.size > 0) fwrite(rec.writer.text, 1, rec.writer.size, rec.file);
    wgf_app_priv_writer_take(&rec.writer);
}

/* Whether the recording waits for the start's loads, decided once, after the program's init. */
static void decide(void)
{
    if (rec.decided) return;
    rec.decided = true;
    rec.start = -1;
    rec.waits = wgf_core_priv_load_get_pending_count() > 0;
    if (rec.waits) {
        wgf_app_priv_writer_line(&rec.writer, "# its start's loads were in flight: what follows counts from their end\n"
                                              "at 0 wait core.loading == 0\n");
    }
}

/* The program's frame `frame` as the recording's: itself, or when the recording waits,
 * counted from the frame the start's loads ended in (0 until then). */
static long at(long frame)
{
    if (!rec.waits) return frame;
    return rec.start < 0 || frame <= rec.start ? 0 : frame - rec.start;
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

static void record_event(const void *event, long frame)
{
    if (rec.file == NULL) return;
    decide();
    wgf_app_priv_writer_event(&rec.writer, (const sapp_event *)event, at(frame));
}

static void record_frame(long frame)
{
    if (rec.file == NULL) return;
    decide();
    if (rec.waits && rec.start < 0 && wgf_core_priv_load_get_pending_count() == 0) rec.start = frame;
    wgf_app_priv_writer_frame(&rec.writer, at(frame));
    write_out();
    fflush(rec.file); /* a crash keeps what was played */
}

static void record_stop(long frames)
{
    int i;
    if (rec.file == NULL) return;
    wgf_app_priv_writer_flush(&rec.writer);
    write_out();
    fprintf(rec.file, "at %ld end\n# the probes as the recording ended, to start its expectations from:\n",
            at(frames > 0 ? frames - 1 : 0));
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
    wgf_app_priv_writer_free(&rec.writer);
    memset(&rec, 0, sizeof(rec));
}

void wgf_app_priv_record_install(void)
{
    static const wgf_app_priv_recorder_t recorder = {record_start, record_event, record_frame, record_stop};
    wgf_app_priv_set_recorder(&recorder);
}
