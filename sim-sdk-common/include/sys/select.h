/*
 * sys/select.h -- host shim (MinGW has no such header)
 *
 * The SDK's posix_io.c/posix_io.h targets embedded newlib and uses the
 * standard fd_set interface. Simulator fds are all small internal integers
 * of the posix layer (< POSIX_DEVICE_SUPPORT_NUM), so a 32-bit bitmap is
 * more than enough.
 *
 * Coexistence with MinGW: windows.h pulls in the winsock fd_set via
 * psdk_inc/_fd_types.h. The two yield to each other on a first-one-wins
 * basis:
 *   - this header first: defines _SYS_TYPES_FD_SET, so _fd_types.h skips its
 *     fd_set definition;
 *   - windows.h first (___WSA_FD_TYPES_H already defined): this header yields
 *     entirely and reuses winsock's fd_set and FD_* macros (semantics are
 *     compatible: the posix layer only uses FD_ZERO/FD_SET/FD_CLR/FD_ISSET).
 */
#ifndef _SIM_SYS_SELECT_H
#define _SIM_SYS_SELECT_H

#ifndef ___WSA_FD_TYPES_H

#define _SYS_TYPES_FD_SET   /* stop a later windows.h from defining fd_set again */

#define FD_SETSIZE 32

typedef struct fd_set {
    unsigned int fds_bits;
} fd_set;

#define FD_ZERO(s)      ((s)->fds_bits = 0U)
#define FD_SET(f, s)    ((s)->fds_bits |= (1U << ((unsigned)(f))))
#define FD_CLR(f, s)    ((s)->fds_bits &= ~(1U << ((unsigned)(f))))
#define FD_ISSET(f, s)  (((s)->fds_bits & (1U << ((unsigned)(f)))) != 0U)

#endif /* !___WSA_FD_TYPES_H */

#endif /* _SIM_SYS_SELECT_H */
