/*
 * sim_at_shims.c -- symbol stubs required to link the real AT framework
 * (at_ctrl + selected at_cmd) on host
 *
 * Principles:
 *   - Do not modify or copy SDK sources; only supply dependencies "outside
 *     the framework" here (sleep locks, cross-core communication, PS
 *     protocol stack, flash/FTL, power reset and other real-device
 *     subsystems).
 *   - Stub behavior is based on "the host never really sleeps, never
 *     resets, has no CP core", staying as close as possible to the visible
 *     parts of real-device semantics (return values/logs).
 *
 * Virtual PS (Phase C placeholder):
 *   The framework sends non-basic commands via SendAt2AtcAp -> xy_atc_data_req
 *   to the "protocol stack"; here a minimal virtual PS replies:
 *   bare "AT" -> OK, everything else -> ERROR.
 *   Phase D will replace this with the real atc_ps_cmd + a virtual 3GPP
 *   state machine.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#include "cmsis_os2.h"
#include "xy_system.h"
#include "xy_lpm.h"
#include "xy_sleep_lock.h"
#include "icc_msg.h"
#include "factory_nv.h"
#include "oss_nv.h"
#include "xy_ftl.h"
#include "at_ps_proxy.h"
#include "xy_at_api.h"
#include "at_com.h"            /* at_cmd_t */

#include "sim_log.h"
#include "sim_vps_net.h"

/* ================================================================== */
/* Shadow registers (host versions of hw_types.h HWREG etc. all land   */
/* here, avoiding dereferencing real-device physical register          */
/* addresses and segfaulting)                                          */
/* ================================================================== */

uint32_t sim_hwreg_scratch32;
uint16_t sim_hwreg_scratch16;
uint8_t  sim_hwreg_scratch8;
uint64_t sim_hwreg_scratch64;

/* ================================================================== */
/* Sleep locks (real device: xy_sleep_lock.c, depends on prcm/rtc/lpm  */
/* hardware)                                                           */
/* Host never really sleeps: all locks spin idle                       */
/* ================================================================== */

int8_t create_sleep_lock(char *lock_name)
{
    static int8_t s_next_fd = 0;
    int8_t fd = s_next_fd;

    if (s_next_fd < 31)
        s_next_fd++;

    LOGD("SIMSHIM", "create_sleep_lock(%s) -> fd=%d", lock_name ? lock_name : "?", fd);
    return fd;
}

void sleep_lock(int8_t lockfd, Lock_Type_E sleep_type)
{
    (void)lockfd;
    (void)sleep_type;
}

void sleep_unlock(int8_t lockfd, Lock_Type_E sleep_type)
{
    (void)lockfd;
    (void)sleep_type;
}

void delete_sleep_lock(int8_t lockfd)
{
    (void)lockfd;
}

int8_t get_lock_stat(int8_t lockfd, Lock_Type_E sleep_type)
{
    (void)lockfd;
    (void)sleep_type;
    return 0;   /* unlocked */
}

uint8_t is_sleep_locked(Lock_Type_E sleep_type)
{
    (void)sleep_type;
    return 0;
}

void sys_lock(Lock_Type_E type)
{
    (void)type;
}

void sys_unlock(Lock_Type_E type)
{
    (void)type;
}

/* Anti-sleep delay lock during AT serial TX/RX: host returns success directly */
int at_delaylock_act(void)
{
    return XY_OK;
}

/* ================================================================== */
/* Boot reason (real device: power management registers)               */
/* ================================================================== */

uint32_t Get_Boot_Reason(void)
{
    return POWER_ON;
}

/* ================================================================== */
/* Cross-core communication (real device: inter_core channel; host has */
/* no CP core, all spin idle)                                          */
/* ================================================================== */

void icc_comm_channel_register(icc_comm_msg_t msg_id, icc_cb_t callback)
{
    (void)msg_id;
    (void)callback;
}

void icc_at_channel_register(icc_at_msg_t msg_id, icc_cb_t callback)
{
    (void)msg_id;
    (void)callback;
}

