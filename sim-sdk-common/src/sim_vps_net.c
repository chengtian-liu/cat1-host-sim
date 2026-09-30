/*
 * sim_vps_net.c -- fake CP control plane: network-class AT commands + PDP activation flow
 *
 * Role: the simulator itself acts as the CP (no module hardware, no SIM,
 * no RF management). On the real device these commands are driven by 3GPP
 * state machines inside the precompiled protocol stack library; on host
 * they are simulated with a fixed "registered" state + the real SDK data
 * plane:
 *
 * Boot auto-activation path (routed via main_proxy since 2026-09-11):
 *
 *   sim_pdp_activate()
 *     -> sim_proxy_start()                in-process userspace proxy (data plane)
 *     -> assemble DataCall_Info_T{ip=10.0.0.2, gw=10.0.0.1, dns=host}
 *     -> send_msg_2_proxy(PROXY_MSG_PS_PDP_ACT, ...)  message to main_proxy
 *     -> main_proxy::main_proxy()          receives the message
 *       -> wan_eth_activate(info)          creates WAN netif, sets IP and UP
 *         -> wan_eth_status_callback       lwip callback
 *           -> data_call_status_ind(EVENT_PS_IPV4_VALID, cid)
 *             -> AP application-layer callback (data_call_status_cb_inner)
 *
 * This path matches the real device: after PS (CP core) obtains an IP from
 * the core network, it notifies the AP side via the cross-core message
 * PROXY_MSG_PS_PDP_ACT, and the main_proxy task completes the lwIP netif
 * configuration. In the host simulation, the PS simulation (sim_pdp_activate
 * in this file) performs the "obtain address" step (currently a static
 * 10.0.0.2/24, fixed allocation from the sim_proxy internal subnet); the
 * rest of the routing is no different from the real device.
 *
 * Deactivation corresponds naturally: send_msg_2_proxy(PROXY_MSG_PS_PDP_DEACT, ...).
 *
 * The proxy subnet is fixed at 10.0.0.0/24: modem = 10.0.0.2, gateway
 * (sim_proxy itself) = 10.0.0.1 (SIM_PROXY_GW_STR). IP packets never leave
 * the process -- no ICS, no Wintun, no administrator (replaces the Phase 4
 * Wintun+ICS scheme, deprecated from N1).
 *
 * Thread context: this file is called from the at_ctl/atproxy task (the
 * same place as real-device ATC AT command handling); main_proxy's
 * wan_eth_activate runs in the mainProxy task (osPriorityAboveNormal),
 * a higher priority than the AT task, ensuring readiness when feeding
 * back lwIP network state.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/ip4_addr.h"

#include "xy_system.h"
#include "pdp_dial.h"
#include "ps_netif_api.h"
#include "xy_wan_api.h"
#include "at_ps_proxy.h"          /* SendAtInd2User                        */
#include "main_proxy.h"           /* PROXY_MSG_PS_PDP_ACT, send_msg_2_proxy */

#include "sim_log.h"
#include "sim_vps_net.h"
#include "sim_proxy.h"

/* Real implementation: net_adapt/src/xy_tcpip_api.c (compiled into xynet
 * since Phase 1). A local declaration is kept here instead of including
 * xy_tcpip_api.h: the latter cascades in the whole lwip/sockets.h family,
 * which this file does not need */
extern bool xy_dns_set2(uint8_t cid, ip_addr_t *dns_addr, int index);

#define SIM_IMSI        "460011234567890"
#define SIM_APN_DEFAULT "CMNET"

/* Fake CP state machine (minimal: "registered" at boot, CGATT switchable,
 * single PDP context) */
typedef struct {
    bool    attached;   /* CGATT state */
    bool    act;        /* CID 1 activated (wan_eth_activate + main_proxy already called) */
    uint8_t cid;        /* current default CID */
    char    apn[32];    /* APN recorded by CGDCONT */
} sim_vps_net_t;

static sim_vps_net_t s_net = { true, false, 1, SIM_APN_DEFAULT };

/* ------------------------------------------------------------------ */
/* Reply helper: real-device format "\r\n<info body>\r\n\r\nOK\r\n",   */
/* sent over the real nearps loop                                      */
/* ------------------------------------------------------------------ */

static void vpsnet_reply(int ttyFd, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
        return;
    SendAtInd2User(buf, (unsigned int)n, ttyFd, 0);
}

/* IPv4 address string of the currently active CID (0.0.0.0 while main_proxy's
 * wan_eth_activate has not completed) */
static const char *sim_pdp_ip_str(char *buf, size_t size)
{
    struct netif *netif = NULL;

    netifapi_netif_find(s_net.cid, &netif);
    if (netif == NULL || ip4_addr_isany(netif_ip4_addr(netif)))
        return "0.0.0.0";

    ip4addr_ntoa_r(netif_ip4_addr(netif), buf, (int)size);
    return buf;
}

