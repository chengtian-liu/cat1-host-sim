#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/tap_adapter.py -- TAP virtual NIC management tool (TAP-Windows Adapter V9)

Relies on an installed TAP-Windows driver directory (C:\\Program Files\\TAP-Windows):
creates/removes virtual NICs through its bundled tapinstall.exe (DevCon), and
performs querying, renaming, enabling and IP configuration via PowerShell CIM.

Usage (create/remove require administrator rights; when run without them the
tool automatically requests UAC elevation):
    python tools/tap_adapter.py list
    python tools/tap_adapter.py create --ip 192.168.100.2
    python tools/tap_adapter.py create --name MyTAP --ip 192.168.100.2 --mask 255.255.255.0 --gateway 192.168.100.1 --dns 8.8.8.8
    python tools/tap_adapter.py remove            (removes SimRNDIS by default)
    python tools/tap_adapter.py remove --name MyTAP
    python tools/tap_adapter.py remove --all

Notes:
    * Each create call installs a brand-new TAP NIC; if a NIC with the same
      name already exists it is simply reused (use --force to create a new one
      anyway).
    * It is normal for a newly created NIC to show the "media disconnected"
      state -- the link only comes UP once a user-space program (such as the
      lwIP/tap reader-writer in the simulator) opens the \\\\.\\<GUID> device.
"""

import argparse
import ctypes
import json
import os
import subprocess
import sys
import time

# ---------------------------------------------------------------- constants ----

TAP_HOME     = r"C:\Program Files\TAP-Windows"
TAPINSTALL   = TAP_HOME + r"\bin\tapinstall.exe"
TAP_INF      = TAP_HOME + r"\driver\OemVista.inf"
TAP_HWID     = "tap0901"                 # TAP-Windows Adapter V9 hardware ID
DEFAULT_NAME = "SimRNDIS"                # default NIC connection name

# Common values of Win32_NetworkAdapter.NetConnectionStatus
NET_STATUS = {
    0: "disconnected", 1: "connecting", 2: "connected", 3: "disconnecting",
    4: "hardware not present", 5: "hardware disabled", 6: "hardware malfunction", 7: "media disconnected",
    8: "authenticating", 9: "authentication failed", 10: "authenticated", 11: "invalid key",
    12: "authentication blocked", 13: "disconnected (system sleeping)",
}

# ------------------------------------------------------------ basic utilities ----

def is_admin() -> bool:
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def relaunch_elevated():
    """Re-run this script as administrator (pops UAC); on success the current process exits immediately.

    Note: after ShellExecuteW elevation the working directory defaults to
    C:\\Windows\\System32, so the script path must be converted to an absolute
    path, otherwise the elevated window flashes a "file not found" error and dies.
    """
    script = os.path.abspath(sys.argv[0])
    py = os.path.abspath(sys.executable)

    # Pop a cmd window running python; pause afterwards so the window stays
    # open instead of flashing shut.
    # Note: paths must be quoted by list2cmdline itself; pre-quoting them
    # manually would get double-escaped.
    # Note: --elevated belongs to the main parser and must come before the
    # subcommand; placed after it, argparse hands it to the sub-parser and
    # reports unrecognized arguments.
    inner = subprocess.list2cmdline(
        [py, "-u", script, "--elevated"] + sys.argv[1:])
    body = '%s & echo. & pause' % inner

    # cmd /c "<...>": cmd strips this outer pair of quotes while preserving all
    # inner quotes/& characters
    rc = ctypes.windll.shell32.ShellExecuteW(
        None, "runas", "cmd.exe", '/c "%s"' % body, None, 1)
    if rc <= 32:
        print("[error] elevation failed (UAC may have been cancelled); please run manually as administrator.")
        pause_any_key()
        sys.exit(1)
    sys.exit(0)


def decode(data: bytes) -> str:
    """Decode Windows console output: mostly GBK on Chinese systems, with UTF-8 as fallback."""
    for enc in ("gbk", "utf-8"):
        try:
            return data.decode(enc)
        except (UnicodeDecodeError, LookupError):
            continue
    return data.decode("utf-8", "replace")


def run(cmd, check=True, timeout=120):
    """Run an external command; returns (rc, combined stdout+stderr text)."""
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=timeout)
    out = decode(p.stdout).strip()
    if check and p.returncode != 0:
        raise RuntimeError("command failed (rc=%d): %s\n%s"
                           % (p.returncode, subprocess.list2cmdline(cmd), out))
    return p.returncode, out


def powershell(script: str):
    """Run a PowerShell snippet; returns stdout text (safe with a GBK console)."""
    rc, out = run(["powershell", "-NoProfile", "-NonInteractive",
                   "-Command", script], check=False)
    if rc != 0:
        raise RuntimeError("PowerShell execution failed:\n%s" % out)
    return out

# ------------------------------------------------------------ TAP query ----

_PS_QUERY = r"""
$adapters = Get-CimInstance Win32_NetworkAdapter |
    Where-Object { $_.Name -like 'TAP-Windows Adapter*' } |
    Select-Object NetConnectionID, PNPDeviceID, NetConnectionStatus, NetEnabled
