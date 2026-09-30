/*
 * sim_tty_device.c - host virtual tty device implementation
 *
 * The device behavior is a faithful copy of the SDK standard device
 * template null_device.c:
 *   - read drains the FIFO; when empty it clears POSIX_OPT_FLAG_SEL_READ
 *     (select blocks again)
 *   - on incoming data it sets SEL_READ + posix_signal_release(sel_sig)
 *     to wake up select
 *   - read never blocks (atrcv waits for data with posix_select; the
 *     device read itself is non-blocking)
 *
 * The only difference from a real device: the FIFO's data source is not
 * a hardware interrupt but the bridge task, which calls sim_tty_poll()
 * every 1ms to move data from the host ring buffer.
 */
#include "sim_tty_device.h"
#include "sim_hostio.h"
#include "sim_log.h"

#include "posix_io.h"
#include "posix_device.h"

#include <string.h>
#include <errno.h>

#define SIM_TTY_FIFO_SIZE   4096
#define SIM_TTY_POLL_CHUNK  512

typedef struct {
    posix_device_t *dev;               /* posix framework device descriptor, bound at registration */
    posix_signal_t  rx_sig;            /* blocking-read signal (spare; read is non-blocking by default) */
    int             host_ch;           /* host backend channel, -1 = no backend */

    /* Device RX FIFO (written by the bridge task, read by the atrcv task) */
    uint8_t         fifo[SIM_TTY_FIFO_SIZE];
    int             head;
    int             tail;
    int             count;

    const char     *name;
} sim_tty_inst_t;

static sim_tty_inst_t s_tty[SIM_TTY_NUM] = {
    { NULL, (posix_signal_t)0, SIM_HOSTIO_CH_USB,    {0}, 0, 0, 0, "usb_at"    },
    { NULL, (posix_signal_t)0, SIM_HOSTIO_CH_LPUART, {0}, 0, 0, 0, "lpuart_at" },
    { NULL, (posix_signal_t)0, SIM_HOSTIO_CH_MODEM,  {0}, 0, 0, 0, "usb_modem" },
};

/* ------------------------------------------------------------------ */
/* Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static sim_tty_inst_t *find_inst(void *dev)
{
    int i;

    for (i = 0; i < SIM_TTY_NUM; i++) {
        if (s_tty[i].dev == (posix_device_t *)dev)
            return &s_tty[i];
    }
    return NULL;
}

/* Wake up select/blocked readers: the xxx_rx_sem_release pattern from
 * the standard device template */
static void sim_tty_rx_sem_release(sim_tty_inst_t *inst)
{
    int critical_sta;

    if (inst->dev == NULL)
        return;

    /* Step 1: set the "data available" flag */
    critical_sta = posix_critical_enter();
    inst->dev->opt_flags |= POSIX_OPT_FLAG_SEL_READ;
    posix_critical_exit(critical_sta);

    /* Step 2: wake up threads blocked in select() */
    if (inst->dev->sel_sig != (posix_signal_t)0)
        posix_signal_release(inst->dev->sel_sig);

    /* Step 3: wake up threads blocked in read() (non-blocking by
     * default, so this is never reached) */
    if (inst->dev->opt_flags & POSIX_OPT_FLAG_READ_BLOCK) {
        critical_sta = posix_critical_enter();
        inst->dev->opt_flags &= ~POSIX_OPT_FLAG_READ_BLOCK;
        posix_critical_exit(critical_sta);

        posix_signal_release(inst->rx_sig);
    }
}

/* ------------------------------------------------------------------ */
/* posix device operations                                            */
/* ------------------------------------------------------------------ */

static int sim_tty_open(void *dev)
{
    sim_tty_inst_t *inst = find_inst(dev);

    if (inst == NULL)
        return -ENODEV;

    inst->dev = (posix_device_t *)dev;

    if (inst->rx_sig == (posix_signal_t)0)
        inst->rx_sig = posix_signal_new();

    LOGI("TTY", "/dev/%s opened (host backend ch=%d)", inst->name, inst->host_ch);
    return 0;
}

