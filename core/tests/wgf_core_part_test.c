#include <stdio.h>
#include <string.h>

#include "wgf_core_part_priv.h"
#include "wgf_core_priv.h"

/* core's list of optional parts (wgf_core_part_priv.h), without a layer above: a part
 * joins once however often it installs, in its order whatever the order of installing;
 * update runs every part's in order; a layer's stop stops and forgets that layer's parts
 * alone, so the next run's first create installs them again; core's shutdown stops what
 * no layer did. gfx's own parts and hooks are tested in gfx (wgf_gfx_part_test). */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static char trail[64]; /* each call, a letter: what ran, in what order */

static void note(char c)
{
    const size_t n = strlen(trail);
    if (n + 1 < sizeof(trail)) {
        trail[n] = c;
        trail[n + 1] = '\0';
    }
}

static void update_text(float dt) { (void)dt; note('t'); }
static void update_audio(float dt) { (void)dt; note('a'); }
static void stop_text(void) { note('T'); }
static void stop_shadows(void) { note('S'); }
static void stop_audio(void) { note('A'); }

static wgf_core_priv_part_t text = {.name = "text", .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                    .order = WGF_CORE_PRIV_PART_TEXT, .update = update_text, .stop = stop_text};
static wgf_core_priv_part_t shadows = {.name = "shadows", .layer = WGF_CORE_PRIV_PART_LAYER_GFX,
                                       .order = WGF_CORE_PRIV_PART_SHADOWS, .stop = stop_shadows};
static wgf_core_priv_part_t audio = {.name = "audio", .layer = WGF_CORE_PRIV_PART_LAYER_AUDIO,
                                     .order = WGF_CORE_PRIV_PART_AUDIO, .update = update_audio, .stop = stop_audio};

static int listed(void)
{
    int count = 0;
    const wgf_core_priv_part_t *part;
    for (part = wgf_core_priv_part_list(); part != NULL; part = part->next) count++;
    return count;
}

int main(void)
{
    wgf_core_priv_init();
    expect(listed() == 0, "no part before one installs");

    /* installed out of order, one twice: listed once each, in order */
    wgf_core_priv_part_install(&audio);
    wgf_core_priv_part_install(&shadows);
    wgf_core_priv_part_install(&text);
    wgf_core_priv_part_install(&text);
    expect(listed() == 3, "installed twice, listed once");
    expect(wgf_core_priv_part_list() == &text && text.next == &shadows && shadows.next == &audio,
           "listed in their order, whatever the order of installing");
    wgf_core_priv_part_update(0.016f);
    expect(strcmp(trail, "ta") == 0, "update runs each part's, in order, skipping one without");

    /* gfx's stop: its parts alone, in order, then forgotten */
    trail[0] = '\0';
    wgf_core_priv_part_stop(WGF_CORE_PRIV_PART_LAYER_GFX);
    expect(strcmp(trail, "TS") == 0, "a layer's stop stops that layer's parts, in order");
    expect(listed() == 1 && wgf_core_priv_part_list() == &audio && !text.installed && text.next == NULL &&
               !shadows.installed && shadows.next == NULL,
           "and forgets them, leaving the others");
    trail[0] = '\0';
    wgf_core_priv_part_update(0.016f);
    expect(strcmp(trail, "a") == 0, "a forgotten part isn't updated");

    /* a second run: installs again */
    wgf_core_priv_part_install(&text);
    expect(listed() == 2 && wgf_core_priv_part_list() == &text, "the next run's first create installs it again");

    /* core's shutdown: what no layer stopped */
    trail[0] = '\0';
    wgf_core_priv_shutdown();
    expect(strcmp(trail, "AT") == 0, "core's shutdown stops every part left: audio's, then any of gfx's");
    expect(listed() == 0 && !audio.installed && !text.installed, "and forgets them");
    return failures == 0 ? 0 : 1;
}
