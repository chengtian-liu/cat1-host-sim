/*
 * FreeRTOSConfig.h -- host-simulation-only configuration (SDK original left untouched)
 *
 * Placed before the SDK's platform/kernel/os/FreeRTOS/config/ in -I order,
 * so FreeRTOS.h's #include "FreeRTOSConfig.h" hits this file.
 *
 * Differences vs. the real-device configuration:
 *   - configUSE_NEWLIB_REENTRANT      1 -> 0 (host has no newlib reent)
 *   - configHEAP_MANAGE_TYPE         10 -> 4 (standard heap_4, no TLSF/hardware heap)
 *   - configUSE_LOW_POWER_FLAG        1 -> 0 (low-power statistics/extension APIs all trimmed)
 *   - configUSE_TIMER_LPM_STATISTICS  1 -> 0
 *   - configGENERATE_RUN_TIME_STATS   1 -> 0 (run-time stats use ticks instead, no mcycle)
 *   - configUSE_IDLE_HOOK             1 -> 0 (implemented in the low-power module on real device, not needed on host)
 *   - configCHECK_FOR_STACK_OVERFLOW  2 -> 0 (task stacks in the Posix port are just bookkeeping memory)
 *   - configUSE_PORT_OPTIMISED_TASK_SELECTION 1 -> 0 (Posix port has no CLZ optimization)
 *   - configTOTAL_HEAP_SIZE enlarged (host memory is plentiful; thread stacks use host pthread stacks)
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "sys_assert.h"

/***************************************************************************************************************/
/*                                        FreeRTOS assert function config                                      */
/***************************************************************************************************************/
#define configASSERT( x )                          Sys_Assert( x )         // assert function

/***************************************************************************************************************/
/*                                        FreeRTOS base configuration options                                  */
/***************************************************************************************************************/
#define configUSE_PREEMPTION                        1
#define configUSE_TIME_SLICING                      1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION     0                   // Posix port has no hardware selection instructions
#define configUSE_TICKLESS_IDLE                     0
#define configUSE_QUEUE_SETS                        1
#define configCPU_CLOCK_HZ                          (1000000000UL)      // placeholder only; Posix port does not depend on it
#define configTICK_RATE_HZ                          (1000)
#define configMAX_PRIORITIES                        (32)
#define configMAX_TASK_NAME_LEN                     (16)
#define configUSE_16_BIT_TICKS                      0
#define configIDLE_SHOULD_YIELD                     1
#define configUSE_TASK_NOTIFICATIONS                1
#define configUSE_MUTEXES                           1
#define configQUEUE_REGISTRY_SIZE                   0
#define configCHECK_FOR_STACK_OVERFLOW              0                   // Posix port: task stacks are host pthread stacks
#define configRECORD_STACK_HIGH_ADDRESS             1
#define configUSE_RECURSIVE_MUTEXES                 1
#define configUSE_APPLICATION_TASK_TAG              0
#define configUSE_COUNTING_SEMAPHORES               1
#define configUSE_NEWLIB_REENTRANT                  0                   // host has no newlib reent

/***************************************************************************************************************/
/*                                FreeRTOS memory allocation config                                            */
/***************************************************************************************************************/
#define configSUPPORT_STATIC_ALLOCATION             1
#define configSUPPORT_DYNAMIC_ALLOCATION            1
#define configTOTAL_HEAP_SIZE                       (1024*1024*4)       // host memory is plentiful
#define configAPPLICATION_ALLOCATED_HEAP            0

/***************************************************************************************************************/
/*                                FreeRTOS hook function config                                                */
/***************************************************************************************************************/
#define configUSE_IDLE_HOOK                         0                   // implemented in the low-power module on real device, unused on host
#define configUSE_TICK_HOOK                         0
#define configUSE_MALLOC_FAILED_HOOK                0
#define configUSE_DAEMON_TASK_STARTUP_HOOK          0

