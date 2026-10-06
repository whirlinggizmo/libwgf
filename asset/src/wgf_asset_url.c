#include "wgf_asset_priv.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgf_log.h"

/* Paths, URLs, and redirects, ported from wgrender's wgr_asset.c: what a reference
 * names against the host, as a browser reads one (RFC 3986), and where a file is looked
 * for through the redirects. */

bool wgf_asset_priv_normalize_path(const char *path, char *out, size_t out_size)
{
    return wgf_core_priv_fs_normalize_path(path, out, out_size); /* wgrender's, in core's fs */
}

bool wgf_asset_priv_normalize_prefix(const char *text, char *out, size_t out_size)
{
    const size_t n = strlen(text);
    const bool slash = n > 0 && (text[n - 1] == '/' || text[n - 1] == '\\');
    size_t len;
    if (!wgf_asset_priv_normalize_path(text, out, out_size)) return false;
    len = strlen(out);
    if (slash) {
        if (len + 1 >= out_size) return false;
        out[len] = '/';
        out[len + 1] = '\0';
    }
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool starts_with_ci(const char *text, const char *prefix)
{
    for (; *prefix != '\0'; text++, prefix++) {
        if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) return false;
    }
    return true;
}

/* How much of `text` is a scheme and its ":" (a letter, then letters, digits, "+", "-"
 * or "."), or 0 when it has none. */
static size_t scheme_length(const char *text)
{
    size_t i = 0;
    if (!isalpha((unsigned char)text[0])) return 0;
    while (isalnum((unsigned char)text[i]) || text[i] == '+' || text[i] == '-' || text[i] == '.') i++;
    return text[i] == ':' ? i + 1 : 0;
}

bool wgf_asset_priv_has_scheme(const char *text)
{
    return text != NULL && scheme_length(text) > 0;
}

/* Percent-decode `len` bytes of `text` into `out`; false for a decoded NUL or no room. */
static bool percent_decode(const char *text, size_t len, char *out, size_t out_size)
{
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        char ch = text[i];
        if (ch == '%' && i + 2 < len && hex_value(text[i + 1]) >= 0 && hex_value(text[i + 2]) >= 0) {
            ch = (char)(hex_value(text[i + 1]) * 16 + hex_value(text[i + 2]));
            i += 2;
        }
        if (ch == '\0' || n + 1 >= out_size) return false;
        out[n++] = ch;
    }
    out[n] = '\0';
    return true;
}

/* RFC 3986's remove_dot_segments over `path`, appended to `out` at `*pos`. An absolute
 * path stops ".." at its root; a relative one keeps what climbs above it. */
static bool remove_dots(const char *path, size_t len, char *out, size_t out_size, size_t *pos)
{
    const bool absolute = len > 0 && path[0] == '/';
    const char *segments[128];
    size_t lengths[128];
    int count = 0, above = 0;
    bool trailing = false;

    for (size_t start = absolute ? 1 : 0; start <= len;) {
        size_t end = start, n;
        while (end < len && path[end] != '/') end++;
        n = end - start;
        if (n == 1 && path[start] == '.') {
            trailing = end >= len;
        } else if (n == 2 && path[start] == '.' && path[start + 1] == '.') {
            if (count > 0) {
                count--;
            } else if (!absolute) {
                above++;
            }
            trailing = end >= len;
        } else {
            if (count == (int)(sizeof(segments) / sizeof(segments[0]))) return false;
            segments[count] = path + start;
            lengths[count++] = n;
            trailing = false;
        }
        start = end + 1;
    }
    if (absolute) {
        if (*pos + 1 >= out_size) return false;
        out[(*pos)++] = '/';
    }
    for (int i = 0; i < above; i++) {
        if (*pos + 3 >= out_size) return false;
        memcpy(out + *pos, "../", 3);
        *pos += 3;
    }
    for (int i = 0; i < count; i++) {
        if (*pos + lengths[i] + 1 >= out_size) return false;
        if (i > 0) out[(*pos)++] = '/';
        memcpy(out + *pos, segments[i], lengths[i]);
        *pos += lengths[i];
    }
    if (trailing && count > 0) {
        if (*pos + 1 >= out_size) return false;
        out[(*pos)++] = '/';
    }
    out[*pos] = '\0';
    return true;
}

