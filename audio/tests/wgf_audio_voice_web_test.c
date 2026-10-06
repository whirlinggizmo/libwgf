#include <emscripten.h>

#include "wgf_app.h"
#include "wgf_fs.h"
#include "wgf_time.h"
#include "wgf_window.h"

/* Voices in a browser (tools/run_in_browser.py, which lets a page play sound without an
 * input), played by Web Audio: wgf_audio_voice_checks.c's steps, the native test's,
 * each run once its wait has passed by the page's clock, positions read from the audio
 * clock. A frame's length and the audio clock's steps make the tolerance wider than
 * natively's. */

#define TOLERANCE 0.06f

#include "wgf_audio_voice_checks.c"

static int frames, stage, next;
static wgf_handle_t task;
static double due;
static unsigned char wav[44 + 44100 * 2];

static void on_frame(void *user)
{
    (void)user;
    if (++frames > 60 * 60) {
        printf("FAIL: still waiting at stage %d, step %d\n", stage, next);
        emscripten_force_exit(1);
        return;
    }
    if (stage == 0) {
        task = wgf_fs_write("sounds/second.wav", wav, make_second(wav));
        stage++;
    } else if (stage == 1 && wgf_fs_task_get_status(task) != WGF_FS_TASK_STATUS_PENDING) {
        expect(wgf_fs_task_get_status(task) == WGF_FS_TASK_STATUS_DONE, "the WAV written");
        wgf_fs_task_destroy(task);
        sound = wgf_sound_create("sounds/second.wav");
        streamed = wgf_sound_create_streamed("sounds/second.wav");
        stage++;
    } else if (stage == 2 && wgf_resource_get_status(sound) != WGF_RESOURCE_STATUS_PENDING &&
               wgf_resource_get_status(streamed) != WGF_RESOURCE_STATUS_PENDING) {
        due = wgf_time_get_seconds();
        stage++;
    } else if (stage == 3 && wgf_time_get_seconds() >= due) {
        const double wait = step(next++);
        if (wait < 0.0) {
            emscripten_force_exit(failures == 0 ? 0 : 1);
            return;
        }
        due = wgf_time_get_seconds() + wait;
    }
}

int main(void)
{
    wgf_window_set_size(64, 64);
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
