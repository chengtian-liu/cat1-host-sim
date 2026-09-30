/*
 * csi_device.h -- host shim (replaces platform/arch/lib/include/csi_device.h)
 *
 * On the real device it pulls in chip/chip.h + csi_core.h + interrupt.h
 * (T-Head RISC-V CSR access). On the host only a few inline functions used by
 * osal/kernel are needed:
 *   - risc_v_get_interrupt_code()  always 0: the host is always in thread context
 *   - __get_MSTATUS()              always 0x8 (MIE=1): IS_IRQ_MASKED() evaluates to "not masked"
 *   - __get_MCYCLE()               monotonically increasing count: emulated with x86 rdtsc
 *
 * Also, the driverlib register headers (xy4101_ll_reg_*.h, pulled in via
 * xy4101.h) use CMSIS-style __I/__O/__IO qualifiers. On the real device these
 * come from the csi core headers; here we provide equivalents. These register
 * structs only participate in compilation on the host and are never
 * dereferenced.
 */

#ifndef CSI_DEVICE_H
#define CSI_DEVICE_H

#include <stdint.h>

#if defined(__i386__) || defined(__x86_64__)
/* Must be included before the __I/__O/__IO macros are defined: GCC's
 * intrinsics header uses __I etc. as parameter names */
#include <x86intrin.h>
#endif

#ifndef __I
#define __I     volatile const   /* read-only register */
#endif
#ifndef __O
#define __O     volatile         /* write-only register */
#endif
#ifndef __IO
#define __IO    volatile         /* read-write register */
#endif

/* CMSIS-style inline qualifiers (used by driverlib's LL headers) */
#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif
#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
#endif

static inline uint32_t risc_v_get_interrupt_code(void)
{
    return 0U;
}

static inline uint32_t __get_MSTATUS(void)
{
    /* bit3 = MIE (interrupt enable). Returning 0x8 makes IS_IRQ_MASKED() == false */
    return 0x8U;
}

static inline uint64_t __get_MCYCLE(void)
{
#if defined(__i386__) || defined(__x86_64__)
    return (uint64_t)__rdtsc();
#else
    static uint64_t fake_cycle = 0;
    return ++fake_cycle;
#endif
}

static inline uint32_t __get_MCAUSE(void)
{
    return 0U;
}

#endif /* CSI_DEVICE_H */
