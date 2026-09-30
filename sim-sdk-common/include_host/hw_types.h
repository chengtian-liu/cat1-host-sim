/*
 * hw_types.h -- host override (replaces driverlib/inc/hw_types.h)
 *
 * On the real device, HWREG(x) dereferences a physical register address
 * directly; on the host these addresses are invalid (e.g.
 * RETENTION_CHIP_VERSION_INFO) and any read segfaults immediately.
 *
 * Host strategy: redirect all register reads/writes to a slice of RAM
 * "shadow registers".
 * Not semantically faithful (different addresses share one word), but:
 *   - in the simulator all real driver paths go through stubs, so nobody
 *     depends on register values;
 *   - the lvalue form keeps working (the SDK writes HWREG(addr) = val);
 *   - no invalid address is ever accessed.
 *
 * The include guard matches the real-device version (__HW_TYPES_H__), so the
 * real-device header arriving later is automatically disabled.
 */
#ifndef __HW_TYPES_H__
#define __HW_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Shadow registers (defined in sim_at_shims.c) */
extern uint32_t sim_hwreg_scratch32;
extern uint16_t sim_hwreg_scratch16;
extern uint8_t  sim_hwreg_scratch8;
extern uint64_t sim_hwreg_scratch64;

#define HWREG(x)   (sim_hwreg_scratch32)
#define HWREGH(x)  (sim_hwreg_scratch16)
#define HWREGB(x)  (sim_hwreg_scratch8)
#define HWREGL(x)  (sim_hwreg_scratch64)

#define HWD_REG_WRITE32(addr, data) (sim_hwreg_scratch32 = (uint32_t)(data))
#define HWD_REG_WRITE16(addr, data) (sim_hwreg_scratch16 = (uint16_t)(data))
#define HWD_REG_WRITE08(addr, data) (sim_hwreg_scratch8  = (uint8_t)(data))

#define HWD_REG_READ32(addr)        (sim_hwreg_scratch32)
#define HWD_REG_READ16(addr)        (sim_hwreg_scratch16)
#define HWD_REG_READ08(addr)        (sim_hwreg_scratch8)

#endif /* __HW_TYPES_H__ */
