#include <stdio.h>

#include "wgf_core_thread_priv.h"

/* Threads where there are any: workers adding under a mutex reach the exact total,
 * and a condition variable hands work over. Where there are none (the web build
 * without threads), create refuses and the locks are harmless. */

#define WORKERS 4
#define ADDS 100000

static int failures;
static wgf_core_priv_mutex_t mutex;
static wgf_core_priv_cond_t cond;
static long total;
static int ready; /* set by the main thread, waited for by the workers */
static int done;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void worker(void *arg)
{
    int i;
    (void)arg;
    wgf_core_priv_mutex_lock(&mutex);
    while (!ready) wgf_core_priv_cond_wait(&cond, &mutex); /* the handover */
    wgf_core_priv_mutex_unlock(&mutex);
    for (i = 0; i < ADDS; i++) {
        wgf_core_priv_mutex_lock(&mutex);
        total++;
        wgf_core_priv_mutex_unlock(&mutex);
    }
    wgf_core_priv_mutex_lock(&mutex);
    done++;
    wgf_core_priv_cond_broadcast(&cond);
    wgf_core_priv_mutex_unlock(&mutex);
}

int main(void)
{
    wgf_core_priv_thread_t threads[WORKERS];
    int i, started = 0;

    expect(wgf_core_priv_thread_get_cpu_count() >= 1, "at least one core");
    wgf_core_priv_mutex_init(&mutex);
    wgf_core_priv_cond_init(&cond);

    if (!wgf_core_priv_thread_is_available()) {
        expect(!wgf_core_priv_thread_create(&threads[0], worker, NULL), "no threads: create refuses");
        wgf_core_priv_mutex_lock(&mutex); /* harmless with one thread */
        wgf_core_priv_mutex_unlock(&mutex);
        wgf_core_priv_cond_broadcast(&cond);
    } else {
        for (i = 0; i < WORKERS; i++) {
            if (wgf_core_priv_thread_create(&threads[i], worker, NULL)) started++;
        }
        expect(started == WORKERS, "every worker started");
        wgf_core_priv_mutex_lock(&mutex);
        ready = 1;
        wgf_core_priv_cond_broadcast(&cond);
        while (done < started) wgf_core_priv_cond_wait(&cond, &mutex); /* the workers finishing */
        wgf_core_priv_mutex_unlock(&mutex);
        for (i = 0; i < started; i++) wgf_core_priv_thread_join(&threads[i]);
        expect(total == (long)started * ADDS, "the mutex kept every add");
    }

    wgf_core_priv_cond_destroy(&cond);
    wgf_core_priv_mutex_destroy(&mutex);
    return failures == 0 ? 0 : 1;
}
