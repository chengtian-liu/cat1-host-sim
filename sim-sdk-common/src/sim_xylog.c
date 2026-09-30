#include "sim_xylog.h"
#include "sim_log.h"

#include <stdio.h>
#include <stdarg.h>

/* SDK real-device rule: xy_printf's fmt does not support %s/%llu/%lld
 * (compressed logging); the host printf supports everything, so the
 * simulator formats directly with standard printf semantics. */

/* Module names use the full SDK XY_SRC_E enum identifiers (no longer
 * abbreviated), mapping one-to-one to the enums in business code, so
 * log entries can be grepped by enum name directly. */
static const char *src_name(int src)
{
    switch (src) {
    case L2_HP_T:         return "L2_HP_T";
    case L2_LP_T:         return "L2_LP_T";
    case L2_ULGRANT_T:    return "L2_ULGRANT_T";
    case L2_T:            return "L2_T";
    case LTEPS_T:         return "LTEPS_T";
    case LTE_PHY_TRX_T:   return "LTE_PHY_TRX_T";
    case LTE_PHY_SCHED_T: return "LTE_PHY_SCHED_T";
    case LTE_PHY_MEAS_T:  return "LTE_PHY_MEAS_T";
    case LPHY:            return "LPHY";
    case L1C:             return "L1C";
    case WIRESHARK:       return "WIRESHARK";
    case PLATFORM:        return "PLATFORM";
    case ATCTRL:          return "ATCTRL";
    case PCTOOL:          return "PCTOOL";
    case WIRESHARK_AP:    return "WIRESHARK_AP";
    case XYAPP:           return "XYAPP";
    case PLATFORM_AP:     return "PLATFORM_AP";
    case GNSS:            return "GNSS";
    case USER_LOG:        return "USER_LOG";
    case ATC_AP_T:        return "ATC_AP_T";
    case BLE:             return "BLE";
    case USB:             return "USB";
    case AUDIO:           return "AUDIO";
    case ADMIN_T:         return "ADMIN_T";
    case BIP_T:           return "BIP_T";
    case LRRC_T:          return "LRRC_T";
    case UICC_MNG_T:      return "UICC_MNG_T";
    case LNAS_T:          return "LNAS_T";
    case VoLTE:           return "VoLTE";
    case UART:            return "UART";
    default:              return "SYS";
    }
}

static int lev_map(int lev)
{
    switch (lev) {
    case DEBUG_LOG:    return LOG_LVL_DBG;
    case INFO_LOG:     return LOG_LVL_INF;
    case WARN_LOG:     return LOG_LVL_WRN;
    case FATAL_LOG:    /* fallthrough */
    case CRITICAL_LOG: return LOG_LVL_ERR;
    default:           return LOG_LVL_INF;
    }
}

void sim_xy_printf(int src, int lev, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* Reuse sim_log's timestamping / level filtering / thread lock */
    sim_log_print(lev_map(lev), src_name(src), "%s", buf);
}

/* ------------------------------------------------------------------ */
/* lwIP debug hook: LWIP_PLATFORM_DIAG routed into the log ring (for  */
/* troubleshooting; for the switch and wiring see the memo at the     */
/* end of sim-sdk-common/lwipopts.h, Lwip Debug options section)      */
/* ------------------------------------------------------------------ */

void sim_lwip_diag(const char *fmt, ...)
{
    char body[1024];
    va_list ap;
    size_t n;

    va_start(ap, fmt);
    n = (size_t)vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    /* lwIP debug messages carry a trailing newline; strip it to avoid blank log lines */
    while (n > 0 && (body[n - 1] == '\n' || body[n - 1] == '\r'))
        body[--n] = '\0';

    sim_log_print(LOG_LVL_INF, "LWIP", "%s", body);
}
