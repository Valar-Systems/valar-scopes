"""3.5a's board run (docs/weatherscope-stage3.md A9.10 B, A9.19): B1, BZ, B2 and B3 in ONE serial open, never reopened.

    <penv python> -I tools/weatherscope/board/run_b35.py <COMx> <board-ip> <empty out dir>

Resolve the port from the board's USB serial first (scripts/board-port.py weatherscope --upload), never by number,
and the address from its name. Run with PlatformIO's penv python (it has pyserial). The trace, the snapshot pair rule
and the TCP check are run_b.py's own (3.3d), imported, not copied.

THE SEQUENCE, fixed here (A9.19):
  open the port (DTR and RTS false BEFORE the open); log every line, host-timestamped; nothing is ever written to the
  port except the gesture bytes below. THE 1 s RULE (A9.10): the runner acts only on a screen line that arrives at
  least 1 s after the open -- a USB-Serial-JTAG board replays its buffered boot output in one burst at the open.
  B1  as each scenario's "[wx] screen=scenario <name> R=60" line arrives, 2 s later capture its snapshot; until all six
      (dry, approaching, herenow, warning, stale, clear) are captured, or 240 s.
  BZ  at the next "dry R=60" start, 1 s later: U, U, U, D, D, H, H. After each byte, wait for the next screen line
      (5 s), 0.5 s later capture "bz<k>". The expected radii are 30, 15, 15 (the edge), 30, 60, 15, 60.
  B2  at the next scenario start (R=60), 2 s later send 'T'; wait for "[wx] screen=loop R=60" (5 s) and then the
      screen line (10 s); 0.5 s later capture "postloop".
  B3  send 'S'; wait for "[wx] screen=legend" (5 s); 1 s later capture "legend". Send 'T'; wait for the screen line
      (5 s); 0.5 s later capture "postlegend".
  close the port; then whether the board is on the network (TCP :80) within 60 s is REPORTED, not recovered: a close
  has put this board in ROM download mode before (2026-10-22), and that is recovered by hand with read-mac (no
  writes) and recorded. Prints no coordinate.
"""
import importlib.util, json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("run_b", os.path.join(HERE, "run_b.py"))
rb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rb)

SCENARIOS = ["dry", "approaching", "herenow", "warning", "stale", "clear"]
BZ = [("U", 30), ("U", 15), ("U", 15), ("D", 30), ("D", 60), ("H", 15), ("H", 60)]
SCREEN = r"^\[wx\] screen=scenario (\w+) R=(\d+)$"


def parse(line):
    p = line.split()
    return p[2], int(p[3][2:])


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    port, ip, out = argv[1], argv[2], argv[3]
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    caps = []
    tr = rb.Trace(port, out)
    t_open = time.time()
    tr.note(f"#HOST opened {port}; board {ip}; acting only on screen lines from t_open + 1 s")
    last = t_open + 1.0
    try:
        # B1
        seen = set()
        while len(seen) < 6 and time.time() - t_open < 240:
            t, l = tr.wait(SCREEN, last, 240 - (time.time() - t_open))
            if not l:
                break
            last = t + 0.001
            name, R = parse(l)
            if name in seen or R != 60:
                continue
            time.sleep(2)
            caps.append(dict(rb.capture(tr, ip, out, name), step="B1", scenario=name, R=R))
            seen.add(name)
        tr.note(f"#HOST B1 captured {sorted(seen)}")
        # BZ, on dry
        t, l = None, None
        while True:
            t, l = tr.wait(SCREEN, last, 240)
            if not l:
                break
            last = t + 0.001
            if parse(l) == ("dry", 60):
                break
        if l:
            tr.note("#HOST BZ starts on dry")
            time.sleep(1)
            for k, (b, want) in enumerate(BZ):
                t0 = time.time()
                tr.send(b.encode())
                t, l = tr.wait(SCREEN, t0, 5)
                if not l:
                    tr.note(f"#HOST BZ {k} {b}: no screen line in 5 s")
                    caps.append({"label": f"bz{k}", "verdict": "FAIL-no-screen-line", "graded": None, "fetches": [],
                                 "step": "BZ", "gesture": b, "wantR": want})
                    continue
                last = t + 0.001
                name, R = parse(l)
                time.sleep(0.5)
                caps.append(dict(rb.capture(tr, ip, out, f"bz{k}"), step="BZ", gesture=b, wantR=want, scenario=name, R=R))
        # B2
        t, l = tr.wait(SCREEN, last, 60)
        if l:
            last = t + 0.001
            time.sleep(2)
            t0 = time.time()
            tr.send(b"T")
            tl, _ = tr.wait(r"^\[wx\] screen=loop R=60$", t0, 5)
            tb, lb = tr.wait(SCREEN, tl or t0, 10)
            if lb:
                last = tb + 0.001
                name, R = parse(lb)
                time.sleep(0.5)
                caps.append(dict(rb.capture(tr, ip, out, "postloop"), step="B2", scenario=name, R=R))
            # B3
            t1 = time.time()
            tr.send(b"S")
            tg, _ = tr.wait(r"^\[wx\] screen=legend$", t1, 5)
            if tg:
                time.sleep(1)
                caps.append(dict(rb.capture(tr, ip, out, "legend"), step="B3"))
                t2 = time.time()
                tr.send(b"T")
                tb2, lb2 = tr.wait(SCREEN, t2, 5)
                if lb2:
                    name, R = parse(lb2)
                    time.sleep(0.5)
                    caps.append(dict(rb.capture(tr, ip, out, "postlegend"), step="B3", scenario=name, R=R))
    finally:
        tr.note("#HOST closing the port")
        tr.close()
    json.dump(caps, open(os.path.join(out, "captures.json"), "w"), indent=1)
    up_at = None
    t0 = time.time()
    while time.time() - t0 < 60:
        if rb.tcp(ip):
            up_at = time.time() - t0
            break
        time.sleep(2)
    json.dump({"tcp_up_within_s": None if up_at is None else round(up_at, 1)}, open(os.path.join(out, "after-close.json"), "w"))
    print(f"captures: {[(c['label'], c['verdict']) for c in caps]}")
    print(f"after the close: TCP :80 {'up after %.1f s' % up_at if up_at is not None else 'DOWN for 60 s -- recover with read-mac (no writes) and record it'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
