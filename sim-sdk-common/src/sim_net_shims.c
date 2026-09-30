/*
 * sim_net_shims.c -- symbol stubs required to link lwip/network layer on host
 *
 * Same principle as sim_at_shims.c: do not modify or copy SDK sources; only
 * supply dependencies "outside the network subsystem". Each stub notes where
 * the real device defines it. Expanded step by step as phases progress:
 *   Phase 1: lwip compile-time dependencies (TCP_WND global, random)
 *   Phase 2: net_adapt dependencies (ps_is_oos, net_mem, entities behind
 *            statistics macros, etc.)
 *   Phase 4: API_Send_Data_2_PS (uplink exit -> fake CP data pump;
 *            from N1 it points to the in-process userspace proxy sim_proxy,
 *            the Wintun pump is retired)
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "xy_system.h"
#include "ps_external.h"
#include "pdp_dial.h"
#include "ps_netif_api.h"
#include "xy_wan_api.h"
#include "softap_nv.h"
#include "app_basic_config.h"
#include "at_mqtt_api.h"

#include "sim_log.h"
#include "sim_proxy.h"
#include "sim_vps_net.h"

/* ================================================================== */
/* TCP receive window global                                           */
/* Real device: platform/application/common/src/app_basic_config.c:102 */
/* (refreshed after boot from g_app_basic_cfg->local_tcp_rcv_wnd)      */
/* Host: use the default value 65535 (DEFAULT_TCP_WND) directly        */
/* ================================================================== */

uint16_t g_local_tcp_rcv_wnd = 65535;

/* ================================================================== */
/* Random numbers (referenced by LWIP_RAND in lwip cc.h)               */
/* Real device: xy_rand (TRNG driver); host: C library rand() suffices */
/* ================================================================== */

uint32_t xy_rand(void)
{
    return (uint32_t)rand();
}

/* ================================================================== */
/* net_mem.c dependency stubs (net_mem.c is already compiled into the  */
/* simulator with TLSF_HARDWARE=1)                                     */
/* The simulator heap_4 does not distinguish hard/soft heap:           */
/* IS_MEM_HARD_HEAP always returns 1,                                  */
/* net_free -> osMemoryFree -> vPortFree, same chain as real device    */
/* ================================================================== */

size_t IS_MEM_HARD_HEAP(void *pv)
{
    (void)pv;
    return 1;
}

size_t IS_MEM_LOCAL_SOFT_HEAP(void *pv)
{
    (void)pv;
    return 0;
}

size_t IS_MEM_OTHER_SOFT_HEAP(void *pv)
{
    (void)pv;
    return 0;
}

void *netMemoryAllocAlignWithRetaddr(size_t size)
{
    return pvPortMalloc(size);
}

void *usbnetMemoryAllocAlign(size_t size, osMemAlign_t align)
{
    (void)align;
    return pvPortMalloc(size);
}

size_t xPortGetFreeHeapSize_Hard(void)
{
    return xPortGetFreeHeapSize();
}

#ifdef xy_malloc_align2
#undef xy_malloc_align2
#endif
void *xy_malloc_align2(size_t size)
{
    return pvPortMalloc(size);
}

void vPortMemCacheInvalid(void *addr)
{
    (void)addr;
}

/* ================================================================== */
/* Uplink control (real device: pdp_dial.c / pkt_process.c /           */
/* xy_net_tools.c)                                                     */
/* Host: never out of service, never rate-limited, no black/white      */
/* lists, no ACK window tuning                                         */
/* ================================================================== */

bool ps_is_oos(void)                    /* real device: pdp_dial.c */
{
    return false;
}

bool is_Uplink_FlowCtl_Open(void)       /* real device: pdp_dial.c */
{
    return false;
}

bool whiteBlackListFilterUlIpPkt(void *pIpdata, int ipdataLen)  /* real device: xy_net_tools.c:678 */
{
    (void)pIpdata; (void)ipdataLen;
    return false;   /* false = pass through */
}

int adjustTcpServAckWnd(void *ipdata)   /* real device: xy_net_tools.c */
{
    (void)ipdata;
    return 0;
}

