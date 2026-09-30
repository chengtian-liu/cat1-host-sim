/*
 * sim_log.c -- simulator logging (plan B: all logs go to stderr, stdout carries only the AT data stream)
 *
 * Asynchronous architecture (2026-09-04 refactor, cause = QFTPLIST whole-machine freeze):
 *   Producer side (any thread, mostly SDK tasks): format one log line -> push into a
 *   bounded ring queue (lock held only briefly; when the queue is full the record is
 *   dropped and counted, never blocks);
 *   Consumer side (host writer thread): the only thread in the whole process touching
 *   the console / mirror file; handles coloring, writing to the window and to the
 *   --logfile mirror.
 *
 * Why it must be asynchronous: writing to the console (WriteConsole) is not always
 * non-blocking -- when the console window is in QuickEdit selection mode (mouse
 * click/drag selecting text), WriteConsole blocks indefinitely. In the old
 * implementation SDK threads did fprintf(stderr)+fflush directly while holding s_cs:
 * once stuck, that thread (e.g. the sockMgr loop) would be pinned in xy_printf of
 * ftp_default.c, all other threads queued up waiting for the lock, the hostio tx
 * thread's WriteFile(stdout) to the same console stalled too, logs and the AT data
 * stream froze at the same time -> the whole machine looked hung (observed on site:
 * logs stopped at "[atDefFtpDataMgrEventCallback]recvd eventType:1", no output at
 * all after CONNECT). The fix = the asynchronization in this file: no matter why the
 * console stalls, SDK threads never wait on it -- worst case some logs are dropped
 * and the dropped count is reported once output resumes; the business logic keeps
 * running. We once also disabled QuickEdit to remove the trigger, but users mainly
 * test with a VSPE serial tool (--at-com, AT data goes over the COM port, not the
 * console), so selection mode cannot stall the business logic; mouse selection was
 * therefore restored (2026-09-04). Note: in the argument-less console self-test mode
 * selection mode still freezes AT output (hostio tx writes stdout synchronously);
 * that is an inherent limitation of that mode -- don't click the window while
 * self-testing.
 *
 * Console coloring: only the warning/error levels, via Win32 SetConsoleTextAttribute
 * (no ANSI escape sequences); color decision/application happens only in the writer
 * thread. When stderr is redirected to a file/pipe, GetConsoleScreenBufferInfo fails
 * and coloring is skipped automatically, keeping the output file clean. The mirror
 * file is written before the console: it almost never blocks, so even if the console
 * hangs, the last log line at the hang point stays in --logfile (useful for
 * troubleshooting).
 */
#include "sim_log.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

static int s_level = LOG_LVL_INF;

/* ------------------------------------------------------------------ */
/* Ring log queue (producers push under lock / writer thread pops under lock) */
/* ------------------------------------------------------------------ */

#define SIM_LOG_NO_COLOR   0xFFFFu
#define SIM_LOG_BODY_MAX   1024                      /* same cap as the old body[] */
#define SIM_LOG_LINE_MAX   (SIM_LOG_BODY_MAX + 96)   /* room for timestamp/level/module decoration */
#define SIM_LOG_RING_NUM   128

typedef struct {
    WORD attr;                         /* SIM_LOG_NO_COLOR = no coloring */
    int  len;
    char text[SIM_LOG_LINE_MAX];
} sim_log_rec_t;

static CRITICAL_SECTION s_cs;
static int s_cs_init = 0;

/* Log mirror (--logfile): when non-NULL, every log record is additionally written
 * to this file. Console output is unaffected (the mirror is a "copy", not a
 * redirect) */
static FILE *s_mirror = NULL;

static sim_log_rec_t s_ring[SIM_LOG_RING_NUM];
static int s_ring_head;       /* write index */
static int s_ring_tail;       /* read index */
static int s_ring_count;
static uint32_t s_dropped;    /* cumulative drops on full queue, reported once output resumes */

static HANDLE s_evt;          /* records available to pop */
static HANDLE s_thread;       /* writer thread */
static volatile LONG s_running;
static int s_async = 0;       /* 1 = use the async ring (only after successful init) */

/* ------------------------------------------------------------------ */
/* Small helpers                                                         */
/* ------------------------------------------------------------------ */