static bool append(char *out, size_t out_size, size_t *pos, const char *text, size_t len)
{
    if (*pos + len >= out_size) return false;
    memcpy(out + *pos, text, len);
    *pos += len;
    out[*pos] = '\0';
    return true;
}

wgf_asset_priv_source_t wgf_asset_priv_resolve_source(const char *host, wgf_asset_priv_host_kind_t kind,
                                                      const char *ref, char *out, size_t out_size)
{
    char r[WGF_ASSET_PRIV_URL_MAX], merged[WGF_ASSET_PRIV_URL_MAX];
    size_t pos = 0, path_len, host_len, prefix_len = 0;
    const size_t host_scheme = host != NULL ? scheme_length(host) : 0;

    if (host == NULL || ref == NULL || ref[0] == '\0' || out == NULL || out_size == 0) {
        return WGF_ASSET_PRIV_SOURCE_REFUSED;
    }
    out[0] = '\0';
    if (scheme_length(ref) > 0) {
        if (kind != WGF_ASSET_PRIV_HOST_BROWSER && !starts_with_ci(ref, "http://") &&
            !starts_with_ci(ref, "https://")) {
            return WGF_ASSET_PRIV_SOURCE_REFUSED;
        }
        return append(out, out_size, &pos, ref, strlen(ref)) ? WGF_ASSET_PRIV_SOURCE_URL
                                                             : WGF_ASSET_PRIV_SOURCE_REFUSED;
    }
    path_len = strcspn(ref, "?#");
    if (kind == WGF_ASSET_PRIV_HOST_LOCAL) {
        return percent_decode(ref, path_len, r, sizeof(r)) && wgf_asset_priv_normalize_path(r, out, out_size)
                   ? WGF_ASSET_PRIV_SOURCE_LOCAL
                   : WGF_ASSET_PRIV_SOURCE_REFUSED;
    }

    /* a URL: "\" is "/" in its path, as the browser reads an http(s) one */
    if (strlen(ref) >= sizeof(r)) return WGF_ASSET_PRIV_SOURCE_REFUSED;
    snprintf(r, sizeof(r), "%s", ref);
    for (size_t i = 0; i < path_len; i++) {
        if (r[i] == '\\') r[i] = '/';
    }
    if (r[0] == '/' && r[1] == '/') { /* another host, on this one's scheme */
        return append(out, out_size, &pos, host, host_scheme) && append(out, out_size, &pos, r, strlen(r))
                   ? WGF_ASSET_PRIV_SOURCE_URL
                   : WGF_ASSET_PRIV_SOURCE_REFUSED;
    }
    host_len = strcspn(host, "?#");
    while (host_len > 0 && host[host_len - 1] == '/') host_len--;
    if (host_scheme > 0 && strncmp(host + host_scheme, "//", 2) == 0) { /* scheme and authority */
        const char *slash = memchr(host + host_scheme + 2, '/', host_len - host_scheme - 2);
        prefix_len = slash != NULL ? (size_t)(slash - host) : host_len;
    }
    if (!append(out, out_size, &pos, host, prefix_len)) return WGF_ASSET_PRIV_SOURCE_REFUSED;
    if (r[0] == '/') {
        snprintf(merged, sizeof(merged), "%.*s", (int)path_len, r);
    } else { /* under the host's path, or on it for a bare query or fragment */
        const size_t base = host_len - prefix_len;
        const size_t kept = r[0] == '?' || r[0] == '#' ? 0 : path_len;
        if (base + kept + 2 >= sizeof(merged)) return WGF_ASSET_PRIV_SOURCE_REFUSED;
        snprintf(merged, sizeof(merged), "%.*s%s%.*s", (int)base, host + prefix_len,
                 base > 0 || prefix_len > 0 ? "/" : "", (int)kept, r);
    }
    if (!remove_dots(merged, strlen(merged), out, out_size, &pos)) return WGF_ASSET_PRIV_SOURCE_REFUSED;
    return append(out, out_size, &pos, r + path_len, strlen(r + path_len)) ? WGF_ASSET_PRIV_SOURCE_URL
                                                                           : WGF_ASSET_PRIV_SOURCE_REFUSED;
}

