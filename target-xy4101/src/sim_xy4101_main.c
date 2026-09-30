/*
 * sim_xy4101_main.c -- XY4101 target entry: boot task + bridge task + banner text
 *
 * The generic startup skeleton sim_main() (CLI parsing, crash diagnostics,
 * logging, three-channel binding, kernel start) lives in
 * sim-sdk-common/src/sim_main.c and is identical for all chip products in the
 * same family; this file only provides XY4101's g_sim_target hook table and a
 * one-line tail-call in main().
 *
 *   boot_task = sim_boot_task, initializes in real-device app_start order:
 *     posix_device_init()   // register virtual devices
 *     fs_proxy_init()       // fs proxy thread (without the proxy, file ops fake success)
 *     at_init()             // real AT framework: at_ctl + atproxy + atrcv tasks
 *     appAtRespInit()       // AT response task
 *     main_proxy_init()     // PS->AP message routing hub
 *     net_init()            // lwip tcpip thread + WAN netif glue (fake-CP foundation)
 *     atSockInit()          // socket/SSL/FTP sockMgr registration (real device uses .appRegTable)
 *     ssl_config_init()     // SSL config mutex (same)
 *     pppDialInit()         // PPP mutex (same)
 *     sim_bridge_task       // 1ms poll: host ring buffer -> virtual devices -> select wakeup
 *     xy4101_bridge_init    // bind sim-core callback table to XY4101 SDK
 *     auto-dial             // auto-activate PDP cid=1 at boot (=AT+CGACT=1)
 *
 * The data plane is an in-process user-mode proxy (sim_proxy): UDP/ICMP/DNS/TCP
 * all go through host user-mode interfaces; no admin rights, no UAC, no
 * ICS/Wintun required.
 */

#include <string.h>

#include "xy_system.h"          /* cmsis_os2 + xy_printf + Sys_Assert */
#include "posix_device.h"
#include "factory_nv.h"

#include "sim_tty_device.h"
#include "sim_log.h"
#include "sim_vps_net.h"
#include "sim_pcap.h"
#include "sim_main.h"
#include "sim_xy4101_bridge.h"

/* Real-device entry points (original SDK, unmodified):
 *   at_init        -- registered into .appRegTable by at_ctl_basic.c; called explicitly here
 *   appAtRespInit  -- registered into .appRegTable by at_response.c; called explicitly here
 *   net_init       -- net_adapt.c: tcpip_init + wan_init etc. On the real device
 *                     application_init schedules it automatically
 *                     (APP_STARTUP_PRIORITY_MIDDLE); the host has no appstart
 *                     scheduler, so it is called explicitly here
 *
 * .appRegTable section auto-registration does not work under the host link
 * model: application_init puts registration entries into the .appRegTable
 * section, and on the real device app_start() walks linker-script symbols and
 * calls each one; but host SDK code is delivered as static libraries, and if a
 * .obj has no referenced symbols the linker never pulls it out of the library
 * -- the registration entry is lost together with its init function. Such
 * inits must be called explicitly here (members of the .appRegTable section
 * can be checked in the map file):
 *   atSockInit     -- at_socket_utils.c (LOW). Registers the handleFunc +
 *                     AT response callbacks of each sockMgr source: plain
 *                     socket(1)/SSL socket(2)/FTP(4). Without it, async
 *                     requests from QIOPEN/QSSLOPEN/QFTPCFG fall through in
 *                     sockMgrProcessRequest ("handleFunc not define"): the
 *                     command gets no response and the AT channel stays busy
 *                     forever (the 2026-09-04 FTP hang was exactly this)
 *   ssl_config_init -- at_ssl_config.c (LOW). Only creates the SSL config
 *                     mutex, which every SSL config path (QSSLCFG etc.) needs
 *   fs_proxy_init  -- fs_proxy.c (HIGH). With LFS_THREADSAFE=0 the runtime
 *                     fs_opt_directly() returns 0, so all file operations go
 *                     through fs_req_proc -> fs_proxy thread proxy. Without
 *                     initialization the queue handles are all NULL, osal
 *                     silently returns an error for NULL handles, and every fs
 *                     operation "fake-succeeds" (ret_code=0 but no real
 *                     read/write) -- FS commands/FTP file transfer/SSL
 *                     certificate reads all spin uselessly (found in the
 *                     2026-09-04 audit, fixed in the same batch as atSockInit)
 *   Remaining application_init items:
 *   - main_proxy_init -- called explicitly in sim_boot_task (PS->AP message routing hub)
 *   - pppDialInit      -- called explicitly in sim_boot_task (PPP mutex;
 *                        otherwise ATD*99# triggers an osal assertion)
 *   - app_basic_config -- has external dependencies (sleep_lock/carrier); not compiled
 *   - cfun_pin / sleep_lock / net_log / carrier & opencpu demos etc.
 *     -- outside the simulator's compilation scope; nothing to do */
