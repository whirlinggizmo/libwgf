#include "wgf_asset_priv.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A reader for the one shape a manifest has: JSON (RFC 8259), strictly, with the
 * members this file doesn't know skipped whole. No JSON library: this is all the
 * JSON libwgf reads outside glTF (which cgltf parses). wgrender's wgr_manifest.c, ported
 * whole, its version key renamed "wgf_manifest". */

#define MAX_DEPTH 32 /* nesting in a skipped value; a manifest itself nests two deep */

typedef struct {
    const char *s;
    size_t n, i;
} reader_t;

static void space(reader_t *r)
{
    while (r->i < r->n && (r->s[r->i] == ' ' || r->s[r->i] == '\t' || r->s[r->i] == '\n' || r->s[r->i] == '\r')) {
        r->i++;
    }
}

static bool eat(reader_t *r, char c)
{
    space(r);
    if (r->i < r->n && r->s[r->i] == c) {
        r->i++;
        return true;
    }
    return false;
}

static bool hex4(const char *s, uint32_t *out)
{
    *out = 0;
    for (int i = 0; i < 4; i++) {
        const char c = s[i];
        const int v = c >= '0' && c <= '9'   ? c - '0'
                      : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                             : -1;
        if (v < 0) return false;
        *out = *out << 4 | (uint32_t)v;
    }
    return true;
}

static void put_utf8(char *out, size_t *pos, uint32_t c)
{
    if (c < 0x80) {
        out[(*pos)++] = (char)c;
    } else if (c < 0x800) {
        out[(*pos)++] = (char)(0xc0 | (c >> 6));
        out[(*pos)++] = (char)(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
        out[(*pos)++] = (char)(0xe0 | (c >> 12));
        out[(*pos)++] = (char)(0x80 | ((c >> 6) & 0x3f));
        out[(*pos)++] = (char)(0x80 | (c & 0x3f));
    } else {
        out[(*pos)++] = (char)(0xf0 | (c >> 18));
        out[(*pos)++] = (char)(0x80 | ((c >> 12) & 0x3f));
        out[(*pos)++] = (char)(0x80 | ((c >> 6) & 0x3f));
        out[(*pos)++] = (char)(0x80 | (c & 0x3f));
    }
}

/* A string, decoded into a malloc'd copy (`out` NULL: only checked and skipped).
 * UTF-8 is never longer than the escapes it comes from, so the raw length is room
 * enough. A NUL, escaped or not, is refused: nothing a name may hold. */
static bool string(reader_t *r, char **out)
{
    size_t start, end, pos = 0;
    char *text = NULL;

    space(r);
    if (r->i >= r->n || r->s[r->i] != '"') return false;
    start = end = r->i + 1;
    while (end < r->n && r->s[end] != '"') {
        end += r->s[end] == '\\' ? 2 : 1;
    }
    if (end >= r->n) return false;
    if (out != NULL && (text = malloc(end - start + 1)) == NULL) return false;
    for (size_t i = start; i < end; i++) {
        const unsigned char c = (unsigned char)r->s[i];
        uint32_t code, low;
        if (c < 0x20) goto bad; /* control characters must be escaped */
        if (c != '\\') {
            if (text != NULL) text[pos++] = (char)c;
            continue;
        }
        switch (r->s[++i]) {
        case '"': code = '"'; break;
        case '\\': code = '\\'; break;
        case '/': code = '/'; break;
        case 'b': code = '\b'; break;
        case 'f': code = '\f'; break;
        case 'n': code = '\n'; break;
        case 'r': code = '\r'; break;
        case 't': code = '\t'; break;
        case 'u':
            if (i + 4 >= end || !hex4(r->s + i + 1, &code)) goto bad;
            i += 4;
            if (code >= 0xd800 && code < 0xdc00) { /* a surrogate pair: the low half must follow */
                if (i + 6 >= end || r->s[i + 1] != '\\' || r->s[i + 2] != 'u' || !hex4(r->s + i + 3, &low) ||
                    low < 0xdc00 || low >= 0xe000) {
                    goto bad;
                }
                i += 6;
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
            } else if (code >= 0xdc00 && code < 0xe000) {
                goto bad;
            }
            break;
        default: goto bad;
        }
        if (code == 0) goto bad;
        if (text != NULL) put_utf8(text, &pos, code);
    }
    r->i = end + 1; /* past the closing quote */
    if (text != NULL) {
        text[pos] = '\0';
        *out = text;
    }
    return true;
bad:
    free(text);
    return false;
}

static bool literal(reader_t *r, const char *word)
{
    const size_t n = strlen(word);
    space(r);
    if (r->n - r->i < n || memcmp(r->s + r->i, word, n) != 0) return false;
    r->i += n;
    return true;
}

static void digits(reader_t *r)
{
    while (r->i < r->n && r->s[r->i] >= '0' && r->s[r->i] <= '9') r->i++;
}

/* A JSON number, checked and skipped; `one` (or NULL) says whether it was exactly 1. */
static bool number(reader_t *r, bool *one)
{
    size_t start, mark;
    space(r);
    start = r->i;
    if (r->i < r->n && r->s[r->i] == '-') r->i++;
    if (r->i < r->n && r->s[r->i] == '0') {
        r->i++;
    } else if (r->i < r->n && r->s[r->i] >= '1' && r->s[r->i] <= '9') {
        digits(r);
    } else {
        return false;
    }
    if (r->i < r->n && r->s[r->i] == '.') {
        mark = ++r->i;
        digits(r);
        if (r->i == mark) return false;
    }
    if (r->i < r->n && (r->s[r->i] == 'e' || r->s[r->i] == 'E')) {
        r->i++;
        if (r->i < r->n && (r->s[r->i] == '+' || r->s[r->i] == '-')) r->i++;
        mark = r->i;
        digits(r);
        if (r->i == mark) return false;
    }
    if (one != NULL) *one = r->i - start == 1 && r->s[start] == '1';
    return true;
}

/* Any JSON value, checked and skipped. */
static bool value(reader_t *r, int depth)
{
    space(r);
    if (r->i >= r->n || depth > MAX_DEPTH) return false;
    switch (r->s[r->i]) {
    case '"': return string(r, NULL);
    case 't': return literal(r, "true");
    case 'f': return literal(r, "false");
    case 'n': return literal(r, "null");
    case '[':
        r->i++;
        if (eat(r, ']')) return true;
        do {
            if (!value(r, depth + 1)) return false;
        } while (eat(r, ','));
        return eat(r, ']');
    case '{':
        r->i++;
        if (eat(r, '}')) return true;
        do {
            if (!string(r, NULL) || !eat(r, ':') || !value(r, depth + 1)) return false;
        } while (eat(r, ','));
        return eat(r, '}');
    default: return number(r, NULL);
    }
}

static bool valid_hash(const char *hash)
{
    if (strlen(hash) != WGF_ASSET_PRIV_SHA256_TEXT - 1 || strncmp(hash, "sha256:", 7) != 0) return false;
    for (const char *p = hash + 7; *p != '\0'; p++) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
    }
    return true;
}