static const char *lvl_str(int lvl)
{
    switch (lvl) {
    case LOG_LVL_DBG: return "debug";
    case LOG_LVL_INF: return "info";
    case LOG_LVL_WRN: return "warn";
    default:          return "error";
    }
}

static WORD lvl_color(int lvl)
{
    switch (lvl) {
    case LOG_LVL_WRN:   /* yellow */
        return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case LOG_LVL_ERR:   /* red */
        return FOREGROUND_RED | FOREGROUND_INTENSITY;
    default:
        return SIM_LOG_NO_COLOR;
    }
}

/* ------------------------------------------------------------------ */
/* writer thread: the only thread in the process writing console/mirror   */
/* ------------------------------------------------------------------ */

/* Emit one record: mirror first (almost never blocks, guarantees a trace),
 * then console (with coloring) */
static void emit_rec(const sim_log_rec_t *rec, FILE *mirror)
{
    HANDLE hcon;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    WORD saved_attr = 0;
    int colored = 0;

    if (mirror != NULL) {
        fwrite(rec->text, 1, rec->len, mirror);
        fflush(mirror);
    }

    if (rec->attr != SIM_LOG_NO_COLOR &&
        (hcon = GetStdHandle(STD_ERROR_HANDLE)) != NULL &&
        hcon != INVALID_HANDLE_VALUE &&
        GetConsoleScreenBufferInfo(hcon, &csbi)) {
        saved_attr = csbi.wAttributes;
        colored = 1;
        SetConsoleTextAttribute(hcon, rec->attr);
    }

    fwrite(rec->text, 1, rec->len, stderr);
    fflush(stderr);

    if (colored)
        SetConsoleTextAttribute(hcon, saved_attr);   /* restore color after flushing */
}

static void drain_ring(void)
{
    for (;;) {
        sim_log_rec_t rec;
        FILE *mirror;
        uint32_t dropped;
        int have;

        EnterCriticalSection(&s_cs);
        have = (s_ring_count > 0);
        if (have) {
            memcpy(&rec, &s_ring[s_ring_tail], sizeof(rec));
            s_ring_tail = (s_ring_tail + 1) % SIM_LOG_RING_NUM;
            s_ring_count--;
        }
        mirror  = s_mirror;
        dropped = s_dropped;
        s_dropped = 0;
        LeaveCriticalSection(&s_cs);

        if (!have)
            return;

        if (dropped != 0) {
            char note[96];
            int n = snprintf(note, sizeof(note),
                    "[sim_log] %lu log record(s) dropped while console was blocked/slow\n",
                    (unsigned long)dropped);

            if (mirror != NULL) {
                fwrite(note, 1, n, mirror);
                fflush(mirror);
            }
            fwrite(note, 1, n, stderr);
            fflush(stderr);
        }

        emit_rec(&rec, mirror);
    }
}

