"""Runs test_wx_answer.cpp against SABOTAGED copies of W7 slice 3a's headers (src/weatherscope/WxSetup.h, WxAnswer.h,
WxRing.h); each must turn the suite red on EXACTLY the checks predicted in docs/weatherscope-stage4.md S4.37, and
nothing else.

    python -I test/host/sabotage_wx_answer.py

  A-NOTIME        the latch ignores the 15 min             -> LATCH-STREAK-NEEDS-TIME, LATCH-ONE-BIT
  A-NOSTREAK      the latch ignores the streak of 5        -> LATCH-TIME-NEEDS-STREAK
  A-WRAP          the 15 min measured across millis()'s wrap as a signed difference -> LATCH-WRAP
  A-NOCLEAR       a 200 or 304 does not clear              -> LATCH-CLEAR, ANSWER-200, ANSWER-304, ANSWER-KEPT
  A-EVIDENCE      any non-refusal clears                   -> LATCH-NOT-EVIDENCE
  A-TWOBITS       NeedsReverify is a second evaluation     -> LATCH-STREAK-NEEDS-TIME, LATCH-ONE-BIT
  A-SCREENIGNORES the refused screen ignores the latch     -> LATCH-ONE-BIT
  A-KINDFIRST     the screen keeps the first refusal's kind -> LATCH-KIND
  A-FACTORYONLY   the pasted key is ignored                -> KEY-PRECEDENCE, KEY-PASTED-ROW
  A-STRAYCOUNTS   an answer to another request is taken    -> ANSWER-STRAY
  A-NOLATCHFEED   answers never reach the latch            -> LATCH-BACKOFF, ANSWER-401
  A-NORING        a replacing 200 is not offered to the ring -> ANSWER-200, ANSWER-304, ANSWER-QUERY-CHANGE
  A-RINGKEEPS     a new query keeps the old place's frames -> ANSWER-QUERY-CHANGE
  A-QUERYKEEPS    a new query keeps the old crop and ETag  -> ANSWER-ETAG, ANSWER-QUERY-CHANGE
  A-ENABLEDWIFI   requests without Wi-Fi                   -> ANSWER-DISABLED
  A-QUERYTRUNC    the policy's latitude truncated, not the wire's 2 dp -> QUERY-MATCH
  A-CLEARPARITY   a cleared ring keeps its every-other count -> ANSWER-QUERY-CHANGE
The real headers are never edited; each plant asserts its anchor occurs once and the copy differs.
"""
import hashlib, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors and --control passes

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WX = os.path.join(ROOT, "src", "weatherscope")
HEADERS = ["WxSetup.h", "WxAnswer.h", "WxRing.h"]
TEST = os.path.join(ROOT, "test", "host", "test_wx_answer.cpp")
INCS = ["-I" + os.path.join(ROOT, "include"), "-I" + WX, "-I" + os.path.join(ROOT, "test", "host", "arduino_shim")]
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

LATCH = "        if (!l.latched && l.streak >= kLatchStreak && (uint32_t)(nowMs - l.firstMs) >= kLatchMs) l.latched = true;"

