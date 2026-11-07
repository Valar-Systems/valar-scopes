"""Runs test_wx_swipe.cpp against SABOTAGED copies of src/weatherscope/WxSwipe.h. Each must fail EXACTLY the set of named
rows frozen in docs/weatherscope-stage3.md A12.144 (S-L's corrected by A12.150), and nothing else.

    python -I test/host/sabotage_wx_swipe.py

The real header is never edited: each plant asserts its anchor occurs once and that the copy differs, and the real file's
hash is compared before and after. The unmodified header runs first, as the control: 0 failures.
"""
import hashlib, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sabotage_common as sc   # the --anchors pass and the colour proof (docs/weatherscope-stage3.md A12.50)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(ROOT, "src", "weatherscope", "WxSwipe.h")
TEST = os.path.join(ROOT, "test", "host", "test_wx_swipe.cpp")
REF = os.path.join(ROOT, "test", "fixtures", "wx", "nws", "k35c.ref.txt")
CXX = os.environ.get("CXX") or os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32/bin/g++.exe")
FLAGS = ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-static-libgcc", "-static-libstdc++"]

THEMES = ("dark", "light")
FORECAST = ("forecast-capture", "forecast-SYNTHETIC-RAIN")
SCREENS = FORECAST + ("conditions-day", "alerts-SYNTHETIC", "alerts-capture")


def rows(prefix, kinds):
    return {f"{prefix}-{k}-{t}" for k in kinds for t in THEMES}


PAGES = ("fc", "cond", "alerts", "legend")
SABOTAGES = {
    "S-SW1": ([("return i < kPages - 1 ? Move{(Page)(i + 1), Act::Go} : stay;",
                "return i < kPages - 1 ? Move{(Page)(i + 1), Act::Go} : Move{Page::Map, Act::Go};  // SABOTAGE")],
              {"N-legend-left"}),
    "S-SW2": ([("return i > 0 ? Move{(Page)(i - 1), Act::Go} : stay;",
                "return i > 0 ? Move{(Page)(i - 1), Act::Go} : Move{Page::Legend, Act::Go};  // SABOTAGE")],
              {"N-map-right"}),
    "S-SW3": ([("return p == Page::Map ? Move{p, Act::OpenLoop} : Move{Page::Map, Act::Go};",
                "return p == Page::Map ? Move{p, Act::OpenLoop} : stay;  // SABOTAGE")],
              {f"N-{p}-tap" for p in PAGES}),
    "S-SW4": ([("return p != Page::Map && radarzoom::IdleExpired(nowMs, lastTouchMs, wxscreens::kZoomIdleMs);",
                "(void)p; (void)nowMs; (void)lastTouchMs; return false;  // SABOTAGE")],
              {f"N-{p}-idle" for p in PAGES}),
    "S-SW5": ([("inline bool CycleRuns(Page p) { return p == Page::Map; }",
                "inline bool CycleRuns(Page p) { (void)p; return true; }  // SABOTAGE")],
              {f"N-{p}-cycle" for p in PAGES}),
    "S-SW6": ([("if (e.hasPop && e.pop >= kChanceLine) {", "if (e.hasPop && e.pop > kChanceLine) {  // SABOTAGE")], {"H3"}),
    "S-SW7": ([("if (e.hasPop && e.pop >= kChanceLine) {", "if (e.pop >= kChanceLine) {  // SABOTAGE")], {"H4"}),
    "S-SW8": ([('return p && Names(p->shortForecast, "snow") && !Names(p->shortForecast, "rain") ? "NEXT SNOW" : "NEXT RAIN";',
                'return p && Names(p->shortForecast, "snow") ? "NEXT SNOW" : "NEXT RAIN";  // SABOTAGE')], {"H14"}),
    "S-SW9": ([("inline int BarHeight(int pop) { return (pop * 40 + 50) / 100; }",
                "inline int BarHeight(int pop) { return pop * 40 / 100; }  // SABOTAGE")], {"B3"}),
    "S-SW10": ([("kPlotScale = 47,", "kPlotScale = 52,")], {"Y2", "Y4"} | rows("K", FORECAST)),
    "S-SW11": ([("if (wxscreens::WarningTitle(a.event, &l1, &l2)) continue;",
                 "l1 = l2 = nullptr; (void)l1; (void)l2;  // SABOTAGE")], {"A1", "A6"}),
    "S-SW12": ([("while (j > 0 && out[j - 1]->startUtc > a.startUtc) {",
                 "while (false && j > 0 && out[j - 1]->startUtc > a.startUtc) {  // SABOTAGE")], {"A2"}),
    "S-SW13": ([("return nowUtc - obsUtc > kObsStaleS;", "return nowUtc - obsUtc >= kObsStaleS; /* SABOTAGE */")], {"C21"}),
    "S-SW14": ([("if (o.hasGust && o.gustMph > o.windMph)", "if (o.hasGust && o.gustMph >= o.windMph)  // SABOTAGE\n       ")],
               {"C7"}),
    "S-SW15": ([("inline int ListTop(int shown, int more) { return BlockTop(more ? kAlertStep * shown + 7 : kAlertStep * shown - 9); }",
                 "inline int ListTop(int shown, int more) { (void)shown; (void)more; return 36; }  // SABOTAGE")],
               {"A8", "A10"} | rows("K", ("alerts-SYNTHETIC",))),
    "S-L": ([("constexpr int kAxis = 132, kPlotBottom = 127, kPlotScale = 47, kLowLabel = 123, kHourLabel = 135;",
              "constexpr int kAxis = 137, kPlotBottom = 132, kPlotScale = 52, kLowLabel = 128, kHourLabel = 141;  // SABOTAGE")],
            {"Y1", "Y3", "Y4"} | rows("L", FORECAST) | rows("K", FORECAST)),
    "S-D": ([("if (i == (int)page) wxraster::Fill(c.sink, kDotX[i] - 2, 222, kDotX[i] + 2, 226, c.th.uiFg);",
              "if (i == (int)page) wxraster::Fill(c.sink, kDotX[i] - 1, 223, kDotX[i] + 1, 225, c.th.uiFg);  // SABOTAGE")],
            {"D1", "D2", "D3"} | rows("K", SCREENS)),
    "S-G": ([('c.Text(x + n * 24, top, "\\xF8", 2, c.th.uiFg);', 'c.Text(x + n * 24, top, "\\xF8", 4, c.th.uiFg);  // SABOTAGE')],
            {"C1", "C2", "C3"} | rows("K", ("conditions-day",))),
}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def plant(name, edits, src):
    out = src
    for anchor, repl in edits:
        sc.prove_colour(name, anchor, repl)   # Q50: refuses a colour the panel cannot see
        assert out.count(anchor) == 1, f"{name}: anchor found {out.count(anchor)} times, need exactly 1:\n{anchor}"
        out = out.replace(anchor, repl)
    assert out != src, f"{name}: the copy is identical -- the sabotage did not apply"
    return out


