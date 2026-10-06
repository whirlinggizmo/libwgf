#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_asset.h"
#include "wgf_asset_priv.h"
#include "wgf_core_fs_priv.h"
#include "wgf_core_load_priv.h"
#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"
#include "wgf_core_resource_priv.h"
#include "wgf_resource.h"

/* The asset part natively (the web's: tools/check_asset_cache.py): wgrender's asset unit
 * test's cases for freshness, paths, and reading a source against a host, ported; then
 * ensures, groups, pings, redirects, and loads through core's locate hook, files on disk
 * under asset_test_root/. */

static int failures;

#define CHECK(x)                                                                                                     \
    do {                                                                                                             \
        if (!(x)) {                                                                                                  \
            printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #x);                                                    \
            failures++;                                                                                              \
        }                                                                                                            \
    } while (0)

static void test_freshness(void)
{
    const double now = 1790000000.0;
    const double year = 365.0 * 24.0 * 3600.0;

    /* nothing said, nothing fresh: every visit asks */
    CHECK(wgf_asset_priv_fresh_until(NULL, NULL, now) == 0.0);
    CHECK(wgf_asset_priv_fresh_until("", "", now) == 0.0);
    CHECK(wgf_asset_priv_fresh_until("public", NULL, now) == 0.0);

    /* max-age from now, less what a shared cache already held it for */
    CHECK(wgf_asset_priv_fresh_until("max-age=600", NULL, now) == now + 600.0);
    CHECK(wgf_asset_priv_fresh_until("public, max-age=600", "100", now) == now + 500.0);
    CHECK(wgf_asset_priv_fresh_until("max-age=600", "600", now) == 0.0);
    CHECK(wgf_asset_priv_fresh_until("max-age=600", "9000", now) == 0.0);
    CHECK(wgf_asset_priv_fresh_until("max-age=0", NULL, now) == 0.0);
    CHECK(wgf_asset_priv_fresh_until("Max-Age=60", NULL, now) == now + 60.0); /* directives ignore case */
    CHECK(wgf_asset_priv_fresh_until("max-age=600", "junk", now) == now + 600.0);

    /* a max-age that isn't a number is none */
    CHECK(wgf_asset_priv_fresh_until("max-age=soon", NULL, now) == 0.0);
    CHECK(wgf_asset_priv_fresh_until("max-age=-5", NULL, now) == 0.0);
    CHECK(wgf_asset_priv_fresh_until("max-age=\"600\"", NULL, now) == 0.0);

    /* immutable: max-age still says how long; without one, a year */
    CHECK(wgf_asset_priv_fresh_until("public, max-age=31536000, immutable", NULL, now) == now + 31536000.0);
    CHECK(wgf_asset_priv_fresh_until("immutable", NULL, now) == now + year);

    /* no-cache and no-store win wherever they are, field-specific no-cache included */
    CHECK(wgf_asset_priv_fresh_until("max-age=600, no-cache", NULL, now) == 0.0);
    CHECK(wgf_asset_priv_fresh_until("no-store, max-age=600", NULL, now) == 0.0);
    CHECK(wgf_asset_priv_fresh_until("immutable,no-cache=\"Set-Cookie\"", NULL, now) == 0.0);
    CHECK(wgf_asset_priv_fresh_until("no-cacheable, max-age=60", NULL, now) == now + 60.0); /* not no-cache */
    CHECK(wgf_asset_priv_fresh_until("  max-age=60  ,  must-revalidate ", NULL, now) == now + 60.0);
}

static void check_normalize(const char *path, const char *expected)
{
    char out[64];
    const bool ok = wgf_asset_priv_normalize_path(path, out, sizeof(out));
    CHECK(ok == (expected != NULL));
    if (ok && expected != NULL && strcmp(out, expected) != 0) {
        fprintf(stderr, "    normalize(%s): got %s, expected %s\n", path, out, expected);
        failures++;
    }
}

static void test_paths(void)
{
    check_normalize("textures/rock.png", "textures/rock.png");
    check_normalize("./textures//rock.png", "textures/rock.png");
    check_normalize("textures\\rock.png", "textures/rock.png");         /* Windows separators */
    check_normalize("textures/../models/box.glb", "models/box.glb");    /* ".." within the root */
    check_normalize("a/b/../../c", "c");
    check_normalize("textures/", "textures");

    check_normalize("/etc/passwd", NULL);           /* absolute */
    check_normalize("\\server\\share", NULL);
    check_normalize("C:/Windows/win.ini", NULL);     /* a drive */
    check_normalize("C:foo", NULL);
    check_normalize("textures/a:b.png", NULL);       /* any ":" */
    check_normalize("../secret", NULL);              /* above the root */
    check_normalize("textures/../../secret", NULL);
    check_normalize("", NULL);                       /* names nothing */
    check_normalize(".", NULL);
    check_normalize("a/..", NULL);
    check_normalize("0123456789/0123456789/0123456789/0123456789/0123456789/0123456789", NULL); /* too long */
    check_normalize(NULL, NULL);

}

