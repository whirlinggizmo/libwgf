#define _POSIX_C_SOURCE 200809L /* pthreads and sysconf, under strict C11 */
#define _DARWIN_C_SOURCE /* macOS hides _SC_NPROCESSORS_ONLN under strict POSIX without it */

#include "wgf_core_thread_priv.h"

#include <pthread.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/threading.h>
#endif
#include <stdlib.h>
#include <unistd.h>

/* POSIX threads: Linux, macOS, and a web build with threads. macOS's mutex is 64
 * bytes, the largest of them; the storage leaves room. */

_Static_assert(sizeof(pthread_t) <= sizeof(wgf_core_priv_thread_t), "wgf_core_priv_thread_t too small");
_Static_assert(sizeof(pthread_mutex_t) <= sizeof(wgf_core_priv_mutex_t), "wgf_core_priv_mutex_t too small");
_Static_assert(sizeof(pthread_cond_t) <= sizeof(wgf_core_priv_cond_t), "wgf_core_priv_cond_t too small");

#define THREAD(t) ((pthread_t *)(void *)(t)->storage)
#define MUTEX(m) ((pthread_mutex_t *)(void *)(m)->storage)
#define COND(c) ((pthread_cond_t *)(void *)(c)->storage)

typedef struct start_t {
    wgf_core_priv_thread_fn fn;
    void *arg;
} start_t;

static void *run(void *param)
{
    start_t start = *(start_t *)param;
    free(param);
    start.fn(start.arg);
    return NULL;
}

bool wgf_core_priv_thread_is_available(void)
{
    return true;
}

int wgf_core_priv_thread_get_cpu_count(void)
{
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (int)count : 1;
}

bool wgf_core_priv_thread_create(wgf_core_priv_thread_t *thread, wgf_core_priv_thread_fn fn, void *arg)
{
    start_t *start = (start_t *)malloc(sizeof(start_t));
    if (start == NULL) return false;
    start->fn = fn;
    start->arg = arg;
    if (pthread_create(THREAD(thread), NULL, run, start) != 0) {
        free(start);
        return false;
    }
    return true;
}

void wgf_core_priv_thread_join(wgf_core_priv_thread_t *thread)
{
    pthread_join(*THREAD(thread), NULL);
}

void wgf_core_priv_thread_detach(wgf_core_priv_thread_t *thread)
{
    pthread_detach(*THREAD(thread));
}

void wgf_core_priv_mutex_init(wgf_core_priv_mutex_t *mutex)
{
    pthread_mutex_init(MUTEX(mutex), NULL);
}

void wgf_core_priv_mutex_destroy(wgf_core_priv_mutex_t *mutex)
{
    pthread_mutex_destroy(MUTEX(mutex));
}

void wgf_core_priv_mutex_lock(wgf_core_priv_mutex_t *mutex)
{
    pthread_mutex_lock(MUTEX(mutex));
}

void wgf_core_priv_mutex_unlock(wgf_core_priv_mutex_t *mutex)
{
    pthread_mutex_unlock(MUTEX(mutex));
}

void wgf_core_priv_cond_init(wgf_core_priv_cond_t *cond)
{
    pthread_cond_init(COND(cond), NULL);
}

void wgf_core_priv_cond_destroy(wgf_core_priv_cond_t *cond)
{
    pthread_cond_destroy(COND(cond));
}

void wgf_core_priv_cond_wait(wgf_core_priv_cond_t *cond, wgf_core_priv_mutex_t *mutex)
{
    pthread_cond_wait(COND(cond), MUTEX(mutex));
}

void wgf_core_priv_cond_broadcast(wgf_core_priv_cond_t *cond)
{
    pthread_cond_broadcast(COND(cond));
}

void wgf_core_priv_thread_service(void)
{
#if defined(__EMSCRIPTEN__)
    emscripten_main_thread_process_queued_calls(); /* the workers' proxied file reads, among them */
#endif
}
