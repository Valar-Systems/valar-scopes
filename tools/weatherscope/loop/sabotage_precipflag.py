"""Runs test_precipflag.py against SABOTAGED copies of precipflag.py; each must fail EXACTLY the rows
docs/weatherscope-stage3.md A12.9.3 froze for it, and nothing else.

    python -I tools/weatherscope/loop/sabotage_precipflag.py <bend.bmpk> <cache-dir>

  S-PF-CODE    snow read as code 10, not 3     -> exactly PF3, PF4
  S-PF-ANCHOR  the grid anchor check removed   -> exactly PF1
The real reader is never edited: each plant goes into a copy, the anchor must occur exactly once, the copy must differ,
and the real file's sha256 is compared before and after.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
READER = os.path.join(HERE, "precipflag.py")
TEST = os.path.join(HERE, "test_precipflag.py")
SABOTAGES = {
    "S-PF-CODE": ("SNOW = 3\n", "SNOW = 10  # SABOTAGE\n", {"PF3", "PF4"}),
    "S-PF-ANCHOR": ("    if (grid[\"ni\"], grid[\"nj\"], grid[\"di\"], grid[\"dj\"], grid[\"scan\"]) != (7000, 3500, 0.01, 0.01, 0):\n"
                    "        raise Refused(f\"ANCHOR: grid {grid['ni']} x {grid['nj']}, di {grid['di']}, scan {grid['scan']}\")\n",
                    "", {"PF1"}),
}


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    before = hashlib.sha256(open(READER, "rb").read()).hexdigest()
    src = open(READER, encoding="utf-8").read()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        for name, (anchor, repl, want) in SABOTAGES.items():
            assert src.count(anchor) == 1, f"{name}: anchor found {src.count(anchor)} times"
            out = src.replace(anchor, repl)
            assert out != src
            planted = os.path.join(td, name + ".py")
            open(planted, "w", encoding="utf-8", newline="\n").write(out)
            r = subprocess.run([sys.executable, "-I", TEST, argv[1], argv[2], planted], capture_output=True, text=True)
            fails = {m.group(1) for m in re.finditer(r"^\s+FAIL\s+(\S+)", r.stdout, re.M)}
            good = r.returncode == 1 and fails == want and "Traceback" not in r.stderr
            print(f"{name}: exit {r.returncode}; failing {sorted(fails)}, predicted {sorted(want)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            if not good and r.stderr:
                print(r.stderr[-600:])
            ok &= good
    after = hashlib.sha256(open(READER, "rb").read()).hexdigest()
    print(f"real precipflag.py unchanged: {before == after} ({after[:12]})")
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