static void check_source(const char *host, wgf_asset_priv_host_kind_t kind, const char *ref, wgf_asset_priv_source_t want,
                         const char *expected)
{
    char out[512];
    const wgf_asset_priv_source_t got = wgf_asset_priv_resolve_source(host, kind, ref, out, sizeof(out));
    CHECK(got == want);
    if (got != want || (expected != NULL && strcmp(out, expected) != 0)) {
        fprintf(stderr, "    resolve(%s, %s): got %d \"%s\", expected %d \"%s\"\n", host, ref, (int)got, out, (int)want,
                expected != NULL ? expected : "");
        if (got == want) failures++;
    }
}

/* A fetch_url is read against the host as a browser reads a URL against a directory;
 * the URL cases' expectations are what the WHATWG URL parser gives (node's URL). */
static void test_resolve_source(void)
{
    const char *cdn = "https://cdn.example.com/game";
    const wgf_asset_priv_source_t url = WGF_ASSET_PRIV_SOURCE_URL, local = WGF_ASSET_PRIV_SOURCE_LOCAL, refused = WGF_ASSET_PRIV_SOURCE_REFUSED;

    /* a URL host, as the browser resolves against it */
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "music/a.mp3", url, "https://cdn.example.com/game/music/a.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "../shared/a.mp3", url, "https://cdn.example.com/shared/a.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "../../../a.mp3", url, "https://cdn.example.com/a.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "/other/a.mp3", url, "https://cdn.example.com/other/a.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "//mirror.example.net/a.mp3", url, "https://mirror.example.net/a.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "music/a.mp3?sig=1#t", url, "https://cdn.example.com/game/music/a.mp3?sig=1#t");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "?v=2", url, "https://cdn.example.com/game/?v=2");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "./music/./x/../a.mp3", url, "https://cdn.example.com/game/music/a.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "music\\a.mp3", url, "https://cdn.example.com/game/music/a.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "..\\shared\\a.mp3", url, "https://cdn.example.com/shared/a.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "music/", url, "https://cdn.example.com/game/music/");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "music/..", url, "https://cdn.example.com/game/");
    check_source("https://cdn.example.com", WGF_ASSET_PRIV_HOST_URL, "music/a.mp3", url, "https://cdn.example.com/music/a.mp3");
    check_source("http://localhost:8000/assets", WGF_ASSET_PRIV_HOST_URL, "music/a.mp3", url,
                 "http://localhost:8000/assets/music/a.mp3"); /* a port is part of the origin */
    check_source("http://localhost:8000/assets", WGF_ASSET_PRIV_HOST_URL, "../x/a.mp3", url, "http://localhost:8000/x/a.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "https://other.example.org/a.mp3", url, "https://other.example.org/a.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "HTTPS://other.example.org/a.mp3", url, "HTTPS://other.example.org/a.mp3");

    /* on desktop an absolute source is http or https: nothing handed over names a local file */
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "file:///etc/passwd", refused, NULL);
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "data:application/octet-stream,AAAA", refused, NULL);
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "C:/Windows/win.ini", refused, NULL); /* "c:" is a scheme */

    /* the web's relative host: relative out, and the page finishes it as it would have
       (page /game/index.html: assets/music/a.mp3, /game/x.mp3, /x.mp3, ...) */
    check_source("assets", WGF_ASSET_PRIV_HOST_BROWSER, "music/a.mp3", url, "assets/music/a.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_BROWSER, "../x.mp3", url, "x.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_BROWSER, "../../x.mp3", url, "../x.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_BROWSER, "/root.mp3", url, "/root.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_BROWSER, "//cdn.example.com/a.mp3", url, "//cdn.example.com/a.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_BROWSER, "?q=1", url, "assets/?q=1");
    check_source("", WGF_ASSET_PRIV_HOST_BROWSER, "music/a.mp3", url, "music/a.mp3");
    check_source("/static/assets", WGF_ASSET_PRIV_HOST_BROWSER, "../../../x.mp3", url, "/x.mp3");
    check_source(cdn, WGF_ASSET_PRIV_HOST_BROWSER, "music/a.mp3", url, "https://cdn.example.com/game/music/a.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_BROWSER, "data:application/octet-stream,AAAA", url,
                 "data:application/octet-stream,AAAA"); /* the browser's business */

    /* a local host: a path under it, held to a key's rules, read where it is */
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "music/a.mp3", local, "music/a.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "music/a.mp3?sig=1#t", local, "music/a.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "music/my%20song.mp3", local, "music/my song.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "music/../shared/a.mp3", local, "shared/a.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "music\\a.mp3", local, "music/a.mp3");
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "../a.mp3", refused, NULL);
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "..\\a.mp3", refused, NULL);
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "%2e%2e/a.mp3", refused, NULL);
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "..%5Ca.mp3", refused, NULL);
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "/etc/passwd", refused, NULL);
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "//server/share/a.mp3", refused, NULL);
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "file:///etc/passwd", refused, NULL);
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "a%00.mp3", refused, NULL);
    check_source("assets", WGF_ASSET_PRIV_HOST_LOCAL, "https://cdn.example.com/a.mp3", url,
                 "https://cdn.example.com/a.mp3"); /* a download, for the fetcher */

    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, "", refused, NULL);
    check_source(cdn, WGF_ASSET_PRIV_HOST_URL, NULL, refused, NULL);

    char dir[256];
    CHECK(wgf_asset_priv_file_url_path("file:///opt/game/assets", dir, sizeof(dir)) && strcmp(dir, "/opt/game/assets") == 0);
    CHECK(wgf_asset_priv_file_url_path("file://localhost/opt/game", dir, sizeof(dir)) && strcmp(dir, "/opt/game") == 0);
    CHECK(wgf_asset_priv_file_url_path("FILE:///opt/my%20game", dir, sizeof(dir)) && strcmp(dir, "/opt/my game") == 0);
