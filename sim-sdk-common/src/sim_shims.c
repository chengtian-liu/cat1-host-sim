/*
 * sim_shims.c - host-side stub implementations (SDK source unmodified)
 *
 * All "non-business" symbols needed to link the real FreeRTOS +
 * cmsis_os2 (osal.c) on a Windows host:
 *   - sim_assert_fail()          landing point for Sys_Assert / configASSERT / OSAL_ASSERT
 *   - FeedUtcWatchdog() etc.     UTC watchdog (host has no hardware WDT)
 *   - g_flash_status + the flash trio (on the context-switch path)
 *   - malloc2_adapt / xy_zalloc* memory adapters (all routed through the
 *                                 FreeRTOS heap_4, guaranteeing the
 *                                 xy_free -> osMemoryFree -> vPortFree pairing)
 *
 * The factory NV (g_softap_fac_nv) has moved to sim_factory_nv.c; a
 * target may override chip-specific fields at startup via
 * sim_target_hooks_t.factory_nv_init.
 *
 * Note: send_debug_str_to_ext is NOT here - its real definition lives in
 * at_ctl_basic.c (compiled into xyat from Phase C onward); redefining it
 * here would cause a link conflict.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys_assert.h"
#include "flash_proxy.h"
#include "cmsis_os2.h"

/* ------------------------------------------------------------------ */
/* Assertions                                                         */
/* ------------------------------------------------------------------ */

void sim_assert_fail(const char *file, int line, const char *expr)
{
    fprintf(stderr, "\n[SIM-ASSERT] %s:%d  (%s)\n", file, line, expr);
    fflush(stderr);
    abort();
}

/* ------------------------------------------------------------------ */
/* UTC watchdog (real device: misc/src/utc_watchdog.c; host no-ops)  */
/* ------------------------------------------------------------------ */

void FeedUtcWatchdog(uint32_t sec)
{
    (void)sec;
}

void FeedUtcWatchdogForLPM(void)
{
}

void FeedWatchdogByUser(uint8_t enable)
{
    (void)enable;
}

/* ------------------------------------------------------------------ */
/* Flash proxy (context-switch path)                                 */
/* ------------------------------------------------------------------ */

flash_PEstatus g_flash_status = flash_Idle;

void suspend_flash_operation(void)
{
}

void flash_protect_switch(void)
{
}

void resume_flash_operation(void)
{
}

/* The factory NV has moved to sim_factory_nv.c - common default values +
 * target override callback */

/* ------------------------------------------------------------------ */
/* FreeRTOS heap memory (replaces heap_4.c; uses a critical section   */
/* instead of vTaskSuspendAll for Win32 thread safety, avoiding the   */
/* configASSERT triggered by the multi-threaded Windows port)         */
/* ------------------------------------------------------------------ */

#include "FreeRTOS.h"
#include "portmacro.h"

void *pvPortMalloc(size_t xWantedSize)
{
    void *pvReturn;
    void *pvOriginal;
    const size_t xAlignment = portBYTE_ALIGNMENT;
    const size_t xTotalSize = xWantedSize + xAlignment + sizeof(void *);

    vPortEnterCritical();
    pvOriginal = malloc(xTotalSize);

    if (pvOriginal != NULL) {
        pvReturn = (void *)(((uintptr_t)pvOriginal + xAlignment + sizeof(void *))
                            & ~((uintptr_t)(xAlignment - 1)));
        ((void **)pvReturn)[-1] = pvOriginal;
    } else {
        pvReturn = NULL;
    }

    vPortExitCritical();
    return pvReturn;
}

void vPortFree(void *pv)
{
    if (pv != NULL) {
        void *pvOriginal = ((void **)pv)[-1];

        vPortEnterCritical();
        free(pvOriginal);
        vPortExitCritical();
    }
}

size_t xPortGetFreeHeapSize(void)
{
    return (size_t)0xFFFFFFFFUL;
}

/* ------------------------------------------------------------------ */
/* Memory adapters (declared in xy_system.h)                         */
/* ------------------------------------------------------------------ */

#if (FreeRTOS_HEAP_MALLOC_WITH_RECORD == 0)
void *malloc2_adapt(size_t size, int align)
{
    (void)align;
    return pvPortMalloc(size);
}
#endif

void *xy_zalloc(uint32_t size)
{
    void *p = pvPortMalloc(size);

    if (p != NULL) {
        memset(p, 0, size);
    } else {
        Sys_Assert(0);
    }
    return p;
}

void *xy_zalloc2(uint32_t size)
{
    void *p = pvPortMalloc(size);

    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

/* ================================================================== */
/* Thread low-power flag                                             */
/* ================================================================== */

osStatus_t osThreadSetLowPowerFlag(osThreadId_t thread_id, osLowPowerType_t lpm_flag)
{
    (void)thread_id;
    (void)lpm_flag;
    return osOK;
}

/* ------------------------------------------------------------------ */
/* USBX MAC address (real device reads it from the USB device         */
/* controller; host uses a fixed MAC)                                 */
/* ------------------------------------------------------------------ */

unsigned char *ux_get_mac_address(void)
{
    static unsigned char mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    return mac;
}