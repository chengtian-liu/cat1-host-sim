/*
 * sim_tap_net.c -- TAP virtual NIC transceiver for the USB ECM/RNDIS gateway
 *
 * On the real device the USB ECM/RNDIS class drivers (ux_device_cdc_ecm.c /
 * ux_device_rndis.c) sit between the USB device controller and the gateway
 * netif. Those class drivers are not compiled into the simulator; instead a
 * TAP-Windows Adapter V9 exposes the same thing to the host as a plain
 * Ethernet NIC, and this file provides the six usb_api.h data-plane entry
 * points the network stack calls:
 *
 *   ux_network_state / ux_network_send / ux_network_free
 *   ux_network_group_size_set / get_usb_network_status
 *   usb_gw_process_active / usb_gw_process_deactive
 *
 * plus the USB_RECV_MEM_USED_MAX constant that pkt_process.c consumes for
 * flow control.
 *
 * Data plane:
 *   uplink   : host client -> TAP read thread -> RTOS rx task ->
 *              usbEthDataInput() -> fastForwardUlIpPkt -> PS (sim_proxy)
 *   downlink : PS -> lwip -> gwEthLinkOutput -> usbEthDataSendToUx ->
 *              ux_network_send() -> TAP write
 *
 * Memory model (zero-copy, matches the real device):
 *   - rx: net_usbnetmalloc(ip_len) reserves a 64-byte header; the Ethernet
 *     frame is copied in front of the returned IP pointer; usbEthDataInput()
 *     takes ownership and frees it via net_free().
 *   - tx: ux_network_send() writes the frame to the TAP handle and, on
 *     success, frees it via net_free(buffer + SIZEOF_ETH_HDR); on failure
 *     the caller (gwEthLinkOutput) frees it.
 */

#include <windows.h>
/* The ERROR macro in wingdi.h may clash with SDK enum names; discard after use */
#ifdef ERROR
#undef ERROR
#endif

#include <winreg.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xy_system.h"           /* cmsis_os2 + xy_printf */
#include "net_mem.h"             /* net_usbnetmalloc / net_free */
#include "pkt_process.h"         /* usbEthDataInput */
#include "usb_api.h"             /* shadow: usb_network_status + prototypes */
#include "lwip/prot/ethernet.h"  /* SIZEOF_ETH_HDR */

#include "sim_log.h"
#include "sim_tap_net.h"

/* USBX return codes (real device: usbx/core/inc/ux_api.h) */
#define SIM_UX_SUCCESS       0x00U
#define SIM_UX_ERROR         0xFFU
#define SIM_UX_INVALID_STATE 0xFBU

#define SIM_TAP_COMPONENT_ID    "tap0901"
#define SIM_TAP_CONNECTION_NAME "SimRNDIS"
#define SIM_TAP_RX_QUEUE_SIZE   256
#define SIM_TAP_FRAME_BUF_SIZE  2048U

/* TAP-Windows ioctl (tap-windows.h from the OpenVPN driver):
 * CTL_CODE(FILE_DEVICE_UNKNOWN, 6, METHOD_BUFFERED, FILE_ANY_ACCESS) */
#define SIM_TAP_IOCTL_SET_MEDIA_STATUS 0x00220018UL

/* The real device caps the USB receive memory at 192 * 1600 bytes
 * (ux_app/src/ux_network.c); pkt_process.c uses this to decide flow control. */
unsigned int USB_RECV_MEM_USED_MAX = 192 * 1600;

typedef struct
{
    void    *frame;   /* raw Ethernet frame, allocated with plain malloc */
    uint16_t len;
} sim_tap_rx_slot_t;

static HANDLE            s_tap_handle       = INVALID_HANDLE_VALUE;
static usb_network_status s_network_status  = USB_NETWORK_MAX_NUM;

static CRITICAL_SECTION  s_rx_cs;
static int               s_rx_cs_inited     = 0;
static sim_tap_rx_slot_t s_rx_slots[SIM_TAP_RX_QUEUE_SIZE];
static int               s_rx_head          = 0;
static int               s_rx_tail          = 0;
static int               s_rx_count         = 0;

