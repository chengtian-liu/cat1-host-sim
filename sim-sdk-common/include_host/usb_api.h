/*
 * usb_api.h -- host override (for simulator builds only; the original SDK
 * file is unaffected)
 *
 * The real-device version lives at platform/kernel/usbx/usb_api/inc/usb_api.h
 * and pulls in the whole USBX stack (ux_api.h -> ThreadX glue / USB register
 * drivers), which the host neither needs nor can host. The AT framework
 * (at_ctrl) actually only uses the few declarations kept in this file, which
 * are extracted here verbatim per the real-device prototypes; the
 * implementation is provided by sim_at_shims.c (capability queries return
 * the simulator's usb_mode NV, state callback registration is a no-op).
 *
 * This directory (sim-sdk-common/include_host) comes first in xyat's -I
 * ordering, ensuring it shadows the real version in usbx/usb_api/inc.
 */

#ifndef SIM_HOST_USB_API_H
#define SIM_HOST_USB_API_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* USB status change function type. (matches the real-device prototype) */
typedef void (* usb_state_change_func_t) (int usb_state, void *data);

typedef enum
{
    USB_STATE_PLUGOUT         = 0,        /* plugout */
    USB_STATE_PLUGIN          = 1 << 0,   /* plugin */
    USB_STATE_SUSPEND_TO_U3   = 1 << 1,   /* suspend */
    USB_STATE_RESUME_TO_U0    = 1 << 2,   /* resume */
    USB_STATE_AT_DTR          = 1 << 3,   /* ACM AT DTR - signal level */
    USB_STATE_MODEM_DTR       = 1 << 4,   /* ACM modem DTR - signal level */
    USB_STATE_MAX
} usb_state_t;

void UsbRegistStateCallback(int usb_state, usb_state_change_func_t change_func);
void UsbStateCallbackHandle(usb_state_t usb_state, void *data);
bool UsbUnregistStateCallback(int usb_state, usb_state_change_func_t change_func);

bool isUsbAtSupported(void);
bool isUsbModemAtSupported(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_HOST_USB_API_H */
