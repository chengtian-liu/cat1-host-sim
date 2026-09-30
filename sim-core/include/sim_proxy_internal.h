/*
 * sim_proxy_internal.h -- internal shared definitions of the in-process user-mode proxy
 *
 * Common header for sim_proxy.c / sim_proxy_udp.c / sim_proxy_tcp.c /
 * sim_proxy_icmp.c: constants, types, inline helpers, global state
 * references, cross-module interface declarations.
 * Not exposed to the rest of the simulator (outside code only uses the
 * public API in sim_proxy.h).
 *
 * sim-core is a pure Win32 module with no chip SDK dependencies. All
 * operations that need chip-environment capabilities (memory allocation,
 * downlink injection, RTOS delay) are invoked indirectly through the
 * callback table in sim_core_api.h. The binding is done by the target-xxx
 * directories at startup.
 *
 * Inclusion convention: winsock2.h is included solely by this header (.c
 * files must not include it themselves).
 * Reason 1: FD_SETSIZE must be defined before winsock2.h is first included (see below);
 * Reason 2: the _WINSOCKAPI_ guard of winsock2 keeps winsock.h from being
 * pulled in, keeping the fd_set definition consistent across all proxy modules.
 */

#ifndef SIM_PROXY_INTERNAL_H
#define SIM_PROXY_INTERNAL_H

/* winsock's fd_set defaults to FD_SETSIZE=64, and FD_SET silently ignores
 * entries beyond the limit -- under the UDP 128 + TCP 128 mapping, once more
 * than 64 sockets exist, the extra sockets will never be monitored by select
 * (connections hang silently). Raise to 512 to cover 128+128+1(wake).
 * Must take effect before any inclusion of winsock2.h */
#ifndef FD_SETSIZE
#define FD_SETSIZE 512
#endif

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_core_api.h"
#include "sim_log.h"
#include "sim_proxy.h"

