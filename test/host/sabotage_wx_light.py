"""3.5t step t4's three device sabotages (docs/weatherscope-stage3.md A12.60), each failing EXACTLY its frozen set across
four suites (k35, k35b, replay, switch):

  S-KL   kLight's rings (150, 146, 138) -> (158, 146, 138) in a copy of WxTheme.h: K-light, all 82 (25 Dallas, 57 Bend).
         Nothing else: RP compares a theme with itself, and LY reads no ring.
  S-LY   in Light only, no town is placed (DrawInvariant hands PlaceLabels 0 towns when `which` is Light), in a copy of
         WxScreens.h: K-light and LY on the 73 scenes with a town label (Dallas 25; Bend dry and Bend rain at 60 and
         30 mi, 14 and 4; the Bend loop, 30). The 9 at 15 mi pass, with no town label in either theme; LY-X passes; RP
         and RP-T pass (both sides of each comparison lose the towns).
  S-RPT  the theme left out of LayerKey (MakeLayerKey no longer sets `which`), in a copy of WxScreens.h: RP-T frames
         8-14 of both series (14 rows), and SW1 and SW4 (0 rebuilds).

    python -I test/host/sabotage_wx_light.py [--anchors | --control]

Method, as sabotage_wx_themed_draw.py's: each plant goes into a MIRROR of src/weatherscope (with the vendored font),
never the real files; the unmodified mirror runs first, as the control (every suite at its count, nothing failing); each
plant asserts its anchor occurs once, that the copy differs, and (S-KL) the colour proof (Q50); a suite that does not
compile or run at its count is the RIG, never a result; the real tree is hashed before and after. S-LY's set is read off
the committed references (a scene places a town label when its rows carry a LABEL line) and must split 73 / 9 as frozen.
"""
import hashlib, os, re, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass, the --control pass and the colour proof (A12.50, A12.61)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src", "weatherscope")
FONT = os.path.join(ROOT, "test", "fixtures", "wx", "glcdfont.c")
WX = os.path.join(ROOT, "test", "fixtures", "wx")
LOOP = os.path.join(WX, "loop")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1"] + (
    ["-static-libgcc", "-static-libstdc++"] if "mingw" in CXX.lower() else [])
SUITES = {   # name: (arguments, checks)
    "k35": ([LOOP, os.path.join(WX, "k35.ref.txt")], 79),
    "k35b": ([WX, os.path.join(WX, "k35b.ref.txt")], 368),
    "replay": ([LOOP], 195),
    "switch": ([LOOP], 6),
}
KL_RING = "    {150, 146, 138},   // ring\n"
LY_CALL = ("    *nLabels = PlaceLabels(in.peaks, towns, order, nTowns, in.radiusMi, Inner(R), fixed, nFixed, peaks, &nPeaks, "
           "labels);\n")
RPT_KEY = "    k.which = (int32_t)in.which;\n"


def ref_rows(name):
    """[(row, places a town label)] in the reference's order."""
    rows, towns = [], set()
    for l in open(os.path.join(WX, name), encoding="utf-8"):
        if l.startswith("ROW "):
            rows.append(l.split()[1])
        elif l.startswith("LABEL "):
            towns.add(l.split()[1])
    return [(r, r in towns) for r in rows]


def tree_sha():
    paths = sorted(os.path.join(d, f) for d, _, fs in os.walk(SRC) for f in fs) + [FONT]
    h = hashlib.sha256()
    for p in paths:
        h.update(os.path.relpath(p, ROOT).encode() + b"\0" + open(p, "rb").read())
    return h.hexdigest()


def read(p):
    return open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")


def plant(text, old, new, name, pairs=None):
    sc.prove_colour(name, old, new, pairs)   # Q50: refuses a colour the panel cannot see
    assert text.count(old) == 1, (name, old[:60], text.count(old))
    out = text.replace(old, new)
    assert out != text, name
    return out


def run_suites(td, label, edits):
    """edits: {file name in src/weatherscope: its planted text}. Returns the set of failing "suite:check", or None (RIG)."""
    base = os.path.join(td, label)
    shutil.copytree(SRC, os.path.join(base, "src", "weatherscope"))
    os.makedirs(os.path.join(base, "test", "fixtures", "wx"))
    shutil.copy(FONT, os.path.join(base, "test", "fixtures", "wx", "glcdfont.c"))
    for f, text in edits.items():
        open(os.path.join(base, "src", "weatherscope", f), "w", encoding="utf-8", newline="\n").write(text)
    fails = set()
    for suite, (args, checks) in SUITES.items():
        exe = os.path.join(td, f"{label}-{suite}.exe")
        b = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(ROOT, "include"), "-I" + os.path.join(base, "src", "weatherscope"),
                            os.path.join(ROOT, "test", "host", f"test_wx_{suite}.cpp"), "-o", exe], capture_output=True, text=True)
        if b.returncode != 0:
            print(f"{label}/{suite}: did not compile (the RIG)\n{b.stderr[-1500:]}")
            return None
        r = subprocess.run([exe, *args], capture_output=True, text=True)
        m = re.search(r"^(\d+) checks, (\d+) failures$", r.stdout, re.M)
        if r.returncode not in (0, 1) or not m or int(m.group(1)) != checks:
            print(f"{label}/{suite}: did not run as {checks} checks (exit {r.returncode}): the RIG\n{r.stdout[-400:]}")
            return None
        for x in re.finditer(r"^\s*FAIL (\S+)", r.stdout, re.M):
            fails.add(f"{suite}:{x.group(1)}")
    return fails


