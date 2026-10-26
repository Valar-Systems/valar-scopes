"""3.5a's board run (docs/weatherscope-stage3.md A9.10 B, A9.19, A9.22): B1, BZ, B2 (twice) and B3 in ONE serial open,
never reopened.

    <penv python> -I tools/weatherscope/board/run_b35.py <COMx> <board host> <empty out dir>

Resolve the port from the board's USB serial first (scripts/board-port.py weatherscope --upload), never by number,
and pass the board's mDNS name, so no LAN address enters the trace. Run with PlatformIO's penv python (pyserial).
The trace and the TCP check are run_b.py's own (3.3d), imported.

THE SEQUENCE (A9.19, as A9.22 amends it):
  open the port (DTR and RTS false BEFORE the open); log every line, host-timestamped; nothing is written to the port
  but the gesture bytes below. THE 1 s RULE: act only on a screen line that arrives at least 1 s after the open.
  B1  as each scenario's "[wx] screen=scenario <name> R=60" line arrives, 2 s later capture it; all six, or 240 s.
  BZ  in blocks, each starting at the board's "dry R=<R>" line, the first byte 1 s after it: U U U D from dry at 60,
      then D H H from dry at 30. A step is: the byte, its screen line (5 s), 0.5 s, a capture. Budget 5.2 s a step
      (0.3 s to the line, 0.5 s, 4 fetches at 1.1 s); a block that would not fit the 25 s left of its dry hold after
      the 5 s spare waits for the next dry.
  B2  at the next scenario start (R=60), 2 s later 'T'; the loop and its end; 0.5 s later capture "postloop". Then at
      the next "warning R=60", 2 s later 'T'; capture "postloop-warning".
  B3  'S'; the legend; 1 s later capture "legend"; 'T'; the screen line; 0.5 s later capture "postlegend".
  close; TCP :80 within 60 s is reported, not recovered.

THE SNAPSHOT RULE (A8.19 + A9.22): up to 6 fetches of /diag/fb, 0.3 s apart; the 3rd torn is a FAIL; graded = the
first untorn fetch byte-identical to the NEXT untorn one. NO CAP on fetches below six (A9.21 item 2). Each fetch keeps
its start and end times and the diag headers (phase, tag, buffer). Whether a capture is BELIEVED (its own scene at both
ends) is the grader's, from the trace. Prints no coordinate.
"""
import hashlib, importlib.util, json, os, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("run_b", os.path.join(HERE, "run_b.py"))
rb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rb)

SCENARIOS = ["dry", "approaching", "herenow", "warning", "stale", "clear"]
BZ_BLOCKS = [(60, [("U", 30), ("U", 15), ("U", 15), ("D", 30)]), (30, [("D", 60), ("H", 15), ("H", 60)])]
STEP_BUDGET_S, HOLD_S, SPARE_S = 5.2, 30.0, 5.0
SCREEN = r"^\[wx\] screen=scenario (\w+) R=(\d+)$"
HEADERS = ("X-Blipscope-Frame-Width", "X-Blipscope-Frame-Height", "X-Blipscope-Frame-Format", "X-Blipscope-Frame-Torn",
           "X-Blipscope-Frame-Seq", "X-Blipscope-Frame-Phase", "X-Blipscope-Frame-Tag", "X-Blipscope-Frame-Buffer",
           "X-Blipscope-Frame-Finished", "X-Blipscope-Frame-Wait-Us", "X-Blipscope-Frame-Attempts")


def parse(line):
    p = line.split()
    return p[2], int(p[3][2:])


def capture(tr, host, out, label):
    fetches, prev_untorn, torn = [], None, 0
    for i in range(6):
        t0 = time.time()
        try:
            r = urllib.request.urlopen(f"http://{host}/diag/fb", timeout=10)
            body = r.read()
            h = {k: r.headers.get(k) for k in HEADERS}
        except Exception as e:
            fetches.append({"i": i, "start": t0, "end": time.time(), "error": f"{type(e).__name__}: {e}"})
            tr.note(f"#HOST fetch {label} {i}: ERROR {type(e).__name__}")
            time.sleep(0.3)
            continue
        t1 = time.time()
        path = os.path.join(out, f"snap-{label}-{i}.bin")
        open(path, "wb").write(body)
        rec = {"i": i, "file": os.path.basename(path), "bytes": len(body), "sha256": hashlib.sha256(body).hexdigest(),
               "headers": h, "start": t0, "end": t1, "utc": t1}
        fetches.append(rec)
        tr.note(f"#HOST fetch {label} {i}: {len(body)} B torn={h['X-Blipscope-Frame-Torn']} seq={h['X-Blipscope-Frame-Seq']} "
                f"phase={h['X-Blipscope-Frame-Phase']} tag={h['X-Blipscope-Frame-Tag']} finished={h['X-Blipscope-Frame-Finished']} "
                f"wait={h['X-Blipscope-Frame-Wait-Us']} sha {rec['sha256'][:12]}")
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


