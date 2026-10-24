"""Runs test_wx_layout.cpp against SABOTAGED copies of src/weatherscope/WxScreens.h. Each must fail EXACTLY the set of
named cases frozen in docs/weatherscope-stage3.md A9.10, and nothing else.

    python -I test/host/sabotage_wx_layout.py

Method, fixed before any run (A9.14):
  - The real header is never edited. Each plant asserts its anchor occurs once and that the copy differs, and the real
    file's hash is compared before and after. The unmodified header runs first, as the control: 0 failures.
  - Every result is graded against A9.10's frozen set. S-IC also carries the set A9.14 registered before the run
    ({IC1, IC4}: IC3's left box is within 4 px of its fixed overlay, so it is skipped with or without the inner
    circle). A result that matches only A9.14's set is still OFF A9.10, and is reported as such: the stop stands.
  - S-CAP is planted at the cap's USE SITE in PlaceTowns, not in Cap() (A9.14): "the cap is 8 at every zoom" for the
    placement, with ZM7 still asking Cap() its own question.
  - S-STALE changes all three age comparisons (the 30 min one in StateLine and TimeText, the 10 min one in TimeText):
    ">= instead of >, at both thresholds".
"""
import hashlib, os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "src", "weatherscope", "WxScreens.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_layout.cpp")
LOOP = os.path.join(ROOT, "test", "fixtures", "wx", "loop")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

# name: (edits, A9.10's frozen set, A9.14's pre-registered set or None)
SABOTAGES = {
    "S-RANK": ([("    for (int i = 1; i < n; ++i) {\n        const int v = order[i];",
                 "    for (int i = n; i < n; ++i) {  // SABOTAGE: file order\n        const int v = order[i];")],
               {"LB1", "LB2a", "LB3", "LB4", "LB5"}, None),
    "S-IC": ([("    if ((x - c) * (x - c) + (y - c) * (y - c) < inner * inner) return p;\n",
               "    (void)inner;  // SABOTAGE: no anchor test\n"),
              ("        if (!ClearsCentre(b, inner)) continue;\n", "        // SABOTAGE: no box test\n")],
             {"IC1", "IC3", "IC4"}, {"IC1", "IC4"}),
    "S-OV": ([("constexpr int kAir = 4;", "constexpr int kAir = 0;  // SABOTAGE")],
             {"OV2", "OV4", "OV5b", "OV5d", "OV6b", "SD3"}, None),
    "S-SIDE": ([("    if (x < c) {\n        const Box b = cands[0];",
                 "    if (false) {  // SABOTAGE: always right first\n        const Box b = cands[0];")],
               {"SD2", "OV6a"}, None),
    "S-CAP": ([("    const int cap = Cap(radiusMi);\n",
                "    const int cap = 8;  // SABOTAGE: 8 at every zoom\n    (void)radiusMi;\n")],
              {"LB4", "LB5"}, None),
    "S-RING": ([("        if (0.15 * R <= kAll[i] && kAll[i] <= 0.85 * R) mi[n++] = kAll[i];",
                 "        if (0.0 * R <= kAll[i] && kAll[i] <= 1.0 * R) mi[n++] = kAll[i];  // SABOTAGE")],
               {"RG1", "RG2", "RG3"}, None),
    "S-ST15": ([("        if (in.echo.mi <= 15.0) *ink = wxraster::kWhite;",
                 "        if (in.echo.mi < 15.0) *ink = wxraster::kWhite;  // SABOTAGE")], {"ST3"}, None),
    "S-STALE": ([("    if (in.ageS > kStaleAfterS) {", "    if (in.ageS >= kStaleAfterS) {  // SABOTAGE"),
                 ("    if (warning && ageS > kStaleAfterS) snprintf", "    if (warning && ageS >= kStaleAfterS) snprintf"),
                 ("    *ink = ageS > kOrangeAfterS ?", "    *ink = ageS >= kOrangeAfterS ?")],
                {"TM1", "TM4", "ST13"}, None),
    "S-INVIEW": ([("            if (sqrt(d2) / wxlogic::kKmPerMi > boundMi) continue;",
                   "            (void)boundMi;  // SABOTAGE: the 60.0 mi bound dropped")], {"ST11a"}, None),
    "S-ARROW": ([("    const int travel = (fromDeg + 180) % 360;",
                  "    const int travel = fromDeg % 360;  // SABOTAGE: along fromDeg")], {"AR1", "AR3"}, None),
    "S-LADDER": ([("    radarzoom::Ladder l;\n    l.step[0] = 15.0f;",
                   "    return radarzoom::BuildLadder(60.0f);  // SABOTAGE\n    radarzoom::Ladder l;\n    l.step[0] = 15.0f;")],
                 {"ZM1", "ZM2", "ZM3", "ZM4"}, None),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    return out


def run(td, name, text):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, "WxScreens.h"), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "src", "weatherscope"),
                        "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        sys.exit(2)
    r = subprocess.run([exe, LOOP], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the suite did not run (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
        sys.exit(2)
    m = re.search(r"^(\d+) checks, (\d+) failures$", r.stdout, re.M)
    if not m or int(m.group(1)) != 73:
        print(f"{name}: {m.group(1) if m else '?'} cases ran, not 73: the RIG")
        sys.exit(2)
    return r.returncode, {x.group(1) for x in re.finditer(r"^\s*FAIL (\S+)", r.stdout, re.M)}


def main():
    before = sha(HEADER)
    src = open(HEADER, encoding="utf-8").read().replace("\r\n", "\n")
    ok, off = True, []
    with tempfile.TemporaryDirectory() as td:
        rc, fails = run(td, "control", src)
        good = rc == 0 and not fails
        print(f"control (the real header): exit {rc}, failing {sorted(fails)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        for name, (edits, frozen, a914) in SABOTAGES.items():
            rc, fails = run(td, name, plant(name, edits, src))
            good = rc == 1 and fails == frozen
            if good:
                verdict = "AS PREDICTED (A9.10)"
            elif a914 is not None and rc == 1 and fails == a914:
                verdict = f"OFF A9.10's {sorted(frozen)}; AS A9.14 REGISTERED BEFORE THE RUN"
            else:
                verdict = "OFF-PREDICTION"
            if not good:
                off.append(name)
            print(f"{name}: failing {sorted(fails)}, predicted {sorted(frozen)} -> {verdict}")
            ok &= good
    after = sha(HEADER)
    print(f"the real src/weatherscope/WxScreens.h unchanged: {before == after} ({after[:12]})")
    print(f"{len(SABOTAGES)} sabotages: " + ("ALL AS PREDICTED" if ok and before == after else
                                              f"OFF-PREDICTION against A9.10 ({', '.join(off) or 'the control'}): STOP"))
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
