/*
 * xy4101_ll_usb_dev.h -- host shim
 *
 * The real-device version is the low-level hardware abstraction (LL driver)
 * of the XY4101 chip USB device controller, containing register-level
 * operations and inline functions. ux_api.h includes this header
 * unconditionally.
 *
 * The simulator has no USB hardware, and USB_NET_SUPPORT=0 excludes all USB
 * data paths, so this shim only needs an empty minimal skeleton.
 */

#ifndef XY4101_LL_USB_DEV_H
#define XY4101_LL_USB_DEV_H

#include <stdint.h>

/* USB device controller register-map placeholder (type declaration only; never instantiated on the host) */
typedef struct { volatile uint32_t reserved[64]; } USB_DEV_TypeDef;

/* External register base-address declaration (referenced internally by ux_api.h/ux_utility.h; not actually accessed on the host) */
extern USB_DEV_TypeDef *USB_DEVx;

#endif /* XY4101_LL_USB_DEV_H */
