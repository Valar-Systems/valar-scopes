"""The --anchors pass's own control (docs/weatherscope-stage3.md A12.50, Q51): it must be able to fail.

A copy of sabotage_daynight.py (and the shared sabotage_common.py) runs --anchors in a temporary tree holding copies of the
two headers it plants. Unmangled, it must exit 0; with one anchor mangled in the copy of DayNight.h, it must exit 1 and
name the plant. The real files are never touched.

    python -I test/host/sabotage_anchors_control.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOST = os.path.join(ROOT, "test", "host")
ANCHOR = "constexpr float kHysteresisDeg = 1.0f;"   # S-DN1's anchor


def tree(td, name, mangle):
    base = os.path.join(td, name)
    os.makedirs(os.path.join(base, "test", "host"))
    os.makedirs(os.path.join(base, "include"))
    for f in ("sabotage_daynight.py", "sabotage_common.py"):
        shutil.copy(os.path.join(HOST, f), os.path.join(base, "test", "host", f))
    shutil.copy(os.path.join(ROOT, "include", "SolarDim.h"), os.path.join(base, "include", "SolarDim.h"))
    src = open(os.path.join(ROOT, "include", "DayNight.h"), "rb").read().decode("utf-8")
    assert src.count(ANCHOR) == 1, "the control's own anchor is not in DayNight.h"
    if mangle:
        out = src.replace(ANCHOR, ANCHOR.replace("1.0f", "1.00f"))
        assert out != src
        src = out
    open(os.path.join(base, "include", "DayNight.h"), "w", encoding="utf-8", newline="\n").write(src)
    return os.path.join(base, "test", "host", "sabotage_daynight.py")


def main():
    with tempfile.TemporaryDirectory() as td:
        clean = subprocess.run([sys.executable, "-I", tree(td, "clean", False), "--anchors"], capture_output=True, text=True)
        bad = subprocess.run([sys.executable, "-I", tree(td, "mangled", True), "--anchors"], capture_output=True, text=True)
    named = "S-DN1" in bad.stdout and "found 0 times" in bad.stdout
    ok = clean.returncode == 0 and bad.returncode == 1 and named
    print(f"  {'ok  ' if ok else 'FAIL'} ANCHORS-CTL the pass on a copy: unmangled exit {clean.returncode} (want 0); one anchor "
          f"mangled exit {bad.returncode} (want 1), naming S-DN1: {named}")
    if not ok:
        print(clean.stdout + clean.stderr + bad.stdout + bad.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