/* Host DNS detected by sim_proxy -> dotted string (for CGCONTRDP; 0.0.0.0 if none) */
static const char *sim_pdp_dns_str(int idx, char *buf, size_t size)
{
    uint32_t a = sim_proxy_dns_ip4(idx);
    ip4_addr_t tmp;

    if (a == 0)
        return "0.0.0.0";
    ip4_addr_set_u32(&tmp, a);
    ip4addr_ntoa_r(&tmp, buf, (int)size);
    return buf;
}

/* ------------------------------------------------------------------ */
/* PDP activate/deactivate: the fake CP's core actions                 */
/* ------------------------------------------------------------------ */

static void sim_pdp_activate(uint8_t cid)
{
    DataCall_Info_T info;
    ip_addr_t dns1;

    if (s_net.act && s_net.cid == cid) {
        LOGI("VPSNET", "PDP cid=%u already active", (unsigned)cid);
        return;
    }

    /* Fake CP data plane: in-process userspace proxy (sim_proxy), idempotent */
    if (sim_proxy_start() != 0)
        LOGW("VPSNET", "sim_proxy unavailable - PDP active but no data path");

    /* --------------------------------------------------------------- */
    /* PS simulation "obtain address"                                   */
    /*                                                                */
    /* Real device: the CP core gets IP/DNS/GW from the core network   */
    /* P-GW via 3GPP NAS signaling (ATTACH + PDP ACTIVATE REQUEST),    */
    /* carried back by control-plane signaling, not via DHCP.          */
    /* The CP notifies the AP-side main_proxy task via the cross-core  */
    /* message send_msg_2_proxy(PROXY_MSG_PS_PDP_ACT, ...) to call     */
    /* wan_eth_activate and complete the lwIP netif configuration.     */
    /*                                                                */
    /* In the host simulation, the PS-simulated "CP core" directly     */
    /* assigns the fixed address 10.0.0.2 (mimicking P-GW allocation). */
    /* The subsequent path (send_msg_2_proxy -> main_proxy ->          */
    /* wan_eth_activate) is no different from the real device.         */
    /* --------------------------------------------------------------- */

    memset(&info, 0, sizeof(info));
    info.psIpType       = D_PDP_TYPE_IPV4;
    info.cid            = cid;
    info.wanIpType      = IP_TYPE_INVALID;
    info.curSetPsIpType = D_PDP_TYPE_IPV4;
    info.ipv4_mtu       = 1500;

    /* IP = 10.0.0.2 (assigned by the PS simulation, mimicking the address the
     * CP core obtains from core-network signaling) */
    IP_ADDR4(&info.ipv4_info.ip, 10, 0, 0, 2);
    /* DNS = host DNS (probed by sim_proxy, falls back to public DNS on failure) */
    {
        uint32_t d0 = sim_proxy_dns_ip4(0);
        uint32_t d1 = sim_proxy_dns_ip4(1);

        if (d0 != 0)
            ip4_addr_set_u32(ip_2_ip4(&info.ipv4_info.pridns), d0);
        else
            IP_ADDR4(&info.ipv4_info.pridns, 223, 5, 5, 5);
        if (d1 != 0)
            ip4_addr_set_u32(ip_2_ip4(&info.ipv4_info.secdns), d1);
        else
            IP_ADDR4(&info.ipv4_info.secdns, 8, 8, 8, 8);
    }

    /* ip4addr_ntoa returns a static buffer; calling it twice in the same
     * statement, the second call overwrites the first -- use ip4addr_ntoa_r
     * to write into separate buffers */
    {
        char ipstr[16], dnstr[16];

        ip4addr_ntoa_r(ip_2_ip4(&info.ipv4_info.ip), ipstr, sizeof(ipstr));
        ip4addr_ntoa_r(ip_2_ip4(&info.ipv4_info.pridns), dnstr, sizeof(dnstr));
        LOGI("VPSNET", "PDP activate cid=%u ip=%s dns=%s apn=%s",
             (unsigned)cid, ipstr, dnstr, s_net.apn);
    }

    /* First set the per-cid DNS of the lwIP DNS module (used by application
     * gethostbyname) */
    dns1 = info.ipv4_info.pridns;
    xy_dns_set2(cid, &dns1, 0);

    /* Complete activation via the main_proxy route: send PROXY_MSG_PS_PDP_ACT;
     * main_proxy receives it and calls wan_eth_activate(info) to configure the
     * lwIP netif and trigger data_call_status_ind -> notify the AP application
     * layer. isCopy=true makes main_proxy copy the data internally, so this
     * function's stack variables can be released safely. */
    send_msg_2_proxy(PROXY_MSG_PS_PDP_ACT, &info, sizeof(info), osWaitForever, true);

    s_net.act = true;
    s_net.cid = cid;
}

