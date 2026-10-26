"""3.3d's board run (docs/weatherscope-stage3.md A8.20): B1, B2 and B3 in ONE serial open, never reopened.

    <penv python> -I tools/weatherscope/board/run_b.py <COMx> <board-ip> <empty out dir>

Resolve the port from the board's USB serial first (scripts/board-port.py weatherscope --upload), never by number,
and the address from its name (weatherscope-326e64.local). Run with PlatformIO's penv python (it has pyserial).

THE SEQUENCE, fixed here:
  open the port (DTR and RTS false BEFORE the open); log every line, host-timestamped, to trace.txt (and the bare
  lines to serial.txt); nothing is ever written to the port except the two gesture bytes below.
  B1  as each scenario's "[wx] screen=scenario <name>" line arrives, 2 s later capture its snapshot; until all four
      (dry, approaching, herenow, warning) are captured, or 180 s.
  B2  at the next scenario start, 2 s later send 'T'; wait for "[wx] screen=loop" and then "[wx] screen=scenario"
      (10 s); 0.5 s later capture "postloop".
  B3  send 'S'; wait for "[wx] screen=legend" (5 s); 1 s later capture "legend". Send 'T'; wait for
      "[wx] screen=scenario" (5 s); 0.5 s later capture "postlegend".
  close the port. Then the board is proven on the network (TCP :80, A7) within 60 s; this tool reports, it does not
  recover -- a drop is recovered by hand with read-mac (no writes) and recorded.

THE SNAPSHOT RULES (A8.19 item 3 + A8.20), per capture: up to 6 fetches of /diag/fb, 0.3 s apart. A fetch with
Torn: true is recorded and refetched; the 3rd torn one on a capture is a FAIL. The graded snapshot is the first
untorn one byte-identical to the NEXT untorn one; no agreeing pair in 6 fetches is a FAIL of the instrument.
Every fetch is saved with its headers (captures.json). Prints no coordinate.
"""
import hashlib, json, os, re, socket, sys, threading, time, urllib.request

import serial

SCENARIOS = ["dry", "approaching", "herenow", "warning"]


class Trace:
    def __init__(self, port, out):
        self.lines, self.lock, self.stop = [], threading.Lock(), False
        self.f_trace = open(os.path.join(out, "trace.txt"), "w", encoding="utf-8", newline="\n")
        self.f_serial = open(os.path.join(out, "serial.txt"), "w", encoding="utf-8", newline="\n")
        self.ser = serial.Serial()
        self.ser.port, self.ser.baudrate, self.ser.timeout = port, 115200, 0.2
        self.ser.dtr, self.ser.rts = False, False
        self.ser.open()
        self.t = threading.Thread(target=self.read, daemon=True)
        self.t.start()

    def read(self):
        buf = b""
        while not self.stop:
            try:
                chunk = self.ser.read(4096)
            except Exception as e:   # a read error is a fact about the board: stop and say so (never reopen)
                self.note(f"#RIG read error: {type(e).__name__}: {e}")
                return
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.add(line.decode("utf-8", errors="replace").rstrip("\r"))

    def add(self, line):
        ts = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime()) + f".{int(time.time() * 1000) % 1000:03d}Z"
        with self.lock:
            self.lines.append((time.time(), line))
            self.f_trace.write(f"{ts} {line}\n")
            self.f_trace.flush()
            self.f_serial.write(line + "\n")
            self.f_serial.flush()

    def note(self, text):
        self.add(text)
        print(text)

    def send(self, b):
        self.note(f"#HOST send {b!r}")
        self.ser.write(b)
        self.ser.flush()

    def wait(self, pattern, since, timeout):
        rx, t_end = re.compile(pattern), time.time() + timeout
        while time.time() < t_end:
            with self.lock:
                for t, l in self.lines:
                    if t >= since and rx.search(l):
                        return t, l
            time.sleep(0.05)
        return None, None

    def close(self):
        self.stop = True
        self.t.join(timeout=2)
        self.ser.close()
        self.f_trace.close()
        self.f_serial.close()


def fetch(ip):
    r = urllib.request.urlopen(f"http://{ip}/diag/fb", timeout=10)
    body = r.read()
    h = {k: r.headers.get(k) for k in ("X-Blipscope-Frame-Width", "X-Blipscope-Frame-Height",
                                        "X-Blipscope-Frame-Format", "X-Blipscope-Frame-Torn", "X-Blipscope-Frame-Seq")}
    return body, h


