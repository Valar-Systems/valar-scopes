"""Runs D6 against SABOTAGED copies of src/weatherscope/WxTowns.h. Each must fail EXACTLY what
docs/weatherscope-stage3.md A8.14 froze, and nothing else.

    python -I test/host/sabotage_wx_towns.py <2025_Gaz_place_national.zip> <sub-est2024.csv>

  control  an unmodified copy: D6(b) 0 failures; make_towns.py --check "identical", exit 0
  S-D6a    the FIRST town's name gains "é" (UTF-8, two bytes) before " city": D6(b) fails exactly {D6:1}
  S-D6b    the LAST town's first coordinate's final digit changed: make_towns.py --check "DIFFERS", exit 1

The real header is never edited: each plant asserts its anchor occurs once and that the copy differs, and the real
file's hash is compared before and after. Prints no coordinate.
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOWNS = os.path.join(ROOT, "src", "weatherscope", "WxTowns.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_towns.cpp")
MAKE = os.path.join(ROOT, "tools", "weatherscope", "towns", "make_towns.py")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
ENTRY = re.compile(r'^    \{"([^"]*)", (-?\d+\.\d+), (-?\d+\.\d+), (\d+)\},$', re.M)   # + the population (A9.10 PJ)


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def d6b(td, name, text):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, "WxTowns.h"), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "src", "weatherscope"),
                        "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        sys.exit(2)
    r = subprocess.run([exe], capture_output=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the test did not run (exit {r.returncode}): the RIG")
        sys.exit(2)
    out = r.stdout.decode("utf-8", "replace")
    return r.returncode, {m.group(1) for m in re.finditer(r"^\s*FAIL (D6:\d+)", out, re.M)}, inc


def d6a(zip_path, csv_path, header):
    r = subprocess.run([sys.executable, "-I", MAKE, zip_path, csv_path, "--check", header], capture_output=True, text=True)
    m = re.search(r"^WxTowns\.h: (identical|DIFFERS)$", r.stdout, re.M)
    return r.returncode, m.group(1) if m else "?"


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    before = sha(TOWNS)
    src = open(TOWNS, encoding="utf-8").read().replace("\r\n", "\n")
    entries = list(ENTRY.finditer(src))
    ok = True
    with tempfile.TemporaryDirectory() as td:
        rc, fails, inc = d6b(td, "control", src)
        arc, aword = d6a(argv[1], argv[2], os.path.join(inc, "WxTowns.h"))
        good = rc == 0 and not fails and arc == 0 and aword == "identical"
        print(f"control: D6(b) exit {rc}, failing {sorted(fails)}; --check exit {arc}, {aword} -> "
              f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good

        first = entries[0]
        anchor = first.group(0)
        assert src.count(anchor) == 1, f"S-D6a: anchor found {src.count(anchor)} times"
        planted = anchor.replace(" city\"", "é city\"", 1)
        assert planted != anchor
        rc, fails, _ = d6b(td, "S-D6a", src.replace(anchor, planted))
        good = rc == 1 and fails == {"D6:1"}
        print(f"S-D6a: failing {sorted(fails)}, predicted ['D6:1'] -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good

        last = entries[-1]
        anchor = last.group(0)
        assert src.count(anchor) == 1, f"S-D6b: anchor found {src.count(anchor)} times"
        lat = last.group(2)
        lat2 = lat[:-1] + ("1" if lat[-1] != "1" else "2")
        planted = anchor.replace(f", {lat}, ", f", {lat2}, ", 1)
        assert planted != anchor
        os.makedirs(os.path.join(td, "S-D6b"))
        p = os.path.join(td, "S-D6b", "WxTowns.h")
        open(p, "w", encoding="utf-8", newline="\n").write(src.replace(anchor, planted))
        arc, aword = d6a(argv[1], argv[2], p)
        good = arc == 1 and aword == "DIFFERS"
        print(f"S-D6b: --check exit {arc}, {aword}; predicted exit 1, DIFFERS -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
    after = sha(TOWNS)
    print(f"the real src/weatherscope/WxTowns.h unchanged: {before == after} ({after[:12]})")
    print("2 sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return 0 if ok and before == after else 1


def anchor_entries():
    """Q51: S-D6a plants the first entry and S-D6b the last; each must occur exactly once, for --anchors."""
    def first(t):
        e = list(ENTRY.finditer(t))
        assert len(e) >= 2, f"{len(e)} entries"
        assert t.count(e[0].group(0)) == 1, "the first entry is not unique"
    def last(t):
        e = list(ENTRY.finditer(t))
        assert len(e) >= 2, f"{len(e)} entries"
        assert t.count(e[-1].group(0)) == 1, "the last entry is not unique"
    src = sc.read(TOWNS)
    return [("S-D6a", os.path.relpath(TOWNS, ROOT), src, first), ("S-D6b", os.path.relpath(TOWNS, ROOT), src, last)]

# --control (Q57, docs/weatherscope-stage3.md A12.61): this driver's unplanted run alone, each suite against its own
# count, nothing planted. run.sh runs it for every driver.
CONTROL_TABLE = {}


def control():
    raise sc.Skip("needs the Census archive: its control runs make_towns.py --check over the 2025 Gazetteer and "
                  "SUB-EST2024 files, its two arguments, which no CI runner holds")


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main(sys.argv))
