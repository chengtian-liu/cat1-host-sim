/*
 * sim_proxy.c -- in-process user-mode proxy data plane (fake CP, replaces Wintun + ICS, stage N1)
 *                (glue layer: ring queues / wakeup / DNS / uplink dispatch / host thread / downlink injection / public API)
 *
 * Background: the stage-4 fake CP data plane used a Wintun tunnel + Windows ICS
 * NAT -- it required administrator rights, a driver install, manually configuring
 * ICS on the host NIC, and ICS itself was unstable (network segment hard-pinned to
 * 192.168.137.0/24, service occasionally stopped working). Since N1 the data plane
 * is a fully in-process user-mode proxy:
 *
 * Uplink   PPP fast path pkt_process.c -> API_Send_Data_2_PS (sim_net_shims.c)
 *          non-PPP scenarios go wan netif output -> ps_netif_api.c -> API_Send_Data_2_PS
 *            -> sim_proxy_uplink() -> uplink ring -> host thread processes packet by packet:
 *              - UDP 5-tuple relay (host connected UDP socket) to the Internet -> sim_proxy_udp.c
 *              - ICMP echo req  iphlpapi IcmpSendEcho host reachability probe,
 *                               forged echo reply -> sim_proxy_icmp.c
 *              - TCP            termination proxy (N2): answers handshake / sends and
 *                               receives data locally, host non-blocking socket relays
 *                               the real external connection -> sim_proxy_tcp.c
 *              - other protocols silently dropped
 * Downlink host thread builds raw IP packets -> downlink ring -> proxy_rx task (a real
 *          RTOS task! packet_recved_from_wan uses netifapi internally, task context only)
 *          -> Ipdata_Msg_T -> packet_recved_from_wan() -> lwip
 *          (original SDK downlink injection, same path as on real hardware;
 *           net_malloc zero-copy ownership handover)
 *
 * Internal network segment 10.0.0.0/24: modem = 10.0.0.2, proxy box = 10.0.0.1.
 * IP packets never enter the Windows routing table -- no ICS, no Wintun, no route
 * commands, no administrator requirement (IcmpCreateFile/GetNetworkParams/plain UDP
 * sockets are all user-mode).
 *
 * Threading model (two iron rules):
 *   - The host thread (proxy_host) is a pure Win32 thread outside the RTOS core:
 *     blocking select and IcmpSendEcho are free to use.
 *   - Downlink injection must happen in an RTOS task: proxy_rx polls the downlink
 *     ring with osDelay(1). WARNING: raw Win32 blocking waits are strictly forbidden
 *     inside tasks (this port's scheduler doesn't know about them -> wild jumps).
 *
 * Submodule split (stage N4):
 *   sim_proxy_internal.h -- shared types/constants/inline helpers/cross-module declarations
 *   sim_proxy_icmp.c     -- ICMP echo reply
 *   sim_proxy_udp.c      -- UDP 5-tuple relay
 *   sim_proxy_tcp.c      -- TCP termination proxy
 *   sim_proxy.c          -- this file: glue layer (rings/wakeup/DNS/dispatch/threads/API)
 */

#include "sim_proxy_internal.h"   /* winsock2.h is included by the internal header
                                   * (FD_SETSIZE=512 must take effect before winsock2.h) */

/* ------------------------------------------------------------------ */
/* Ring queues (CRITICAL_SECTION double-lock separation: one for uplink,  */
/* one for downlink)                                                    */
/* ------------------------------------------------------------------ */

static void proxy_q_init(proxy_q_t *q)
{
    InitializeCriticalSection(&q->cs);
    q->head = q->tail = q->count = 0;
}

static int proxy_q_push(proxy_q_t *q, void *buf, uint16_t len)
{
    int rc = 0;

    EnterCriticalSection(&q->cs);
    if (q->count >= PROXY_QSIZE) {
        rc = -1;
    } else {
        q->s[q->tail].buf = buf;
        q->s[q->tail].len = len;
        q->tail = (q->tail + 1) % PROXY_QSIZE;
        q->count++;
    }
    LeaveCriticalSection(&q->cs);
    return rc;
}

