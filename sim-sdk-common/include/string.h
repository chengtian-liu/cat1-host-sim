/*
 * string.h -- host shim
 *
 * Only adds the BSD extensions missing from MinGW libc; everything else is
 * passed through to the real header (#include_next).
 *   - strnstr(): used by the SDK's at_utils.c, provided by newlib on real device
 */
#ifndef SIM_STRING_SHIM_H
#define SIM_STRING_SHIM_H

#include_next <string.h>

#ifdef __cplusplus
extern "C" {
#endif

char *strnstr(const char *haystack, const char *needle, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SIM_STRING_SHIM_H */