static DWORD WINAPI log_writer_thread(LPVOID arg)
{
    (void)arg;

    for (;;) {
        if (WaitForSingleObject(s_evt, 200) == WAIT_OBJECT_0)
            ResetEvent(s_evt);    /* manual-reset event: reset before draining to avoid spinning */

        drain_ring();

        if (!s_running)
            break;                /* drained before exit (producers stopped or turned synchronous) */
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                             */
/* ------------------------------------------------------------------ */

void sim_log_init(void)
{
    InitializeCriticalSection(&s_cs);
    s_cs_init = 1;

    /* Output UTF-8 to the console to avoid garbled non-ASCII text */
    SetConsoleOutputCP(65001);

    /* Don't disable QuickEdit: users mainly test with a VSPE serial tool
     * (--at-com), AT data goes over the COM port, not the console, so the
     * console entering "selection" mode cannot stall the business logic; the
     * logging itself is already asynchronous, so being selection-paused costs
     * at most some dropped logs. Only in the "argument-less console self-test"
     * mode does selection mode still freeze AT output (hostio tx writes stdout
     * synchronously) -- an inherent limitation of that mode; don't click the
     * window with the mouse while self-testing (decided 2026-09-04; it had been
     * wrongly disabled before) */

    /* writer thread: from here on SDK threads only push to the ring and never
     * touch the console. If creation fails, fall back to synchronous direct
     * output (old behavior); no functionality lost */
    s_evt = CreateEventA(NULL, TRUE, FALSE, NULL);
    s_running = 1;
    s_thread = CreateThread(NULL, 0, log_writer_thread, NULL, 0, NULL);
    if (s_evt != NULL && s_thread != NULL) {
        s_async = 1;
        /* atexit is LIFO: sim_log_close_mirror registered earlier in main runs
         * later, i.e. the writer is stopped first (draining leftovers into the
         * mirror) and the mirror file is closed afterwards */
        atexit(sim_log_shutdown);
    } else {
        s_running = 0;
        if (s_evt != NULL) {
            CloseHandle(s_evt);
            s_evt = NULL;
        }
        s_thread = NULL;
    }
}

/* Stop the writer thread and drain leftovers (called automatically via atexit; idempotent) */
void sim_log_shutdown(void)
{
    if (s_thread == NULL)
        return;

    s_async = 0;                       /* other callers fall back to synchronous direct output */
    InterlockedExchange(&s_running, 0);
    SetEvent(s_evt);
    WaitForSingleObject(s_thread, 3000);   /* bounded wait for draining; don't drag out exit */
    CloseHandle(s_thread);
    s_thread = NULL;
    if (s_evt != NULL) {
        CloseHandle(s_evt);
        s_evt = NULL;
    }
}

void sim_log_set_level(int lvl)
{
    s_level = lvl;
}

int sim_log_open_mirror(const char *path)
{
    FILE *f;

    if (path == NULL || path[0] == '\0')
        return -1;

    /* Overwrite the old file (same semantics as 2>); binary vs text is
     * irrelevant, text mode is fine */
    f = fopen(path, "w");
    if (f == NULL)
        return -1;

    if (s_cs_init)
        EnterCriticalSection(&s_cs);
    if (s_mirror != NULL)
        fclose(s_mirror);
    s_mirror = f;
    if (s_cs_init)
        LeaveCriticalSection(&s_cs);

    return 0;
}

void sim_log_close_mirror(void)
{
    if (s_cs_init)
        EnterCriticalSection(&s_cs);
    if (s_mirror != NULL) {
        fclose(s_mirror);
        s_mirror = NULL;
    }
    if (s_cs_init)
        LeaveCriticalSection(&s_cs);
}

/* ------------------------------------------------------------------ */
/* Producer-side entry (any thread)                                      */
/* ------------------------------------------------------------------ */

void sim_log_print(int lvl, const char *tag, const char *fmt, ...)
{
    char body[SIM_LOG_BODY_MAX];
    va_list ap;
    SYSTEMTIME st;
    WORD attr;

    if (lvl < s_level) {
        return;
    }

    /* Format the body once (single evaluation); destination is the async ring
     * or the direct-output fallback */
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    attr = lvl_color(lvl);
    GetLocalTime(&st);

    if (!s_async) {
        /* Before init / after shutdown: direct output (the process is basically
         * single-threaded then, no risk of the console stalling) */
        fprintf(stderr, "[%02d:%02d:%02d.%03d][%-5s][%-12s] %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                lvl_str(lvl), tag, body);
        fflush(stderr);
        return;
    }

    /* Push to ring: hold the lock briefly for one snprintf; on full queue drop
     * and count, never block the caller -- even if the console hangs, SDK
     * threads keep running, only logs from that period are dropped */
    {
        int full;

        EnterCriticalSection(&s_cs);
        full = (s_ring_count >= SIM_LOG_RING_NUM);
        if (!full) {
            sim_log_rec_t *rec = &s_ring[s_ring_head];

            rec->attr = attr;
            rec->len = snprintf(rec->text, sizeof(rec->text),
                                "[%02d:%02d:%02d.%03d][%-5s][%-12s] %s\n",
                                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                                lvl_str(lvl), tag, body);
            s_ring_head = (s_ring_head + 1) % SIM_LOG_RING_NUM;
            s_ring_count++;
        } else {
            s_dropped++;
        }
        LeaveCriticalSection(&s_cs);
    }

    SetEvent(s_evt);
}
