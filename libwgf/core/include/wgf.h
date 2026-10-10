#ifndef WGF_H
#define WGF_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Marks a public function. The library is built with hidden visibility, so only
 * functions marked WGF_API are exported. */
#include "wgf_api.h"

/* The libwgf version, "MAJOR.MINOR.PATCH", from the repo's VERSION file. */
WGF_API const char *wgf_version_get(void);

/* Its numbers: a binding built against one version checks them at start, and refuses a
 * library of another major or minor version (a patch apart is compatible). */
WGF_API int wgf_version_get_major(void);
WGF_API int wgf_version_get_minor(void);
WGF_API int wgf_version_get_patch(void);

#ifdef __cplusplus
}
#endif

#endif
