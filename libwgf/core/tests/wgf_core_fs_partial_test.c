#include <stdio.h>
#include <string.h>

#include "wgf_core_fs_priv.h"
#include "wgf_core_os_priv.h"
#include "wgf_core_thread_priv.h"

/* A download read while it arrives, by fs's rule (wgf_core_fs_priv.h): a writer thread
 * appends to a file under .part/ in pieces of many sizes, as a program's fetcher does,
 * while the main thread reads on from what it has; then the main thread moves it into
 * place and reads on under the final path. Every read must be the start of the whole
 * file. Its point is a build with tsan. Native only (CMake builds it there); files land
 * under fs_partial_test_root/ in the working dir. */

#define TOTAL (3 * 1024 * 1024 + 17)

static int failures;
static wgf_core_priv_mutex_t mutex;
static int written_all; /* set by the writer once its file is closed */
static int write_failed;
static char partial[256];

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static unsigned char byte_at(unsigned long long i)
{
    return (unsigned char)((i * 131u + i / 251u) & 0xffu);
}

static void writer(void *arg)
{
    static unsigned char piece[70000];
    char full[1100];
    unsigned long long at = 0;
    unsigned seed = 12345u;
    FILE *f;
    (void)arg;
    wgf_core_priv_fs_resolve(partial, full, sizeof(full));
    f = fopen(full, "wb");
    while (f != NULL && at < TOTAL) {
        size_t size;
        seed = seed * 1103515245u + 12345u;
        size = 1 + (seed >> 8) % sizeof(piece); /* 1 byte to most of a piece */
        if (size > TOTAL - at) size = (size_t)(TOTAL - at);
        for (size_t i = 0; i < size; i++) piece[i] = byte_at(at + i);
        if (fwrite(piece, 1, size, f) != size) break;
        fflush(f); /* seen by a reader now: the append the rule allows */
        at += size;
        if ((seed >> 4) % 8 == 0) wgf_core_priv_os_sleep(0.001);
    }
    if (f != NULL) fclose(f);
    wgf_core_priv_mutex_lock(&mutex);
    written_all = 1;
    write_failed = f == NULL || at != TOTAL;
    wgf_core_priv_mutex_unlock(&mutex);
}

/* Read on from `*at` in `path` while there is more, checking each byte; false on a
 * wrong byte. */
static int read_on(const char *path, unsigned long long *at, long long *got_last)
{
    static unsigned char buffer[50000];
    long long got;
    while ((got = wgf_core_priv_fs_read_at(path, *at, buffer, sizeof(buffer))) > 0) {
        for (long long i = 0; i < got; i++) {
            if (buffer[i] != byte_at(*at + (unsigned long long)i)) return 0;
        }
        *at += (unsigned long long)got;
    }
    *got_last = got;
    return 1;
}

int main(void)
{
    wgf_core_priv_thread_t thread;
    unsigned long long at = 0;
    long long got = 0;
    int reads = 0, done = 0, ok = 1;
    unsigned char byte;

    wgf_core_priv_fs_init("fs_partial_test_root");
    wgf_core_priv_fs_remove("music/long.ogg");
    expect(wgf_core_priv_fs_partial_path("music/long.ogg", partial, sizeof(partial)), "a partial path");
    wgf_core_priv_fs_make_parents(partial);
    wgf_core_priv_fs_make_parents("music/long.ogg");
    wgf_core_priv_fs_remove(partial);
    expect(wgf_core_priv_fs_read_at(partial, 0, &byte, 1) == -1, "no file yet: -1");
    if (!wgf_core_priv_thread_is_available()) {
        printf("no threads: skipped\n");
        return failures == 0 ? 0 : 1;
    }
    wgf_core_priv_mutex_init(&mutex);
    expect(wgf_core_priv_thread_create(&thread, writer, NULL), "the writer starts");
    while (ok && !done) {
        int finished;
        wgf_core_priv_mutex_lock(&mutex);
        finished = written_all;
        wgf_core_priv_mutex_unlock(&mutex);
        ok = read_on(partial, &at, &got); /* after `finished` was seen: all of it, then */
        reads++;
        done = finished;
        if (!done) wgf_core_priv_os_sleep(0.0005);
    }
    wgf_core_priv_thread_join(&thread);
    expect(ok, "every byte read while it arrived is the file's own");
    expect(!write_failed, "the writer wrote it all");
    expect(at == TOTAL, "all of it read from .part/ once its writer closed it");
    printf("%d reads while it arrived\n", reads);

    /* moved into place on the reading thread; the reader goes on under the final path */
    expect(wgf_core_priv_fs_replace(partial, "music/long.ogg"), "moved into place");
    expect(wgf_core_priv_fs_read_at(partial, at, &byte, 1) == -1, "gone from .part/");
    expect(read_on("music/long.ogg", &at, &got) && got == 0 && at == TOTAL, "nothing more under the final path");
    at = TOTAL - 5;
    expect(read_on("music/long.ogg", &at, &got) && at == TOTAL, "the last bytes at their offset");
    expect(wgf_core_priv_fs_read_at("music/long.ogg", TOTAL + 10ull, &byte, 1) == 0, "past the end: 0");

    wgf_core_priv_mutex_destroy(&mutex);
    expect(wgf_core_priv_fs_remove("music/long.ogg"), "tidy");
    wgf_core_priv_fs_deinit();
    return failures == 0 ? 0 : 1;
}