/* ================================================================== */
/* Downlink black/white list (real device: xy_net_tools.c:726/775;
 * conditionally compiled, returns false when MULTI_SUBNET=0)           */
/* Host: no black/white list, always pass through                       */
/* ================================================================== */

bool whiteBlackListFilterDlIpPkt(void *pIpdata, int ipdataLen)
{
    (void)pIpdata; (void)ipdataLen;
    return false;
}

/* ================================================================== */
/* Downlink rate-limit variables (real device: xy_net_tools.c:35/37/40) */
/* Host: no rate limiting; variables stay off/empty                     */
/* ================================================================== */

int               g_DL_limit_open = 0;
uint32_t          g_DL_limit_rate = 0;
osSemaphoreId_t   g_DL_rate_sema  = NULL;

/* PPP stubs removed -- real device ppp_server.c compiled directly (see CMakeLists.txt) */

/* ================================================================== */
/*                                                                     */
/* Real device chain: data_call_status_ind (lwip tcpip thread, from    */
/* wan_eth_status_callback) -> send_msg_2_proxy -> main_proxy thread   */
/* data_call_status_cb_inner -> walks datacall_cblist running user     */
/* callbacks. The host has no main_proxy, so dispatch happens          */
/* synchronously right in ind; registry structure, dedup and dispatch  */
/* conditions match pdp_dial.c line by line (including 0xFF -> default */
/* CID substitution and the semantics of EVENT_PHY_STATE broadcast to  */
/* all cids).                                                          */
/* Callbacks run in the lwip tcpip thread -- consistent with the real  */
/* device constraint "do not block inside callbacks".                  */
/* ================================================================== */

typedef struct dataCallCbList
{
    uint8_t             cid;         /* CID_MIN_VAL..CID_MAX_VAL, 0xFF=default CID */
    uint8_t             padding[3];
    uint32_t            eventGroup;  /* DataCall_Status_T bit combination */
    dataCallStatusCB_T  callback;
    struct dataCallCbList *next;
} dataCallCbList_T;

static dataCallCbList_T *datacall_cblist = NULL;
/* datacall_cblist_mux is a real-device global (defined in ps_netif_api.c:35,
 * created by wan_init(), osMutexRecursive). ps_netif_api.c is already
 * compiled in, so we reuse it here via the extern declaration in
 * ps_netif_api.h -- the same lock as on the real device */

static void sim_cblist_lock(void)
{
    /* Same as real-device reg: no mutex before the kernel starts */
    if (osKernelGetState() == osKernelInactive)
        return;
    if (datacall_cblist_mux != NULL)
        osMutexAcquire(datacall_cblist_mux, osWaitForever);
}

static void sim_cblist_unlock(void)
{
    if (datacall_cblist_mux != NULL)
        osMutexRelease(datacall_cblist_mux);
}

/* Real-device prototype in ps_netif_api.h; lwip tcpip thread context */
void data_call_status_ind(DataCall_Status_T eventId, uint8_t cid)
{
    dataCallCbList_T *tmp;

    if (datacall_cblist == NULL)
        return;

    LOGI("SIMNET", "data_call_status_ind event=0x%x cid=%u", (unsigned)eventId, (unsigned)cid);

    sim_cblist_lock();
    for (tmp = datacall_cblist; tmp != NULL; tmp = tmp->next)
    {
        /* 0xFF means subscribed to the default CID (same semantics as real
         * device, substituted in place) */
        if (tmp->cid == 0xFF)
            tmp->cid = xy_get_DefWan_Cid();

        /* Physical-layer events (OOS/IS) are cid-independent; broadcast to
         * all subscribers */
        if ((tmp->cid == cid || ((uint32_t)eventId & EVENT_PHY_STATE)) &&
            tmp->callback != NULL &&
            ((uint32_t)eventId & tmp->eventGroup))
            tmp->callback((uint32_t)eventId, tmp->cid);
    }
    sim_cblist_unlock();
}

