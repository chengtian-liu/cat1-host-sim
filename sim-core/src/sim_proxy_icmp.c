/*
 * sim_proxy_icmp.c -- ICMP echo handling (in-process user-mode proxy data plane submodule)
 *
 * The host probes reachability with IcmpSendEcho2Ex; a forged echo reply is
 * injected into the downlink ring.
 * WARNING: IcmpSendEcho is a synchronous blocking call (up to PROXY_ICMP_TO_MS),
 * while the host thread simultaneously pumps TCP/UDP -- calling it directly in
 * the host thread would stall the whole data plane for 2s on one ping to an
 * unreachable address (TCP RTO is only 400ms; in-flight connections would all
 * suffer spurious retransmissions). So probing runs serially in a dedicated
 * Win32 thread (a pure thread outside the core, blocking waits are free to
 * use); the host thread only enqueues jobs / polls results, never blocking.
 * Depends on g_proxy / proxy_fill_ip_hdr / proxy_enqueue_down / byte-order
 * helpers / checksum helpers in sim_proxy_internal.h.
 */

#include "sim_proxy_internal.h"   /* winsock2.h is included by the internal header
                                   * (FD_SETSIZE=512 must take effect before winsock2.h) */

/* ------------------------------------------------------------------ */
/* ICMP probe thread (outside the core; accesses job fields mutually      */
/* exclusively with the host thread)                                    */
/* ------------------------------------------------------------------ */

static CRITICAL_SECTION s_icmp_cs;
static HANDLE           s_icmp_wake;          /* manual-reset event */
static HANDLE           s_icmp_thread;
static volatile LONG    s_icmp_quit;

/* Current probe job (0 = no job) */
static uint32_t s_job_dst;
static uint8_t  s_job_payload[PROXY_ICMP_MAX_DATA];
static int      s_job_plen;
static uint16_t s_job_id, s_job_seq;
static uint32_t s_job_src;
static int      s_job_active;                 /* guarded by s_icmp_cs */

/* Probe result (0 = none; 1 = success, dst/... valid; 2 = failure, silently dropped) */
static int      s_res_valid;
static int      s_res_ok;
static uint32_t s_res_dst, s_res_src;
static uint16_t s_res_id, s_res_seq;
static uint8_t  s_res_payload[PROXY_ICMP_MAX_DATA];
static int      s_res_plen;

static DWORD WINAPI icmp_probe_thread(LPVOID arg)
{
    (void)arg;

    for (;;) {
        uint8_t  rbuf[sizeof(ICMP_ECHO_REPLY) + PROXY_ICMP_MAX_DATA];
        uint32_t dst, src;
        uint16_t id, seq;
        uint8_t  payload[PROXY_ICMP_MAX_DATA];
        int      plen, ok = 0;
        DWORD    nr;

        if (WaitForSingleObject(s_icmp_wake, INFINITE) != WAIT_OBJECT_0)
            break;
        ResetEvent(s_icmp_wake);
        if (InterlockedCompareExchange(&s_icmp_quit, 0, 0))
            break;

        EnterCriticalSection(&s_icmp_cs);
        if (!s_job_active) {
            LeaveCriticalSection(&s_icmp_cs);
            continue;
        }
        dst = s_job_dst;
        src = s_job_src;
        id  = s_job_id;
        seq = s_job_seq;
        plen = s_job_plen;
        memcpy(payload, s_job_payload, (size_t)plen);
        s_job_active = 0;
        LeaveCriticalSection(&s_icmp_cs);

        /* Blocks synchronously for up to PROXY_ICMP_TO_MS -- stalls only this thread, not the data plane */
        nr = IcmpSendEcho2Ex(g_proxy.icmp_h, NULL, NULL, NULL,
                             g_proxy.host_ip, dst,
                             payload, (WORD)plen,
                             NULL, rbuf, sizeof(rbuf), PROXY_ICMP_TO_MS);
        if (nr == 0) {
            LOGW(PROXY_TAG, "ping %s: host probe timeout (%ums)",
                 proxy_ip_str(dst), PROXY_ICMP_TO_MS);
        } else if (((ICMP_ECHO_REPLY *)rbuf)->Status != IP_SUCCESS) {
            LOGW(PROXY_TAG, "ping %s: host probe status=%lu",
                 proxy_ip_str(dst), ((ICMP_ECHO_REPLY *)rbuf)->Status);
        } else {
            LOGI(PROXY_TAG, "ping %s: host probe OK rtt=%lums",
                 proxy_ip_str(dst), ((ICMP_ECHO_REPLY *)rbuf)->RoundTripTime);
            ok = 1;
        }

        /* Publish the result; the host thread polls and consumes it (overwriting
         * an old result is harmless -- ping is a low-frequency diagnostic) */
        EnterCriticalSection(&s_icmp_cs);
        s_res_ok   = ok;
        s_res_dst  = dst;
        s_res_src  = src;
        s_res_id   = id;
        s_res_seq  = seq;
        s_res_plen = plen;
        memcpy(s_res_payload, payload, (size_t)plen);
        s_res_valid = 1;
        LeaveCriticalSection(&s_icmp_cs);
    }
    return 0;
}

/* Called by the host thread after each select round: take the probe result,
 * build an echo reply and inject it into the downlink ring */
