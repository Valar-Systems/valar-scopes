"""WD8b's host half (docs/wx-device.md): plant S-leg -- one swatch typed in by hand, one level off (code 5 as
rgb(105,105,0), its colour at 41 %, where the layer draws it at 40 %) -- into a COPY of include/WxLegendHtml.h and
require exactly test_wx_legend_html.cpp's [L2] row to go red. The real header is never written; its sha256 is
compared before and after.

    python -I test/host/sabotage_wx_legend_html.py      exit 0 = as predicted, 1 = not, 2 = rig
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "include", "WxLegendHtml.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_legend_html.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
OLD = 'fill=\\"" + Rgb(wxs2::Pixel888(code, 0, 0, look)) + "\\"/>";'
NEW = 'fill=\\"" + (code == 5 ? std::string("rgb(105,105,0)") : Rgb(wxs2::Pixel888(code, 0, 0, look))) + "\\"/>";   // SABOTAGE S-leg'


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def run(inc, td, tag):
    exe = os.path.join(td, f"t-{tag}.exe")
    b = subprocess.run([CXX] + FLAGS + ["-I", inc, "-I", os.path.join(ROOT, "include"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return None, b.stderr
    return subprocess.run([exe], capture_output=True, text=True), ""


def main():
    before = sha(HEADER)
    src = open(HEADER, encoding="utf-8").read().replace("\r\n", "\n")
    with tempfile.TemporaryDirectory() as td:
        r, err = run(os.path.join(ROOT, "include"), td, "control")
        if r is None or r.returncode != 0:
            print(f"control (the real header): did not pass -- the rig is broken\n{err}{r.stdout if r else ''}")
            return 2
        print("control (the real header): all legend checks pass")
        if src.count(OLD) != 1:
            print(f"RIG: the anchor occurs {src.count(OLD)} times"); return 2
        out = src.replace(OLD, NEW)
        assert out != src, "the sabotage did not apply"
        inc = os.path.join(td, "s")
        os.makedirs(inc)
        open(os.path.join(inc, "WxLegendHtml.h"), "w", encoding="utf-8", newline="\n").write(out)
        r, err = run(inc, td, "s")
        if r is None:
            print(f"S-leg: did not compile (rig)\n{err}"); return 2
        red = sorted({m.group(1) for l in r.stdout.splitlines() for m in [re.search(r"FAIL\s+\[(L\d)\]", l)] if m})
        good = r.returncode == 1 and red == ["L2"]
        print(f"S-leg, code 5 typed in at 41 %: exit {r.returncode}; red rows {red or 'none'} (want ['L2']) -> "
              f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
    after = sha(HEADER)
    print(f"real include/WxLegendHtml.h unchanged: {before == after} ({after[:12]})")
    return 0 if good and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
