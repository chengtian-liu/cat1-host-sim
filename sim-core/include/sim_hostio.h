/*
 * sim_hostio.h -- host byte-stream IO layer (pure Win32, no RTOS dependency)
 *
 * Duty: send/receive "physical serial port" bytes on the Windows host.
 *   - RX: one host receive thread per channel (COM uses overlapped ReadFile,
 *         stdio uses ReadConsoleInput/ReadFile); bytes are pushed into a
 *         ring buffer;
 *   - TX: RTOS tasks call sim_hostio_tx() to enqueue; a host send thread
 *         writes the data out.
 *
 * Constraint: no function in this module may call RTOS APIs --
 *       the RTOS world only reaches this module through the bridge tasks
 *       of sim_tty_device.
 */
#ifndef SIM_HOSTIO_H
#define SIM_HOSTIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Channel numbers (one-to-one with the virtual ttys of sim_tty_device) */
#define SIM_HOSTIO_CH_USB      0   /* DEV_USB_AT backend (VSPE virtual serial port or console) */
#define SIM_HOSTIO_CH_LPUART   1   /* DEV_LPUART_AT backend (VSPE virtual serial port) */
#define SIM_HOSTIO_CH_MODEM    2   /* DEV_USB_MODEM backend (VSPE virtual serial port, PPP dial-up) */
#define SIM_HOSTIO_CH_MAX      3

/* Backend types */
#define SIM_HOSTIO_BE_NONE     0
#define SIM_HOSTIO_BE_COM      1
#define SIM_HOSTIO_BE_STDIO    2

/**
 * Open the channel with a COM port backend (VSPE virtual serial port, e.g. "COM5").
 * @return 0 success, -1 failure
 */
int sim_hostio_open_com(int ch, const char *port, int baud);

/**
 * Open the channel with a console backend (receive on stdin / send on stdout).
 * @return 0 success, -1 failure
 */
int sim_hostio_open_stdio(int ch);

/**
 * Non-blocking read from the channel RX ring buffer (called by bridge tasks).
 * @return number of bytes actually read
 */
int sim_hostio_read(int ch, void *buf, int max);

/**
 * Send bytes (queued into the TX queue, written out by the host send thread;
 * does not block the caller).
 */
void sim_hostio_tx(int ch, const void *data, int len);

/** Channel backend type */
int sim_hostio_backend(int ch);

#ifdef __cplusplus
}
#endif

#endif /* SIM_HOSTIO_H */
