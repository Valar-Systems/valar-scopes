"""Runs test_wx_replay.cpp and test_wx_k35.cpp against SABOTAGED copies of src/weatherscope/WxScreens.h. Each must fail
EXACTLY the sets registered in docs/weatherscope-stage3.md A9.23, and nothing else.

    python -I test/host/sabotage_wx_replay.py

  S-RP1  a layer kept across a zoom change: SameLayer ignores the radius.
         test_wx_replay fails exactly the 60 rows at 30 and 15 mi, in both warning states; test_wx_k35 fails nothing.
  S-RP2  one label missing from the layer: the build erases its first placed label's box from the layer's image.
         test_wx_replay fails all 90 rows; test_wx_k35 fails exactly the 15 K:loop rows.

The plants go into a MIRROR of src/weatherscope (and the font WxRaster.h includes by a relative path), never the real
files; each asserts its anchor occurs once and that the copy differs; the real files are hashed before and after.
The unmodified mirror runs first, as the control: 90/0 and 28/0.
"""
import hashlib, os, re, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src", "weatherscope")
FONT = os.path.join(ROOT, "test", "fixtures", "wx", "glcdfont.c")
REF = os.path.join(ROOT, "test", "fixtures", "wx", "k35.ref.txt")
LOOP = os.path.join(ROOT, "test", "fixtures", "wx", "loop")
TESTS = {"replay": os.path.join(ROOT, "test", "host", "test_wx_replay.cpp"),
         "k35": os.path.join(ROOT, "test", "host", "test_wx_k35.cpp")}
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1"] + (
    ["-static-libgcc", "-static-libstdc++"] if "mingw" in CXX.lower() else [])

ROWS = {w: [f"RP:{w}-{R}-{i:02d}" for R in (60, 30, 15) for i in range(15)] for w in ("off", "warn")}
KLOOP = {f"K:loop-{i:02d}" for i in range(15)}

SABOTAGES = {
    "S-RP1": ([("inline bool SameLayer(const LayerKey& a, const LayerKey& b) { return memcmp(&a, &b, sizeof a) == 0; }",
                "inline bool SameLayer(const LayerKey& a, const LayerKey& b) { LayerKey x = a; x.radiusMi = b.radiusMi; "
                "return memcmp(&x, &b, sizeof a) == 0; }  // SABOTAGE: the radius ignored")],
              {"replay": {r for w in ROWS for r in ROWS[w] if not r.split("-")[1] == "60"}, "k35": set()}),
    "S-RP2": ([("    DrawInvariant(sink, in, l.view, l.labels, &l.nLabels);\n",
                "    DrawInvariant(sink, in, l.view, l.labels, &l.nLabels);\n"
                "    if (l.nLabels) {  // SABOTAGE: the first label erased from the layer\n"
                "        const Box& e = l.labels[0].at.box;\n"
                "        uint8_t* q = (uint8_t*)l.px32;\n"
                "        for (int y = e.y0; y <= e.y1; ++y)\n"
                "            for (int x = e.x0; x <= e.x1; ++x)\n"
                "                if (x >= 0 && x < 240 && y >= 0 && y < 240) q[y * 240 + x] = 0;\n"
                "    }\n")],
              {"replay": set(ROWS["off"]) | set(ROWS["warn"]), "k35": KLOOP}),
}


def tree_sha():
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
    return base


def plant(path, edits, name):
    src = open(path, encoding="utf-8").read().replace("\r\n", "\n")
    out = src
    for anchor, repl in edits:
        sc.prove_colour(name, anchor, repl)   # Q50: refuses a colour the panel cannot see
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    open(path, "w", encoding="utf-8", newline="\n").write(out)


def run(base, name, which):
    exe = os.path.join(base, f"{name}-{which}.exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + os.path.join(base, "src", "weatherscope"), "-I" + os.path.join(ROOT, "include"),
                        TESTS[which], "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}/{which}: did not compile (the RIG, not a result)\n{b.stderr}")
        sys.exit(2)
    args = [exe, LOOP] if which == "replay" else [exe, LOOP, REF]
    r = subprocess.run(args, capture_output=True, text=True)
    want = 90 if which == "replay" else 28
    m = re.search(r"^(\d+) checks, (\d+) failures$", r.stdout, re.M)
    if r.returncode not in (0, 1) or not m or int(m.group(1)) != want:
        print(f"{name}/{which}: exit {r.returncode}, {m.group(1) if m else '?'} checks (want {want}): the RIG")
        sys.exit(2)
    return r.returncode, {x.group(1) for x in re.finditer(r"^\s*FAIL (\S+)", r.stdout, re.M)}


def main():
    before = tree_sha()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        base = mirror(td, "control")
        for which in ("replay", "k35"):
            rc, fails = run(base, "control", which)
            good = rc == 0 and not fails
            print(f"control/{which}: exit {rc}, failing {sorted(fails)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
        for name, (edits, expect) in SABOTAGES.items():
            base = mirror(td, name)
            plant(os.path.join(base, "src", "weatherscope", "WxScreens.h"), edits, name)
            for which in ("replay", "k35"):
                rc, fails = run(base, name, which)
                want = expect[which]
                good = fails == want and rc == (1 if want else 0)
                print(f"{name}/{which}: failing {len(fails)} {sorted(fails)[:3]}{' ...' if len(fails) > 3 else ''}, "
                      f"predicted {len(want)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
                if not good:
                    print(f"    unexpected: {sorted(fails - want)[:10]}; missing: {sorted(want - fails)[:10]}")
                ok &= good
    after = tree_sha()
    print(f"the real src/weatherscope, font and reference unchanged: {before == after} ({after[:12]})")
    print("2 sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return 0 if ok and before == after else 1


def anchor_entries():
    """Q51: every plant's anchor in WxScreens.h, for --anchors (no plant runs)."""
    p = os.path.join(SRC, "WxScreens.h")
    return [(name, os.path.relpath(p, ROOT), sc.read(p), a) for name, (edits, _) in SABOTAGES.items() for a, _ in edits]

# --control (Q57, docs/weatherscope-stage3.md A12.61): this driver's unplanted run alone, each suite against its own
# count, nothing planted. run.sh runs it for every driver.
CONTROL_TABLE = {"test_wx_replay": ("checks", 90), "test_wx_k35": ("checks", 28)}


def control():
    with tempfile.TemporaryDirectory() as td:
        base = mirror(td, "control")
        for which in ("replay", "k35"):
            run(base, "control", which)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
