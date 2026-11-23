#ifndef WGF_H
#define WGF_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Marks a public function. The library is built with hidden visibility, so only the
 * functions marked WGF_API are exported. */
#include "wgf_api.h"

/* Returns the libwgf version as "MAJOR.MINOR.PATCH", taken from the repo's VERSION file. */
WGF_API const char *wgf_version_get(void);

/* Returns one part of the libwgf version as a number.
 *
 * A binding checks these numbers at start against the version it was built for. It refuses
 * a library of another major or minor version. A different patch number is compatible. */
WGF_API int wgf_version_get_major(void);
WGF_API int wgf_version_get_minor(void);
WGF_API int wgf_version_get_patch(void);

#ifdef __cplusplus
}
#endif

#endif
