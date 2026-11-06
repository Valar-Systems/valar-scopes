"""W-CR (docs/weatherscope-stage3.md A12.119-A12.122): the touch-wedge cap on 907069326E64, with the bench image
weatherscope-touchbench-s3-128 (TouchBench.h: the chip DEAD from boot, idle 20 s, healthy reset 180 s).

    <penv python> -I tools/weatherscope/board/touch_cap_bench.py chain   <COMx> <board-ip> <out dir>
    <penv python> -I tools/weatherscope/board/touch_cap_bench.py clear   <COMx> <board-ip> <out dir>
    <penv python> -I tools/weatherscope/board/touch_cap_bench.py bootread <COMx> <board-ip> <out dir>

Resolve the port from the board's USB serial first (scripts/board-port.py), never by number, and the address from the
board's name. Each phase is ONE serial open with DTR and RTS false BEFORE it, never reopened (the USB-Serial-JTAG port
survives a soft reset, so the chain's restarts need no reopen). `clear` and `bootread` reset the board first with
esptool read-mac (no write) and open straight after it, so the buffered boot output is read in the same command.

chain (B0-B4, A12.119/A12.121/A12.122):
  B0  'r' at once: the stored run S (predicted 0). If S != 0: 'w' (alive), the bench's 180 s healthy reset writes 0,
      'w' (dead) again, and the chain starts there. T0 = the boot's start (S = 0) or that second 'w'.
  B4c T0 + 40 s: /diag/fb x3, the CONTROL -- the strip in 0 of 3.
  B1  'P' (a held bench press) at T0 + 60 s, 'L' at T0 + 140 s. The watchdog's "recommending reboot" line comes
      before 'L'; no reboot while the press is held; "touch reboot 1 of 3" 20.0-21.0 s after 'L'.
  B2  boots 2 and 3: "touch reboot k of 3" within 1.0 s of that boot's "recommending reboot"; each followed by
      "run k-1 -> k (NVS write: touch reboot)" and "restarting (cause SW_TOUCHWD stamped)".
  B3  boot 4: its boot line reads run=3 stored 3 "touch unavailable"; then no cap line and no restart for 360 s.
  B4  boot 4 + 40 s: /diag/fb x3 -- the strip in at least 2 of 3.
clear (B5): read-mac, open; boot 5's line reads run=3 stored 3, not power-on (POWERON -> B5 not run, recorded);
  'w' within 30 s; /diag/fb x3 -- the strip in at least 2 of 3; by device time 180 s, "run 3 -> 0 (NVS write: an hour
  without the wedge)" and "an hour without the wedge: leaving touch unavailable"; /diag/fb x3 -- the strip in 0 of 3;
  'r' reads run=0 stored=0 unavailable=0.
bootread (B6's boot read, after the clean image is written): read-mac, open; "[build] env=weatherscope-s3-128" and
  the "[wx] init" line's "theme=auto read=known".

THE STRIP ON /diag/fb (A12.122): the box is x 32-205, y 42-77 (A12.119). A frame SHOWS it when all 416 edge pixels are
one theme's uiWarn in RGB565 and the box's inside corner (33, 43) is that same theme's bg -- so the reading names its
theme itself, from the pixels, and needs no serial line or time. The frame is RGB565BE, 240 x 240.

Writes trace.txt (host-timestamped lines) and the frames to <out dir>. Prints no address and no network name.
Exit 0 every row AS PREDICTED, 1 a row OFF-PREDICTION, 2 the rig (the instrument could not run).
"""
import os, re, subprocess, sys, threading, time, urllib.request

import serial

N = 240
BOX = (32, 42, 174, 36)   # x, y, w, h (A12.119; test_touch_wedge_cap CR20)
# WxTheme.h's roles, RGB888: uiWarn and bg of each theme.
THEMES = {"dark": ((255, 176, 40), (0, 0, 0)), "light": ((143, 94, 0), (234, 231, 223))}
ESPTOOL = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")


def c565(rgb):
    r, g, b = rgb
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


def edges():
    x0, y0, w, h = BOX
    x1, y1 = x0 + w - 1, y0 + h - 1
    pts = {(x, y0) for x in range(x0, x1 + 1)} | {(x, y1) for x in range(x0, x1 + 1)}
    pts |= {(x0, y) for y in range(y0, y1 + 1)} | {(x1, y) for y in range(y0, y1 + 1)}
    return sorted(pts)


EDGES = edges()
assert len(EDGES) == 416, len(EDGES)


def strip_theme(frame):
    """The theme whose strip this frame shows, or None."""
    if len(frame) != N * N * 2:
        return None
    px = lambda x, y: frame[2 * (y * N + x)] << 8 | frame[2 * (y * N + x) + 1]
    for name, (warn, bg) in THEMES.items():
        if px(33, 43) == c565(bg) and all(px(x, y) == c565(warn) for x, y in EDGES):
            return name
    return None