#if defined(_WIN32)
    CHECK(wgf_asset_priv_file_url_path("file:///C:/games/assets", dir, sizeof(dir)) && strcmp(dir, "C:/games/assets") == 0);
#endif
    CHECK(!wgf_asset_priv_file_url_path("file://server/share", dir, sizeof(dir))); /* another machine's */
    CHECK(!wgf_asset_priv_file_url_path("https://cdn.example.com/game", dir, sizeof(dir)));
    CHECK(!wgf_asset_priv_file_url_path("file:///opt/a%00b", dir, sizeof(dir)));
}

/* wgrender's manifest test's cases, ported: the hash, and the reader. */
static bool hash_is(const char *text, size_t size, const char *expected)
{
    char out[WGF_ASSET_PRIV_SHA256_TEXT];
    wgf_asset_priv_sha256_text((const unsigned char *)text, size, out);
    return strcmp(out + 7, expected) == 0 && strncmp(out, "sha256:", 7) == 0;
}

static void test_sha256(void)
{
    /* FIPS 180-4's examples, and the lengths either side of a second padding block */
    CHECK(hash_is("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(hash_is("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(hash_is("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK(hash_is("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
                  112, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
    CHECK(hash_is("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 55,
                  "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
    CHECK(hash_is("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 64,
                  "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));

    /* a million a's: many blocks */
    char *million = malloc(1000000);
    CHECK(million != NULL);
    if (million != NULL) {
        memset(million, 'a', 1000000);
        CHECK(hash_is(million, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
        free(million);
    }
}

#define HA "sha256:9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"
#define HB "sha256:3a7bd3e2360a3d29eea436fcfb7e44c735d117c42d1c1835420b6b9942dd4f1b"

static bool parses(const char *json)
{
    wgf_asset_priv_manifest_t m;
    const bool ok = wgf_asset_priv_manifest_parse(json, strlen(json), &m);
    wgf_asset_priv_manifest_free(&m);
    return ok;
}

/* Fed in pieces of every size up to a block and a half, the same hash as all at once. */
static void test_sha256_pieces(void)
{
    static unsigned char data[1000];
    char whole[WGF_ASSET_PRIV_SHA256_TEXT], pieces[WGF_ASSET_PRIV_SHA256_TEXT];
    for (size_t i = 0; i < sizeof(data); i++) data[i] = (unsigned char)(i * 7u + 3u);
    wgf_asset_priv_sha256_text(data, sizeof(data), whole);
    for (size_t step = 1; step <= 96; step++) {
        wgf_asset_priv_sha256_t state;
        wgf_asset_priv_sha256_begin(&state);
        for (size_t at = 0; at < sizeof(data); at += step) {
            wgf_asset_priv_sha256_feed(&state, data + at, at + step <= sizeof(data) ? step : sizeof(data) - at);
        }
        wgf_asset_priv_sha256_end(&state, pieces);
        CHECK(strcmp(whole, pieces) == 0);
    }
}

static void test_manifest_parse(void)
{
    wgf_asset_priv_manifest_t m;
    const char *json = "{\n  \"wgf_manifest\": 1,\n"
                       "  \"files\": { \"tiles.png\": \"" HA "\", \"a \\\"quoted\\\" \\u00e9 name\": \"" HB "\",\n"
                       "             \"textures\": \"" HB "\" },\n"
                       "  \"dirs\": { \"textures\": \"" HA "\" },\n"
                       "  \"generator\": { \"by\": [\"gen_manifest.py\", 1.5e3, true, null] }\n}\n";

    CHECK(wgf_asset_priv_manifest_parse(json, strlen(json), &m));
    CHECK(m.count == 4);
    CHECK(wgf_asset_priv_manifest_find(&m, "tiles.png", false) != NULL &&
          strcmp(wgf_asset_priv_manifest_find(&m, "tiles.png", false), HA) == 0);
    CHECK(wgf_asset_priv_manifest_find(&m, "a \"quoted\" \xc3\xa9 name", false) != NULL); /* escapes decoded */
    /* a file and a directory may share a name; each is found as what it is */
    CHECK(strcmp(wgf_asset_priv_manifest_find(&m, "textures", false), HB) == 0);
    CHECK(strcmp(wgf_asset_priv_manifest_find(&m, "textures", true), HA) == 0);
    CHECK(wgf_asset_priv_manifest_find(&m, "tiles.png", true) == NULL);
    CHECK(wgf_asset_priv_manifest_find(&m, "missing.png", false) == NULL);
    wgf_asset_priv_manifest_free(&m);
    CHECK(m.entries == NULL && m.count == 0);

    /* the smallest one, and an empty one */
    CHECK(parses("{\"wgf_manifest\":1}"));
    CHECK(parses("{\"wgf_manifest\":1,\"files\":{},\"dirs\":{}}"));
    CHECK(parses("{\"wgf_manifest\":1,\"files\":{\"\\ud83d\\ude00.png\":\"" HA "\"}}")); /* a surrogate pair */

    /* anything that isn't exactly a manifest is none */
    CHECK(!parses(""));
    CHECK(!parses("[]"));
    CHECK(!parses("{}"));                                       /* no version */
    CHECK(!parses("{\"wgf_manifest\":2}"));                      /* another version */
    CHECK(!parses("{\"wgf_manifest\":1.0}"));
    CHECK(!parses("{\"wgf_manifest\":\"1\"}"));
    CHECK(!parses("{\"wgf_manifest\":1"));                       /* cut short */
    CHECK(!parses("{\"wgf_manifest\":1}{}"));                    /* something after it */
    CHECK(!parses("{\"wgf_manifest\":1,}"));                     /* a trailing comma */
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"a\":\"sha256:abc\"}}"));   /* a short hash */
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"a\":\"" "SHA256:9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08" "\"}}"));
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"a\":\"sha256:9F86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\"}}"));
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"a/b\":\"" HA "\"}}"));     /* not a name */
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"..\":\"" HA "\"}}"));
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"\":\"" HA "\"}}"));
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"a\\u0000\":\"" HA "\"}}"));
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"\\ud83d.png\":\"" HA "\"}}")); /* half a pair */
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"a\\q\":\"" HA "\"}}"));        /* no such escape */
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"a\":\"" HA "\",\"a\":\"" HB "\"}}")); /* twice */
    CHECK(!parses("{\"wgf_manifest\":1,\"files\":{\"a\":1}}"));
    CHECK(!parses("{\"wgf_manifest\":1,\"other\":[1,2}"));        /* a broken unknown member */
    CHECK(!parses("{\"wgf_manifest\":1,\"other\":01}"));
}