static void sim_pdp_deactivate(uint8_t cid)
{
    LOGI("VPSNET", "PDP deactivate cid=%u", (unsigned)cid);

    /* Complete deactivation via the main_proxy route: send PROXY_MSG_PS_PDP_DEACT;
     * main_proxy receives it and calls wan_eth_deactivate(cid). */
    send_msg_2_proxy(PROXY_MSG_PS_PDP_DEACT, &cid, sizeof(cid), osWaitForever, true);

    if (s_net.cid == cid)
        s_net.act = false;
}

/* ------------------------------------------------------------------ */
/* Boot auto-activation (equivalent to auto-sending AT+CGACT=1; the     */
/* application has network as soon as it powers up)                     */
/* Same implementation as the command path (sim_pdp_activate has its     */
/* own "already active" idempotency guard)                              */
/* ------------------------------------------------------------------ */

int sim_vps_net_auto_activate(void)
{
    if (!s_net.attached) {
        LOGW("VPSNET", "auto-dial skipped: not attached");
        return -1;
    }

    sim_pdp_activate(s_net.cid);
    return s_net.act ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Activation entry for AP-side application calls (routing target of   */
/* the xy_activate_cid / xy_activate_cid_QIACTEX stubs, see            */
/* sim_net_shims.c). Same implementation as CGACT=1/auto-activation,   */
/* idempotent; refused when not attached (CGATT=0) or cid invalid      */
/* ------------------------------------------------------------------ */

int sim_vps_activate_cid(uint8_t cid)
{
    if (!s_net.attached) {
        LOGW("VPSNET", "activate cid=%u refused: not attached", (unsigned)cid);
        return -1;
    }

    if (cid < CID_MIN_VAL || cid > CID_MAX_VAL) {
        LOGW("VPSNET", "activate cid=%u refused: out of range", (unsigned)cid);
        return -1;
    }

    sim_pdp_activate(cid);
    return s_net.act ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Command dispatch (cmd already uppercased, trailing terminators      */
/* stripped, '?'/'='/parameters kept)                                  */
/*                                                                     */
/* Purely static commands (independent of s_net state) -> table lookup */
/* and reply directly; dynamic commands -> sprintf on demand           */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *cmd;
    const char *rsp;
} vps_static_cmd_t;

static const vps_static_cmd_t s_vps_static[] = {
    { "AT+CPIN?",   "\r\n+CPIN: READY\r\n\r\nOK\r\n" },
    { "AT+CIMI",    "\r\n" SIM_IMSI "\r\n\r\nOK\r\n" },
    { "AT+CSQ",     "\r\n+CSQ: 31,99\r\n\r\nOK\r\n" },
    { "AT+CREG?",   "\r\n+CREG: 0,1\r\n\r\nOK\r\n" },
    { "AT+CEREG?",  "\r\n+CEREG: 0,1\r\n\r\nOK\r\n" },
    { "AT+CGREG?",  "\r\n+CGREG: 0,1\r\n\r\nOK\r\n" },
    { "AT+COPS?",   "\r\n+COPS: 0,0,\"CHINA MOBILE\",7\r\n\r\nOK\r\n" },
    { "AT+CGDCONT=?", "\r\n+CGDCONT: (1-8),\"IP\",,,(0-2),(0-4)\r\n\r\nOK\r\n" },
};

int sim_vps_net_handle(const char *cmd, int ttyFd)
{
    char ipbuf[20];
    unsigned int i;

    for (i = 0; i < sizeof(s_vps_static) / sizeof(s_vps_static[0]); i++) {
        if (strcmp(cmd, s_vps_static[i].cmd) == 0) {
            vpsnet_reply(ttyFd, "%s", s_vps_static[i].rsp);
            goto handled;
        }
    }

    /* ---- PS attach ---- */
    if (strcmp(cmd, "AT+CGATT?") == 0) {
        vpsnet_reply(ttyFd, "\r\n+CGATT: %d\r\n\r\nOK\r\n", s_net.attached ? 1 : 0);
        goto handled;
    }
    if (strncmp(cmd, "AT+CGATT=", 9) == 0) {
        s_net.attached = (cmd[9] != '0');
        vpsnet_reply(ttyFd, "\r\nOK\r\n");
        goto handled;
    }

    /* ---- PDP context definition ---- */
    if (strcmp(cmd, "AT+CGDCONT?") == 0) {
        vpsnet_reply(ttyFd, "\r\n+CGDCONT: %u,\"IP\",\"%s\",,0,0\r\n\r\nOK\r\n",
                     (unsigned)s_net.cid, s_net.apn);
        goto handled;
    }
    /* The '?' of AT+CGDCONT=? is at cmd[11] ("AT+CGDCONT=" is exactly 11
     * chars) -- the old code checked cmd[12], off by one: when the command
     * is exactly "AT+CGDCONT=" it would also read past the NUL. The test
     * command form is already handled by the static table above, so just
     * exclude it here */
    if (strncmp(cmd, "AT+CGDCONT=", 11) == 0 && cmd[11] != '?') {
        /* AT+CGDCONT=<cid>,"IP","<apn>" -- only record cid and APN.
         * The four quotes in order are: pdp_type open/close, apn open/close;
         * take what is between the 3rd and 4th */
        unsigned cid = 1;
        const char *q = cmd + 11;
        int qi;

        sscanf(cmd + 11, "%u", &cid);
        if (cid < 1 || cid > 8) {
            vpsnet_reply(ttyFd, "\r\n+CME ERROR: invalid index\r\n");
            return 1;
        }
        s_net.cid = (uint8_t)cid;

        for (qi = 0; qi < 3 && q != NULL; qi++) {   /* skip the first three quotes */
            q = strchr(q, '"');
            if (q != NULL)
                q++;
        }
        if (q != NULL) {
            size_t n = strcspn(q, "\"");
            if (n > 0 && n < sizeof(s_net.apn)) {
                memcpy(s_net.apn, q, n);
                s_net.apn[n] = '\0';
            }
        }
        vpsnet_reply(ttyFd, "\r\nOK\r\n");
        goto handled;
    }

    /* ---- PDP activate/deactivate (triggers real data-plane setup) ---- */
    if (strcmp(cmd, "AT+CGACT?") == 0) {
        vpsnet_reply(ttyFd, "\r\n+CGACT: %u,%d\r\n\r\nOK\r\n",
                     (unsigned)s_net.cid, s_net.act ? 1 : 0);
        goto handled;
    }
    if (strncmp(cmd, "AT+CGACT=", 9) == 0) {
        int state = (cmd[9] == '1');
        unsigned cid = s_net.cid;
        const char *comma = strchr(cmd + 9, ',');

        if (comma != NULL)
            sscanf(comma + 1, "%u", &cid);

        if (cid < 1 || cid > 8) {
            vpsnet_reply(ttyFd, "\r\n+CME ERROR: invalid index\r\n");
        } else if (state) {
            if (!s_net.attached) {
                vpsnet_reply(ttyFd, "\r\n+CME ERROR: operation not allowed\r\n");
                return 1;
            }
            sim_pdp_activate((uint8_t)cid);
            vpsnet_reply(ttyFd, "\r\nOK\r\n");
        } else {
            sim_pdp_deactivate((uint8_t)cid);
            vpsnet_reply(ttyFd, "\r\nOK\r\n");
        }
        goto handled;
    }

    /* ---- Address query ---- */
    if (strcmp(cmd, "AT+CGPADDR") == 0 || strncmp(cmd, "AT+CGPADDR=", 11) == 0) {
        vpsnet_reply(ttyFd, "\r\n+CGPADDR: %u,%s\r\n\r\nOK\r\n",
                     (unsigned)s_net.cid, sim_pdp_ip_str(ipbuf, sizeof(ipbuf)));
        goto handled;
    }
    if (strcmp(cmd, "AT+CGCONTRDP?") == 0 || strncmp(cmd, "AT+CGCONTRDP=", 13) == 0) {
        if (!s_net.act) {
            vpsnet_reply(ttyFd, "\r\n+CME ERROR: operation not allowed\r\n");
        } else {
            char dns1buf[20], dns2buf[20];

            /* Fields: cid,bearer,apn,local address,mask,gateway,primary DNS,
             * secondary DNS (gateway=10.0.0.1, DNS=host-probed values, see
             * sim_proxy) */
            vpsnet_reply(ttyFd,
                         "\r\n+CGCONTRDP: %u,,\"%s\",\"%s\",\"" SIM_PROXY_MASK_STR
                         "\",\"" SIM_PROXY_GW_STR "\",\"%s\",\"%s\"\r\n\r\nOK\r\n",
                         (unsigned)s_net.cid, s_net.apn,
                         sim_pdp_ip_str(ipbuf, sizeof(ipbuf)),
                         sim_pdp_dns_str(0, dns1buf, sizeof(dns1buf)),
                         sim_pdp_dns_str(1, dns2buf, sizeof(dns2buf)));
        }
        goto handled;
    }

    return 0;   /* not a network-class command -> fall back to the original virtual PS flow */

handled:
    LOGI("VPSNET", "cmd='%s' fd=%d handled", cmd, ttyFd);
    return 1;
}
