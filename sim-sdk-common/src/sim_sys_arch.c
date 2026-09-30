/* SPDX-License-Identifier: BSD-3-Clause
 *
 * sim_sys_arch.c - simulator lwIP OS porting layer (stand-in)
 *
 * Differences from the SDK sys_arch.c:
 *   1. sys_arch_sem_wait:  uses xSemaphoreTake instead of osSemaphoreAcquire
 *   2. sys_arch_protect:   uses portDISABLE_INTERRUPTS instead of osKernelLock
 *   3. sys_arch_unprotect: uses portENABLE_INTERRUPTS instead of osKernelRestoreLock
 */
#include "lwip/opt.h"
#include "lwip/arch.h"
#if !NO_SYS
#include "arch/sys_arch.h"
#endif
#include "lwip/sys.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/timeouts.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/dns.h"
#include "lwip/nd6.h"

#include <string.h>
#include "net_debug.h"

#ifdef _WIN32
#include <windows.h>
#endif

void sys_init(void)
{
}

u32_t sys_now(void)
{
	return (u32_t)osKernelGetTickCount();
}

u32_t sys_jiffies(void)
{
	return sys_now();
}

#if !NO_SYS

static osThreadId_t tcpip_thd_h = NULL;

err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
	*mbox = osMessageQueueNew(size, sizeof(void *), NULL);
#if SYS_STATS
	++lwip_stats.sys.mbox.used;
	if (lwip_stats.sys.mbox.max < lwip_stats.sys.mbox.used)
	{
		lwip_stats.sys.mbox.max = lwip_stats.sys.mbox.used;
	}
#endif
	if (*mbox == NULL)
		return ERR_MEM;
	return ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
	if (osMessageQueueGetCount(*mbox) != 0)
	{
#if SYS_STATS
		lwip_stats.sys.mbox.err++;
#endif
	}

	osMessageQueueDelete(*mbox);

#if SYS_STATS
	--lwip_stats.sys.mbox.used;
#endif
}

void sys_mbox_post(sys_mbox_t *mbox, void *data)
{
	while (osMessageQueuePut(*mbox, &data, 0, osWaitForever) != osOK)
	{
	}
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
	MSG_QUEUE_USED_MAX_UPDATE(mbox_used_max, osMessageQueueGetCount(*mbox));

	if (osMessageQueuePut(*mbox, &msg, 0, osNoWait) == osOK)
	{
		return ERR_OK;
	}
	else
	{
#if SYS_STATS
		lwip_stats.sys.mbox.err++;
#endif
		IP_PAC_STATS_INC(mbox_fail)
		return ERR_MEM;
	}
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
	return sys_mbox_trypost(mbox, msg);
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
	void *dummyptr;
	u32_t StartTime, EndTime, Elapsed;

	StartTime = osKernelGetTickCount();

	if (msg == NULL)
	{
		msg = &dummyptr;
	}

	if (timeout != 0)
	{
		if (osOK == osMessageQueueGet(*mbox, &(*msg), NULL, timeout / OS_TICK_PERIOD_MS))
		{
			EndTime = osKernelGetTickCount();
			Elapsed = (EndTime - StartTime) * OS_TICK_PERIOD_MS;
			return (Elapsed);
		}
		else
		{
			*msg = NULL;
			return SYS_ARCH_TIMEOUT;
		}
	}
	else
	{
		while (osOK != osMessageQueueGet(*mbox, &(*msg), NULL, osWaitForever))
		{
		}
		EndTime = osKernelGetTickCount();
		Elapsed = (EndTime - StartTime) * OS_TICK_PERIOD_MS;

		return (Elapsed);
	}
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
	void *dummyptr;

	if (msg == NULL)
	{
		msg = &dummyptr;
	}

	if (osOK == osMessageQueueGet(*mbox, &(*msg), NULL, osNoWait))
	{
		return ERR_OK;
	}
	else
	{
		return SYS_MBOX_EMPTY;
	}
}

int sys_mbox_valid(sys_mbox_t *mbox)
{
	if (*mbox == SYS_MBOX_NULL)
		return 0;
	else
		return 1;
}

void sys_mbox_set_invalid(sys_mbox_t *mbox)
{
	*mbox = SYS_MBOX_NULL;
}

