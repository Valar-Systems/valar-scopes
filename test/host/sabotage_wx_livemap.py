"""Runs test_wx_livemap.cpp against SABOTAGED copies of W7 slice 3b's pure headers (src/weatherscope/WxSetup.h,
WxLiveMap.h, WxSetupScreen.h); each must turn the suite red on EXACTLY the checks predicted in
docs/weatherscope-stage4.md S4.40, and nothing else.

    python -I test/host/sabotage_wx_livemap.py

  M-COVERSALWAYS  every view counts as covered            -> LIVE-COVERS, LIVE-BASEMAP
  M-PKGNOCHECK    the package is built without the check  -> LIVE-BASEMAP
  M-AGEFRAME      a live frame's age read as 0             -> LIVE-AGE
  M-DALLASTOWNS   a live frame given WxTowns.h's towns     -> LIVE-NOTOWNS
  M-LOOPTOWNS     a live loop frame given them             -> LIVE-NOTOWNS
  M-LOOPNOTLOOP   a live loop frame not marked as the loop -> LIVE-LOOP
  M-REFUSEDINK    a refusal's title in the plain ink       -> SETUP-DRAW
  M-WRONGWORDS    every setup screen in NO WI-FI's words   -> SETUP-DRAW
  M-LIVEDRAWS     Live draws a setup screen                -> SETUP-LIVE-BLANK
  M-TITLETOP      the title in the frame's narrow top row  -> SETUP-FITS
  M-NOCLOCKCHECK  a frame shown before the clock is set    -> INPUTS-FILL, NOCLOCK
  M-INPUTSKIND    the refusal read from the streak, not the latch -> INPUTS-FILL
The real headers are never edited; each plant asserts its anchor occurs once and the copy differs.
"""
import hashlib, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WX = os.path.join(ROOT, "src", "weatherscope")
HEADERS = ["WxSetup.h", "WxLiveMap.h", "WxSetupScreen.h"]
TEST = os.path.join(ROOT, "test", "host", "test_wx_livemap.cpp")
FIXTURES = os.path.join(ROOT, "test", "fixtures", "wx")
INCS = ["-I" + os.path.join(ROOT, "include"), "-I" + WX]
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "M-COVERSALWAYS": ("WxLiveMap.h", [("    return sqrt(e * e + s * s) + viewKm <= pk.radiusMm / 1e6;",
                                        "    return sqrt(e * e + s * s) + viewKm <= pk.radiusMm / 1e6 || true;  // SABOTAGE")],
                       {"LIVE-COVERS", "LIVE-BASEMAP"}),
    "M-PKGNOCHECK": ("WxLiveMap.h", [("    if (!Covers(pk, c.lat, c.lon, radiusMi * kKmPerMi)) return false;",
                                      "    (void)c;  // SABOTAGE: no coverage check")],
                     {"LIVE-BASEMAP"}),
    "M-AGEFRAME": ("WxLiveMap.h", [("    in->ageS = nowUtc - (int64_t)h.frameEpoch;", "    in->ageS = 0 * nowUtc;  // SABOTAGE")],
                   {"LIVE-AGE"}),
    "M-DALLASTOWNS": ("WxLiveMap.h", [("    in->towns = nullptr;   // a live frame has no towns: WxTowns.h is Dallas's",
                                       "    in->towns = wxtowns::kTowns;  // SABOTAGE")],
                      {"LIVE-NOTOWNS"}),
    "M-LOOPTOWNS": ("WxLiveMap.h", [("    in->towns = nullptr;   // the loop's frames have no towns either",
                                     "    in->towns = wxtowns::kTowns;  // SABOTAGE")],
                    {"LIVE-NOTOWNS"}),
    "M-LOOPNOTLOOP": ("WxLiveMap.h", [("    in->loop = true;", "    in->loop = false;  // SABOTAGE")], {"LIVE-LOOP"}),
    "M-REFUSEDINK": ("WxSetupScreen.h", [("    c.Centred(w.title, kTitleY, 1, s == wxsetup::State::Refused ? c.th.uiWarn : c.th.uiFg);",
                                          "    c.Centred(w.title, kTitleY, 1, c.th.uiFg);  // SABOTAGE")],
                     {"SETUP-DRAW"}),
    "M-WRONGWORDS": ("WxSetupScreen.h", [("    const wxsetup::Words w = wxsetup::WordsOf(s, r);",
                                          "    const wxsetup::Words w = wxsetup::WordsOf(wxsetup::State::NoWifi, r);  // SABOTAGE")],
                     {"SETUP-DRAW"}),
    "M-LIVEDRAWS": ("WxSetupScreen.h", [("    if (s == wxsetup::State::Live) return false;", "    // SABOTAGE: Live draws too")],
                    {"SETUP-LIVE-BLANK"}),
    "M-TITLETOP": ("WxSetupScreen.h", [("constexpr int kTitleY = 104;", "constexpr int kTitleY = 9;  /* SABOTAGE */")],
                   {"SETUP-FITS"}),
    "M-NOCLOCKCHECK": ("WxSetup.h", [("    in.frame = haveCrop && clockSet;",
                                      "    in.frame = haveCrop || (clockSet && false);  // SABOTAGE")],
                       {"INPUTS-FILL", "NOCLOCK"}),
    "M-INPUTSKIND": ("WxSetup.h", [("    in.refused = Shown(latch);", "    in.refused = latch.kind;  // SABOTAGE")],
                     {"INPUTS-FILL"}),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
        sc.prove_colour(name, anchor, repl)
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    return out


