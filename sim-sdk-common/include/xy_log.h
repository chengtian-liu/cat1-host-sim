/*
 * xy_log.h -- host shim (replaces platform/kernel/diag_ctrl/api/xy_log.h)
 *
 * Real-device logging goes through the diag compressed chain (6 diag_*.h
 * headers + binary output on the LOG port). On the host everything is
 * redirected to sim_xylog.h:
 *   xy_printf / xy_prints / user_printf / PlatPrintf / PrintLog / PrintUserLog
 *     -> sim_xy_printf() -> vsnprintf -> plain text on the console
 *
 * The XY_LOG_LEV / XY_SRC_E enums in sim_xylog.h are aligned item by item
 * with diag_item_types.h, so SDK sources need no changes at all.
 */

#ifndef __XY_LOG_SHIM_H__
#define __XY_LOG_SHIM_H__

#include "sim_xylog.h"
#include "sim_pcap.h"

/* wireshark diag output (real device: diag_ps_wireshark_log packs it into a
 * WIRESHARK_AP=15 diag binary record sent over the LOG port, exported to
 * pcap by the logview tool on the PC).
 * On the host this is redirected to sim_pcap_write: writes a classic pcap
 * file directly, readable by Wireshark.
 * Only call site: net_debug.c wireshark_forward_format_print (the NV
 * log_wireshark gate is unchanged; the host defaults it to 1 = capture all).
 * sim_pcap_write is a no-op unless capture was enabled with --pcap. */
#define diag_wireshark_dataAP(data, len, type) \
    sim_pcap_write((data), (unsigned)(len), (int)(type))

#endif /* __XY_LOG_SHIM_H__ */
