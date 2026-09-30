/*
 * sim_core_api.h -- simulation core abstract interface
 *
 * sim-core is a pure Win32 module with no dependency on any chip SDK (such as
 * xy_system.h / net_mem.h / pkt_process.h, etc.). All operations that need SDK
 * capabilities (memory allocation, downlink injection, RTOS thread creation,
 * etc.) go through the function-pointer callbacks defined in this file, bound
 * by the target-xxx directories at startup.
 *
 * Usage:
 *   1. main() calls sim_core_init(&cfg) or the setter functions to configure
 *      the callbacks
 *   2. afterwards sim-core internally calls the target functions indirectly
 *      via g_sim_fn->xxx()
 */

#ifndef SIM_CORE_API_H
#define SIM_CORE_API_H

#include <stddef.h>
#include <stdint.h>

/* ---- memory allocation callbacks ---- */
typedef void *(*sim_malloc_fn) (size_t size);
typedef void  (*sim_free_fn)   (void *ptr);

/* ---- downlink injection callback ---- */
/* buf is allocated by the injecting side (sim-core) via the malloc above;
 * after injection the receiving side (target layer) must free it via the
 * free above -> net_free, returning it to the lwIP pool */
typedef void (*sim_inject_fn)(const void *data, uint16_t len);

/* ---- RTOS delay callback (emulates osDelay) ---- */
typedef void (*sim_delay_fn)(uint32_t ms);

/* ---- downlink injection ring queue slot type ---- */
/* Defined by sim-core; the target layer only binds:
 *   buf -> raw IP packet (malloc-allocated; target must free after injection)
 *   len -> IP packet length in bytes */
typedef struct {
    void    *buf;
    uint16_t len;
} sim_dl_slot_t;

/* ---- callback table ---- */
typedef struct {
    sim_malloc_fn  malloc;
    sim_free_fn    free;
    sim_inject_fn  inject;
    sim_delay_fn   delay;
} sim_core_callbacks_t;

/* ---- API ---- */

/*
 * Configure the callback table (must be called before sim_proxy_start()).
 * If no RTOS delay is needed (pure polling thread), delay may be NULL
 * (internally degrades to busy-wait). malloc / free / inject must all be
 * non-NULL, otherwise the table is considered not ready.
 */
void sim_core_set_callbacks(const sim_core_callbacks_t *cb);

/*
 * Get the current callback table (used internally by modules such as sim_proxy).
 * Returns an all-NULL default table when not configured.
 */
const sim_core_callbacks_t *sim_core_get_callbacks(void);

/*
 * Query whether the callback table is correctly configured (ready as soon as
 * malloc / free / inject are non-NULL). sim_proxy_start() performs this check
 * before starting, to prevent wild-pointer crashes.
 * @return non-zero = ready, 0 = not configured or missing required callbacks
 */
int sim_core_callbacks_ready(void);

#endif /* SIM_CORE_API_H */
