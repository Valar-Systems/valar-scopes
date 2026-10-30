"""Runs test_wx_legend.cpp against SABOTAGED copies of src/weatherscope/WxLegend.h; each must turn the suite red,
failing EXACTLY the check predicted in docs/weatherscope-stage2.md (addendum 8) and nothing else.

    python -I test/host/sabotage_wx_legend.py

  S-G1  the swatch column at x = 150 (the snow row's edge reaches 115.4 px) -> exactly G1
  S-G2  "No data" at 10 x 10                                                -> exactly G2
  S-G3  the first row 4 px higher (label y 46, swatches 44)                  -> exactly G3
  S-G4  labels at x = 50 ("No radar coverage" ends at 152)                   -> exactly G4
The real header is never edited; each plant asserts its anchor occurs once and the copy differs.
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "src", "weatherscope", "WxLegend.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_legend.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "S-G1": ([("constexpr int kSwatchX0 = 144;", "constexpr int kSwatchX0 = 150;  // SABOTAGE")], [r"G1\b"]),
    "S-G2": ([("    S(6, Kind::NoData, 15, 0)};",
               "    Swatch{6, Kind::NoData, 15, kSwatchX0, (int16_t)(kRows[6].y - 2), 10, 10}};  // SABOTAGE")], [r"G2\b"]),
    "S-G3": ([('{{"Light rain", 50},', '{{"Light rain", 46},  /* SABOTAGE */')], [r"G3\b"]),
    "S-G4": ([("constexpr int kLabelX = 36;", "constexpr int kLabelX = 50;  // SABOTAGE")], [r"G4\b"]),
}

def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()

def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
        sc.prove_colour(name, anchor, repl)   # Q50: refuses a colour the panel cannot see
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    return out

def main():
    before = sha(HEADER)
    src = open(HEADER, encoding="utf-8").read().replace("\r\n", "\n")
    ok = True
    with tempfile.TemporaryDirectory() as td:
        for name, (edits, expect) in SABOTAGES.items():
            inc = os.path.join(td, name); os.makedirs(inc)
            open(os.path.join(inc, "WxLegend.h"), "w", encoding="utf-8", newline="\n").write(plant(name, edits, src))
            exe = os.path.join(td, name + ".exe")
            b = subprocess.run([CXX, *FLAGS, "-I" + inc, TEST, "-o", exe], capture_output=True, text=True)
            if b.returncode != 0:
                print(f"{name}: did not compile (rig)\n{b.stderr}"); return 2
            r = subprocess.run([exe], capture_output=True, text=True)
            fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith("FAIL")]
            matched = [sum(1 for l in fails if re.search(rx, l)) for rx in expect]
            good = r.returncode == 1 and all(m == 1 for m in matched) and len(fails) == len(expect)
            print(f"{name}: suite exit {r.returncode}; {len(fails)} failing check(s) -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            for l in fails:
                print(f"    {l}")
            ok &= good
    after = sha(HEADER)
    print(f"real src/weatherscope/WxLegend.h unchanged: {before == after} ({after[:12]})")
    return 0 if ok and before == after else 1

def anchor_entries():
    """Q51: every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    src = sc.read(HEADER)
    out = [(name, os.path.relpath(HEADER, ROOT), src, a) for name, entry in SABOTAGES.items() for a, _ in entry[0]]
    return out


if __name__ == "__main__":
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