/* ------------------------------------------------- libwgf's own: on disk ---- */

/* A note: a file's text, as core's resource test has, made from a path through the
 * asset part. */
typedef struct note_t {
    wgf_core_priv_resource_t resource;
    char *text;
} note_t;

static wgf_core_priv_handle_pool_t note_pool;
static note_t *notes;

static void *prepare(const char *path)
{
    unsigned char *data;
    int size;
    char *text;
    if (!wgf_core_priv_fs_read(path, &data, &size)) return NULL;
    text = (char *)malloc((size_t)size + 1);
    memcpy(text, data, (size_t)size);
    text[size] = '\0';
    wgf_core_priv_fs_read_free(data);
    return text;
}

static wgf_core_priv_load_step_t finish(void *data, wgf_handle_t resource)
{
    note_t *note_ptr = (note_t *)wgf_core_priv_resource_get(resource);
    if (note_ptr == NULL) return WGF_CORE_PRIV_LOAD_FAILED;
    note_ptr->text = (char *)data;
    wgf_core_priv_resource_loaded(resource, NULL);
    return WGF_CORE_PRIV_LOAD_DONE;
}

static void discard(void *data)
{
    for (uint16_t i = 1; i < note_pool.capacity; i++) {
        if (note_pool.occupied[i] && notes[i].text == data) return; /* finished: the note's now */
    }
    free(data);
}