SABOTAGES = {
    "A-NOTIME": ("WxSetup.h", [(LATCH, "        if (!l.latched && l.streak >= kLatchStreak) l.latched = true;  // SABOTAGE")],
                 {"LATCH-STREAK-NEEDS-TIME", "LATCH-ONE-BIT"}),
    "A-NOSTREAK": ("WxSetup.h", [(LATCH, "        if (!l.latched && (uint32_t)(nowMs - l.firstMs) >= kLatchMs) "
                                         "l.latched = true;  // SABOTAGE")],
                   {"LATCH-TIME-NEEDS-STREAK"}),
    "A-WRAP": ("WxSetup.h", [(LATCH, "        if (!l.latched && l.streak >= kLatchStreak && (int64_t)nowMs - "
                                     "(int64_t)l.firstMs >= (int64_t)kLatchMs) l.latched = true;  // SABOTAGE")],
               {"LATCH-WRAP"}),
    "A-NOCLEAR": ("WxSetup.h", [("        l = AuthLatch();", "        (void)0;  // SABOTAGE")],
                  {"LATCH-CLEAR", "ANSWER-200", "ANSWER-304", "ANSWER-KEPT"}),
    "A-EVIDENCE": ("WxSetup.h", [("    } else if (status == 200 || status == 304) {", "    } else {  // SABOTAGE")],
                   {"LATCH-NOT-EVIDENCE"}),
    "A-TWOBITS": ("WxSetup.h", [("inline bool NeedsReverify(const AuthLatch& l) { return Shown(l) != Refusal::None; }",
                                 "inline bool NeedsReverify(const AuthLatch& l) { return l.streak >= kLatchStreak; }"
                                 "  // SABOTAGE")],
                  {"LATCH-STREAK-NEEDS-TIME", "LATCH-ONE-BIT"}),
    "A-SCREENIGNORES": ("WxSetup.h", [("    if (in.refused != Refusal::None) return State::Refused;",
                                       "    // SABOTAGE: the latch never reaches the screen")],
                        {"LATCH-ONE-BIT"}),
    "A-KINDFIRST": ("WxSetup.h", [("        l.kind = r;", "        if (l.streak == 1) l.kind = r;  // SABOTAGE")],
                    {"LATCH-KIND"}),
    "A-FACTORYONLY": ("WxSetup.h", [("    if (pasted != nullptr && pasted[0] != 0) return pasted;",
                                     "    (void)pasted;  // SABOTAGE")],
                      {"KEY-PRECEDENCE", "KEY-PASTED-ROW"}),
    "A-STRAYCOUNTS": ("WxAnswer.h", [("    if (!L.policy.inFlight || seq != L.policy.seq) return res;",
                                      "    // SABOTAGE: no stray guard")],
                      {"ANSWER-STRAY"}),
    "A-NOLATCHFEED": ("WxAnswer.h", [("    wxsetup::OnAnswer(L.latch, r.status, r.error, nowMs);",
                                      "    // SABOTAGE: the latch is never fed")],
                      {"LATCH-BACKOFF", "ANSWER-401"}),
    "A-NORING": ("WxAnswer.h", [("            res.kept = ring.Offer(r.body, r.bytes, r.frameEpoch);",
                                 "            res.kept = false; (void)ring;  // SABOTAGE")],
                 {"ANSWER-200", "ANSWER-304", "ANSWER-QUERY-CHANGE"}),
    "A-RINGKEEPS": ("WxAnswer.h", [("    ring.Clear();", "    (void)ring;  // SABOTAGE")],
                    {"ANSWER-QUERY-CHANGE"}),
    "A-QUERYKEEPS": ("WxAnswer.h", [("    wxpolicy::OnQuery(L.policy, q);", "    L.policy.query = q;  // SABOTAGE")],
                     {"ANSWER-ETAG", "ANSWER-QUERY-CHANGE"}),
    "A-ENABLEDWIFI": ("WxAnswer.h", [("inline bool Enabled(const wxsetup::Inputs& in) { return in.wifi && in.location && in.key; }",
                                      "inline bool Enabled(const wxsetup::Inputs& in) { return in.location && in.key; }"
                                      "  // SABOTAGE")],
                      {"ANSWER-DISABLED"}),
    "A-QUERYTRUNC": ("WxAnswer.h", [("    q.lat100 = (int32_t)lround(wireloc::Round2(lat) * 100.0);",
                                     "    q.lat100 = (int32_t)(lat * 100.0);  // SABOTAGE")],
                     {"QUERY-MATCH"}),
    "A-CLEARPARITY": ("WxRing.h", [("        newSeen = 0;", "        // SABOTAGE: newSeen kept")],
                      {"ANSWER-QUERY-CHANGE"}),
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
        print("W7 ANSWER PLANTS AS PREDICTED")
    return rigs.close(0 if ok and same else 1)


def anchor_entries():
    """Every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    return [(name, os.path.relpath(os.path.join(WX, h), ROOT), sc.read(os.path.join(WX, h)), a)
            for name, (h, edits, _) in SABOTAGES.items() for a, _ in edits]


# --control: this driver's unplanted run alone, the suite against its own count. run.sh runs it for every driver.
ANSWER_OKS = 19   # the suite's count: the one place, read by the run path and by CONTROL_TABLE
CONTROL_TABLE = {"test_wx_answer": ("ok", ANSWER_OKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        build_run(td, "control", None)


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