def report(name, got, frozen, note=""):
    good = got is not None and got == frozen
    if got is None:
        print(f"{name}: the RIG -> OFF-PREDICTION")
    else:
        extra, missing = sorted(got - frozen), sorted(frozen - got)
        by = {s: len([g for g in got if g.startswith(s + ":")]) for s in SUITES}
        print(f"{name}: {len(got)} failing {by}, predicted {len(frozen)}{note}; extra {extra[:8]}"
              f"{' ...' if len(extra) > 8 else ''}, missing {missing[:8]}{' ...' if len(missing) > 8 else ''} -> "
              f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
    return good


def main():
    before = tree_sha()
    ok = True
    dallas, bend = ref_rows("k35.ref.txt"), ref_rows("k35b.ref.txt")
    if len(dallas) != 25 or len(bend) != 57:
        print(f"the references hold {len(dallas)} Dallas and {len(bend)} Bend rows, not 25 and 57: the RIG")
        return 2
    none = sorted(r for r, t in dallas + bend if not t)
    want_none = sorted(["bdry-15-" + v for v in ("towns", "water", "rivers", "roads", "peaks", "B", "C")] +
                       ["brain-15-B", "brain-15-C"])
    split = none == want_none
    ok &= split
    print(f"S-LY's set off the references: {len(dallas) + len(bend) - len(none)} scenes place a town label, {len(none)} "
          f"none {none} -> {'as A12.60 froze (73 / 9)' if split else 'NOT A12.60 (73 / 9)'}")

    theme, screens = read(os.path.join(SRC, "WxTheme.h")), read(os.path.join(SRC, "WxScreens.h"))
    s_kl = plant(theme, KL_RING, "    {158, 146, 138},   // ring SABOTAGE\n", "S-KL")
    s_ly = plant(screens, LY_CALL, LY_CALL.replace("order, nTowns,", "order, in.which == wxtheme::Which::Light ? 0 : nTowns,")
                 .replace(";\n", ";   // SABOTAGE\n"), "S-LY")
    s_rpt = plant(screens, RPT_KEY, "    // SABOTAGE: the theme left out of the key\n", "S-RPT")
    kl = {"k35:KL:" + r for r, _ in dallas} | {"k35b:KL:" + r for r, _ in bend}
    ly = ({f"k35:{p}:{r}" for r, t in dallas if t for p in ("KL", "LY")} |
          {f"k35b:{p}:{r}" for r, t in bend if t for p in ("KL", "LY")})
    rpt = ({"replay:RPT:dallas-%02d" % i for i in range(8, 15)} | {"k35b:RPT:bend-%02d" % i for i in range(8, 15)} |
           {"switch:SW1", "switch:SW4"})
    with tempfile.TemporaryDirectory() as td:
        ctl = run_suites(td, "control", {})
        good_c = ctl == set()
        ok &= good_c
        print(f"control (the unplanted mirror): failing {sorted(ctl) if ctl is not None else 'RIG'} -> "
              f"{'AS PREDICTED' if good_c else 'OFF-PREDICTION'}")
        ok &= report("S-KL", run_suites(td, "S-KL", {"WxTheme.h": s_kl}), kl, " (K-light: 25 Dallas, 57 Bend)")
        ok &= report("S-LY", run_suites(td, "S-LY", {"WxScreens.h": s_ly}), ly, " (K-light and LY on 73 scenes)")
        ok &= report("S-RPT", run_suites(td, "S-RPT", {"WxScreens.h": s_rpt}), rpt, " (RP-T 8-14 of both series, SW1, SW4)")
    after = tree_sha()
    ok &= after == before
    print(f"the real tree unchanged: {after == before} ({after[:12]})")
    print(f"3 sabotages: {'ALL AS PREDICTED' if ok else 'NOT ALL AS PREDICTED'}")
    return 0 if ok else 1


def anchor_entries():
    """Q51: S-KL's anchor in WxTheme.h, S-LY's and S-RPT's in WxScreens.h, for --anchors (no plant runs)."""
    s, t = os.path.join(SRC, "WxScreens.h"), os.path.join(SRC, "WxTheme.h")
    return [("S-KL", os.path.relpath(t, ROOT), sc.read(t), KL_RING),
            ("S-LY", os.path.relpath(s, ROOT), sc.read(s), LY_CALL),
            ("S-RPT", os.path.relpath(s, ROOT), sc.read(s), RPT_KEY)]

# --control (Q57, docs/weatherscope-stage3.md A12.61): this driver's unplanted run alone, each suite against its own
# count, nothing planted. run.sh runs it for every driver.
CONTROL_TABLE = {f"test_wx_{s}": ("checks", n) for s, (_, n) in SUITES.items()}


def control():
    with tempfile.TemporaryDirectory() as td:
        run_suites(td, "control", {})


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
