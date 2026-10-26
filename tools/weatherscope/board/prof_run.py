"""3.5a's profile run (docs/weatherscope-stage3.md A9.22): one serial open, never reopened; loops at 60 mi, on the
warning screen, at 30 and at 15, for A9.23's design of (a). Then the trace is read against P2, P3 and P4.

    <penv python> -I tools/weatherscope/board/prof_run.py <COMx> <board host> <empty out dir>

THE SEQUENCE (A9.22): the 1 s rule; at the first scenario line, 2 s later 'T' (a loop at 60); at the next
"warning R=60", 2 s later 'T'; 'U' then 'T' (a loop at 30); 'U' then 'T' (a loop at 15); 'D', 'D' (back to 60); close.
Prints no coordinate.
"""
import importlib.util, json, os, re, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("run_b", os.path.join(HERE, "run_b.py"))
rb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rb)

SCREEN = r"^\[wx\] screen=scenario (\w+) R=(\d+)$"


def loop(tr, label):
    t0 = time.time()
    tr.send(b"T")
    tl, l = tr.wait(r"^\[wx\] screen=loop R=\d+$", t0, 5)
    te, le = tr.wait(SCREEN, tl or t0, 10)
    tr.note(f"#HOST loop {label}: {'started' if tl else 'NO START'}; {'ended' if te else 'NO END'}")
    return te or time.time()


def run(port, host, out):
    tr = rb.Trace(port, out)
    t_open = time.time()
    tr.note(f"#HOST opened {port}; board {host}; acting only on lines from t_open + 1 s")
    last = t_open + 1.0
    try:
        t, l = tr.wait(SCREEN, last, 60)
        if l:
            time.sleep(2)
            last = loop(tr, "first scenario, 60")
        t, l = None, None
        while True:
            t, l = tr.wait(SCREEN, last, 240)
            if not l:
                break
            last = t + 0.001
            if l.split()[2] == "warning" and l.endswith("R=60"):
                break
        if l:
            time.sleep(2)
            last = loop(tr, "warning, 60")
        for b, label in ((b"U", "30"), (b"U", "15")):
            t0 = time.time()
            tr.send(b)
            tr.wait(SCREEN, t0, 5)
            time.sleep(1)
            last = loop(tr, label)
        for b in (b"D", b"D"):
            t0 = time.time()
            tr.send(b)
            tr.wait(SCREEN, t0, 5)
            time.sleep(1)
    finally:
        tr.note("#HOST closing the port")
        tr.close()


def kv(line):
    return dict(re.findall(r"(\w+)=(\S+)", line))


def analyse(out):
    lines = [l.rstrip("\n").partition(" ")[2] for l in open(os.path.join(out, "trace.txt"), encoding="utf-8")]
    res = []

    def row(name, ok, detail):
        res.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {detail}")

    prof = [l for l in lines if l.startswith("[wx] prof ")]
    bad = [l for l in prof if not 0 <= int(kv(l)["compose_us"]) - int(kv(l)["sum"]) <= 100]
    row("P2 scenario profiles add up", bool(prof) and not bad, f"{len(prof)} lines, {len(bad)} outside 0..100 us")
    lprof = [l for l in lines if l.startswith("[wx] lprof ")]
    badl = [l for l in lprof if not 0 <= int(kv(l)["draw_us"]) - int(kv(l)["decode"]) - int(kv(l)["sum"]) <= 500]
    row("P3 loop profiles add up", bool(lprof) and not badl, f"{len(lprof)} lines, {len(badl)} outside 0..500 us")
    fb = [kv(l) for l in lines if l.startswith("[wx] fbcopy ")]
    badc = [k for k in fb if not (int(k["n"]) >= 1 and int(k["min"]) <= int(k["mean"]) <= int(k["max"]))]
    row("P4 full-frame copy lines", bool(fb) and not badc,
        f"{len(fb)} lines; min {min(int(k['min']) for k in fb) if fb else '-'} us, "
        f"max {max(int(k['max']) for k in fb) if fb else '-'} us")
    heads = [i for i, l in enumerate(lines) if l.startswith("[wx] loop records=")]
    loops = []
    for i in heads:
        recs = [kv(l) | {"i": int(l.split()[2])} for l in lines[i + 1:i + 16] if l.startswith("[wx] lrec ")]
        loops.append((int(kv(lines[i])["R"]), recs))
    row("loops", [R for R, _ in loops] == [60, 60, 30, 15] and all(len(r) == 15 for _, r in loops),
        f"radii {[R for R, _ in loops]}, records {[len(r) for _, r in loops]}")
    sref = {f["name"][14:20]: f for f in json.load(open(os.path.join(ROOT, "test", "fixtures", "wx", "loop", "screens.ref.json")))["frames"]}
    man = json.load(open(os.path.join(ROOT, "test", "fixtures", "wx", "loop", "MANIFEST.json")))["frames"]
    ref = [sref[f["name"][14:20]]["screenSha256"] for f in man]
    for R, recs in loops:
        if R == 60:
            n = sum(1 for r in recs if r["sha"] == ref[r["i"]])
            row("B2 hashes at 60", n == 15, f"{n} of 15 equal screens.ref.json")
    print(f"{len(res)} checks, {res.count(False)} failing")
    return 0 if all(res) else 1


def main(argv):
    if len(argv) == 3 and argv[1] == "--analyse":
        return analyse(argv[2])
    if len(argv) != 4:
        print(__doc__)
        return 2
    port, host, out = argv[1:]
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    run(port, host, out)
    return analyse(out)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