void proxy_icmp_poll(void)
{
    uint8_t pkt[PROXY_MAX_IP_LEN];
    uint32_t dst, src;
    uint16_t id, seq;
    uint8_t  payload[PROXY_ICMP_MAX_DATA];
    int      plen, ok, rlen;

    if (s_icmp_thread == NULL)
        return;

    EnterCriticalSection(&s_icmp_cs);
    if (!s_res_valid) {
        LeaveCriticalSection(&s_icmp_cs);
        return;
    }
    ok   = s_res_ok;
    dst  = s_res_dst;
    src  = s_res_src;
    id   = s_res_id;
    seq  = s_res_seq;
    plen = s_res_plen;
    memcpy(payload, s_res_payload, (size_t)plen);
    s_res_valid = 0;
    LeaveCriticalSection(&s_icmp_cs);

    if (!ok)
        return;

    rlen = 8 + plen;
    pkt[20] = 0;
    pkt[21] = 0;
    wr16n(pkt + 22, 0);
    wr16n(pkt + 24, id);
    wr16n(pkt + 26, seq);
    memcpy(pkt + 28, payload, (size_t)plen);
    proxy_fill_ip_hdr(pkt, dst, src, 1, (uint16_t)rlen);
    wr16n(pkt + 22, proxy_cksum(pkt + 20, rlen, 0));

    proxy_enqueue_down(pkt, 20 + rlen);
    g_proxy.icmp_replies++;
}

/* ------------------------------------------------------------------ */
/* Public interface                                                      */
/* ------------------------------------------------------------------ */

void proxy_icmp_init(void)
{
    if (g_proxy.icmp_h == NULL)
        return;   /* IcmpCreateFile failed (firewall/permissions), QPING unavailable */

    InitializeCriticalSection(&s_icmp_cs);
    s_icmp_wake = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (s_icmp_wake == NULL) {
        LOGE(PROXY_TAG, "ICMP wake event create failed (err=%lu)", GetLastError());
        return;
    }
    s_icmp_thread = CreateThread(NULL, 0, icmp_probe_thread, NULL, 0, NULL);
    if (s_icmp_thread == NULL) {
        LOGE(PROXY_TAG, "CreateThread(icmp_probe) failed (err=%lu)", GetLastError());
        CloseHandle(s_icmp_wake);
        s_icmp_wake = NULL;
    }
}

/* Uplink ICMP echo request: local segment answered directly; public addresses
 * are first confirmed reachable by the probe thread, whose result is
 * re-injected asynchronously by proxy_icmp_poll */
void proxy_uplink_icmp(uint32_t src, uint32_t dst,
                       const uint8_t *l4, int l4len)
{
    uint16_t id, seq;
    const uint8_t *payload;
    int plen;

    if (l4len < 8)
        return;
    if (l4[0] != 8) {
        if (proxy_log_gate(&g_proxy.log_proto_ms))
            LOGI(PROXY_TAG, "ICMP type %u (non-echo) dropped", (unsigned)l4[0]);
        return;
    }

    id  = rd16n(l4 + 4);
    seq = rd16n(l4 + 6);
    payload = l4 + 8;
    plen = l4len - 8;

    if (plen < 0 || plen > PROXY_ICMP_MAX_DATA)
        return;

    /* Local segment (the gateway itself + 10.0.0.0/24 link-local): answer
     * directly, no probing.
     * WARNING: IP constants are stored low-byte-first (in PROXY_IPv4(10,..) the
     * 10 sits in the lowest byte), so checking the first byte requires & 0xFF --
     * the old code (dst & 0xFF000000) checked the highest byte, so this branch
     * never matched and pings to the local segment also went through the 2s host
     * probe */
    if (dst == PROXY_IP_GW || (dst & 0xFF) == 0x0A) {
        uint8_t pkt[PROXY_MAX_IP_LEN];
        int rlen = 8 + plen;

        pkt[20] = 0;
        pkt[21] = 0;
        wr16n(pkt + 22, 0);
        wr16n(pkt + 24, id);
        wr16n(pkt + 26, seq);
        memcpy(pkt + 28, payload, (size_t)plen);
        proxy_fill_ip_hdr(pkt, dst, src, 1, (uint16_t)rlen);
        wr16n(pkt + 22, proxy_cksum(pkt + 20, rlen, 0));

        proxy_enqueue_down(pkt, 20 + rlen);
        g_proxy.icmp_replies++;
        return;
    }

    /* Public Internet: enqueue for the probe thread (single slot -- if the
     * previous probe hasn't finished, this one is dropped; ping is a
     * low-frequency diagnostic command, dropping is acceptable and prevents
     * queue buildup) */
    if (s_icmp_thread == NULL)
        return;

    EnterCriticalSection(&s_icmp_cs);
    if (s_job_active) {
        LeaveCriticalSection(&s_icmp_cs);
        if (proxy_log_gate(&g_proxy.log_proto_ms))
            LOGW(PROXY_TAG, "ping %s: probe busy -- request dropped",
                 proxy_ip_str(dst));
        return;
    }
    s_job_dst  = dst;
    s_job_src  = src;
    s_job_id   = id;
    s_job_seq  = seq;
    s_job_plen = plen;
    memcpy(s_job_payload, payload, (size_t)plen);
    s_job_active = 1;
    LeaveCriticalSection(&s_icmp_cs);

    SetEvent(s_icmp_wake);
}
