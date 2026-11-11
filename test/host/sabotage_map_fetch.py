"""Step A's sabotages (docs/blipscope-map-client-steps.md, step A; registered 2026-11-11, item 5): 23 plants, each in a
COPY of the tree. Each must turn red exactly its registered set across test_map_fetch.cpp (42 cases) and
check_map_fetch.py (CA12-CA14), no fewer and no more. The sets were computed before the code: the request plants' by the
scratch reference run under each plant, the policy and source plants' by applying the registered rules row by row.

  request (include/MapQuery.h)    S-CA-MARGIN, S-CA-EPSFLOOR, S-CA-FLOAT, S-CA-LEVELM, S-CA-CAP, S-CA-ZOOMFETCH
  policy  (include/MapPolicy.h)   S-CA-RA, S-CA-RAZERO, S-CA-404RETRY, S-CA-FLAT, S-CA-NORESET, S-CA-NOTIMEOUT,
                                  S-CA-GATE, S-CA-GATE1, S-CA-NOSPREAD, S-CA-SPREADALL, S-CA-DMOD, S-CA-CHANGEWAIT,
                                  S-CA-KEEPPKG, S-CA-NOSEQ
  source  (the check's files)     S-CA-NVS and S-CA-PRINTD in src/AircraftManager.cpp's map regions, S-CA-NORA in
                                  src/HttpRequestManager.cpp. The C++ test does not compile those files, so neither can
                                  turn a compile failure into extra red rows.

S-CA-FLOAT's float goes through a volatile float, so an i686 x87 host rounds it as the device would (Q-MC13).

Method: include/, the two route files the test reads its bounds from, and the three source files the check reads are
copied into a temporary tree with the repo's own layout; the plant goes into the copy only, after asserting its anchor
occurs exactly once and that the copy changed. The real files' sha256 are compared before and after. The unplanted copy
runs first, as the control.
    python -I test/host/sabotage_map_fetch.py [--anchors | --control]   exit 0 = as predicted, 1 = not, 2 = the rig"""
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes (docs/weatherscope-stage3.md A12.50, A12.61)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEST = os.path.join(ROOT, "test", "host", "test_map_fetch.cpp")
CHECK = os.path.join(ROOT, "test", "host", "check_map_fetch.py")
QUERY = os.path.join("include", "MapQuery.h")
POLICY = os.path.join("include", "MapPolicy.h")
AMCPP = os.path.join("src", "AircraftManager.cpp")
HRM = os.path.join("src", "HttpRequestManager.cpp")
COPIED = [os.path.join("proxy", "src", "basemap", "query.ts"), os.path.join("proxy", "src", "basemap", "budget.ts"),
          os.path.join("src", "AircraftManager.h"), AMCPP, HRM]
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]
PY = sys.executable

CA4 = {"CA4-202", "CA4-429", "CA4-edges"}
CA6 = {"CA6-404", "CA6-422", "CA6-400"}
CA7 = {"CA7-500", "CA7-503", "CA7-429", "CA7-transport", "CA7-timeout", "CA7-cap", "CA7-reset", "CA7b-401", "CA7b-403"}
CA1ALL = {"CA1", "CA1b", "CA1c", "CA1d", "CA1e", "CA1f"}

NO_PACKAGE = ("    if (status == 404 || status == 422 || status == 400) {\n"
              "        s.stopped = true;   // CA6 (F5): no package for this config\n"
              "        return Outcome::NoPackage;\n"
              "    }\n")
SPREADS = "    return followsUpdate || deferredCause == rebootcause::OTA_CHECK || deferredCause == rebootcause::NET_WEDGE;\n"
GATE = "        if (++s.gateRefusals >= kGateRefusalsMax) {\n"
HEADERS = ('"X-Wx-Max-Age",\n                                             "Retry-After" };')
NVS_AT = "        mapAllocFailed = false;       // a new config earns a new allocation attempt\n"
PRINTD_AT = ("        mappolicy::Begin(mapState, waits, mappolicy::SpreadS(DeviceIdentity::LeaderboardId().c_str()),\n"
             "                         (uint32_t)millis());\n")

