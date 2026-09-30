/*
 * sim_proxy_tcp.c -- TCP termination proxy (in-process user-mode proxy data plane submodule, stage N2)
 *
 * Toward the modem: emulates a TCP server endpoint (SYN-ACK / ACK / data / FIN);
 * toward the Internet: the host opens the real connection on the modem's behalf
 * with non-blocking sockets and pumps data both ways.
 * The state machine runs only in the host thread context, no locking needed.
 * Depends on g_proxy / proxy_fill_ip_hdr / proxy_enqueue_down / byte-order
 * helpers / checksum helpers in sim_proxy_internal.h.
 */

#include "sim_proxy_internal.h"   /* winsock2.h is included by the internal header
                                   * (FD_SETSIZE=512 must take effect before winsock2.h) */

/* ------------------------------------------------------------------ */
/* Internal helpers                                                      */
/* ------------------------------------------------------------------ */

static uint16_t proxy_tcp_adv_wnd(const proxy_tcp_t *c)
{
    int free_sp = PROXY_TCP_BUF - c->rx_len;

    if (free_sp < 0)
        free_sp = 0;
    if (free_sp > 65535)
        free_sp = 65535;
    return (uint16_t)free_sp;
}

static void proxy_tcp_send_seg(proxy_tcp_t *c, uint8_t flags, uint32_t seq,
                               uint32_t ack, const uint8_t *data, int dlen,
                               int with_mss)
{
    uint8_t pkt[PROXY_MAX_IP_LEN];
    uint8_t *tcp = pkt + 20;
    int hdrlen = with_mss ? 24 : 20;
    int tcp_len;

    if (dlen < 0)
        dlen = 0;
    if (dlen > PROXY_MAX_IP_LEN - 20 - hdrlen)
        dlen = PROXY_MAX_IP_LEN - 20 - hdrlen;
    tcp_len = hdrlen + dlen;

    memset(tcp, 0, (size_t)hdrlen);
    wr16n(tcp + 0, c->rmt_port);
    wr16n(tcp + 2, c->loc_port);
    wr32n(tcp + 4, seq);
    wr32n(tcp + 8, ack);
    tcp[12] = (uint8_t)((hdrlen / 4) << 4);
    tcp[13] = flags;
    wr16n(tcp + 14, proxy_tcp_adv_wnd(c));
    if (with_mss) {
        tcp[20] = 2;
        tcp[21] = 4;
        wr16n(tcp + 22, PROXY_TCP_MSS);
    }
    if (dlen > 0)
        memcpy(tcp + hdrlen, data, (size_t)dlen);

    wr16n(tcp + 16,
          proxy_tcp_cksum(c->rmt_ip, c->src_ip, tcp, (uint16_t)tcp_len));
    proxy_fill_ip_hdr(pkt, c->rmt_ip, c->src_ip, 6, (uint16_t)tcp_len);
    proxy_enqueue_down(pkt, 20 + tcp_len);

    if (dlen > 0)
        g_proxy.tcp_bytes_dn += (uint32_t)dlen;
}

static void proxy_tcp_send_ack(proxy_tcp_t *c)
{
    proxy_tcp_send_seg(c, PROXY_TF_ACK, c->snd_nxt, c->rcv_nxt, NULL, 0, 0);
    c->ack_needed = 0;
    c->delack_cnt = 0;
}

static void proxy_tcp_send_synack(proxy_tcp_t *c)
{
    proxy_tcp_send_seg(c, PROXY_TF_SYN | PROXY_TF_ACK, c->iss, c->irs + 1,
                       NULL, 0, 1);
}

static void proxy_tcp_send_rst(proxy_tcp_t *c)
{
    proxy_tcp_send_seg(c, PROXY_TF_RST | PROXY_TF_ACK, c->snd_nxt, c->rcv_nxt,
                       NULL, 0, 0);
}

static void proxy_tcp_close(proxy_tcp_t *c)
{
    if (c->hs != INVALID_SOCKET) {
        closesocket(c->hs);
        c->hs = INVALID_SOCKET;
    }
    c->in_use = 0;
    c->state  = TCPS_CLOSED;
}

