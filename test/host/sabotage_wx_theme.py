"""Runs test_wx_theme.cpp against SABOTAGED copies of src/weatherscope/WxTheme.h, or of the test's own metric code.
Each must fail EXACTLY the set of named checks frozen in docs/weatherscope-stage3.md A12.43, A12.44 and A12.46, and
nothing else.

    python -I test/host/sabotage_wx_theme.py

Method, fixed before any run (A12.46):
  - The real files are never edited. A plant in the header goes into a copy of WxTheme.h in a temporary directory
    searched first; a plant in a metric (CIEDE2000, WCAG, the RGB565 conversion) goes into a copy of the test itself.
    Each plant asserts its anchor occurs exactly once and that the copy differs; the real files' hashes are compared
    before and after.
  - The unmodified files run first, as the control: 72 checks, 0 failures.
  - A plant keeps every variable used: under -Werror an unused one is a compile failure, which is the RIG.
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "src", "weatherscope", "WxTheme.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_theme.cpp")
FIX = os.path.join(ROOT, "test", "fixtures", "wx")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
CHECKS = 72
G565 = {"C4g565:" + k for k in ("label/halo", "label/paper", "dim/paper", "ringnum/halo", "peak/halo", "otext/paper",
                                "otext/halo", "accent/paper", "warn/paper")}

# name: (which file, [(anchor, replacement)], the frozen failing set)
SABOTAGES = {
    "S-C4a": ("header", [("{171, 204, 237}, {96, 146, 206}, {178, 150, 112}}",
                          "{163, 196, 228}, {96, 146, 206}, {178, 150, 112}}  /* SABOTAGE: the old water */")],
              {"C4a:1", "C4e:1"}),
    "S-C4b": ("header", [("{135, 135, 0}, {155, 128, 0},", "{135, 135, 0}, {231, 192, 0},  /* SABOTAGE: code 6 back */")],
              {"C4a:6"}),
    "S-C4c": ("header", [("    {93, 93, 93},      // noCoverage", "    {99, 99, 99},      // noCoverage SABOTAGE")], {"C4c"}),
    "S-C4d": ("header", [("    {124, 124, 124},   // hatchLine", "    {128, 128, 128},   // hatchLine SABOTAGE")], {"C4d"}),
    "S-C4e": ("header", [("{{0, 108, 206}, {0, 95, 177},", "{{0, 108, 206}, {0, 108, 206},  /* SABOTAGE: snow 2 = snow 1 */")],
              {"C4e:2", "C3e'", "C3h'"}),
    "S-C4g": ("header", [("    {159, 85, 0},      // orangeText", "    {161, 86, 0},      // orangeText SABOTAGE")],
              {"C4g888:otext/paper", "C4g565:otext/paper", "C4g565:otext/halo"}),
    "S-HOLD": ("header", [("    {0, 200, 0},       // holdTrack: lighter than the arc, so the progress stays the strong mark\n"
                           "    {0, 90, 0},        // holdArc\n",
                           "    {0, 90, 0},        // holdTrack SABOTAGE: un-swapped\n"
                           "    {0, 200, 0},       // holdArc SABOTAGE: un-swapped\n")],
               {"C4-HOLD1", "C4-HOLD2"}),
    "S-TS": ("header", [("    return p == daynight::Phase::Day ? Which::Light : Which::Dark;\n",
                         "    return p == daynight::Phase::Night ? Which::Dark : Which::Light;   // SABOTAGE: Unknown -> Light\n")],
             {"TS1"}),
    "S-TP": ("header", [('    if (strcmp(s, "light") == 0) return Parsed{Setting::Light, Read::Known};\n',
                         '    if (strcmp(s, "light") == 0 || strcmp(s, "Light") == 0) return Parsed{Setting::Light, Read::Known};'
                         '   // SABOTAGE: folds case\n')],
             {"TP6"}),
    "S-TD": ("header", [("inline bool NightDim(bool autoDim, daynight::Phase p) { return autoDim && p == daynight::Phase::Night; }",
                         "inline bool NightDim(bool autoDim, daynight::Phase p) { return autoDim && p != daynight::Phase::Day; }"
                         "  // SABOTAGE")],
             {"TD3"}),
    "S-CTL1": ("test", [("    const double Rt = -std::sin(Rad(2 * dth)) * Rc;\n",
                         "    const double Rt = 0.0 * dth * Rc;   // SABOTAGE: no R_T\n")],
               {"C4-CTL1"}),
    "S-CTL2": ("test", [("double RelLum(Rgb c) { return 0.2126 * Lin(c.r) + 0.7152 * Lin(c.g) + 0.0722 * Lin(c.b); }",
                         "double RelLum(Rgb c) { return 0.2126 * std::pow(c.r / 255.0, 2.2) + 0.7152 * std::pow(c.g / 255.0, 2.2) "
                         "+ 0.0722 * std::pow(c.b / 255.0, 2.2); }  // SABOTAGE: a flat 2.2 gamma")],
               {"C4-CTL2"} | G565),
    "S-565": ("test", [("    return Rgb{(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)), (uint8_t)((b << 3) | (b >> 2))};\n",
                        "    return Rgb{(uint8_t)(r << 3), (uint8_t)(g << 2), (uint8_t)(b << 3)};   // SABOTAGE: zero fill\n")],
              set(G565)),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def read(p):
    return open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")


def run(td, name, header, test):
    inc = os.path.join(td, name)
    os.makedirs(inc, exist_ok=True)
    open(os.path.join(inc, "WxTheme.h"), "w", encoding="utf-8", newline="\n").write(header)
    src = os.path.join(inc, "test_wx_theme.cpp")
    open(src, "w", encoding="utf-8", newline="\n").write(test)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "include"),
                        "-I" + os.path.join(ROOT, "src", "weatherscope"), src, "-o", exe], capture_output=True, text=True)
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
    return r.returncode, {ln.split()[1] for ln in lines}, lines


def main():
    before = {"WxTheme.h": sha(HEADER), "test_wx_theme.cpp": sha(TEST)}
    real = {"header": read(HEADER), "test": read(TEST)}
    ok = True
    with tempfile.TemporaryDirectory() as td:
        rc, fails, _ = run(td, "control", real["header"], real["test"])
        good = rc == 0 and fails == set()
        ok &= good
        print(f"control (the real files): exit {rc}, failing {sorted(fails or [])} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        for name, (which, edits, frozen) in SABOTAGES.items():
            texts = dict(real)
            src = texts[which]
            try:
                for old, new in edits:
                    sc.prove_colour(name, old, new)   # Q50: refuses a colour the panel cannot see
            except sc.Refused as e:   # Q50: not a result, and not as predicted
                print(f"{name}: REFUSED -- {e}")
                ok = False
                continue
            for old, new in edits:
                assert src.count(old) == 1, (name, old[:50], src.count(old))
                src = src.replace(old, new)
            assert src != texts[which], name
            texts[which] = src
            rc, fails, lines = run(td, name, texts["header"], texts["test"])
            good = fails is not None and fails == frozen
            ok &= good
            print(f"{name} ({which}): failing {sorted(fails or [])}, predicted {sorted(frozen)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            for line in lines:
                print(f"    {line}")
    after = {"WxTheme.h": sha(HEADER), "test_wx_theme.cpp": sha(TEST)}
    same = after == before
    ok &= same
    print(f"the real files unchanged: {same} ({', '.join(k + ' ' + v[:12] for k, v in after.items())})")
    print(f"{len(SABOTAGES)} sabotages: {'ALL AS PREDICTED' if ok else 'NOT ALL AS PREDICTED'}")
    return 0 if ok else 1


def anchor_entries():
    """Q51: every plant's anchor in the header or the test it plants, for --anchors (no plant runs)."""
    files = {"header": HEADER, "test": TEST}
    return [(name, os.path.relpath(files[which], ROOT), sc.read(files[which]), a)
            for name, (which, edits, _) in SABOTAGES.items() for a, _ in edits]

# --control (Q57, docs/weatherscope-stage3.md A12.61): this driver's unplanted run alone, each suite against its own
# count, nothing planted. run.sh runs it for every driver.
CONTROL_TABLE = {"test_wx_theme": ("checks", 72)}


def control():
    with tempfile.TemporaryDirectory() as td:
        run(td, "control", read(HEADER), read(TEST))


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