/* ------------------------------------------------------------------ */
/* TAP device-path resolution (registry enumeration)                   */
/* ------------------------------------------------------------------ */

static int tap_query_connection_name(const char *guid, char *name, size_t name_cap)
{
    HKEY  key = NULL;
    LONG  rc;
    DWORD type = 0, size = (DWORD)name_cap;
    char  subpath[512];

    snprintf(subpath, sizeof(subpath),
             "SYSTEM\\CurrentControlSet\\Control\\Network\\"
             "{4D36E972-E325-11CE-BFC1-08002BE10318}\\%s\\Connection",
             guid);

    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, subpath, 0, KEY_READ, &key);
    if (rc != ERROR_SUCCESS)
        return -1;

    rc = RegQueryValueExA(key, "Name", NULL, &type, (LPBYTE)name, &size);
    RegCloseKey(key);

    return (rc == ERROR_SUCCESS && type == REG_SZ) ? 0 : -1;
}

/*
 * Find the TAP adapter whose connection name is exactly "SimRNDIS".
 * Other tap0901 adapters (e.g. OpenVPN's) are deliberately NOT used as a
 * fallback -- grabbing one would silently inject module traffic into a
 * foreign virtual NIC. If only foreign adapters exist, fail with a log
 * naming them so the user can create/rename the right one.
 */
static int tap_find_device_path(char *path, size_t path_cap)
{
    HKEY  class_key = NULL;
    LONG  rc;
    DWORD idx;
    int   foreign_seen = 0;
    char  foreign_name[256] = {0};

    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                       "SYSTEM\\CurrentControlSet\\Control\\Class\\"
                       "{4D36E972-E325-11CE-BFC1-08002BE10318}",
                       0, KEY_READ, &class_key);
    if (rc != ERROR_SUCCESS)
        return -1;

    for (idx = 0; ; idx++)
    {
        char  subkey[64];
        char  component[64] = {0};
        char  guid[64]       = {0};
        char  name[256]      = {0};
        HKEY  adapter_key = NULL;
        DWORD type = 0, size = 0;

        size = sizeof(subkey);
        rc = RegEnumKeyExA(class_key, idx, subkey, &size, NULL, NULL, NULL, NULL);
        if (rc != ERROR_SUCCESS)
            break;

        if (RegOpenKeyExA(class_key, subkey, 0, KEY_READ, &adapter_key) != ERROR_SUCCESS)
            continue;

        size = sizeof(component);
        rc = RegQueryValueExA(adapter_key, "ComponentId", NULL, &type,
                              (LPBYTE)component, &size);
        if (rc != ERROR_SUCCESS || type != REG_SZ ||
            strcmp(component, SIM_TAP_COMPONENT_ID) != 0)
        {
            RegCloseKey(adapter_key);
            continue;
        }

        size = sizeof(guid);
        rc = RegQueryValueExA(adapter_key, "NetCfgInstanceId", NULL, &type,
                              (LPBYTE)guid, &size);
        RegCloseKey(adapter_key);
        if (rc != ERROR_SUCCESS || type != REG_SZ || guid[0] == '\0')
            continue;

        if (tap_query_connection_name(guid, name, sizeof(name)) == 0 &&
            strcmp(name, SIM_TAP_CONNECTION_NAME) == 0)
        {
            /* NetCfgInstanceId comes back brace-wrapped ({8A237FCF-...});
             * the device-path template below also adds a pair of braces, so
             * strip any existing pair first to avoid a double-braced path
             * like \\.\Global\{{...}}.tap. */
            const char *g = guid;
            char bare[64];
            size_t n;

            if (g[0] == '{')
                g++;
            n = strlen(g);
            if (n > 0 && g[n - 1] == '}')
                n--;
            if (n >= sizeof(bare))
                n = sizeof(bare) - 1;
            memcpy(bare, g, n);
            bare[n] = '\0';

            snprintf(path, path_cap, "\\\\.\\Global\\{%s}.tap", bare);
            RegCloseKey(class_key);
            return 0;
        }

        /* Remember the first foreign tap0901 adapter for the error log only */
        if (!foreign_seen)
        {
            foreign_seen = 1;
            if (name[0] != '\0')
                snprintf(foreign_name, sizeof(foreign_name), "%s", name);
            else
                snprintf(foreign_name, sizeof(foreign_name), "%s", guid);
        }
    }

    RegCloseKey(class_key);

    if (foreign_seen)
        LOGE("SIMTAP", "TAP adapter \"%s\" exists but is not named \""
             SIM_TAP_CONNECTION_NAME "\"; refusing to use it (create/rename "
             "one with tools/tap_adapter.py)", foreign_name);

    return -1;
}

