"""3.5c step 7's board run (docs/weatherscope-stage3.md A12.165): the swipe screens on the glass, in ONE serial open (DTR and
RTS false, never reopened), on the product image.

    <penv python> -I tools/weatherscope/board/run_b35c.py <COMx> <board-ip> <empty out dir>
    <penv python> -I tools/weatherscope/board/run_b35c.py --selftest

Navigation is driven by the bytes N (left swipe) and V (right swipe) only, the path a touch takes (Q107). S appears in one
row of its own, which shows the bench shortcut still reaches the legend. Every frame is /diag/fb, read until two untorn
fetches agree (run_b35.capture), and held to test/fixtures/wx/nws/k35c.ref.txt by sha256 of the big-endian RGB565 frame: the
same 57,600 pixels the host's K rows hash. A failed fetch is the instrument, never a frame.

The first row off ends the run, and every path leaves the stored theme at auto for the close. Prints no coordinate and no
board address; the committed record keeps no time of day. Exit 0 every row as predicted; 1 a row off; 2 the rig.
"""
import importlib.util, os, re, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
REF = os.path.join(ROOT, "test", "fixtures", "wx", "nws", "k35c.ref.txt")
sys.dont_write_bytecode = True


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


SCENARIO = r"^\[wx\] screen=scenario \w+ R=\d+$"
PAGE = {"forecast": r"^\[wx\] screen=forecast$", "conditions": r"^\[wx\] screen=conditions$", "alerts": r"^\[wx\] screen=alerts$",
        "legend": r"^\[wx\] screen=legend$", "map": SCENARIO}
ANY_SCREEN = r"^\[wx\] screen="
ROW_OF = {"forecast": "forecast-capture", "conditions": "conditions-day", "alerts": "alerts-SYNTHETIC"}
IDLE_S, IDLE_SLACK_S = 600.0, 5.0
OTA_WANT = "current=18 latest=18"   # A12.165: a higher latest means this board would update itself mid-run
ROWS = []
STOP = False   # run() sets it: a row off ends the run. The selftest leaves it off to count rows past a failure


class Off(Exception):
    pass


def row(name, ok, detail=""):
    ROWS.append((name, bool(ok), detail))
    print(f"{'ok ' if ok else 'OFF'} {name}  {detail}", flush=True)
    if STOP and not ok:
        raise Off(name)


def ota_ok(line, failed):
    """Q103: the version check answered through GetSecure, with exactly the versions A12.165 froze."""
    return line is not None and line.endswith(OTA_WANT) and not failed


def read_ref():
    ref = {}
    for line in open(REF, encoding="ascii"):
        p = line.split()
        if len(p) == 3 and p[0] == "ROW":
            ref[p[1]] = p[2]
    return ref


def go(tr, byte, want, timeout=4.0):
    """Sends one gesture byte; True when `want`'s screen line follows."""
    t = time.time()
    tr.send(byte)
    return tr.wait(PAGE[want], t, timeout)[0] is not None


def stays(tr, byte, quiet=3.0):
    """Sends one gesture byte; True when no screen line follows within `quiet` seconds."""
    t = time.time()
    tr.send(byte)
    time.sleep(quiet)
    return not tr.between(ANY_SCREEN, t, time.time())


def walk(tr, ip, out, capture, ref, theme):
    """N1 and N2 for one theme: forward to the legend, the end stop, back to the map, the other end stop."""
    good = True
    for page in ("forecast", "conditions", "alerts"):
        reached = go(tr, b"N", page)
        good &= reached
        time.sleep(0.5)
        c = capture(tr, ip, out, f"{theme}-{page}")
        want = ref[f"{ROW_OF[page]}-{theme}"]
        got = next((f["sha256"] for f in c["fetches"] if f.get("file") == c["graded"]), None)
        row(f"K-glass {page} {theme}", reached and c["verdict"] == "graded" and got == want,
            f"{c['verdict']}; frame {(got or 'none')[:12]}, want {want[:12]}")
    good &= go(tr, b"N", "legend")
    end = stays(tr, b"N")
    for page in ("alerts", "conditions", "forecast", "map"):
        good &= go(tr, b"V", page)
    start = stays(tr, b"V")
    row(f"N1 forward and back, {theme}", good, "map, forecast, conditions, alerts, legend, and back, on N and V")
    row(f"N2 the ends stop, {theme}", end and start, f"N on the legend quiet: {end}; V on the map quiet: {start}")