# name: (the file the plant goes into, its edits, the registered set)
SABOTAGES = {
    "S-CA-MARGIN": (QUERY, [("    const int r = (int)ceil(q->rvMm / 1e6 + kMarginKm);\n",
                             "    const int r = (int)ceil(q->rvMm / 1e6);  // SABOTAGE S-CA-MARGIN\n")],
                    {"CA1", "CA1b", "CA1c", "CA2-rcover"}),
    "S-CA-EPSFLOOR": (QUERY, [("    q->epsMm = eps < kEpsMinMm ? kEpsMinMm : eps;\n",
                               "    q->epsMm = eps;  // SABOTAGE S-CA-EPSFLOOR\n")], {"CA1f", "CA2-bounds"}),
    "S-CA-FLOAT": (QUERY, [("    return (uint32_t)floor(c * 1e6 + 0.5);\n",
                            "    volatile float f = (float)c;  // SABOTAGE S-CA-FLOAT\n"
                            "    return (uint32_t)floor(f * 1e6 + 0.5);\n")], {"CA1", "CA1c"}),
    "S-CA-LEVELM": (QUERY, [("constexpr double kLevelMarginMm = 800000.0;",
                             "constexpr double kLevelMarginMm = 700000.0;  /* SABOTAGE S-CA-LEVELM */")],
                    {"CA1b", "CA1c", "CA1d", "CA2-levels"}),
    "S-CA-CAP": (QUERY, [("constexpr uint32_t kCapBytes = 32768;\n",
                          "constexpr uint32_t kCapBytes = 32000;  // SABOTAGE S-CA-CAP\n")], CA1ALL | {"CA3"}),
    "S-CA-RA": (POLICY, [("            Wait(s, nowMs, ra);   // CA4\n",
                          "            Wait(s, nowMs, 30);   // SABOTAGE S-CA-RA\n")], CA4),
    "S-CA-RAZERO": (POLICY, [("    return ok;   // refused is never a value\n",
                              "    return true;   // SABOTAGE S-CA-RAZERO: a refused value read as 0\n")],
                    {"CA5-absent", "CA5-values", "CA7-429"}),
    "S-CA-404RETRY": (POLICY, [(NO_PACKAGE, "    // SABOTAGE S-CA-404RETRY: no no-package branch\n")], CA6),
    "S-CA-FLAT": (POLICY, [("    if (credential) return kCredentialS;\n",
                            "    (void)credential; (void)s; return 60;   // SABOTAGE S-CA-FLAT\n")], CA7),
    "S-CA-NORESET": (POLICY, [("            s.backoffS = kBackoffFirstS;   // CA7: the reset\n",
                               "            // SABOTAGE S-CA-NORESET: no reset\n")], {"CA7-reset"}),
    "S-CA-NOTIMEOUT": (POLICY, [("        if ((int32_t)(nowMs - s.sentMs) < (int32_t)kReplyTimeoutMs) return false;"
                                 "   // one in flight\n",
                                 "        return false;   // SABOTAGE S-CA-NOTIMEOUT\n")], {"CA7-timeout"}),
    "S-CA-GATE": (POLICY, [(GATE, "        ++s.gateRefusals;\n        if (false) {   // SABOTAGE S-CA-GATE: no limit\n")],
                  {"CA8-stop"}),
    "S-CA-GATE1": (POLICY, [("constexpr uint8_t kGateRefusalsMax = 3;       // CA8\n",
                             "constexpr uint8_t kGateRefusalsMax = 1;   // SABOTAGE S-CA-GATE1\n")],
                   {"CA8-first", "CA8-recover", "CA8-stop"}),
    "S-CA-NOSPREAD": (POLICY, [(SPREADS, "    return false && (" + SPREADS.strip()[len("return "):-1]
                                + ");   // SABOTAGE S-CA-NOSPREAD\n")], {"CA9-ota", "CA9-nightly", "CA9-netwedge"}),
    "S-CA-SPREADALL": (POLICY, [(SPREADS, "    return true || (" + SPREADS.strip()[len("return "):-1]
                                 + ");   // SABOTAGE S-CA-SPREADALL\n")],
                       {"CA9-poweron", "CA9-firstrun", "CA9-config", "CA9-touchwedge"}),
    "S-CA-DMOD": (POLICY, [("    return h % kSpreadS;\n", "    return h % 3600;   // SABOTAGE S-CA-DMOD\n")], {"CA9-dist"}),
    "S-CA-ZOOMFETCH": (QUERY, [("    const int top = n - 1;   // THE LEVELS' STEPS: the ladder's top three\n",
                                "    const int top = viewStep;   // SABOTAGE S-CA-ZOOMFETCH\n")], {"CA10-zoom"}),
    "S-CA-CHANGEWAIT": (POLICY, [("    s.waiting = false;       // CA10: at once, whatever was pending\n",
                                  "    // SABOTAGE S-CA-CHANGEWAIT: the pending wait kept\n")],
                        {"CA10-change", "CA10-location"}),
    "S-CA-KEEPPKG": (POLICY, [("    s.held = false;          // CA10: the package belongs to the old view\n",
                               "    // SABOTAGE S-CA-KEEPPKG: the held package kept\n")],
                     {"CA10-change", "CA10-location", "CA7-reset"}),
    "S-CA-NOSEQ": (POLICY, [("    if (s.sentGen != s.gen) return Outcome::Dropped;             // CA11: it answers a config"
                             " that has since changed\n",
                             "    // SABOTAGE S-CA-NOSEQ: no config sequence check\n")], {"CA11"}),
    "S-CA-NVS": (AMCPP, [(NVS_AT, NVS_AT + "        Preferences p; p.begin(\"map\", false);"
                          " p.putString(\"held\", \"1\");   // SABOTAGE S-CA-NVS\n")], {"CA12"}),
    "S-CA-PRINTD": (AMCPP, [(PRINTD_AT, PRINTD_AT + "        Serial.printf(\"[map] wait %lu s\\n\","
                             " (unsigned long)mappolicy::SpreadS(DeviceIdentity::LeaderboardId().c_str()));"
                             "   // SABOTAGE S-CA-PRINTD\n")], {"CA13"}),
    "S-CA-NORA": (HRM, [(HEADERS, '"X-Wx-Max-Age" };   // SABOTAGE S-CA-NORA')], {"CA14"}),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def make_tree(td, tag, target=None, edits=None):
    """A copy of what the test and the check read, in the repo's own layout; `edits` planted into its `target`."""
    tree = os.path.join(td, tag)
    os.makedirs(os.path.join(tree, "include"), exist_ok=True)
    for f in glob.glob(os.path.join(ROOT, "include", "*.h")):
        shutil.copyfile(f, os.path.join(tree, "include", os.path.basename(f)))
    for rel in COPIED:
        os.makedirs(os.path.dirname(os.path.join(tree, rel)), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, rel), os.path.join(tree, rel))
    if edits is not None:
        p = os.path.join(tree, target)
        src = open(p, "rb").read().decode("utf-8").replace("\r\n", "\n")
        out = src
        for old, new in edits:
            assert out.count(old) == 1, f"{tag}: anchor found {out.count(old)} times: {old.strip()[:60]!r}"
            out = out.replace(old, new)
        assert out != src, f"{tag}: the sabotage did not apply"
        open(p, "w", encoding="utf-8", newline="\n").write(out)
    return tree