class Trace:
    def __init__(self, port, out, name):
        self.lines, self.lock, self.stop = [], threading.Lock(), False
        self.f = open(os.path.join(out, f"trace-{name}.txt"), "w", encoding="utf-8", newline="\n")
        self.ser = serial.Serial()
        self.ser.port, self.ser.baudrate, self.ser.timeout = port, 115200, 0.2
        self.ser.dtr, self.ser.rts = False, False   # BEFORE the open: an open must not reset the board
        self.ser.open()
        self.t = threading.Thread(target=self.reader, daemon=True)
        self.t.start()

    def reader(self):
        buf = b""
        while not self.stop:
            try:
                buf += self.ser.read(4096)
            except Exception as e:
                self.add(f"#HOST read error {type(e).__name__}: the trace STOPS here (never reopened)")
                return
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                self.add(raw.decode("utf-8", "replace").rstrip("\r"))

    def add(self, line):
        now = time.time()
        with self.lock:
            self.lines.append((now, line))
            self.f.write(f"{now:.3f} {line}\n")
            self.f.flush()

    def note(self, text):
        self.add(text)
        print(text, flush=True)

    def send(self, b):
        self.note(f"#HOST send {b!r}")
        self.ser.write(b)
        self.ser.flush()

    def wait(self, pattern, since, timeout):
        rx, end = re.compile(pattern), time.time() + timeout
        while time.time() < end:
            with self.lock:
                for t, l in self.lines:
                    if t >= since and rx.search(l):
                        return t, l
            time.sleep(0.05)
        return None, None

    def between(self, pattern, t0, t1):
        rx = re.compile(pattern)
        with self.lock:
            return [(t, l) for t, l in self.lines if t0 <= t <= t1 and rx.search(l)]

    def close(self):
        self.stop = True
        self.t.join(timeout=2)
        self.ser.close()
        self.f.close()


def frames(tr, ip, out, label):
    """Three /diag/fb reads, 0.3 s apart. Returns the themes seen (None for a frame without the strip)."""
    seen = []
    for i in range(3):
        try:
            body = urllib.request.urlopen(f"http://{ip}/diag/fb", timeout=10).read()
        except Exception as e:
            tr.note(f"#HOST fb {label} {i}: ERROR {type(e).__name__}")
            seen.append("error")
            time.sleep(0.3)
            continue
        open(os.path.join(out, f"fb-{label}-{i}.bin"), "wb").write(body)
        th = strip_theme(body)
        seen.append(th)
        tr.note(f"#HOST fb {label} {i}: {len(body)} B, strip {th or 'absent'}")
        time.sleep(0.3)
    return seen


ROWS = []
RIGS = []


def row(name, ok, detail):
    ROWS.append((name, ok, detail))
    print(f"{name}: {'AS PREDICTED' if ok else 'OFF-PREDICTION'} -- {detail}", flush=True)


def rig(name, detail):
    """The instrument could not read the row: never a pass and never a red (A12.127)."""
    RIGS.append((name, detail))
    print(f"{name}: the RIG -- {detail}", flush=True)


def tally(seen):
    """(frames showing the strip, failed fetches). A failed fetch is not a frame and is never counted as one."""
    return sum(1 for x in seen if x not in (None, "error")), seen.count("error")


def until(t):
    while time.time() < t:
        time.sleep(0.05)


BENCH_R = r"\[touch-bench\] t=(\d+) run=(\d+) stored=(\d+) unavailable=(\d) wedgeSeen=(\d) chip=(\w+)"
REC = r"\[touch-wd\] wedge #\d+ has outlived \d+s; recommending reboot at next idle"
CAP_LINE = (r"\[touch-wd\] (wedged past the outage bound|wedged again|run \d+ -> \d+|restarting|reboot due, held|"
            r"an hour without the wedge|touch is back)")


