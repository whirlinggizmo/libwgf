/* SRW locks and condition variables are Vista's: an older MinGW targets XP unless
 * told otherwise, and then declares neither. */
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "wgf_core_thread_priv.h"

#include <stdlib.h>
#include <windows.h>

/* Win32 threads. A mutex is an SRW lock, not a CRITICAL_SECTION: one pointer at
 * every width, and not recursive, like the default pthread mutex. */

_Static_assert(sizeof(HANDLE) <= sizeof(wgf_core_priv_thread_t), "wgf_core_priv_thread_t too small");
_Static_assert(sizeof(SRWLOCK) <= sizeof(wgf_core_priv_mutex_t), "wgf_core_priv_mutex_t too small");
_Static_assert(sizeof(CONDITION_VARIABLE) <= sizeof(wgf_core_priv_cond_t), "wgf_core_priv_cond_t too small");

#define THREAD(t) ((HANDLE *)(void *)(t)->storage)
#define MUTEX(m) ((SRWLOCK *)(void *)(m)->storage)
#define COND(c) ((CONDITION_VARIABLE *)(void *)(c)->storage)

typedef struct start_t {
    wgf_core_priv_thread_fn fn;
    void *arg;
} start_t;

static DWORD WINAPI run(LPVOID param)
{
    start_t start = *(start_t *)param;
    free(param);
    start.fn(start.arg);
    return 0;
}

bool wgf_core_priv_thread_is_available(void)
{
    return true;
}

int wgf_core_priv_thread_get_cpu_count(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
}

bool wgf_core_priv_thread_create(wgf_core_priv_thread_t *thread, wgf_core_priv_thread_fn fn, void *arg)
{
    start_t *start = (start_t *)malloc(sizeof(start_t));
    if (start == NULL) return false;
    start->fn = fn;
    start->arg = arg;
    *THREAD(thread) = CreateThread(NULL, 0, run, start, 0, NULL);
    if (*THREAD(thread) == NULL) {
        free(start);
        return false;
    }
    return true;
}

void wgf_core_priv_thread_join(wgf_core_priv_thread_t *thread)
{
    WaitForSingleObject(*THREAD(thread), INFINITE);
    CloseHandle(*THREAD(thread));
}

void wgf_core_priv_thread_detach(wgf_core_priv_thread_t *thread)
{
    CloseHandle(*THREAD(thread));
}

void wgf_core_priv_mutex_init(wgf_core_priv_mutex_t *mutex)
{
    InitializeSRWLock(MUTEX(mutex));
}

void wgf_core_priv_mutex_destroy(wgf_core_priv_mutex_t *mutex)
{
    (void)mutex; /* an SRW lock holds nothing to free */
}

void wgf_core_priv_mutex_lock(wgf_core_priv_mutex_t *mutex)
{
    AcquireSRWLockExclusive(MUTEX(mutex));
}

void wgf_core_priv_mutex_unlock(wgf_core_priv_mutex_t *mutex)
{
    ReleaseSRWLockExclusive(MUTEX(mutex));
}

void wgf_core_priv_cond_init(wgf_core_priv_cond_t *cond)
{
    InitializeConditionVariable(COND(cond));
}

void wgf_core_priv_cond_destroy(wgf_core_priv_cond_t *cond)
{
    (void)cond; /* nor does a condition variable */
}

void wgf_core_priv_cond_wait(wgf_core_priv_cond_t *cond, wgf_core_priv_mutex_t *mutex)
{
    SleepConditionVariableSRW(COND(cond), MUTEX(mutex), INFINITE, 0);
}

void wgf_core_priv_cond_broadcast(wgf_core_priv_cond_t *cond)
{
    WakeAllConditionVariable(COND(cond));
}

void wgf_core_priv_thread_service(void)
{
}
