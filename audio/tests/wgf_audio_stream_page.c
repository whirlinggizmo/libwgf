#include <emscripten.h>
#include <stddef.h>

#include "wgf_app.h"
#include "wgf_asset.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_sound.h"
#include "wgf_time.h"
#include "wgf_voice.h"
#include "wgf_window.h"

/* The page tools/check_stream.py visits: a streamed sound from beside the page, a mod's
 * redirect asked first, which hasn't the file (404). Its file is played from a copy here
 * when there is one, else fetched by its <audio> element as it plays. With "?ensure" the
 * page ensures the file first and makes the sound once it is local. It plays at once and
 * logs, four times a second, the sound's status and length and the voice's state and
 * position, which the tool reads against what its server has sent. */

static wgf_handle_t ensured, sound, voice;
static double next_log;

static void play(void)
{
    sound = wgf_sound_create_streamed("music/long.ogg");
    voice = wgf_voice_create(sound);
    wgf_voice_play(voice); /* starts once its sound can */
}

static void init(void *user)
{
    (void)user;
    wgf_asset_add_redirect("music/", "mods/loud/music/");
    if (emscripten_run_script_int("location.search === '?ensure'")) {
        ensured = wgf_asset_ensure("music/long.ogg", NULL, 0);
    } else {
        play();
    }
    wgf_log_info("stream page: started");
}

static void frame(void *user)
{
    const double now = wgf_time_get_seconds();
    (void)user;
    if (ensured != 0 && wgf_asset_task_get_status(ensured) != WGF_ASSET_TASK_STATUS_PENDING) {
        wgf_log_info("stream page: ensured, %s", wgf_asset_task_get_status(ensured) == WGF_ASSET_TASK_STATUS_DONE
                                                     ? "done"
                                                     : "FAILED");
        wgf_asset_task_destroy(ensured);
        ensured = 0;
        play();
    }
    if (now < next_log || sound == 0) return;
    next_log = now + 0.25;
    wgf_log_info("stream page: status %d duration %.2f state %d position %.2f", (int)wgf_resource_get_status(sound),
                 (double)wgf_sound_get_duration(sound), (int)wgf_voice_get_state(voice),
                 (double)wgf_voice_get_position(voice));
}

int main(void)
{
    wgf_window_set_size(64, 64);
    wgf_app_run(init, NULL, frame, NULL, NULL);
    return 0;
}