void reg_data_call_status_cb(uint8_t cid, uint32_t eventGroup, dataCallStatusCB_T callback)
{
    dataCallCbList_T *cur;

    sim_cblist_lock();

    for (cur = datacall_cblist; cur != NULL; cur = cur->next) {
        /* Do not register the same triple twice (same semantics as real device) */
        if (cur->cid == cid && cur->eventGroup == eventGroup && cur->callback == callback)
            break;
    }

    if (cur == NULL) {
        cur = (dataCallCbList_T *)xy_malloc(sizeof(dataCallCbList_T));
        cur->cid = cid;
        cur->padding[0] = cur->padding[1] = cur->padding[2] = 0;
        cur->callback = callback;
        cur->eventGroup = eventGroup;
        cur->next = datacall_cblist;
        datacall_cblist = cur;
        LOGI("SIMNET", "reg_data_call_status_cb cid=%u event=0x%x cb=%p",
             (unsigned)cid, (unsigned)eventGroup, (void *)(uintptr_t)callback);
    }

    sim_cblist_unlock();
}

bool dereg_data_call_status_cb(uint8_t cid, uint32_t eventGroup, dataCallStatusCB_T callback)
{
    dataCallCbList_T *prev = NULL;
    dataCallCbList_T *cur;

    if (datacall_cblist == NULL)
        return false;

    sim_cblist_lock();
    for (cur = datacall_cblist; cur != NULL; prev = cur, cur = cur->next) {
        if (cur->cid == cid && cur->eventGroup == eventGroup && cur->callback == callback) {
            if (cur == datacall_cblist)
                datacall_cblist = cur->next;
            else
                prev->next = cur->next;
            xy_free(cur);
            sim_cblist_unlock();
            return true;
        }
    }
    sim_cblist_unlock();
    return false;
}

/* ================================================================== */
/* Uplink exit: send IP packet to CP core (real device: LtePs          */
/* cross-core interface)                                               */
/* Host semantics: buffer ownership transfers to the "CP side" (fake   */
/* CP data pump); after processing, net_free(pData) must be called.    */
/* N1: sim_proxy_uplink copies into the uplink ring internally, then   */
/* we free here (Phase 4 used to go through the Wintun send ring, now  */
/* replaced by the in-process userspace proxy).                        */
/* ================================================================== */

E_API_RSLT API_Send_Data_2_PS(unsigned char ucCid, unsigned char ucRai,
                              unsigned char ucDataType, unsigned short usDdataLen,
                              unsigned char *pData, unsigned long ulIpSn,
                              unsigned char ucIsTcpAck)
{
    (void)ucRai; (void)ucDataType; (void)ulIpSn; (void)ucIsTcpAck;

    if (pData == NULL)
        return E_API_NULL_PNT;

    if (sim_proxy_uplink(pData, usDdataLen) != 0) {
        /* Pump not ready (PDP not activated, etc.): drop and warn */
        LOGW("SIMNET", "API_Send_Data_2_PS cid=%u len=%u - drop (no data path)",
             (unsigned)ucCid, (unsigned)usDdataLen);
    }

    net_free(pData);
    return E_API_OK;
}

/* ================================================================== */
/* Variable NV (real device: flash retention instance, used for        */
/* deep-sleep wake recovery)                                           */
/* Host: RAM instance. The netif status callback in ps_netif_api.c     */
/*       uses it to back up addresses per cid, for the "no notify if   */
/*       address unchanged" comparison after deep-sleep wake           */
/* ================================================================== */

static softap_var_nv_t sim_var_nv;
softap_var_nv_t *g_softap_var_nv = &sim_var_nv;