#define PROXY_TAG "SIMPROXY"

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define PROXY_IPv4(a, b, c, d) (((uint32_t)(a)) | ((uint32_t)(b) << 8) | \
                                ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define PROXY_IP_CLIENT   PROXY_IPv4(10, 0, 0, 2)
#define PROXY_IP_GW       PROXY_IPv4(10, 0, 0, 1)
#define PROXY_IP_BCAST    PROXY_IPv4(255, 255, 255, 255)
#define PROXY_IP_MASK     PROXY_IPv4(255, 255, 255, 0)

#define PROXY_QSIZE       256
#define PROXY_UDP_MAX     128
#define PROXY_UDP_IDLE_MS 60000u
#define PROXY_IP_TTL      64
#define PROXY_IP_ID_SEED  0x4200u
#define PROXY_SELECT_MS   50
#define PROXY_ICMP_TO_MS  2000
#define PROXY_MAX_IP_LEN  1500
#define PROXY_ICMP_MAX_DATA (PROXY_MAX_IP_LEN - 28)  /* max echo-reply payload */

/* ---- TCP ---- */
#define PROXY_TCP_MAX        128
#define PROXY_TCP_BUF        16384
#define PROXY_TCP_MSS        1460
#define PROXY_TCP_RTO_MS     400
#define PROXY_TCP_RTO_MAX_MS   4000
#define PROXY_TCP_MAX_RETRANS 10
#define PROXY_TCP_DELACK_CNT  4
#define PROXY_TCP_DELACK_MS 200u     /* delayed-ACK time cap (prevents starvation when drain blocks) */
#define PROXY_TCP_IDLE_MS    120000u /* idle reclaim for intermediate states (SYN_RCVD etc.) */
#define PROXY_TCP_EST_IDLE_MS 1800000u /* ESTABLISHED idle reclaim: 30min. A real network
                                        * does not tear down a connection after 2min of
                                        * silence (MQTT keepalive can be >120s); too short
                                        * would kill normal long-lived connections */
#define PROXY_TCP_CONNECT_MS 3000u   /* host connect() timeout: for a black-hole address
                                        * (firewall DROP) the socket never becomes
                                        * writable; time out and RST so the lwIP side
                                        * fails fast instead of hanging 120s */

#define PROXY_SEQ_LT(a, b)  ((int32_t)((uint32_t)(a) - (uint32_t)(b)) < 0)
#define PROXY_SEQ_LEQ(a, b) ((int32_t)((uint32_t)(a) - (uint32_t)(b)) <= 0)
#define PROXY_SEQ_GT(a, b)  ((int32_t)((uint32_t)(a) - (uint32_t)(b)) > 0)

#define PROXY_TF_FIN  0x01
#define PROXY_TF_SYN  0x02
#define PROXY_TF_RST  0x04
#define PROXY_TF_PSH  0x08
#define PROXY_TF_ACK  0x10

/* ------------------------------------------------------------------ */
/* Type definitions                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    void     *buf;
    uint16_t  len;
} proxy_slot_t;

typedef struct {
    CRITICAL_SECTION cs;
    proxy_slot_t     s[PROXY_QSIZE];
    int              head, tail, count;
} proxy_q_t;

typedef struct {
    SOCKET   s;
    uint32_t rmt_ip;
    uint16_t rmt_port;
    uint16_t loc_port;
    uint32_t src_ip;
    DWORD    last_ms;
} proxy_udp_map_t;

typedef enum {
    TCPS_CLOSED = 0,
    TCPS_SYN_RCVD,
    TCPS_ESTABLISHED,
    TCPS_FIN_WAIT_1,
    TCPS_CLOSE_WAIT,
    TCPS_LAST_ACK,
    TCPS_TIME_WAIT,
} proxy_tcp_state_t;

typedef struct {
    int      in_use;
    SOCKET   hs;
    int      connecting;
    int      connected;

    uint32_t rmt_ip;
    uint16_t rmt_port;
    uint16_t loc_port;
    uint16_t modem_mss;
    uint32_t src_ip;

    proxy_tcp_state_t state;

    uint32_t iss, irs;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint32_t snd_wnd;

    uint8_t  tx[PROXY_TCP_BUF];
    int      tx_len;

    uint8_t  rx[PROXY_TCP_BUF];
    int      rx_len;
    int      want_write;

    int      modem_fin_seen;
    int      host_eof;
    int      host_shutdown;
    int      our_fin_sent;
    int      our_fin_acked;

    int      ack_needed;
    int      delack_cnt;

    uint32_t rto_ms;
    DWORD    rto_deadline;     /* RTO expiry instant (absolute GetTickCount value).
                                * WARNING: the old implementation accumulated ticks
                                * per sweep call, but select returns in <1ms when
                                * events occur -- under high traffic a 400ms RTO
                                * "expired" within milliseconds, causing spurious
                                * retransmissions */
    int      retrans;
    int      rto_pending;

    DWORD    delack_deadline;  /* latest instant to emit the delayed ACK */
    DWORD    connect_deadline; /* timeout instant of the connecting state */

    DWORD    last_ms;
} proxy_tcp_t;

typedef struct {
    volatile LONG  up;

    proxy_q_t      ulq;
    proxy_q_t      dlq;

    SOCKET         wake_recv;
    SOCKET         wake_send;
    struct sockaddr_in wake_addr;

    HANDLE         icmp_h;
    HANDLE         host_thread;

    uint32_t       dns[3];
    int            dns_count;
    uint32_t       host_ip;

    proxy_udp_map_t  udp[PROXY_UDP_MAX];
    proxy_tcp_t      tcp[PROXY_TCP_MAX];
    uint16_t         ip_id;

    uint32_t       up_pkts, dn_pkts;
    uint32_t       up_drop_qfull, dn_drop_qfull, dn_drop_nomem;
    uint32_t       udp_maps, icmp_replies;
    uint32_t       tcp_conns, tcp_bytes_up, tcp_bytes_dn;

    DWORD          log_tcp_ms;
    DWORD          log_frag_ms;
    DWORD          log_proto_ms;
    DWORD          log_drop_ms;
} sim_proxy_t;

extern sim_proxy_t g_proxy;

/* ------------------------------------------------------------------ */
/* Byte-order helpers -- two semantics, never mix them:               */
/*                                                                    */
/* A) rd32/wr32 = little-endian read/write = transparent "wire bytes  */
/*    <-> register" copy (equivalent to memcpy). Use only for fields  */
/*    that "round-trip byte-for-byte as-is": IP addresses (PROXY_IPv4 */
/*    constants share the same space as inet_addr), etc.              */
/*                                                                    */
/* B) rd16n/wr16n = network byte order (big-endian) <-> host integer. */
/*    All protocol integer fields (total length, ports, flags, id/seq,*/
/*    UDP length) must go through this set.                           */
/* ------------------------------------------------------------------ */

