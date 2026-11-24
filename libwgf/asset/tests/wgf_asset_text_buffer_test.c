#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_asset.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"
#include "wgf_handle.h"
#include "wgf_log.h"
#include "wgf_resource.h"
#include "wgf_text_buffer.h"

/* Text buffers natively: made in code, set, added to, and read; loaded from files under
 * text_buffer_test_root/, loaded again in place, and failing. */

static int failures;

#define CHECK(x)                                                                                                     \
    do {                                                                                                             \
        if (!(x)) {                                                                                                  \
            printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #x);                                                    \
            failures++;                                                                                              \
        }                                                                                                            \
    } while (0)

static void write_text(const char *path, const char *text)
{
    CHECK(wgf_core_priv_fs_write(path, (const unsigned char *)text, (int)strlen(text)));
}

/* Until nothing is loading: reads run on workers, so updates alone don't wait. */
static void settle(void)
{
    for (int i = 0; i < 1000000 && wgf_core_priv_load_get_pending_count() > 0; i++) wgf_core_priv_update();
}

static void test_made(void)
{
    wgf_text_buffer_t chat, empty;
    int version;
    char *big;

    CHECK(strcmp(wgf_text_buffer_get_text(0), "") == 0 && wgf_text_buffer_get_length(0) == 0 &&
          wgf_text_buffer_get_version(0) == 0 && !wgf_text_buffer_set_text(0, "x") && !wgf_text_buffer_append(0, "x"));

    chat = wgf_text_buffer_create_text("hello");
    CHECK(chat != 0 && strcmp(wgf_handle_get_kind_name(chat), "asset.text_buffer") == 0);
    CHECK(wgf_resource_get_status(chat) == WGF_RESOURCE_STATUS_READY);
    CHECK(strcmp(wgf_text_buffer_get_text(chat), "hello") == 0 && wgf_text_buffer_get_length(chat) == 5);
    version = wgf_text_buffer_get_version(chat);
    CHECK(version != 0);

    CHECK(wgf_text_buffer_append(chat, ", world") && strcmp(wgf_text_buffer_get_text(chat), "hello, world") == 0 &&
          wgf_text_buffer_get_length(chat) == 12 && wgf_text_buffer_get_version(chat) != version);
    version = wgf_text_buffer_get_version(chat);
    CHECK(wgf_text_buffer_append(chat, "") && wgf_text_buffer_append(chat, NULL) &&
          wgf_text_buffer_get_version(chat) == version); /* nothing added: no change */

    /* a chat's history: many lines added, the text grown */
    for (int i = 0; i < 1000; i++) CHECK(wgf_text_buffer_append(chat, "\nplayer_42: a line of the chat"));
    CHECK(wgf_text_buffer_get_length(chat) == 12 + 1000 * 30);
    CHECK(strncmp(wgf_text_buffer_get_text(chat) + wgf_text_buffer_get_length(chat) - 7, "he chat", 7) == 0);

    /* its own text, added and set */
    CHECK(wgf_text_buffer_set_text(chat, "ab") && wgf_text_buffer_append(chat, wgf_text_buffer_get_text(chat)) &&
          strcmp(wgf_text_buffer_get_text(chat), "abab") == 0);
    CHECK(wgf_text_buffer_set_text(chat, wgf_text_buffer_get_text(chat) + 1) &&
          strcmp(wgf_text_buffer_get_text(chat), "bab") == 0);
    CHECK(wgf_text_buffer_set_text(chat, NULL) && strcmp(wgf_text_buffer_get_text(chat), "") == 0 &&
          wgf_text_buffer_get_length(chat) == 0);

    /* over 16 MiB: refused, nothing changed */
    big = (char *)malloc(16 * 1024 * 1024 + 2);
    CHECK(big != NULL);
    if (big != NULL) {
        memset(big, 'x', 16 * 1024 * 1024 + 1);
        big[16 * 1024 * 1024 + 1] = '\0';
        CHECK(wgf_text_buffer_set_text(chat, "kept") && !wgf_text_buffer_set_text(chat, big) &&
              strcmp(wgf_text_buffer_get_text(chat), "kept") == 0);
        CHECK(wgf_text_buffer_create_text(big) == 0);
        big[16 * 1024 * 1024 - 4] = '\0'; /* "kept" and this: the most, exactly */
        CHECK(wgf_text_buffer_append(chat, big) && wgf_text_buffer_get_length(chat) == 16 * 1024 * 1024);
        CHECK(!wgf_text_buffer_append(chat, "xyz") && wgf_text_buffer_get_length(chat) == 16 * 1024 * 1024);
        free(big);
    }

    empty = wgf_text_buffer_create_text(NULL);
    CHECK(empty != 0 && strcmp(wgf_text_buffer_get_text(empty), "") == 0 && wgf_text_buffer_get_version(empty) != 0);

    CHECK(wgf_resource_release(chat) && !wgf_handle_is_alive(chat));
    CHECK(strcmp(wgf_text_buffer_get_text(chat), "") == 0 && !wgf_text_buffer_append(chat, "x"));
    wgf_resource_release(empty);
}

