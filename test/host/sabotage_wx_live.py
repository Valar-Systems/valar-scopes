"""Runs test_wx_live.cpp against SABOTAGED copies of W7's first-slice headers (src/weatherscope/WxRing.h, WxSetup.h,
WxLiveUrl.h); each must turn the suite red on EXACTLY the checks predicted in docs/weatherscope-stage4.md S4.33, and
nothing else.

    python -I test/host/sabotage_wx_live.py

  L-KEEPALL     the ring keeps every new frame       -> RING-EVERY-OTHER, RING-REPEAT, RING-WRAP, RING-LOOP-MIN, RING-OVERSIZE
  L-REPEATNEW   a frame no newer counts as new       -> RING-REPEAT
  L-PARTIAL     a failed allocation frees nothing    -> RING-ALLOC-NONE
  L-LIVENOFRAME live without a frame                 -> STATE-PRECEDENCE, STATE-LIVE-NEEDS-FRAME, STATE-REFUSED
  L-401NOWIFI   a 401 drawn with no Wi-Fi's words    -> STATE-WORDS
  L-THREEDP     a 3-dp latitude, past WireLocation.h -> URL-QUERY
  L-BAREPATH    the path without its last slash      -> URL-PATH
The real headers are never edited; each plant asserts its anchor occurs once and the copy differs.
"""
import hashlib, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WX = os.path.join(ROOT, "src", "weatherscope")
HEADERS = ["WxRing.h", "WxSetup.h", "WxLiveUrl.h"]
TEST = os.path.join(ROOT, "test", "host", "test_wx_live.cpp")
INCS = ["-I" + os.path.join(ROOT, "include"), "-I" + WX, "-I" + os.path.join(ROOT, "test", "host", "arduino_shim")]
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

SABOTAGES = {
    "L-KEEPALL": ("WxRing.h", [("        const bool keep = (newSeen % 2) == 0;",
                                "        const bool keep = true;  // SABOTAGE")],
                  {"RING-EVERY-OTHER", "RING-REPEAT", "RING-WRAP", "RING-LOOP-MIN", "RING-OVERSIZE"}),
    "L-REPEATNEW": ("WxRing.h", [("        if (newSeen > 0 && frameEpoch <= newestSeen) return false;",
                                  "        if (false) return false;  // SABOTAGE")],
                    {"RING-REPEAT"}),
    "L-PARTIAL": ("WxRing.h", [("                    release(slot[j]);", "                    (void)release;  // SABOTAGE")],
                  {"RING-ALLOC-NONE"}),
    "L-LIVENOFRAME": ("WxSetup.h", [("    if (!in.frame) return State::Waiting;", "    // SABOTAGE: no frame check")],
                      {"STATE-PRECEDENCE", "STATE-LIVE-NEEDS-FRAME", "STATE-REFUSED"}),
    "L-401NOWIFI": ("WxSetup.h", [('        case Refusal::KeyRejected: return {"KEY NOT ACCEPTED", "ENROL THIS BOARD AGAIN"};',
                                   '        case Refusal::KeyRejected: return {"NO WI-FI", "SET UP WI-FI FROM YOUR PHONE"};  // SABOTAGE')],
                    {"STATE-WORDS"}),
    "L-THREEDP": ("WxLiveUrl.h", [("    q += wireloc::Center(lat).c_str();",
                                   '    { char b3[320]; snprintf(b3, sizeof b3, "%.3f", lat); q += b3; }  // SABOTAGE')],
                  {"URL-QUERY"}),
    "L-BAREPATH": ("WxLiveUrl.h", [('constexpr const char* kPath = "/api/v1/blipscope/wx/";',
                                    'constexpr const char* kPath = "/api/v1/blipscope/wx";  // SABOTAGE')],
                   {"URL-PATH"}),
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
    return b, subprocess.run([exe], capture_output=True, text=True)


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
        print("W7 LIVE PLANTS AS PREDICTED")
    return rigs.close(0 if ok and same else 1)


def anchor_entries():
    """Every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    return [(name, os.path.relpath(os.path.join(WX, h), ROOT), sc.read(os.path.join(WX, h)), a)
            for name, (h, edits, _) in SABOTAGES.items() for a, _ in edits]


# --control: this driver's unplanted run alone, the suite against its own count. run.sh runs it for every driver.
LIVE_OKS = 14   # the suite's count: the one place, read by the run path and by CONTROL_TABLE
CONTROL_TABLE = {"test_wx_live": ("ok", LIVE_OKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        build_run(td, "control", None)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
