/*
 * sim_main.c -- common host simulation startup skeleton (shared by all
 * products in the SDK family)
 *
 * Extracted from the original sim_xy4101_main.c: CLI parsing, crash/exit
 * diagnostics, log initialization, three-channel (USB AT / MODEM / LPUART)
 * backend binding, routing table printout, kernel start -- completely
 * identical for any chip product of the same family. All product
 * differences converge into the g_sim_target hook table (see sim_main.h):
 * banner text + boot_task.
 *
 * Usage:
 *   xysim.exe                            # USB AT port on this console (selftest/boot smoke)
 *   xysim.exe --at-com COM5              # USB AT port on VSPE virtual serial COM5
 *   xysim.exe --at-com COM5 --modem-com COM6 --lpuart-com COM7 --baud 921600
 *   All logs go to stderr (stdout = pure AT data stream); use --logfile <path>
 *   to save logs. For interactive AT debugging do not use the console (no
 *   local echo, logs mixed in the same window); use --at-com + a serial
 *   tool. The console channel is meant for pipe selftest and boot smoke
 *   checks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
/* The ERROR macro in wingdi.h may clash with SDK enum names; discard after use */
#ifdef ERROR
#undef ERROR
#endif

#include "xy_system.h"          /* cmsis_os2 + xy_printf + Sys_Assert */

#include "sim_hostio.h"
#include "sim_tty_device.h"
#include "sim_log.h"
#include "sim_pcap.h"
#include "sim_main.h"

/* --pcap capture file path (empty = no capture). Parsed by main; boot_task
 * fetches it via sim_main_pcap_path() and opens the file.
 * Without a path argument it defaults to xysim_capture.pcap next to the exe
 * (usually out/) */
static char g_pcap_path[MAX_PATH];

/* --logfile mirror file path (empty = no mirror). Parsed by main, opened
 * right after sim_log initialization. Not the shell's 2> redirection --
 * the mirror must coexist with console output, and redirection can only
 * pick one */
static char g_logfile_path[MAX_PATH];

const char *sim_main_pcap_path(void)
{
    return g_pcap_path;
}

/* ------------------------------------------------------------------ */
/* Exit diagnostics: a crash/exit must leave a trace.                  */
/*   - unhandled exception (out-of-bounds access etc.) -> one line of   */
/*     exception info + function backtrace                             */
/*   - normal exit (who called exit/return) -> atexit prints a trace    */
/* ------------------------------------------------------------------ */

/* Print backtrace: kernel32's RtlCaptureStackBackTrace (no extra library
 * needed under mingw) */
static void sim_print_backtrace(void)
{
    typedef USHORT (WINAPI *pfn_rtl_capture)(ULONG, ULONG, PVOID *, PULONG);
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    pfn_rtl_capture cap;
    PVOID frames[16];
    USHORT n;
    USHORT j;

    if (k32 == NULL)
        return;
    cap = (pfn_rtl_capture)(void *)GetProcAddress(k32, "RtlCaptureStackBackTrace");
    if (cap == NULL)
        return;

    n = cap(0, 16, frames, NULL);
    fprintf(stderr, "[CRASH] backtrace (%u frames):\n", (unsigned)n);
    for (j = 0; j < n; j++)
        fprintf(stderr, "  #%u %p\n", (unsigned)j, frames[j]);
}

static LONG WINAPI sim_crash_filter(EXCEPTION_POINTERS *ep)
{
    PCONTEXT ctx = ep->ContextRecord;

    fprintf(stderr, "\n[CRASH] unhandled exception code=0x%08lX at address %p (thread %lu)\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress,
            (unsigned long)GetCurrentThreadId());

    if (ctx) {
        fprintf(stderr, "[CRASH] registers:\n");
        fprintf(stderr, "  EIP=%p  ESP=%p  EBP=%p\n",
                (void*)(uintptr_t)ctx->Eip,
                (void*)(uintptr_t)ctx->Esp,
                (void*)(uintptr_t)ctx->Ebp);
        fprintf(stderr, "  EAX=%08lX  EBX=%08lX  ECX=%08lX  EDX=%08lX\n",
                (unsigned long)ctx->Eax, (unsigned long)ctx->Ebx,
                (unsigned long)ctx->Ecx, (unsigned long)ctx->Edx);
        fprintf(stderr, "  ESI=%08lX  EDI=%08lX\n",
                (unsigned long)ctx->Esi, (unsigned long)ctx->Edi);
    }
    fflush(stderr);
    sim_print_backtrace();
    fflush(stderr);

    return EXCEPTION_EXECUTE_HANDLER;
}

