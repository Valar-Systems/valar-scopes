#!/usr/bin/env python3
"""v6_fleet_sabotage.py -- red first for tools/basemap/v6_fleet.py: each plant, one exact edit in a temp copy, must fail EXACTLY its
registered selftest rows. The baseline must pass every row first. Every copy is deleted.
  python -I tools/basemap/v6_fleet_sabotage.py            exit 0 every plant as registered, 1 otherwise
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
TOOL = os.path.join(HERE, "v6_fleet.py")

# (name, needle, replacement, the rows that must FAIL)
PLANTS = [
    ("sabF the / filter dropped", 'return isinstance(route, str) and route.startswith("/")', "return isinstance(route, str)", {"ST3"}),
    ("sabR the filter narrowed to /api/", 'return isinstance(route, str) and route.startswith("/")',
     'return isinstance(route, str) and route.startswith("/api/")', {"ST2"}),
    ("sabD A \\ B for B \\ A", "    return len(b - a)", "    return len(a - b)", {"ST5", "ST6", "ST7"}),
    ("sabW A starting at T", '"A": (T + 5 * m, T + 65 * m)', '"A": (T, T + 65 * m)', {"ST1"}),
    ("sabG V6-4's lower bound 0.3", "BAND = (0.4, 2.5)", "BAND = (0.3, 2.5)", {"ST7"}),
    ("sabB BLIND graded as a pass", '"BLIND" if blind else', '"BLIND" if False else', {"ST8"}),
    ("sabP the device set printed", '    out["b_not_a"] = diff(sets["B"], sets["A"])',
     '    out["b_not_a"] = diff(sets["B"], sets["A"])\n    out["devices_B"] = sorted(sets["B"])', {"ST4"}),
    ("sabC compare ignoring a moved value", "    moved = [k for k in KEYS if r1.get(k) != r2.get(k)]", "    moved = []", {"ST9"}),
]


def run(path):
    env = dict(os.environ, V6_FLEET_REPO=REPO)
    p = subprocess.run([sys.executable, "-I", path, "--selftest"], capture_output=True, text=True, env=env, timeout=300)
    fails = set(re.findall(r"^FAIL (ST\w+)", p.stdout, re.M))
    return p.returncode, fails, p.stdout, p.stderr


def main():
    src = open(TOOL, encoding="utf-8").read()
    rc, fails, out, err = run(TOOL)
    rows = len(re.findall(r"^(PASS|FAIL) ST", out, re.M))
    print(f"baseline: exit {rc}, {rows} rows, failing {sorted(fails) or 'none'}, stderr {len(err)} B")
    if rc != 0 or fails or err:
        print("BASELINE NOT CLEAN: the plants cannot be graded")
        return 1
    ok = True
    for name, needle, new, want in PLANTS:
        if src.count(needle) != 1:
            print(f"OFF {name}: the needle occurs {src.count(needle)} times, not once (the plant cannot apply)")
            ok = False
            continue
        planted = src.replace(needle, new)
        fd, path = tempfile.mkstemp(suffix="_v6_fleet_sab.py")
        try:
            with os.fdopen(fd, "w", encoding="utf-8", newline="") as f:
                f.write(planted)
            assert open(path, encoding="utf-8").read() == planted != src, "the copy is not the plant"
            rc, fails, out, err = run(path)
        finally:
            os.remove(path)
        good = rc == 1 and fails == want and not err
        ok &= good
        print(f"{'as registered' if good else 'OFF'} {name}: exit {rc}, failing {sorted(fails)} (registered {sorted(want)}), "
              f"stderr {len(err)} B")
    print("SABOTAGE " + ("ALL AS REGISTERED" if ok else "OFF"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