static void fail(wgf_handle_t resource)
{
    wgf_core_priv_resource_failed(resource);
}

static const wgf_core_priv_loader_t loader = {"note", prepare, finish, discard, fail, NULL};

static const wgf_core_priv_loader_t *loader_of(const char *path)
{
    (void)path;
    return &loader;
}

static void free_note(wgf_handle_t resource, void *record)
{
    (void)resource;
    free(((note_t *)record)->text);
}

static const wgf_core_priv_resource_kind_t note_kind = {.create = "note_create", .loader = loader_of, .free = free_note};

static void write_text(const char *path, const char *text)
{
    CHECK(wgf_core_priv_fs_write(path, (const unsigned char *)text, (int)strlen(text)));
}

static void updates(int n)
{
    for (int i = 0; i < n; i++) wgf_core_priv_update();
}

/* Until nothing is loading: prepares run on workers, so updates alone don't wait. */
static void settle(void)
{
    for (int i = 0; i < 1000000 && wgf_core_priv_load_get_pending_count() > 0; i++) wgf_core_priv_update();
}

static const char *text_of(wgf_handle_t note)
{
    const note_t *note_ptr = (const note_t *)wgf_core_priv_resource_get(note);
    return note_ptr != NULL && note_ptr->text != NULL ? note_ptr->text : "";
}

static void test_url_key(void)
{
    char a[256], b[256];
    CHECK(wgf_asset_priv_url_key("https://cdn.example.com/game/rock.png", a, sizeof(a)));
    CHECK(strncmp(a, ".url/", 5) == 0 && strlen(a) == 5 + 16 + 1 + strlen("rock.png") &&
          strcmp(a + strlen(a) - 9, "/rock.png") == 0);
    CHECK(wgf_asset_priv_url_key("https://mirror.example.net/game/rock.png", b, sizeof(b)) && strcmp(a, b) != 0);
    CHECK(wgf_asset_priv_url_key("https://cdn.example.com/my%20rock.png?v=2", b, sizeof(b)) &&
          strcmp(b + strlen(b) - strlen("/my rock.png"), "/my rock.png") == 0); /* decoded, the query dropped */
    CHECK(wgf_core_priv_fs_normalize_path(a, b, sizeof(b))); /* a key is a path under the root */
    CHECK(!wgf_asset_priv_url_key("https://cdn.example.com/", a, sizeof(a))); /* no file */
    CHECK(!wgf_asset_priv_url_key("textures/rock.png", a, sizeof(a)));       /* not a URL */
}