size_t icc_at_channel_write(icc_at_msg_t msg_id, void *data, size_t size)
{
    (void)msg_id;
    (void)data;
    return size;
}

/* ================================================================== */
/* AT subsystem peripheral init stubs                                  */
/* ================================================================== */

/* RF driver AT registration (real device: at_RF_cmd.c, involves RF HAL)
 * -- the simulator has no RF */
void rfdrv_at_init(void)
{
    LOGI("SIMSHIM", "rfdrv_at_init stubbed (no RF on host)");
}

/* PS-side URC callback registration (real device: at_ps_urc.c, involves
 * protocol stack events) -- deferred to Phase D */
void ps_urc_register_callback_init(void)
{
    LOGI("SIMSHIM", "ps_urc_register_callback_init stubbed (virtual PS minimal)");
}

/* ================================================================== */
/* USB capability query (real device: usb_api.c, reads enumeration     */
/* status)                                                             */
/* ================================================================== */

extern softap_fac_nv_t *g_softap_fac_nv;

/* usb_mode bitmap: bit0-1 network / bit2 AT / bit3 LOG / bit4 MODEM */
bool isUsbAtSupported(void)
{
    return (g_softap_fac_nv->usb_mode & (1 << 2)) ? true : false;
}

/* MODEM(PPP) port: the simulator serves it with the /dev/modem virtual
 * serial port (host channel 2). Same pattern as isUsbAtSupported,
 * decided by factory NV usb_mode bit4. Default usb_mode=31 -> bit4 set
 * -> supported; at_ctl registers AT_USB_MODEM_FD and opens /dev/modem.
 * PPP dial (ATD*99#) rides on that tty via ppp_server. */
bool isUsbModemAtSupported(void)
{
    return (g_softap_fac_nv->usb_mode & (1 << 4)) ? true : false;
}

/* ================================================================== */
/* URC diversion (real device: protocol stack SMS event table)         */
/* ================================================================== */

int Check_SMS_IND_By_Event(unsigned short usEvent)
{
    (void)usEvent;
    return 0;   /* no SMS events, take the ordinary sysAtURC path */
}

/* ================================================================== */
/* cache / alignment (host memory consistency guaranteed by the        */
/* compiler, all spin idle)                                            */
/* ================================================================== */

void csi_dcache_clean_range(void *addr, uint32_t size)
{
    (void)addr;
    (void)size;
}

void csi_dcache_invalid_range(void *addr, uint32_t size)
{
    (void)addr;
    (void)size;
}

/* core timer count (declaration in sim_missing_decls.h, injected via
 * -include): real device is about 32kHz; here we use kernel tick (1ms)
 * x32 as an equivalent conversion, keeping the "+32 = 1ms" busy-wait
 * timeout semantics of at_passthrough.c correct. */
uint64_t csi_coret_get_value2(void)
{
    return (uint64_t)osKernelGetTickCount() * 32U;
}

/* ================================================================== */
/* Virtual PS: ATC data entry (real device: atc_ps_main.c)             */
/*                                                                     */
/* Phase C placeholder implementation:                                 */
/*   - bare "AT" (including "AT" + terminator) -> "\r\nOK\r\n"         */
/*   - identification commands (ATI / AT+CGMI / AT+CGMM / AT+CGMR /    */
/*     AT+CGSN) -> answered in real-device atc_ps_cmd format (on the   */
/*     real device these live on the protocol stack side, e.g.         */
/*     ATC_ATI_LTE_Command; once Phase D brings in the real            */
/*     atc_ps_cmd this whole section retires)                          */
/*   - everything else -> "\r\nERROR\r\n"                              */
/* Responses go through SendAtInd2User(ttyFd) over the real nearps     */
/* loop, finally output by at_ctl on the matching virtual serial port  */
/* -- the chain is exactly the same as on the real device, only the    */
/* "protocol stack" got simpler.                                       */
/* ================================================================== */