def capture(tr, ip, out, label):
    fetches, prev_untorn, torn = [], None, 0
    for i in range(6):
        try:
            body, h = fetch(ip)
        except Exception as e:
            fetches.append({"i": i, "error": f"{type(e).__name__}: {e}"})
            tr.note(f"#HOST fetch {label} {i}: ERROR {type(e).__name__}")
            time.sleep(0.3)
            continue
        path = os.path.join(out, f"snap-{label}-{i}.bin")
        open(path, "wb").write(body)
        rec = {"i": i, "file": os.path.basename(path), "bytes": len(body), "sha256": hashlib.sha256(body).hexdigest(),
               "headers": h, "utc": time.time()}
        fetches.append(rec)
        tr.note(f"#HOST fetch {label} {i}: {len(body)} B torn={h['X-Blipscope-Frame-Torn']} "
                f"seq={h['X-Blipscope-Frame-Seq']} sha {rec['sha256'][:12]}")
        if h["X-Blipscope-Frame-Torn"] != "false":
            torn += 1
            if torn == 3:
                return {"label": label, "verdict": "FAIL-torn-3", "graded": None, "fetches": fetches}
        else:
            if prev_untorn is not None and prev_untorn["sha256"] == rec["sha256"]:
                return {"label": label, "verdict": "graded", "graded": prev_untorn["file"], "fetches": fetches}
            prev_untorn = rec
        time.sleep(0.3)
    return {"label": label, "verdict": "FAIL-no-agreeing-pair", "graded": None, "fetches": fetches}


def tcp(ip):
    try:
        with socket.create_connection((ip, 80), timeout=1.5):
            return True
    except OSError:
        return False


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    port, ip, out = argv[1], argv[2], argv[3]
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    caps = []
    tr = Trace(port, out)
    tr.note(f"#HOST opened {port}; board {ip}")
    try:
        # B1
        t_start, seen = time.time(), set()
        while len(seen) < 4 and time.time() - t_start < 180:
            t, l = tr.wait(r"^\[wx\] screen=scenario (\w+)$", max(t_start, getattr(tr, "_last", 0)), 180)
            if not l:
                break
            tr._last = t + 0.001
            name = l.split()[-1]
            if name in seen:
                continue
            time.sleep(2)
            caps.append(dict(capture(tr, ip, out, name), step="B1", scenario=name))
            seen.add(name)
        tr.note(f"#HOST B1 captured {sorted(seen)}")
        # B2
        t, l = tr.wait(r"^\[wx\] screen=scenario (\w+)$", getattr(tr, "_last", time.time()), 60)
        if l:
            time.sleep(2)
            tr.send(b"T")
            t0 = time.time()
            tl, _ = tr.wait(r"^\[wx\] screen=loop$", t0, 10)
            tb, lb = tr.wait(r"^\[wx\] screen=scenario (\w+)$", tl or t0, 10)
            if lb:
                time.sleep(0.5)
                caps.append(dict(capture(tr, ip, out, "postloop"), step="B2", scenario=lb.split()[-1]))
            # B3
            t1 = time.time()
            tr.send(b"S")
            tg, _ = tr.wait(r"^\[wx\] screen=legend$", t1, 5)
            if tg:
                time.sleep(1)
                caps.append(dict(capture(tr, ip, out, "legend"), step="B3"))
                t2 = time.time()
                tr.send(b"T")
                tb2, lb2 = tr.wait(r"^\[wx\] screen=scenario (\w+)$", t2, 5)
                if lb2:
                    time.sleep(0.5)
                    caps.append(dict(capture(tr, ip, out, "postlegend"), step="B3", scenario=lb2.split()[-1]))
    finally:
        tr.note("#HOST closing the port")
        tr.close()
    json.dump(caps, open(os.path.join(out, "captures.json"), "w"), indent=1)
    up_at = None
    t0 = time.time()
    while time.time() - t0 < 60:
        if tcp(ip):
            up_at = time.time() - t0
            break
        time.sleep(2)
    after = {"tcp_up_within_s": None if up_at is None else round(up_at, 1)}
    json.dump(after, open(os.path.join(out, "after-close.json"), "w"))
    print(f"captures: {[(c['label'], c['verdict']) for c in caps]}")
    print(f"after the close: TCP :80 {'up after %.1f s' % up_at if up_at is not None else 'DOWN for 60 s -- recover with read-mac (no writes) and record it'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
