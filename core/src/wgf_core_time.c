#include "wgf_time.h"

#include <stdbool.h>
#include <stdint.h>

#include "sokol_time.h"
#include "wgf_core_time_priv.h"

static bool time_running;
static uint64_t time_start;

void wgf_core_priv_time_init(void)
{
    stm_setup();
    time_start = stm_now();
    time_running = true;
}

void wgf_core_priv_time_deinit(void)
{
    time_running = false;
}

double wgf_time_get_seconds(void)
{
    return time_running ? stm_sec(stm_since(time_start)) : 0.0;
}
