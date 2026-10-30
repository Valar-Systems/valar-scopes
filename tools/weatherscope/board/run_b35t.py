"""3.5t step t5's board run (docs/weatherscope-stage3.md A12.66 section 4, A12.67): B1 and B3 in each theme, then BSW and
BSW-L, in ONE serial open, never reopened, with the live watcher (screen_watch.py, with A12.66's theme rules) stopping
the run at the first unexplained screen change.

    <penv python> -I tools/weatherscope/board/run_b35t.py <COMx> <board host> <empty out dir>
    <penv python> -I tools/weatherscope/board/run_b35t.py <COMx> <board host> <empty out dir> --bsw-only   (S-SWB's pass)

run_b35's capture() and waits and run_b35b's WatchedTrace are imported, not copied. The steps:
  theme  a POST of wx-theme alone to the board's /save: a partial POST (no cfg-form, no cfg-toggles), so no other
         setting is touched. "#HOST post wx-theme=<v>" goes into the trace before the request, the reply's status after.
         First light, then dark, so the dark switch is always seen: the board's theme before the run is not assumed.
  B1     per theme (dark, then light): the eight scenarios at 60 mi with terrain off, each captured 2 s after its screen
         line; at the next bdry, G, "[wx] terrain on", bdry's recompose, capture bdry-C; brain's next showing, brain-C;
         then G, "[wx] terrain off". 600 s per theme.
  B3     per theme: at a scenario screen line, S, the legend, capture, T.
  BSW    POST wx-theme=auto; at bdry's screen line (R=60), 96 O, 0.2 s apart; then R (the build records).
  BSW-L  O up to one press before BSW's first phase change; at a scenario screen line, T; at "[wx] screen=loop", O (the
         switching press); the loop's end and the recompose; then O to the wrap (offset 0, where BSW began).
--bsw-only runs only BSW (S-SWB's image, A12.66 section 5).
--location fixture (Q64, open: use only once ruled) posts, before BSW, the centre of the Dallas loop fixture's first crop
as the board's latitude and longitude -- a public fixture's place, read from its own header and never printed -- because
Auto needs a location and the bench board has none (step 7's init line: location=unset). The page cannot clear a
location again, so the board keeps it afterwards; the run says so.
Prints no coordinate and no board address. The trace keeps times; the committed record does not (Q62, A12.67).
"""
import importlib.util, json, os, re, sys, time, urllib.parse, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


rb35b = _load("run_b35b")
rb35, rb = rb35b.rb35, rb35b.rb
SCREEN = rb35.SCREEN
PRESSES, PRESS_S = 96, 0.2
OFFSET = re.compile(r"^\[wx\] auto offset=\+(\d+) min phase=(\w+)$")


def post_theme(tr, host, v):
    tr.note(f"#HOST post wx-theme={v}")
    try:
        req = urllib.request.Request(f"http://{host}/save", data=urllib.parse.urlencode({"wx-theme": v}).encode(),
                                     method="POST")
        with urllib.request.urlopen(req, timeout=10) as r:
            tr.note(f"#HOST post wx-theme={v}: HTTP {r.status}")
            return r.status == 200
    except Exception as e:
        tr.note(f"#HOST post wx-theme={v}: ERROR {type(e).__name__}")
        return False


def post_fixture_location(tr, host):
    """Q64 (a): the Dallas loop fixture's centre, from its own header (ref35.centre), never printed."""
    spec = importlib.util.spec_from_file_location("ref35", os.path.join(HERE, "..", "screens", "ref35.py"))
    ref35 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ref35)
    names = sorted(n for n in os.listdir(ref35.LOOP) if n.startswith("shsr-") and n.endswith(".wxc"))
    h, _ = ref35.decode(os.path.join(ref35.LOOP, names[0]))
    lat, lon = ref35.centre(h)
    tr.note("#HOST post location=<the Dallas loop fixture's centre> (Q64)")
    try:
        req = urllib.request.Request(f"http://{host}/save", method="POST", data=urllib.parse.urlencode(
            {"latitude": "%.6f" % lat, "longitude": "%.6f" % lon}).encode())
        with urllib.request.urlopen(req, timeout=10) as r:
            tr.note(f"#HOST post location: HTTP {r.status}")
            return r.status == 200
    except Exception as e:
        tr.note(f"#HOST post location: ERROR {type(e).__name__}")
        return False


