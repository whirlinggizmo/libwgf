#ifndef WGF_CORE_THREAD_PRIV_H
#define WGF_CORE_THREAD_PRIV_H

#include <stdbool.h>

/* Threads for library internals, such as the load pipeline's workers: POSIX
 * threads, or Win32 on Windows. A web build without threads has none:
 * wgf_core_priv_thread_is_available is false, create fails, and the mutex and
 * condition calls do nothing, which is safe with one thread. Cribbed from
 * wgrender's wgr_thread.
 *
 * The types are storage of a fixed size, the same everywhere, so this header has
 * no platform #if; each platform's file asserts its own types fit. */

typedef union wgf_core_priv_thread_t {
    void *align;
    unsigned char storage[16];
} wgf_core_priv_thread_t;

typedef union wgf_core_priv_mutex_t {
    void *align;
    unsigned char storage[128];
} wgf_core_priv_mutex_t;

typedef union wgf_core_priv_cond_t {
    void *align;
    unsigned char storage[128];
} wgf_core_priv_cond_t;

typedef void (*wgf_core_priv_thread_fn)(void *arg);

bool wgf_core_priv_thread_is_available(void);

/* Logical CPU cores, at least 1. */
int wgf_core_priv_thread_get_cpu_count(void);

bool wgf_core_priv_thread_create(wgf_core_priv_thread_t *thread, wgf_core_priv_thread_fn fn, void *arg);
void wgf_core_priv_thread_join(wgf_core_priv_thread_t *thread);
/* Let the thread end on its own; its resources are freed when it does. */
void wgf_core_priv_thread_detach(wgf_core_priv_thread_t *thread);

/* On the main thread, run what the other threads asked of it. Natively nothing. On the
 * web with threads, Emscripten proxies a worker's file reads (the JS file system is the
 * main thread's) to the main thread, which runs them when it returns to the event loop:
 * a program that waits in a loop instead (a test in main) would never see its workers
 * finish, so core's update calls this. */
void wgf_core_priv_thread_service(void);

/* Not recursive: a thread must not lock a mutex it holds. */
void wgf_core_priv_mutex_init(wgf_core_priv_mutex_t *mutex);
void wgf_core_priv_mutex_destroy(wgf_core_priv_mutex_t *mutex);
void wgf_core_priv_mutex_lock(wgf_core_priv_mutex_t *mutex);
void wgf_core_priv_mutex_unlock(wgf_core_priv_mutex_t *mutex);

/* wait unlocks `mutex` while it waits and holds it again when it returns; it can
 * return without a broadcast, so wait in a loop on the condition itself. */
void wgf_core_priv_cond_init(wgf_core_priv_cond_t *cond);
void wgf_core_priv_cond_destroy(wgf_core_priv_cond_t *cond);
void wgf_core_priv_cond_wait(wgf_core_priv_cond_t *cond, wgf_core_priv_mutex_t *mutex);
void wgf_core_priv_cond_broadcast(wgf_core_priv_cond_t *cond);

#endif