def real(h):
    return open(os.path.join(WX, h), encoding="utf-8").read().replace("\r\n", "\n")


def build_run(td, name, planted):
    """The three headers into their own folder (one planted), the suite built against it first, then run."""
    inc = os.path.join(td, name)
    os.makedirs(inc)
    for h in HEADERS:
        text = planted[1] if planted and planted[0] == h else real(h)
        open(os.path.join(inc, h), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, *INCS, TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        return b, None
    return b, subprocess.run([exe, FIXTURES], capture_output=True, text=True)


def main():
    before = {h: sha(os.path.join(WX, h)) for h in HEADERS}
    ok = True
    rigs = sc.Rigs()
    with tempfile.TemporaryDirectory() as td:
        for name, (header, edits, expect) in SABOTAGES.items():
            if rigs.forced(name):
                ok = False
                continue
            b, r = build_run(td, name, (header, plant(name, edits, real(header))))
            if r is None:
                print(f"{name}: did not compile (rig)\n{b.stderr}")
                rigs.add(name)
                ok = False
                continue
            fails = {l.split()[1] for l in r.stdout.splitlines() if l.strip().startswith("FAIL") and len(l.split()) > 1}
            good = r.returncode == 1 and fails == expect
            print(f"{name}: suite exit {r.returncode}; red {sorted(fails)} (want {sorted(expect)}) -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = {h: sha(os.path.join(WX, h)) for h in HEADERS}
    same = before == after
    print(f"real headers unchanged: {same} ({', '.join(h + ' ' + after[h][:12] for h in HEADERS)})")
    if ok and same:
        print("W7 LIVE MAP PLANTS AS PREDICTED")
    return rigs.close(0 if ok and same else 1)


def anchor_entries():
    """Every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    return [(name, os.path.relpath(os.path.join(WX, h), ROOT), sc.read(os.path.join(WX, h)), a)
            for name, (h, edits, _) in SABOTAGES.items() for a, _ in edits]


# --control: this driver's unplanted run alone, the suite against its own count. run.sh runs it for every driver.
LIVEMAP_OKS = 10   # the suite's count: the one place, read by the run path and by CONTROL_TABLE
CONTROL_TABLE = {"test_wx_livemap": ("ok", LIVEMAP_OKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        build_run(td, "control", None)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