static void proxy_tcp_abort(proxy_tcp_t *c)
{
    if (c->state >= TCPS_SYN_RCVD && !c->our_fin_sent)
        proxy_tcp_send_rst(c);
    if (proxy_log_gate(&g_proxy.log_tcp_ms))
        LOGW(PROXY_TAG, "TCP abort: :%u -> %s:%u (state=%d hs=%d eof=%d)",
             (unsigned)c->loc_port, proxy_ip_str(c->rmt_ip),
             (unsigned)c->rmt_port, (int)c->state, c->connected, c->host_eof);
    proxy_tcp_close(c);
}

static proxy_tcp_t *proxy_tcp_find(uint16_t loc_port, uint32_t rmt_ip,
                                   uint16_t rmt_port)
{
    int i;

    for (i = 0; i < PROXY_TCP_MAX; i++) {
        proxy_tcp_t *c = &g_proxy.tcp[i];

        if (c->in_use && c->loc_port == loc_port &&
            c->rmt_ip == rmt_ip && c->rmt_port == rmt_port)
            return c;
    }
    return NULL;
}

static void proxy_tcp_arm_rto(proxy_tcp_t *c)
{
    c->rto_pending  = 1;
    c->rto_deadline = GetTickCount() + c->rto_ms;
}

static void proxy_tcp_process_ack(proxy_tcp_t *c, uint32_t ack)
{
    int seqs, drop;

    if (PROXY_SEQ_LEQ(ack, c->snd_una))
        return;
    /* The upper bound is snd_nxt (the highest sequence number actually sent),
     * not snd_una+tx_len: FIN is only sent after the tx buffer drains, and
     * afterwards snd_nxt=snd_una+1 while tx_len=0. The old code judged the ACK
     * of our FIN as invalid and dropped it -> our_fin_acked was never set ->
     * FIN retransmitted to the limit -> abort RST, connection could not close
     * normally */
    if (PROXY_SEQ_GT(ack, c->snd_nxt))
        return;

    seqs = (int)(ack - c->snd_una);
    drop = seqs > c->tx_len ? c->tx_len : seqs;
    if (drop > 0) {
        memmove(c->tx, c->tx + drop, (size_t)(c->tx_len - drop));
        c->tx_len -= drop;
    }
    c->snd_una = ack;
    c->retrans = 0;
    c->rto_ms  = PROXY_TCP_RTO_MS;
    if (c->snd_una == c->snd_nxt)
        c->rto_pending = 0;
    if (c->our_fin_sent && PROXY_SEQ_LEQ(c->snd_nxt, ack))
        c->our_fin_acked = 1;
}

static void proxy_tcp_try_send(proxy_tcp_t *c)
{
    if (c->state >= TCPS_ESTABLISHED && !c->our_fin_sent) {
        for (;;) {
            int in_flight = (int)(c->snd_nxt - c->snd_una);
            int win       = (int)c->snd_wnd;
            int unsent_off, unsent_len, seg;

            if (win <= 0 || in_flight >= win) {
                /* Zero window with nothing in flight: the modem won't send
                 * window updates on its own. Reuse the RTO mechanism to
                 * periodically resend unacked data (RFC 1122 section 4.2.2.16
                 * persistence-timer semantics, exponential backoff capped at
                 * 4s) -- as soon as the peer's window-update ACK arrives,
                 * process_ack resumes normal sending; otherwise deadlocked
                 * until reap */
                if (win <= 0 && in_flight == 0 && c->tx_len > 0 && !c->rto_pending)
                    proxy_tcp_arm_rto(c);
                break;
            }
            unsent_off = in_flight;
            unsent_len = c->tx_len - unsent_off;
            if (unsent_len <= 0)
                break;
            seg = unsent_len;
            if (seg > (int)c->modem_mss)
                seg = (int)c->modem_mss;
            if (seg > win - in_flight)
                seg = win - in_flight;
            proxy_tcp_send_seg(c, PROXY_TF_ACK | PROXY_TF_PSH, c->snd_nxt,
                               c->rcv_nxt, c->tx + unsent_off, seg, 0);
            c->snd_nxt += (uint32_t)seg;
            proxy_tcp_arm_rto(c);
            c->ack_needed  = 0;
            c->delack_cnt  = 0;
        }
    }

    if (!c->our_fin_sent && c->state >= TCPS_ESTABLISHED &&
        c->tx_len == 0 && c->snd_una == c->snd_nxt &&
        (c->host_eof || c->modem_fin_seen)) {
        proxy_tcp_send_seg(c, PROXY_TF_FIN | PROXY_TF_ACK, c->snd_nxt, c->rcv_nxt,
                           NULL, 0, 0);
        c->our_fin_sent = 1;
        c->snd_nxt++;
        proxy_tcp_arm_rto(c);
        c->ack_needed  = 0;
        c->delack_cnt  = 0;
        c->state = c->modem_fin_seen ? TCPS_LAST_ACK : TCPS_FIN_WAIT_1;
    }
}

