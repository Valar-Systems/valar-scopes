"""The light-look viewer's board run (docs/weatherscope-stage3.md A12.32): ONE serial open, never reopened.

    <penv python> -I tools/weatherscope/board/run_look.py <COMx> <empty out dir>

Resolve the port from the board's USB serial first (scripts/board-port.py weatherscope --upload), never by number.
THE 1 s RULE: act only on lines that arrive at least 1 s after the open. 'R', then for each look 'T U U U U S', each
byte waiting for the screen line it causes, so every (look, frame) pair is drawn once and the run ends on dark F0.
Writes lines.json (every line on the runner's clock) and the trace. The grade is grade_look.py's.
"""
import importlib.util, json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("run_b", os.path.join(HERE, "run_b.py"))
rb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rb)
SHOWN = r"^\[look\] look=\w+ frame=\d+ sha="


def step(tr, b, want, since, timeout=8):
    t0 = time.time()
    tr.send(b)
    t, l = tr.wait(want, max(since, t0), timeout)
    tr.note(f"#HOST {b.decode()}: {'ok' if l else 'NO LINE in %d s' % timeout}")
    return (t + 1e-3) if l else time.time()


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    port, out = argv[1], argv[2]
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    tr = rb.Trace(port, out)
    t_open = time.time()
    tr.note(f"#HOST opened {port}; acting only on lines from t_open + 1 s")
    try:
        time.sleep(1.5)
        last = step(tr, b"R", SHOWN, t_open + 1.0)
        for _ in range(3):
            last = step(tr, b"T", r"^\[look\] lrec look=\w+ i=14 ", last, 10)   # the loop, F1, then its records
            for _ in range(4):
                last = step(tr, b"U", SHOWN, last)
            last = step(tr, b"S", SHOWN, last, 10)   # the next look's title (1 s), then its frame
        time.sleep(0.5)
    finally:
        tr.note("#HOST closing the port")
        tr.close()
    json.dump([[tt, ll] for tt, ll in tr.lines], open(os.path.join(out, "lines.json"), "w"))
    print(f"lines: {len(tr.lines)}; out: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
