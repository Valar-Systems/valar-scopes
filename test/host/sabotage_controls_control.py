"""The --control pass's own control (docs/weatherscope-stage3.md A12.61): it must be able to fail.

A copy of sabotage_wx_themed_draw.py (and the shared sabotage_common.py) runs --control from a temporary directory, its
ROOT pointed at this tree, which it only reads. Unmangled, it must exit 0. With test_wx_k35b's count put back to 143 --
the stale value that left the driver unrunnable from #507 to the harness PR -- it must exit 1, naming test_wx_k35b.
The real driver is never edited.

    python -I test/host/sabotage_controls_control.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOST = os.path.join(ROOT, "test", "host")
DRIVER = "sabotage_wx_themed_draw.py"
ROOT_LINE = "ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))\n"
COUNT = '    "k35b": ([WX, os.path.join(WX, "k35b.ref.txt")], 149),'


def copy(td, tag, stale):
    d = os.path.join(td, tag)
    os.makedirs(d)
    shutil.copy(os.path.join(HOST, "sabotage_common.py"), os.path.join(d, "sabotage_common.py"))
    src = open(os.path.join(HOST, DRIVER), "rb").read().decode("utf-8").replace("\r\n", "\n")
    assert src.count(ROOT_LINE) == 1, "the driver's ROOT line moved"
    out = src.replace(ROOT_LINE, f"ROOT = {ROOT!r}\n")
    if stale:
        assert out.count(COUNT) == 1, "the driver's k35b count line moved"
        out = out.replace(COUNT, COUNT.replace("149", "143"))
    assert out != src
    p = os.path.join(d, DRIVER)
    open(p, "w", encoding="utf-8", newline="\n").write(out)
    return p


def main():
    with tempfile.TemporaryDirectory() as td:
        clean = subprocess.run([sys.executable, "-I", copy(td, "clean", False), "--control"], capture_output=True, text=True)
        bad = subprocess.run([sys.executable, "-I", copy(td, "stale", True), "--control"], capture_output=True, text=True)
    named = "FAIL test_wx_k35b: 149 checks" in bad.stdout and "against checks 143" in bad.stdout
    ok = clean.returncode == 0 and bad.returncode == 1 and named
    print(f"  {'ok  ' if ok else 'FAIL'} CONTROLS-CTL the pass on a copy: unmangled exit {clean.returncode} (want 0); "
          f"143 restored exit {bad.returncode} (want 1), naming test_wx_k35b's 149 against 143: {named}")
    if not ok:
        print(clean.stdout[-1500:] + clean.stderr[-500:] + bad.stdout[-1500:] + bad.stderr[-500:])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
