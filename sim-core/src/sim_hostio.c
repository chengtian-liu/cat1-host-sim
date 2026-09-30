/*
 * sim_hostio.c -- host byte-stream IO layer implementation (pure Win32)
 *
 * Three pieces per channel:
 *   RX ring (written by host receive thread / read by bridging task)
 *   TX ring (written by RTOS side / read by host send thread)
 *   CRITICAL_SECTION guarding the rings (shared by host threads and FreeRTOS
 *   tasks; on the MSVC-MingW port FreeRTOS critical sections ultimately just
 *   disable the simulated host interrupts too, so using Win32 critical
 *   sections here is safe for both sides)
 */
#include "sim_hostio.h"
#include "sim_log.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define RX_RING_SIZE   8192
#define TX_RING_SIZE   16384
#define RX_CHUNK       256

typedef struct {
    uint8_t buf[RX_RING_SIZE > TX_RING_SIZE ? RX_RING_SIZE : TX_RING_SIZE];
    int cap;
    int head;   /* write index */
    int tail;   /* read index */
    int count;
} byte_ring_t;

typedef struct {
    int backend;                /* SIM_HOSTIO_BE_xxx */

    CRITICAL_SECTION cs;

    byte_ring_t rx;
    byte_ring_t tx;

    HANDLE hcom;                /* COM handle (invalid for stdio backend) */
    HANDLE rx_evt;
    HANDLE tx_evt;
    HANDLE rx_thread;
    HANDLE tx_thread;
    volatile LONG running;

    char name[32];
} hostio_ch_t;

static hostio_ch_t s_ch[SIM_HOSTIO_CH_MAX];
static volatile LONG s_stdio_used = 0;   /* console backend: only one channel allowed globally */

/* ------------------------------------------------------------------ */
/* Ring buffer (caller holds the lock)                                   */
/* ------------------------------------------------------------------ */

static void ring_init(byte_ring_t *r, int cap)
{
    r->cap = cap;
    r->head = r->tail = r->count = 0;
}

static int ring_write(byte_ring_t *r, const uint8_t *data, int len)
{
    int n = 0;

    while (n < len && r->count < r->cap) {
        r->buf[r->head] = data[n++];
        r->head = (r->head + 1) % r->cap;
        r->count++;
    }
    return n;
}