/* ================================================================== */
/* APP basic config (real device: app_basic_config.c, FTL loads from   */
/* flash)                                                              */
/* Host: static default instance, field-by-field aligned with the      */
/* real-device app_basic_cfg_val defaults.                             */
/* (2026-09-04 audit: the original table initialized only a few        */
/*  fields; C designated initialization zero-filled the rest -- among  */
/*  them sockSingleReadMax=0 made sockMgrBindSocket set                */
/*  dlReadMaxLen=0, sockMgrSockRead returned -1 directly, and every    */
/*  downlink read event of QFTPOPEN/socket reported "read event fail   */
/*  ret:-1". Now completed per real-device defaults)                   */
/* Referenced by: DNS fallback in ps_netif_api.c, dlReadMaxLen at      */
/*   bind time in socketMgrApi.c, dlfcMaxLen in socket_default/        */
/*   socket_ssl, nitz in at_walltime.c, ntp_serv/dns_priority in       */
/*   xy_tcpip_api.c/at_http_api.c                                      */
/* ================================================================== */

static app_basic_cfg_t sim_app_basic_cfg = {
    .nitz              = 1,            /* QLTS/CCLK display mode, real-device default 1 */
    .local_tcp_rcv_wnd = 65535,
    .def_gw_ip4        = 0x0100A8C0U,  /* 192.168.0.1, network byte order */
    .def_pridns4       = 0x08080808U,  /* 8.8.8.8 */
    .def_secdns4       = 0x050505DFU,  /* 223.5.5.5 (real-device default;
                                        * previously filled 114.114.114.114,
                                        * reverted to real-device value) */
    .dns_priority      = 3,            /* real device NETCONN_DNS_IPV6_IPV4=3
                                        * (value happens to map to Sock_IPv64).
                                        * Host LWIP_IPV6=0: v6 attempt fails
                                        * fast and falls back to v4 */
    .dns_retries       = 2,            /* real-device quec default (previously 4) */
    .dns_interval      = 10,           /* DNS single-attempt timeout in seconds,
                                        * real-device default (previously 1, too
                                        * short and prone to timeouts) */
    .mtu               = 1500,
    .sockSingleReadMax = 1500,         /* critical: recv() single-read buffer
                                        * (real device USR_CUSTOM14 undefined
                                        * -> 1500). If 0, all socket/FTP
                                        * downlink reads fail */
    .sockDlfcLimit     = 10,           /* buffered-mode downlink flow-control threshold (KB) */
    .ftp_links         = 0,            /* 0 = FTP link count limit disabled (same as real device) */
    .ntp_cnt           = 1,
    .ntp_interval      = 30,
    .ntp_serv          = "ntp7.aliyun.com", /* QNTP/background NTP default server */
    .ppp_cid           = INVAILD_CID_FLAG, /* real-device default: use the boot
                                            * default activation CID, otherwise
                                            * pppNetCidGet() returns 0 and
                                            * netif_get_by_cid(0) finds nothing */
    /* Remaining fields (USB netif / cloud management / tcpUlAckWnd /
     * tcpDlAckWnd window clamping, etc.) stay 0 = disabled, matching
     * real-device defaults; the host has no PC gateway netif, so window
     * clamping would not take effect anyway */
};
app_basic_cfg_t *g_app_basic_cfg = &sim_app_basic_cfg;

/* ================================================================== */
/* PS test mode (real device: pdp_dial.c; 0=normal, 1=factory/         */
/* comprehensive test)                                                 */
/* Host: always normal mode                                            */
/* ================================================================== */

uint8_t get_ps_test_mode(void)
{
    return 0;
}

/* ================================================================== */
/* main_proxy.c dependency stubs (2026-09-11)                          */
/* Never triggered on host: main_proxy()'s branches below are only     */
/* entered on receiving the corresponding messages; the auto-lightup   */
/* path goes through PROXY_MSG_PS_PDP_ACT and never hits these         */
/* symbols. But the linker requires all symbols to exist, so empty     */
/* stubs are provided.                                                 */
/* ================================================================== */

#define STUB_VOID0(name)         void name(void) {}
#define STUB_VOID1(name, t, p)   void name(t p) { (void)(p); }
#define STUB_BOOL0(name)         bool name(void) { return false; }

STUB_VOID0(net_rm_session_file)
STUB_VOID0(OTA_update_upgrade_result)
STUB_VOID0(net_resume)
STUB_VOID1(data_call_status_cb_inner, uint32_t, arg)
STUB_BOOL0(Is_WakeUp_By_Self)
STUB_BOOL0(Is_WakeUp_By_Lpuart)
void *p_Sleep_Recovry_URC_Hook = NULL;