/* ------------------------------------------------------------------ */
/* TAP link-state helper                                               */
/* ------------------------------------------------------------------ */

static void tap_set_media_status(int connected)
{
    ULONG status = connected ? 1UL : 0UL;
    DWORD len = 0;

    if (s_tap_handle == INVALID_HANDLE_VALUE)
        return;

    DeviceIoControl(s_tap_handle, SIM_TAP_IOCTL_SET_MEDIA_STATUS,
                    &status, sizeof(status), &status, sizeof(status), &len, NULL);
}

/* ------------------------------------------------------------------ */
/* Downlink: lwip -> TAP (called on the tcpip thread)                  */
/* ------------------------------------------------------------------ */

unsigned int ux_network_state(void)
{
    if (s_tap_handle != INVALID_HANDLE_VALUE &&
        s_network_status == USB_NETWORK_CONNECT)
        return SIM_UX_SUCCESS;

    return SIM_UX_INVALID_STATE;
}

unsigned int ux_network_send(void *buffer, unsigned long count)
{
    DWORD written = 0;

    if (s_tap_handle == INVALID_HANDLE_VALUE ||
        s_network_status != USB_NETWORK_CONNECT)
        return SIM_UX_INVALID_STATE;

    if (!WriteFile(s_tap_handle, buffer, (DWORD)count, &written, NULL) ||
        written != count)
        return SIM_UX_ERROR;

    /* Success: ownership is consumed; release the IP payload */
    net_free((char *)buffer + SIZEOF_ETH_HDR);

    return SIM_UX_SUCCESS;
}

unsigned int ux_network_free(void *buffer)
{
    (void)buffer;
    return SIM_UX_ERROR;   /* mirrors the real device's disabled stub */
}

void ux_network_group_size_set(unsigned char group_size)
{
    (void)group_size;   /* TAP has no RNDIS packet bundling */
}

usb_network_status get_usb_network_status(void)
{
    return s_network_status;
}

void usb_gw_process_active(void)
{
    if (s_network_status == USB_NETWORK_CONNECT)
        return;

    s_network_status = USB_NETWORK_CONNECT;
    tap_set_media_status(1);
    LOGI("SIMTAP", "gateway link UP");
}

void usb_gw_process_deactive(void)
{
    if (s_network_status == USB_NETWORK_DISCONNECT)
        return;

    tap_set_media_status(0);
    s_network_status = USB_NETWORK_DISCONNECT;
    LOGI("SIMTAP", "gateway link DOWN");
}

/* ------------------------------------------------------------------ */
/* Uplink: TAP -> lwip/PS                                              */
/* ------------------------------------------------------------------ */

static void tap_rx_enqueue(void *frame, uint16_t frame_len)
{
    int drop = 0;

    EnterCriticalSection(&s_rx_cs);
    if (s_rx_count >= SIM_TAP_RX_QUEUE_SIZE)
        drop = 1;
    else
    {
        s_rx_slots[s_rx_tail].frame = frame;
        s_rx_slots[s_rx_tail].len   = frame_len;
        s_rx_tail = (s_rx_tail + 1) % SIM_TAP_RX_QUEUE_SIZE;
        s_rx_count++;
    }
    LeaveCriticalSection(&s_rx_cs);

    if (drop)
        free(frame);
}