static void test_ensure(void)
{
    wgf_handle_t done, missing, group, ping, member;
    write_text("textures/rock.png", "rock");
    write_text("mods/hd/textures/rock.png", "hd rock");
    write_text("elsewhere/rock.png", "elsewhere");

    CHECK(wgf_asset_ensure("/etc/passwd", NULL, WGF_ASSET_ENSURE_NONE) == 0);
    CHECK(wgf_asset_ensure("../outside.png", NULL, WGF_ASSET_ENSURE_NONE) == 0);
    CHECK(wgf_asset_ensure("C:/x.png", NULL, WGF_ASSET_ENSURE_NONE) == 0);
    CHECK(wgf_asset_ensure("textures/rock.png", "../x.png", WGF_ASSET_ENSURE_NONE) == 0); /* out of a local host */
    CHECK(!wgf_asset_evict("../../etc/passwd"));
    CHECK(!wgf_asset_add_redirect("textures/", "../mods/"));
    CHECK(!wgf_asset_add_redirect("/textures/", "mods/"));
    CHECK(!wgf_asset_add_redirect("", "mods/"));

    done = wgf_asset_ensure("textures/rock.png", NULL, WGF_ASSET_ENSURE_NONE);
    missing = wgf_asset_ensure("textures/none.png", NULL, WGF_ASSET_ENSURE_NONE);
    CHECK(wgf_asset_task_get_status(done) == WGF_ASSET_TASK_STATUS_PENDING &&
          strcmp(wgf_asset_task_get_path(done), "") == 0 && wgf_asset_task_get_progress(done) == 0.0f);
    updates(1);
    CHECK(wgf_asset_task_get_status(done) == WGF_ASSET_TASK_STATUS_DONE &&
          wgf_asset_task_get_progress(done) == 1.0f);
    CHECK(strstr(wgf_asset_task_get_path(done), "asset_test_root") != NULL &&
          strstr(wgf_asset_task_get_path(done), "textures/rock.png") != NULL);
    CHECK(wgf_asset_task_get_status(missing) == WGF_ASSET_TASK_STATUS_FAILED);
    CHECK(wgf_asset_task_destroy(done) && wgf_asset_task_get_status(done) == WGF_ASSET_TASK_STATUS_NONE &&
          !wgf_asset_task_destroy(done));
    wgf_asset_task_destroy(missing);

    /* redirects: a mod over the file, then the file itself; a mod without it is no error */
    CHECK(wgf_asset_add_redirect("textures/", "mods/hd/textures/"));
    CHECK(wgf_asset_add_redirect("./textures/", "mods/sd/../none/")); /* normalized: mods/none/ */
    done = wgf_asset_ensure("textures/rock.png", NULL, WGF_ASSET_ENSURE_NONE);
    updates(3);
    CHECK(wgf_asset_task_get_status(done) == WGF_ASSET_TASK_STATUS_DONE &&
          strstr(wgf_asset_task_get_path(done), "mods/hd/textures/rock.png") != NULL);
    wgf_asset_task_destroy(done);
    wgf_asset_clear_redirects();

    /* an explicit source under a local host: read where it is, and what a create loads */
    done = wgf_asset_ensure("textures/rock.png", "elsewhere/rock.png", WGF_ASSET_ENSURE_NONE);
    updates(1);
    CHECK(wgf_asset_task_get_status(done) == WGF_ASSET_TASK_STATUS_DONE &&
          strstr(wgf_asset_task_get_path(done), "elsewhere/rock.png") != NULL);
    {
        const wgf_handle_t note = wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "textures/rock.png");
        settle();
        CHECK(wgf_resource_get_status(note) == WGF_RESOURCE_STATUS_READY && strcmp(text_of(note), "elsewhere") == 0 &&
              strcmp(wgf_resource_get_path(note), "elsewhere/rock.png") == 0);
        wgf_resource_release(note);
    }
    wgf_asset_task_destroy(done);
    /* natively nothing is downloaded yet: a URL source fails, logged */
    done = wgf_asset_ensure("textures/rock.png", "https://cdn.example.com/rock.png", WGF_ASSET_ENSURE_NONE);
    updates(1);
    CHECK(wgf_asset_task_get_status(done) == WGF_ASSET_TASK_STATUS_FAILED);
    wgf_asset_task_destroy(done);

    /* groups */
    group = wgf_asset_group_create();
    member = wgf_asset_ensure("textures/rock.png", NULL, WGF_ASSET_ENSURE_NONE);
    CHECK(wgf_asset_group_add(group, member) && !wgf_asset_group_add(group, member) && !wgf_asset_group_add(group, group));
    CHECK(wgf_asset_group_add(group, wgf_asset_ensure("elsewhere/rock.png", NULL, WGF_ASSET_ENSURE_NONE)));
    CHECK(wgf_asset_task_get_status(group) == WGF_ASSET_TASK_STATUS_PENDING &&
          wgf_asset_task_get_progress(group) == 0.0f && strcmp(wgf_asset_task_get_path(group), "") == 0);
    updates(2);
    CHECK(wgf_asset_task_get_status(group) == WGF_ASSET_TASK_STATUS_DONE &&
          wgf_asset_task_get_status(member) == WGF_ASSET_TASK_STATUS_DONE);
    CHECK(!wgf_asset_group_add(group, wgf_asset_ensure("x/y.png", NULL, 0))); /* finished */
    wgf_asset_task_destroy(group);
    CHECK(wgf_asset_task_get_status(member) == WGF_ASSET_TASK_STATUS_NONE); /* went with it */
    group = wgf_asset_group_create();
    CHECK(wgf_asset_group_add(group, wgf_asset_ensure("textures/none.png", NULL, 0)) &&
          wgf_asset_group_add(group, wgf_asset_ensure("textures/rock.png", NULL, 0)));
    updates(2);
    CHECK(wgf_asset_task_get_status(group) == WGF_ASSET_TASK_STATUS_FAILED);
    wgf_asset_task_destroy(group);
    group = wgf_asset_group_create();
    updates(1);
    CHECK(wgf_asset_task_get_status(group) == WGF_ASSET_TASK_STATUS_DONE); /* empty */
    wgf_asset_task_destroy(group);

    /* pings: a local host is there or it isn't */
    ping = wgf_asset_ping_host(NULL, 0);
    CHECK(wgf_asset_task_get_status(ping) == WGF_ASSET_TASK_STATUS_PENDING);
    updates(1);
    CHECK(wgf_asset_task_get_status(ping) == WGF_ASSET_TASK_STATUS_DONE && wgf_asset_ping_get_milliseconds(ping) == 0.0f);
    wgf_asset_task_destroy(ping);
    ping = wgf_asset_ping_host("no/such/dir", 0);
    updates(1);
    CHECK(wgf_asset_task_get_status(ping) == WGF_ASSET_TASK_STATUS_FAILED);
    wgf_asset_task_destroy(ping);
}

