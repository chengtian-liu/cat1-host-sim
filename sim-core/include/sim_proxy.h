#ifndef SIM_PROXY_H
#define SIM_PROXY_H

/*
 * sim_proxy.h -- in-process user-mode proxy data plane (replaces Wintun + ICS, stage N1)
 *
 * The simulator itself plays "core network + egress router": the internal
 * subnet is fixed at 10.0.0.0/24, "modem" = 10.0.0.2 (statically assigned by
 * the PS simulation), proxy box (this module) = 10.0.0.1. IP packets never
 * leave the process and never enter the host routing table; no administrator
 * rights are needed. Implementation in src/ of the same directory.
 */

#include <stdint.h>

/* Proxy internal-network constants (used by the fake CP to answer CGCONTRDP etc.) */
#define SIM_PROXY_GW_STR      "10.0.0.1"    /* gateway (proxy box) */
#define SIM_PROXY_CLIENT_STR  "10.0.0.2"    /* "modem" address */
#define SIM_PROXY_MASK_STR    "255.255.255.0"

/**
 * Start the user-mode proxy (idempotent): WSAStartup, probe host DNS, start
 * host send/receive threads (plain Windows threads handling select /
 * send-receive / NAT, no RTOS involvement).
 * The downlink injection task (proxy_rx) is not created here: RTOS tasks must
 * be created by the target layer, see sim_core_proxy_rx_loop().
 * @return 0 success, -1 failure (does not block PDP activation; just no data plane)
 */
int sim_proxy_start(void);

/** Whether the proxy data plane is up */
int sim_proxy_is_up(void);

/**
 * Uplink entry point (called by API_Send_Data_2_PS): copies the IP packet
 * into the uplink ring and wakes the host thread.
 * The caller keeps ownership of pData (must net_free it itself after return).
 * @return 0 = accepted (including silent drops), -1 = pump not ready (caller logs a warning)
 */
int sim_proxy_uplink(const void *ip_pkt, uint32_t len);

/**
 * DNS probed on the host (network byte order, same form as the return value
 * of inet_addr).
 * @param idx 0/1/2
 * @return the corresponding DNS; returns 0 if out of range or absent. Always 0 before sim_proxy_start.
 */
uint32_t sim_proxy_dns_ip4(int idx);

/** Number of DNS servers probed (0 = not started) */
int sim_proxy_dns_count(void);

/**
 * Downlink injection loop (called by the target layer inside its FreeRTOS task).
 * Internal infinite loop, never returns -- sim_proxy_start() must have been
 * called beforehand. The target layer is responsible for creating the RTOS
 * task and binding the callback table (sim_core_set_callbacks), then using
 * this function as the task body.
 */
void sim_core_proxy_rx_loop(void);

#endif /* SIM_PROXY_H */