static void sim_exit_trace(void)
{
    fprintf(stderr, "\n[EXIT] process is exiting (normal termination)\n");
    fflush(stderr);
}

/* ------------------------------------------------------------------ */
/* Command line                                                        */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "  --at-com <port>      USB AT channel via VSPE virtual COM (e.g. COM5)\n");
    fprintf(stderr, "  --modem-com <port>   MODEM (PPP dial) channel via VSPE virtual COM\n");
    fprintf(stderr, "  --lpuart-com <port>  LPUART AT channel via VSPE virtual COM\n");
    fprintf(stderr, "  --baud <rate>        baud rate for any opened COM (default 115200)\n");
    fprintf(stderr, "  --pcap [file]        capture UL+DL IP packets to a pcap file\n");
    fprintf(stderr, "                       (Wireshark opens it directly; default file:\n");
    fprintf(stderr, "                       xysim_capture.pcap next to the exe)\n");
    fprintf(stderr, "  --logfile [file]     mirror logs into a file (still shown on screen;\n");
    fprintf(stderr, "                       default file: xysim.log next to the exe)\n");
    fprintf(stderr, "  Defaults: USB AT via console (no echo; use --at-com + serial tool for interactive).\n");
    fprintf(stderr, "  Other channels need --*-com. Each channel needs its own VSPE pair (no sharing).\n");
    fprintf(stderr, "  Logs to stderr, AT data to stdout. Save logs with --logfile [file].\n");
    fprintf(stderr, "  Data plane: host socket proxy. PDP cid=1 auto-activated at boot.\n");
}

