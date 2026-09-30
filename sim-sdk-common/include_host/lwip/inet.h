/*
 * lwip/inet.h -- host override
 *
 * Background: the SDK's lwip/inet.h unconditionally defines struct in_addr /
 * in6_addr and the INADDR_* macros (the embedded target has no system socket
 * headers, so this is the standard practice).
 * But on the host, windows.h (pulled in via FreeRTOS portmacro.h)
 * unconditionally brings in winsock.h, which already defines structures with
 * the same names -> redefinition errors.
 *
 * The simulator does not compile/link the lwip implementation; the header
 * only participates at the declaration level.
 * Therefore:
 *   - Non-Windows (winsock not pulled in) -> pass through the original SDK
 *     header (#include_next)
 *   - Windows host -> skip the struct definitions, keeping only the
 *     supplementary declarations on the lwip side
 */
#ifndef SIM_LWIP_INET_OVERRIDE_H
#define SIM_LWIP_INET_OVERRIDE_H

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/ip_addr.h"
#include "lwip/ip6_addr.h"

#ifndef _WINSOCKAPI_
/* Real-device path: original header */
#include_next <lwip/inet.h>
#else

#ifdef __cplusplus
extern "C" {
#endif

/* Supplement for when winsock.h does not provide in_addr_t (reuses the
 * guard condition of the original header) */
#if !defined(in_addr_t) && !defined(IN_ADDR_T_DEFINED)
typedef u32_t in_addr_t;
#endif

/* winsock 1.x lacks these three BSD-style functions; declare them so
 * referencing code compiles
 * (nobody calls them on the host; on the real device they are implemented
 * by lwip's sockets layer) */
int inet_aton(const char *cp, struct in_addr *addr);
const char *inet_ntop(int af, const void *src, char *dst, int size);
int inet_pton(int af, const char *src, void *dst);

#ifdef __cplusplus
}
#endif

#endif /* _WINSOCKAPI_ */

#endif /* SIM_LWIP_INET_OVERRIDE_H */
