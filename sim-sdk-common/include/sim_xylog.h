#ifndef SIM_XYLOG_H
#define SIM_XYLOG_H

/*
 * Host shim for the XY SDK logging macros.
 *
 * Real-device path:
 *   xy_printf -> PlatPrintf -> diag_platform_fixed_arguments_log()
 *   uses diag compressed logging: fmt is compressed into an ID by the build
 *   script, binary packets go out on the LOG port, and decoding requires the
 *   Logview tool with a loginfo file; %s/%llu/%lld are unsupported.
 *
 * Simulation path:
 *   the same macros are redirected here to sim_xy_printf(), which does a
 *   plain vsnprintf on the host and prints to the front-end console (the
 *   xysim.exe window) with timestamp/level/module name, human-readable.
 *
 * Enum values are identical to the SDK's
 * platform/kernel/diag_ctrl/core/format/diag_item_types.h, so when real
 * business sources are compiled into the simulator later, only the include
 * path needs to point at this shim.
 */

/* ---- identical to the SDK's XY_LOG_LEV ---- */
typedef enum {
    DEBUG_LOG,
    INFO_LOG,
    WARN_LOG,
    FATAL_LOG,
    CRITICAL_LOG,
    MAX_LEV_LOG
} XY_LOG_LEV;

/* ---- identical to the SDK's XY_SRC_E (parts not involved in simulation may be omitted) ---- */
typedef enum {
    XYLOG_BASE      = 0,
    L2_HP_T         = 1,
    L2_LP_T         = 2,
    L2_ULGRANT_T    = 3,
    L2_T            = 4,
    LTEPS_T         = 5,
    LTE_PHY_TRX_T   = 6,
    LTE_PHY_SCHED_T = 7,
    LTE_PHY_MEAS_T  = 8,
    LPHY            = 9,
    L1C             = 10,
    WIRESHARK       = 11,
    PLATFORM        = 12,
    ATCTRL          = 13,
    PCTOOL          = 14,
    WIRESHARK_AP    = 15,
    XYAPP           = 16,   /* application business module */
    PLATFORM_AP     = 17,
    GNSS            = 18,
    USER_LOG        = 19,   /* user log (user_printf) */
    ATC_AP_T        = 20,
    BLE             = 21,
    USB             = 22,
    AUDIO           = 23,
    ADMIN_T         = 24,
    BIP_T           = 25,
    LRRC_T          = 26,
    UICC_MNG_T      = 27,
    LNAS_T          = 28,
    VoLTE           = 29,
    UART            = 30,
    XYLOG_MAX_BLOCK
} XY_SRC_E;

/* Host implementation: format and print directly to the front-end console */
void sim_xy_printf(int src, int lev, const char *fmt, ...);

/* lwIP LWIP_PLATFORM_DIAG hook (wiring notes for debugging are at the end of
 * sim-sdk-common/lwipopts.h, Lwip Debug options section; normally disabled,
 * declaration always available) */
void sim_lwip_diag(const char *fmt, ...);

/* ---- macros with the same names as in the SDK's xy_log.h (real-device
 * definitions live in diag_ctrl/api/xy_log.h) ----
 * Note: xy_printf / xy_prints / user_printf are defined in xy_system.h on the
 * real device and are deliberately not redefined here to avoid conflicting
 * with the SDK headers (redefinition warnings).
 * Host-side code that does not include xy_system.h may define them itself or
 * call sim_xy_printf directly. */
#define PlatPrintf(src_id, lev, fmt, ...)      sim_xy_printf((src_id), (lev), fmt, ##__VA_ARGS__)
#define PrintLog(src_id, lev, fmt, ...)        sim_xy_printf((src_id), (lev), fmt, ##__VA_ARGS__)
#define PrintLogSt(src_id, lev, fmt, ...)      sim_xy_printf((src_id), (lev), fmt, ##__VA_ARGS__)
#define PrintUserLog(src_id, lev, fmt, ...)    sim_xy_printf((src_id), (lev), fmt, ##__VA_ARGS__)

#endif /* SIM_XYLOG_H */