def run(td, name, text):
    inc = os.path.join(td, name)
    os.makedirs(inc)
    open(os.path.join(inc, "WxSwipe.h"), "w", encoding="utf-8", newline="\n").write(text)
    exe = os.path.join(td, name + ".exe")
    b = subprocess.run([CXX, *FLAGS, "-I" + inc, "-I" + os.path.join(ROOT, "src", "weatherscope"),
                        "-I" + os.path.join(ROOT, "include"), TEST, "-o", exe], capture_output=True, text=True)
    if b.returncode != 0:
        print(f"{name}: did not compile (the RIG, not a result)\n{b.stderr}")
        raise sc.Rig(name)
    r = subprocess.run([exe, REF], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        print(f"{name}: the suite did not run (exit {r.returncode}): the RIG\n{r.stdout}{r.stderr}")
        raise sc.Rig(name)
    return r.returncode, {m.group(1) for m in re.finditer(r"^\s*FAIL (\S+)", r.stdout, re.M)}


def main():
    before = sha(HEADER)
    src = open(HEADER, encoding="utf-8").read().replace("\r\n", "\n")
    ok = True
    rigs = sc.Rigs()
    with tempfile.TemporaryDirectory() as td:
        try:
            rc, fails = run(td, "control", src)
        except sc.Rig:   # the control is not a plant: nothing after it can be graded (A12.88)
            return 2
        good = rc == 0 and not fails
        print(f"control (the real header): exit {rc}, failing {sorted(fails)} -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        for name, (edits, expect) in SABOTAGES.items():
            res = rigs.run(name, run, td, name, plant(name, edits, src))
            if res is None:
                ok = False
                continue
            rc, fails = res
            good = rc == 1 and fails == expect
            print(f"{name}: failing {len(fails)} rows, predicted {len(expect)}; off by {sorted(fails ^ expect)} -> "
                  f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
            ok &= good
    after = sha(HEADER)
    print(f"the real src/weatherscope/WxSwipe.h unchanged: {before == after} ({after[:12]})")
    print(f"{len(SABOTAGES)} sabotages: " + ("ALL AS PREDICTED" if ok and before == after else "OFF-PREDICTION: STOP"))
    return rigs.close(0 if ok and before == after else 1)


def anchor_entries():
    """Q51: every plant's anchor in the header it plants, for --anchors (no plant runs)."""
    src = sc.read(HEADER)
    return [(name, os.path.relpath(HEADER, ROOT), src, a) for name, entry in SABOTAGES.items() for a, _ in entry[0]]


# --control (Q57, docs/weatherscope-stage3.md A12.61): this driver's unplanted run alone, against its own count, nothing
# planted. run.sh runs it for every driver.
SWIPE_CHECKS = 133   # the suite's count: the one place, read by the run path and by CONTROL_TABLE (A12.70)
CONTROL_TABLE = {"test_wx_swipe": ("checks", SWIPE_CHECKS)}


def control():
    with tempfile.TemporaryDirectory() as td:
        run(td, "control", open(HEADER, encoding="utf-8").read().replace("\r\n", "\n"))


if __name__ == "__main__":
    if sc.wants_control():
        sys.exit(sc.control_mode(os.path.basename(__file__), control, CONTROL_TABLE))
    if sc.wants_anchors():
        sys.exit(sc.anchors_mode(os.path.basename(__file__), anchor_entries()))
    sys.exit(main())