bool wgf_asset_priv_file_url_path(const char *url, char *out, size_t out_size)
{
    const char *path;
    size_t authority;
    if (url == NULL || out == NULL || out_size == 0 || !starts_with_ci(url, "file://")) return false;
    url += 7;
    authority = strcspn(url, "/");
    if (authority != 0 && !(authority == 9 && starts_with_ci(url, "localhost"))) {
        return false; /* another machine's file */
    }
    path = url + authority;
    if (!percent_decode(path, strcspn(path, "?#"), out, out_size)) return false;
#if defined(_WIN32)
    if (out[0] == '/' && isalpha((unsigned char)out[1]) && out[2] == ':') {
        memmove(out, out + 1, strlen(out)); /* "/C:/game" is "C:/game" */
    }
#endif
    return out[0] != '\0';
}

bool wgf_asset_priv_url_key(const char *url, char *key, size_t key_size)
{
    char hash[WGF_ASSET_PRIV_SHA256_TEXT], name[256];
    const size_t path_len = url != NULL ? strcspn(url, "?#") : 0;
    size_t start = path_len;
    if (url == NULL || scheme_length(url) == 0 || strstr(url, "://") == NULL) return false;
    while (start > 0 && url[start - 1] != '/') start--;
    if (start == path_len || !percent_decode(url + start, path_len - start, name, sizeof(name)) ||
        strpbrk(name, "/\\:") != NULL) {
        return false; /* no file name, or one that isn't a name */
    }
    wgf_asset_priv_sha256_text((const unsigned char *)url, strlen(url), hash);
    /* "sha256:" + hex: the first 16 hex digits tell URLs apart; the name keeps the
       extension a loader is chosen by */
    return snprintf(key, key_size, ".url/%.16s/%s", hash + 7, name) < (int)key_size;
}

bool wgf_asset_priv_is_relative_uri(const char *uri)
{
    if (uri == NULL || uri[0] == '\0' || uri[0] == '/' || strncmp(uri, "data:", 5) == 0) return false;
    for (const char *c = uri; *c != '\0' && *c != '/'; c++) {
        if (*c == ':') return false; /* a scheme (http:, file:, ...) */
    }
    return true;
}

bool wgf_asset_priv_join_relative(const char *base_path, const char *uri, char *out, size_t out_size)
{
    char buffer[1024];
    const char *segments[128];
    size_t lengths[128];
    int count = 0;
    size_t n = 0, pos = 0;
    const char *last_slash = NULL;

    if (base_path == NULL || uri == NULL || out == NULL || out_size == 0) return false;
    /* the base's directory, then the decoded uri, as one string of segments */
    for (const char *c = base_path; *c != '\0'; c++) {
        if (*c == '/' || *c == '\\') last_slash = c;
    }
    if (last_slash != NULL) {
        n = (size_t)(last_slash - base_path) + 1;
        if (n >= sizeof(buffer)) return false;
        memcpy(buffer, base_path, n);
    }
    for (const char *c = uri; *c != '\0'; c++) {
        char ch = *c;
        if (ch == '%' && hex_value(c[1]) >= 0 && hex_value(c[2]) >= 0) {
            ch = (char)(hex_value(c[1]) * 16 + hex_value(c[2]));
            c += 2;
        }
        if (ch == ':' || ch == '\0') return false; /* a drive, decoded or not */
        if (n + 1 >= sizeof(buffer)) return false;
        buffer[n++] = ch;
    }
    buffer[n] = '\0';
    for (size_t start = 0; start <= n;) {
        size_t end = start, len;
        while (end < n && buffer[end] != '/' && buffer[end] != '\\') end++;
        len = end - start;
        if (len == 0 || (len == 1 && buffer[start] == '.')) {
            /* empty or "." */
        } else if (len == 2 && buffer[start] == '.' && buffer[start + 1] == '.') {
            if (count == 0) return false; /* above the top directory */
            count--;
        } else {
            if (count >= (int)(sizeof(segments) / sizeof(segments[0]))) return false;
            segments[count] = &buffer[start];
            lengths[count++] = len;
        }
        start = end + 1;
    }
    for (int i = 0; i < count; i++) {
        if (pos + lengths[i] + (i > 0 ? 1 : 0) >= out_size) return false;
        if (i > 0) out[pos++] = '/';
        memcpy(out + pos, segments[i], lengths[i]);
        pos += lengths[i];
    }
    out[pos] = '\0';
    return count > 0;
}

