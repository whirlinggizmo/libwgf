/* The program's options (wgf_app.h): its arguments natively, its page's query on the web,
 * read once into name and value pairs. Natively the arguments come from the system, as
 * wgf_app_run is given none: /proc/self/cmdline on Linux, the C runtime's on Windows, and
 * _NSGetArgv on macOS. */
#include "wgf_app.h"

#include <stdlib.h>
#include <string.h>

#include "wgf_app_priv.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(_WIN32)
#include <stdlib.h> /* __argc, __argv */
#elif defined(__APPLE__)
#include <crt_externs.h>
#else
#include <stdio.h>
#endif

#define OPTIONS_MAX 64
#define TEXT_MAX 4096

static struct {
    bool read;
    char text[TEXT_MAX]; /* names and values, each ended by a 0 */
    const char *names[OPTIONS_MAX], *values[OPTIONS_MAX];
    int count;
    const char *test_query; /* wgf_app_priv_set_options' */
} opts;

/* One option, "name" or "name=value" (`length` bytes), added; a query's decoded. */
static void add(const char *option, size_t length, bool query)
{
    static size_t used;
    const char *eq = memchr(option, '=', length);
    const size_t name_length = eq != NULL ? (size_t)(eq - option) : length;
    char *name, *value = NULL, *out;
    size_t i;
    if (opts.count == 0) used = 0;
    if (name_length == 0 || opts.count >= OPTIONS_MAX || used + length + 2 > TEXT_MAX) return;
    name = &opts.text[used];
    out = name;
    for (i = 0; i < length; i++) {
        char c = option[i];
        if (i == name_length) { /* the '=': the name ends, the value begins */
            *out++ = '\0';
            value = out;
            continue;
        }
        if (query && c == '+') c = ' ';
        if (query && c == '%' && i + 2 < length) {
            const char hex[3] = {option[i + 1], option[i + 2], '\0'};
            c = (char)strtol(hex, NULL, 16);
            i += 2;
        }
        *out++ = c;
    }
    *out++ = '\0';
    if (eq == NULL) {
        value = out - 1; /* the name's own end: "" */
    }
    used = (size_t)(out - opts.text);
    opts.names[opts.count] = name;
    opts.values[opts.count] = value;
    opts.count++;
}

static void add_query(const char *query)
{
    while (*query == '?') query++;
    while (*query != '\0') {
        const char *end = strchr(query, '&');
        const size_t length = end != NULL ? (size_t)(end - query) : strlen(query);
        add(query, length, true);
        query += length;
        if (*query == '&') query++;
    }
}

#if defined(__EMSCRIPTEN__)
EM_JS(char *, page_query, (void), {
    var q = (typeof location !== "undefined" && location.search) ? location.search : "";
    return stringToNewUTF8(q);
})
#endif

/* "--name=value" arguments, the rest left alone. */
static void add_argument(const char *argument)
{
    if (strncmp(argument, "--", 2) == 0 && argument[2] != '\0') add(argument + 2, strlen(argument + 2), false);
}

static void read_options(void)
{
    if (opts.read) return;
    opts.read = true;
    opts.count = 0;
    if (opts.test_query != NULL) {
        add_query(opts.test_query);
        return;
    }
#if defined(__EMSCRIPTEN__)
    {
        char *query = page_query();
        if (query != NULL) {
            add_query(query);
            free(query);
        }
    }
#elif defined(_WIN32)
    {
        int i;
        for (i = 1; i < __argc; i++) add_argument(__argv[i]);
    }
#elif defined(__APPLE__)
    {
        int i;
        for (i = 1; i < *_NSGetArgc(); i++) add_argument((*_NSGetArgv())[i]);
    }
#else
    {
        static char line[TEXT_MAX];
        FILE *file = fopen("/proc/self/cmdline", "rb");
        size_t size, at, first = 1;
        if (file == NULL) return;
        size = fread(line, 1, sizeof(line) - 1, file);
        fclose(file);
        line[size] = '\0';
        for (at = 0; at < size; at += strlen(&line[at]) + 1, first = 0) {
            if (!first) add_argument(&line[at]);
        }
    }
#endif
}

void wgf_app_priv_set_options(const char *query)
{
    opts.test_query = query;
    opts.read = false;
}

static int find(const char *name)
{
    int i, found = -1;
    if (name == NULL) return -1;
    read_options();
    for (i = 0; i < opts.count; i++) {
        if (strcmp(opts.names[i], name) == 0) found = i; /* the last given wins */
    }
    return found;
}

bool wgf_app_has_option(const char *name)
{
    return find(name) >= 0;
}

const char *wgf_app_get_option(const char *name)
{
    const int i = find(name);
    return i >= 0 ? opts.values[i] : "";
}

int wgf_app_get_option_int(const char *name, int otherwise)
{
    const int i = find(name);
    char *end;
    long value;
    if (i < 0 || opts.values[i][0] == '\0') return otherwise;
    value = strtol(opts.values[i], &end, 10);
    return *end == '\0' ? (int)value : otherwise;
}

float wgf_app_get_option_float(const char *name, float otherwise)
{
    const int i = find(name);
    char *end;
    float value;
    if (i < 0 || opts.values[i][0] == '\0') return otherwise;
    value = strtof(opts.values[i], &end);
    return *end == '\0' ? value : otherwise;
}