def rows(tr, ip, out, ref, rb35t, rb35):
    """Every row, in order. Returns 2 for the rig; the first row off raises Off."""
    t0 = time.time()
    tb, lb = tr.wait(r"\[build\] env=(\S+)", t0 - 1, 20)
    row("B0 the image", lb is not None and "env=weatherscope-s3-128" in lb and "touchbench" not in lb, "env=weatherscope-s3-128" if lb else "no banner")
    to, lo = tr.wait(r"\[ota\] channel=weatherscope-s3-128 current=\d+ latest=\d+", t0 - 1, 60)
    failed = tr.between(r"\[ota\] version check failed", t0 - 1, time.time())
    row("Q103 the HTTP read through GetSecure", ota_ok(lo, failed), (lo or "no [ota] channel line").split("] ", 1)[-1])
    if tr.wait(SCENARIO, t0 - 1, 60)[0] is None:
        tr.note("#HOST no scenario screen line within 60 s of the open: the RIG")
        return 2

    for theme in ("dark", "light"):
        rb35t.post_theme(tr, ip, theme)
        t = time.time()
        if tr.wait(rf"^\[wx\] theme={theme} ", t - 8, 10)[0] is None and theme == "light":
            tr.note("#HOST no theme line after the POST")
        tr.wait(SCENARIO, time.time(), 40)
        walk(tr, ip, out, rb35.capture, ref, theme)
    rb35t.post_theme(tr, ip, "dark")
    tr.wait(SCENARIO, time.time(), 40)

    # N3: a tap on any page goes to the map.
    good = True
    for n, page in enumerate(("forecast", "conditions", "alerts", "legend"), 1):
        for k, p in enumerate(("forecast", "conditions", "alerts", "legend")[:n]):
            good &= go(tr, b"N", p)
        good &= go(tr, b"T", "map")
    row("N3 a tap goes home", good, "from each of the 4 pages")

    # N4: on a page, a vertical swipe does nothing. The map's own zoom is unchanged, and its K rows hold it.
    good = go(tr, b"N", "forecast")
    t = time.time()
    tr.send(b"U")
    tr.send(b"D")
    time.sleep(3)
    zoomed = tr.between(r"\[wx\] .*zoom|screen=scenario", t, time.time())
    good &= not zoomed and go(tr, b"V", "map")
    row("N4 no zoom on a page", good, f"{len(zoomed)} zoom or screen line(s) after U and D")

    # N5: the cycle pauses on a page, past its 30 s hold, and resumes on the map.
    good = go(tr, b"N", "forecast")
    t = time.time()
    time.sleep(45)
    moved = tr.between(ANY_SCREEN, t, time.time())
    good &= not moved and go(tr, b"V", "map")
    t = time.time()
    resumed = tr.wait(SCENARIO, t + 0.5, 40)[0] is not None
    row("N5 the cycle pauses and resumes", good and resumed, f"{len(moved)} screen line(s) in 45 s on a page; resumed: {resumed}")

    # Q107: S, once, a bench shortcut and not a swipe.
    ok = go(tr, b"S", "legend") and go(tr, b"T", "map")
    row("Q107 S reaches the legend from the map", ok, "not part of any navigation row")

    # N6: ten minutes untouched goes home.
    t = time.time()
    tr.send(b"N")
    if tr.wait(PAGE["forecast"], t, 4)[0] is None:
        row("N6 the idle return", False, "the forecast never opened")
    else:
        th, _ = tr.wait(SCENARIO, t + 1, IDLE_S + 30)
        d = (th - t) if th else float("nan")
        row("N6 the idle return", th is not None and IDLE_S <= d <= IDLE_S + IDLE_SLACK_S, f"{d:.1f} s after the last byte (predicted 600 to 605)")
    return 0