static DWORD WINAPI tap_host_thread(LPVOID arg)
{
    (void)arg;

    for (;;)
    {
        void  *frame;
        DWORD  n = 0;

        frame = malloc(SIM_TAP_FRAME_BUF_SIZE);
        if (frame == NULL)
        {
            Sleep(1);
            continue;
        }

        if (!ReadFile(s_tap_handle, frame, SIM_TAP_FRAME_BUF_SIZE, &n, NULL))
        {
            DWORD err = GetLastError();
            free(frame);
            if (err == ERROR_OPERATION_ABORTED || err == ERROR_INVALID_HANDLE)
                break;
            Sleep(1);
            continue;
        }

        if (n >= SIZEOF_ETH_HDR)
            tap_rx_enqueue(frame, (uint16_t)n);
        else
            free(frame);
    }

    return 0;
}

static void tap_rx_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        void    *frame = NULL;
        void    *ipdata;
        void    *frame_buf;
        uint16_t len = 0;

        EnterCriticalSection(&s_rx_cs);
        if (s_rx_count > 0)
        {
            frame = s_rx_slots[s_rx_head].frame;
            len   = s_rx_slots[s_rx_head].len;
            s_rx_head = (s_rx_head + 1) % SIM_TAP_RX_QUEUE_SIZE;
            s_rx_count--;
        }
        LeaveCriticalSection(&s_rx_cs);

        if (frame == NULL)
        {
            osDelay(1);
            continue;
        }

        /* Copy into a net_usbnetmalloc block (64-byte reserved header);
         * usbEthDataInput() takes ownership and frees via net_free(). */
        ipdata = net_usbnetmalloc((size_t)(len - SIZEOF_ETH_HDR));
        if (ipdata == NULL)
        {
            free(frame);
            continue;
        }

        frame_buf = (char *)ipdata - SIZEOF_ETH_HDR;
        memcpy(frame_buf, frame, len);
        free(frame);

        usbEthDataInput(frame_buf, len);
    }
}

/* ------------------------------------------------------------------ */
/* Public entry                                                        */
/* ------------------------------------------------------------------ */

int sim_tap_net_start(void)
{
    char           dev_path[256];
    osThreadAttr_t attr;

    if (s_tap_handle != INVALID_HANDLE_VALUE)
        return 0;   /* already up */

    if (tap_find_device_path(dev_path, sizeof(dev_path)) != 0)
    {
        LOGE("SIMTAP", "no TAP adapter named \"" SIM_TAP_CONNECTION_NAME
             "\" found (create one with tools/tap_adapter.py)");
        return -1;
    }

    s_tap_handle = CreateFileA(dev_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, NULL);
    if (s_tap_handle == INVALID_HANDLE_VALUE)
    {
        LOGE("SIMTAP", "open %s failed (err=%lu)", dev_path, (unsigned long)GetLastError());
        return -1;
    }

    LOGI("SIMTAP", "opened %s", dev_path);

    InitializeCriticalSection(&s_rx_cs);
    s_rx_cs_inited = 1;

    /* The physical USB link is "plugged in" as soon as the handle is open;
     * the gateway netif link callback toggles it later via
     * usb_gw_process_active/deactive as PDP comes up/down. */
    tap_set_media_status(1);

    if (CreateThread(NULL, 0, tap_host_thread, NULL, 0, NULL) == NULL)
    {
        LOGE("SIMTAP", "CreateThread(tap_host) failed (err=%lu)", (unsigned long)GetLastError());
        CloseHandle(s_tap_handle);
        s_tap_handle = INVALID_HANDLE_VALUE;
        return -1;
    }

    memset(&attr, 0, sizeof(attr));
    attr.name       = "taprx";
    attr.priority   = osPriorityNormal;
    attr.stack_size = 16384;
    if (osThreadNew(tap_rx_task, NULL, &attr) == NULL)
    {
        LOGE("SIMTAP", "cannot spawn tap_rx task");
        /* host thread + handle remain; the rx path just never drains */
    }

    return 0;
}