/* ================================================================== */
/* CID validity (real device: atc_ps_cmd/src/xy_ps_api.c:1375)         */
/* Host: judged by the CID_MIN_VAL..CID_MAX_VAL range in pdp_dial.h    */
/* ================================================================== */

bool xy_get_cid_vaild(unsigned char cid)
{
    return (cid >= CID_MIN_VAL && cid <= CID_MAX_VAL);
}

/* ================================================================== */
/* lwip DNS set/get: real implementations already compiled in          */
/* (net_adapt/src/xy_tcpip_api.c, in the xynet source list since       */
/* Phase 1). The earlier stub-phase xy_dns_set2/xy_dns_get stubs were  */
/* removed accordingly to avoid multiple definition.                   */
/* ================================================================== */

/* ================================================================== */
/* TCP event callback (real device: net_adapt/src/xy_socket_event.c:245)
 * The real device posts accept/recv/sent/close/err events to the socket
 * event proxy thread lwip_tcp_event -- already provided by
 * xy_socket_event.c (data-plane TCP event callback), no longer stubbed
 * here. */

/* ================================================================== */
/* net_adapt startup dependencies (real device: pkt_process.c /        */
/* xy_socket_event.c etc.)                                             */
/* ================================================================== */

/* Note: snapshot_init is no longer stubbed here -- the real
 * platform/application/common/src/xy_walltime.c is compiled in
 * (Phase 1, CCLK/QLTS/NTP time base) and defines the same-named
 * function; stubbing again would cause multiple definition.
 * lwip_tcp_event / socket_event_proxy_init are no longer stubbed
 * either -- the real xy_socket_event.c is compiled into xynet
 * (sntp_client.c depends on its reg_sock_event_cb /
 * dereg_sock_event_cb). */

/* lwip_tcp_event -- already provided by xy_socket_event.c */
/* socket_event_proxy_init -- already provided by xy_socket_event.c */

/* After pkt_process.c was integrated, the real pktMsgProcInit comes
 * from the SDK; several subsystem init functions it calls only need
 * empty stubs in the simulator: radvd (router advertisement),
 * net_tools (network toolset, no hardware dependencies). */
/* radvd_init stub removed -- real radvd_reply.c compiled in with PPP (see CMakeLists.txt) */

void net_tools_init(void)               /* real device: netpkt/src/xy_net_tools.c */
{
    LOGI("SIMNET", "net_tools_init - no-op on host");
}

/* ================================================================== */
/* Gateway netif getter (real device: netpkt/src/gw_netif_api.c,       */
/* USB/RNDIS gateway port)                                             */
/* Only host reference: socketMgrApi.c sockMgrBindGwNetif (called      */
/* only in the RNDIS debug mode where g_softap_fac_nv->xytest==8,      */
/* never triggered on host). The host has no gateway netif; always     */
/* returns NULL.                                                       */
/* ================================================================== */

struct netif *gwEthNetifGet(void)
{
    return NULL;
}

/* ================================================================== */
/* Byte stream to uppercase hex string (real device:                   */
/* kernel/misc/src/xy_utils.c:80)                                      */
/* Host mirrors the same algorithm; the real device xy_assert(0)s on   */
/* invalid input, the host returns 0 instead                           */
/* ================================================================== */

int bytes2hexstr(unsigned char *src, signed long src_len, char *dst, signed long dst_size)
{
    const char tab[] = "0123456789ABCDEF";
    signed long i;

    if (src == NULL || dst == NULL || src_len < 0 || dst_size <= src_len * 2)
        return 0;

    for (i = 0; i < src_len; i++) {
        *dst++ = tab[*src >> 4];
        *dst++ = tab[*src & 0x0f];
        src++;
    }

    *dst = '\0';

    return src_len * 2;
}

/* ================================================================== */
/* APP basic config persistence (real device: app_basic_config.c,      */
/* writes FTL flash)                                                   */
/* Host: g_app_basic_cfg is a RAM instance; changes take effect        */
/* immediately, no flash to write, succeed directly.                   */
/* ================================================================== */

