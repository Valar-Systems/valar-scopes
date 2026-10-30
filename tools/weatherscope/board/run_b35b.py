"""3.5b step 7's board run (docs/weatherscope-stage3.md A12.55): B1, BZ on Bend, B2 three times and B3 in ONE serial open,
never reopened, with the live watcher (screen_watch.py) stopping the run at the first unexplained screen change.

    <penv python> -I tools/weatherscope/board/run_b35b.py <COMx> <board host> <empty out dir>

run_b35.py's rules unchanged (the 1 s rule, the snapshot rule, captures over /diag/fb; its capture() and waits are
imported, not copied), with A12.55's steps:
  B1   the eight scenarios at 60 mi as they come, each captured 2 s after its screen line, terrain off (the default).
       After bdry's capture: G, "[wx] terrain on" (2 s), bdry's recomposed screen line (5 s), 0.5 s, capture "bdry-C".
       Terrain then stays on. Budget 330 s.
  BZ   on Bend dry with terrain on, three blocks, each on bdry's 30 s hold: from bdry R=60 U U; from bdry R=15 U D;
       from bdry R=30 D H H. A step's budget is 5.2 s + the largest terrain-on base build_us seen so far (0.1 s up);
       a block runs only if 1 s + its steps fit the 25 s left of the hold, else it waits for the next bdry (3 tries).
  B2   the next Bend scenario at 60 mi: 2 s, T, the loop, 0.5 s, capture "postloop-bend"; the next Dallas scenario at
       60: T, "postloop"; the next warning R=60: T, "postloop-warning".
  B3   S, the legend, capture; T; "postlegend".
Prints no coordinate.
"""
import importlib.util, json, math, os, re, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


rb35 = _load("run_b35")
rb = rb35.rb
sw = _load("screen_watch")

SCENES = sw.DALLAS + sw.BEND
SCREEN = rb35.SCREEN
BZ_BLOCKS = [(60, [("U", 30), ("U", 15)]), (15, [("U", 15), ("D", 30)]), (30, [("D", 60), ("H", 15), ("H", 60)])]
STEP_S, HOLD_S, SPARE_S = 5.2, 30.0, 5.0


class Stopped(Exception):
    pass


class WatchedTrace(rb.Trace):
    """run_b's Trace, with every line also fed to the watcher as it arrives."""

    def __init__(self, port, out):
        self.watch, self.unexplained = sw.Watcher(SCENES), None
        super().__init__(port, out)

    def add(self, line):
        super().add(line)
        msg = self.watch.feed(time.time(), line)
        if msg and self.unexplained is None:
            self.unexplained = msg

    def wait(self, pattern, since, timeout):
        t_end = time.time() + timeout
        while time.time() < t_end:
            if self.unexplained:
                raise Stopped(self.unexplained)
            t, l = super().wait(pattern, since, min(0.5, t_end - time.time()))
            if l:
                return t, l
        if self.unexplained:
            raise Stopped(self.unexplained)
        return None, None


def build_max_s(tr):
    """The largest terrain-on base build_us the trace has shown, in seconds, rounded up to 0.1 s."""
    us = [int(m.group(1)) for _, l in list(tr.lines) for m in [re.match(r"^\[wx\] base build_us=(\d+) .* terrain=1 ", l)] if m]
    return math.ceil(max(us) / 1e5) / 10 if us else 0.0


