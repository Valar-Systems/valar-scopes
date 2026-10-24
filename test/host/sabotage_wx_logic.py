"""Runs test_wx_logic.cpp against SABOTAGED copies of src/weatherscope/WxLogic.h. Each must fail EXACTLY the set of
named cases frozen in docs/weatherscope-stage3.md (A8.2, A8.8), and nothing else.

    python -I test/host/sabotage_wx_logic.py

Method, fixed before any run:
  - The real header is never edited. Each plant asserts its anchor occurs once and that the copy differs.
  - RK (the real KDFW crop) is graded ONLY under S-R2, where A8.2 says it differs. Under every other sabotage, RK's
    result is printed and not graded, because A8.2 predicts nothing for it there.
  - S-LP is "overlap tested against every EARLIER candidate, drawn or skipped". The other reading, every candidate,
    would also skip LP2's nearest label, which A8.8's exact set {LP3} excludes.
  - The unmodified header is run first, as the control: 0 failures.
"""
import hashlib, os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "src", "weatherscope", "WxLogic.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_logic.cpp")
FIXTURES = os.path.join(ROOT, "test", "fixtures", "wx")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "S-R1": ([("            if (!wxcrop::IsEcho(code)) continue;",
               "            if (!(code >= 2 && code <= 13)) continue;  // SABOTAGE")], {"R2", "R4"}),
    "S-R2": ([("    const double kx = kKmPerDeg * cos(here.lat * (kPi / 180.0));",
               "    const double kx = kKmPerDeg;  // SABOTAGE")], {"R7", "RK"}),
    "S-P": ([("    if (hereNow != Precip::None) return Choice{Screen::PrecipNow, hereNow, -1};\n"
              "    if (approaching) return Choice{Screen::PrecipApproaching, Precip::None, -1};",
              "    if (approaching) return Choice{Screen::PrecipApproaching, Precip::None, -1};  // SABOTAGE\n"
              "    if (hereNow != Precip::None) return Choice{Screen::PrecipNow, hereNow, -1};")], {"P2"}),
    "S-C": ([("    for (size_t i = 0, j = n - 1; i < n; j = i++) if (OnSegment(ring[j], ring[i], p)) return true;   // the boundary counts",
              "    // SABOTAGE: no boundary test")], {"C7"}),
    "S-W": ([('    if (strcmp(e, "Tornado Warning") == 0) return 0;',
              '    if (strncmp(e, "Tornado", 7) == 0) return 0;  // SABOTAGE')], {"W5"}),
    "S-W3": ([("    if (ma.valid && mb.valid) {", "    if (false && ma.valid && mb.valid) {  // SABOTAGE")], {"W11"}),
    "S-E": ([("    return nowUtc >= w.expiresUtc;", "    return nowUtc > w.expiresUtc;  // SABOTAGE")], {"E2"}),
    "S-M": ([("Compass8(fmod(m.fromDeg + 180.0, 360.0)); }", "Compass8(fmod(m.fromDeg + 0.0, 360.0)); }  // SABOTAGE")],
            {"M2", "M3", "M4"}),
    "S-M2": ([("    for (int k = 1; k < m.nPos; ++k) if (DistanceMi(here, m.pos[k]) < DistanceMi(here, m.pos[best])) best = k;",
               "    (void)here; (void)m;  // SABOTAGE: the first position")], {"M11"}),
    "S-M3": ([('    if (m.speedKt == 0) snprintf(t.travel, sizeof(t.travel), "STATIONARY");',
               '    if (false) snprintf(t.travel, sizeof(t.travel), "STATIONARY");  // SABOTAGE')], {"M8"}),
    "S-M4": ([('    if (t.distanceMi < 0.5) snprintf(t.distance, sizeof(t.distance), "<1 MI");',
               '    if (false) snprintf(t.distance, sizeof(t.distance), "<1 MI");  // SABOTAGE')], {"M9"}),
    "S-U": ([("inline int KtToMph(double kt) { return (int)lround(kt * kMphPerKt); }",
              "inline int KtToMph(double kt) { return (int)(kt * kMphPerKt); }  // SABOTAGE")], {"U1", "U3", "U4"}),
    "S-L": ([("    if (n >= 5 && strcmp(out + n - 5, \" CITY\") == 0) out[n - 5] = '\\0';",
              "    for (char* q; (q = strstr(out, \"CITY\")) != nullptr;) memmove(q, q + 4, strlen(q + 4) + 1);"
              " for (size_t k = strlen(out); k > 0 && out[k - 1] == ' ';) out[--k] = '\\0'; (void)n;  // SABOTAGE")],
            {"L4"}),
    "S-H1": ([("    for (size_t e = 0; e < n; ++e) {", "    for (size_t e = 0; e < (n ? 1u : 0u); ++e) {  // SABOTAGE")],
             {"H8", "H20"}),
    "S-H2": ([('inline bool SnowCode(const char* p) { return Is2(p, "SN") || Is2(p, "SG") || Is2(p, "PL") || Is2(p, "IC"); }',
               'inline bool SnowCode(const char* p) { return Is2(p, "SG") || Is2(p, "PL") || Is2(p, "IC"); }  // SABOTAGE'),
              ('|| Is2(p, "GS") || Is2(p, "UP"); }', '|| Is2(p, "GS") || Is2(p, "UP") || Is2(p, "SN"); }  // SABOTAGE')],
             {"H4", "H7", "H9", "H20"}),
    "S-H3": ([("        if (SnowCode(g + i)) *snow = true;",
               "        if (*snow || *rain) return; if (SnowCode(g + i)) *snow = true;  // SABOTAGE: the first token decides")],
             {"H9", "H20"}),
    "S-H4": ([("    if (nowUtc - obsUtc > kObsMaxAgeS) return Precip::None;",
               "    if (nowUtc - obsUtc >= kObsMaxAgeS) return Precip::None;  // SABOTAGE")], {"H22"}),
    "S-H5": ([('    if (i + 1 < n && Is2(g + i, "VC")) return;                 // "in the vicinity" is not here (Q1)',
               "    // SABOTAGE: the VC prefix ignored")], {"H19"}),
    "S-T": ([('        if (w.tornadoDamageThreat && strcmp(w.tornadoDamageThreat, "CATASTROPHIC") == 0) k.Add("TORNADO EMERGENCY");',
              '        if (false) k.Add("TORNADO EMERGENCY");  // SABOTAGE')], {"T3"}),
    "S-B": ([("    if (dot) { while (n > 0 && num[n - 1] == '0') num[--n] = '\\0'; if (n > 0 && num[n - 1] == '.') num[--n] = '\\0'; }",
              "    (void)dot;  // SABOTAGE: trailing zeros kept")], {"B1", "B8b", "T1"}),
    "S-HL": ([('    if (c.screen == Screen::PrecipApproaching) return "PRECIP WITHIN 15 MI";',
               '    if (c.screen == Screen::PrecipApproaching) return "RAIN WITHIN 15 MI";  // SABOTAGE')], {"HL3"}),
    "S-LP": ([("        for (int k = 0; k < nd; ++k) if (Overlap(b, in[drawn[k]])) clash = true;",
               "        for (int k = 0; k < i; ++k) if (Overlap(b, in[order[k]])) clash = true;  // SABOTAGE: every earlier candidate")],
             {"LP3"}),
    "S-LP2": ([("        while (j >= 0 && in[v].distMi < in[order[j]].distMi) { order[j + 1] = order[j]; --j; }",
                "        while (j >= 0 && in[v].distMi > in[order[j]].distMi) { order[j + 1] = order[j]; --j; }  // SABOTAGE")],
              {"LP2"}),
    "S-X": ([("    const int local = hour * 60 + minute;",
              "    const int local = ((hour * 60 + minute - offMin) % 1440 + 1440) % 1440;  // SABOTAGE")], {"X1", "X2", "X3"}),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    return out