int save_app_basic_cfg(void *arg, size_t arg_offset, uint32_t arg_len)
{
    (void)arg;
    LOGI("SIMNET", "save_app_basic_cfg offset=%u len=%u - RAM-only on host",
         (unsigned)arg_offset, (unsigned)arg_len);
    return 0;
}

/* ================================================================== */
/* PDP activation request (real device:                                */
/* LtePs/atc_ps_cmd/src/xy_ps_api.c:1158/1181, asynchronously sends    */
/* AT+CIDACT/QIACTEX to the CP core).                                  */
/* Host: routes to the fake CP's sim_vps_activate_cid (wan_eth_activate */
/* + DHCP, same implementation as AT+CGACT=1, idempotent). Return      */
/* value convention matches the real device: XY_OK=success /           */
/* XY_ERR=failure (real device returns XY_ERR=-1 when AT delivery      */
/* fails).                                                             */
/* ================================================================== */

int xy_activate_cid(char cid, char mipcallflg)
{
    (void)mipcallflg;

    if (!xy_get_cid_vaild((unsigned char)cid))
        return XY_ERR;

    return (sim_vps_activate_cid((uint8_t)cid) == 0) ? XY_OK : XY_ERR;
}

int xy_activate_cid_QIACTEX(char cid, char ViewMode)
{
    /* The live call site (socket_default.c:2012) passes arguments in the
     * order (0, cid), opposite to the header declaration (cid, ViewMode);
     * whichever is nonzero first is the real cid, compatible with both
     * calling styles. ViewMode is meaningless on host */
    char real_cid = (cid != 0) ? cid : ViewMode;

    if (!xy_get_cid_vaild((unsigned char)real_cid))
        return XY_ERR;

    return (sim_vps_activate_cid((uint8_t)real_cid) == 0) ? XY_OK : XY_ERR;
}

/* ================================================================== */
/* PPP / ppp_dl_malloc / ppp_ref_pbuf_free_custom / ppp_ref_pbuf_free_custom2
 * stubs removed -- real net_mem.c compiled with TLSF_HARDWARE=1 (see CMakeLists.txt) */

/* ================================================================== */
/* MQTT OneNET device info global (real device: cmiot/cm_at_mqtt/src/at_cm_mqtt.c:20) */

cm_mqtt_onenet_device_info_t cm_mqtt_onenet_devinfo = { NULL, NULL, NULL };

/* ================================================================== */
/* Device identity info (real device: LtePs/atc_ps_cmd/src/xy_ps_api.c) */
/* Host has no module; returns fixed simulation values used for MQTT   */
/* ClientID generation                                                 */
/* ================================================================== */

int xy_get_IMEI(char *imei, int len)
{
    static const char sim_imei[] = "000000000000000";
    int n = sizeof(sim_imei);

    if (imei == NULL || len <= 0)
        return 0;

    if (n > len)
        n = len;

    memcpy(imei, sim_imei, n);
    return n;
}

int xy_get_IMSI(char *imsi, int len)
{
    static const char sim_imsi[] = "000000000000000";
    int n = sizeof(sim_imsi);

    if (imsi == NULL || len <= 0)
        return 0;

    if (n > len)
        n = len;

    memcpy(imsi, sim_imsi, n);
    return n;
}

int xy_get_NCCID(char *ccid, int len)
{
    static const char sim_ccid[] = "00000000000000000000";

    if (ccid == NULL || len <= 0)
        return 0;

    int n = sizeof(sim_ccid);
    if (n > len) n = len;
    memcpy(ccid, sim_ccid, n);
    return n;
}

int xy_dev_get_SN(char *sn, uint32_t sn_len)
{
    static const char sim_sn[] = "SIM0000000000001";

    if (sn == NULL || sn_len == 0)
        return 0;

    int n = sizeof(sim_sn);
    int L = (int)sn_len;
    if (n > L) n = L;
    memcpy(sn, sim_sn, n);
    return n;
}