static void test_loaded(void)
{
    wgf_text_buffer_t credits, again, bom, missing;
    int version;
    write_text("text/credits.txt", "Made by Rob.\nThanks to everyone.");
    write_text("text/bom.txt", "\xEF\xBB\xBFwith a mark");

    credits = wgf_text_buffer_create("text/credits.txt");
    CHECK(credits != 0 && strcmp(wgf_text_buffer_get_text(credits), "") == 0); /* PENDING: "" */
    CHECK(!wgf_text_buffer_append(credits, "x"));                               /* not READY: refused */
    bom = wgf_text_buffer_create("text/bom.txt");
    missing = wgf_text_buffer_create("text/none.txt");
    settle();
    CHECK(wgf_resource_get_status(credits) == WGF_RESOURCE_STATUS_READY &&
          strcmp(wgf_text_buffer_get_text(credits), "Made by Rob.\nThanks to everyone.") == 0);
    CHECK(strcmp(wgf_text_buffer_get_text(bom), "with a mark") == 0);
    CHECK(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED &&
          strcmp(wgf_text_buffer_get_text(missing), "") == 0 && wgf_text_buffer_get_version(missing) == 0);

    /* the same path, the same buffer: a change seen through both */
    again = wgf_text_buffer_create("text/credits.txt");
    CHECK(again == credits && wgf_text_buffer_append(again, "\nThe end.") &&
          strcmp(wgf_text_buffer_get_text(credits) + wgf_text_buffer_get_length(credits) - 8, "The end.") == 0);

    /* loaded again in place: the file's text, the change gone, a new version */
    version = wgf_text_buffer_get_version(credits);
    write_text("text/credits.txt", "Made by Rob, again.");
    CHECK(!wgf_asset_is_reload_enabled() && !wgf_asset_can_reload("text/credits.txt") &&
          wgf_asset_reload("text/credits.txt") == 0); /* not enabled: nothing loads again */
    CHECK(wgf_asset_enable_reload() && wgf_asset_is_reload_enabled());
    CHECK(wgf_asset_can_reload("text/credits.txt"));
    CHECK(!wgf_asset_can_reload("text/unloaded.txt") && !wgf_asset_can_reload("../outside.txt") &&
          !wgf_asset_can_reload(NULL)); /* nothing made from it, not under the host, no path */
    CHECK(wgf_asset_reload("text/credits.txt") == 1);
    settle();
    CHECK(wgf_resource_get_status(credits) == WGF_RESOURCE_STATUS_READY &&
          strcmp(wgf_text_buffer_get_text(credits), "Made by Rob, again.") == 0 &&
          wgf_text_buffer_get_version(credits) != version);

    wgf_resource_release(again);
    CHECK(wgf_handle_is_alive(credits)); /* one reference left */
    wgf_resource_release(credits);
    CHECK(!wgf_handle_is_alive(credits));
    wgf_resource_release(bom);
    wgf_resource_release(missing);
}

int main(void)
{
    wgf_core_priv_init();
    wgf_core_priv_fs_set_root("text_buffer_test_root");
    test_made();
    test_loaded();
    wgf_core_priv_os_remove_tree("text_buffer_test_root");
    wgf_core_priv_shutdown();
    printf("wgf_asset_text_buffer_test: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