def run(td, name, header_text):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, "WxLogic.h"), "w", encoding="utf-8", newline="\n").write(header_text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe],
                       capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        sys.exit(2)
    r = subprocess.run([exe, FIXTURES], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the suite did not run (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
        sys.exit(2)
    fails = {m.group(1) for m in re.finditer(r"^\s*FAIL (\S+)", r.stdout, re.M)}
    return r.returncode, fails


def main():
    before = sha(HEADER)
    src = open(HEADER, encoding="utf-8").read().replace("\r\n", "\n")
    ok = True
    with tempfile.TemporaryDirectory() as td:
        rc, fails = run(td, "control", src)
        good = rc == 0 and not fails
        print(f"control (the real header): exit {rc}, failing {sorted(fails)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        for name, (edits, expect) in SABOTAGES.items():
            rc, fails = run(td, name, plant(name, edits, src))
            graded = fails if "RK" in expect else fails - {"RK"}
            good = rc == 1 and graded == expect
            rk_note = "" if "RK" in expect else f"  (RK, not graded here: {'FAIL' if 'RK' in fails else 'ok'})"
            print(f"{name}: failing {sorted(graded)}, predicted {sorted(expect)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}{rk_note}")
            ok &= good
    after = sha(HEADER)
    print(f"the real src/weatherscope/WxLogic.h unchanged: {before == after} ({after[:12]})")
    print(f"{len(SABOTAGES)} sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
