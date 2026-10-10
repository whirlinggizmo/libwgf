#include "wgf_core_thread_priv.h"

/* The web build without threads: one thread, so there is nothing to start, and
 * nothing for a lock to keep out. A web build with threads uses
 * wgf_core_thread_posix.c instead. */

bool wgf_core_priv_thread_is_available(void)
{
    return false;
}

int wgf_core_priv_thread_get_cpu_count(void)
{
    return 1;
}

bool wgf_core_priv_thread_create(wgf_core_priv_thread_t *thread, wgf_core_priv_thread_fn fn, void *arg)
{
    (void)thread;
    (void)fn;
    (void)arg;
    return false;
}

void wgf_core_priv_thread_join(wgf_core_priv_thread_t *thread)
{
    (void)thread;
}

void wgf_core_priv_thread_detach(wgf_core_priv_thread_t *thread)
{
    (void)thread;
}

void wgf_core_priv_mutex_init(wgf_core_priv_mutex_t *mutex)
{
    (void)mutex;
}

void wgf_core_priv_mutex_destroy(wgf_core_priv_mutex_t *mutex)
{
    (void)mutex;
}

void wgf_core_priv_mutex_lock(wgf_core_priv_mutex_t *mutex)
{
    (void)mutex;
}

void wgf_core_priv_mutex_unlock(wgf_core_priv_mutex_t *mutex)
{
    (void)mutex;
}

void wgf_core_priv_cond_init(wgf_core_priv_cond_t *cond)
{
    (void)cond;
}

void wgf_core_priv_cond_destroy(wgf_core_priv_cond_t *cond)
{
    (void)cond;
}

void wgf_core_priv_cond_wait(wgf_core_priv_cond_t *cond, wgf_core_priv_mutex_t *mutex)
{
    /* With one thread nothing could ever broadcast, so a wait would hang forever:
       callers check is_available and don't wait. */
    (void)cond;
    (void)mutex;
}

void wgf_core_priv_cond_broadcast(wgf_core_priv_cond_t *cond)
{
    (void)cond;
}

void wgf_core_priv_thread_service(void)
{
}
