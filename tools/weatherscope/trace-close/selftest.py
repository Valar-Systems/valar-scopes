"""The trace-close test's classifier, proven able to reach every verdict in A7.2 before it grades a real run.

Builds one synthetic run per outcome (watch.csv + events.csv), runs `run.ps1 -AnalyseOnly` on each, and compares the
VERDICT line's outcome with the one the case was built for. Run: python -I tools/weatherscope/trace-close/selftest.py"""
import os
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timedelta, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
T0 = datetime(2026, 10, 22, 4, 0, 0, tzinfo=timezone.utc)
iso = lambda t: t.strftime("%Y-%m-%dT%H:%M:%S.") + f"{t.microsecond // 1000:03d}Z"


def build(dirpath, drops, extra_events, d_connects):
    """drops: list of (start_s, n_polls); polls every 2 s over 26 minutes. extra_events: (t_s, event, detail)."""
    down = set()
    for start, n in drops:
        down.update(range(start, start + 2 * n, 2))
    with open(os.path.join(dirpath, "watch.csv"), "w") as f:
        f.write("utc,ok\n")
        for s in range(0, 26 * 60, 2):
            f.write(f"{iso(T0 + timedelta(seconds=s))},{0 if s in down else 1}\n")
    ev = [(0, "address", "x"), (5, "precondition", "GET / #1 = 200"), (35, "precondition", "GET / #2 = 200")]
    for w in range(3):
        ev += [(40 + 180 * w, "C-start", f"window {w + 1}"), (40 + 180 * (w + 1), "C-end", f"window {w + 1}")]
    for t in range(3):
        b = 580 + 190 * t
        ev += [(b, "T-trace-open", f"trial {t + 1} port=COM6"), (b + 62, "T-trace-closed", f"trial {t + 1}"),
               (b + 182, "T-end", f"trial {t + 1}")]
    ev += [(1160, "D", f"connects={d_connects} rc={0 if d_connects else 2}"), (1280, "D-end", "")]
    ev += extra_events
    ev.sort(key=lambda e: e[0])
    with open(os.path.join(dirpath, "events.csv"), "w") as f:
        f.write("utc,event,detail\n")
        for s, e, d in ev:
            f.write(f"{iso(T0 + timedelta(seconds=s))},{e},{d}\n")


CASES = [
    ("(ii)", [], [], False),
    # trial 2: open 770, closed 832, end 952 -- the drop starts 8 s after the close and lasts past the window end
    ("(i)", [(840, 60)], [(953, "discriminator", "T2 connects=True rc=0"), (956, "recovery", "T2 read-mac rc=0"),
                          (965, "recovered", "T2 up=True")], False),
    ("(iii)", [(300, 10)], [], False),
    ("(iv)", [(650, 30)], [(763, "discriminator", "T1 connects=False rc=2"), (766, "recovery", "T1 read-mac rc=0"),
                           (780, "recovered", "T1 up=True")], False),
    ("(v)", [], [], True),                                        # D connects: the discriminator is not what it claims
    ("(v)", [(600, 10)], [], False),                              # a drop while a trace is open
]

ok = True
for want, drops, extra, d_conn in CASES:
    tmp = tempfile.mkdtemp(prefix="tc-selftest-")
    try:
        build(tmp, drops, extra, d_conn)
        r = subprocess.run(["pwsh", "-NoProfile", "-File", os.path.join(HERE, "run.ps1"), "-OutDir", tmp, "-AnalyseOnly"],
                           capture_output=True, text=True)
        line = next((l for l in r.stdout.splitlines() if l.startswith("VERDICT")), f"(no verdict) {r.stderr.strip()[:200]}")
        got = line.split(": ", 1)[1][:5].strip() if ": " in line else "?"
        good = got.startswith(want)
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'} built for {want:6s} -> {line}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
print("selftest " + ("PASSED" if ok else "FAILED"))
sys.exit(0 if ok else 1)
