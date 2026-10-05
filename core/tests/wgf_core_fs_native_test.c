#include <stdio.h>
#include <string.h>

#include "wgf_core_fs_priv.h"

/* fs's private side as it is natively: a cached file's metadata is a sidecar
 * under the root's .meta/, removed with its directory, and nothing is ever
 * "cached" to read in later. Native only (CMake builds it there). Files land
 * under fs_native_test_root/ and fs_native_test_cache/ in the working dir. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    wgf_core_priv_fs_meta_t meta, got;

    wgf_core_priv_fs_init("fs_native_test_root");
    wgf_core_priv_fs_set_cache_root("fs_native_test_cache");
    expect(wgf_core_priv_fs_write("a/b.txt", (const unsigned char *)"hello", 5), "a plain file");

    /* metadata rides with the bytes under the cache root */
    memset(&meta, 0, sizeof(meta));
    strcpy(meta.etag, "\"v1\"");
    meta.fresh_until = 123.5;
    strcpy(meta.hash, "sha256:ab");
    expect(wgf_core_priv_fs_write_meta("cache:x/y.bin", (const unsigned char *)"xy", 2, &meta), "write with meta");
    expect(wgf_core_priv_fs_meta_get("cache:x/y.bin", &got) && strcmp(got.etag, "\"v1\"") == 0 &&
               got.fresh_until == 123.5 && strcmp(got.hash, "sha256:ab") == 0,
           "meta read back");
    expect(!wgf_core_priv_fs_meta_get("a/b.txt", &got), "a file without meta has none");
    strcpy(meta.etag, "\"v2\"");
    expect(wgf_core_priv_fs_meta_set("cache:x/y.bin", &meta), "meta replaced");
    expect(wgf_core_priv_fs_meta_get("cache:x/y.bin", &got) && strcmp(got.etag, "\"v2\"") == 0, "new meta read back");
    expect(!wgf_core_priv_fs_meta_set("cache:nothing", &meta), "meta of a missing file is refused");
    expect(wgf_core_priv_fs_write("cache:x/y.bin", (const unsigned char *)"z", 1), "plain write over it");
    expect(!wgf_core_priv_fs_meta_get("cache:x/y.bin", &got), "a plain write drops the old meta");

    /* a removed cache directory takes its metadata sidecars with it */
    expect(wgf_core_priv_fs_write_meta("cache:d/e.bin", (const unsigned char *)"e", 1, &meta), "write with meta in d/");
    expect(wgf_core_priv_fs_is_dir("cache:d") && wgf_core_priv_fs_is_dir("cache:.meta/d"), "d/ and its sidecar dir");
    expect(wgf_core_priv_fs_rmdir("cache:d"), "rmdir d/");
    expect(!wgf_core_priv_fs_is_dir("cache:d") && !wgf_core_priv_fs_is_dir("cache:.meta/d"), "both gone");
    expect(!wgf_core_priv_fs_rmdir("cache:d"), "rmdir again fails");
    /* natively nothing is ever "cached" */
    expect(!wgf_core_priv_fs_is_cached("cache:x/y.bin"), "not cached");
    expect(wgf_core_priv_fs_cache_read_begin("cache:x/y.bin") == 0, "no cache read to begin");
    expect(wgf_core_priv_fs_cache_read_poll(0) == -1, "polling nothing fails");

    expect(wgf_core_priv_fs_remove("a/b.txt"), "tidy");
    expect(wgf_core_priv_fs_remove("cache:x/y.bin"), "tidy the cached file");
    wgf_core_priv_fs_deinit();
    return failures == 0 ? 0 : 1;
}
