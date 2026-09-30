/*
 * performance_monitor.h -- host shim (replaces platform/arch/lib/rv32/inc/performance_monitor.h)
 *
 * The real device provides access to RISC-V performance counters such as
 * mcycle/mhpmcounter. On the host we keep only get_sysclk_freq(), the one most
 * likely to be referenced (returns a nominal clock frequency).
 */

#ifndef __PERFORMANCE_MONITOR_SHIM_H__
#define __PERFORMANCE_MONITOR_SHIM_H__

#include <stdint.h>

static inline uint32_t get_sysclk_freq(void)
{
    /* Nominal clock frequency; only used by a few assertions/conversions,
     * not sensitive on the host */
    return 256000000U;
}

#endif /* __PERFORMANCE_MONITOR_SHIM_H__ */
