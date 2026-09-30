/*
 * flash_proxy.h -- host shim (replaces platform/kernel/flash_daemon/inc/flash_proxy.h)
 *
 * The real-device flash proxy (erase/write suspend/resume, protection switch)
 * does not exist on the host: tasks.c's context switch checks g_flash_status
 * and calls suspend_flash_operation()/flash_protect_switch(). Minimal symbols
 * are provided here, all no-ops.
 *
 * Note: these must be real externally-linked functions (not static inline) --
 * tasks.c contains an internal redeclaration `extern void flash_protect_switch(void);`,
 * which combined with static inline would require an external definition.
 */

#ifndef __FLASH_PROXY_SHIM_H__
#define __FLASH_PROXY_SHIM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    flash_Idle = 0,
    flash_Erasing,
    flash_EraseSuspend,
    flash_Programming,
    flash_ProgramSuspend,
} flash_PEstatus;

extern flash_PEstatus g_flash_status;

void suspend_flash_operation(void);
void flash_protect_switch(void);
void resume_flash_operation(void);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_PROXY_SHIM_H__ */
