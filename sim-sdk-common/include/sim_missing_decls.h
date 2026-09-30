/*
 * sim_missing_decls.h -- centralized declarations for missing symbols
 * (injected into every compilation unit via -include)
 *
 * Some functions are provided on the real device by RISC-V-only headers
 * (core_rv32.h etc., containing inline assembly) which the host cannot
 * include; meanwhile the SDK sources referencing them include none of the
 * headers we control, and GCC 16 errors out on implicit declarations.
 * Declarations are injected here via -include, with implementations
 * gathered in sim_at_shims.c.
 */
#ifndef SIM_MISSING_DECLS_H
#define SIM_MISSING_DECLS_H

#include <stdint.h>

/* Kernel core timer count (about 32kHz on real device; @see core_rv32.h:752).
 * Host implementation: osKernelGetTickCount()*32, preserving the numeric
 * semantics of "32 ticks ~ 1ms", matching the +32=1ms usage in
 * at_passthrough.c. */
uint64_t csi_coret_get_value2(void);

/* D-cache maintenance (real device: csi core headers, RISC-V only; called
 * directly by ps_adapt.c). The host has no cache-coherency issues, so the
 * implementations are no-ops, see sim_at_shims.c. */
void csi_dcache_clean_range(void *addr, uint32_t size);
void csi_dcache_invalid_range(void *addr, uint32_t size);

/* Memory-heap detection functions of the multi-heap manager heap_10
 * (called by net_mem.c, ps_adapt.c, etc. when TLSF_HARDWARE=1). The
 * simulator uses a single heap_4 heap; IS_MEM_HARD_HEAP is always true.
 * On the real device these are declared in port_heap.h inside the
 * #if configHEAP_MANAGE_TYPE==10 gate, which the simulator's
 * HEAP_MANAGE_TYPE=4 never expands; declarations are supplied here. */
size_t IS_MEM_HARD_HEAP(void *pv);
size_t IS_MEM_LOCAL_SOFT_HEAP(void *pv);
size_t IS_MEM_OTHER_SOFT_HEAP(void *pv);

/* heap_10 cache maintenance (real device: RISC-V dcache flush, ps_adapt.c:75).
 * The host has no dcache; no-op stubs, see sim_net_shims.c */
void vPortMemCacheInvalid(void *addr);

/* Aligned allocation functions of the multi-heap manager (called by
 * net_mem.c when TLSF_HARDWARE=1). Simulator stubs call pvPortMalloc
 * directly, see sim_net_shims.c */
void *netMemoryAllocAlignWithRetaddr(size_t size);
void *usbnetMemoryAllocAlign(size_t size, unsigned int align);

/* Free function of net_mem.c (used by the sim_proxy_uplink callback in
 * sim_net_shims.c) */
void net_free(void *pMem);

/* USBX MAC address (real device reads it from the USB device controller,
 * see xy4101_usb_udc.h/ux_port.h; the host has no hardware, sim_shims.c
 * returns a fixed MAC) */
unsigned char *ux_get_mac_address(void);

#endif /* SIM_MISSING_DECLS_H */