static bool valid_name(const char *name)
{
    return name[0] != '\0' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && strchr(name, '/') == NULL;
}

static int compare(const void *a, const void *b)
{
    const wgf_asset_priv_manifest_entry_t *x = a, *y = b;
    if (x->dir != y->dir) return x->dir ? 1 : -1;
    return strcmp(x->name, y->name);
}

static bool add(wgf_asset_priv_manifest_t *out, int *capacity, char *name, const char *hash, bool dir)
{
    if (out->count == *capacity) {
        const int grown = *capacity > 0 ? *capacity * 2 : 16;
        wgf_asset_priv_manifest_entry_t *grown_entries = realloc(out->entries, sizeof(*grown_entries) * (size_t)grown);
        if (grown_entries == NULL) return false;
        out->entries = grown_entries;
        *capacity = grown;
    }
    out->entries[out->count].name = name;
    out->entries[out->count].dir = dir;
    memcpy(out->entries[out->count].hash, hash, WGF_ASSET_PRIV_SHA256_TEXT);
    out->count++;
    return true;
}

/* "files" or "dirs": an object of name -> hash. */
static bool entries(reader_t *r, bool dir, wgf_asset_priv_manifest_t *out, int *capacity)
{
    if (!eat(r, '{')) return false;
    if (eat(r, '}')) return true;
    do {
        char *name = NULL, *hash = NULL;
        const bool ok = string(r, &name) && eat(r, ':') && string(r, &hash) && valid_name(name) &&
                        valid_hash(hash) && add(out, capacity, name, hash, dir);
        free(hash);
        if (!ok) {
            free(name);
            return false;
        }
    } while (eat(r, ','));
    return eat(r, '}');
}

bool wgf_asset_priv_manifest_parse(const char *json, size_t size, wgf_asset_priv_manifest_t *out)
{
    reader_t r = {json, size, 0};
    int capacity = 0;
    bool version = false, ok = true;

    memset(out, 0, sizeof(*out));
    if (json == NULL || !eat(&r, '{')) return false;
    if (!eat(&r, '}')) {
        do {
            char *key = NULL;
            if (!string(&r, &key) || !eat(&r, ':')) {
                free(key);
                ok = false;
                break;
            }
            if (strcmp(key, "wgf_manifest") == 0) {
                ok = number(&r, &version) && version;
            } else if (strcmp(key, "files") == 0) {
                ok = entries(&r, false, out, &capacity);
            } else if (strcmp(key, "dirs") == 0) {
                ok = entries(&r, true, out, &capacity);
            } else {
                ok = value(&r, 1);
            }
            free(key);
        } while (ok && eat(&r, ','));
        ok = ok && eat(&r, '}');
    }
    space(&r);
    ok = ok && version && r.i == r.n; /* nothing after the object */
    if (ok && out->count > 1) {
        qsort(out->entries, (size_t)out->count, sizeof(out->entries[0]), compare);
        for (int i = 1; i < out->count && ok; i++) { /* a name listed twice */
            ok = compare(&out->entries[i - 1], &out->entries[i]) != 0;
        }
    }
    if (!ok) {
        wgf_asset_priv_manifest_free(out);
        return false;
    }
    return true;
}

const char *wgf_asset_priv_manifest_find(const wgf_asset_priv_manifest_t *manifest, const char *name, bool dir)
{
    const wgf_asset_priv_manifest_entry_t key = {.name = (char *)name, .dir = dir};
    const wgf_asset_priv_manifest_entry_t *found;
    if (manifest == NULL || manifest->count == 0 || name == NULL) return NULL;
    found = bsearch(&key, manifest->entries, (size_t)manifest->count, sizeof(key), compare);
    return found != NULL ? found->hash : NULL;
}

void wgf_asset_priv_manifest_free(wgf_asset_priv_manifest_t *manifest)
{
    if (manifest == NULL) return;
    for (int i = 0; i < manifest->count; i++) free(manifest->entries[i].name);
    free(manifest->entries);
    manifest->entries = NULL;
    manifest->count = 0;
}
