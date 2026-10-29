"""3.5t step t3's two sabotages (docs/weatherscope-stage3.md A12.47), each failing EXACTLY its frozen set:

  S-NL  `Rgb{1, 2, 3}` planted in a copy of src/weatherscope/WxScreens.h: NL's scan finds exactly 1 hit, at the plant's
        line. (The real file: 0.)
  S-KD  dark's ring ink (64, 64, 64) -> (65, 64, 64) in a copy of WxTheme.h: K-dark fails all 25 Dallas K screens and all
        51 Bend rows, and nothing else, across six suites (k35, k35b, replay, layout, basemap, theme).

    python -I test/host/sabotage_wx_themed_draw.py

Method, as sabotage_wx_k35.py's:
  - S-KD's plant goes into a MIRROR of src/weatherscope (with the vendored font, which WxRaster.h includes by a relative
    path), never the real files: a quoted #include finds its sibling first, so a single copied header would be silently
    bypassed. The unmodified mirror runs first, as the control: every suite passes. The real tree is hashed before and
    after.
  - Each plant asserts its anchor occurs once and that the copy differs.
  - A suite that does not compile or run is the RIG, never a result.
"""
import hashlib, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src", "weatherscope")
FONT = os.path.join(ROOT, "test", "fixtures", "wx", "glcdfont.c")
WX = os.path.join(ROOT, "test", "fixtures", "wx")
LOOP = os.path.join(WX, "loop")
SCAN = os.path.join(ROOT, "test", "host", "scan_wx_colour_literals.py")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1"] + (
    ["-static-libgcc", "-static-libstdc++"] if "mingw" in CXX.lower() else [])
SUITES = {   # name: (arguments, checks)
    "k35": ([LOOP, os.path.join(WX, "k35.ref.txt")], 28),
    "k35b": ([WX, os.path.join(WX, "k35b.ref.txt")], 143),
    "replay": ([LOOP], 90),
    "layout": ([LOOP], 73),
    "basemap": ([WX], 84),
    "theme": ([WX], 72),
}
DALLAS = ["dry-60", "approaching-60", "herenow-60", "warning-60", "stale-60", "clear-60", "approaching-30",
          "approaching-15", "dry-30", "dry-15"] + ["loop-%02d" % i for i in range(15)]


def bend_rows():
    return [l.split()[1] for l in open(os.path.join(WX, "k35b.ref.txt"), encoding="utf-8") if l.startswith("ROW ")]


def tree_sha():
    paths = sorted(os.path.join(d, f) for d, _, fs in os.walk(SRC) for f in fs) + [FONT]
    h = hashlib.sha256()
    for p in paths:
        h.update(os.path.relpath(p, ROOT).encode() + b"\0" + open(p, "rb").read())
    return h.hexdigest()


def read(p):
    return open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")


def plant(text, old, new, name):
    assert text.count(old) == 1, (name, old[:60], text.count(old))
    out = text.replace(old, new)
    assert out != text, name
    return out


def run_suites(td, label, theme_text):
    base = os.path.join(td, label)
    shutil.copytree(SRC, os.path.join(base, "src", "weatherscope"))
    os.makedirs(os.path.join(base, "test", "fixtures", "wx"))
    shutil.copy(FONT, os.path.join(base, "test", "fixtures", "wx", "glcdfont.c"))
    open(os.path.join(base, "src", "weatherscope", "WxTheme.h"), "w", encoding="utf-8", newline="\n").write(theme_text)
    fails = set()
    for suite, (args, checks) in SUITES.items():
        exe = os.path.join(td, f"{label}-{suite}.exe")
        b = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(ROOT, "include"), "-I" + os.path.join(base, "src", "weatherscope"),
                            os.path.join(ROOT, "test", "host", f"test_wx_{suite}.cpp"), "-o", exe], capture_output=True, text=True)
        if b.returncode != 0:
            print(f"{label}/{suite}: did not compile (the RIG)\n{b.stderr}")
            return None
        r = subprocess.run([exe, *args], capture_output=True, text=True)
        m = re.search(r"^(\d+) checks, (\d+) failures$", r.stdout, re.M)
        if r.returncode not in (0, 1) or not m or int(m.group(1)) != checks:
            print(f"{label}/{suite}: did not run as {checks} checks (exit {r.returncode}): the RIG\n{r.stdout[-400:]}")
            return None
        for x in re.finditer(r"^\s*FAIL (\S+)", r.stdout, re.M):
            fails.add(f"{suite}:{x.group(1)}")
    return fails


def main():
    before = tree_sha()
    ok = True

    # S-NL
    screens = read(os.path.join(SRC, "WxScreens.h"))
    planted = plant(screens, "    HomeMarker(s, th.white);\n", "    HomeMarker(s, Rgb{1, 2, 3});   // SABOTAGE\n", "S-NL")
    plant_line = planted[:planted.index("Rgb{1, 2, 3}")].count("\n") + 1
    with tempfile.TemporaryDirectory() as td:
        real = subprocess.run([sys.executable, "-I", SCAN, os.path.join(SRC, "WxScreens.h")], capture_output=True, text=True)
        copy = os.path.join(td, "WxScreens.h")
        open(copy, "w", encoding="utf-8", newline="\n").write(planted)
        r = subprocess.run([sys.executable, "-I", SCAN, copy], capture_output=True, text=True)
    hits = re.findall(r":(\d+): R\d", r.stdout)
    good = real.returncode == 0 and r.returncode == 1 and hits == [str(plant_line)]
    ok &= good
    print(f"S-NL: the real WxScreens.h exit {real.returncode} ({real.stdout.strip().splitlines()[-1]}); planted at line "
          f"{plant_line}: exit {r.returncode}, hits at lines {hits} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")

    # S-KD
    theme = read(os.path.join(SRC, "WxTheme.h"))
    planted = plant(theme, "    {64, 64, 64},      // ring\n", "    {65, 64, 64},      // ring SABOTAGE\n", "S-KD")
    frozen = {"k35:K:" + r for r in DALLAS} | {"k35b:" + r for r in bend_rows()}
    with tempfile.TemporaryDirectory() as td:
        ctl = run_suites(td, "control", theme)
        good_c = ctl == set()
        ok &= good_c
        print(f"control (the unplanted mirror): failing {sorted(ctl) if ctl is not None else 'RIG'} -> "
              f"{'AS PREDICTED' if good_c else 'OFF-PREDICTION'}")
        got = run_suites(td, "S-KD", planted)
    good = got is not None and got == frozen
    ok &= good
    if got is not None:
        extra, missing = sorted(got - frozen), sorted(frozen - got)
        print(f"S-KD: {len(got)} failing ({len([g for g in got if g.startswith('k35:')])} Dallas, "
              f"{len([g for g in got if g.startswith('k35b:')])} Bend), predicted {len(frozen)} (25 Dallas, "
              f"{len(frozen) - 25} Bend); extra {extra}, missing {missing} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
    after = tree_sha()
    ok &= after == before
    print(f"the real tree unchanged: {after == before} ({after[:12]})")
    print(f"2 sabotages: {'ALL AS PREDICTED' if ok else 'NOT ALL AS PREDICTED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
