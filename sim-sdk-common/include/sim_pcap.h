/*
 * sim_pcap.h -- host-side IP packet capture (writes classic pcap files
 * directly, openable in Wireshark as-is)
 *
 * The real-device capture chain is binary:
 *   packet hook wireshark_forward_format_print (net_debug.c, gated by NV
 *   log_wireshark) -> diag_wireshark_dataAP -> diag_ps_wireshark_log packs
 *   it into a diag record with WIRESHARK_AP=15 -> output on the LOG port ->
 *   the logview tool on the PC exports pcap.
 * The host does not have (and does not need) this binary diag chain:
 * sim-sdk-common/include/xy_log.h redirects diag_wireshark_dataAP to this
 * module's sim_pcap_write(), writing the pcap file directly with no
 * intermediate tool.
 *
 * File format: classic pcap (magic 0xA1B2C3D4, v2.4, LINKTYPE_ETHERNET=1).
 * The WAN netif is pure L3 (no Ethernet framing), so each packet is prefixed
 * with a 14-byte fake Ethernet header (all-zero MACs + ethertype 0x0800/
 * 0x86DD per IP version) and Wireshark parses it as EN10MB.
 * Uplink and downlink are merged into one file (classic pcap has no
 * direction field; the local address 10.0.0.2 distinguishes them).
 *
 * Thread context: uplink runs in the lwip tcpip thread, downlink in the
 * nat_rx task, open/close in the simboot task; an internal osMutex
 * serializes them. Each packet is fflushed right after writing (a crash
 * does not lose what was captured). sim_pcap_write is a no-op when
 * sim_pcap_open() has not been called.
 */

#ifndef SIM_PCAP_H
#define SIM_PCAP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open/create the capture file (truncating old content) and write the pcap
 * global header. Must be called in RTOS task context (creates the mutex
 * internally). Repeated calls close the old file first.
 *
 * @param path  target file path
 * @return      0 = success; -1 = open failed (all subsequent writes are no-ops)
 */
int sim_pcap_open(const char *path);

/**
 * Capture one IP packet. Called from the diag_wireshark_dataAP macro in
 * sim-sdk-common/include/xy_log.h (i.e. net_debug.c's hook path); the
 * direction field is received but unused (single merged file).
 * No-op when capture is not enabled.
 *
 * @param data  start of the IP packet (v4/v6, guaranteed by the hook side)
 * @param len   byte count
 * @param dir   IP_PACKET_DIR_E (0=uplink 1=downlink), currently unused
 */
void sim_pcap_write(const void *data, unsigned len, int dir);

/**
 * Close the capture file (idempotent). Called on the process exit path
 * (atexit) and on re-open.
 */
void sim_pcap_close(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_PCAP_H */
