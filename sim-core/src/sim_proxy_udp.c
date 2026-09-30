/*
 * sim_proxy_udp.c -- UDP 5-tuple relay (in-process user-mode proxy data plane submodule)
 *
 * Look up / create the (rmt_ip, rmt_port, loc_port) mapping -> host connected UDP
 * socket to the Internet; select re-injects downlink data. Idle mappings are
 * reclaimed on a timer.
 * Depends on g_proxy / proxy_fill_ip_hdr / proxy_enqueue_down / byte-order
 * helpers / checksum helpers in sim_proxy_internal.h.
 */

#include "sim_proxy_internal.h"   /* winsock2.h is included by the internal header
                                   * (FD_SETSIZE=512 must take effect before winsock2.h) */

/* ------------------------------------------------------------------ */
/* Internal helpers                                                      */
/* ------------------------------------------------------------------ */

static SOCKET proxy_udp_socket_open(uint32_t rmt_ip, uint16_t rmt_port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in sa;
    u_long nb = 1;

    if (s == INVALID_SOCKET)
        return INVALID_SOCKET;

    ioctlsocket(s, FIONBIO, &nb);

    if (g_proxy.host_ip) {
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        memcpy(&sa.sin_addr, &g_proxy.host_ip, 4);
        sa.sin_port = 0;
        if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
            int e = WSAGetLastError();
            closesocket(s);
            LOGW(PROXY_TAG, "UDP bind to %s failed (err=%d)", proxy_ip_str(g_proxy.host_ip), e);
            return INVALID_SOCKET;
        }
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    memcpy(&sa.sin_addr, &rmt_ip, 4);
    sa.sin_port = htons(rmt_port);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

static proxy_udp_map_t *proxy_udp_get(uint32_t rmt_ip, uint16_t rmt_port,
                                      uint16_t loc_port, uint32_t src_ip)
{
    proxy_udp_map_t *free_slot = NULL;
    proxy_udp_map_t *m;
    int i;

    for (i = 0; i < PROXY_UDP_MAX; i++) {
        m = &g_proxy.udp[i];
        if (m->s != INVALID_SOCKET) {
            if (m->rmt_ip == rmt_ip && m->rmt_port == rmt_port &&
                m->loc_port == loc_port)
                return m;
        } else if (free_slot == NULL) {
            free_slot = m;
        }
    }

    if (free_slot == NULL) {
        if (proxy_log_gate(&g_proxy.log_drop_ms))
            LOGW(PROXY_TAG, "UDP mapping table full (%d) -- dropping", PROXY_UDP_MAX);
        return NULL;
    }

    free_slot->s = proxy_udp_socket_open(rmt_ip, rmt_port);
    if (free_slot->s == INVALID_SOCKET) {
        if (proxy_log_gate(&g_proxy.log_drop_ms))
            LOGW(PROXY_TAG, "UDP relay socket create failed (err=%d)", WSAGetLastError());
        return NULL;
    }
    free_slot->rmt_ip   = rmt_ip;
    free_slot->rmt_port = rmt_port;
    free_slot->loc_port = loc_port;
    free_slot->src_ip   = src_ip;
    free_slot->last_ms  = GetTickCount();
    g_proxy.udp_maps++;
    LOGI(PROXY_TAG, "UDP map new: %s:%u -> %s:%u",
         proxy_ip_str(src_ip), (unsigned)loc_port,
         proxy_ip_str(rmt_ip), (unsigned)rmt_port);
    return free_slot;
}

static void proxy_udp_out(uint32_t src_ip, uint16_t sport,
                          uint32_t dst_ip, uint16_t dport,
                          const uint8_t *data, int dlen)
{
    proxy_udp_map_t *m;

    if (dlen < 0 || dlen > 1472)
        return;

    m = proxy_udp_get(dst_ip, dport, sport, src_ip);
    if (m == NULL)
        return;

    m->last_ms = GetTickCount();
    if (send(m->s, (const char *)data, dlen, 0) == SOCKET_ERROR) {
        int e = WSAGetLastError();

        if (e == WSAECONNRESET || e == WSAENOTCONN) {
            closesocket(m->s);
            m->s = proxy_udp_socket_open(dst_ip, dport);
            if (m->s != INVALID_SOCKET)
                send(m->s, (const char *)data, dlen, 0);
        } else if (e != WSAEWOULDBLOCK && proxy_log_gate(&g_proxy.log_drop_ms)) {
            LOGW(PROXY_TAG, "UDP relay send failed (err=%d)", e);
        }
    }
}

static void proxy_udp_recv(proxy_udp_map_t *m)
{
    uint8_t pkt[PROXY_MAX_IP_LEN];
    uint8_t *data = pkt + 28;
    int n = recv(m->s, (char *)data, 1472, 0);

    if (n == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAECONNRESET) {
            closesocket(m->s);
            m->s = INVALID_SOCKET;
        }
        return;
    }
    if (n <= 0)
        return;

    LOGI(PROXY_TAG, "UDP recv %d bytes from %s:%u -> %s:%u",
         n, proxy_ip_str(m->rmt_ip), (unsigned)m->rmt_port,
         proxy_ip_str(m->src_ip), (unsigned)m->loc_port);

    m->last_ms = GetTickCount();

    wr16n(pkt + 20, m->rmt_port);
    wr16n(pkt + 22, m->loc_port);
    wr16n(pkt + 24, (uint16_t)(8 + n));
    wr16n(pkt + 26, 0);
    proxy_fill_ip_hdr(pkt, m->rmt_ip, m->src_ip, 17, (uint16_t)(8 + n));
    wr16n(pkt + 26, proxy_udp_cksum(m->rmt_ip, m->src_ip, pkt + 20, (uint16_t)(8 + n)));

    proxy_enqueue_down(pkt, 20 + 8 + n);
}

