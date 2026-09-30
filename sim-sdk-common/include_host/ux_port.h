/*
 * ux_port.h -- host shim for the USBX porting layer
 *
 * The real-device USBX porting layer (ports/inc/ux_port.h) conflicts with
 * Windows system headers under MingW:
 *   1. typedef signed char CHAR;  -- winnt.h already has typedef char CHAR;
 *   2. typedef ... INT/UINT/...   -- windef.h already defines types with the same names
 *   3. USB_DEV_TypeDef           -- hardware register mapping from xy4101_ll_usb_dev.h
 *
 * This shim is hit before usbx/ports/inc and simply skips all type
 * definitions -- even if the host does use USBX (USB serial/logging,
 * USB_AT=1 USB_LOG=1), it goes through the stub interface in usb_api.h and
 * does not depend on these basic type aliases.
 *
 * Notes:
 *   - UX_STANDALONE is already defined on the compile line, so ux_port.h
 *     skips tx_api.h (ThreadX).
 *   - This shim does not declare any USBX porting-layer functions/variables
 *     (their real-device translation units are not linked); it only breaks
 *     the type redefinition.
 */

#ifndef UX_PORT_H
#define UX_PORT_H

/* Satisfies the include-guard check for ux_port.h from UX_INCLUDE in ux_api.h */
#define UX_PORT_H

/* All basic types are already defined in <stdint.h>/<windows.h>; no duplicate typedefs */
#include <stdint.h>
#include <windows.h>

/* Declare a few basic macros used internally by USBX (just enough to get
 * compilation through; the values are irrelevant) */
#define USBX_MAX_SCRATCH_POOLS      8
#define UX_THREAD_STACK_NAME        "usbx"

/* ALIGN_TYPE -- the real-device ux_port.h defines it via ULONG; referenced by ux_utility.h */
#define ALIGN_TYPE_DEFINED
#define ALIGN_TYPE                  ULONG

/* ---- Empty definitions for the macros below if referenced by conditional compilation in the real-device ux_port.h ---- */
#ifndef VOID
#define VOID void
#endif
#ifndef UINT
#define UINT unsigned int
#endif
#ifndef ULONG
#define ULONG unsigned long
#endif

#endif /* UX_PORT_H */