def next_scene(tr, since, ok, timeout):
    """The next screen line whose (name, R) satisfies ok(name, R)."""
    t_end = time.time() + timeout
    while time.time() < t_end:
        t, l = tr.wait(SCREEN, since, t_end - time.time())
        if not l:
            return None, None
        since = t + 0.001
        if ok(*rb35.parse(l)):
            return t, l
    return None, None


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    port, host, out = argv[1], argv[2], argv[3]
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    caps, stopped = [], None
    tr = WatchedTrace(port, out)
    t_open = time.time()
    tr.note(f"#HOST opened {port}; board {host}; acting only on screen lines from t_open + 1 s")
    last = t_open + 1.0
    terrain = False
    try:
        # B1
        seen = set()
        while len(seen) < len(SCENES) and time.time() - t_open < 330:
            t, l = tr.wait(SCREEN, last, 330 - (time.time() - t_open))
            if not l:
                break
            last = t + 0.001
            name, R = rb35.parse(l)
            if name in seen or R != 60:
                continue
            time.sleep(2)
            caps.append(dict(rb35.capture(tr, host, out, name), step="B1", scenario=name, R=R, terrain=terrain))
            seen.add(name)
            if name == "bdry" and not terrain:
                t0 = time.time()
                tr.send(b"G")
                tg, _ = tr.wait(r"^\[wx\] terrain on$", t0, 2)
                ts, ls = tr.wait(r"^\[wx\] screen=scenario bdry R=60$", t0, 5)
                if tg and ls:
                    terrain = True
                    last = ts + 0.001
                    time.sleep(0.5)
                    caps.append(dict(rb35.capture(tr, host, out, "bdry-C"), step="B1", scenario="bdry", R=60, terrain=True))
                else:
                    tr.note("#HOST B1 G: no terrain line or no recomposed bdry within its window")
                    caps.append({"label": "bdry-C", "verdict": "FAIL-no-terrain", "graded": None, "fetches": [],
                                 "step": "B1", "scenario": "bdry", "R": 60, "terrain": True})
        tr.note(f"#HOST B1 captured {sorted(seen)}; terrain {'on' if terrain else 'OFF'}")
        # BZ, on Bend dry with terrain on
        k = 0
        for startR, steps in BZ_BLOCKS:
            tries, ran = 0, False
            while tries < 3 and terrain:
                t, l = rb35.wait_scene(tr, last, ("bdry", startR), 400)
                if not l:
                    break
                last = t + 0.001
                tries += 1
                budget = 1.0 + (STEP_S + build_max_s(tr)) * len(steps)
                left = (t + HOLD_S - SPARE_S) - time.time()
                if left < budget:
                    tr.note(f"#HOST BZ block from bdry R={startR}: {left:.1f} s left < {budget:.1f} s budget (try {tries})")
                    continue
                tr.note(f"#HOST BZ block from bdry R={startR} starts; budget {budget:.1f} s")
                time.sleep(max(0.0, t + 1.0 - time.time()))
                for b, want in steps:
                    t0 = time.time()
                    tr.send(b.encode())
                    ts, ls = tr.wait(SCREEN, t0, 5 + build_max_s(tr))
                    if not ls:
                        tr.note(f"#HOST BZ {k} {b}: no screen line")
                        caps.append({"label": f"bz{k}", "verdict": "FAIL-no-screen-line", "graded": None, "fetches": [],
                                     "step": "BZ", "gesture": b, "wantR": want, "terrain": True})
                        k += 1
                        continue
                    last = ts + 0.001
                    name, R = rb35.parse(ls)
                    time.sleep(0.5)
                    caps.append(dict(rb35.capture(tr, host, out, f"bz{k}"), step="BZ", gesture=b, wantR=want,
                                     scenario=name, R=R, terrain=True))
                    k += 1
                ran = True
                break
            if not ran:
                tr.note(f"#HOST BZ block from bdry R={startR}: did not run")
                for b, want in steps:
                    caps.append({"label": f"bz{k}", "verdict": "FAIL-block-not-run", "graded": None, "fetches": [],
                                 "step": "BZ", "gesture": b, "wantR": want, "terrain": True})
                    k += 1
        # B2: Bend, then Dallas, then warning
        t, l = next_scene(tr, last, lambda n, R: n in sw.BEND and R == 60, 300)
        if l:
            last = t + 0.001
            time.sleep(2)
            last = (rb35.loop_and_capture(tr, host, out, "postloop-bend", caps) or time.time()) + 0.001
            caps[-1]["terrain"] = terrain
        t, l = next_scene(tr, last, lambda n, R: n in sw.DALLAS and R == 60, 300)
        if l:
            last = t + 0.001
            time.sleep(2)
            last = (rb35.loop_and_capture(tr, host, out, "postloop", caps) or time.time()) + 0.001
        t, l = rb35.wait_scene(tr, last, ("warning", 60), 300)
        if l:
            last = t + 0.001
            time.sleep(2)
            last = (rb35.loop_and_capture(tr, host, out, "postloop-warning", caps) or time.time()) + 0.001
        # B3
        t1 = time.time()
        tr.send(b"S")
        tg, _ = tr.wait(r"^\[wx\] screen=legend$", t1, 5)
        if tg:
            time.sleep(1)
            caps.append(dict(rb35.capture(tr, host, out, "legend"), step="B3"))
            t2 = time.time()
            tr.send(b"T")
            tb2, lb2 = tr.wait(SCREEN, t2, 5)
            if lb2:
                name, R = rb35.parse(lb2)
                time.sleep(0.5)
                caps.append(dict(rb35.capture(tr, host, out, "postlegend"), step="B3", scenario=name, R=R, terrain=terrain))
    except Stopped as e:
        stopped = str(e)
        tr.note(f"#HOST STOPPED by the watcher: {stopped}")
    finally:
        tr.note("#HOST closing the port")
        tr.close()
    json.dump(caps, open(os.path.join(out, "captures.json"), "w"), indent=1)
    json.dump([[tt, ll] for tt, ll in tr.lines], open(os.path.join(out, "lines.json"), "w"))
    json.dump({"stopped": stopped, "changes": len(tr.watch.changes)}, open(os.path.join(out, "watch.json"), "w"), indent=1)
    up_at = None
    t0 = time.time()
    while time.time() - t0 < 60:
        if rb.tcp(host):
            up_at = time.time() - t0
            break
        time.sleep(2)
    json.dump({"tcp_up_within_s": None if up_at is None else round(up_at, 1)}, open(os.path.join(out, "after-close.json"), "w"))
    print(f"captures: {[(c['label'], c['verdict']) for c in caps]}")
    print(f"watcher: {'STOPPED -- ' + stopped if stopped else 'no unexplained change'}")
    print(f"after the close: TCP :80 {'up after %.1f s' % up_at if up_at is not None else 'DOWN for 60 s -- recover with read-mac (no writes) and record it'}")
    return 1 if stopped else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