static void proxy_tcp_rx_data(proxy_tcp_t *c, uint32_t seq,
                              const uint8_t *data, int len)
{
    int space, take;

    if (len <= 0)
        return;
    if (seq != c->rcv_nxt) {
        proxy_tcp_send_ack(c);
        return;
    }
    space = PROXY_TCP_BUF - c->rx_len;
    take  = len < space ? len : space;
    if (take > 0) {
        memcpy(c->rx + c->rx_len, data, (size_t)take);
        c->rx_len  += take;
        c->rcv_nxt += (uint32_t)take;
        g_proxy.tcp_bytes_up += (uint32_t)take;
    }
    c->ack_needed  = 1;
    /* Delayed-ACK time cap: while drain_rx keeps returning WOULDBLOCK,
     * delack_cnt is reset by every new segment; a pure counter would postpone
     * the ACK indefinitely (lwIP window deadlock) */
    c->delack_deadline = GetTickCount() + PROXY_TCP_DELACK_MS;
}

static void proxy_tcp_drain_rx(proxy_tcp_t *c)
{
    int sent;

    c->want_write = 0;
    if (c->rx_len == 0 || !c->connected || c->hs == INVALID_SOCKET ||
        c->host_shutdown)
        return;

    sent = send(c->hs, (const char *)c->rx, c->rx_len, 0);
    if (sent == SOCKET_ERROR) {
        int e = WSAGetLastError();

        if (e == WSAEWOULDBLOCK) {
            c->want_write = 1;
            return;
        }
        proxy_tcp_abort(c);
        return;
    }
    if (sent > 0) {
        memmove(c->rx, c->rx + sent, (size_t)(c->rx_len - sent));
        c->rx_len -= sent;
        proxy_tcp_send_ack(c);
    }
    if (c->rx_len > 0)
        c->want_write = 1;

    if (c->modem_fin_seen && c->rx_len == 0 && !c->host_shutdown) {
        shutdown(c->hs, SD_SEND);
        c->host_shutdown = 1;
    }
}

static void proxy_tcp_host_readable(proxy_tcp_t *c)
{
    int space, n;

    if (!c->connected || c->hs == INVALID_SOCKET || c->host_eof)
        return;
    space = PROXY_TCP_BUF - c->tx_len;
    if (space <= 0)
        return;

    n = recv(c->hs, (char *)(c->tx + c->tx_len), space, 0);
    if (n == 0) {
        c->host_eof = 1;
        c->last_ms  = GetTickCount();
        proxy_tcp_try_send(c);
        return;
    }
    if (n == SOCKET_ERROR) {
        int e = WSAGetLastError();

        if (e == WSAEWOULDBLOCK)
            return;
        proxy_tcp_abort(c);
        return;
    }
    c->tx_len += n;
    c->last_ms = GetTickCount();
    proxy_tcp_try_send(c);
}

static void proxy_tcp_on_writable(proxy_tcp_t *c)
{
    if (c->connecting) {
        int err = 0, elen = (int)sizeof(err);

        getsockopt(c->hs, SOL_SOCKET, SO_ERROR, (char *)&err, &elen);
        c->connecting = 0;
        if (err != 0) {
            LOGW(PROXY_TAG, "TCP connect %s:%u failed (err=%d) -> RST to modem",
                 proxy_ip_str(c->rmt_ip), (unsigned)c->rmt_port, err);
            proxy_tcp_abort(c);
            return;
        }
        c->connected = 1;
        c->last_ms   = GetTickCount();
        LOGI(PROXY_TAG, "TCP host connected: :%u -> %s:%u",
             (unsigned)c->loc_port, proxy_ip_str(c->rmt_ip),
             (unsigned)c->rmt_port);
    }
    if (c->in_use && c->rx_len > 0)
        proxy_tcp_drain_rx(c);
}