def b1(tr, host, out, caps, theme, last):
    t0, seen = time.time(), set()
    while len(seen) < 8 and time.time() - t0 < 600:
        t, l = tr.wait(SCREEN, last, 600 - (time.time() - t0))
        if not l:
            break
        last = t + 0.001
        name, R = rb35.parse(l)
        if name in seen or R != 60:
            continue
        time.sleep(2)
        caps.append(dict(rb35.capture(tr, host, out, f"{theme}-{name}"), step="B1", theme=theme, scenario=name, R=R,
                         terrain=False))
        seen.add(name)
    tr.note(f"#HOST B1 {theme}: captured {sorted(seen)} with terrain off")
    t, l = rb35.wait_scene(tr, last, ("bdry", 60), 300)
    if l:
        last = t + 0.001
        t1 = time.time()
        tr.send(b"G")
        tg, _ = tr.wait(r"^\[wx\] terrain on$", t1, 2)
        ts, ls = tr.wait(r"^\[wx\] screen=scenario bdry R=60$", t1, 5)
        if tg and ls:
            last = ts + 0.001
            time.sleep(0.5)
            caps.append(dict(rb35.capture(tr, host, out, f"{theme}-bdry-C"), step="B1", theme=theme, scenario="bdry",
                             R=60, terrain=True))
            t, l = rb35.wait_scene(tr, last, ("brain", 60), 60)
            if l:
                last = t + 0.001
                time.sleep(2)
                caps.append(dict(rb35.capture(tr, host, out, f"{theme}-brain-C"), step="B1", theme=theme,
                                 scenario="brain", R=60, terrain=True))
        else:
            tr.note(f"#HOST B1 {theme} G: no terrain line or no recomposed bdry within its window")
        t, l = tr.wait(SCREEN, last, 60)   # terrain off again, on the scenario screen G needs
        if l:
            last = t + 0.001
            t1 = time.time()
            tr.send(b"G")
            tr.wait(r"^\[wx\] terrain off$", t1, 2)
    return last


def b3(tr, host, out, caps, theme, last):
    t, l = tr.wait(SCREEN, last, 60)
    if not l:
        tr.note(f"#HOST B3 {theme}: no scenario screen line")
        return last
    last = t + 0.001
    time.sleep(1)
    t1 = time.time()
    tr.send(b"S")
    tg, _ = tr.wait(r"^\[wx\] screen=legend$", t1, 5)
    if tg:
        time.sleep(1)
        caps.append(dict(rb35.capture(tr, host, out, f"{theme}-legend"), step="B3", theme=theme))
        t2 = time.time()
        tr.send(b"T")
        tb, lb = tr.wait(SCREEN, t2, 5)
        if lb:
            last = tb + 0.001
    return last


def presses(tr, n, label):
    tr.note(f"#HOST {label}: {n} O presses")
    for _ in range(n):
        tr.send(b"O")
        time.sleep(PRESS_S)
    time.sleep(2)