extern void at_init(void);
extern void appAtRespInit(void);
extern void main_proxy_init(void);
extern void net_init(void);
extern void atSockInit(void);
extern void ssl_config_init(void);
extern void fs_proxy_init(void);
extern void pppDialInit(void);

/* ------------------------------------------------------------------ */
/* Bridge task: the only entry for host bytes -> virtual devices       */
/*                                                                     */
/* RTOS APIs cannot be called directly from a bare Windows thread      */
/* (the MSVC-MingW port's catch-up tick delivery reads the current     */
/* task's ThreadState), so a real RTOS task does the polling.          */
/* ------------------------------------------------------------------ */

static void sim_bridge_task(void *arg)
{
    (void)arg;

    for (;;) {
        osDelay(1);
        sim_tty_poll();
    }
}

/* ------------------------------------------------------------------ */
/* Boot task: initialize devices and the AT framework in real-device    */
/* order                                                               */
/* ------------------------------------------------------------------ */

static void sim_boot_task(void *arg)
{
    osThreadAttr_t attr;
    const char *pcap_path = sim_main_pcap_path();

    (void)arg;

    LOGI("SIMBOOT", "boot task running");

    xy_printf(PLATFORM_AP, INFO_LOG, "[SIMBOOT] posix_device_init ...");
    posix_device_init();

    /* fs proxy thread (HIGH priority tier on the real device): with
     * LFS_THREADSAFE=0, all runtime file operations go through the
     * fs_req_proc proxy queue, so the proxy thread/queue/mutex must be
     * created first, otherwise every fs operation silently "fake-succeeds"
     * (no real read/write). Placed before at_init: any AT/SSL/FTP path that
     * touches files depends on it */
    xy_printf(PLATFORM_AP, INFO_LOG, "[SIMBOOT] fs_proxy_init (fs proxy thread) ...");
    fs_proxy_init();

    xy_printf(PLATFORM_AP, INFO_LOG, "[SIMBOOT] at_init (real AT framework) ...");
    at_init();

    xy_printf(PLATFORM_AP, INFO_LOG, "[SIMBOOT] appAtRespInit ...");
    appAtRespInit();

    /* Cross-module message proxy task (main_proxy): the routing hub for
     * PS->AP messages (on the real device, application_init schedules it
     * after CFUN_PIN and before network initialization). Calling it
     * explicitly here ensures the message queue is ready before net_init
     * registers network callbacks, avoiding loss of data_call_status_ind
     * during the boot bring-up phase. */
    xy_printf(PLATFORM_AP, INFO_LOG, "[SIMBOOT] main_proxy_init ...");
    main_proxy_init();

    /* Network subsystem: lwip tcpip thread + WAN netif glue (the foundation
     * of the fake-CP data plane; scheduled by application_init on the real
     * device, called explicitly on the host) */
    xy_printf(PLATFORM_AP, INFO_LOG, "[SIMBOOT] net_init (lwip/tcpip + WAN netif) ...");
    net_init();

    /* Application-layer socket/SSL/FTP registration (on the real device
     * app_start walks .appRegTable and schedules it at the LOW priority
     * tier; on the host, at_socket_utils.c.obj has no referenced symbols and
     * is not pulled out of libxynet.a by the linker, so it can only be
     * called explicitly here -- otherwise sockMgr never receives the
     * handleFunc for source=1/2/4, and QIOPEN/QSSLOPEN/QFTPCFG all get
     * "handleFunc not define" with no response) */
    xy_printf(PLATFORM_AP, INFO_LOG, "[SIMBOOT] atSockInit (socket/SSL/FTP sockMgr handlers) ...");
    atSockInit();

    xy_printf(PLATFORM_AP, INFO_LOG, "[SIMBOOT] ssl_config_init (SSL cfg mutex) ...");
    ssl_config_init();

    /* PPP mutex initialization: on the real device it is called
       automatically via the application_init macro; the simulator has no
       such scheduler, so it must be called explicitly, otherwise ATD*99#
       triggers an osal assertion */
    xy_printf(PLATFORM_AP, INFO_LOG, "[SIMBOOT] pppDialInit (PPP mutex) ...");
    pppDialInit();

    /* Packet capture: open before any packet flows (uplink hook is in the
     * original SDK path ps_netif_api.c, downlink hook is in sim_proxy's
     * proxy_rx task) */
    if (pcap_path[0] != '\0') {
        if (sim_pcap_open(pcap_path) != 0)
            xy_printf(PLATFORM_AP, WARN_LOG,
                      "[SIMBOOT] pcap capture requested but open failed: %s", pcap_path);
    }

    /* Bridge task: priority below atrcv(22)/atctl(21)/atproxy(21) */
    memset(&attr, 0, sizeof(attr));
    attr.name = "simbridge";
    attr.priority = osPriorityBelowNormal3;
    attr.stack_size = 4096;
    Sys_Assert(osThreadNew(sim_bridge_task, NULL, &attr) != NULL);

    /* Auto-activation at boot (user requirement: network available by
     * default): equivalent to automatically sending AT+CGACT=1, so the
     * business side has network at power-up without typing commands. The
     * data plane (sim_proxy) is already ready from the net_init step; here
     * we only trigger PDP activation (static address assignment), and the
     * AT channel sends/receives as usual */
    xy_printf(PLATFORM_AP, INFO_LOG,
              "[SIMBOOT] auto-dial: activating PDP cid=1 (like AT+CGACT=1) ...");
    xy4101_bridge_init();           /* bind sim-core callbacks to the XY4101 SDK */
    sim_vps_net_auto_activate();
    xy4101_proxy_rx_start();        /* start the proxy_rx FreeRTOS task */

    xy_printf(PLATFORM_AP, INFO_LOG,
              "[SIMBOOT] real AT framework up -- tasks: at_ctl/atproxy/atrcv/simbridge");
    osThreadExit();
}