def chain(port, ip, out):
    tr = Trace(port, out, "chain")
    try:
        t_open = time.time()
        tr.send(b"r")
        # 20 s: the edition reads serial only after its 4 s splash, so a reply just after a reset takes ~8 s.
        t, l = tr.wait(BENCH_R, t_open, 20)
        if t is None:
            tr.note("#HOST no [touch-bench] state line in 20 s: the RIG")
            return 2
        ms, run, stored = (int(x) for x in re.search(BENCH_R, l).groups()[:3])
        boot1 = t - ms / 1000.0
        row("B0 stored run", stored == 0, f"S={stored} (predicted 0); run={run}, device t={ms} ms")
        t0 = boot1
        if stored != 0:   # the planned prelude (A12.119): revive, let the 180 s reset write 0, kill again
            tr.send(b"w")
            tz, _ = tr.wait(r"\[touch-wd\] run \d+ -> 0 \(NVS write: an hour without the wedge\)", t, 200)
            if tz is None:
                tr.note("#HOST the prelude's healthy reset never came: the RIG")
                return 2
            tr.send(b"w")
            t0 = time.time()
        until(t0 + 40)
        ctl = frames(tr, ip, out, "boot1-control")
        nc, ne = tally(ctl)
        if ne:
            rig("B4c CONTROL", f"{ne} of 3 fetches failed: no reading")
        else:
            row("B4c CONTROL", nc == 0, f"the strip in {nc} of 3 boot-1 frames (predicted 0)")
        until(t0 + 60)
        tr.send(b"P")
        t_p = time.time()
        until(t0 + 140)
        tr.send(b"L")
        t_l = time.time()
        t_rec, _ = tr.wait(REC, t0, 1)
        t_r1, _ = tr.wait(r"touch reboot 1 of 3", t_p, 40)
        early = tr.between(r"touch reboot 1 of 3", t_p, t_l)
        ok = t_rec is not None and t_rec < t_l and not early and t_r1 is not None and 20.0 <= t_r1 - t_l <= 21.0
        row("B1 idle rule", ok, f"recommend at T0+{(t_rec - t0) if t_rec else float('nan'):.1f} s (before L: "
            f"{t_rec is not None and t_rec < t_l}); reboot while held: {bool(early)}; reboot 1 at L+"
            f"{(t_r1 - t_l) if t_r1 else float('nan'):.2f} s (predicted 20.0-21.0)")
        if t_r1 is None:
            return 1
        t_prev = t_r1
        for k in (1, 2, 3):
            if k > 1:
                tb, _ = tr.wait(rf"\[touch-wd\] boot: run={k - 1} \(stored {k - 1}, reset not power-on\)$", t_prev, 30)
                tk_rec, _ = tr.wait(REC, tb or t_prev, 150)
                tk, _ = tr.wait(rf"touch reboot {k} of 3", tk_rec or t_prev, 150)
                ok = tb is not None and tk_rec is not None and tk is not None and 0 <= tk - tk_rec <= 1.0
                row(f"B2 reboot {k}", ok, f"boot line {'seen' if tb else 'MISSING'}; reboot {k} at recommend+"
                    f"{(tk - tk_rec) if (tk and tk_rec) else float('nan'):.2f} s (predicted <= 1.0)")
                if tk is None:
                    return 1
                t_prev = tk
            w, _ = tr.wait(rf"\[touch-wd\] run {k - 1} -> {k} \(NVS write: touch reboot\)$", t_prev, 5)
            s, _ = tr.wait(r"\[touch-wd\] restarting \(cause SW_TOUCHWD stamped\)$", t_prev, 5)
            row(f"B2 write+stamp {k}", w is not None and s is not None, f"run {k - 1} -> {k} {'seen' if w else 'MISSING'}, "
                f"restarting {'seen' if s else 'MISSING'}")
        t4, _ = tr.wait(r"\[touch-wd\] boot: run=3 \(stored 3, reset not power-on\) -- touch unavailable, no touch reboots"
                        r" this boot$", t_prev, 30)
        if t4 is None:
            row("B3 the cap holds", False, "boot 4's unavailable boot line MISSING")
            return 1
        until(t4 + 40)
        b4 = frames(tr, ip, out, "boot4")
        n4, ne = tally(b4)
        if ne:
            rig("B4 the strip on glass", f"{ne} of 3 fetches failed: no reading")
        else:
            row("B4 the strip on glass", n4 >= 2, f"the strip in {n4} of 3 boot-4 frames ({', '.join(map(str, b4))};"
                f" predicted >= 2)")
        until(t4 + 360)
        rec4 = tr.between(REC, t4, t4 + 360)
        cap4 = tr.between(CAP_LINE, t4, t4 + 360)
        boots = tr.between(r"\[touch-wd\] boot:|\[build\] env=", t4 + 1, t4 + 360)
        row("B3 the cap holds", rec4 != [] and cap4 == [] and boots == [],
            f"recommend {'seen' if rec4 else 'MISSING'}; cap lines after the boot line {len(cap4)} (predicted 0); "
            f"restarts {len(boots)} (predicted 0) in 360 s")
        return 0 if all(ok for _, ok, _ in ROWS) else 1
    finally:
        tr.close()


def read_mac(port, out, name):
    py = sys.executable
    r = subprocess.run([py, ESPTOOL, "--port", port, "read_mac"], capture_output=True, text=True)
    open(os.path.join(out, f"read-mac-{name}.log"), "w").write(r.stdout + r.stderr)
    return r.returncode