static int ring_read(byte_ring_t *r, uint8_t *data, int max)
{
    int n = 0;

    while (n < max && r->count > 0) {
        data[n++] = r->buf[r->tail];
        r->tail = (r->tail + 1) % r->cap;
        r->count--;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* TX send thread                                                        */
/* ------------------------------------------------------------------ */

static DWORD WINAPI tx_thread(LPVOID arg)
{
    hostio_ch_t *c = (hostio_ch_t *)arg;
    uint8_t chunk[512];

    while (c->running) {
        if (WaitForSingleObject(c->tx_evt, 200) == WAIT_OBJECT_0)
            ResetEvent(c->tx_evt);   /* manual-reset event: reset before draining,
                                      * otherwise once the first TX sets it the event
                                      * never clears and this thread spins at 100%
                                      * on one core (same fix as the sim_log writer) */
        if (!c->running)
            break;

        for (;;) {
            int n;

            EnterCriticalSection(&c->cs);
            n = ring_read(&c->tx, chunk, sizeof(chunk));
            LeaveCriticalSection(&c->cs);
            if (n <= 0)
                break;

            DWORD written = 0;
            if (c->backend == SIM_HOSTIO_BE_COM) {
                OVERLAPPED ov;
                memset(&ov, 0, sizeof(ov));
                ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
                if (!WriteFile(c->hcom, chunk, (DWORD)n, &written, &ov)) {
                    if (GetLastError() == ERROR_IO_PENDING) {
                        WaitForSingleObject(ov.hEvent, 2000);
                        GetOverlappedResult(c->hcom, &ov, &written, FALSE);
                    }
                }
                CloseHandle(ov.hEvent);
            } else {
                WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), chunk, (DWORD)n,
                          &written, NULL);
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* RX receive thread                                                     */
/* ------------------------------------------------------------------ */

static void rx_push(hostio_ch_t *c, const uint8_t *data, int len)
{
    EnterCriticalSection(&c->cs);
    ring_write(&c->rx, data, len);
    LeaveCriticalSection(&c->cs);
}

static DWORD WINAPI rx_thread_com(LPVOID arg)
{
    hostio_ch_t *c = (hostio_ch_t *)arg;
    uint8_t chunk[RX_CHUNK];

    while (c->running) {
        OVERLAPPED ov;
        memset(&ov, 0, sizeof(ov));
        ov.hEvent = c->rx_evt;
        ResetEvent(c->rx_evt);

        DWORD got = 0;
        BOOL ok = ReadFile(c->hcom, chunk, sizeof(chunk), &got, &ov);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                DWORD w = WaitForSingleObject(c->rx_evt, 200);
                if (w == WAIT_OBJECT_0) {
                    if (!GetOverlappedResult(c->hcom, &ov, &got, FALSE))
                        got = 0;
                } else if (w == WAIT_TIMEOUT) {
                    CancelIo(c->hcom);
                    continue;
                } else {
                    break;
                }
            } else if (err == ERROR_OPERATION_ABORTED) {
                continue;
            } else {
                LOGE("HOSTIO", "%s read error %lu", c->name, (unsigned long)err);
                break;
            }
        }
        if (got > 0)
            rx_push(c, chunk, (int)got);
    }

    LOGI("HOSTIO", "%s rx thread exit", c->name);
    return 0;
}

static DWORD WINAPI rx_thread_stdio(LPVOID arg)
{
    hostio_ch_t *c = (hostio_ch_t *)arg;
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD ftype = GetFileType(hin);

    if (ftype == FILE_TYPE_CHAR) {
        /* Real console: receive byte by byte from key events (no need to wait for Enter) */
        INPUT_RECORD recs[64];
        while (c->running) {
            DWORD n = 0;
            if (!ReadConsoleInputA(hin, recs, 64, &n) || n == 0) {
                if (!c->running)
                    break;
                Sleep(10);
                continue;
            }
            for (DWORD i = 0; i < n; i++) {
                if (recs[i].EventType == KEY_EVENT &&
                    recs[i].Event.KeyEvent.bKeyDown) {
                    char ch = recs[i].Event.KeyEvent.uChar.AsciiChar;
                    if (ch != 0)
                        rx_push(c, (const uint8_t *)&ch, 1);
                }
            }
        }
    } else {
        /* Pipe / redirection */
        uint8_t chunk[RX_CHUNK];
        while (c->running) {
            DWORD got = 0;
            if (!ReadFile(hin, chunk, sizeof(chunk), &got, NULL)) {
                Sleep(10);
                continue;
            }
            if (got == 0) {
                /* EOF: park; don't exit and don't block anyone else */
                Sleep(100);
                continue;
            }
            rx_push(c, chunk, (int)got);
        }
    }

    LOGI("HOSTIO", "%s rx thread exit", c->name);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Common channel startup                                                */
/* ------------------------------------------------------------------ */

static int ch_start_threads(hostio_ch_t *c, LPTHREAD_START_ROUTINE rx_fn)
{
    InitializeCriticalSection(&c->cs);
    ring_init(&c->rx, RX_RING_SIZE);
    ring_init(&c->tx, TX_RING_SIZE);
    c->rx_evt = CreateEventA(NULL, TRUE, FALSE, NULL);
    c->tx_evt = CreateEventA(NULL, TRUE, FALSE, NULL);

    c->running = 1;
    c->rx_thread = CreateThread(NULL, 0, rx_fn, c, 0, NULL);
    c->tx_thread = CreateThread(NULL, 0, tx_thread, c, 0, NULL);
    if (!c->rx_thread || !c->tx_thread) {
        LOGE("HOSTIO", "%s create thread failed", c->name);
        c->running = 0;
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public interface                                                      */
/* ------------------------------------------------------------------ */

int sim_hostio_open_com(int ch, const char *port, int baud)
{
    hostio_ch_t *c;
    char path[64];

    if (ch < 0 || ch >= SIM_HOSTIO_CH_MAX || port == NULL)
        return -1;
    c = &s_ch[ch];
    if (c->backend != SIM_HOSTIO_BE_NONE)
        return -1;

    snprintf(path, sizeof(path), "\\\\.\\%s", port);
    c->hcom = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                          0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (c->hcom == INVALID_HANDLE_VALUE) {
        LOGE("HOSTIO", "open %s failed (err=%lu): check VSPE pair exists and port is free",
             port, (unsigned long)GetLastError());
        c->hcom = NULL;
        return -1;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    GetCommState(c->hcom, &dcb);
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    SetCommState(c->hcom, &dcb);

    COMMTIMEOUTS to;
    memset(&to, 0, sizeof(to));
    to.ReadIntervalTimeout = MAXDWORD;
    to.WriteTotalTimeoutConstant = 1000;
    to.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(c->hcom, &to);

    SetupComm(c->hcom, 4096, 4096);
    PurgeComm(c->hcom, PURGE_RXCLEAR | PURGE_TXCLEAR);

    snprintf(c->name, sizeof(c->name), "%s", port);
    c->backend = SIM_HOSTIO_BE_COM;

    if (ch_start_threads(c, rx_thread_com) != 0) {
        CloseHandle(c->hcom);
        c->hcom = NULL;
        c->backend = SIM_HOSTIO_BE_NONE;
        return -1;
    }

    LOGI("HOSTIO", "%s opened, baud=%d", port, baud);
    return 0;
}

int sim_hostio_open_stdio(int ch)
{
    hostio_ch_t *c;

    if (ch < 0 || ch >= SIM_HOSTIO_CH_MAX)
        return -1;
    c = &s_ch[ch];
    if (c->backend != SIM_HOSTIO_BE_NONE)
        return -1;

    if (InterlockedCompareExchange(&s_stdio_used, 1, 0) == 1) {
        LOGE("HOSTIO", "stdio backend already used by another channel");
        return -1;
    }

    snprintf(c->name, sizeof(c->name), "stdio");
    c->hcom = NULL;
    c->backend = SIM_HOSTIO_BE_STDIO;

    if (ch_start_threads(c, rx_thread_stdio) != 0) {
        c->backend = SIM_HOSTIO_BE_NONE;
        InterlockedExchange(&s_stdio_used, 0);
        return -1;
    }

    LOGI("HOSTIO", "channel %d bound to console (stdin/stdout)", ch);
    return 0;
}

int sim_hostio_read(int ch, void *buf, int max)
{
    hostio_ch_t *c;
    int n;

    if (ch < 0 || ch >= SIM_HOSTIO_CH_MAX || buf == NULL || max <= 0)
        return 0;
    c = &s_ch[ch];
    if (c->backend == SIM_HOSTIO_BE_NONE)
        return 0;

    EnterCriticalSection(&c->cs);
    n = ring_read(&c->rx, (uint8_t *)buf, max);
    LeaveCriticalSection(&c->cs);
    return n;
}

void sim_hostio_tx(int ch, const void *data, int len)
{
    hostio_ch_t *c;
    int written;

    if (ch < 0 || ch >= SIM_HOSTIO_CH_MAX || data == NULL || len <= 0)
        return;
    c = &s_ch[ch];
    if (c->backend == SIM_HOSTIO_BE_NONE)
        return;

    EnterCriticalSection(&c->cs);
    written = ring_write(&c->tx, (const uint8_t *)data, len);
    LeaveCriticalSection(&c->cs);

    if (written < len)
        LOGE("HOSTIO", "%s tx ring full, dropped %d bytes", c->name, len - written);

    SetEvent(c->tx_evt);
}

int sim_hostio_backend(int ch)
{
    if (ch < 0 || ch >= SIM_HOSTIO_CH_MAX)
        return SIM_HOSTIO_BE_NONE;
    return s_ch[ch].backend;
}
