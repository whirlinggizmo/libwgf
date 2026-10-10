#include "wgf_asset_priv.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* How long a cached copy stays fresh, from what its response said (wgf_asset.h,
 * REVALIDATE), wgrender's wgri_asset_fresh_until. */

/* A year: how long an immutable response without a max-age stays fresh. */
#define IMMUTABLE_SECONDS (365.0 * 24.0 * 3600.0)

double wgf_asset_priv_fresh_until(const char *cache_control, const char *age, double now)
{
    const char *p = cache_control != NULL ? cache_control : "";
    double max_age = -1.0, already = 0.0;
    bool immutable = false;

    while (*p != '\0') {
        char token[64];
        size_t n = 0;
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        while (*p != '\0' && *p != ',') {
            if (n + 1 < sizeof(token)) token[n++] = (char)tolower((unsigned char)*p);
            p++;
        }
        while (n > 0 && (token[n - 1] == ' ' || token[n - 1] == '\t')) n--;
        token[n] = '\0';
        /* no-cache="field" is still no-cache, as far as a whole file goes */
        if ((strncmp(token, "no-cache", 8) == 0 && (token[8] == '\0' || token[8] == '=')) ||
            strcmp(token, "no-store") == 0) {
            return 0.0;
        }
        if (strcmp(token, "immutable") == 0) {
            immutable = true;
        } else if (strncmp(token, "max-age=", 8) == 0) {
            char *end;
            const double value = strtod(token + 8, &end);
            if (end != token + 8 && *end == '\0' && value >= 0.0) max_age = value;
        }
    }
    if (max_age < 0.0) return immutable ? now + IMMUTABLE_SECONDS : 0.0;
    if (age != NULL && age[0] != '\0') {
        char *end;
        const double value = strtod(age, &end);
        if (end != age && value > 0.0) already = value; /* how long a shared cache held it */
    }
    return max_age > already ? now + (max_age - already) : 0.0;
}
