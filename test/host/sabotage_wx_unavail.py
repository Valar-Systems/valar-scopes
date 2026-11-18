"""Runs test_wx_unavail.cpp against SABOTAGED copies of src/weatherscope/WxSwipe.h (W7 slice 2, Q126 (a)); each must turn
the suite red on EXACTLY the checks predicted in docs/weatherscope-stage4.md S4.35, and nothing else.

    python -I test/host/sabotage_wx_unavail.py

  U-ALLCLEAR       the alerts page ignores `available`             -> ALERTS-UNAVAIL-EMPTY, ALERTS-UNAVAIL-STORED
  U-ALLCLEARWORDS  an unreadable alerts feed draws the all-clear   -> ALERTS-UNAVAIL-EMPTY, ALERTS-UNAVAIL-STORED
                   (stage4 section 8's registered sabotage)
  U-FORECAST       the forecast page ignores `available`           -> FORECAST-UNAVAIL
  U-CONDITIONS     the conditions page ignores `valid`             -> CONDITIONS-UNAVAIL
The real header is never edited; each plant asserts its anchor occurs once and the copy differs.
"""
import hashlib, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "src", "weatherscope", "WxSwipe.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_unavail.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "U-ALLCLEAR": ([("    if (!list.available) {", "    if (false) {  // SABOTAGE")],
                   {"ALERTS-UNAVAIL-EMPTY", "ALERTS-UNAVAIL-STORED"}),
    "U-ALLCLEARWORDS": ([('        c.Centred("ALERTS UNAVAILABLE", 116, 1, c.th.uiWarn);',
                          '        c.Centred("NO WATCHES OR ADVISORIES", 116, 1, c.th.uiDim);  // SABOTAGE')],
                        {"ALERTS-UNAVAIL-EMPTY", "ALERTS-UNAVAIL-STORED"}),
    "U-FORECAST": ([("    if (!f.available) {", "    if (false) {  // SABOTAGE")], {"FORECAST-UNAVAIL"}),
    "U-CONDITIONS": ([("    if (!o.valid) {", "    if (false) {  // SABOTAGE")], {"CONDITIONS-UNAVAIL"}),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
        sc.prove_colour(name, anchor, repl)
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    return out


def real():
    return open(HEADER, encoding="utf-8").read().replace("\r\n", "\n")


def build_run(td, name, text):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, "WxSwipe.h"), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "src", "weatherscope"),
                        "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return b, None
    return b, subprocess.run([exe], capture_output=True, text=True)


def main():
    before = sha(HEADER)
    ok = True
    rigs = sc.Rigs()
    with tempfile.TemporaryDirectory() as td:
        for name, (edits, expect) in SABOTAGES.items():
            if rigs.forced(name):
                ok = False
                continue
            b, r = build_run(td, name, plant(name, edits, real()))
            if r is None:
                print(f"{name}: did not compile (rig)\n{b.stderr}")
                rigs.add(name)
                ok = False
                continue
            fails = {l.split()[1] for l in r.stdout.splitlines() if l.strip().startswith("FAIL") and len(l.split()) > 1}
            good = r.returncode == 1 and fails == expect
            print(f"{name}: suite exit {r.returncode}; red {sorted(fails)} (want {sorted(expect)}) -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = sha(HEADER)
    print(f"real src/weatherscope/WxSwipe.h unchanged: {before == after} ({after[:12]})")
    if ok and before == after:
        print("W7 UNAVAILABLE PLANTS AS PREDICTED")
    return rigs.close(0 if ok and before == after else 1)


def anchor_entries():
    """Every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    src = sc.read(HEADER)
    return [(name, os.path.relpath(HEADER, ROOT), src, a) for name, (edits, _) in SABOTAGES.items() for a, _ in edits]


# --control: this driver's unplanted run alone, the suite against its own count. run.sh runs it for every driver.
UNAVAIL_OKS = 8   # the suite's count: the one place, read by the run path and by CONTROL_TABLE
CONTROL_TABLE = {"test_wx_unavail": ("ok", UNAVAIL_OKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        build_run(td, "control", real())


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