/***************************************************************************************************************/
/*                                FreeRTOS running time and task status collection config                      */
/***************************************************************************************************************/
#define configGENERATE_RUN_TIME_STATS               0
#define configINTERRUPT_RECORD_TIME                 0
#define configCRITICAL_RECORD_TIME                  0
#define configUSE_TRACE_FACILITY                    1
#define configUSE_STATS_FORMATTING_FUNCTIONS        1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE()            xTaskGetTickCount()

/***************************************************************************************************************/
/*                                FreeRTOS coroutine config                                                    */
/***************************************************************************************************************/
#define configUSE_CO_ROUTINES                       0
#define configMAX_CO_ROUTINE_PRIORITIES             2

/***************************************************************************************************************/
/*                                FreeRTOS software timer config                                               */
/***************************************************************************************************************/
#define configTIMER_SERVICE_TASK_NAME               "Tmr Svc"
#define configUSE_TIMERS                            1
#define configTIMER_TASK_PRIORITY                  (configMAX_PRIORITIES-1)
#define configTIMER_QUEUE_LENGTH                    10
#define configTIMER_TASK_STACK_DEPTH                0x150               // bookkeeping only under the Posix port

/***************************************************************************************************************/
/*                                FreeRTOS idle config                                                         */
/***************************************************************************************************************/
#define configIDLE_TASK_NAME                        "IDLE"
#define configMINIMAL_STACK_SIZE                    0x150               // bookkeeping only under the Posix port

/***************************************************************************************************************/
/*                                FreeRTOS include functions                                                   */
/***************************************************************************************************************/
#define INCLUDE_xTaskGetSchedulerState              1
#define INCLUDE_vTaskPrioritySet                    1
#define INCLUDE_uxTaskPriorityGet                   1
#define INCLUDE_vTaskDelete                         1
#define INCLUDE_vTaskCleanUpResources               1
#define INCLUDE_vTaskSuspend                        1
#define INCLUDE_vTaskDelayUntil                     1
#define INCLUDE_vTaskDelay                          1
#define INCLUDE_xTaskGetHandle                      1
#define INCLUDE_eTaskGetState                       1
#define INCLUDE_uxTaskGetStackHighWaterMark         1
#define INCLUDE_xTaskGetIdleTaskHandle              1
#define INCLUDE_xTaskGetCurrentTaskHandle           1
#define INCLUDE_xQueueGetMutexHolder                1
#define INCLUDE_xTimerPendFunctionCall              1

/***************************************************************************************************************/
/*                                FreeRTOS Heap config (host: standard heap_4)                                 */
/***************************************************************************************************************/
#define configHEAP_MANAGE_TYPE                      4                   // heap_4.c
#define configUSE_HEAP_USED_LINKED_LIST             0
#define configHEAP_MEMORY_OVERFLOW_CHECK            0
#define configHEAP_RECORD_TYPE                      0
#define configGET_RETURN_ADDRESS()                  __builtin_return_address(0)
#define configUSE_HEAP_MALLOC_DEBUG                 0
#define configUSE_HEAP_TRACE_RECORD                 0
#define configHEAP_TRACE_RECORD_NUM                 0

/* heap_4 has no aligned-allocation wrapper, but osal.c's osMemoryAllocAlign needs this macro:
 * on the host there are no DMA/cache alignment requirements, so use standard allocation directly. */
#define pvPortAdaptMallocAlign( size, align )       pvPortMalloc( size )

/***************************************************************************************************************/
/*                                Custom FreeRTOS for xinyi                                                    */
/***************************************************************************************************************/
#define configUSE_LOW_POWER_FLAG                    0                   // low-power extensions trimmed entirely
#define configUSE_TICK_INTERRUPT_LESS               0
#define configUSE_TIMER_LPM_STATISTICS              0

/***************************************************************************************************************/
/*                                FreeRTOS trace debug config                                                  */
/***************************************************************************************************************/
#include "osal_statistics.h"
#include "osal_cpu_utilization.h"

#endif /* FREERTOS_CONFIG_H */
