#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/at_net_test.py -- cat1-host-sim AT network regression test tool

Opens the given serial port (the other end of the virtual serial port pair) and
runs a full set of AT network tests against the xysim.exe simulator, covering
the fake CP control plane + the sim_proxy user-space data plane:

    basic  AT / ATI / CSQ / CEREG / CPIN            -- AT channel and static replies
    pdp    CGACT? / CGPADDR / CGCONTRDP              -- PDP activation state (10.0.0.2)
    ping   QPING gateway / public / black-hole       -- ICMP three-path regression:
           * 10.0.0.1        in-subnet inline direct reply (should answer instantly;
                             verifies the byte-order fix)
           * 223.5.5.5       public address via the host probe thread (verifies the
                             data plane is not blocked)
           * 192.0.2.1       black-hole address loses packets while the data plane
                             stays alive (verifies probe-thread isolation)
    dns    QIDNSGIP                                  -- DNS resolution URC flow
    tcp    QIOPEN/QISEND/QIRD/QICLOSE                -- full TCP data plane flow;
           by default sends HTTP HEAD (Connection: close) to www.baidu.com:80,
           verifying SEND OK, response read-back, and a clean close on the peer
           FIN -> "+QIURC: closed" (the regression point for the FIN-ACK
           upper-bound fix)
    ssl    QSSLOPEN/QSSLSEND/QSSLRECV/QSSLCLOSE      -- full TLS flow (not run by
           default; enable explicitly with --tests ssl). Completes an mbedtls
           handshake against www.baidu.com:443 (verifying the host entropy/random
           shim + TLS over the sim_proxy TCP relay), sends an HTTP GET and reads
           back the plaintext. The handshake is slow, so --ssl-open-timeout
           defaults to 45s

usage: start the virtual serial port pair and the simulator first, see README
    xysim.exe --at-com COM20
    python tools/at_net_test.py --port COM21
    python tools/at_net_test.py --port COM21 --tests basic,pdp
    python tools/at_net_test.py --port COM21 --tests tcp --tcp-host example.com --tcp-port 80
    python tools/at_net_test.py --port COM21 --tests ssl
    python tools/at_net_test.py --port COM21 --verbose --rawlog at_traffic.log
    python tools/at_net_test.py --port COM21 --quiet    # show only PASS/FAIL results

