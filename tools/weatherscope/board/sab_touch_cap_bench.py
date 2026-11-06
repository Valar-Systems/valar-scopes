"""Runs touch_cap_bench.py --selftest against SABOTAGED copies of the runner (docs/weatherscope-stage3.md A12.128).
Each plant must turn EXACTLY its frozen checks red, and the unplanted copy must read 0 failures first.

    python -I tools/weatherscope/board/sab_touch_cap_bench.py

  S-TB1  W-CR's first defect restored: a failed fetch counted as a frame showing the strip -> ST1, ST2, ST4
  S-TB2  the address check removed: any address accepted                                  -> ST5, ST6
The real runner is never edited: each copy goes in its own scratch directory, each anchor must occur once, and the
copy must differ. Prints the real runner's sha256 (12 hex), so the proof names the file it proves.
"""
import hashlib, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RUNNER = os.path.join(HERE, "touch_cap_bench.py")
SABOTAGES = {
    "S-TB1": ([('    return sum(1 for x in seen if x not in (None, "error")), seen.count("error")\n',
                '    return sum(1 for x in seen if x is not None), 0  # SABOTAGE S-TB1: a failed fetch is a frame\n')],
              ["ST1", "ST2", "ST4"]),
    "S-TB2": ([('    m = re.fullmatch(r"(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})", ip or "")\n'
                '    return bool(m) and all(int(p) <= 255 for p in m.groups())\n',
                '    return True  # SABOTAGE S-TB2: the address check removed\n')],
              ["ST5", "ST6"]),
}
FAIL = re.compile(r"^\s*FAIL\s+(ST\d+):")
COUNT = re.compile(r"(\d+) checks, (\d+) failures")


def run(path):
    r = subprocess.run([sys.executable, "-I", path, "--selftest"], capture_output=True, text=True)
    fails = [m.group(1) for l in r.stdout.splitlines() for m in [FAIL.match(l)] if m]
    m = COUNT.findall(r.stdout)
    return r.returncode, fails, (int(m[-1][0]), int(m[-1][1])) if m else (None, None), r.stdout


def main():
    src = open(RUNNER, encoding="utf-8").read().replace("\r\n", "\n")
    before = hashlib.sha256(open(RUNNER, "rb").read()).hexdigest()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        ctl = os.path.join(td, "control")
        os.makedirs(ctl)
        p = os.path.join(ctl, "touch_cap_bench.py")
        open(p, "w", encoding="utf-8", newline="\n").write(src)
        rc, fails, (n, f), _ = run(p)
        g = rc == 0 and fails == [] and f == 0 and n is not None
        print(f"control: {n} checks, {f} failures, exit {rc} -> {'AS PREDICTED' if g else 'OFF-PREDICTION'}")
        ok &= g
        for name, (plants, want) in SABOTAGES.items():
            text = src
            for anchor, repl in plants:
                assert text.count(anchor) == 1, f"{name}: anchor found {text.count(anchor)} times"
                text = text.replace(anchor, repl)
            assert text != src, f"{name}: the copy is identical -- the sabotage did not apply"
            d = os.path.join(td, name)
            os.makedirs(d)
            p = os.path.join(d, "touch_cap_bench.py")
            open(p, "w", encoding="utf-8", newline="\n").write(text)
            rc, fails, (n, f), out = run(p)
            g = rc == 1 and sorted(fails) == sorted(want)
            print(f"{name}: exit {rc}; {n} checks; failing {fails or '-'} (want {want}) -> "
                  f"{'AS PREDICTED' if g else 'OFF-PREDICTION'}")
            if not g:
                print(out)
            ok &= g
    after = hashlib.sha256(open(RUNNER, "rb").read()).hexdigest()
    print(f"the real runner unchanged: {before == after} (touch_cap_bench.py {after[:12]})")
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