static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static inline uint16_t rd16n(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline void wr16n(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static inline uint32_t rd32n(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void wr32n(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}

/* ------------------------------------------------------------------ */
/* Checksums (inline; shared by UDP/TCP/ICMP)                         */
/* ------------------------------------------------------------------ */

static inline uint16_t proxy_cksum(const uint8_t *p, int len, uint32_t acc)
{
    while (len > 1) {
        acc += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len != 0)
        acc += (uint32_t)p[0] << 8;
    while (acc >> 16)
        acc = (acc & 0xffff) + (acc >> 16);
    return (uint16_t)(~acc & 0xffff);
}

static inline uint16_t proxy_udp_cksum(uint32_t src, uint32_t dst,
                                const uint8_t *udp, uint16_t ulen)
{
    const uint8_t *p;
    uint32_t acc = 17u + ulen;

    p = (const uint8_t *)&src;
    acc += rd16n(p);
    acc += rd16n(p + 2);
    p = (const uint8_t *)&dst;
    acc += rd16n(p);
    acc += rd16n(p + 2);
    return proxy_cksum(udp, ulen, acc);
}

static inline uint16_t proxy_tcp_cksum(uint32_t src, uint32_t dst,
                                const uint8_t *tcp, uint16_t tlen)
{
    const uint8_t *p;
    uint32_t acc = 6u + tlen;

    p = (const uint8_t *)&src;
    acc += rd16n(p);
    acc += rd16n(p + 2);
    p = (const uint8_t *)&dst;
    acc += rd16n(p);
    acc += rd16n(p + 2);
    return proxy_cksum(tcp, tlen, acc);
}

/* ------------------------------------------------------------------ */
/* Utility functions (inline)                                         */
/* ------------------------------------------------------------------ */

static const char *proxy_ip_str(uint32_t net_ip)
{
    static char bufs[4][16];
    static int next;
    struct in_addr a;
    const char *s;

    memcpy(&a, &net_ip, 4);
    s = inet_ntoa(a);
    next = (next + 1) & 3;
    strncpy(bufs[next], s, sizeof(bufs[next]) - 1);
    bufs[next][sizeof(bufs[next]) - 1] = '\0';
    return bufs[next];
}

static int proxy_log_gate(DWORD *last)
{
    DWORD now = GetTickCount();

    if (now - *last < 3000u)
        return 0;
    *last = now;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Cross-module function declarations (implemented in sim_proxy.c)    */
/* ------------------------------------------------------------------ */

void proxy_fill_ip_hdr(uint8_t *ip, uint32_t src, uint32_t dst,
                       uint8_t proto, uint16_t payload_len);
void proxy_enqueue_down(const uint8_t *ip, int len);

/* ---- Submodule entry points ---- */

/* sim_proxy_icmp.c */
void proxy_icmp_init(void);      /* start the probe thread (g_proxy.icmp_h must be ready first) */
void proxy_icmp_poll(void);      /* host thread collects probe results each round and injects replies */
void proxy_uplink_icmp(uint32_t src, uint32_t dst,
                       const uint8_t *l4, int l4len);

/* sim_proxy_udp.c */
void proxy_udp_init(void);
void proxy_uplink_udp(uint32_t src, uint32_t dst,
                      const uint8_t *l4, int l4len);
void proxy_udp_select(fd_set *rfds, SOCKET *maxs);
void proxy_udp_process(int n, fd_set *rfds);
void proxy_udp_sweep(void);

/* sim_proxy_tcp.c */
void proxy_tcp_init(void);
void proxy_uplink_tcp(uint32_t src, uint32_t dst,
                      const uint8_t *l4, int l4len);
void proxy_tcp_select(fd_set *rfds, fd_set *wfds, SOCKET *maxs);
void proxy_tcp_process(int n, fd_set *rfds, fd_set *wfds);
void proxy_tcp_sweep(uint32_t tick_ms);

#endif /* SIM_PROXY_INTERNAL_H */
