"""Runs test_daynight.cpp, and SY's scan, against SABOTAGED copies of include/DayNight.h and include/SolarDim.h. Each
must fail EXACTLY the set of named checks frozen in docs/weatherscope-stage3.md A12.43 and A12.45, and nothing else.

    python -I test/host/sabotage_daynight.py

Method, fixed before any run (A12.45):
  - The real files are never edited. Both headers are copied into a temporary directory searched first, and the plant
    goes into exactly one copy (DayNight.h includes "SolarDim.h", which resolves from its own directory first, so both
    must be copied). Each plant asserts its anchor occurs exactly once and that the copy differs; the real files' hashes
    are compared before and after.
  - The unmodified copies run first, as the control: 28 checks, 0 failures, SY 0 hits.
  - SY's scan (test/host/scan_daynight_symbols.sh) runs on the copy of DayNight.h, so S-SY plants there.
  - A plant keeps every variable used: under -Werror an unused one is a compile failure, which is the RIG, not a result.
"""
import hashlib, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FILES = {"DayNight.h": os.path.join(ROOT, "include", "DayNight.h"),
         "SolarDim.h": os.path.join(ROOT, "include", "SolarDim.h")}
TEST = os.path.join(ROOT, "test", "host", "test_daynight.cpp")
SCAN = os.path.join(ROOT, "test", "host", "scan_daynight_symbols.sh")
FIX = os.path.join(ROOT, "test", "fixtures", "daynight")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
CHECKS = 28

# name: (file, [(anchor, replacement)], the frozen failing set)
SABOTAGES = {
    "S-DN1": ("DayNight.h",
              [("constexpr float kHysteresisDeg = 1.0f;", "constexpr float kHysteresisDeg = 0.0f;   // SABOTAGE: H = 0")],
              {"D9", "D11", "D14", "Q1", "Q2", "Q3"}),
    "S-DN2": ("DayNight.h",
              [("    if (!clockValid || !locationValid) return prev;\n",
                "    if (!locationValid) return prev;   // SABOTAGE: no clock gate\n    (void)clockValid;\n")],
              {"D1", "D3"}),
    "S-DN3": ("DayNight.h",
              [("    if (!clockValid || !locationValid) return prev;\n",
                "    if (!clockValid) return prev;   // SABOTAGE: no location gate\n    (void)locationValid;\n")],
              {"D2", "D4"}),
    "S-DN4": ("DayNight.h",
              [("    if (prev == Phase::Unknown) return elevDeg >= kThresholdDeg ? Phase::Day : Phase::Night;\n",
                "    // SABOTAGE: the first verdict goes through the band\n")],
              {"D5", "D6", "D7", "D8"}),
    "S-DNE": ("SolarDim.h",
              [("    const double lambda = L + 1.915 * sin(d2r(g)) + 0.020 * sin(d2r(2 * g));\n",
                "    const double lambda = L + 0.0 * g;   // SABOTAGE: no equation of centre\n")],
              {"DN-E"} | {"DN-U%d" % i for i in range(1, 9)}),
    "S-SY": ("DayNight.h",
             [("namespace daynight {\n", "namespace daynight {\n// SABOTAGE: wxpalette\n")],
             {"SY"}),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def read(p):
    return open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")


def run(td, name, texts):
    inc = os.path.join(td, name)
    os.makedirs(inc, exist_ok=True)
    for fn, t in texts.items():
        open(os.path.join(inc, fn), "w", encoding="utf-8", newline="\n").write(t)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe],
                       capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        return None, None, []
    r = subprocess.run([exe, FIX], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the suite did not run (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
        return None, None, []
    m = re.search(r"^(\d+) checks, (\d+) failures$", r.stdout, re.M)
    if not m or int(m.group(1)) != CHECKS:
        print(f"{name}: {m.group(1) if m else '?'} checks ran, not {CHECKS}: the RIG")
        return None, None, []
    lines = [x.group(0).strip() for x in re.finditer(r"^\s*FAIL \S+.*$", r.stdout, re.M)]
    fails = {ln.split()[1] for ln in lines}
    sy = subprocess.run(["bash", SCAN, os.path.join(inc, "DayNight.h")], capture_output=True, text=True)
    if sy.returncode >= 2:
        print(f"{name}: SY's scan did not run (exit {sy.returncode}): the RIG\n{sy.stdout}")
        return None, None, []
    if sy.returncode == 1:
        fails.add("SY")
        lines.append("FAIL SY " + sy.stdout.strip().replace("\n", " | "))
    return r.returncode, fails, lines


def main():
    before = {k: sha(p) for k, p in FILES.items()}
    real = {k: read(p) for k, p in FILES.items()}
    ok = True
    with tempfile.TemporaryDirectory() as td:
        rc, fails, _ = run(td, "control", real)
        good = rc == 0 and fails == set()
        ok &= good
        print(f"control (the real headers): exit {rc}, failing {sorted(fails or [])} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        for name, (fn, edits, frozen) in SABOTAGES.items():
            texts = dict(real)
            src = texts[fn]
            for old, new in edits:
                assert src.count(old) == 1, (name, old[:50], src.count(old))
                src = src.replace(old, new)
            assert src != texts[fn], name
            texts[fn] = src
            rc, fails, lines = run(td, name, texts)
            good = fails is not None and fails == frozen
            ok &= good
            print(f"{name} ({fn}): failing {sorted(fails or [])}, predicted {sorted(frozen)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            for line in lines:
                print(f"    {line}")
    after = {k: sha(p) for k, p in FILES.items()}
    same = after == before
    ok &= same
    print(f"the real headers unchanged: {same} ({', '.join(k + ' ' + v[:12] for k, v in after.items())})")
    print(f"{len(SABOTAGES)} sabotages: {'ALL AS PREDICTED' if ok else 'NOT ALL AS PREDICTED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
