"""The S-WAIT pass (docs/weatherscope-stage3.md A9.25 item 2, A9.26): on a SEPARATE flash whose /diag/fb has its wait and
retry removed (one immediate copy, the verification and the headers kept), 60 single fetches about 1 s apart of
whatever scenario screens the cycle shows at 60 mi, no gestures, one serial open. Then:
  S-WAIT red   at least 1 of the 60 is unfinished (not its tag's scene), each marked Finished: false;
  (i)          every unfinished fetch has Clearing (4) or Cleared (1) at an end (A9.25's restatement);
  (ii), (iii)  as frozen (grade_b35.diag_read, A9.23's refinement for (iii)).

    <penv python> -I tools/weatherscope/board/sab_wait.py <COMx> <board host> <empty out dir>
    <penv python> -I tools/weatherscope/board/sab_wait.py --grade <out dir>

Prints no coordinate.
"""
import hashlib, importlib.util, json, os, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


rb = load("run_b", os.path.join(HERE, "run_b.py"))
gb = load("grade_b35", os.path.join(HERE, "grade_b35.py"))
rn = load("run_b35", os.path.join(HERE, "run_b35.py"))
N_FETCHES = 60


def run(port, host, out):
    tr = rb.Trace(port, out)
    t_open = time.time()
    tr.note(f"#HOST opened {port}; board {host}; S-WAIT: {N_FETCHES} single fetches from t_open + 1 s")
    fetches = []
    try:
        time.sleep(1.0)
        for i in range(N_FETCHES):
            t0 = time.time()
            try:
                resp = urllib.request.urlopen(f"http://{host}/diag/fb", timeout=10)
                body = resp.read()
                h = {k: resp.headers.get(k) for k in rn.HEADERS}
            except Exception as e:
                fetches.append({"i": i, "start": t0, "end": time.time(), "error": f"{type(e).__name__}: {e}"})
                tr.note(f"#HOST fetch {i}: ERROR {type(e).__name__}")
                time.sleep(1.0)
                continue
            t1 = time.time()
            path = os.path.join(out, f"snap-{i:02d}.bin")
            open(path, "wb").write(body)
            fetches.append({"i": i, "file": os.path.basename(path), "sha256": hashlib.sha256(body).hexdigest(),
                            "headers": h, "start": t0, "end": t1})
            tr.note(f"#HOST fetch {i}: phase={h['X-Blipscope-Frame-Phase']} tag={h['X-Blipscope-Frame-Tag']} "
                    f"finished={h['X-Blipscope-Frame-Finished']} attempts={h['X-Blipscope-Frame-Attempts']}")
            time.sleep(max(0.0, 1.0 - (time.time() - t0)))
    finally:
        tr.note("#HOST closing the port")
        tr.close()
    json.dump(fetches, open(os.path.join(out, "fetches.json"), "w"), indent=1)
    json.dump([[tt, ll] for tt, ll in tr.lines], open(os.path.join(out, "lines.json"), "w"))


def grade(out):
    text, rows = gb.ref35.k_text()
    committed = open(gb.ref35.K_REF, encoding="utf-8").read().replace("\r\n", "\n")
    if text != committed:
        print("FAIL: ref35.py's K rows differ from the committed k35.ref.txt: nothing is graded")
        return 1
    REF = {name: (scr.rgb565(), lines) for name, scr, lines in rows}
    trace = gb.parse_trace(out)
    lines = [l for _, _, l in trace]
    screens = [(t, l) for t, _, l in trace if l.startswith("[wx] screen=")]
    fetches = json.load(open(os.path.join(out, "fetches.json")))
    attempts = sorted({f["headers"].get("X-Blipscope-Frame-Attempts") for f in fetches if f.get("headers")})
    verdict, report, c = gb.diag_read(lines, screens, fetches, REF, out)
    ok = True
    print(f"the plant took: every fetch made {attempts} attempt(s) (the sabotaged route copies once)")
    ok &= attempts == ["1"]
    for l in report:
        print("  " + l)
    red = c["unfinished"] >= 1 and c["finished_bad"] == 0
    print(f"  {'RED  ' if red else 'GREEN'} S-WAIT: {c['unfinished']} of {c['n']} unfinished, {c['finished_bad']} of them marked "
          f"finished -> {'AS PREDICTED' if red else 'OFF-PREDICTION'}")
    ok &= red
    i_holds = c["clear_ends"] == c["unfinished"]
    print(f"  {'ok  ' if i_holds else 'FAIL'} (i) restated: {c['clear_ends']} of {c['unfinished']} unfinished fetches have "
          f"Clearing or Cleared at an end")
    ok &= i_holds
    print(f"  VERDICT: {verdict}")
    ok &= verdict.startswith("(i)")
    print("S-WAIT pass: " + ("AS PREDICTED" if ok else "OFF-PREDICTION: STOP"))
    return 0 if ok else 1


def main(argv):
    if len(argv) == 3 and argv[1] == "--grade":
        return grade(argv[2])
    if len(argv) != 4:
        print(__doc__)
        return 2
    port, host, out = argv[1:]
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    run(port, host, out)
    return grade(out)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
