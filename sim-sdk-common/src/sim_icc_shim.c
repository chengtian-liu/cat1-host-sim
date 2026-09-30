/*
 * sim_icc_shim.c - inter-core communication (ICC) shim
 *
 * On the real device ICC is a hardware shared-memory channel between the
 * AP/CP dual cores. The simulator has no second core, so
 * icc_data_channel_register only stores the callback pointer and
 * icc_data_channel_write invokes it directly - equivalent to the
 * semantics of "instant delivery to the CP core".
 *
 * Data flow:
 *   proxy_rx task (receives downlink IP packets)
 *     -> icc_data_channel_write(ICC_DATA_IP, Ps_Ipdata_Info_T)
 *     -> dlAllDataRecvFromPs / dlDataRecvFromPs (the registered callback)
 *     -> dlPktMsgSendToTask -> dlPktMsgProcTask -> pppIpDataInput / packet_recved_from_wan
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "icc_msg.h"
#include "sim_log.h"

#define ICC_TAG "SIMICC"

/* At most one callback per message type (same semantics as real-device ICC) */
static icc_cb_t g_icc_data_cb[ICC_DATA_MAX];

void icc_data_channel_register(icc_data_msg_t msg_id, icc_cb_t callback)
{
    if (msg_id >= ICC_DATA_MAX) {
        LOGE(ICC_TAG, "icc_data_channel_register: invalid msg_id=%d", (int)msg_id);
        return;
    }

    g_icc_data_cb[msg_id] = callback;
    LOGI(ICC_TAG, "icc_data_channel_register msg_id=%d cb=%p",
         (int)msg_id, (void *)(uintptr_t)callback);
}

size_t icc_data_channel_write(icc_data_msg_t msg_id, void *data, size_t size)
{
    if (msg_id >= ICC_DATA_MAX) {
        LOGE(ICC_TAG, "icc_data_channel_write: invalid msg_id=%d", (int)msg_id);
        return 0;
    }

    if (g_icc_data_cb[msg_id] == NULL) {
        LOGW(ICC_TAG, "icc_data_channel_write: no callback for msg_id=%d", (int)msg_id);
        return 0;
    }

    g_icc_data_cb[msg_id](data, size);
    return size;
}