def run(port, ip, out):
    global STOP
    STOP = True
    tcb, rb35t, rb35 = _load("touch_cap_bench"), _load("run_b35t"), _load("run_b35")
    if not tcb.valid_address(ip):
        print("REFUSED: the board address is not an IPv4 address (resolve the name first): the RIG")
        return 2
    ref = read_ref()
    if tcb.read_mac(port, out, "open") != 0:
        print("read-mac failed: the RIG")
        return 2
    tr = tcb.Trace(port, out, "run")
    rc = 2
    try:
        rc = rows(tr, ip, out, ref, rb35t, rb35)
    except Off as off:
        tr.note(f"#HOST stopped at {off}: a row off ends the run (A12.165)")
        rc = 1
    finally:
        # Every path, a crash included, leaves the theme the close reads. post_fields never raises: it reports.
        rb35t.post_theme(tr, ip, "auto")
        time.sleep(3)
        tr.close()
    return rc


def selftest():
    """The grade's own rows, with no board: a K-glass row must fail on a wrong frame and a failed capture, a row off must
    stop the walk, and Q103 must hold the versions exactly."""
    global STOP
    ROWS.clear()
    ref = read_ref()
    bad = 0

    class Tr:
        def __init__(self):
            self.t = []

        def send(self, b):
            self.t.append(b)

        def wait(self, p, s, t):
            return (1.0, "line")

        def between(self, p, a, b):
            return []

    def cap(sha, verdict="graded"):
        return lambda tr, ip, out, label: {"verdict": verdict, "graded": "f", "fetches": [{"file": "f", "sha256": sha}]}

    good = {f"{ROW_OF[p]}-dark": ref[f"{ROW_OF[p]}-dark"] for p in ROW_OF}
    global time
    real_sleep = time.sleep
    time.sleep = lambda s: None
    try:
        for name, c, want in (("CONTROL, each frame the reference's", None, True), ("a wrong frame", cap("0" * 64), False),
                              ("a failed capture", cap(None, "FAIL-no-agreeing-pair"), False)):
            ROWS.clear()
            if c is None:
                seq = iter([good["forecast-capture-dark"], good["conditions-day-dark"], good["alerts-SYNTHETIC-dark"]])
                cc = lambda tr, ip, out, label, s=seq: cap(next(s))(tr, ip, out, label)
            else:
                cc = c
            walk(Tr(), "x", "x", cc, ref, "dark")
            k = [r for r in ROWS if r[0].startswith("K-glass")]
            ok = len(k) == 3 and all(r[1] for r in k) == want and (want or not any(r[1] for r in k))
            print(f"  {'ok  ' if ok else 'FAIL'} {name}: {sum(r[1] for r in k)} of {len(k)} K-glass rows pass", flush=True)
            bad += not ok

        # A row off ends the run: one K-glass row reads, and nothing after it runs.
        ROWS.clear()
        STOP = True
        stopped = False
        try:
            walk(Tr(), "x", "x", cap("0" * 64), ref, "dark")
        except Off:
            stopped = True
        STOP = False
        ok = stopped and len(ROWS) == 1
        print(f"  {'ok  ' if ok else 'FAIL'} a row off stops the run: stopped {stopped}, {len(ROWS)} row(s) read", flush=True)
        bad += not ok
    finally:
        STOP = False
        time.sleep = real_sleep

    line = "[ota] channel=weatherscope-s3-128 "
    for name, got, want in (("Q103 CONTROL, the frozen versions", ota_ok(line + OTA_WANT, []), True),
                            ("Q103 a higher latest", ota_ok(line + "current=18 latest=19", []), False),
                            ("Q103 latest=180", ota_ok(line + OTA_WANT + "0", []), False),
                            ("Q103 a failed check beside the line", ota_ok(line + OTA_WANT, ["failed"]), False),
                            ("Q103 no line", ota_ok(None, []), False)):
        ok = got == want
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {got}", flush=True)
        bad += not ok

    print(f"{9 - bad} of 9 selftest rows as predicted")
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(2)
    sys.exit(run(sys.argv[1], sys.argv[2], sys.argv[3]))
