#include <emscripten.h>
#include <stdio.h>

#include "wgf_app.h"
#include "wgf_time.h"
#include "wgf_voice.h"
#include "wgf_window.h"

/* A pushed voice in a browser (tools/run_in_browser.py, which lets a page play sound
 * without an input): its queue taken from by its ScriptProcessorNode as the page's clock
 * runs, nothing taken while paused, twice as fast at pitch 2, running out an underrun,
 * and stop emptying it. The processor takes 2048 frames of the context's at a time, so
 * the amounts are checked loosely; the exact ones are wgf_audio_pushed_test's, natively. */

static int failures, frames, stage;
static double due;
static wgf_voice_t voice;
static int queued;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void on_frame(void *user)
{
    static float second[22050];
    (void)user;
    if (++frames > 60 * 30) {
        printf("FAIL: still waiting at stage %d\n", stage);
        emscripten_force_exit(1);
        return;
    }
    if (wgf_time_get_seconds() < due) return;
    switch (stage++) {
    case 0:
        for (int i = 0; i < 22050; i++) second[i] = (i % 50) < 25 ? 0.1f : -0.1f;
        voice = wgf_voice_create_pushed(22050, 1, 22050);
        expect(wgf_voice_push(voice, second, 22050) == 22050 && wgf_voice_get_room(voice) == 0, "a second pushed");
        expect(wgf_voice_play(voice), "played");
        due = wgf_time_get_seconds() + 0.5;
        break;
    case 1:
        queued = wgf_voice_get_queued(voice);
        printf("wgf_audio_pushed_web_test: half a second played, %d frames of 22050 left\n", queued);
        expect(queued < 22050 - 2205 && queued > 22050 - 22050 * 8 / 10, "about half a second's taken");
        expect(wgf_voice_get_position(voice) > 0.1f, "its position on");
        expect(wgf_voice_pause(voice), "paused");
        queued = wgf_voice_get_queued(voice);
        due = wgf_time_get_seconds() + 0.3;
        break;
    case 2:
        expect(wgf_voice_get_queued(voice) == queued, "paused: nothing taken");
        expect(wgf_voice_get_underruns(voice) == 0, "no underrun while it had samples");
        expect(wgf_voice_resume(voice) && wgf_voice_set_pitch(voice, 2.0f), "resumed at pitch 2");
        due = wgf_time_get_seconds() + 0.15;
        break;
    case 3:
        printf("wgf_audio_pushed_web_test: 0.15 s more at pitch 2, %d frames taken\n",
               queued - wgf_voice_get_queued(voice));
        expect(queued - wgf_voice_get_queued(voice) > 22050 * 15 / 100, "pitch 2: more than its rate's taken");
        due = wgf_time_get_seconds() + 1.0;
        break;
    case 4:
        expect(wgf_voice_get_queued(voice) == 0 && wgf_voice_get_underruns(voice) == 1 &&
                   wgf_voice_get_state(voice) == WGF_PLAY_STATE_PLAYING,
               "run out: one underrun, still PLAYING");
        expect(wgf_voice_push(voice, second, 100) == 100 && wgf_voice_stop(voice) && wgf_voice_get_queued(voice) == 0,
               "stopped: emptied");
        wgf_voice_destroy(voice);
        expect(wgf_voice_get_room(voice) == 0, "destroyed: refused");
        emscripten_force_exit(failures == 0 ? 0 : 1);
        break;
    default: break;
    }
}

int main(void)
{
    wgf_app_run(NULL, NULL, on_frame, NULL, NULL);
    return 0;
}
