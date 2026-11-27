/* cJSON, compiled once for core (wgf_core_json.c's wrapper): vendored code, compiled as it
 * comes. */
#include <stdlib.h> /* declared before the name is taken below */

#include "wgf_core_format_priv.h"

/* its numbers read by core's reader, not the C library's strtod (wgf_core_format_priv.h) */
#define strtod(text, end) wgf_core_priv_parse_double(text, end)

#include "cJSON.c"
