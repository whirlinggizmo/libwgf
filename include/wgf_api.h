#ifndef WGF_API_H
#define WGF_API_H

/* WGF_API marks every public function: exported from the library, where symbols are
 * hidden by default. Every public header includes this. On Windows the library is
 * static and nothing is exported, so it marks nothing; tools/headers.py parses with
 * WGF_API_PARSE, so the mark is there to read on every platform. */

#if defined(_WIN32) && !defined(WGF_API_PARSE)
#define WGF_API
#else
#define WGF_API __attribute__((visibility("default")))
#endif

#endif
