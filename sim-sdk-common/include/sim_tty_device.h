/*
 * sim_tty_device.h -- host virtual tty devices (posix device layer)
 *
 * Implements three virtual serial ports using the SDK's native posix device
 * model (same interface set as null_device.c), serving as the only "physical
 * ports" the at_ctrl framework is aware of:
 *
 *   DEV_USB_AT    (/dev/usb_at)    <-> host channel 0 (VSPE virtual serial port/console)
 *   DEV_LPUART_AT (/dev/lpuart_at) <-> host channel 1 (VSPE virtual serial port)
 *   DEV_USB_MODEM (/dev/modem)     <-> host channel 2 (VSPE virtual serial port, PPP dial-up)
 *
 * Data plane:
 *   RX: sim_tty_poll() is called periodically by the bridge task (a real
 *       RTOS task); it drains bytes from the host ring buffer into the
 *       device FIFO, sets SEL_READ per the standard device model, and
 *       releases sel_sig to wake the atrcv task blocked in posix_select;
 *   TX: the framework arrives here via ctx->lowlevelOutput -> posix_write
 *       and is forwarded straight to sim_hostio_tx for the host TX thread
 *       to write out.
 */
#ifndef SIM_TTY_DEVICE_H
#define SIM_TTY_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Virtual tty instance numbers */
#define SIM_TTY_USB      0
#define SIM_TTY_LPUART   1
#define SIM_TTY_MODEM    2
#define SIM_TTY_NUM      3

/**
 * Simulator's device registration entry (replaces the SDK's posix_device.c;
 * the original file is not linked). Registers DEV_USB_AT / DEV_LPUART_AT /
 * DEV_USB_MODEM / DEV_NULL_DEVICE. Declaration comes from posix_device.h.
 */
void posix_device_init(void);

/**
 * Called periodically by the bridge task: moves host RX data into the
 * virtual devices and wakes select.
 */
void sim_tty_poll(void);

/**
 * Assign the host backend channel for a virtual tty (defaults: USB->channel 0,
 * LPUART->channel 1, MODEM->channel 2). Pass -1 for host_ch to give the
 * virtual port no backend (device exists but never receives data).
 */
void sim_tty_set_backend(int tty, int host_ch);

#ifdef __cplusplus
}
#endif

#endif /* SIM_TTY_DEVICE_H */