def bsw(tr, host, last):
    post_theme(tr, host, "auto")
    time.sleep(3)
    t, l = rb35.wait_scene(tr, last, ("bdry", 60), 400)
    if not l:
        tr.note("#HOST BSW: no bdry R=60 screen line")
        return last, None
    t_start = time.time()
    tr.note("#HOST BSW starts")
    presses(tr, PRESSES, "BSW")
    tr.note("#HOST BSW ends")
    t1 = time.time()
    tr.send(b"R")
    tr.wait(r"^\[wx\] frames n=", t1, 10)
    offs = [OFFSET.match(l) for tt, l in list(tr.lines) if tt >= t_start]
    phases = [m.group(2) for m in offs if m]
    first = next((i for i in range(1, len(phases)) if phases[i] != phases[i - 1]), None)
    return time.time(), (first + 1 if first is not None else None)   # the 1-based press that changed the phase


def bswl(tr, host, last, k):
    presses(tr, k - 1, "BSW-L positioning")
    t, l = tr.wait(SCREEN, time.time(), 60)
    if not l:
        tr.note("#HOST BSW-L: no scenario screen line")
        return
    time.sleep(1)
    t0 = time.time()
    tr.note("#HOST BSW-L starts")
    tr.send(b"T")
    tl, _ = tr.wait(r"^\[wx\] screen=loop ", t0, 5)
    if not tl:
        tr.note("#HOST BSW-L: no loop")
        return
    tr.send(b"O")
    tr.wait(r"^\[wx\] loop records=", t0, 10)
    tr.wait(r"^\[wx\] theme=", t0, 10)
    time.sleep(2)
    tr.note("#HOST BSW-L ends")
    presses(tr, PRESSES - k, "BSW-L wrap")


def main(argv):
    only = "--bsw-only" in argv
    fixture = "--location" in argv and argv[argv.index("--location") + 1:argv.index("--location") + 2] == ["fixture"]
    if "--location" in argv:
        i = argv.index("--location")
        argv = argv[:i] + argv[i + 2:]
    argv = [a for a in argv if a != "--bsw-only"]
    if len(argv) != 4:
        print(__doc__)
        return 2
    port, host, out = argv[1], argv[2], argv[3]
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    caps, stopped, k = [], None, None
    tr = rb35b.WatchedTrace(port, out)
    t_open = time.time()
    tr.note(f"#HOST opened {port}; acting only on screen lines from t_open + 1 s" + ("; BSW only (S-SWB)" if only else ""))
    last = t_open + 1.0
    try:
        if not only:
            post_theme(tr, host, "light")
            time.sleep(3)
            post_theme(tr, host, "dark")
            if not tr.wait(r"^\[wx\] theme=dark ", t_open, 8)[1]:
                tr.note("#HOST setup: no [wx] theme=dark line after the POSTs")
            last = time.time()
            last = b1(tr, host, out, caps, "dark", last)
            last = b3(tr, host, out, caps, "dark", last)
            post_theme(tr, host, "light")
            if not tr.wait(r"^\[wx\] theme=light ", last, 8)[1]:
                tr.note("#HOST setup: no [wx] theme=light line after the POST")
            last = time.time()
            last = b1(tr, host, out, caps, "light", last)
            last = b3(tr, host, out, caps, "light", last)
        if fixture:
            post_fixture_location(tr, host)
            tr.wait(r"^\[wx\] init; location=set ", time.time() - 1, 8)
        last, k = bsw(tr, host, last)
        tr.note(f"#HOST BSW: the first phase change came at a press {'found' if k else 'NOT found'}")
        if not only and k and 1 < k <= PRESSES:
            bswl(tr, host, last, k)
    except rb35b.Stopped as e:
        stopped = str(e)
        tr.note(f"#HOST STOPPED by the watcher: {stopped}")
    finally:
        tr.note("#HOST closing the port")
        tr.close()
    json.dump(caps, open(os.path.join(out, "captures.json"), "w"), indent=1)
    json.dump([[tt, ll] for tt, ll in tr.lines], open(os.path.join(out, "lines.json"), "w"))
    json.dump({"stopped": stopped, "changes": len(tr.watch.changes), "bsw_only": only},
              open(os.path.join(out, "watch.json"), "w"), indent=1)
    up_at, t0 = None, time.time()
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