if ($adapters) { $adapters | ConvertTo-Json -Compress } else { '[]' }
"""


def query_tap_adapters():
    """Return the list of TAP NICs: [{name, pnp_id, status, enabled}, ...]"""
    out = powershell(_PS_QUERY).strip()
    try:
        data = json.loads(out) if out else []
    except json.JSONDecodeError:
        data = []
    if isinstance(data, dict):          # with only one NIC, ConvertTo-Json returns an object
        data = [data]
    result = []
    for a in data:
        result.append({
            "name":    a.get("NetConnectionID") or "",
            "pnp_id":  a.get("PNPDeviceID") or "",
            "status":  NET_STATUS.get(a.get("NetConnectionStatus"),
                                      str(a.get("NetConnectionStatus"))),
            "enabled": bool(a.get("NetEnabled")),
        })
    return result


def tapinstall(*args, check=True):
    return run([TAPINSTALL] + list(args), check=check)


def _print_diag():
    """Print troubleshooting info: admin status, whether tapinstall exists, etc."""
    print("    [diag] administrator rights: %s" % ("yes" if is_admin() else "no"))
    print("    [diag] tapinstall exists: %s"
          % ("yes" if os.path.exists(TAPINSTALL) else "no -> %s" % TAPINSTALL))

# ------------------------------------------------------------ subcommands ----

def cmd_list(_args):
    adapters = query_tap_adapters()
    if not adapters:
        print("No TAP-Windows NICs found.")
        return 0
    print("Found %d TAP-Windows NIC(s):" % len(adapters))
    print("-" * 72)
    print("%-18s %-16s %-10s %s" % ("Connection name", "Status", "Enabled", "Device instance ID"))
    for a in adapters:
        print("%-18s %-16s %-10s %s"
              % (a["name"], a["status"], "yes" if a["enabled"] else "no",
                 a["pnp_id"]))
    print("-" * 72)
    return 0


def _rename(pnp_id: str, new: str):
    """Rename a NIC by device instance ID.

    Does not use Rename-NetAdapter -LiteralName: Rename-NetAdapter in Windows
    PowerShell 5.1 has no -LiteralName parameter (only -Name, which matches by
    wildcard). Here we directly modify Win32_NetworkAdapter.NetConnectionID,
    which works with all PS versions and locates the device precisely by
    PNPDeviceID.
    """
    script = (
        "$a = Get-CimInstance Win32_NetworkAdapter | "
        "Where-Object { $_.PNPDeviceID -eq '%s' }; "
        "if (-not $a) { throw 'device not found: %s' }; "
        "Set-CimInstance -InputObject $a -Property @{ NetConnectionID = '%s' }"
        % (pnp_id.replace("'", "''"), pnp_id, new.replace("'", "''")))
    powershell(script)


def _ensure_enabled(pnp_id: str):
    """Make sure the NIC is not in the "disabled" state (best effort; on failure only warn, do not abort creation).

    Notes:
      * A NIC freshly installed by tapinstall install is administratively
        enabled by default, so usually nothing needs to be done;
      * "Enabled: no / media disconnected" shown by list reflects the media
        (link) state reported by NetEnabled -- a TAP NIC's link is naturally
        down until a user-space program (the simulator) opens the device
        handle; this is normal and does not mean it is disabled;
      * Only NetConnectionStatus=5 (hardware disabled) means administratively
        disabled; in that case enable it by instance ID with Enable-PnpDevice
        (the 2016-era tapinstall.exe frequently reports "No matching devices
        found" for instance-ID matching on Win10/11 and is unreliable).
    """
    out = powershell(
        "$a = Get-CimInstance Win32_NetworkAdapter | "
        "Where-Object { $_.PNPDeviceID -eq '%s' }; "
        "if ($a) { $a.NetConnectionStatus } else { '' }"
        % pnp_id.replace("'", "''")).strip()
    if out.isdigit() and int(out) != 5:
        return                       # not disabled (7 = media disconnected is normal), nothing to do
    try:
        powershell("Enable-PnpDevice -InstanceId '%s' -Confirm:$false"
                   % pnp_id.replace("'", "''"))
    except RuntimeError as e:
        print("      [warning] failed to enable the NIC (does not affect the creation result): %s" % e)


def _get_interface_index(pnp_id: str):
    """Get InterfaceIndex by device instance ID (corresponds to Win32_NetworkAdapterConfiguration.Index)."""
    out = powershell(
        "$a = Get-CimInstance Win32_NetworkAdapter | "
        "Where-Object { $_.PNPDeviceID -eq '%s' }; "
        "if ($a -and $a.InterfaceIndex) { $a.InterfaceIndex } else { '' }"
        % pnp_id.replace("'", "''")).strip()
    return int(out) if out.isdigit() else None


def _set_ip(pnp_id: str, ip: str, mask: str, gateway, dns):
    """Configure a static IP by InterfaceIndex (CIM methods, no dependence on netsh name resolution)."""
    idx = _get_interface_index(pnp_id)
    if idx is None:
        raise RuntimeError("cannot obtain the InterfaceIndex of the NIC (PNP: %s)" % pnp_id)

    cfg = ("$cfg = Get-CimInstance Win32_NetworkAdapterConfiguration | "
           "Where-Object { $_.Index -eq %d }; " % idx)

    r = powershell(cfg +
        "$r = Invoke-CimMethod -InputObject $cfg -MethodName EnableStatic "
        "-Arguments @{ IPAddress = @('%s'); SubnetMask = @('%s') }; "
        "$r.ReturnValue" % (ip, mask)).strip()
    if r not in ("0", "1"):       # 0 = success, 1 = reboot required
        raise RuntimeError("EnableStatic failed, ReturnValue=%s" % r)

    if gateway:
        r = powershell(cfg +
            "$r = Invoke-CimMethod -InputObject $cfg -MethodName SetGateways "
            "-Arguments @{ DefaultIPGateway = @('%s'); GatewayCostMetric = @(1) }; "
            "$r.ReturnValue" % gateway).strip()
        if r not in ("0", "1"):
            raise RuntimeError("SetGateways failed, ReturnValue=%s" % r)

    if dns:
        r = powershell(cfg +
            "$r = Invoke-CimMethod -InputObject $cfg -MethodName SetDNSServerSearchOrder "
            "-Arguments @{ DNSServerSearchOrder = @('%s') }; "
            "$r.ReturnValue" % dns).strip()
        if r not in ("0", "1"):
            raise RuntimeError("SetDNSServerSearchOrder failed, ReturnValue=%s" % r)


def _wait_new_adapter(known_ids, timeout=30.0):
    """Poll until a new TAP NIC appears; return its info dict, or None on timeout."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        for a in query_tap_adapters():
            if a["pnp_id"] and a["pnp_id"] not in known_ids:
                return a
        time.sleep(1.0)
    return None


def cmd_create(args):
    adapters = query_tap_adapters()

    # Same-name check
    exist = next((a for a in adapters if a["name"] == args.name), None)
    if exist and not args.force:
        print("[note] NIC '%s' already exists (%s), skipping creation. Add --force to create a new one anyway."
              % (args.name, exist["pnp_id"]))
        return 0

    known_ids = {a["pnp_id"] for a in adapters}

    print("[1/4] tapinstall install %s %s ..." % (TAP_INF, TAP_HWID))
    try:
        _, out = tapinstall("install", TAP_INF, TAP_HWID, check=False)
        print(out)
    except RuntimeError as e:
        print("[error] %s" % e)
        _print_diag()
        return 1
    if "created" not in out.lower() and "installed" not in out.lower():
        print("[error] unexpected output from tapinstall install, see above.")
        _print_diag()
        return 1

    print("[2/4] waiting for the NIC to be enumerated ...")
    new = _wait_new_adapter(known_ids)
    if new is None:
        print("[error] timed out before a new TAP NIC was detected; run 'list' or check Device Manager.")
        return 1
    print("      new NIC: %s (%s)" % (new["name"], new["pnp_id"]))

    print("[3/4] renaming to '%s' ..." % args.name)
    if new["name"] != args.name:
        _rename(new["pnp_id"], args.name)
        # The CIM rename takes effect asynchronously; poll to confirm the new
        # name (only for display/subsequent list -- enabling and IP config both
        # operate by PNP instance ID and do not depend on the name)
        deadline = time.time() + 10.0
        final_name = new["name"]
        while time.time() < deadline:
            for a in query_tap_adapters():
                if a["pnp_id"] == new["pnp_id"]:
                    final_name = a["name"]
                    break
            if final_name == args.name:
                break
            time.sleep(0.5)
        if final_name != args.name:
            print("[warning] the rename did not take effect; current name is still '%s'." % final_name)
            args.name = final_name
    _ensure_enabled(new["pnp_id"])

    print("[4/4] configuring IP ...")
    if args.ip:
        try:
            _set_ip(new["pnp_id"], args.ip, args.mask, args.gateway, args.dns)
            print("      static IP: %s / %s%s"
                  % (args.ip, args.mask,
                     "  gateway: %s" % args.gateway if args.gateway else ""))
        except RuntimeError as e:
            # IP configuration failure does not affect the NIC creation itself; warn and continue
            print("      [warning] failed to configure IP (the NIC was created successfully; configure it manually later): %s" % e)
    else:
        print("      no --ip given, keeping DHCP/unconfigured state.")

    print("[done] TAP NIC '%s' created successfully." % args.name)
    return 0


def cmd_remove(args):
    adapters = query_tap_adapters()
    if not adapters:
        print("[note] there are no TAP-Windows NICs in the system, nothing to remove.")
        return 0

    if args.all:
        targets = adapters
    elif args.instance:
        targets = [a for a in adapters
                   if a["pnp_id"].upper() == args.instance.upper()]
        if not targets:
            print("[error] no TAP NIC with instance ID '%s' found." % args.instance)
            return 1
    else:
        targets = [a for a in adapters if a["name"] == args.name]
        if not targets:
            print("[error] no TAP NIC named '%s' found." % args.name)
            print("       existing: %s" % ", ".join(a["name"] for a in adapters))
            return 1

    ok = True
    for a in targets:
        print("removing TAP NIC: '%s' (%s) ..." % (a["name"], a["pnp_id"]))
        try:
            _, out = tapinstall("remove", a["pnp_id"], check=False)
            print(out)
            if "removed" not in out.lower() and "no matching" not in out.lower():
                print("[error] unexpected output from tapinstall remove, see above.")
                _print_diag()
                ok = False
        except RuntimeError as e:
            print("[error] %s" % e)
            _print_diag()
            ok = False

    left = query_tap_adapters()
    left_ids = {a["pnp_id"] for a in left}
    if any(a["pnp_id"] in left_ids for a in targets):
        # Fallback when removal by instance fails: remove everything by hardware ID
        print("[warning] leftovers remain, trying to remove all tap0901 devices by hardware ID ...")
        _, out = tapinstall("remove", TAP_HWID, check=False)
        print(out)

    remaining = query_tap_adapters()
    if any(a["pnp_id"] in {x["pnp_id"] for x in remaining} for a in targets):
        ok = False
        print("[error] removal failed, the following NICs still exist:")
        for a in remaining:
            print("       %s (%s) status: %s" % (a["name"], a["pnp_id"], a["status"]))
        _print_diag()
        print("    possible causes:")
        print("      1. not running with administrator rights (see diagnostics above)")
        print("      2. the device is in use / pending deletion; try: tapinstall remove then reboot,")
        print("         or uninstall it manually in Device Manager")
        print("      3. group policy blocks device install/uninstall")

    print("[done] removal finished, %d TAP NIC(s) remaining." % len(remaining))
    return 0 if ok else 1

# ---------------------------------------------------------------- main ----

def build_parser():
    p = argparse.ArgumentParser(
        prog="tap_adapter.py",
        description="TAP virtual NIC create/remove tool (TAP-Windows Adapter V9)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""Examples:
  python tools/tap_adapter.py list
  python tools/tap_adapter.py create --ip 192.168.100.2
  python tools/tap_adapter.py create --name MyTAP --ip 192.168.100.2
  python tools/tap_adapter.py remove
  python tools/tap_adapter.py remove --name MyTAP
  python tools/tap_adapter.py remove --all
""")
    p.add_argument("--elevated", action="store_true",
                   help=argparse.SUPPRESS)  # internal use: marks the elevated child process
    sub = p.add_subparsers(dest="command", required=True)

    sub.add_parser("list", help="list all TAP-Windows NICs")

    c = sub.add_parser("create", help="create a new TAP NIC")
    c.add_argument("--name", default=DEFAULT_NAME,
                   help="NIC connection name (default: %(default)s)")
    c.add_argument("--ip", help="static IP address (leave unset to skip configuration)")
    c.add_argument("--mask", default="255.255.255.0",
                   help="subnet mask (default: %(default)s)")
    c.add_argument("--gateway", help="default gateway (optional)")
    c.add_argument("--dns", help="DNS server (optional)")
    c.add_argument("--force", action="store_true",
                   help="force creation even if a NIC with the same name already exists")

    r = sub.add_parser("remove", help="remove TAP NICs")
    g = r.add_mutually_exclusive_group()
    g.add_argument("--name", default=DEFAULT_NAME,
                   help="remove by connection name (default: %(default)s)")
    g.add_argument("--instance", help="remove by device instance ID, e.g. ROOT\\NET\\0001")
    g.add_argument("--all", action="store_true", help="remove all TAP NICs")
    return p


def pause_any_key():
    """Pause after the elevated window finishes so the output/errors can be read; press any key to exit."""
    print("\nPress any key to exit ...")
    try:
        import msvcrt
        sys.stdout.flush()
        msvcrt.getch()
    except Exception:
        try:
            input()
        except EOFError:
            pass


def main():
    args = build_parser().parse_args()

    # create/remove require administrator rights; without them, auto-elevate and restart
    if args.command in ("create", "remove") and not is_admin():
        print("[note] administrator rights required, requesting UAC elevation ...")
        relaunch_elevated()

    try:
        rc = {"list": cmd_list, "create": cmd_create, "remove": cmd_remove}[
            args.command](args)
    except Exception as e:
        import traceback
        print("[exception] %s" % e)
        traceback.print_exc()
        _print_diag()
        rc = 1

    # The elevated window is kept open by the outer cmd's pause (press any key
    # to close); python does not need to pause again, avoiding a double keypress.
    sys.exit(rc)


if __name__ == "__main__":
    if sys.platform != "win32":
        print("[error] this tool only supports Windows.")
        sys.exit(1)
    main()
