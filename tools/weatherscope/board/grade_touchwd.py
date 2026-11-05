"""The reader of the touch supervisor's heartbeat (docs/weatherscope-stage3.md A12.84): grades the `[health] touch-wd`
lines of a passive watch's trace (each line prefixed by its ISO host time, as touch_watch.py writes it).

    python -I tools/weatherscope/board/grade_touchwd.py <trace.txt> --clean <lines>   the supervisor runs and is healthy
    python -I tools/weatherscope/board/grade_touchwd.py <trace.txt> --stw <lines>     S-TW: it never runs (OnPoll removed)
    python -I tools/weatherscope/board/grade_touchwd.py --selftest                    its control

--clean: exactly <lines> heartbeat lines, about 600 s apart (590-610 s by host time); on every one, every counter 0 but
         the ok-probe count, which rises by 55-61 from one line to the next (the first line counts from boot); and no
         other [touch-wd] line in the trace. A rising probe count is the only thing that separates "runs and is healthy"
         from "never runs" -- a healthy chip prints no [touch-wd] line at all.
--stw:   exactly <lines> heartbeat lines, every one all zero, probes=0/0 included.
Prints the heartbeat lines without their times, and the gaps as durations: never a time of day. Exit 0 AS PREDICTED,
1 OFF-PREDICTION, 2 usage.
"""
import calendar
import re
import sys
import time

HB = re.compile(r"^\[health\] touch-wd wedges=(\d+) recovered=(\d+)/(\d+) \(soft=(\d+) hard=(\d+)\) wakes=(\d+) "
                r"probes=(\d+)/(\d+) maxOutage=(\d+)ms rebootRec=(\d+)$")
FIELDS = ("wedges", "recoveries", "attempts", "soft", "hard", "wakes", "probesOk", "probesFailed", "maxOutageMs",
          "rebootRec")
TS = re.compile(r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(\.\d+)?Z (.*)$")


def parse(lines):
    """[(host seconds, line text)] from a trace's lines."""
    out = []
    for raw in lines:
        m = TS.match(raw.rstrip("\r\n"))
        if m:
            out.append((calendar.timegm(time.strptime(m.group(1), "%Y-%m-%dT%H:%M:%S")) + float(m.group(2) or 0),
                        m.group(3)))
    return out


def grade(entries, mode, want):
    beats = [(t, dict(zip(FIELDS, map(int, m.groups()))), text) for t, text in entries for m in [HB.match(text)] if m]
    other = [text for _, text in entries if text.startswith("[touch-wd]")]
    problems = []
    if len(beats) != want:
        problems.append(f"{len(beats)} heartbeat line(s), want {want}")
    for i, (t, f, _) in enumerate(beats):
        zero = [k for k in FIELDS if k != "probesOk" and f[k] != 0]
        if mode == "stw":
            zero = [k for k in FIELDS if f[k] != 0]
        if zero:
            problems.append(f"line {i + 1}: nonzero {zero}")
        if mode == "clean":
            prev = beats[i - 1][1]["probesOk"] if i else 0
            if not 55 <= f["probesOk"] - prev <= 61:
                problems.append(f"line {i + 1}: ok probes rose by {f['probesOk'] - prev}, want 55-61")
        if i and not 590 <= t - beats[i - 1][0] <= 610:
            problems.append(f"line {i + 1}: {t - beats[i - 1][0]:.1f} s after the one before, want 590-610")
    if other:
        problems.append(f"{len(other)} [touch-wd] line(s): {other[:3]}")
    for i, (t, _, text) in enumerate(beats):
        gap = f"+{t - beats[i - 1][0]:.1f} s" if i else "first"
        print(f"  {gap:>9}  {text}")
    return problems


def selftest():
    def beat(ok, failed=0, wakes=0):
        return f"[health] touch-wd wedges=0 recovered=0/0 (soft=0 hard=0) wakes={wakes} probes={ok}/{failed} maxOutage=0ms rebootRec=0"

    def trace(texts, step=600.0):
        return [(1000.0 + step * (i + 1), x) for i, x in enumerate(texts)]

    cases = [
        ("clean: six lines, rising 59 each", "clean", 6, trace([beat(59 * (i + 1)) for i in range(6)]), True),
        ("clean: a flat probe count (never runs)", "clean", 6, trace([beat(0)] * 6), False),
        ("clean: a failed probe", "clean", 6, trace([beat(59 * (i + 1), failed=(i == 3)) for i in range(6)]), False),
        ("clean: a wake", "clean", 6, trace([beat(59 * (i + 1), wakes=(i == 2)) for i in range(6)]), False),
        ("clean: a line missing", "clean", 6, trace([beat(59 * (i + 1)) for i in range(5)]), False),
        ("clean: a [touch-wd] line", "clean", 2, trace([beat(59), "[touch-wd] WEDGE #1: controller dead", beat(118)], 300.0), False),
        ("clean: lines 900 s apart", "clean", 2, trace([beat(59), beat(118)], 900.0), False),
        ("stw: all zero", "stw", 2, trace([beat(0), beat(0)]), True),
        ("stw: probes rising (it runs after all)", "stw", 2, trace([beat(59), beat(118)]), False),
    ]
    ok = True
    for name, mode, want, entries, expect in cases:
        problems = grade(entries, mode, want)
        good = (not problems) == expect
        ok &= good
        print(f"{'ok  ' if good else 'FAIL'} SELFTEST {name}: {'passes' if not problems else 'refused'} "
              f"(want {'passes' if expect else 'refused'})")
    print("SELFTEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv):
    if argv[1:] == ["--selftest"]:
        return selftest()
    if len(argv) != 4 or argv[2] not in ("--clean", "--stw"):
        print(__doc__)
        return 2
    entries = parse(open(argv[1], encoding="utf-8", errors="replace").readlines())
    problems = grade(entries, argv[2][2:], int(argv[3]))
    for p in problems:
        print(f"  OFF: {p}")
    print("AS PREDICTED" if not problems else "OFF-PREDICTION")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
