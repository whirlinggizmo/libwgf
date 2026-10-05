#include "wgf.h"
#include "wgf_core_priv.h"

#include <stdbool.h>

#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_part_priv.h"
#include "wgf_core_probe_priv.h"
#include "wgf_core_random_priv.h"
#include "wgf_core_time_priv.h"

static bool running;

const char *wgf_version_get(void)
{
    return WGF_VERSION_STRING;
}

void wgf_core_priv_init(void)
{
    if (running) return;
    wgf_core_priv_time_init();
    wgf_core_priv_random_init();
    wgf_core_priv_probe_init();
    wgf_core_priv_fs_init(NULL);
    wgf_core_priv_load_init();
    running = true;
}

void wgf_core_priv_shutdown(void)
{
    if (!running) return;
    /* the parts no layer stopped first (audio's), before what they use */
    wgf_core_priv_part_stop(WGF_CORE_PRIV_PART_LAYER_AUDIO);
    wgf_core_priv_part_stop(WGF_CORE_PRIV_PART_LAYER_GFX);
    wgf_core_priv_part_stop(WGF_CORE_PRIV_PART_LAYER_ASSET); /* after the layers loading through it */
    wgf_core_priv_load_deinit(); /* before fs: its workers read files */
    wgf_core_priv_fs_deinit();
    wgf_core_priv_probe_deinit();
    wgf_core_priv_time_deinit();
    running = false;
}

void wgf_core_priv_update(void)
{
    if (!running) return;
    wgf_core_priv_fs_update();
    wgf_core_priv_load_update();
}
