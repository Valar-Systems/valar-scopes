"""R0b's and RX1's sabotages (docs/weatherscope-stage3.md A12.16): each plant goes into a COPY of the files ref35 and
geo35 need, mirrored into a temporary root (tools/weatherscope/screens, tools/weatherscope/basemap, test/fixtures/wx),
never into the real ones. Every anchor must occur exactly once, the copy must differ, and the real files' sha256s are
compared before and after. Prints no coordinate.

    python -I tools/weatherscope/screens/sabotage_geo35.py <mockup dir> --sources <dir>

  S-R0-DISC   P's map disc radius 119.5 in place of 120             -> R0b' fails on B and C: pixels unnamed
  S-R0-ROUND  P's rounding floor(v)                                 -> R0b' fails on A, B and C: pixels unnamed
              (re-registered, A12.18.1; A12.16 froze B and C, a miss kept on record: A's rings use it too)
  S-R0-FILL   P's fill takes a pixel's top-left corner, not centre  -> R0b' fails on B and C: pixels unnamed
  S-R0-GREY   G's water fill (10, 34, 72) -> (10, 34, 73)           -> R0a' fails on B and C
  S-RX-DELTA  each polyline's first vertex read from the last item  -> RX1 fails for rivers and roads
(the predicted screens are A12.16's, as frozen; the runner prints which screens actually fail)
"""
import hashlib, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
DIRS = [os.path.join("tools", "weatherscope", "screens"), os.path.join("tools", "weatherscope", "basemap"),
        os.path.join("test", "fixtures", "wx")]
REF, GEO = os.path.join(DIRS[0], "ref35.py"), os.path.join(DIRS[0], "geo35.py")

SABOTAGES = {
    "S-R0-DISC": (REF, [("            elif (x + 0.5 - C) ** 2 + (y + 0.5 - C) ** 2 > C * C:\n",
                         "            elif (x + 0.5 - C) ** 2 + (y + 0.5 - C) ** 2 > 119.5 * 119.5:   # SABOTAGE\n")],
                  "r0b", {"B", "C"}),
    "S-R0-ROUND": (REF, [("        p = (int(math.floor(xv + 0.5)), int(math.floor(yv + 0.5)))\n",
                          "        p = (int(math.floor(xv)), int(math.floor(yv)))   # SABOTAGE\n")],
                   "r0b", {"A", "B", "C"}),   # re-registered (A12.18.1): A12.16 froze {B, C}; A's rings use it too
    "S-R0-FILL": (GEO, [("        py = y + 0.5\n", "        py = y   # SABOTAGE: the top-left corner\n"),
                        ("            for x in range(max(0, int(math.ceil(a - 0.5))), min(ref35.SIZE, int(math.ceil(b - 0.5)))):\n",
                         "            for x in range(max(0, int(math.ceil(a))), min(ref35.SIZE, int(math.ceil(b)))):   # SABOTAGE\n")],
                  "r0b", {"B", "C"}),
    "S-R0-GREY": (GEO, [("                    img[y][x] = WATER_FILL\n", "                    img[y][x] = (10, 34, 73)   # SABOTAGE\n")],
                  "r0a", {"B", "C"}),
    "S-RX-DELTA": (GEO, [("                lines.append((idx, _verts(r, r.var(), (0, 0))))\n",
                          "                lines.append((idx, _verts(r, r.var(), lines[-1][1][-1] if lines else (0, 0))))   # SABOTAGE\n"),
                         ("                items.append((cls, _verts(r, r.var(), (0, 0))))\n",
                          "                items.append((cls, _verts(r, r.var(), items[-1][1][-1] if items else (0, 0))))   # SABOTAGE\n")],
                   "rx1", {"rivers", "roads"}),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def mirror(dst):
    for d in DIRS:
        shutil.copytree(os.path.join(ROOT, d), os.path.join(dst, d), ignore=shutil.ignore_patterns("__pycache__"))


def main(argv):
    if len(argv) < 4 or "--sources" not in argv:
        print(__doc__)
        return 2
    mock, src_root = argv[1], argv[argv.index("--sources") + 1]
    before = {f: sha(os.path.join(ROOT, f)) for f in (REF, GEO)}
    ok = True
    for name, (target, edits, check, want) in SABOTAGES.items():
        with tempfile.TemporaryDirectory() as td:
            mirror(td)
            path = os.path.join(td, target)
            src = open(path, encoding="utf-8").read().replace("\r\n", "\n")
            out = src
            for anchor, repl in edits:
                n = out.count(anchor)
                assert n == 1, f"{name}: anchor found {n} times:\n{anchor}"
                out = out.replace(anchor, repl)
            assert out != src, f"{name}: the copy did not change"
            open(path, "w", encoding="utf-8", newline="\n").write(out)
            geo = os.path.join(td, GEO)
            if check == "rx1":
                cmd = [sys.executable, "-I", "-B", geo, "--rx1", os.path.join(td, "test", "fixtures", "wx", "basemap", "bend.bmpk")]
            else:
                cmd = [sys.executable, "-I", "-B", geo, "--r0b", mock, "--sources", src_root]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if check == "rx1":
                failing = set(re.findall(r"^\s+FAIL\s+(\w+):", r.stdout, re.M))
            else:
                tag = "R0a'" if check == "r0a" else "R0b'"
                block = r.stdout.split(tag, 1)[1] if tag in r.stdout else ""
                block = block.split("R0b'", 1)[0] if check == "r0a" else block
                failing = set(re.findall(r"^\s+FAIL\s+screen-bend-([ABC])", block, re.M))
            good = r.returncode == 1 and failing == want and "Traceback" not in r.stderr
            print(f"{name}: exit {r.returncode}; {check} fails on {sorted(failing)}, predicted {sorted(want)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            if "Traceback" in r.stderr:
                print(r.stderr[-800:])
            ok &= good
    after = {f: sha(os.path.join(ROOT, f)) for f in (REF, GEO)}
    print(f"real ref35.py and geo35.py unchanged: {before == after}")
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