def clear(port, ip, out):
    if read_mac(port, out, "clear") != 0:
        print("read-mac failed: the RIG")
        return 2
    tr = Trace(port, out, "clear")
    try:
        t_open = time.time()
        tb, l = tr.wait(r"\[touch-wd\] boot: run=\d+ \(stored \d+, reset (POWERON|not power-on)\)", t_open - 1, 20)
        if tb is None:
            tr.note("#HOST no boot line within 20 s of the open: the RIG")
            return 2
        if "POWERON" in l:
            row("B5 how it clears", False, "boot 5 read POWERON: the run reset at boot, no strip -- B5 NOT RUN (A12.119)")
            return 1
        row("B5 boot 5", "run=3 (stored 3, reset not power-on) -- touch unavailable" in l, l.split("] ", 1)[1])
        tr.send(b"w")
        tr.send(b"r")
        t, l = tr.wait(BENCH_R, t_open, 20)   # answered after the splash, as in chain
        if t is None:
            tr.note("#HOST no [touch-bench] state line: the RIG")
            return 2
        boot5 = t - int(re.search(BENCH_R, l).group(1)) / 1000.0
        row("B5 'w' in time", t - boot5 <= 30, f"'w' at device t={(t - boot5):.1f} s (predicted <= 30)")
        until(boot5 + 40)
        before = frames(tr, ip, out, "boot5-before")
        nb, ne = tally(before)
        if ne:
            rig("B5 strip after 'w'", f"{ne} of 3 fetches failed: no reading")
        else:
            row("B5 strip after 'w'", nb >= 2, f"the strip in {nb} of 3 frames (predicted >= 2)")
        t_run, _ = tr.wait(r"\[touch-wd\] run 3 -> 0 \(NVS write: an hour without the wedge\)$", t_open, boot5 + 200 - time.time())
        t_leave, _ = tr.wait(r"\[touch-wd\] an hour without the wedge: leaving touch unavailable$", t_open, 5)
        ok = t_run is not None and t_leave is not None and t_run - boot5 >= 179.0
        row("B5 the reset", ok, f"run 3 -> 0 at device t={(t_run - boot5) if t_run else float('nan'):.1f} s (predicted >= 180); "
            f"leaving line {'seen' if t_leave else 'MISSING'}")
        time.sleep(2)
        after = frames(tr, ip, out, "boot5-after")
        na, ne = tally(after)
        if ne:
            rig("B5 strip cleared", f"{ne} of 3 fetches failed: no reading")
        else:
            row("B5 strip cleared", na == 0, f"the strip in {na} of 3 frames (predicted 0)")
        t_r = time.time()
        tr.send(b"r")
        t, l = tr.wait(BENCH_R, t_r, 5)
        m = re.search(BENCH_R, l or "")
        row("B5 'r'", m is not None and m.group(2) == "0" and m.group(3) == "0" and m.group(4) == "0",
            (l or "no line").split("] ", 1)[-1])
        return 0 if all(ok for _, ok, _ in ROWS) else 1
    finally:
        tr.close()


def bootread(port, ip, out):
    if read_mac(port, out, "bootread") != 0:
        print("read-mac failed: the RIG")
        return 2
    tr = Trace(port, out, "bootread")
    try:
        t_open = time.time()
        tb, lb = tr.wait(r"\[build\] env=(\S+)", t_open - 1, 20)
        ti, li = tr.wait(r"\[wx\] init; ", t_open - 1, 20)
        env_ok = lb is not None and "env=weatherscope-s3-128" in lb and "touchbench" not in lb
        theme_ok = li is not None and " theme=auto read=known" in li
        row("B6 the clean image", env_ok, f"build line {'env=weatherscope-s3-128' if env_ok else repr(lb)}")
        row("B6 the stored theme", theme_ok, "theme=auto read=known" if theme_ok else f"init line {li!r}")
        time.sleep(3)
        return 0 if env_ok and theme_ok else 1
    finally:
        tr.close()


if __name__ == "__main__":
    if len(sys.argv) != 5 or sys.argv[1] not in ("chain", "clear", "bootread"):
        print(__doc__)
        sys.exit(2)
    phase, port, ip, out = sys.argv[1:]
    # An empty address made every /diag/fb fetch fail in W-CR's first chain (A12.127): refuse it before any open.
    if phase != "bootread" and not re.fullmatch(r"\d{1,3}(\.\d{1,3}){3}", ip):
        print("REFUSED: the board address is not an IPv4 address (resolve the name first): the RIG")
        sys.exit(2)
    os.makedirs(out, exist_ok=True)
    rc = {"chain": chain, "clear": clear, "bootread": bootread}[phase](port, ip, out)
    if RIGS and rc == 0:
        rc = 2   # a row the instrument could not read is never a pass
    if RIGS:
        print(f"the RIG: {len(RIGS)} row(s) unread -- {', '.join(n for n, _ in RIGS)}")
    sys.exit(rc)