def run_suites(tree, td, tag):
    """The C++ suite and the source check against the tree. (red case names, the C++ case count, None) or
    (None, None, why)."""
    exe = os.path.join(td, f"t-{tag}.exe")
    b = subprocess.run([CXX] + FLAGS + ["-I", os.path.join(tree, "include"), TEST, "-o", exe],
                       capture_output=True, text=True)
    if b.returncode != 0:
        return None, None, "did not compile:\n" + b.stderr[-2000:]
    r = subprocess.run([exe, tree], capture_output=True, text=True)
    m = sc.COUNT.search(r.stdout)
    if r.returncode not in (0, 1) or not m:
        return None, None, f"the C++ suite did not run (exit {r.returncode})\n" + r.stdout[-1500:]
    red = {mm.group(1) for mm in re.finditer(r"^\s*FAIL\s+(CA[\w-]+)", r.stdout, re.M)}
    c = subprocess.run([PY, "-I", CHECK, tree], capture_output=True, text=True)
    cm = sc.COUNT.search(c.stdout)
    if c.returncode not in (0, 1) or not cm or int(cm.group(1)) != SOURCE_CHECKS:
        return None, None, f"the source check did not run as {SOURCE_CHECKS} rows (exit {c.returncode})\n" + c.stdout + c.stderr
    red |= {mm.group(1) for mm in re.finditer(r"^\s*FAIL\s+(CA1[234])\b", c.stdout, re.M)}
    return red, int(m.group(1)), None


def real_files():
    return [os.path.join(ROOT, p) for p in (QUERY, POLICY, AMCPP, HRM)]


def main():
    before = [sha(p) for p in real_files()]
    rigs = sc.Rigs()
    ok = True
    with tempfile.TemporaryDirectory() as td:
        red, n, why = run_suites(make_tree(td, "control"), td, "control")
        if why or red or n != CASES:
            print(f"CONTROL (the real files): not clean -- the rig. {why or ''} red {sorted(red or [])}, {n} cases")
            return 2
        print(f"control (the real files): {n} cases and CA12-CA14, all pass")
        for i, (name, (target, edits, want)) in enumerate(SABOTAGES.items()):
            def one():
                red, n, why = run_suites(make_tree(td, f"s{i}", target, edits), td, f"s{i}")
                if why:
                    print(f"{name}: {why}")
                    raise sc.Rig()
                return red, n
            got = rigs.run(name, one)
            if got is None:
                ok = False
                continue
            red, n = got
            good = red == want and n == CASES
            extra, missing = sorted(red - want), sorted(want - red)
            print(f"{name}: {len(red)} red (want {len(want)}); extra {extra or 'none'}; missing {missing or 'none'}"
                  f" -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    same = before == [sha(p) for p in real_files()]
    print(f"real files unchanged: {same}")
    print(f"MAP FETCH SABOTAGES ({len(SABOTAGES)}) AS PREDICTED" if ok and same else "MAP FETCH SABOTAGES: NOT AS PREDICTED")
    return rigs.close(0 if ok and same else 1)


# --control: the unplanted run alone, each suite against this table. CASES and SOURCE_CHECKS are the one place each count
# lives, read by the full run too.
CASES = 42
SOURCE_CHECKS = 3   # CA12, CA13, CA14
CONTROL_TABLE = {"test_map_fetch": ("checks", CASES), "check_map_fetch.py": ("checks", SOURCE_CHECKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        run_suites(make_tree(td, "control"), td, "control")


def anchor_entries():
    """Every plant's anchor in the real files, for run.sh's --anchors pass (no plant runs)."""
    out = []
    for name, (target, edits, _) in SABOTAGES.items():
        text = sc.read(os.path.join(ROOT, target))
        for old, _ in edits:
            out.append((name, target, text, old))
    return out


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