int sim_main(int argc, char **argv)
{
    const char *at_com = NULL;       /* USB AT channel */
    const char *modem_com = NULL;    /* MODEM(PPP) channel */
    const char *lpuart_com = NULL;   /* LPUART AT channel */
    int baud = 115200;
    int i;

    /* Install as early as possible: a crash/abnormal exit must leave a trace */
    SetUnhandledExceptionFilter(sim_crash_filter);
    atexit(sim_exit_trace);
    atexit(sim_pcap_close);
    atexit(sim_log_close_mirror);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--at-com") == 0 && i + 1 < argc) {
            at_com = argv[++i];
        } else if (strcmp(argv[i], "--modem-com") == 0 && i + 1 < argc) {
            modem_com = argv[++i];
        } else if (strcmp(argv[i], "--lpuart-com") == 0 && i + 1 < argc) {
            lpuart_com = argv[++i];
        } else if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
            baud = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--pcap") == 0) {
            /* Capture to file (uplink+downlink merged, Wireshark reads it
             * directly). With a path use the path; without, default to
             * xysim_capture.pcap next to the exe */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                snprintf(g_pcap_path, sizeof(g_pcap_path), "%s", argv[++i]);
            } else {
                char exe[MAX_PATH];
                char *slash;

                if (GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0)
                    exe[0] = '\0';
                slash = strrchr(exe, '\\');
                if (slash != NULL)
                    slash[1] = '\0';
                snprintf(g_pcap_path, sizeof(g_pcap_path),
                         "%sxysim_capture.pcap", exe);
            }
            /* Normalize to an absolute path: log/capture files land where
             * the caller can see them, decoupled from the current working
             * directory */
            {
                char abs[MAX_PATH];

                if (GetFullPathNameA(g_pcap_path, sizeof(abs), abs, NULL) != 0)
                    snprintf(g_pcap_path, sizeof(g_pcap_path), "%s", abs);
            }
        } else if (strcmp(argv[i], "--logfile") == 0) {
            /* Log mirror: logs still print to the window as usual, and a
             * copy goes into this file. With a path use the path; without,
             * default to xysim.log next to the exe */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                snprintf(g_logfile_path, sizeof(g_logfile_path), "%s", argv[++i]);
            } else {
                char exe[MAX_PATH];
                char *slash;

                if (GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0)
                    exe[0] = '\0';
                slash = strrchr(exe, '\\');
                if (slash != NULL)
                    slash[1] = '\0';
                snprintf(g_logfile_path, sizeof(g_logfile_path),
                         "%sxysim.log", exe);
            }
            /* Same as --pcap: normalize to an absolute path */
            {
                char abs[MAX_PATH];

                if (GetFullPathNameA(g_logfile_path, sizeof(abs), abs, NULL) != 0)
                    snprintf(g_logfile_path, sizeof(g_logfile_path), "%s", abs);
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* The same COM port cannot back two channels at once (the VSPE pair
     * would fight over bytes). COM numbers are case-insensitive, use
     * _stricmp for duplicate detection */
    {
        const char *ports[3] = { at_com, modem_com, lpuart_com };
        const char *names[3] = { "USB AT", "MODEM", "LPUART" };
        int a, b;

        for (a = 0; a < 3; a++)
            for (b = a + 1; b < 3; b++)
                if (ports[a] != NULL && ports[b] != NULL &&
                    _stricmp(ports[a], ports[b]) == 0) {
                    fprintf(stderr,
                            "FATAL: %s and %s channels both want %s; "
                            "each channel needs its own VSPE port\n",
                            names[a], names[b], ports[a]);
                    return 1;
                }
    }

    /* sim_log first: the mirror file must open before any log is produced */
    sim_log_init();

    if (g_logfile_path[0] != '\0') {
        if (sim_log_open_mirror(g_logfile_path) != 0)
            fprintf(stderr, "WARNING: cannot open --logfile for mirror: %s\n",
                    g_logfile_path);
    }

    /* Startup banner goes to stderr (stdout carries only the AT data
     * stream). Product text comes from the target hook table; no build
     * timestamp (__DATE__/__TIME__ freeze and mislead -- always judge
     * build age by file mtime/size) */
    fprintf(stderr, "=====================================================\n");
    fprintf(stderr, "  %s\n", g_sim_target.product);
    fprintf(stderr, "  %s\n", g_sim_target.byline);
    fprintf(stderr, "  FreeRTOS %s + cmsis_os2\n", tskKERNEL_VERSION_NUMBER);
    if (g_sim_target.coverage[0] != NULL)
        fprintf(stderr, "  %s\n", g_sim_target.coverage[0]);
    if (g_sim_target.coverage[1] != NULL)
        fprintf(stderr, "  %s\n", g_sim_target.coverage[1]);
    fprintf(stderr, "  Data plane: fake CP + userspace NAT (no ICS/admin)\n");
    fprintf(stderr, "=====================================================\n");
    fflush(stderr);

    /* USB AT channel backend: this console by default, or the VSPE virtual
     * serial port given by --at-com */
    if (at_com != NULL) {
        if (sim_hostio_open_com(SIM_HOSTIO_CH_USB, at_com, baud) != 0) {
            fprintf(stderr, "FATAL: cannot open %s for USB AT channel\n", at_com);
            return 1;
        }
    } else {
        if (sim_hostio_open_stdio(SIM_HOSTIO_CH_USB) != 0) {
            fprintf(stderr, "FATAL: cannot bind console to USB AT channel\n");
            return 1;
        }
    }

    /* MODEM(PPP) channel backend: none by default (device present but
     * receives no data), enabled with --modem-com */
    if (modem_com != NULL) {
        if (sim_hostio_open_com(SIM_HOSTIO_CH_MODEM, modem_com, baud) != 0) {
            fprintf(stderr, "FATAL: cannot open %s for MODEM channel\n", modem_com);
            return 1;
        }
    } else {
        sim_tty_set_backend(SIM_TTY_MODEM, -1);
    }

    /* LPUART channel backend: none by default, enabled with --lpuart-com */
    if (lpuart_com != NULL) {
        if (sim_hostio_open_com(SIM_HOSTIO_CH_LPUART, lpuart_com, baud) != 0) {
            fprintf(stderr, "FATAL: cannot open %s for LPUART channel\n", lpuart_com);
            return 1;
        }
    } else {
        sim_tty_set_backend(SIM_TTY_LPUART, -1);
    }

    fprintf(stderr, "DIAG: all channels opened OK\n");
    fflush(stderr);

    /* Three-channel routing table (to stderr, telling the user where each
     * port is connected) */
    /* stdin type: PIPE/DISK = pipe/redirect selftest; anything else (CHAR
     * real console, or the edge case of a missing handle) prompts as an
     * interactive console */
    DWORD in_type = GetFileType(GetStdHandle(STD_INPUT_HANDLE));

    if (at_com != NULL) {
        fprintf(stderr, "USB AT channel   : %s @ %d (use your serial tool on the paired VSPE port)\n",
                at_com, baud);
    } else if (in_type == FILE_TYPE_PIPE || in_type == FILE_TYPE_DISK) {
        fprintf(stderr, "USB AT channel   : this console (pipe/redirect selftest)\n");
    } else {
        /* Interactive console (including the missing-handle edge case):
         * typing keys here gets no local echo -- ReadConsoleInput bypasses
         * the console's echoing read, factory NV has ate=0, and the SDK's
         * ATE echo only replays a whole line after receiving \r. Blind
         * typing is a trap; explicitly steer users to --at-com + a serial
         * tool */
        fprintf(stderr, "USB AT channel   : this console (interactive)\n");
        fprintf(stderr, "                   NOTE: typed keys are NOT echoed here, and logs share\n");
        fprintf(stderr, "                   this window. For interactive AT use --at-com <port>\n");
        fprintf(stderr, "                   plus a serial tool with local echo. Console mode is\n");
        fprintf(stderr, "                   meant for pipe selftest (xysim < in.txt > out.txt)\n");
        fprintf(stderr, "                   and boot smoke checks only.\n");
    }
    if (modem_com != NULL)
        fprintf(stderr, "MODEM channel    : %s @ %d (PPP dial rides this port)\n",
                modem_com, baud);
    else
        fprintf(stderr, "MODEM channel    : (no backend; pass --modem-com to enable)\n");
    if (lpuart_com != NULL)
        fprintf(stderr, "LPUART channel   : %s @ %d (use your serial tool on the paired VSPE port)\n",
                lpuart_com, baud);
    else
        fprintf(stderr, "LPUART channel   : (no backend; pass --lpuart-com to enable)\n");
    fprintf(stderr, "pcap capture     : %s\n",
            g_pcap_path[0] != '\0' ? g_pcap_path : "(off; pass --pcap to enable)");
    fprintf(stderr, "log mirror       : %s\n",
            g_logfile_path[0] != '\0' ? g_logfile_path : "(off; pass --logfile [file] to enable)");
    fprintf(stderr, "-----------------------------------------------------\n");
    fflush(stderr);

    fprintf(stderr, "DIAG: entering osKernelInitialize...\n");
    fflush(stderr);

    /* Factory NV chip overlay: the target's factory_nv_init callback
     * injects chip-specific pins here, ensuring osKernelInitialize ->
     * boot_task -> at_init() reads the complete NV */
    if (g_sim_target.factory_nv_init != NULL) {
        extern softap_fac_nv_t *g_softap_fac_nv;
        g_sim_target.factory_nv_init(g_softap_fac_nv);
    }

    Sys_Assert(osKernelInitialize() == osOK);
    fprintf(stderr, "DIAG: osKernelInitialize OK\n");
    fflush(stderr);

    osThreadAttr_t boot_attr;
    memset(&boot_attr, 0, sizeof(boot_attr));
    boot_attr.name = "simboot";
    boot_attr.priority = osPriorityNormal;
    boot_attr.stack_size = 8192;
    Sys_Assert(osThreadNew(g_sim_target.boot_task, NULL, &boot_attr) != NULL);
    fprintf(stderr, "DIAG: osThreadNew OK\n");
    fflush(stderr);

    /* Does not return: from here the main thread carries the host-side
     * scheduler entry (MSVC-MingW port model) */
    fprintf(stderr, "DIAG: entering osKernelStart...\n");
    fflush(stderr);
    Sys_Assert(osKernelStart() == osOK);

    return 0;
}