/* ------------------------------------------------------------------ */
/* Public interface                                                      */
/* ------------------------------------------------------------------ */

void proxy_udp_init(void)
{
    int i;

    for (i = 0; i < PROXY_UDP_MAX; i++)
        g_proxy.udp[i].s = INVALID_SOCKET;
}

void proxy_uplink_udp(uint32_t src, uint32_t dst,
                      const uint8_t *l4, int l4len)
{
    uint16_t sport, dport, ulen;
    const uint8_t *data;
    int dlen;

    if (l4len < 8)
        return;
    sport = rd16n(l4);
    dport = rd16n(l4 + 2);
    ulen  = rd16n(l4 + 4);
    if (ulen < 8 || ulen > (uint16_t)l4len)
        return;
    data = l4 + 8;
    dlen = ulen - 8;

    if (dst == PROXY_IP_BCAST || dst == PROXY_IP_GW)
        return;

    proxy_udp_out(src, sport, dst, dport, data, dlen);
}

void proxy_udp_select(fd_set *rfds, SOCKET *maxs)
{
    int i;

    for (i = 0; i < PROXY_UDP_MAX; i++) {
        if (g_proxy.udp[i].s != INVALID_SOCKET) {
            FD_SET(g_proxy.udp[i].s, rfds);
            if (g_proxy.udp[i].s > *maxs)
                *maxs = g_proxy.udp[i].s;
        }
    }
}

void proxy_udp_process(int n, fd_set *rfds)
{
    int i;

    if (n <= 0)
        return;
    for (i = 0; i < PROXY_UDP_MAX; i++) {
        if (g_proxy.udp[i].s != INVALID_SOCKET &&
            FD_ISSET(g_proxy.udp[i].s, rfds))
            proxy_udp_recv(&g_proxy.udp[i]);
    }
}

void proxy_udp_sweep(void)
{
    DWORD now = GetTickCount();
    int i;

    for (i = 0; i < PROXY_UDP_MAX; i++) {
        proxy_udp_map_t *m = &g_proxy.udp[i];

        if (m->s != INVALID_SOCKET && (now - m->last_ms) > PROXY_UDP_IDLE_MS) {
            closesocket(m->s);
            m->s = INVALID_SOCKET;
            LOGD(PROXY_TAG, "UDP map expired (idle > %ums)", PROXY_UDP_IDLE_MS);
        }
    }
}