err_t sys_mutex_new(sys_mutex_t *mutex)
{
	*mutex = (sys_mutex_t)osMutexNew(NULL);
	if (*mutex == NULL)
	{
#if SYS_STATS
		++lwip_stats.sys.mutex.err;
#endif
		return ERR_MEM;
	}

#if SYS_STATS
	++lwip_stats.sys.mutex.used;
	if (lwip_stats.sys.mutex.max < lwip_stats.sys.mutex.used)
	{
		lwip_stats.sys.mutex.max = lwip_stats.sys.mutex.used;
	}
#endif
	return ERR_OK;
}

void sys_mutex_free(sys_mutex_t *mutex)
{
#if SYS_STATS
	--lwip_stats.sys.mutex.used;
#endif

	osMutexDelete(*mutex);
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
	osMutexAcquire(*mutex, osWaitForever);
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
	osMutexRelease(*mutex);
}

sys_prot_t sys_arch_protect(void)
{
	/*
	 * Simulator customization: use portDISABLE_INTERRUPTS /
	 * portENABLE_INTERRUPTS instead of osKernelLock /
	 * osKernelRestoreLock, leveraging the global mutex built into the
	 * Windows port layer (pvInterruptEventMutex) to achieve true
	 * cross-thread atomic protection.
	 *
	 * vTaskSuspendAll cannot prevent Windows threads from preempting in
	 * the simulator - other task threads can still call blocking APIs
	 * such as xSemaphoreTake inside the protected region, which in turn
	 * triggers FreeRTOS kernel assertions (tasks.c:2263, queue.c:1482).
	 *
	 * portDISABLE_INTERRUPTS -> vPortEnterCritical
	 *   -> WaitForSingleObject(pvInterruptEventMutex)
	 * and xSemaphoreTake -> taskENTER_CRITICAL -> vPortEnterCritical
	 *   -> the same lock -> other threads will be blocked on the lock
	 *      by the Windows kernel.
	 */
	if (osKernelGetState() != osKernelInactive && osKernelGetState() != osKernelReady)
	{
		portDISABLE_INTERRUPTS();
		return 0;
	}

	return -1;
}

void sys_arch_unprotect(sys_prot_t pval)
{
	(void)pval;

	if (osKernelGetState() != osKernelInactive && osKernelGetState() != osKernelReady)
	{
		portENABLE_INTERRUPTS();
	}
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio)
{
	osThreadId_t createdTask = NULL;
	osThreadAttr_t task_attr = {0};

	task_attr.name = (signed char *)name;
	task_attr.priority = prio;
	task_attr.stack_size = stacksize;
	createdTask = osThreadNew((osThreadFunc_t)(thread), arg, &task_attr);

	return createdTask;
}

err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
	*sem = osSemaphoreNew(0xFFFF, count, NULL);
	if (*sem == NULL)
	{
#if SYS_STATS
		++lwip_stats.sys.sem.err;
#endif
		return ERR_MEM;
	}

#if SYS_STATS
	++lwip_stats.sys.sem.used;
	if (lwip_stats.sys.sem.max < lwip_stats.sys.sem.used)
	{
		lwip_stats.sys.sem.max = lwip_stats.sys.sem.used;
	}
#endif

	return ERR_OK;
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
	u32_t StartTime, EndTime, Elapsed;

	StartTime = osKernelGetTickCount();

	if (timeout != 0)
	{
		if (pdTRUE == xSemaphoreTake(*sem, timeout / OS_TICK_PERIOD_MS))
		{
			EndTime = osKernelGetTickCount();
			Elapsed = (EndTime - StartTime) * OS_TICK_PERIOD_MS;
			return (Elapsed);
		}
		else
		{
			return SYS_ARCH_TIMEOUT;
		}
	}
	else
	{
		while (pdTRUE != xSemaphoreTake(*sem, portMAX_DELAY))
		{
			Sleep(1);
		}
		EndTime = osKernelGetTickCount();
		Elapsed = (EndTime - StartTime) * OS_TICK_PERIOD_MS;
		return (Elapsed);
	}
}

void sys_sem_signal(sys_sem_t *sem)
{
	osSemaphoreRelease(*sem);
}

void sys_sem_free(sys_sem_t *sem)
{
#if SYS_STATS
	--lwip_stats.sys.sem.used;
#endif
	osSemaphoreDelete(*sem);
}

int sys_sem_valid(sys_sem_t *sem)
{
	if (*sem == SYS_SEM_NULL)
		return 0;
	else
		return 1;
}

void sys_sem_set_invalid(sys_sem_t *sem)
{
	*sem = SYS_SEM_NULL;
}

#endif /* NO_SYS */