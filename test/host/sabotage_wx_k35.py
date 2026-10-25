"""Runs test_wx_k35.cpp against SABOTAGED copies of the device's screen code, and of its reference. Each must fail
EXACTLY the set registered in docs/weatherscope-stage3.md A9.18 before the run, and nothing else.

    python -I test/host/sabotage_wx_k35.py

Method:
  - The plants go into a MIRROR of src/weatherscope (and the vendored font, which WxRaster.h includes by a relative
    path), never the real files: a quoted #include finds its sibling first, so a single copied header would be
    silently bypassed. Each plant asserts its anchor occurs once and that the copy differs; the real tree's files are
    hashed before and after. The unmodified mirror runs first, as the control: 28 checks, 0 failures.
  - S-K4 plants into a copy of the REFERENCE (test/fixtures/wx/k35.ref.txt), so the label comparison is shown to be live.
"""
import hashlib, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src", "weatherscope")
FONT = os.path.join(ROOT, "test", "fixtures", "wx", "glcdfont.c")
REF = os.path.join(ROOT, "test", "fixtures", "wx", "k35.ref.txt")
LOOP = os.path.join(ROOT, "test", "fixtures", "wx", "loop")
TEST = os.path.join(ROOT, "test", "host", "test_wx_k35.cpp")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1"] + (
    ["-static-libgcc", "-static-libstdc++"] if "mingw" in CXX.lower() else [])

ROWS = ["dry-60", "approaching-60", "herenow-60", "warning-60", "stale-60", "clear-60", "approaching-30",
        "approaching-15", "dry-30", "dry-15"] + ["loop-%02d" % i for i in range(15)]

# name: (file, [(anchor, replacement)], the registered failing set)
SABOTAGES = {
    "S-K1": ("WxRaster.h", [("kLabel{176, 176, 176}", "kLabel{184, 176, 176} /* SABOTAGE */")],
             {"K:" + r for r in ROWS}),
    "S-K2": ("WxScreens.h", [("        const int kx = 60 + in.loopIndex * 116 / (kLoopFrames - 1);",
                              "        const int kx = 60 + in.loopIndex * 120 / (kLoopFrames - 1);  // SABOTAGE")],
             {"K:loop-%02d" % i for i in range(2, 15)}),
    "S-K3": ("WxScreens.h", [("    const int travel = (fromDeg + 180) % 360;",
                              "    const int travel = fromDeg % 360;  // SABOTAGE")],
             {"K:warning-60", "AR1PIX"}),
    "S-K4": ("k35.ref.txt", None, {"K:dry-60"}),
    "S-K5": ("WxScreens.h", [("constexpr int64_t kStaleAfterS = 1800;", "constexpr int64_t kStaleAfterS = 3600;  // SABOTAGE")],
             {"K:stale-60"}),
}


def tree_sha():
    """Every file under src/weatherscope (it holds subdirectories), the font and the reference."""
    paths = sorted(os.path.join(d, f) for d, _, fs in os.walk(SRC) for f in fs) + [FONT, REF]
    h = hashlib.sha256()
    for path in paths:
        h.update(os.path.relpath(path, ROOT).encode() + b"\0" + open(path, "rb").read())
    return h.hexdigest()


def mirror(td, name):
    base = os.path.join(td, name)
    shutil.copytree(SRC, os.path.join(base, "src", "weatherscope"))
    os.makedirs(os.path.join(base, "test", "fixtures", "wx"))
    shutil.copy(FONT, os.path.join(base, "test", "fixtures", "wx", "glcdfont.c"))
    shutil.copy(REF, os.path.join(base, "k35.ref.txt"))
    return base


def plant_file(path, edits, name):
    src = open(path, encoding="utf-8").read().replace("\r\n", "\n")
    out = src
    for anchor, repl in edits:
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    open(path, "w", encoding="utf-8", newline="\n").write(out)


def plant_ref(path):
    """S-K4: the first LABEL line of dry-60, its x0 moved by 1 px."""
    lines = open(path, encoding="utf-8").read().replace("\r\n", "\n").split("\n")
    i = next(k for k, l in enumerate(lines) if l.startswith("LABEL dry-60 "))
    f = lines[i].split(" ")
    f[4] = str(int(f[4]) + 1)
    new = " ".join(f)
    assert new != lines[i]
    lines[i] = new
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines))


def run(base, name):
    exe = os.path.join(base, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(base, "src", "weatherscope"), "-I" + os.path.join(ROOT, "include"),
                        TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        sys.exit(2)
    r = subprocess.run([exe, LOOP, os.path.join(base, "k35.ref.txt")], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the test did not run (exit {r.returncode}): the RIG\n{r.stdout[-2000:]}")
        sys.exit(2)
    m = re.search(r"^(\d+) checks, (\d+) failures$", r.stdout, re.M)
    if not m or int(m.group(1)) != 28:
        print(f"{name}: {m.group(1) if m else '?'} checks, not 28: the RIG")
        sys.exit(2)
    return r.returncode, {x.group(1) for x in re.finditer(r"^\s*FAIL (\S+)", r.stdout, re.M)}


def main():
    before = tree_sha()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        rc, fails = run(mirror(td, "control"), "control")
        good = rc == 0 and not fails
        print(f"control (the real code, mirrored): exit {rc}, failing {sorted(fails)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        for name, (target, edits, expect) in SABOTAGES.items():
            base = mirror(td, name)
            if edits is None:
                plant_ref(os.path.join(base, "k35.ref.txt"))
            else:
                plant_file(os.path.join(base, "src", "weatherscope", target), edits, name)
            rc, fails = run(base, name)
            good = rc == 1 and fails == expect
            shown = sorted(fails) if len(fails) <= 6 else f"{len(fails)} cases: {sorted(fails)[:3]} ..."
            want = sorted(expect) if len(expect) <= 6 else f"{len(expect)} cases"
            print(f"{name} ({target}): failing {shown}, predicted {want} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            if not good:
                print(f"    unexpected: {sorted(fails - expect)}; missing: {sorted(expect - fails)}")
            ok &= good
    after = tree_sha()
    print(f"the real src/weatherscope, font and reference unchanged: {before == after} ({after[:12]})")
    print(f"{len(SABOTAGES)} sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