/* ---------------------------------------------------------------- redirects ---- */

#define MAX_REDIRECTS 32
static struct {
    char prefix[256];
    char target[512];
    bool url; /* a download source ("scheme://..."), not another path */
} redirects[MAX_REDIRECTS];
static int redirect_count;

bool wgf_asset_add_redirect(const char *prefix, const char *target)
{
    bool url;
    wgf_asset_priv_install();
    if (prefix == NULL || target == NULL || prefix[0] == '\0' || target[0] == '\0') {
        wgf_log_warn("wgf_asset_add_redirect: needs a prefix and a target");
        return false;
    }
    if (redirect_count >= MAX_REDIRECTS || strlen(prefix) >= sizeof(redirects[0].prefix) ||
        strlen(target) >= sizeof(redirects[0].target)) {
        wgf_log_warn("wgf_asset_add_redirect: too many redirects (%d), or too long", MAX_REDIRECTS);
        return false;
    }
    url = strstr(target, "://") != NULL;
    /* paths stay under the host, as an ensure's do; a URL is a URL */
    if (!wgf_asset_priv_normalize_prefix(prefix, redirects[redirect_count].prefix, sizeof(redirects[0].prefix)) ||
        (!url &&
         !wgf_asset_priv_normalize_prefix(target, redirects[redirect_count].target, sizeof(redirects[0].target)))) {
        wgf_log_warn("wgf_asset_add_redirect: %s -> %s: a path that isn't under the host", prefix, target);
        return false;
    }
    if (url) snprintf(redirects[redirect_count].target, sizeof(redirects[0].target), "%s", target);
    redirects[redirect_count].url = url;
    redirect_count++;
    return true;
}

void wgf_asset_clear_redirects(void)
{
    wgf_asset_priv_install();
    redirect_count = 0;
}

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

wgf_asset_priv_candidate_t *wgf_asset_priv_plan(const char *path, int *count)
{
    const int max = redirect_count + 1;
    wgf_asset_priv_candidate_t *list =
        (wgf_asset_priv_candidate_t *)malloc(sizeof(wgf_asset_priv_candidate_t) * (size_t)max);
    int n = 0;
    *count = 0;
    if (list == NULL) return NULL;
    for (int pass = 0; pass < 2; pass++) { /* 0: the rules' paths, newest first; 1: the path itself */
        for (int r = pass == 0 ? redirect_count - 1 : -1; r >= -1 && n < max; r--) {
            wgf_asset_priv_candidate_t *c = &list[n];
            if (pass == 0 && (r < 0 || redirects[r].url || !starts_with(path, redirects[r].prefix))) continue;
            if (pass == 0) {
                if (snprintf(c->path, sizeof(c->path), "%s%s", redirects[r].target,
                             path + strlen(redirects[r].prefix)) >= (int)sizeof(c->path)) {
                    continue;
                }
            } else {
                snprintf(c->path, sizeof(c->path), "%s", path);
            }
            c->overlay = pass == 0;
            c->url[0] = '\0';
            for (int u = redirect_count - 1; u >= 0; u--) { /* the newest download rule matching it */
                if (redirects[u].url && starts_with(c->path, redirects[u].prefix)) {
                    snprintf(c->url, sizeof(c->url), "%s%s", redirects[u].target,
                             c->path + strlen(redirects[u].prefix));
                    break;
                }
            }
            n++;
            if (pass == 1) break;
        }
    }
    *count = n;
    return list;
}
