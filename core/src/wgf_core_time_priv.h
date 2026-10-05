#ifndef WGF_CORE_TIME_PRIV_H
#define WGF_CORE_TIME_PRIV_H

/* The clock wgf_time_get_seconds reads, started and stopped with core. */
void wgf_core_priv_time_init(void);
void wgf_core_priv_time_deinit(void);

#endif
