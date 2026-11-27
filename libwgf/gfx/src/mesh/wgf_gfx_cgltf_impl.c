/* cgltf, compiled once for gfx: glTF 2.0's JSON and binary files parsed from memory
 * (files are read through core's fs). Vendored code, compiled as it comes. */
#include "wgf_core_format_priv.h"

/* its numbers read by core's reader, not the C library's atof (wgf_core_format_priv.h) */
#define CGLTF_ATOF(text) wgf_core_priv_parse_double(text, NULL)
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
