#include "wgf_identity.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "wgf_core_os_priv.h"
#include "wgf_core_priv.h"
#include "wgf_log.h"

/* The program's identity for what libwgf keeps for it on the desktop (wgf.h): the
 * company and the app, each one safe path component, and the directory they name under
 * the user's cache. Ported from wgrender's wgr.c (wgri_app_clean_name, wgri_app_join,
 * wgri_app_cache_dir); its setters returned void and warned, libwgf's refuse with false
 * (CONVENTIONS.md, "Say clamp or refuse"). Outside core's reset: a program names itself
 * first, once. */

#define NAME_SIZE 128

static char app_company[NAME_SIZE];
static char app_name[NAME_SIZE];
static char app_default_name[NAME_SIZE];

bool wgf_core_priv_app_clean_name(const char *name, char *out, size_t out_size)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL"};
    size_t n = 0, start = 0, end, stem;
    char kept[NAME_SIZE];
    bool device = false;

    if (out == NULL || out_size == 0) return false;
    out[0] = '\0';
    if (name == NULL || strlen(name) >= sizeof(kept)) return false; /* too long: refused, never cut */
    for (const char *c = name; *c != '\0'; c++) {
        const unsigned char ch = (unsigned char)*c;
        kept[n++] = ch < 0x20 || strchr("<>:\"/\\|?*", ch) != NULL ? '_' : (char)ch;
    }
    end = n;
    while (start < end && (kept[start] == '.' || kept[start] == ' ')) start++;
    while (end > start && (kept[end - 1] == '.' || kept[end - 1] == ' ')) end--;
    if (start == end) return false;

    /* Windows opens a device for these names, whatever follows a dot */
    for (stem = start; stem < end && kept[stem] != '.'; stem++) {
    }
    for (size_t d = 0; d < sizeof(devices) / sizeof(devices[0]) && !device; d++) {
        device = stem - start == 3 && tolower((unsigned char)kept[start]) == tolower((unsigned char)devices[d][0]) &&
                 tolower((unsigned char)kept[start + 1]) == tolower((unsigned char)devices[d][1]) &&
                 tolower((unsigned char)kept[start + 2]) == tolower((unsigned char)devices[d][2]);
    }
    if (stem - start == 4 && isdigit((unsigned char)kept[start + 3]) && kept[start + 3] != '0') {
        char three[4] = {(char)toupper((unsigned char)kept[start]), (char)toupper((unsigned char)kept[start + 1]),
                         (char)toupper((unsigned char)kept[start + 2]), '\0'};
        device = device || strcmp(three, "COM") == 0 || strcmp(three, "LPT") == 0;
    }
    return snprintf(out, out_size, "%s%.*s", device ? "_" : "", (int)(end - start), kept + start) < (int)out_size;
}

/* `name` cleaned into `kept` (NULL or "": the default, cleared); false, `kept` as it
 * was, for one refused. */
static bool take_name(const char *name, char *kept, size_t size)
{
    char cleaned[NAME_SIZE];
    if (name == NULL || name[0] == '\0') {
        kept[0] = '\0';
        return true;
    }
    if (!wgf_core_priv_app_clean_name(name, cleaned, sizeof(cleaned))) return false;
    snprintf(kept, size, "%s", cleaned);
    return true;
}

bool wgf_identity_set_company(const char *company)
{
    if (!take_name(company, app_company, sizeof(app_company))) {
        wgf_log_warn("wgf_identity_set_company: \"%s\" can't name a directory (too long, or nothing left of it); "
                     "the company stays \"%s\"",
                     company, wgf_identity_get_company());
        return false;
    }
    return true;
}

const char *wgf_identity_get_company(void)
{
    return app_company[0] != '\0' ? app_company : "DefaultCompany";
}

bool wgf_identity_set_product(const char *name)
{
    if (!take_name(name, app_name, sizeof(app_name))) {
        wgf_log_warn("wgf_identity_set_product: \"%s\" can't name a directory (too long, or nothing left of it); "
                     "the app stays \"%s\"",
                     name, wgf_identity_get_product());
        return false;
    }
    return true;
}

const char *wgf_identity_get_product(void)
{
    if (app_name[0] != '\0') return app_name;
    if (app_default_name[0] == '\0') {
        char exe[NAME_SIZE] = "";
        wgf_core_priv_os_executable_name(exe, sizeof(exe));
        if (!wgf_core_priv_app_clean_name(exe, app_default_name, sizeof(app_default_name))) {
            snprintf(app_default_name, sizeof(app_default_name), "DefaultApp");
        }
    }
    return app_default_name;
}

bool wgf_core_priv_app_cache_dir(char *out, size_t out_size)
{
    char base[512];
    int n;
    if (!wgf_core_priv_os_user_cache_dir(base, sizeof(base))) return false;
#if defined(_WIN32)
    /* LOCALAPPDATA\<company>\<app> is the app's own, not only its cache */
    n = snprintf(out, out_size, "%s/%s/%s/cache", base, wgf_identity_get_company(), wgf_identity_get_product());
#else
    n = snprintf(out, out_size, "%s/%s/%s", base, wgf_identity_get_company(), wgf_identity_get_product());
#endif
    if (n < 0 || n >= (int)out_size) return false;
    for (char *c = out; *c != '\0'; c++) {
        if (*c == '\\') *c = '/'; /* Windows takes either, and fs makes directories at "/" */
    }
    return true;
}

bool wgf_core_priv_app_data_dir(char *out, size_t out_size)
{
    char base[512];
    int n;
    if (!wgf_core_priv_os_user_data_dir(base, sizeof(base))) return false;
    n = snprintf(out, out_size, "%s/%s/%s", base, wgf_identity_get_company(), wgf_identity_get_product());
    if (n < 0 || n >= (int)out_size) return false;
    for (char *c = out; *c != '\0'; c++) {
        if (*c == '\\') *c = '/'; /* Windows takes either, and fs makes directories at "/" */
    }
    return true;
}
