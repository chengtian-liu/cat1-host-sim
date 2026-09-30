/*
 * sim_xy4101_bridge.c -- callback bindings from the XY4101 chip to sim-core
 *
 * Binds sim-core's abstract callbacks (memory allocation, downlink injection,
 * RTOS delay) to the concrete XY4101 SDK implementations. Also responsible
 * for creating the proxy_rx FreeRTOS task.
 *
 * Dependencies: xy_system.h / net_mem.h / pkt_process.h / icc_msg.h / sim_proxy.h / sim_core_api.h
 */

#include "xy_system.h"
#include "net_mem.h"
#include "pkt_process.h"
#include "icc_msg.h"

#include "sim_proxy.h"        /* sim_core_proxy_rx_loop */
#include "sim_core_api.h"     /* sim_core_set_callbacks */
#include "sim_log.h"

/* ---- Callback implementations ---- */

static void *xy4101_malloc(size_t size)
{
    return net_malloc(size);
}

static void xy4101_free(void *ptr)
{
    if (ptr != NULL)
        net_free(ptr);
}

static void xy4101_inject(const void *data, uint16_t len)
{
    Ps_Ipdata_Info_T ipinfo;

    memset(&ipinfo, 0, sizeof(ipinfo));
    ipinfo.ip_pkt.data     = (uint32_t)(uintptr_t)data;
    ipinfo.ip_pkt.data_len = len;
    ipinfo.ip_pkt.cid      = 1;
    ipinfo.ip_pkt.direct   = 0;
    icc_data_channel_write(ICC_DATA_IP, &ipinfo, sizeof(ipinfo));
}

/* ---- proxy_rx FreeRTOS task ---- */

static void xy4101_proxy_rx_task(void *arg)
{
    (void)arg;
    sim_core_proxy_rx_loop();
}

void xy4101_proxy_rx_start(void)
{
    osThreadAttr_t attr;

    memset(&attr, 0, sizeof(attr));
    attr.name       = "proxy_rx";
    attr.priority   = osPriorityNormal;
    attr.stack_size = 16384;

    if (osThreadNew(xy4101_proxy_rx_task, NULL, &attr) == NULL) {
        LOGE("BRIDGE", "cannot spawn proxy_rx task");
    }
}

/* ---- Callback binding entry ---- */

void xy4101_bridge_init(void)
{
    sim_core_callbacks_t cb;

    memset(&cb, 0, sizeof(cb));
    cb.malloc = xy4101_malloc;
    cb.free   = xy4101_free;
    cb.inject = xy4101_inject;
    cb.delay  = (sim_delay_fn)osDelay;

    sim_core_set_callbacks(&cb);
}