static void test_loads(void)
{
    wgf_handle_t note, redirected, missing, url;
    write_text("notes/a.txt", "alpha");
    write_text("lang/fr/notes/a.txt", "alpha, en francais");

    note = wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/a.txt");
    updates(1);
    CHECK(wgf_resource_get_status(note) == WGF_RESOURCE_STATUS_PENDING ||
          wgf_resource_get_status(note) == WGF_RESOURCE_STATUS_READY);
    settle();
    CHECK(wgf_resource_get_status(note) == WGF_RESOURCE_STATUS_READY && strcmp(text_of(note), "alpha") == 0 &&
          strcmp(wgf_resource_get_path(note), "notes/a.txt") == 0);
    wgf_resource_release(note);

    CHECK(wgf_asset_add_redirect("notes/", "lang/fr/notes/"));
    redirected = wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/a.txt");
    missing = wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "notes/none.txt");
    url = wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "https://cdn.example.com/notes/b.txt");
    settle();
    CHECK(wgf_resource_get_status(redirected) == WGF_RESOURCE_STATUS_READY &&
          strcmp(text_of(redirected), "alpha, en francais") == 0 &&
          strcmp(wgf_resource_get_path(redirected), "lang/fr/notes/a.txt") == 0);
    CHECK(wgf_resource_get_status(missing) == WGF_RESOURCE_STATUS_FAILED);
    CHECK(url != 0 && wgf_resource_get_status(url) == WGF_RESOURCE_STATUS_FAILED); /* natively, not yet */
    CHECK(wgf_core_priv_load_get_pending_count() == 0);
    wgf_resource_release(redirected);
    wgf_resource_release(missing);
    wgf_resource_release(url);
    wgf_asset_clear_redirects();
}

/* A stand-in format for what a file names (core's lister): a ".list" file, a name a line,
 * "!" before a required one, "name|fallback" for a fallback. */
static void list_names(const unsigned char *data, int size, wgf_core_priv_load_add_fn add, void *context)
{
    char text[512], *line, *next;
    snprintf(text, sizeof(text), "%.*s", size, (const char *)data);
    for (line = text; line != NULL && *line != '\0'; line = next) {
        char *bar;
        const bool required = line[0] == '!';
        next = strchr(line, '\n');
        if (next != NULL) *next++ = '\0';
        if (required) line++;
        bar = strchr(line, '|');
        if (bar != NULL) *bar++ = '\0';
        add(line, bar, required, context);
    }
}

static wgf_asset_task_status_t ensure_settled(const char *path, float *progress_waiting, char *where, size_t where_size)
{
    const wgf_handle_t task = wgf_asset_ensure(path, NULL, 0);
    wgf_asset_task_status_t status;
    for (int i = 0; i < 20 && wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_PENDING; i++) {
        updates(1);
        if (progress_waiting != NULL && wgf_asset_task_get_status(task) == WGF_ASSET_TASK_STATUS_PENDING) {
            *progress_waiting = wgf_asset_task_get_progress(task);
        }
    }
    status = wgf_asset_task_get_status(task);
    if (where != NULL) snprintf(where, where_size, "%s", wgf_asset_task_get_path(task));
    wgf_asset_task_destroy(task);
    return status;
}