/* Real-device version strings (platform/include/version.h), referenced
 * directly by identification responses to guarantee consistency with
 * the real device; after Phase D the real atc_ps_cmd outputs them */
#define SIM_VPS_MANUFAC   "XINYI"
#define SIM_VPS_MODULE    "XY4101PC"
#define SIM_VPS_FWVER     "V4101B00003R00C0023"
#define SIM_VPS_IMEI      "860000000000001"  /* simulated placeholder IMEI */

typedef struct {
    const char *cmd;    /* uppercase form (without trailing ?) */
    const char *rsp;    /* info body, real-device format: \r\n<value>\r\n */
} vps_id_cmd_t;

static const vps_id_cmd_t s_vps_id_cmds[] = {
    { "AT+CGMI", "\r\n" SIM_VPS_MANUFAC "\r\n" },
    { "AT+CGMM", "\r\n" SIM_VPS_MODULE  "\r\n" },
    { "AT+CGMR", "\r\n" SIM_VPS_FWVER   "\r\n" },
    { "AT+CGSN", "\r\n" SIM_VPS_IMEI    "\r\n" },
};

/* Uppercase copy (used only for command matching, original data untouched) */
static void vps_upper_copy(char *dst, const unsigned char *src, int len, int dst_size)
{
    int i;
    int n = (len < dst_size - 1) ? len : (dst_size - 1);

    for (i = 0; i < n; i++) {
        char c = (char)src[i];
        dst[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    dst[n] = '\0';

    /* Strip trailing terminators (\r \n) */
    while (n > 0 && (dst[n - 1] == '\r' || dst[n - 1] == '\n'))
        dst[--n] = '\0';
}

void xy_atc_data_req(unsigned short usDataLen, unsigned char *pucData, int ittyFd)
{
    char rsp[160];
    char cmd[64];
    const char *info = NULL;
    int bare_at = 0;
    int is_ati = 0;
    unsigned int i;

    if (pucData != NULL && usDataLen >= 2 &&
        (pucData[0] == 'A' || pucData[0] == 'a') &&
        (pucData[1] == 'T' || pucData[1] == 't')) {

        vps_upper_copy(cmd, pucData, usDataLen, sizeof(cmd));

        /* Bare AT: only terminators left after AT */
        bare_at = (cmd[2] == '\0');

        /* ATI (real device: ATC_ATI_LTE_Command, format aligned line by
         * line with real-device output) */
        is_ati = (strcmp(cmd, "ATI") == 0);

        /* Network-class commands: intercepted by the fake CP control plane
         * (Phase 3, sim_vps_net.c) */
        if (!bare_at && !is_ati && sim_vps_net_handle(cmd, ittyFd)) {
            LOGI("VPS", "cmd='%.*s' fd=%d -> VPSNET",
                 (int)usDataLen, pucData ? (const char *)pucData : "", ittyFd);
            return;
        }

        /* Identification query/execute (queries look like AT+CGMI?; the
         * real device returns the same value too) */
        if (!bare_at && !is_ati) {
            size_t clen = strlen(cmd);
            if (clen > 0 && cmd[clen - 1] == '?')
                cmd[clen - 1] = '\0';
            for (i = 0; i < sizeof(s_vps_id_cmds) / sizeof(s_vps_id_cmds[0]); i++) {
                if (strcmp(cmd, s_vps_id_cmds[i].cmd) == 0) {
                    info = s_vps_id_cmds[i].rsp;
                    break;
                }
            }
        }
    }

    if (bare_at)
        snprintf(rsp, sizeof(rsp), "\r\nOK\r\n");
    else if (is_ati)
        snprintf(rsp, sizeof(rsp),
                 "\r\n%s\r\n%s\r\n\r\nRevision:%s\r\n\r\nOK\r\n",
                 SIM_VPS_MANUFAC, SIM_VPS_MODULE, SIM_VPS_FWVER);
    else if (info != NULL)
        snprintf(rsp, sizeof(rsp), "%s\r\nOK\r\n", info);
    else
        snprintf(rsp, sizeof(rsp), "\r\nERROR\r\n");

    LOGI("VPS", "cmd='%.*s' fd=%d -> %s",
         (int)usDataLen, pucData ? (const char *)pucData : "", ittyFd,
         (bare_at || is_ati || info != NULL) ? "OK" : "ERROR");

    SendAtInd2User(rsp, (unsigned int)strlen(rsp), ittyFd, 0);
}

/* CP->AP zero-copy message (real device in the precompiled protocol
 * stack library; host has no CP core) */
void PsRecvMsg_Cp2Ap(void *pMsg, unsigned int ulMsgLen)
{
    (void)pMsg;
    (void)ulMsgLen;
}

/* ================================================================== */
/* FTL (real device: flash wear leveling; host simulates with RAM      */
/* slots)                                                              */
/*                                                                     */
/* Addresses are absolute flash addresses from memlayout; here they    */
/* are used only as keys.                                              */
/* ================================================================== */

#define SIM_FTL_SLOTS   32

typedef struct {
    uint32_t addr;
    uint32_t size;
    uint8_t *data;
} sim_ftl_slot_t;

static sim_ftl_slot_t s_ftl[SIM_FTL_SLOTS];

static sim_ftl_slot_t *ftl_find(uint32_t addr)
{
    int i;

    for (i = 0; i < SIM_FTL_SLOTS; i++) {
        if (s_ftl[i].data != NULL && s_ftl[i].addr == addr)
            return &s_ftl[i];
    }
    return NULL;
}

bool xy_ftl_write(uint32_t addr, uint8_t *data, uint32_t size)
{
    sim_ftl_slot_t *slot;

    taskENTER_CRITICAL();
    slot = ftl_find(addr);
    if (slot == NULL) {
        int i;
        for (i = 0; i < SIM_FTL_SLOTS; i++) {
            if (s_ftl[i].data == NULL) {
                slot = &s_ftl[i];
                break;
            }
        }
        if (slot == NULL) {
            taskEXIT_CRITICAL();
            LOGE("SIMSHIM", "xy_ftl_write: no free slot for 0x%08x", (unsigned)addr);
            return false;
        }
        slot->addr = addr;
        slot->size = 0;
        slot->data = NULL;
    }

    if (slot->size != size) {
        free(slot->data);
        slot->data = (uint8_t *)malloc(size);
        slot->size = size;
    }
    if (slot->data == NULL) {
        slot->size = 0;
        taskEXIT_CRITICAL();
        return false;
    }
    memcpy(slot->data, data, size);
    taskEXIT_CRITICAL();

    LOGD("SIMSHIM", "xy_ftl_write addr=0x%08x size=%u", (unsigned)addr, (unsigned)size);
    return true;
}

bool xy_ftl_read(uint32_t addr, uint8_t *data, uint32_t size)
{
    sim_ftl_slot_t *slot;
    bool ok = false;

    taskENTER_CRITICAL();
    slot = ftl_find(addr);
    if (slot != NULL && slot->size >= size) {
        memcpy(data, slot->data, size);
        ok = true;
    }
    taskEXIT_CRITICAL();

    LOGD("SIMSHIM", "xy_ftl_read addr=0x%08x size=%u -> %s",
         (unsigned)addr, (unsigned)size, ok ? "hit" : "miss");
    return ok;
}

void xy_ftl_erase(uint32_t addr)
{
    sim_ftl_slot_t *slot;

    taskENTER_CRITICAL();
    slot = ftl_find(addr);
    if (slot != NULL) {
        free(slot->data);
        slot->data = NULL;
        slot->size = 0;
    }
    taskEXIT_CRITICAL();

    LOGD("SIMSHIM", "xy_ftl_erase addr=0x%08x", (unsigned)addr);
}

/* ================================================================== */
/* USB state callbacks (real device: usb_api.c + USBX; host has no     */
/* USB enumeration)                                                    */
/* Declarations in sim-sdk-common/include_host/usb_api.h               */
/* ================================================================== */

typedef void (*usb_state_change_func_t)(int usb_state, void *data);
typedef int usb_state_t_shim;   /* only needed for parameter width, avoids re-including the enum */

void UsbRegistStateCallback(int usb_state, usb_state_change_func_t change_func)
{
    (void)usb_state;
    (void)change_func;
}

void UsbStateCallbackHandle(int usb_state, void *data)
{
    (void)usb_state;
    (void)data;
}

bool UsbUnregistStateCallback(int usb_state, usb_state_change_func_t change_func)
{
    (void)usb_state;
    (void)change_func;
    return true;
}

/* ================================================================== */
/* libc gap-fill: strnstr (newlib has it, MinGW does not; used by      */
/* at_utils.c)                                                         */
/* ================================================================== */

char *strnstr(const char *haystack, const char *needle, size_t len)
{
    size_t nlen = strlen(needle);
    size_t i;

    if (nlen == 0)
        return (char *)haystack;

    for (i = 0; i + nlen <= len; i++) {
        if (haystack[i] == needle[0] &&
            memcmp(haystack + i, needle, nlen) == 0)
            return (char *)(haystack + i);
    }
    return NULL;
}

/* ================================================================== */
/* UART driver (real device: driverlib xy4101_ll_uart.c)               */
/* send_debug_str_to_ext prints debug strings to the main LPUART;      */
/* host redirects to front-end logs.                                   */
/* Signature uses void* instead of LPUA_UART_TypeDef* to avoid         */
/* pulling in chip register headers                                    */
/* ================================================================== */

void LL_UART_StringPut(void *UARTx, int8_t *str)
{
    (void)UARTx;
    if (str != NULL)
        LOGI("DBGUART", "%s", (const char *)str);
}

void LL_UART_WaitTxDone(void *UARTx)
{
    (void)UARTx;
}

/* ================================================================== */
/* Boot/calibration/low-power state queries (real device: OTP/PMU      */
/* registers)                                                          */
/* ================================================================== */

uint8_t GetFtCalibrationFlag(void)
{
    return 0;   /* host has no FT calibration data */
}

bool Is_WakeUp_From_Dsleep(void)
{
    return false;
}

bool Is_WakeUp_From_Sleep(void)
{
    return false;
}

uint64_t Get_Boot_Sub_Reason(void)
{
    return 0;
}

uint8_t get_corner_info(void)
{
    return 0;
}

/* ================================================================== */
/* Low-power hook variables (real device: defined in xy_sys_hook.c /   */
/* deepsleep.c)                                                        */
/* at_response.c calls back when non-NULL; host keeps NULL             */
/* ================================================================== */

void (*p_SysUp_URC_Hook)(void) = NULL;              /* Sys_Func_Cb */
void (*p_SleepInd_Cb)(int status, int source) = NULL; /* pm_sleepind_cb */
void *gCpLpmDebugInfo = NULL;   /* real-device type LPM_TIMER_INFO_RECOVERY_T* */

/* ================================================================== */
/* Version info (real device: xy_version.c, strings injected by the    */
/* build system)                                                       */
/* ================================================================== */

char *GetSdkVersion(void)
{
    return (char *)"XY4101_SDK_SIM";
}

char *GetSoCVersion(void)
{
    return (char *)"XY4101-HOST";
}

char *GetHardwareVer(void)
{
    return (char *)"SIM_HW_V1.0";
}

/* ================================================================== */
/* CME error reporting mode (real device in atc_ps_cmd; before Phase D */
/* use the value from NV)                                              */
/* ================================================================== */

unsigned char api_GetCmeeValue(void)
{
    return (unsigned char)g_softap_fac_nv->cmee_mode;
}

/* ================================================================== */
/* hexstr2bytes (real device: kernel/misc xy_utils.c) -- self-         */
/* contained, implemented directly                                     */
/* "1A34" -> {0x1A,0x34}, returns byte count; returns -1 on error      */
/* ================================================================== */

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int hexstr2bytes(char *src, int src_len, char *dst, int dst_size)
{
    int i, out = 0;

    if (src == NULL || dst == NULL)
        return -1;

    for (i = 0; i + 1 < src_len; i += 2) {
        int hi = hex_nibble(src[i]);
        int lo = hex_nibble(src[i + 1]);

        if (hi < 0 || lo < 0)
            return -1;
        if (out >= dst_size)
            return -1;
        dst[out++] = (char)((hi << 4) | lo);
    }
    return out;
}

/* ================================================================== */
/* Peripheral subsystem stubs (real device in at_cmd / net_adapt /     */
/* lowpower / protocol stack)                                          */
/* ================================================================== */

/* CP-side PHY command confirm callback (real device: at_phy_cmd.c)
 * -- host has no CP, spins idle */
void phy_icc_comm_at_cnf_callback(void *data, size_t size)
{
    (void)data;
    (void)size;
}

/* Protocol stack abort of long commands (e.g. +COPS=? network scan)
 * -- the virtual PS has no long commands, return false */
bool xy_atc_PsCancelAtCmdProc(unsigned short usDataLen, unsigned char *pucData, int ittyFd)
{
    (void)usDataLen;
    (void)pucData;
    (void)ittyFd;
    return false;
}

/* app_delay_lock/app_delay_unlock stubs retired (2026-09-03):
 * the real app_utils.c is compiled in (Phase 2 MQTT depends on its
 * hmac_sha256) */

/* Low-power debug counters (real device: lowpower/src/xy_lpm.c, not
 * compiled in). Referenced by GetWorkDebugInfo in app_utils.c; the
 * host never sleeps, so constant 0 is fine */
uint32_t gWakupNumDebug = 0;
uint32_t gSleepNumDebug = 0;

/* atUartBaudRateGet is provided by the SDK's at_com.c; not redefined
 * here. (Old versions needed a stub because the old SDK did not
 * compile in at_com.c; the new SDK compiles that file normally) */

/* Low-power LPUART AT output stub.
 * Called from the else branches of at_ctl_basic.c / at_response.c
 * (LPUART path). xysim has no LPUART sub-core, an empty stub suffices. */
void lpuart_at_output_lowpower(char *buf)
{
    (void)buf;
}

/* AT custom command table pointer stub.
 * Referenced by at_ctl.c (extern at_cmd_t *g_at_custom_req).
 * xysim has no custom AT command extensions, NULL is fine. */
at_cmd_t *g_at_custom_req = NULL;

/* Hardware flow control status: the get_ifc_val stub formerly here was
 * deleted (2026-09-02) -- it duplicated the real definition at
 * at_com.c:983 and broke linking. The real one reads g_at_config,
 * accepts only AT_LPUART_FD/AT_UART_FD and returns a parameter error
 * for other handles, consistent with real-device behavior; the host
 * uses it directly (same precedent as deleting the set_at_tcpip_err
 * stub). */

/* ================================================================== */
/* System action stubs (real device resets/deletes files/switches      */
/* CFUN; host only logs)                                               */
/* ================================================================== */

int xy_cfun_excute(int status)
{
    LOGI("SIMSHIM", "xy_cfun_excute(%d) - no-op on host", status);
    return XY_OK;
}

void erase_nv_flash(void)
{
    LOGW("SIMSHIM", "erase_nv_flash() - no-op on host");
}

/* xy_fremove stub retired (2026-09-03): the real xy_fs_api.c is compiled in (Phase 2 FS) */

void xy_Soft_Reset(Soft_Reset_Type soft_rst_reason)
{
    LOGW("SIMSHIM", "xy_Soft_Reset(%d) requested - host keeps running", (int)soft_rst_reason);
}

void xy_Soft_Reset_safe(int erase_NV)
{
    LOGW("SIMSHIM", "xy_Soft_Reset_safe(erase_NV=%d) requested - host keeps running", erase_NV);
}

void ux_device_cdc_acm_modem_recv_free(void *buffer)
{
    (void)buffer;
}
