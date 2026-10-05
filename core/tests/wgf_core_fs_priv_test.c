#include <stdio.h>
#include <string.h>

#include "wgf_core_fs_priv.h"

/* The private side of fs that the layers above core use, as every platform has it:
 * path normalizing, the cache prefix, partial downloads moved into place,
 * directories. Metadata is per platform: wgf_core_fs_native_test.c. Files land
 * under fs_priv_test_root/ and fs_priv_test_cache/ in the working dir. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void check_normalize(const char *path, const char *expected)
{
    char out[64];
    const bool ok = wgf_core_priv_fs_normalize_path(path, out, sizeof(out));
    if (ok != (expected != NULL) || (ok && strcmp(out, expected) != 0)) {
        printf("FAIL: normalize(%s): got %s, expected %s\n", path ? path : "NULL", ok ? out : "(refused)",
               expected ? expected : "(refused)");
        failures++;
    }
}

/* What a caller may name: paths under the root. wgrender's cases, plus control
 * characters. */
static void test_normalize(void)
{
    check_normalize("textures/rock.png", "textures/rock.png");
    check_normalize("./textures//rock.png", "textures/rock.png");
    check_normalize("textures\\rock.png", "textures/rock.png"); /* Windows separators */
    check_normalize("textures/../models/box.glb", "models/box.glb"); /* ".." within the root */
    check_normalize("a/b/../../c", "c");
    check_normalize("textures/", "textures");

    check_normalize("/etc/passwd", NULL); /* absolute */
    check_normalize("\\server\\share", NULL);
    check_normalize("C:/Windows/win.ini", NULL); /* a drive */
    check_normalize("C:foo", NULL);
    check_normalize("textures/a:b.png", NULL); /* any ":" */
    check_normalize("cache:x", NULL);          /* the private cache prefix */
    check_normalize("../secret", NULL);        /* above the root */
    check_normalize("textures/../../secret", NULL);
    check_normalize("a\\..\\..\\secret", NULL);
    check_normalize("", NULL); /* names nothing */
    check_normalize(".", NULL);
    check_normalize("a/..", NULL);
    check_normalize("a\nb", NULL); /* a control character */
    check_normalize("0123456789/0123456789/0123456789/0123456789/0123456789/0123456789", NULL); /* too long */
    check_normalize(NULL, NULL);

    /* the program's own files: the prefix kept, the rest under its root */
    check_normalize("user:saves/high.txt", "user:saves/high.txt");
    check_normalize("user:./saves\\..\\settings.txt", "user:settings.txt");
    check_normalize("user:", NULL);           /* names nothing */
    check_normalize("user:../escape", NULL);  /* above its root */
    check_normalize("user:/etc/passwd", NULL); /* absolute under it */
    check_normalize("user:a:b", NULL);        /* a second ":" */
    check_normalize("saves/user:x", NULL);    /* the prefix only leads */
}

int main(void)
{
    unsigned char *data;
    int size;
    char partial[256];

    test_normalize();

    wgf_core_priv_fs_init("fs_priv_test_root/");
    expect(strcmp(wgf_core_priv_fs_root(), "fs_priv_test_root") == 0, "init trims the root");
    wgf_core_priv_fs_set_cache_root("fs_priv_test_cache");
    expect(wgf_core_priv_fs_is_ready(), "ready");

    expect(!wgf_core_priv_fs_exists("a/b.txt"), "nothing there yet");
    expect(wgf_core_priv_fs_write("a/b.txt", (const unsigned char *)"hello", 5), "write makes parents");
    expect(wgf_core_priv_fs_exists("a/b.txt"), "then it exists");
    expect(wgf_core_priv_fs_read("a/b.txt", &data, &size) && size == 5 && memcmp(data, "hello", 6) == 0,
           "read back, NUL-terminated");
    wgf_core_priv_fs_read_free(data);

    /* a download lands under .part/ and is moved into place whole */
    expect(wgf_core_priv_fs_write("cache:x/y.bin", (const unsigned char *)"z", 1), "write under the cache root");
    expect(wgf_core_priv_fs_partial_path("cache:x/y.bin", partial, sizeof(partial)) &&
               strcmp(partial, "cache:.part/x/y.bin") == 0,
           "partial path");
    wgf_core_priv_fs_make_parents(partial);
    expect(wgf_core_priv_fs_write(partial, (const unsigned char *)"new", 3), "write the partial");
    expect(wgf_core_priv_fs_replace(partial, "cache:x/y.bin"), "replace");
    expect(wgf_core_priv_fs_read("cache:x/y.bin", &data, &size) && size == 3 && memcmp(data, "new", 3) == 0,
           "the replacement is read");
    wgf_core_priv_fs_read_free(data);
    expect(!wgf_core_priv_fs_exists(partial), "the partial is gone");

    expect(wgf_core_priv_fs_remove("a/b.txt") && !wgf_core_priv_fs_exists("a/b.txt"), "remove");
    expect(!wgf_core_priv_fs_remove("a/b.txt"), "remove again fails");
    expect(wgf_core_priv_fs_remove("cache:x/y.bin"), "remove the cached file");

    expect(wgf_core_priv_fs_mkdir("cache:d/e") && wgf_core_priv_fs_is_dir("cache:d/e"), "mkdir under the cache root");
    expect(wgf_core_priv_fs_rmdir("cache:d") && !wgf_core_priv_fs_is_dir("cache:d"), "rmdir under the cache root");
    expect(!wgf_core_priv_fs_rmdir("cache:d"), "rmdir again fails");

    expect(wgf_core_priv_fs_mkdir("m/n") && wgf_core_priv_fs_is_dir("m/n") && wgf_core_priv_fs_rmdir("m"), "mkdir, rmdir");

    wgf_core_priv_fs_deinit();
    return failures == 0 ? 0 : 1;
}
