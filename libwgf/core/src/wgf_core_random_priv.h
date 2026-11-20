#ifndef WGF_CORE_RANDOM_PRIV_H
#define WGF_CORE_RANDOM_PRIV_H

/* Seed the program's random numbers from the clock, as core starts (wgf_random.h). */
void wgf_core_priv_random_init(void);
/* The game's generators let go of, as core stops. */
void wgf_core_priv_random_shutdown(void);

#endif