static void *proxy_q_pop(proxy_q_t *q, uint16_t *len)
{
    void *buf = NULL;

    EnterCriticalSection(&q->cs);
    if (q->count > 0) {
        buf  = q->s[q->head].buf;
        *len = q->s[q->head].len;
        q->head = (q->head + 1) % PROXY_QSIZE;
        q->count--;
    }
    LeaveCriticalSection(&q->cs);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Global state                                                          */
/* ------------------------------------------------------------------ */

sim_proxy_t g_proxy;

/* ------------------------------------------------------------------ */
/* Kick the host thread awake (call after uplink enqueue; UDP 1 byte,     */
/* non-blocking, failure is harmless -- select also has a 50ms fallback   */
/* tick)                                                                  */
/* ------------------------------------------------------------------ */

static void proxy_wake_kick(void)
{
    char c = 1;

    if (g_proxy.wake_send != INVALID_SOCKET)
        sendto(g_proxy.wake_send, &c, 1, 0,
               (struct sockaddr *)&g_proxy.wake_addr, sizeof(g_proxy.wake_addr));
}

static int proxy_wake_init(void)
{
    SOCKET l, s;
    struct sockaddr_in sa;
    int alen = sizeof(sa);
    u_long nb = 1;

    l = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (l == INVALID_SOCKET || s == INVALID_SOCKET) {
        if (l != INVALID_SOCKET) closesocket(l);
        if (s != INVALID_SOCKET) closesocket(s);
        LOGE(PROXY_TAG, "wake socketpair create failed (err=%d)", WSAGetLastError());
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    if (bind(l, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
        getsockname(l, (struct sockaddr *)&sa, &alen) != 0) {
        closesocket(l);
        closesocket(s);
        LOGE(PROXY_TAG, "wake socketpair bind failed (err=%d)", WSAGetLastError());
        return -1;
    }

    ioctlsocket(l, FIONBIO, &nb);
    ioctlsocket(s, FIONBIO, &nb);

    g_proxy.wake_recv = l;
    g_proxy.wake_send = s;
    g_proxy.wake_addr = sa;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Host DNS detection (GetNetworkParams, no admin needed)                */
/* ------------------------------------------------------------------ */

static void proxy_detect_dns(void)
{
    FIXED_INFO stack_buf;
    FIXED_INFO *fi = &stack_buf;
    ULONG len = sizeof(stack_buf);
    DWORD r;
    IP_ADDR_STRING *p;

    r = GetNetworkParams(fi, &len);
    if (r == ERROR_BUFFER_OVERFLOW) {
        fi = (FIXED_INFO *)malloc(len);
        if (fi != NULL)
            r = GetNetworkParams(fi, &len);
    }

    if (r == NO_ERROR && fi != NULL) {
        for (p = &fi->DnsServerList;
             p != NULL && g_proxy.dns_count < 3;
             p = p->Next) {
            uint32_t a = (uint32_t)inet_addr(p->IpAddress.String);

            if (a != INADDR_NONE && a != 0)
                g_proxy.dns[g_proxy.dns_count++] = a;
        }
    }
    if (fi != &stack_buf)
        free(fi);

    if (g_proxy.dns_count == 0) {
        g_proxy.dns[0] = (uint32_t)inet_addr("223.5.5.5");
        g_proxy.dns[1] = (uint32_t)inet_addr("8.8.8.8");
        g_proxy.dns_count = 2;
        LOGW(PROXY_TAG, "no host DNS found via GetNetworkParams -- fallback to %s / %s",
             proxy_ip_str(g_proxy.dns[0]), proxy_ip_str(g_proxy.dns[1]));
    } else {
        LOGI(PROXY_TAG, "host DNS detected (%d): %s%s%s", g_proxy.dns_count,
             proxy_ip_str(g_proxy.dns[0]),
             g_proxy.dns_count > 1 ? " / " : "",
             g_proxy.dns_count > 1 ? proxy_ip_str(g_proxy.dns[1]) : "");
    }
}

/* ------------------------------------------------------------------ */
/* Host outbound NIC IP detection (for UDP socket bind, prevents routes   */
/* from going astray after PPP dial-up)                                 */
/* ------------------------------------------------------------------ */

static uint32_t proxy_detect_host_ip(void)
{
    SOCKET s;
    struct sockaddr_in sa, local;
    int alen = sizeof(local);
    uint32_t ip = 0;

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return 0;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr("8.8.8.8");
    sa.sin_port = htons(53);

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
        if (getsockname(s, (struct sockaddr *)&local, &alen) == 0)
            ip = (uint32_t)local.sin_addr.s_addr;
    }
    closesocket(s);
    return ip;
}

/* ------------------------------------------------------------------ */
/* Downlink packet build: fill IP header -> push to downlink ring         */
/* (host thread only)                                                   */
/* ------------------------------------------------------------------ */

void proxy_fill_ip_hdr(uint8_t *ip, uint32_t src, uint32_t dst,
                       uint8_t proto, uint16_t payload_len)
{
    ip[0] = 0x45;
    ip[1] = 0;
    wr16n(ip + 2, (uint16_t)(20 + payload_len));
    wr16n(ip + 4, g_proxy.ip_id++);
    wr16n(ip + 6, 0x4000);
    ip[8] = PROXY_IP_TTL;
    ip[9] = proto;
    wr16n(ip + 10, 0);
    wr32(ip + 12, src);
    wr32(ip + 16, dst);
    wr16n(ip + 10, proxy_cksum(ip, 20, 0));
}

void proxy_enqueue_down(const uint8_t *ip, int len)
{
    void *buf;

    if (len < 20 || len > PROXY_MAX_IP_LEN)
        return;

    buf = sim_core_get_callbacks()->malloc((size_t)len);
    if (buf == NULL) {
        g_proxy.dn_drop_nomem++;
        return;
    }
    memcpy(buf, ip, (size_t)len);

    if (proxy_q_push(&g_proxy.dlq, buf, (uint16_t)len) != 0) {
        sim_core_get_callbacks()->free(buf);
        g_proxy.dn_drop_qfull++;
        if (proxy_log_gate(&g_proxy.log_drop_ms))
            LOGW(PROXY_TAG, "downlink queue full -- dropping");
        return;
    }
    g_proxy.dn_pkts++;
}

/* ------------------------------------------------------------------ */
/* Uplink single-packet parse and dispatch (host thread)                 */
/* ------------------------------------------------------------------ */

static void proxy_process_uplink(const uint8_t *ip, int len)
{
    int ihl, l4len;
    uint16_t tot, frag;
    uint8_t proto;
    uint32_t src, dst;

    if (len < 20)
        return;
    if ((ip[0] >> 4) != 4)
        return;
    ihl = (ip[0] & 0x0f) * 4;
    if (ihl < 20 || ihl > len)
        return;
    tot = rd16n(ip + 2);
    if (tot < (uint16_t)ihl || tot > (uint16_t)len)
        return;
    frag = rd16n(ip + 6);
    if ((frag & 0x2000) != 0 || (frag & 0x1fff) != 0) {
        if (proxy_log_gate(&g_proxy.log_frag_ms))
            LOGW(PROXY_TAG, "fragmented uplink packet dropped (N1 does not support fragments)");
        return;
    }

    proto  = ip[9];
    src    = rd32(ip + 12);
    dst    = rd32(ip + 16);
    l4len  = tot - ihl;

    /* Multicast (224.0.0.0/4, incl. LLMNR/mDNS etc.): link-local/ASM,
     * the Internet won't answer, drop directly */
    if ((ntohl(dst) & 0xF0000000) == 0xE0000000) {
        if (proxy_log_gate(&g_proxy.log_proto_ms))
            LOGI(PROXY_TAG, "multicast dst %s dropped", proxy_ip_str(dst));
        return;
    }

    switch (proto) {
    case 17:
        proxy_uplink_udp(src, dst, ip + ihl, l4len);
        break;
    case 1:
        proxy_uplink_icmp(src, dst, ip + ihl, l4len);
        break;
    case 6:
        proxy_uplink_tcp(src, dst, ip + ihl, l4len);
        break;
    default:
        if (proxy_log_gate(&g_proxy.log_proto_ms))
            LOGW(PROXY_TAG, "proto %u uplink dropped", (unsigned)proto);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Host send/receive thread (outside the core: blocking select /          */
/* IcmpSendEcho are free to use)                                        */
/* ------------------------------------------------------------------ */

static DWORD WINAPI proxy_host_thread(LPVOID arg)
{
    (void)arg;

    LOGI(PROXY_TAG, "host thread running (select tick %ums)", PROXY_SELECT_MS);

    for (;;) {
        fd_set rfds, wfds;
        struct timeval tv;
        SOCKET maxs = g_proxy.wake_recv;
        int n;

        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(g_proxy.wake_recv, &rfds);

        proxy_udp_select(&rfds, &maxs);
        proxy_tcp_select(&rfds, &wfds, &maxs);

        tv.tv_sec  = 0;
        tv.tv_usec = PROXY_SELECT_MS * 1000;
        n = select((int)maxs + 1, &rfds, &wfds, NULL, &tv);

        if (n > 0 && FD_ISSET(g_proxy.wake_recv, &rfds)) {
            char tmp[64];

            while (recv(g_proxy.wake_recv, tmp, sizeof(tmp), 0) > 0)
                ;
        }

        for (;;) {
            uint16_t plen = 0;
            void *buf = proxy_q_pop(&g_proxy.ulq, &plen);

            if (buf == NULL)
                break;
            proxy_process_uplink((const uint8_t *)buf, plen);
            free(buf);
        }

        proxy_tcp_process(n, &rfds, &wfds);
        proxy_udp_process(n, &rfds);
        proxy_icmp_poll();     /* re-inject probe thread results (non-blocking, see sim_proxy_icmp.c) */

        proxy_udp_sweep();
        proxy_tcp_sweep(PROXY_SELECT_MS);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Downlink injection task (real RTOS task: packet_recved_from_wan uses   */
/* netifapi, task context only; poll with osDelay, raw Win32 blocking     */
/* waits strictly forbidden)                                            */
/* ------------------------------------------------------------------ */

void sim_core_proxy_rx_loop(void)
{
    const sim_core_callbacks_t *cb = sim_core_get_callbacks();

    for (;;) {
        if (cb->delay != NULL)
            cb->delay(1);

        for (;;) {
            uint16_t len = 0;
            void *buf = proxy_q_pop(&g_proxy.dlq, &len);

            if (buf == NULL)
                break;

            if (cb->inject != NULL)
                cb->inject(buf, len);
            /* Note: do NOT cb->free(buf) here. The inject callback takes over
             * buffer ownership; the consumer in the target SDK (e.g. the far end
             * of icc_data_channel_write) is responsible for freeing it */
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public interface                                                      */
/* ------------------------------------------------------------------ */

int sim_proxy_start(void)
{
    /* Concurrent double-entry guard: auto-activation and AT+CGACT=1 may arrive
     * at the same time. The old implementation's if(g_proxy.up) was
     * check-then-act; the loser would memset(&g_proxy,0,...) after the winner
     * had already started threads, wiping the running state. CAS claims the
     * start right; the loser waits until the winner sets up, then returns
     * success directly */
    static volatile LONG s_starting = 0;

    WSADATA wsa;

    /* The callback table must be bound before the proxy starts: the target
     * injects malloc/free/inject via sim_core_set_callbacks(), otherwise
     * downlink injection and memory allocation are all wild pointers */
    if (!sim_core_callbacks_ready()) {
        LOGE(PROXY_TAG, "callbacks not bound -- target must call "
                        "sim_core_set_callbacks() before sim_proxy_start()");
        return -1;
    }

    if (g_proxy.up)
        return 0;

    if (InterlockedCompareExchange(&s_starting, 1, 0) != 0) {
        int i;
        const sim_core_callbacks_t *cb = sim_core_get_callbacks();

        for (i = 0; i < 500 && !g_proxy.up; i++) {   /* wait at most 5s */
            if (cb->delay != NULL)
                cb->delay(10);
            else
                Sleep(10);
        }
        return g_proxy.up ? 0 : -1;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        LOGE(PROXY_TAG, "WSAStartup failed (err=%d)", WSAGetLastError());
        InterlockedExchange(&s_starting, 0);
        return -1;
    }

    memset(&g_proxy, 0, sizeof(g_proxy));
    g_proxy.wake_recv = INVALID_SOCKET;
    g_proxy.wake_send = INVALID_SOCKET;
    g_proxy.ip_id     = PROXY_IP_ID_SEED;

    proxy_udp_init();
    proxy_tcp_init();

    proxy_q_init(&g_proxy.ulq);
    proxy_q_init(&g_proxy.dlq);
    proxy_detect_dns();
    g_proxy.host_ip = proxy_detect_host_ip();
    if (g_proxy.host_ip)
        LOGI(PROXY_TAG, "host outbound IP detected: %s", proxy_ip_str(g_proxy.host_ip));

    if (proxy_wake_init() != 0) {
        InterlockedExchange(&s_starting, 0);
        return -1;
    }

    g_proxy.icmp_h = IcmpCreateFile();
    if (g_proxy.icmp_h == INVALID_HANDLE_VALUE) {
        g_proxy.icmp_h = NULL;
        LOGW(PROXY_TAG, "IcmpCreateFile failed (err=%lu) -- QPING will be unavailable "
                      "(host firewall or permission restriction)", GetLastError());
    }
    proxy_icmp_init();   /* dedicated probe thread: IcmpSendEcho blocks
                          * synchronously for up to 2s and must not occupy the
                          * host thread (otherwise the whole data plane stalls) */

    g_proxy.host_thread = CreateThread(NULL, 0, proxy_host_thread, NULL, 0, NULL);
    if (g_proxy.host_thread == NULL) {
        LOGE(PROXY_TAG, "CreateThread(proxy_host) failed (err=%lu)", GetLastError());
        InterlockedExchange(&s_starting, 0);
        return -1;
    }

    InterlockedExchange(&g_proxy.up, 1);
    InterlockedExchange(&s_starting, 0);   /* from here on short-circuited by up==1; restore initial state */
    LOGI(PROXY_TAG, "userspace proxy up: client=" SIM_PROXY_CLIENT_STR
                   " gw=" SIM_PROXY_GW_STR " dns=%d -- no ICS/Wintun/admin needed",
         g_proxy.dns_count);
    return 0;
}

int sim_proxy_is_up(void)
{
    return g_proxy.up;
}

int sim_proxy_uplink(const void *ip_pkt, uint32_t len)
{
    void *buf;

    if (!g_proxy.up)
        return -1;
    if (len < 20 || len > PROXY_MAX_IP_LEN)
        return 0;

    buf = malloc(len);
    if (buf == NULL)
        return 0;
    memcpy(buf, ip_pkt, len);

    if (proxy_q_push(&g_proxy.ulq, buf, (uint16_t)len) != 0) {
        free(buf);
        g_proxy.up_drop_qfull++;
        if (proxy_log_gate(&g_proxy.log_drop_ms))
            LOGW(PROXY_TAG, "uplink queue full -- dropping");
        return 0;
    }
    g_proxy.up_pkts++;
    proxy_wake_kick();
    return 0;
}

uint32_t sim_proxy_dns_ip4(int idx)
{
    if (idx < 0 || idx >= g_proxy.dns_count)
        return 0;
    return g_proxy.dns[idx];
}

int sim_proxy_dns_count(void)
{
    return g_proxy.dns_count;
}
