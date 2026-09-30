/*
 * xy4101_usb_udc.h -- host shim
 *
 * The real-device version is the hardware abstraction layer of the XY4101
 * chip USB device controller. The simulator's pkt_process.c includes this
 * header unconditionally, but because USB_NET_SUPPORT=0 all USB data paths
 * are excluded by #if and no symbol from this header is actually referenced.
 *
 * This shim works together with the ux_port.h shim: ux_port.h skips USBX
 * porting-layer conflicts, and this header provides the minimal xy4101-level
 * compilation skeleton.
 */

#ifndef XY4101_USB_UDC_H
#define XY4101_USB_UDC_H

/* USB_DEV_TypeDef and other chip-level definitions are provided centrally by
 * the xy4101_ll_usb_dev.h shim included via ux_api.h; not redefined here. */
#include "xy4101_ll_usb_dev.h"

/* Endpoint numbers (for compilation only; the host has no actual USB hardware) */
#define USB_SUPPORT_PHY_EP_NUM      11
#define USB_EP_BULK_IN              1
#define USB_EP_BULK_OUT             0

/* Cache operations -- empty macros (the real device operates the hardware cache via csi_dcache_clean_range etc.) */
#define USB_CACHE_CLEAN(addr, size)             do { (void)(addr); (void)(size); } while(0)
#define USB_CACHE_INVALID(addr, size)           do { (void)(addr); (void)(size); } while(0)
#define USB_CACHE_INVALID_NOCHECK(addr, size)   do { (void)(addr); (void)(size); } while(0)

#define USB_ALIGNED(size)           __attribute__((aligned(size)))
#define USB_CACHE_LINE_SIZE         32

/* Chip-level assert -- mapped to the host's generic assertion */
#define USB_ASSERT(x)               do { (void)(x); } while(0)

#endif /* XY4101_USB_UDC_H */
