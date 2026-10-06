#include <stdio.h>
#include <string.h>

#include "wgf.h"

int main(void) {
    const char *version = wgf_version_get();
    if (version == NULL || strcmp(version, WGF_TEST_EXPECTED_VERSION) != 0) {
        fprintf(stderr, "wgf_version_get: got \"%s\", expected \"%s\"\n",
                version ? version : "(null)", WGF_TEST_EXPECTED_VERSION);
        return 1;
    }
    {
        char numbers[64];
        snprintf(numbers, sizeof(numbers), "%d.%d.%d", wgf_version_get_major(), wgf_version_get_minor(),
                 wgf_version_get_patch());
        if (strcmp(numbers, WGF_TEST_EXPECTED_VERSION) != 0) {
            fprintf(stderr, "wgf_version_get_major/minor/patch: got %s, expected %s\n", numbers,
                    WGF_TEST_EXPECTED_VERSION);
            return 1;
        }
    }
    return 0;
}