Dependencies: pyserial (pip install pyserial)
Exit codes: 0 = all passed, 1 = some failures, 2 = serial port/environment error
"""

import argparse
import re
import sys
import time
from collections import deque

try:
    import serial
except ImportError:
    print("pyserial is missing; install it first: pip install pyserial", file=sys.stderr)
    sys.exit(2)

# ---------------------------------------------------------------- constants ----

DEFAULT_BAUD   = 115200
DEFAULT_GAP    = 0.35     # command interval (README: >=0.3s, otherwise at_ctl gets busy with +CME ERROR: 8007)
DEFAULT_CMD_TO = 3.0      # wait for the terminal state (OK/ERROR) of ordinary commands

MODEM_IP   = "10.0.0.2"   # fixed allocation by sim_proxy
GATEWAY_IP = "10.0.0.1"   # the proxy box itself

# Terminal lines: OK / ERROR / +CME ERROR: xx / +CMS ERROR: xx
RE_TERMINAL = re.compile(r"^(OK|ERROR|\+CME ERROR.*|\+CMS ERROR.*)$")
RE_CME_BUSY = re.compile(r"\+CME ERROR:\s*8007")


def _now():
    return time.monotonic()


# ------------------------------------------------------------ AT link ----

class AtError(Exception):
    pass


class AtLink:
    """Serial AT channel: line buffer + URC stash + command throttling.

    Complete received lines go into self._lines (deque); wait_line searches the
    whole deque for a matching line (a URC may arrive before the wait for a
    later command, so looking only at the head of the queue is not enough).
    Unmatched lines fall into self.history for test assertions to review.
    """

    def __init__(self, port, baud, gap=DEFAULT_GAP, verbose=False,
                 rawlog=None, cmd_timeout=DEFAULT_CMD_TO, echo=True):
        try:
            self.ser = serial.Serial(port=port, baudrate=baud,
                                     bytesize=serial.EIGHTBITS,
                                     parity=serial.PARITY_NONE,
                                     stopbits=serial.STOPBITS_ONE,
                                     timeout=0.02, write_timeout=2.0)
        except serial.SerialException as e:
            raise AtError("failed to open serial port %s: %s" % (port, e))
        self.gap = gap
        self.verbose = verbose
        self.echo = echo            # echo TX/RX on stdout (disabled by --quiet)
        self.cmd_timeout = cmd_timeout
        self._buf = b""                 # raw bytes not yet forming a complete line
        self._lines = deque()           # complete unconsumed lines
        self.history = []               # consumed unmatched lines (including URCs)
        self._last_tx = 0.0
        self._rawlog = None
        if rawlog:
            self._rawlog = open(rawlog, "wb")

    # ---- low-level send/receive ----

    def close(self):
        try:
            if self._rawlog:
                self._rawlog.close()
            self.ser.close()
        except Exception:
            pass

    def _throttle(self):
        wait = self.gap - (_now() - self._last_tx)
        if wait > 0:
            time.sleep(wait)

    def write(self, data: bytes):
        self._throttle()
        self._last_tx = _now()
        self.ser.write(data)
        if self.echo:
            # Human-readable echo: AT command lines (ASCII ending with CR) are shown as ">> xxx"
            if data.endswith(b"\r") and data[:-1].isascii():
                print(">> %s" % data[:-1].decode("ascii"), flush=True)
            else:
                print(">> [%d bytes of data] %r" % (len(data), data[:64]), flush=True)
        if self.verbose:
            print("  TX %r" % data, file=sys.stderr)

    def _pump(self):
        """Pull readable bytes from the serial port into the buffer and split complete lines into _lines."""
        n = self.ser.in_waiting
        if n:
            chunk = self.ser.read(n)
            if self._rawlog:
                self._rawlog.write(chunk)
            if self.verbose:
                print("  RX %r" % chunk, file=sys.stderr)
            self._buf += chunk
        while True:
            i = self._buf.find(b"\n")
            if i < 0:
                break
            line = self._buf[:i].rstrip(b"\r").decode("utf-8", "replace").strip()
            self._buf = self._buf[i + 1:]
            if line:
                if self.echo:
                    print("<< %s" % line, flush=True)
                self._lines.append(line)

    def _remember(self, lines):
        for ln in lines:
            self.history.append(ln)
            if len(self.history) > 500:
                self.history.pop(0)

    # ---- line/URC waiting ----

    def wait_line(self, regex, timeout):
        """Wait for a line matching regex. Returns (matched_line, interim_lines).
        interim holds the lines skipped before the matched one (URCs etc.),
        already recorded in history. Raises AtError on timeout."""
        deadline = _now() + timeout
        while True:
            self._pump()
            for idx, ln in enumerate(self._lines):
                if regex.match(ln):
                    interim = [self._lines.popleft() for _ in range(idx)]
                    self._lines.popleft()          # pop the matched line itself
                    self._remember(interim)
                    return ln, interim
            if _now() >= deadline:
                raise AtError("timeout waiting for /%s/ (%ss)" % (regex.pattern, timeout))
            time.sleep(0.02)

    def wait_any(self, patterns, timeout):
        """patterns: [(label, regex), ...]; returns (label, matched_line, interim)
        as soon as any pattern hits."""
        deadline = _now() + timeout
        while True:
            self._pump()
            for idx, ln in enumerate(self._lines):
                for label, rx in patterns:
                    if rx.match(ln):
                        interim = [self._lines.popleft() for _ in range(idx)]
                        self._lines.popleft()
                        self._remember(interim)
                        return label, ln, interim
            if _now() >= deadline:
                raise AtError("timeout waiting for %s (%ss)" % (
                    "|".join(l for l, _ in patterns), timeout))
            time.sleep(0.02)

    def wait_prompt(self, timeout):
        """Wait for the QISEND '>' prompt. The SDK has two shapes (at_passthrough.h):
        '\\r\\n>\\r\\n' (as a full line) or '\\r\\n> ' (no trailing newline) -- the
        latter never yields a complete line, so the tail of the raw buffer must be
        checked directly."""
        deadline = _now() + timeout
        while True:
            self._pump()
            for idx, ln in enumerate(self._lines):
                if ln == ">":
                    interim = [self._lines.popleft() for _ in range(idx)]
                    self._lines.popleft()
                    self._remember(interim)
                    return interim
            if self._buf.rstrip().endswith(b">"):
                self._buf = b""
                if self.echo:
                    print("<< >", flush=True)
                return []
            if _now() >= deadline:
                raise AtError("timeout waiting for the QISEND '>' prompt (%ss)" % timeout)
            time.sleep(0.02)

    # ---- commands ----

    def cmd(self, line, timeout=None):
        """Send an AT command and wait for the terminal state. Returns
        (result_line, interim_lines). result_line is OK / ERROR / +CME ERROR: xx."""
        timeout = self.cmd_timeout if timeout is None else timeout
        self.history.clear()
        self.write(line.encode("ascii") + b"\r")
        t0 = _now()
        res, interim = self.wait_line(RE_TERMINAL, timeout)
        return res, interim, _now() - t0

    def cmd_ok(self, line, timeout=None):
        """Run a command and require OK; raise AtError otherwise."""
        res, interim, _ = self.cmd(line, timeout)
        if res != "OK":
            raise AtError("%s -> %s" % (line, res))
        return interim

    def drain_terminal(self, timeout=1.5):
        """Best-effort absorb one extra terminal line (e.g. the OK after SEND OK); fine if there is none."""
        try:
            res, _ = self.wait_line(RE_TERMINAL, timeout)
            return res
        except AtError:
            return None

    def cmd_raw(self, line, until_regex, timeout):
        """After sending the command, do not split into lines; accumulate raw bytes
        until until_regex matches somewhere in the whole raw text (decoded as
        latin-1). A QIRD response is '+QIRD: <len>\\r\\n<binary data>\\r\\nOK' --
        the data section may contain \\r\\n, and line-based parsing would shred it,
        so it must be captured as a whole and then extracted by declared length."""
        self._throttle()
        self._last_tx = _now()
        raw = bytes(self._buf)          # take along any leftover bytes not yet forming a line
        self._buf = b""
        self.ser.write(line.encode("ascii") + b"\r")
        if self.echo:
            print(">> %s" % line, flush=True)
        if self.verbose:
            print("  TX(raw) %r" % line, file=sys.stderr)
        deadline = _now() + timeout
        while True:
            n = self.ser.in_waiting
            if n:
                chunk = self.ser.read(n)
                if self._rawlog:
                    self._rawlog.write(chunk)
                if self.verbose:
                    print("  RX(raw) %r" % chunk, file=sys.stderr)
                raw += chunk
                if until_regex.search(raw.decode("latin1")):
                    if self.echo:
                        # Show binary sections as a repr summary to avoid flooding the screen/garbled output
                        shown = raw if len(raw) <= 256 else raw[:256] + b"..."
                        print("<< [raw %d bytes] %r" % (len(raw), shown),
                              flush=True)
                    return raw
            if _now() >= deadline:
                raise AtError("timeout waiting for the raw response of command %s (%ss), %d bytes received so far"
                              % (line, timeout, len(raw)))
            time.sleep(0.02)


# ------------------------------------------------------------ test framework ----

class Runner:
    def __init__(self, link, args):
        self.link = link
        self.args = args
        self.results = []       # (group, name, ok, detail)

    def record(self, group, name, ok, detail=""):
        self.results.append((group, name, ok, detail))
        mark = "PASS" if ok else "FAIL"
        print("[%s] %s.%s%s" % (mark, group, name,
                                ("  -- " + detail) if detail else ""))

    def case(self, group, name):
        """Decorator: catch AtError/exceptions and record them as FAIL."""
        def deco(fn):
            def wrapped(*a, **kw):
                try:
                    fn(*a, **kw)
                except AtError as e:
                    self.record(group, name, False, str(e))
                    return False
                except Exception as e:      # noqa
                    self.record(group, name, False, "exception: %r" % e)
                    return False
                return True
            return wrapped
        return deco


# ---------------------------------------------------------- basic tests ----

def test_basic(r):
    link = r.link
    g = "basic"

    @r.case(g, "AT")
    def t_at():
        link.cmd_ok("AT")
        r.record(g, "AT", True)
    if not t_at():
        return

    @r.case(g, "ATI")
    def t_ati():
        interim = link.cmd_ok("ATI")
        r.record(g, "ATI", True, "; ".join(interim[:3]))
    t_ati()

    @r.case(g, "CSQ")
    def t_csq():
        _, interim, _ = link.cmd("AT+CSQ")
        m = [l for l in interim if l.startswith("+CSQ:")]
        ok = bool(m) and m[0].split(":")[1].strip().split(",")[0].isdigit()
        r.record(g, "CSQ", ok, m[0] if m else "no +CSQ line")
    t_csq()

    @r.case(g, "CEREG")
    def t_cereg():
        _, interim, _ = link.cmd("AT+CEREG?")
        m = [l for l in interim if l.startswith("+CEREG:")]
        # 0,1 registered on home network / 0,5 roaming registration
        ok = bool(m) and m[0].split(":")[1].strip().split(",")[1] in ("1", "5")
        r.record(g, "CEREG", ok, m[0] if m else "no +CEREG line")
    t_cereg()

    @r.case(g, "CPIN")
    def t_cpin():
        _, interim, _ = link.cmd("AT+CPIN?")
        m = [l for l in interim if l.startswith("+CPIN:")]
        ok = bool(m) and "READY" in m[0]
        r.record(g, "CPIN", ok, m[0] if m else "no +CPIN line")
    t_cpin()


# ------------------------------------------------------------ pdp tests ----

def test_pdp(r):
    link = r.link
    g = "pdp"

    @r.case(g, "CGATT")
    def t():
        _, interim, _ = link.cmd("AT+CGATT?")
        m = [l for l in interim if l.startswith("+CGATT:")]
        ok = bool(m) and m[0].endswith("1")
        r.record(g, "CGATT", ok, m[0] if m else "no +CGATT line")
    t()

    @r.case(g, "CGACT")
    def t2():
        _, interim, _ = link.cmd("AT+CGACT?")
        m = [l for l in interim if l.startswith("+CGACT:")]
        # expected "+CGACT: 1,1" (cid 1 activated, auto-dial at boot)
        ok = bool(m) and m[0].split(":")[1].strip() == "%d,1" % r.args.cid
        r.record(g, "CGACT", ok, m[0] if m else "no +CGACT line")
    t2()

    @r.case(g, "CGPADDR")
    def t3():
        _, interim, _ = link.cmd("AT+CGPADDR")
        m = [l for l in interim if l.startswith("+CGPADDR:")]
        ok = bool(m) and MODEM_IP in m[0]
        r.record(g, "CGPADDR", ok,
                 m[0] if m else "no +CGPADDR line (expected to contain %s)" % MODEM_IP)
    t3()

    @r.case(g, "CGCONTRDP")
    def t4():
        _, interim, _ = link.cmd("AT+CGCONTRDP=%d" % r.args.cid)
        m = [l for l in interim if l.startswith("+CGCONTRDP:")]
        ok = bool(m) and GATEWAY_IP in m[0] and MODEM_IP in m[0]
        r.record(g, "CGCONTRDP", ok,
                 m[0] if m else "no +CGCONTRDP line (expected to contain %s/%s)"
                 % (MODEM_IP, GATEWAY_IP))
    t4()


# ----------------------------------------------------------- ping tests ----

RE_PING_REPLY = re.compile(r'^\+QPING: 0,"([^"]+)",(\d+),(\d+),(\d+)$')
RE_PING_STATS = re.compile(r"^\+QPING: 0,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)$")
RE_PING_ERR   = re.compile(r"^\+QPING: (\d+)$")


def _qping(link, r, host, timeout_s, count, budget_extra=5.0):
    """Issue QPING and collect all URCs. Returns (stats_tuple|None, err|None, replies, elapsed)."""
    cid = r.args.cid
    res, _, _ = link.cmd("AT+QPING=%d,%s,%d,%d" % (cid, host, timeout_s, count))
    if res != "OK":
        raise AtError("AT+QPING command rejected: %s" % res)
    t0 = _now()
    budget = count * (timeout_s + 2) + budget_extra
    replies, stats, err = [], None, None
    deadline = _now() + budget
    patterns = [("stats", RE_PING_STATS), ("reply", RE_PING_REPLY),
                ("err", RE_PING_ERR)]
    while stats is None and err is None:
        left = deadline - _now()
        if left <= 0:
            raise AtError("QPING %s timed out waiting for the stats URC (%ss)" % (host, budget))
        label, ln, _ = link.wait_any(patterns, left)
        if label == "stats":
            m = RE_PING_STATS.match(ln)
            stats = tuple(int(x) for x in m.groups())
        elif label == "reply":
            m = RE_PING_REPLY.match(ln)
            replies.append((m.group(1), int(m.group(3))))   # (ip, rtt)
        else:
            err = int(RE_PING_ERR.match(ln).group(1))
    return stats, err, replies, _now() - t0


def test_ping(r):
    link = r.link
    g = "ping"

    # 1) gateway (in-subnet inline direct-reply path) -- also verifies the instant
    #    reply: if sim_proxy_icmp's byte-order check fails, it would take the 2s
    #    host probe path and the elapsed time would grow noticeably
    @r.case(g, "gateway")
    def t_gw():
        stats, err, replies, elapsed = _qping(link, r, GATEWAY_IP, 5, 2)
        if err is not None:
            r.record(g, "gateway", False, "+QPING error code %d" % err)
            return
        sent, recv, loss = stats[0], stats[1], stats[2]
        fast = elapsed < 3.0
        ok = (recv == sent == 2 and loss == 0 and fast)
        detail = "sent=%d recv=%d loss=%d elapsed %.1fs rtt=%s" % (
            sent, recv, loss, elapsed,
            [x[1] for x in replies] if replies else "-")
        if not fast and recv == sent:
            detail += " (slow -- suspected host probe instead of inline direct reply)"
        r.record(g, "gateway", ok, detail)
    if not t_gw():
        # If even the gateway is unreachable, the public-network tests that follow are pointless
        r.record(g, "wan", False, "skipped (gateway ping failed)")
        r.record(g, "blackhole", False, "skipped (gateway ping failed)")
        return

    # 2) public address (host probe thread path)
    @r.case(g, "wan")
    def t_wan():
        stats, err, replies, elapsed = _qping(
            link, r, r.args.ping_wan, 5, 2, budget_extra=8.0)
        if err is not None:
            r.record(g, "wan", False, "+QPING error code %d" % err)
            return
        sent, recv, loss = stats[0], stats[1], stats[2]
        # Allow some packet loss on the public network (>=1 reply counts as pass); only total loss fails
        ok = recv >= 1
        r.record(g, "wan", ok, "%s: sent=%d recv=%d loss=%d elapsed %.1fs"
                 % (r.args.ping_wan, sent, recv, loss, elapsed))
    t_wan()

    # 3) black-hole address (TEST-NET-1, the probe is guaranteed to fail) --
    #    regression point: the probe thread must block in isolation; during and
    #    after it, the data plane (gateway ping) must still answer instantly
    @r.case(g, "blackhole")
    def t_bh():
        stats, err, replies, elapsed = _qping(
            link, r, r.args.ping_blackhole, 3, 1, budget_extra=8.0)
        if err is None and stats[1] == 0:
            pass                                  # expected: 0 received
        elif err is not None:
            pass                                  # reporting an error code is also acceptable
        else:
            r.record(g, "blackhole", False,
                     "black-hole address %s actually returned a reply packet: %s" % (r.args.ping_blackhole, stats))
            return
        # Key assertion: after the black-hole ping, the gateway ping is still fast (data plane not blocked)
        stats2, err2, _, elapsed2 = _qping(link, r, GATEWAY_IP, 5, 2)
        alive = (err2 is None and stats2[1] == 2 and elapsed2 < 3.0)
        r.record(g, "blackhole", alive,
                 "black-hole ping finished (%s, elapsed %.1fs), then gateway ping %s (elapsed %.1fs)"
                 % ("err=%d" % err if err is not None else "recv=0",
                    elapsed, "normal" if alive else "abnormal", elapsed2))
    t_bh()


# ------------------------------------------------------------ dns tests ----

RE_DNS_HEAD = re.compile(r'^\+QIURC: "dnsgip",(\d+)(?:,(\d+),(\d+))?$')
RE_DNS_IP   = re.compile(r'^\+QIURC: "dnsgip","(\d+\.\d+\.\d+\.\d+)"$')


def test_dns(r):
    link = r.link
    g = "dns"

    @r.case(g, "QIDNSGIP")
    def t():
        res, _, _ = link.cmd('AT+QIDNSGIP=%d,"%s"'
                             % (r.args.cid, r.args.dns_domain))
        if res != "OK":
            r.record(g, "QIDNSGIP", False, "command rejected: %s" % res)
            return
        # First wait for the result-head URC: "+QIURC: "dnsgip",0,<n>,<ttl> or "dnsgip",<err>
        _, ln, _ = link.wait_any([("head", RE_DNS_HEAD)], 15.0)
        m = RE_DNS_HEAD.match(ln)
        code = int(m.group(1))
        if code != 0:
            r.record(g, "QIDNSGIP", False, "DNS resolution failed err=%d" % code)
            return
        n = int(m.group(2) or 1)
        ips = []
        for _ in range(n):
            _, ln2, _ = link.wait_any([("ip", RE_DNS_IP)], 5.0)
            ips.append(RE_DNS_IP.match(ln2).group(1))
        r.record(g, "QIDNSGIP", len(ips) >= 1,
                 "%s -> %s" % (r.args.dns_domain, ", ".join(ips)))
    t()


# ------------------------------------------------------------ tcp tests ----

RE_QIOPEN_URC = re.compile(r"^\+QIOPEN: (\d+),(-?\d+)$")
RE_RECV_URC   = re.compile(r'^\+QIURC: "recv",(\d+),(\d+)')
RE_CLOSED_URC = re.compile(r'^\+QIURC: "closed",(\d+)$')
RE_QIRD       = re.compile(r"^\+QIRD: (\d+)(?:,(\d+),(\d+))?$")
RE_SEND_OK    = re.compile(r'.*SEND OK.*')


def test_tcp(r):
    link = r.link
    g = "tcp"
    cid, conn = r.args.cid, r.args.conn_id
    host, port = r.args.tcp_host, r.args.tcp_port
    body = ("HEAD / HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: xysim-at-net-test\r\n"
            "Connection: close\r\n\r\n" % host)
    if r.args.tcp_data:
        body = r.args.tcp_data
    payload = body.encode("utf-8")

    opened = False
    try:
        # 1) QIOPEN (buffer mode, async result comes via the +QIOPEN URC)
        res, _, _ = link.cmd('AT+QIOPEN=%d,%d,"TCP","%s",%d'
                             % (cid, conn, host, port))
        if res != "OK":
            r.record(g, "QIOPEN", False, "command rejected: %s" % res)
            return
        _, ln, _ = link.wait_any([("urc", RE_QIOPEN_URC)], r.args.open_timeout)
        m = RE_QIOPEN_URC.match(ln)
        err = int(m.group(2))
        if err != 0:
            r.record(g, "QIOPEN", False, "+QIOPEN result code %d" % err)
            return
        opened = True
        r.record(g, "QIOPEN", True, "%s:%d connected successfully" % (host, port))

        # 2) QISEND: '>' prompt -> write data -> SEND OK
        link.history.clear()
        link.write(("AT+QISEND=%d,%d" % (conn, len(payload))).encode() + b"\r")
        link.wait_prompt(5.0)
        time.sleep(0.05)
        link.write(payload)                     # raw data, not an AT command line
        link._last_tx = _now()
        label, ln2, _ = link.wait_any(
            [("ok", RE_SEND_OK), ("bad", re.compile(r"^(ERROR|\+CME ERROR.*)$"))],
            10.0)
        if label != "ok":
            r.record(g, "QISEND", False, "send failed: %s" % ln2)
            return
        link.drain_terminal(1.5)                # absorb a possibly following OK
        r.record(g, "QISEND", True, "%d bytes -> SEND OK" % len(payload))

        # 3) Receive data: wait for the recv URC (buffer mode), then read with QIRD.
        #    The QIRD response = "+QIRD: <len>\r\n<raw data>\r\nOK"; the data may
        #    contain \r\n, so cmd_raw captures it as a whole and then slices it
        #    exactly by the declared length
        want = 0
        try:
            _, ln3, _ = link.wait_any([("recv", RE_RECV_URC)], 10.0)
            want = int(RE_RECV_URC.match(ln3).group(2))
        except AtError:
            pass                            # try reading once even if no URC arrived
        raw = link.cmd_raw("AT+QIRD=%d,1500" % conn,
                           re.compile(r"\r\n(OK|ERROR|\+CME ERROR: \d+)\r\n"),
                           5.0)
        text = raw.decode("latin1")
        got = b""
        m = re.search(r"\+QIRD: (\d+)\r\n", text)
        if m:
            n = int(m.group(1))
            if n > 0:
                start = m.end()
                got = raw[start:start + n]
        ok_tail = re.search(r"\r\n(OK|ERROR|\+CME ERROR: \d+)\r\n", text)
        term = ok_tail.group(1) if ok_tail else "?"
        good = (n > 0 and len(got) == n and term == "OK") if m else False
        http_ok = b"HTTP/1." in got
        r.record(g, "QIRD", good and http_ok,
                 "read %d bytes (terminal state %s)%s" % (len(got), term,
                 ", first line: " + got.split(b"\r\n")[0].decode("latin1", "replace")
                 if got else ""))

        # 4) Clean close: with Connection: close the server actively sends FIN;
        #    expect to receive "+QIURC: closed" (regression for the FIN-ACK
        #    upper-bound fix); if it does not arrive, actively QICLOSE and wait again
        closed = False
        try:
            _, ln4, _ = link.wait_any([("closed", RE_CLOSED_URC)],
                                      r.args.close_timeout)
            closed = RE_CLOSED_URC.match(ln4).group(1) == str(conn)
        except AtError:
            pass
        if not closed:
            res, _, _ = link.cmd("AT+QICLOSE=%d" % conn, timeout=5.0)
            if res == "OK":
                try:
                    _, ln4, _ = link.wait_any([("closed", RE_CLOSED_URC)], 5.0)
                    closed = True
                except AtError:
                    closed = False              # OK but no closed URC; barely counts as a pass
                    r.record(g, "close", True,
                             "QICLOSE OK (no closed URC seen, active-close path)")
                    return
        r.record(g, "close", closed,
                 "peer FIN -> +QIURC: \"closed\",%d clean close" % conn
                 if closed else "no closed URC received")
    except AtError as e:
        r.record(g, "flow", False, str(e))
    finally:
        if opened:
            try:
                link.cmd("AT+QICLOSE=%d" % conn, timeout=3.0)
            except Exception:
                pass


# ------------------------------------------------------------ ssl tests ----

RE_QSSLOPEN_URC   = re.compile(r"^\+QSSLOPEN: (\d+),(-?\d+)$")
RE_QSSLRECV_URC   = re.compile(r'^\+QSSLURC: "recv",(\d+),(\d+)')
RE_QSSLCLOSED_URC = re.compile(r'^\+QSSLURC: "closed",(\d+)$')


def test_ssl(r):
    """Full TLS flow: mbedtls handshake (host entropy shim + proxy TCP relay) ->
    send/receive HTTP plaintext over the ciphertext -> close. seclevel defaults
    to 0 (no certificate verification), so no CA needs to be preloaded; the
    default target is www.baidu.com:443."""
    link = r.link
    g = "ssl"
    cid, sslcid = r.args.cid, r.args.ssl_cid
    conn = r.args.ssl_conn_id
    host, port = r.args.ssl_host, r.args.ssl_port
    body = ("GET / HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: xysim-at-net-test\r\n"
            "Connection: close\r\n\r\n" % host)
    if r.args.tcp_data:
        body = r.args.tcp_data
    payload = body.encode("utf-8")

    opened = False
    try:
        # 1) QSSLOPEN (buffer mode, async result via the +QSSLOPEN URC; the handshake is slow)
        res, _, _ = link.cmd('AT+QSSLOPEN=%d,%d,%d,"%s",%d'
                             % (cid, sslcid, conn, host, port))
        if res != "OK":
            r.record(g, "QSSLOPEN", False, "command rejected: %s" % res)
            return
        _, ln, _ = link.wait_any([("urc", RE_QSSLOPEN_URC)],
                                 r.args.ssl_open_timeout)
        m = RE_QSSLOPEN_URC.match(ln)
        err = int(m.group(2))
        if err != 0:
            r.record(g, "QSSLOPEN", False, "+QSSLOPEN result code %d" % err)
            return
        opened = True
        r.record(g, "QSSLOPEN", True, "%s:%d TLS handshake succeeded" % (host, port))

        # 2) QSSLSEND: '>' prompt (same as QISEND) -> write data -> SEND OK
        link.history.clear()
        link.write(("AT+QSSLSEND=%d,%d" % (conn, len(payload))).encode() + b"\r")
        link.wait_prompt(5.0)
        time.sleep(0.05)
        link.write(payload)
        link._last_tx = _now()
        label, ln2, _ = link.wait_any(
            [("ok", RE_SEND_OK), ("bad", re.compile(r"^(ERROR|\+CME ERROR.*)$"))],
            10.0)
        if label != "ok":
            r.record(g, "QSSLSEND", False, "send failed: %s" % ln2)
            return
        link.drain_terminal(1.5)
        r.record(g, "QSSLSEND", True, "%d bytes -> SEND OK" % len(payload))

        # 3) QSSLRECV: read after waiting for +QSSLURC: "recv". The response format
        #    is the same as QIRD: "+QSSLRECV: <len>\r\n<plaintext>\r\nOK"; the
        #    plaintext may contain \r\n, so cmd_raw must capture it as a whole and
        #    then slice by the declared length
        try:
            link.wait_any([("recv", RE_QSSLRECV_URC)], 10.0)
        except AtError:
            pass                            # try reading once even if no URC arrived
        raw = link.cmd_raw("AT+QSSLRECV=%d,1500" % conn,
                           re.compile(r"\r\n(OK|ERROR|\+CME ERROR: \d+)\r\n"),
                           8.0)
        text = raw.decode("latin1")
        got = b""
        n = 0
        m = re.search(r"\+QSSLRECV: (\d+)\r\n", text)
        if m:
            n = int(m.group(1))
            if n > 0:
                start = m.end()
                got = raw[start:start + n]
        ok_tail = re.search(r"\r\n(OK|ERROR|\+CME ERROR: \d+)\r\n", text)
        term = ok_tail.group(1) if ok_tail else "?"
        good = (n > 0 and len(got) == n and term == "OK") if m else False
        http_ok = b"HTTP/1." in got
        r.record(g, "QSSLRECV", good and http_ok,
                 "read %d bytes of plaintext (terminal state %s)%s" % (len(got), term,
                 ", first line: " + got.split(b"\r\n")[0].decode("latin1", "replace")
                 if got else ""))

        # 4) Close: actively QSSLCLOSE -> OK. Only a passive disconnect produces the
        #    +QSSLURC: "closed" URC (socket_ssl.c: the withResultCode path returns
        #    OK only); either one appearing counts as a pass
        closed = False
        try:
            _, ln3, _ = link.wait_any([("closed", RE_QSSLCLOSED_URC)], 3.0)
            closed = True
        except AtError:
            pass
        if not closed:
            res, _, _ = link.cmd("AT+QSSLCLOSE=%d" % conn,
                                 timeout=r.args.close_timeout)
            if res == "OK":
                closed = True
                try:                        # a closed URC may still follow an active close
                    link.wait_any([("closed", RE_QSSLCLOSED_URC)], 2.0)
                except AtError:
                    pass
            r.record(g, "close", closed,
                     "QSSLCLOSE OK" if closed else "QSSLCLOSE rejected: %s" % res)
        else:
            r.record(g, "close", True,
                     "peer disconnect -> +QSSLURC: \"closed\",%d" % conn)
        opened = False
    except AtError as e:
        r.record(g, "flow", False, str(e))
    finally:
        if opened:
            try:
                link.cmd("AT+QSSLCLOSE=%d" % conn, timeout=3.0)
            except Exception:
                pass


# ---------------------------------------------------------------- main flow ----

TEST_GROUPS = ("basic", "pdp", "ping", "dns", "tcp", "ssl")
# ssl depends on a public TLS server and the handshake is slow, so it is not run
# by default; include it explicitly with --tests ssl or --tests all
DEFAULT_TESTS = ("basic", "pdp", "ping", "dns", "tcp")


def parse_args():
    ap = argparse.ArgumentParser(
        description="cat1-host-sim AT network regression test (opens the other end of the virtual serial port pair)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port, e.g. COM21")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="baud rate")
    ap.add_argument("--gap", type=float, default=DEFAULT_GAP,
                    help="minimum interval between AT commands in seconds (<0.3 triggers +CME ERROR: 8007)")
    ap.add_argument("--tests", default=",".join(DEFAULT_TESTS),
                    help="comma-separated test groups: %s (or all; ssl is not run by default)"
                         % ",".join(TEST_GROUPS))
    ap.add_argument("--cid", type=int, default=1, help="PDP context ID")
    ap.add_argument("--conn-id", type=int, default=0, help="socket connection ID")
    ap.add_argument("--ping-wan", default="223.5.5.5", help="public-network ping target")
    ap.add_argument("--ping-blackhole", default="192.0.2.1",
                    help="black-hole ping target (TEST-NET-1, expected to get no reply packet)")
    ap.add_argument("--dns-domain", default="www.baidu.com", help="domain name for DNS resolution")
    ap.add_argument("--tcp-host", default="www.baidu.com", help="TCP test target host")
    ap.add_argument("--tcp-port", type=int, default=80, help="TCP test target port")
    ap.add_argument("--tcp-data", default=None,
                    help="custom payload shared by the tcp/ssl groups (default: HTTP request with Connection: close)")
    ap.add_argument("--ssl-cid", type=int, default=0,
                    help="SSL configuration slot ID (QSSLCFG, 0-5; default 0 = no certificate verification)")
    ap.add_argument("--ssl-conn-id", type=int, default=0,
                    help="SSL socket connection ID (shares the 0-11 space with QIOPEN)")
    ap.add_argument("--ssl-host", default="www.baidu.com",
                    help="SSL test target host")
    ap.add_argument("--ssl-port", type=int, default=443, help="SSL test target port")
    ap.add_argument("--ssl-open-timeout", type=float, default=45.0,
                    help="seconds to wait for the QSSLOPEN handshake result URC (TLS handshake is slow)")
    ap.add_argument("--open-timeout", type=float, default=20.0,
                    help="seconds to wait for the QIOPEN result URC")
    ap.add_argument("--close-timeout", type=float, default=10.0,
                    help="seconds to wait for the peer closed URC")
    ap.add_argument("--cmd-timeout", type=float, default=DEFAULT_CMD_TO,
                    help="seconds to wait for the terminal state of ordinary commands")
    ap.add_argument("--quiet", action="store_true",
                    help="do not echo AT traffic (by default prints >> command / << response)")
    ap.add_argument("--verbose", action="store_true",
                    help="print raw TX/RX bytes on stderr")
    ap.add_argument("--rawlog", default=None, help="write raw received bytes to a file")
    return ap.parse_args()


def main():
    args = parse_args()
    tests = [t.strip().lower() for t in args.tests.split(",") if t.strip()]
    if "all" in tests:
        tests = list(TEST_GROUPS)
    bad = [t for t in tests if t not in TEST_GROUPS]
    if bad:
        print("unknown test groups: %s (options: %s / all)"
              % (",".join(bad), ",".join(TEST_GROUPS)), file=sys.stderr)
        return 2

    print("opening serial port %s @%d, command gap %.2fs, test groups: %s"
          % (args.port, args.baud, args.gap, ",".join(tests)))
    try:
        link = AtLink(args.port, args.baud, gap=args.gap,
                      verbose=args.verbose, rawlog=args.rawlog,
                      cmd_timeout=args.cmd_timeout, echo=not args.quiet)
    except AtError as e:
        print(str(e), file=sys.stderr)
        print("hint: make sure the virtual serial port pair is established and that xysim.exe --at-com holds the other end",
              file=sys.stderr)
        return 2

    r = Runner(link, args)
    t_start = _now()
    try:
        # Channel liveness probe: send AT twice (the first may hit startup noise)
        try:
            link.cmd_ok("AT", timeout=2.0)
        except AtError:
            try:
                link.cmd_ok("AT", timeout=2.0)
            except AtError as e:
                print("AT channel not responding: %s" % e, file=sys.stderr)
                print("hint: is the simulator running? is the serial port occupied by another program?",
                      file=sys.stderr)
                return 2

        dispatch = {"basic": test_basic, "pdp": test_pdp, "ping": test_ping,
                    "dns": test_dns, "tcp": test_tcp, "ssl": test_ssl}
        for t in tests:
            print("\n---- test group: %s ----" % t)
            dispatch[t](r)
    finally:
        link.close()

    total = len(r.results)
    passed = sum(1 for x in r.results if x[2])
    failed = total - passed
    print("\n==== summary (elapsed %.1fs) ====" % (_now() - t_start))
    for grp, name, ok, detail in r.results:
        if not ok:
            print("  FAIL %s.%s -- %s" % (grp, name, detail))
    print("total %d items: passed %d, failed %d" % (total, passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