static void test_dependencies(void)
{
    char where[600];
    float waiting = -1.0f;
    CHECK(wgf_core_priv_load_set_lister(".list", list_names));
    write_text("pack/pack.list", "!a.bin\nimages/b.png\n!a.bin\nhttps://cdn.example.com/c.bin\ndata:,x\n");
    write_text("pack/a.bin", "a");
    /* a required one there, an optional one missing (a warning), a URL and data: skipped,
       a name listed twice made local once */
    CHECK(ensure_settled("pack/pack.list", &waiting, where, sizeof(where)) == WGF_ASSET_TASK_STATUS_DONE);
    CHECK(strstr(where, "pack/pack.list") != NULL);
    CHECK(waiting < 0.0f || (waiting >= 0.5f && waiting < 1.0f)); /* half for itself, half for what it names */

    /* a required one missing fails it */
    write_text("pack/broken.list", "!missing.bin\nimages/b.png\n");
    CHECK(ensure_settled("pack/broken.list", NULL, NULL, 0) == WGF_ASSET_TASK_STATUS_FAILED);
    /* one climbing out of the host fails it too, if required */
    write_text("pack/escape.list", "!../../../outside.bin\n");
    CHECK(ensure_settled("pack/escape.list", NULL, NULL, 0) == WGF_ASSET_TASK_STATUS_FAILED);

    /* a fallback, when its own file is missing: a texture's own image for its compressed one */
    write_text("pack/images/own.png", "own");
    write_text("pack/fallback.list", "!images/compressed.ktx|images/own.png\n");
    CHECK(ensure_settled("pack/fallback.list", NULL, NULL, 0) == WGF_ASSET_TASK_STATUS_DONE);

    /* the redirects apply to what a file names, beside wherever it came from */
    write_text("mods/pack/only_in_mod.bin", "modded");
    write_text("pack/redirected.list", "!only_in_mod.bin\n");
    CHECK(ensure_settled("pack/redirected.list", NULL, NULL, 0) == WGF_ASSET_TASK_STATUS_FAILED); /* not without it */
    CHECK(wgf_asset_add_redirect("pack/", "mods/pack/"));
    CHECK(ensure_settled("pack/redirected.list", NULL, NULL, 0) == WGF_ASSET_TASK_STATUS_DONE);
    wgf_asset_clear_redirects();
    {
        /* a load whose file names others: located once they all are */
        const wgf_handle_t note = wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "pack/pack.list");
        const wgf_handle_t broken = wgf_asset_priv_resource_create(WGF_CORE_PRIV_HANDLE_KIND_TEST_A, "pack/broken.list");
        settle();
        CHECK(wgf_resource_get_status(note) == WGF_RESOURCE_STATUS_READY);
        CHECK(wgf_resource_get_status(broken) == WGF_RESOURCE_STATUS_FAILED);
        wgf_resource_release(note);
        wgf_resource_release(broken);
    }
}

static void test_settings(void)
{
    CHECK(wgf_asset_get_cache_mode() == WGF_ASSET_CACHE_MODE_REVALIDATE);
    CHECK(wgf_asset_set_cache_mode(WGF_ASSET_CACHE_MODE_TRUST) && wgf_asset_get_cache_mode() == WGF_ASSET_CACHE_MODE_TRUST);
    CHECK(!wgf_asset_set_cache_mode((wgf_asset_cache_mode_t)3) && !wgf_asset_set_cache_mode((wgf_asset_cache_mode_t)-1));
    CHECK(wgf_asset_get_cache_mode() == WGF_ASSET_CACHE_MODE_TRUST);
    CHECK(wgf_asset_set_cache_mode(WGF_ASSET_CACHE_MODE_REVALIDATE));
    CHECK(strcmp(wgf_asset_get_host(), wgf_core_priv_fs_root()) == 0); /* the default: the storage's root */
    CHECK(strcmp(wgf_asset_get_manifest(), "") == 0 && wgf_asset_set_manifest("assets/manifest.json") &&
          strcmp(wgf_asset_get_manifest(), "assets/manifest.json") == 0);
    CHECK(!wgf_asset_set_manifest("/manifest.json") && !wgf_asset_set_manifest("https://cdn.example.com/m.json") &&
          strcmp(wgf_asset_get_manifest(), "assets/manifest.json") == 0);
    CHECK(wgf_asset_set_manifest(NULL) && strcmp(wgf_asset_get_manifest(), "") == 0);
    {
        char long_host[600];
        memset(long_host, 'a', sizeof(long_host) - 1);
        long_host[sizeof(long_host) - 1] = '\0';
        CHECK(!wgf_asset_set_host(long_host));
    }
}

int main(void)
{
    wgf_core_priv_init();
    wgf_core_priv_fs_set_root("asset_test_root");
    wgf_core_priv_handle_pool_init(&note_pool, WGF_CORE_PRIV_HANDLE_KIND_TEST_A, (void **)&notes, sizeof(note_t), 4, 64);
    wgf_core_priv_resource_register(&note_pool, &note_kind);
    test_sha256();
    test_sha256_pieces();
    test_manifest_parse();
    test_freshness();
    test_paths();
    test_resolve_source();
    test_url_key();
    test_settings();
    test_ensure();
    test_loads();
    test_dependencies();
    wgf_core_priv_resource_unregister(&note_pool);
    wgf_core_priv_handle_pool_destroy(&note_pool);
    wgf_core_priv_os_remove_tree("asset_test_root");
    wgf_core_priv_shutdown();
    printf("wgf_asset_test: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