static ssize_t sim_tty_read(void *dev, const void *buf, const size_t count)
{
    sim_tty_inst_t *inst = find_inst(dev);
    posix_device_t *device = (posix_device_t *)dev;
    uint8_t *out = (uint8_t *)buf;
    size_t n = 0;
    int critical_sta;

    if (inst == NULL || count == 0)
        return 0;

    critical_sta = posix_critical_enter();
    while (n < count && inst->count > 0) {
        out[n++] = inst->fifo[inst->tail];
        inst->tail = (inst->tail + 1) % SIM_TTY_FIFO_SIZE;
        inst->count--;
    }

    /* FIFO drained: clear SEL_READ; select will block until the next data arrives */
    if ((device->opt_flags & POSIX_OPT_FLAG_SEL_READ) && (inst->count == 0))
        device->opt_flags &= ~POSIX_OPT_FLAG_SEL_READ;
    posix_critical_exit(critical_sta);

    return (ssize_t)n;
}

static ssize_t sim_tty_write(void *dev, const void *buf, const size_t count)
{
    sim_tty_inst_t *inst = find_inst(dev);

    if (inst == NULL || count == 0)
        return 0;

    /* Framework output: hand off to the host TX thread */
    sim_hostio_tx(inst->host_ch, buf, (int)count);
    return (ssize_t)count;
}

/* The host has no real hardware parameters to configure: all ioctls are
 * accepted (baud rate/flow control etc. are meaningless for simulation) */
static int sim_tty_ioctl(void *dev, unsigned long request, unsigned long arg)
{
    sim_tty_inst_t *inst = find_inst(dev);

    if (inst == NULL)
        return -ENODEV;

    LOGD("TTY", "/dev/%s ioctl req=0x%lx arg=0x%lx (accepted)",
         inst->name, request, arg);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Registration callbacks (one per instance; binds instance pointer)  */
/* ------------------------------------------------------------------ */

static void sim_tty_common_init(void *dev, int idx)
{
    posix_device_t *dev_dsct = (posix_device_t *)dev;

    dev_dsct->open  = sim_tty_open;
    dev_dsct->read  = sim_tty_read;
    dev_dsct->write = sim_tty_write;
    dev_dsct->ioctl = sim_tty_ioctl;
    dev_dsct->close = NULL;

    /* Registration binds the instance (confirmed again at open time) */
    s_tty[idx].dev = dev_dsct;
}

static void sim_tty_usb_dev_init(void *dev)
{
    sim_tty_common_init(dev, SIM_TTY_USB);
}

static void sim_tty_lpuart_dev_init(void *dev)
{
    sim_tty_common_init(dev, SIM_TTY_LPUART);
}

static void sim_tty_modem_dev_init(void *dev)
{
    sim_tty_common_init(dev, SIM_TTY_MODEM);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void posix_device_init(void)
{
    /* null device: SDK atrcv uses it to wake up select (message-style
     * device, compiled as-is) */
    posix_register(DEV_NULL_DEVICE, null_device_init);

    /* Virtual serial ports: AT / LPUART / MODEM (PPP) */
    posix_register(DEV_USB_AT, sim_tty_usb_dev_init);
    posix_register(DEV_LPUART_AT, sim_tty_lpuart_dev_init);
    posix_register(DEV_USB_MODEM, sim_tty_modem_dev_init);

    LOGI("TTY", "sim posix devices registered: %s %s %s %s",
         DEV_USB_AT, DEV_LPUART_AT, DEV_USB_MODEM, DEV_NULL_DEVICE);
}

void sim_tty_set_backend(int tty, int host_ch)
{
    if (tty < 0 || tty >= SIM_TTY_NUM)
        return;
    s_tty[tty].host_ch = host_ch;
}

void sim_tty_poll(void)
{
    int i;

    for (i = 0; i < SIM_TTY_NUM; i++) {
        sim_tty_inst_t *inst = &s_tty[i];
        uint8_t chunk[SIM_TTY_POLL_CHUNK];
        int total = 0;

        if (inst->dev == NULL || inst->host_ch < 0)
            continue;

        for (;;) {
            int free_space;
            int got;
            int critical_sta;

            critical_sta = posix_critical_enter();
            free_space = SIM_TTY_FIFO_SIZE - inst->count;
            posix_critical_exit(critical_sta);

            if (free_space <= 0)
                break;

            got = sim_hostio_read(inst->host_ch, chunk,
                                  free_space < (int)sizeof(chunk) ? free_space : (int)sizeof(chunk));
            if (got <= 0)
                break;

            critical_sta = posix_critical_enter();
            for (int k = 0; k < got && inst->count < SIM_TTY_FIFO_SIZE; k++) {
                inst->fifo[inst->head] = chunk[k];
                inst->head = (inst->head + 1) % SIM_TTY_FIFO_SIZE;
                inst->count++;
            }
            posix_critical_exit(critical_sta);
            total += got;
        }

        if (total > 0)
            sim_tty_rx_sem_release(inst);
    }
}
