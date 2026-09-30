# CAT1 Host Sim

[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey.svg)](#quick-start)
[![Build](https://img.shields.io/badge/build-CMake%20%2B%20Ninja-064F8C.svg)](#build)
[![Target](https://img.shields.io/badge/target-Xinyi%20XY4101-blue.svg)](#adding-a-new-chip-product)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](#license)

*LTE Cat.1 module SDK host simulator — run, AT-test and single-step debug module firmware on Windows, with no module hardware.*

The module SDK source (RTOS kernel / AT framework / lwIP stack / application code) is **referenced read-only and compiled as-is** into a single Windows executable, `out\xysim.exe`; all hardware-facing parts (registers / serial ports / RF / flash / RTC) are bridged by the shim layer. The current target is the Xinyi XY4101, and the generic parts are architecturally separated from the chip product — adding a new chip only requires adding a `target-xxx/` directory.

> **SDK required to build.** This project is the host simulation *shell* around the SDK; it **does not contain or ship the SDK source**. Building requires the matching LTE Cat.1 module SDK (AP-side source tree) on the local machine; without it `xysim.exe` cannot be compiled — this README can still be read as an architecture reference. To obtain the SDK, contact Xinyi Information Technology sales/technical support.

**Contents:** [Features](#features) · [Quick Start](#quick-start) · [How It Works](#how-it-works) · [Architecture](#architecture-three-layer-directory-layout) · [Build](#build) · [Running & Self-Test](#running-and-self-test) · [gdb Debugging](#gdb-single-step-debugging) · [Serial Tool Testing](#serial-tool-testing-virtual-serial-port-pair) · [Verified AT Commands](#verified-at-commands-excerpt) · [New Chip Target](#adding-a-new-chip-product) · [Toolchain Appendix](#appendix-toolchain-setup-notes) · [License](#license)

## Features

| Feature | Description |
|---|---|
| **Real SDK RTOS kernel** | Business code runs on the stock FreeRTOS (official Windows port) — tasks are Windows threads, not a simulated OS |
| **Stock AT framework** | AT commands are parsed / routed / answered by the unmodified SDK AT framework; HTTP / FTP / MQTT / TLS / file-system commands work exactly as on the real device |
| **Fake CP — "on network" at boot** | Commands missing from the AP command table are forwarded to the virtual PS and answered by a simulated CP (`sim_vps_net`): CPIN READY / CREG 0,1 / CSQ 31,99; **cid=1 PDP auto-activates at boot** (equivalent to `AT+CGACT=1`) |
| **Real internet, zero setup** | In-process user-space NAT proxy (TCP / UDP five-tuple relay / ICMP ping / DNS discovery) exits via ordinary host sockets — **no ICS, no drivers, no administrator** |
| **Virtual serial ports** | AT / MODEM (PPP dial-up) / LPUART channels each map to a VSPE or com0com pair; Windows modem dialing supported |
| **gdb single-step debugging** | Debug build (-g, no optimization); live FreeRTOS task-table inspection via `vTaskList` |
| **pcap capture & log shim** | `--pcap` taps uplink/downlink IP packets; `xy_printf` / `user_printf` land on the plain-text colored console — no Logview tool needed |
| **Multi-chip architecture** | Three-layer design (generic core + SDK common + chip target); a new product is just a new `target-xxx/` directory |

**What's real, what's simulated:**

| Component | Real device | Simulator |
|---|---|---|
| RTOS kernel | FreeRTOS on chip | ✅ Same FreeRTOS — official Windows port |
| AT framework | Stock SDK | ✅ Same code, unmodified |
| lwIP stack / application code | Stock SDK | ✅ Compiled as-is |
| CP core (3GPP / RF stack) | Real baseband | 🔁 Fake CP (`sim_vps_net`) — not simulated |
| Data egress to network | Via CP core | 🔁 User-space proxy → host sockets |
| Serial / USB ports | Hardware | 🔁 VSPE / com0com virtual serial pairs |
| Flash | Hardware | 🔁 Disk image (littlefs backend) |
| Registers / RF / RTC | Hardware | 🔁 Shim layer |

## Quick Start

**Requirements:** the LTE Cat.1 module SDK source tree (see note above, expected at `../LTEcat1_SDK_AP` by default), plus a 32-bit MinGW gcc + CMake + Ninja toolchain (setup notes in the [Appendix](#appendix-toolchain-setup-notes)).

```bat
build.bat          :: configure + build Release -> out\xysim.exe
out\xysim.exe      :: AT port goes to this console - type AT commands directly
```

Console self-test right away:

```text
AT            -> OK
ATI           -> XINYI / XY4101PC / <firmware version string>
AT+CSQ        -> +CSQ: 31,99
AT+CGPADDR    -> 10.0.0.2   (network up as soon as power is on)
```

Full build options: [Build](#build). Running with virtual serial ports: [Running and Self-Test](#running-and-self-test).

## How It Works

- Business code runs on the **real SDK RTOS kernel** (tasks = Windows threads, official Windows port), not a simulated OS
- AT commands are parsed/routed/answered for real by the **stock SDK AT framework**; commands that miss the AP command table are forwarded to the virtual PS and answered by the **fake CP** played by the simulator (`sim-sdk-common/src/sim_vps_net.c`) — "registered on the network" right from boot (CPIN READY / CREG 0,1 / CSQ 31,99)
- **Auto PDP activation of cid=1 at boot** (equivalent to automatically sending `AT+CGACT=1`); the network is up as soon as power is on
- Data plane: **in-process user-space proxy** (`sim-core/src/sim_proxy*.c`) — IP packets from the lwip WAN netif (pure L3) terminate inside the process: static address assignment (modem=10.0.0.2), UDP five-tuple relay, ICMP ping, DNS discovery, TCP terminating proxy, going out to the public internet via ordinary host sockets. Application sockets really reach the internet; **no ICS, no drivers, no administrator**
- SDK log macros such as `xy_printf` / `user_printf` are redirected through the shim to plain-text console output; no Logview tool needed (see [Log Shim](#log-shim))
- The real-device 3GPP/RF stack is not simulated; the control plane is replaced by the fake CP

### AT Command Path

```text
┌──────────────────────────────────┐
│  Host terminal / serial tool /   │
│  script                          │
└─────────────────┬────────────────┘
                  │  COM pair (VSPE / com0com), read/write
                  ▼
┌──────────────────────────────────┐
│  Virtual serial port shim        │ ←── sim_hostio_read() /
│  (sim_tty_device.c)              │     sim_hostio_write()
└─────────────────┬────────────────┘
                  │  sim_tty_poll() — polled every 1 ms, bytes pushed
                  │  into the SDK AT framework RX buffer
                  ▼
┌──────────────────────────────────┐
│  SDK AT command task (unmodified)│
│  AT+QIOPEN / QPING / QNTP / QSSL │
│  +HTTP / +FTP / +MQTT / +CCLK... │
│  (at_cmd_regist_decl.h)          │
└─────────────────┬────────────────┘
                  │  miss in the AP command table → forwarded
                  ▼
┌──────────────────────────────────┐
│  sim_vps_net — fake CP           │ ←── replaces baseband /
│  +CPIN? → READY  +CREG? → 0,1    │     protocol stack
│  +CSQ → 31,99  +CGPADDR→10.0.0.2 │     (sim_vps_net.c)
└─────────────────┬────────────────┘
                  │  responses return along the same path
                  ▼
┌──────────────────────────────────┐
│  AT framework →                  │
│  sim_hostio_write() → COM →      │
│  host terminal                   │
└──────────────────────────────────┘
```

### PPP Dial-up and Data Plane

```text
┌────────────────────────────────────────────────────────┐
│  PPP dial-up (ATD*99#) — MODEM port                    │ ←── Windows modem dialing
│                                                        │     needs com0com (DTR/DSR
│                                                        │     handshake); a home-grown
│                                                        │     PPP client can use VSPE
└───────────┬─────────────────────────────┬──────────────┘
            │ Uplink: PPP frames          │ Downlink: PPP frames
            ▼                             ▲
┌───────────────────────┐   ┌────────────────────────────┐
│ lwIP PPP deframing →  │   │ SDK downlink → HDLC        │
│ fast-path routing     │   │ encoding → AT channel      │
│ (inside the SDK; the  │   │ (real-device path; the     │
│  simulation does not  │   │  simulation does not       │
│  interfere)           │   │  interfere)                │
└───────────┬───────────┘   └─────────────▲──────────────┘
            ▼                             │
┌───────────────────────┐   ┌────────────────────────────┐
│ Wedge point 1 — SDK   │   │ Wedge point 2 — cross-core │
│ AP→CP data egress,    │   │ data channel injection     │
│ replaced by the shim  │   │ API, replaced by the shim  │
│ (sim_net_shims.c)     │   │                            │
└───────────┬───────────┘   └─────────────▲──────────────┘
            ▼                             │
┌───────────────────────┐   ┌────────────────────────────┐
│ sim_proxy_uplink()    │   │ sim_proxy downlink ring    │
│ → host socket         │   │ buffer → proxy_rx task     │
└───────────┬───────────┘   └─────────────▲──────────────┘
            ▼                             │
┌────────────────────────────────────────────────────────┐
│               Internet (via host sockets)              │
└────────────────────────────────────────────────────────┘
```

> **Wedge point 1**: the real-device egress toward the CP core; the simulation replaces it with `sim_proxy_uplink()`, which goes out to the public internet via host sockets.
> **Wedge point 2**: after the host receives packets, they are injected back into the SDK downlink through the cross-core data channel shim and travel the real-device PPP HDLC encoding → AT channel path back to the terminal, transparent to the PPP terminal.

## Architecture (Three-Layer Directory Layout)

```text
cat1-host-sim/
 ├─ CMakeLists.txt          # top level: SDK_ROOT / SIM_TARGET selection, add_subdirectory per layer
 ├─ build.bat               # one-shot build (MSYS2 mingw32 + ninja, verified)
 │
 ├─ sim-core/               # layer 1: pure host core (zero SDK deps) → simcore static lib
 │   ├─ include/            #   sim_core_api.h (callback table) / sim_hostio.h /
 │   │                      #   sim_log.h / sim_proxy.h / sim_proxy_internal.h
 │   └─ src/                #   user-space NAT proxy (TCP/UDP/ICMP), host IO
 │                          #   threads (COM/console), async logging
 │
 ├─ sim-sdk-common/         # layer 2: SDK-family common (FreeRTOS+lwIP+AT shared)
 │   ├─ CMakeLists.txt      #   SDK library definitions: xyos / xyat / xynet / xymbed
 │   │                      #   + common shim lib simcommon (incl. generic startup skeleton sim_main)
 │   ├─ include/            #   override headers: FreeRTOSConfig.h / sim_main.h (hook table) /
 │   │                      #   sim_xylog.h (log shim) / sim_vps_net.h (fake CP) /
 │   │                      #   sim_tty_device.h / sim_pcap.h / memmap.h / ...
 │   ├─ include_host/       #   host-only overrides: hw_types.h (shadow registers) /
 │   │                      #   usb_api.h / at_cmd_regist_decl.h / lwip/inet.h / ...
 │   ├─ lwipopts.h          #   simulator-modified (shadows the stock SDK lwIP config)
 │   └─ src/                #   stubs: sim_main.c (generic skeleton) / sim_xylog.c (log shim) /
 │                          #   sim_vps_net.c (fake CP) / sim_shims.c / sim_flash.c /
 │                          #   sim_rtc_shims.c / sim_tty_device.c / sim_pcap.c /
 │                          #   sim_icc_shim.c / sim_net_shims.c / sim_at_shims.c /
 │                          #   sim_at_cmd_regist.c / sim_factory_nv.c / sim_sys_arch.c
 │
 ├─ target-xy4101/          # layer 3: chip-specific (copy this dir for new products)
 │   ├─ sim-target.cmake    #   chip-difference injection point (SIM_CHIP_MEMMAP etc.)
 │   ├─ CMakeLists.txt      #   xy4101shim lib + xysim executable (final linking)
 │   ├─ include/            #   csi_device.h (RISC-V CSR) / xy_prcm.h /
 │   │                      #   sim_xy4101_bridge.h / performance_monitor.h
 │   ├─ include_host/       #   USB low-level override headers
 │   │                      #   (xy4101_ll_usb_dev.h / xy4101_usb_udc.h)
 │   └─ src/                #   sim_xy4101_bridge.c (callback-table binding + proxy_rx task)
 │                          #   sim_xy4101_main.c (g_sim_target hook table + main forwarding)
 │
 ├─ third_party/            # FreeRTOS Windows port (MSVC-MingW + GCC/Posix)
 ├─ tools/                  # helper scripts and debug probes
 ├─ build/                  # Release build intermediates (*.a, xysim.map)
 ├─ build-dbg/              # Debug build intermediates (only after build.bat debug)
 └─ out/                    # Release deliverable (xysim.exe only); Debug build in out-dbg/
```

Layering rules (dependencies may only point downward):

| Layer | Library | Allowed dependencies |
|---|---|---|
| sim-core | `simcore` | Win32 API only; **includes no SDK/simulation headers** |
| sim-sdk-common | `xyos` `xyat` `xynet` `xymbed` `simcommon` | SDK source + simcore |
| target-xxx | `xy4101shim` + `xysim` | Everything from the two layers above |

Two narrow cross-layer interfaces:

1. **Callback table** (`sim-core/include/sim_core_api.h`): when simcore needs malloc/free/packet injection/delay from the RTOS world, it calls back through `sim_core_callbacks_t`, bound at startup by the target bridge layer (`xy4101_bridge_init()`) — simcore therefore has zero SDK dependencies;
2. **Hook table** (`sim-sdk-common/include/sim_main.h`): the generic startup skeleton `sim_main()` (CLI parsing, channel opening, kernel startup, boot thread creation) lives in simcommon; the target only defines `const sim_target_hooks_t g_sim_target` (product banner + boot task) and provides a `main()` that forwards in one line. Note that `main()` cannot be placed in a static library (pulling main from an archive is unreliable for the linker), which is why the skeleton entry is named `sim_main()`.

## Build

Prerequisite toolchain (32-bit MinGW gcc + cmake + ninja). `build.bat` searches in the following order:

1. Command-line arguments: `--mingw <mingw32\bin directory>` / `--ninja-dir <ninja directory>` / `--cmake-dir <cmake bin directory>`
2. Environment variables: `SIM_MINGW` / `SIM_NINJA` / `SIM_CMAKE`
3. Built-in defaults (dev-machine layout): `D:\msys64\mingw32\bin`, `D:\prebuilts\win64\{ninja, cmake\bin}`
4. If none of the above, gcc / ninja / cmake are auto-detected from `PATH`

```bat
build.bat          :: configure + build Release; output out\xysim.exe
build.bat debug    :: Debug build (-g, no optimization); output out-dbg\xysim.exe; does not affect Release
build.bat clean    :: clean all build outputs of both configurations (.o/.a/map/exe; keeps the configure cache)
```

**SDK path**: the build has a hard dependency on the matching SDK source tree (see [note at the top](#cat1-host-sim)); by default it lives at `../LTEcat1_SDK_AP` under the parent directory of the simulation project. When it is not at the default location, use `build.bat --sdk <SDK root directory>` (or cmake `-DSDK_ROOT=...`). If the SDK directory is missing or invalid, configure fails immediately with a usage hint.

**Target selection**: cmake `-DSIM_TARGET=target-xy4101` (the default).

When the toolchain is not at the default location (most common when switching machines):

```bat
build.bat --mingw C:/msys64/mingw32/bin --ninja-dir C:/tools --cmake-dir C:/tools/cmake/bin
rem or set the environment variables once:
set SIM_MINGW=C:\msys64\mingw32\bin
build.bat
```

Output: `out\xysim.exe` (PE32, i686 32-bit, statically linked). The link map is written to `build\xysim.map` (for crash address → symbol resolution); the Debug build is at `out-dbg\xysim.exe` (map at `build-dbg\xysim.map`), for gdb debugging only — see the [next section](#gdb-single-step-debugging).

> ⚠️ **Gotcha**: after adding/moving override headers (`sim-sdk-common/include*`, `target-xxx/include*`), a full rebuild via `build.bat clean` is mandatory — Ninja adds no dependency edges for new headers, so old .obj files are not recompiled.

> ⚠️ **Gotcha (shadow ordering)**: override headers beat the real SDK versions via -I ordering (e.g. `FreeRTOSConfig.h`, `hw_types.h`, `lwipopts.h`, `memmap.h`). In each library's `target_include_directories`, **the shadowing directories (target/include\*, sim-sdk-common root + include\*, sim-core/include) must come before the SDK directories**; keep this order when changing include lists.

## Running and Self-Test

```bat
out\xysim.exe --help
```

| Argument | Effect |
|---|---|
| (no arguments) | The USB AT port goes to this console (self-test: type AT commands directly) |
| `--at-com <port>` | The USB AT port goes to a virtual serial port (VSPE / com0com, e.g. COM5) |
| `--modem-com <port>` | The MODEM (PPP dial-up) port goes to a virtual serial port (Windows modem dialing must use com0com, see below) |
| `--lpuart-com <port>` | The LPUART AT port goes to a virtual serial port (VSPE / com0com) |
| `--baud <rate>` | Applies to all opened COM ports (default 115200; virtual serial ports are not rate-limited) |
| `--pcap [file]` | Captures uplink/downlink IP packets into a pcap file (default `xysim_capture.pcap` if unspecified) |
| `--logfile [file]` | Mirrors logs into a file (window display unchanged; default `xysim.log` if unspecified) |

The three channels can work simultaneously, each on its own independent virtual serial port pair; the same COM number cannot be assigned to two channels at once (detected → immediate exit). Logs uniformly go to **stderr**; stdout carries only the AT data stream. To save logs to disk, use `--logfile [file]` (default `xysim.log` if unspecified; the window still displays while the file is written in sync).

Two notes:

1. **No administrator needed**: the data plane is an in-process user-space proxy (sim_proxy) — no UAC prompt, no driver install, no ICS setup; it runs directly as an ordinary user;
2. **Auto PDP activation at boot**, no need to type `AT+CGACT=1` by hand; as long as the host itself has working internet, simulator traffic gets real public-network access (sim_proxy auto-discovers the host DNS).

Console self-test (type after running with no arguments):

```text
AT            -> OK
ATI           -> XINYI / XY4101PC / <firmware version string> (same as real device)
AT+CSQ        -> +CSQ: 31,99
AT+CEREG?     -> +CEREG: 0,1
AT+CGSN       -> IMEI
AT+CIMI       -> IMSI
AT+CGACT?     -> +CGACT: 1,1   (auto-activated at boot)
AT+CGPADDR    -> 10.0.0.2  (statically assigned by sim_proxy)
```

Note when piping commands in batch: sending a dozen or so at once triggers at_ctl busy (`+CME ERROR: 8007`); leave an interval of ≥0.3s between commands.

## gdb Single-Step Debugging

1. `build.bat debug` produces `out-dbg\xysim.exe` (-g, no optimization);
2. Start gdb (the data plane is an ordinary user-space process; no administrator needed):

```text
set PATH=<MSYS2 install dir>\mingw32\bin;%PATH%   <- keep the same directory as SIM_MINGW
gdb out-dbg\xysim.exe
(gdb) set args --at-com COM21      <- required
(gdb) start                        <- starts and stops at the top of main() (or just use run)
(gdb) continue                     <- release; let the program run fully
```

- **Inspect the task table**: once the program is running, press `Ctrl+C` to interrupt

```text
(gdb) set $buf = (char *)malloc(2048)
(gdb) call vTaskList($buf)
(gdb) printf "%s\n", $buf
(gdb) call (void)free($buf)
```

Example actual output:

```text
simbridge         X      11        1014   13
IDLE              R      0          326    2
tcpip_thd         B      16         758   10
proxy_rx          B      12        4086   14
fsProxy           B      14        1014    4
at_ctl            B      21         502    5
atproxy           B      21        1014    6
atrcv             B      22         502    7
appResp           B      15         374    8
pktDL             B      0          374   12
sockEvent         B      16         374   11
mainProxy         B      16         758    9
Tmr Svc           B      31         326    3
```

`vTaskList` has no header; the five columns (tab-separated) mean:

| Column | Meaning |
|---|---|
| `Name` | Task name (specified via `attr.name` in `osThreadNew`) |
| `State` | X=Running, R=Ready, B=Blocked, S=Suspended, D=Deleted |
| `Priority` | FreeRTOS priority; higher number = higher |
| `Stack` | Remaining bytes at the stack high-water mark; smaller = more dangerous (close to stack overflow) |
| `Num` | Task number (creation order) |

Key points:

- Do not use `next/step` to cross blocking calls (`osDelay`, queue/semaphore/mutex waits): while GDB is paused, ticks cannot be delivered, and single-stepping stuck inside a blocking call will never see the wakeup. The correct approach is to set a breakpoint at the destination past the block and `continue` to it;
- After a long stop at a breakpoint, when you `continue`, ticks are back-filled in one burst according to real elapsed time (back-fill logic in `port.c`); timeouts/timers fire in a cluster — this is expected.

## Serial Tool Testing (Virtual Serial Port Pair)

The virtual serial port tool can be **VSPE** or **com0com** (both free). Either works for the AT command channel; PPP dialing (`--modem-com`) splits into two cases:

- **Windows modem dialing** (create a dial-up connection in "Network Connections") → **com0com required**: the Windows dial-up subsystem checks the DTR/DSR/DCD handshake signals; VSPE does not simulate these hardware line states, causing "modem not responding" or dial failure;
- **Home-grown PPP client** → VSPE also works: pure-software PPP does not depend on hardware flow control; it only needs transparent byte passthrough.

### Option 1: VSPE (recommended for AT command testing)

1. Create a virtual serial port pair in VSPE: Add device → Pair, e.g. `COM20 <-> COM21`, click run;
2. Start the simulator occupying one end: `out\xysim.exe --at-com COM20`;
3. Open the other end `COM21` in a serial tool (SSCOM/XCOM/PuTTY) at 115200 8N1;
4. Exchange AT commands; behavior matches the console self-test.

### Option 2: com0com (recommended for Windows modem dialing)

1. Download and install [com0com](https://sourceforge.net/projects/com0com/);
2. Windows blocks unsigned drivers by default; driver signature enforcement must be disabled before installing/using it (pick one):
   - **Temporary** (valid for this boot): hold Shift and click Restart → Troubleshoot → Advanced options → Startup Settings → Restart → press `7` or `F7` (Disable driver signature enforcement);
   - **Permanent**: run `bcdedit.exe /set nointegritychecks on` in an administrator PowerShell; effective after reboot. Revert with: `bcdedit.exe /set nointegritychecks off`;
3. Create the virtual serial port pair from the command line: `setupc.exe install PortName=COM20 PortName=COM21`;
4. Usage from here is the same as VSPE.

## Verified AT Commands (Excerpt)

| Category | Commands |
|---|---|
| Basic | AT / ATE0 / ATI / AT+CGMI / CGMM / CGMR / CGSN |
| SIM/signal | CPIN? / CIMI / CSQ / COPS? |
| Network registration | CREG / CEREG / CGREG (read + set) |
| PDP | CGATT / CGDCONT / CGACT / CGPADDR / CGCONTRDP |
| Data plane | ping / full plaintext socket chain / NTP / CCLK etc. (macro alignment compiled in) |
| Application protocols | HTTP / FTP / MQTT (same implementation as the real device) |
| Security | Full SSL/TLS chain (mbedtls + stock SDK TLS AT commands, same list as the real device) |
| Files | Full set of file-system AT commands (littlefs; flash backend is a disk image) |

Unregistered commands are handled with real-device semantics (forwarded to the virtual PS; most reply ERROR).

## Log Shim

SDK log macros (`xy_printf` / `user_printf`) are redirected through the shim to plain-text console output; no Logview tool needed. Logs are emitted asynchronously and do not block SDK application code; output format `[h:m:s.ms][level][module] message` (warning yellow, error red; coloring is automatically disabled when redirected to a file).

## Adding a New Chip Product

Copy an existing `target-xxx/` directory as the skeleton, then modify:

- `sim-target.cmake` — chip-difference injection point
- chip-specific headers — `include/` / `include_host/`
- the bridge layer — `src/sim_xxx_bridge.c`
- the entry point — `src/sim_xxx_main.c`

Switch the build with `cmake -DSIM_TARGET=target-xxx`. sim-core and sim-sdk-common are fully reused; only differences at the chip register/memory-layout level go into target-xxx.

## Appendix: Toolchain Setup Notes

> Reference when switching machines. The steps below use the dev-machine layout (MSYS2 at `D:\msys64`) as an example. When installing elsewhere: point `build.bat` at it via `--mingw/--ninja-dir/--cmake-dir` (or `SIM_MINGW/SIM_NINJA/SIM_CMAKE`); for the Python scripts under `tools/`, override with `set MSYS2_ROOT=<install directory>`.

1. Extract MSYS2 base to `D:\msys64`;
2. Switch mirrors to Aliyun: prepend to the first lines of `etc/pacman.d/mirrorlist.msys` and `mirrorlist.mingw`
   `https://mirrors.aliyun.com/msys2/msys/$arch/`,
   `https://mirrors.aliyun.com/msys2/mingw/$repo/`
   (**note: the mingw repo must use `$repo`; using `$arch` gives 404**);
3. Keyring initialization (`pacman-key` cannot run in the sandbox; equivalent manual steps are under `tools/`):
   `tools/pacman_key_init.py` (imports msys2.gpg + ownertrust),
   `tools/fix_keyring.py` (exports the legacy `pubring.gpg` — pacman 6.1 checks that file — and sets the 5 master keys to ultimate trust);
4. `pacman -S mingw-w64-i686-gcc mingw-w64-i686-gdb` (the simulator itself only needs the 32-bit toolchain; gdb is for single-step debugging, script `tools/pacman_install_gdb.py`);
5. Run `build.bat`.

## License

MIT — see [LICENSE](LICENSE).

> **Statement**: this project is designed, developed and maintained by Chengtian Liu; copyright is jointly owned by Chengtian Liu and Xinyi Information Technology Co., Ltd., under the MIT license. The module SDK copyright belongs to Xinyi Information Technology; this repository contains and distributes no SDK source. A few individual files such as `sim-sdk-common/lwipopts.h` retain the original SDK copyright notice, and `third_party/FreeRTOS-Kernel` is the official MIT-licensed FreeRTOS Windows port.