/* ------------------------------------------------------------------ */
/* XY4101 chip-specific pin overrides of factory NV defaults           */
/*                                                                     */
/* All pin-class fields in sim_factory_nv.c are 255 (not connected).   */
/* XY4101-specific JTAG / debug UART / SIM / power-control pins are    */
/* injected by this function.                                          */
/* ------------------------------------------------------------------ */

static void xy4101_factory_nv_init(softap_fac_nv_t *nv)
{
    if (nv == NULL)
        return;

    nv->wkuprst_ctl = 1;
    nv->pwrkey_ctl  = 17;
    nv->cpjlink[0]  = 31;
    nv->cpjlink[1]  = 32;
    nv->csp_log_tx  = 26;
    nv->csp_log_rx  = 25;
    nv->sim0_det    = 3;
}

/* ------------------------------------------------------------------ */
/* target hook table (skeleton in sim_main.c, resolved at link time)    */
/* ------------------------------------------------------------------ */

const sim_target_hooks_t g_sim_target = {
    .product  = "XY4101 LTE Cat.1 Host Simulator",
    .byline   = "by Chengtian Liu",
    .coverage = {
        "Real-code coverage: AT core / socket / ping / NTP / time /",
        "                    FS / SSL / HTTP / FTP / MQTT / pcap"
    },
    .boot_task       = sim_boot_task,
    .factory_nv_init = xy4101_factory_nv_init,
};

/* Executable entry: tail-calls the generic skeleton directly (main is not put
 * in a static library; see the comment in sim_main.h) */
int main(int argc, char **argv)
{
    return sim_main(argc, argv);
}