def wait_scene(tr, since, want, timeout):
    """The next screen line equal to `want` (name, R), from `since`; returns (t, line) or (None, None)."""
    t_end = time.time() + timeout
    while time.time() < t_end:
        t, l = tr.wait(SCREEN, since, t_end - time.time())
        if not l:
            return None, None
        since = t + 0.001
        if parse(l) == want:
            return t, l
    return None, None


def loop_and_capture(tr, host, out, label, caps):
    t0 = time.time()
    tr.send(b"T")
    tl, _ = tr.wait(r"^\[wx\] screen=loop R=60$", t0, 5)
    tb, lb = tr.wait(SCREEN, tl or t0, 10)
    if not lb:
        tr.note(f"#HOST {label}: the loop did not end in 10 s")
        return tb
    name, R = parse(lb)
    time.sleep(0.5)
    caps.append(dict(capture(tr, host, out, label), step="B2", scenario=name, R=R))
    return tb


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    port, host, out = argv[1], argv[2], argv[3]
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    caps = []
    tr = rb.Trace(port, out)
    t_open = time.time()
    tr.note(f"#HOST opened {port}; board {host}; acting only on screen lines from t_open + 1 s")
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
            caps.append(dict(capture(tr, host, out, name), step="B1", scenario=name, R=R))
            seen.add(name)
        tr.note(f"#HOST B1 captured {sorted(seen)}")
        # BZ, in blocks
        k = 0
        for startR, steps in BZ_BLOCKS:
            budget = 1.0 + STEP_BUDGET_S * len(steps)
            while True:
                t, l = wait_scene(tr, last, ("dry", startR), 400)
                if not l:
                    break
                last = t + 0.001
                left = (t + HOLD_S - SPARE_S) - time.time()
                if left >= budget:
                    break
                tr.note(f"#HOST BZ block from dry R={startR}: {left:.1f} s left < {budget:.1f} s budget; waiting for the next dry")
            if not l:
                tr.note(f"#HOST BZ block from dry R={startR}: no dry in 400 s")
                break
            tr.note(f"#HOST BZ block from dry R={startR} starts; budget {budget:.1f} s")
            time.sleep(max(0.0, t + 1.0 - time.time()))
            for b, want in steps:
                t0 = time.time()
                tr.send(b.encode())
                ts, ls = tr.wait(SCREEN, t0, 5)
                if not ls:
                    tr.note(f"#HOST BZ {k} {b}: no screen line in 5 s")
                    caps.append({"label": f"bz{k}", "verdict": "FAIL-no-screen-line", "graded": None, "fetches": [],
                                 "step": "BZ", "gesture": b, "wantR": want})
                    k += 1
                    continue
                last = ts + 0.001
                name, R = parse(ls)
                time.sleep(0.5)
                caps.append(dict(capture(tr, host, out, f"bz{k}"), step="BZ", gesture=b, wantR=want, scenario=name, R=R))
                k += 1
        # B2, twice
        t, l = tr.wait(SCREEN, last, 60)
        if l and parse(l)[1] == 60:
            last = t + 0.001
            time.sleep(2)
            last = (loop_and_capture(tr, host, out, "postloop", caps) or time.time()) + 0.001
        t, l = wait_scene(tr, last, ("warning", 60), 240)
        if l:
            last = t + 0.001
            time.sleep(2)
            last = (loop_and_capture(tr, host, out, "postloop-warning", caps) or time.time()) + 0.001
        # B3
        t1 = time.time()
        tr.send(b"S")
        tg, _ = tr.wait(r"^\[wx\] screen=legend$", t1, 5)
        if tg:
            time.sleep(1)
            caps.append(dict(capture(tr, host, out, "legend"), step="B3"))
            t2 = time.time()
            tr.send(b"T")
            tb2, lb2 = tr.wait(SCREEN, t2, 5)
            if lb2:
                name, R = parse(lb2)
                time.sleep(0.5)
                caps.append(dict(capture(tr, host, out, "postlegend"), step="B3", scenario=name, R=R))
    finally:
        tr.note("#HOST closing the port")
        tr.close()
    json.dump(caps, open(os.path.join(out, "captures.json"), "w"), indent=1)
    # every line's receive time on the runner's float clock, for the grader's belief rule (the printed stamps read
    # seconds and milliseconds at two instants)
    json.dump([[tt, ll] for tt, ll in tr.lines], open(os.path.join(out, "lines.json"), "w"))
    up_at = None
    t0 = time.time()
    while time.time() - t0 < 60:
        if rb.tcp(host):
            up_at = time.time() - t0
            break
        time.sleep(2)
    json.dump({"tcp_up_within_s": None if up_at is None else round(up_at, 1)}, open(os.path.join(out, "after-close.json"), "w"))
    print(f"captures: {[(c['label'], c['verdict']) for c in caps]}")
    print(f"after the close: TCP :80 {'up after %.1f s' % up_at if up_at is not None else 'DOWN for 60 s -- recover with read-mac (no writes) and record it'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