static proxy_tcp_t *proxy_tcp_new(uint32_t src_ip, uint16_t loc_port, uint32_t rmt_ip,
                                  uint16_t rmt_port, uint32_t irs,
                                  const uint8_t *syn_opts, int opts_len)
{
    proxy_tcp_t *c = NULL;
    struct sockaddr_in sa;
    u_long nb = 1;
    int i, oi = 0;
    uint16_t mss = 536;

    for (i = 0; i < PROXY_TCP_MAX; i++) {
        if (!g_proxy.tcp[i].in_use && g_proxy.tcp[i].hs == INVALID_SOCKET) {
            c = &g_proxy.tcp[i];
            break;
        }
    }
    if (c == NULL) {
        if (proxy_log_gate(&g_proxy.log_tcp_ms))
            LOGW(PROXY_TAG, "TCP table full (%d) -- SYN dropped", PROXY_TCP_MAX);
        return NULL;
    }

    while (syn_opts != NULL && oi + 1 < opts_len) {
        uint8_t kind = syn_opts[oi], olen;

        if (kind == 0)
            break;
        if (kind == 1) { oi++; continue; }
        olen = syn_opts[oi + 1];
        if (olen < 2 || oi + olen > opts_len)
            break;
        if (kind == 2 && olen == 4)
            mss = rd16n(syn_opts + oi + 2);
        oi += olen;
    }

    memset(c, 0, sizeof(*c));
    c->hs = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c->hs == INVALID_SOCKET) {
        LOGW(PROXY_TAG, "TCP host socket create failed (err=%d)",
             WSAGetLastError());
        return NULL;
    }
    ioctlsocket(c->hs, FIONBIO, &nb);

    if (g_proxy.host_ip) {
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        memcpy(&sa.sin_addr.s_addr, &g_proxy.host_ip, 4);
        sa.sin_port = 0;
        if (bind(c->hs, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
            int e = WSAGetLastError();
            LOGW(PROXY_TAG, "TCP bind to %s failed (err=%d)", proxy_ip_str(g_proxy.host_ip), e);
            closesocket(c->hs);
            c->hs = INVALID_SOCKET;
            return NULL;
        }
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(rmt_port);
    memcpy(&sa.sin_addr.s_addr, &rmt_ip, 4);
    if (connect(c->hs, (struct sockaddr *)&sa, sizeof(sa)) == SOCKET_ERROR) {
        int e = WSAGetLastError();

        if (e != WSAEWOULDBLOCK) {
            LOGW(PROXY_TAG, "TCP connect %s:%u failed (err=%d)",
                 proxy_ip_str(rmt_ip), (unsigned)rmt_port, e);
            closesocket(c->hs);
            c->hs = INVALID_SOCKET;
            return NULL;
        }
        c->connecting = 1;
    } else {
        c->connected = 1;
    }

    c->in_use     = 1;
    c->rmt_ip     = rmt_ip;
    c->rmt_port   = rmt_port;
    c->loc_port   = loc_port;
    c->modem_mss  = mss;
    c->src_ip     = src_ip;
    c->iss        = (uint32_t)GetTickCount() ^ ((uint32_t)loc_port << 16)
                    ^ 0x5A5A5A5Au;
    c->irs        = irs;
    c->snd_una    = c->iss;
    c->snd_nxt    = c->iss + 1;
    c->rcv_nxt    = irs + 1;
    c->snd_wnd    = PROXY_TCP_BUF;
    c->state      = TCPS_SYN_RCVD;
    c->rto_ms     = PROXY_TCP_RTO_MS;
    c->last_ms    = GetTickCount();
    if (c->connecting)
        c->connect_deadline = GetTickCount() + PROXY_TCP_CONNECT_MS;

    proxy_tcp_send_synack(c);

    g_proxy.tcp_conns++;
    LOGI(PROXY_TAG, "TCP new: :%u -> %s:%u (mss=%u)", (unsigned)loc_port,
         proxy_ip_str(rmt_ip), (unsigned)rmt_port, (unsigned)mss);
    return c;
}

/* ------------------------------------------------------------------ */
/* Public interface                                                      */
/* ------------------------------------------------------------------ */

void proxy_tcp_init(void)
{
    int i;

    for (i = 0; i < PROXY_TCP_MAX; i++)
        g_proxy.tcp[i].hs = INVALID_SOCKET;
}

void proxy_uplink_tcp(uint32_t src, uint32_t dst,
                      const uint8_t *l4, int l4len)
{
    uint16_t sport, dport, wnd;
    uint32_t seq, ack;
    int dataofs, plen;
    uint8_t flags;
    proxy_tcp_t *c;

    if (l4len < 20)
        return;
    sport   = rd16n(l4 + 0);
    dport   = rd16n(l4 + 2);
    seq     = rd32n(l4 + 4);
    ack     = rd32n(l4 + 8);
    dataofs = (l4[12] >> 4) * 4;
    flags   = l4[13];
    wnd     = rd16n(l4 + 14);
    if (dataofs < 20 || dataofs > l4len)
        return;
    plen = l4len - dataofs;

    c = proxy_tcp_find(sport, dst, dport);

    if (c == NULL) {
        if (flags & PROXY_TF_SYN)
            proxy_tcp_new(src, sport, dst, dport, seq, l4 + 20, dataofs - 20);
        else if (proxy_log_gate(&g_proxy.log_tcp_ms))
            LOGW(PROXY_TAG, "TCP non-SYN for unknown conn :%u -> %s:%u dropped",
                 (unsigned)sport, proxy_ip_str(dst), (unsigned)dport);
        return;
    }

    c->last_ms = GetTickCount();

    if (flags & PROXY_TF_RST) {
        LOGI(PROXY_TAG, "TCP RST from modem: :%u -> %s:%u", (unsigned)sport,
             proxy_ip_str(c->rmt_ip), (unsigned)c->rmt_port);
        proxy_tcp_close(c);
        return;
    }
    if (flags & PROXY_TF_SYN) {
        proxy_tcp_send_synack(c);
        return;
    }
    if (!(flags & PROXY_TF_ACK))
        return;

    c->snd_wnd = wnd;
    proxy_tcp_process_ack(c, ack);

    if (c->state == TCPS_SYN_RCVD) {
        c->state = TCPS_ESTABLISHED;
        LOGI(PROXY_TAG, "TCP established: :%u -> %s:%u",
             (unsigned)c->loc_port, proxy_ip_str(c->rmt_ip),
             (unsigned)c->rmt_port);
    }

    if (plen > 0 && c->state >= TCPS_ESTABLISHED && !c->modem_fin_seen)
        proxy_tcp_rx_data(c, seq, l4 + dataofs, plen);

    if ((flags & PROXY_TF_FIN) && !c->modem_fin_seen &&
        seq + (uint32_t)plen == c->rcv_nxt) {
        c->rcv_nxt++;
        c->modem_fin_seen = 1;
        proxy_tcp_send_ack(c);
        if (c->state < TCPS_CLOSE_WAIT)
            c->state = TCPS_CLOSE_WAIT;
    }

    if (c->in_use) {
        proxy_tcp_drain_rx(c);
        proxy_tcp_try_send(c);
    }
}

void proxy_tcp_select(fd_set *rfds, fd_set *wfds, SOCKET *maxs)
{
    int i;

    for (i = 0; i < PROXY_TCP_MAX; i++) {
        proxy_tcp_t *c = &g_proxy.tcp[i];

        if (!c->in_use || c->hs == INVALID_SOCKET)
            continue;
        if (c->connecting || c->want_write)
            FD_SET(c->hs, wfds);
        if (c->connected && !c->host_eof &&
            c->state >= TCPS_ESTABLISHED && c->tx_len < PROXY_TCP_BUF)
            FD_SET(c->hs, rfds);
        if (c->hs > *maxs)
            *maxs = c->hs;
    }
}

void proxy_tcp_process(int n, fd_set *rfds, fd_set *wfds)
{
    int i;

    if (n <= 0)
        return;
    for (i = 0; i < PROXY_TCP_MAX; i++) {
        proxy_tcp_t *c = &g_proxy.tcp[i];

        if (c->in_use && c->hs != INVALID_SOCKET &&
            FD_ISSET(c->hs, wfds))
            proxy_tcp_on_writable(c);
    }
    for (i = 0; i < PROXY_TCP_MAX; i++) {
        proxy_tcp_t *c = &g_proxy.tcp[i];

        if (c->in_use && c->hs != INVALID_SOCKET &&
            FD_ISSET(c->hs, rfds))
            proxy_tcp_host_readable(c);
    }
}

void proxy_tcp_sweep(uint32_t tick_ms)
{
    DWORD now = GetTickCount();
    int i;

    (void)tick_ms;   /* all timing switched to absolute GetTickCount deadlines, no longer accumulated per call */

    for (i = 0; i < PROXY_TCP_MAX; i++) {
        proxy_tcp_t *c = &g_proxy.tcp[i];

        if (!c->in_use)
            continue;

        /* host connect() timeout: against a black-hole address the socket
         * never becomes writable, and until idle reap it keeps ACKing on the
         * modem's behalf -- the application layer sees "connected but no
         * response". Abort on timeout so the lwIP side gets a RST quickly */
        if (c->connecting && (int)(now - c->connect_deadline) >= 0) {
            LOGW(PROXY_TAG, "TCP connect timeout :%u -> %s:%u",
                 (unsigned)c->loc_port, proxy_ip_str(c->rmt_ip),
                 (unsigned)c->rmt_port);
            proxy_tcp_abort(c);
            continue;
        }

        if (c->rto_pending && c->state >= TCPS_SYN_RCVD &&
            (int)(now - c->rto_deadline) >= 0) {
            c->retrans++;
            if (c->retrans > PROXY_TCP_MAX_RETRANS) {
                LOGW(PROXY_TAG, "TCP retransmit giveup :%u -> %s:%u",
                     (unsigned)c->loc_port, proxy_ip_str(c->rmt_ip),
                     (unsigned)c->rmt_port);
                proxy_tcp_abort(c);
                continue;
            }
            c->rto_ms *= 2;
            if (c->rto_ms > PROXY_TCP_RTO_MAX_MS)
                c->rto_ms = PROXY_TCP_RTO_MAX_MS;
            c->rto_deadline = now + c->rto_ms;
            c->snd_nxt = c->snd_una;
            if (c->state == TCPS_SYN_RCVD)
                proxy_tcp_send_synack(c);
            else if (c->our_fin_sent && !c->our_fin_acked) {
                proxy_tcp_send_seg(c, PROXY_TF_FIN | PROXY_TF_ACK, c->snd_una,
                                 c->rcv_nxt, NULL, 0, 0);
                c->snd_nxt = c->snd_una + 1;
            } else
                proxy_tcp_try_send(c);   /* includes zero-window persistence resend */
        }

        if (c->ack_needed) {
            /* Dual condition: counter (fast path) + time cap (prevents starvation when drain is blocked) */
            if (c->delack_cnt > 0 && (int)(now - c->delack_deadline) < 0) {
                c->delack_cnt--;
            } else {
                proxy_tcp_send_ack(c);
            }
        }

        if (c->our_fin_acked && c->modem_fin_seen) {
            LOGI(PROXY_TAG, "TCP closed cleanly: :%u -> %s:%u",
                 (unsigned)c->loc_port, proxy_ip_str(c->rmt_ip),
                 (unsigned)c->rmt_port);
            proxy_tcp_close(c);
            continue;
        }
        if (c->our_fin_acked && (now - c->last_ms) > 3000u) {
            LOGI(PROXY_TAG, "TCP reaped: our FIN acked, modem silent 3s "
                 ":%u -> %s:%u", (unsigned)c->loc_port, proxy_ip_str(c->rmt_ip),
                 (unsigned)c->rmt_port);
            proxy_tcp_close(c);
            continue;
        }
        {
            /* ESTABLISHED uses a long idle timeout (real networks don't tear
             * down silent long-lived connections; MQTT keepalive can exceed
             * 120s); intermediate states use a short idle timeout for fast
             * reclamation */
            DWORD idle_ms = (c->state == TCPS_ESTABLISHED)
                                ? PROXY_TCP_EST_IDLE_MS : PROXY_TCP_IDLE_MS;

            if ((now - c->last_ms) > idle_ms) {
                LOGI(PROXY_TAG, "TCP idle reap :%u (state=%d idle>%ums)",
                     (unsigned)c->loc_port, (int)c->state, (unsigned)idle_ms);
                proxy_tcp_close(c);
                continue;
            }
        }
    }
}
