"""Is a SHIPPING leg red NOW? The firmware workflow's ship-alert decision, read from the run's own jobs
(docs/ship-alert-latest.md).

    python3 scripts/ship-alert.py --matrix '<needs.skus.outputs.matrix>' --jobs <jobs file>
    python3 scripts/ship-alert.py --selftest

A shipping leg is a build-matrix row with a slug. Its job is named exactly "build (<env>, <slug>)", which is GitHub's
default name for a matrix job: the job id, then the row's values in order. --jobs is the jobs API's list for this run,
as JSON lines (gh api --jq '.jobs[]') or one {"jobs": [...]} object. Per leg, the execution with the highest
run_attempt decides, so the answer is the same whether the API was asked for filter=latest or filter=all.

WHY NOT MARKERS. The marker artifacts this replaced belonged to the RUN, not the attempt, so a transient failure's
marker outlived the re-run that turned its leg green, and kept main red (2026-11-12, run 43266574187). Naming the
marker after the attempt goes false-green instead: re-run ship-alert alone over a leg that is still red, and the new
attempt has no marker. Only the leg's own latest conclusion says what is true now.

Exit 0: every shipping leg's latest conclusion is success.
Exit 1: one or more shipping legs failed (one line each).
Exit 2: INSTRUMENT. A shipping row with no job, two executions of one leg in one attempt, a conclusion that is
        neither success nor a failure, no shipping row at all, or unreadable input. Never green: a decision this job
        cannot see is reported as one it cannot make.
"""
import json
import sys

RED = ("failure", "timed_out")


def decide(matrix, jobs):
    """(exit code, lines). matrix: {"include": [{"env", "slug"?}, ...]}; jobs: [{"name", "run_attempt", "conclusion"}]."""
    rows = [r for r in matrix.get("include", []) if r.get("slug")]
    if not rows:
        return 2, ["INSTRUMENT: the matrix has no shipping row, so there is nothing this job can vouch for"]
    lines, red, blind = [], [], []
    for r in rows:
        name = "build (%s, %s)" % (r["env"], r["slug"])
        mine = [j for j in jobs if j.get("name") == name]          # exact: a prefix would catch build-only rows
        if not mine:
            blind.append("%s: no job found" % name)
            continue
        top = max(int(j.get("run_attempt") or 1) for j in mine)
        latest = [j for j in mine if int(j.get("run_attempt") or 1) == top]
        if len(latest) != 1:
            blind.append("%s: %d executions in attempt %d" % (name, len(latest), top))
            continue
        c = latest[0].get("conclusion")
        lines.append("%s: %s (attempt %d)" % (name, c, top))
        if c == "success":
            continue
        if c in RED:
            red.append(name)
        else:
            blind.append("%s: conclusion %r is neither success nor a failure" % (name, c))
    if blind:
        return 2, lines + ["INSTRUMENT: " + b for b in blind]
    if red:
        return 1, lines + ["RED: %d shipping leg(s): %s" % (len(red), ", ".join(red))]
    return 0, lines + ["green: %d shipping leg(s) read, all success" % len(rows)]


def load_jobs(path):
    text = open(path, encoding="utf-8").read().strip()
    try:
        whole = json.loads(text)
        if isinstance(whole, list):
            return whole
        return whole["jobs"] if "jobs" in whole else [whole]    # one {"jobs": [...]}, or a single job line
    except ValueError:
        return [json.loads(line) for line in text.splitlines() if line.strip()]


def selftest():
    M = {"include": [{"env": "blipscope-s3-128", "slug": "s3-128"}, {"env": "blipscope-s3-128-wxlook"},
                     {"env": "blipscope-pro-s3-21"}]}
    SHIP, WX, PRO = "build (blipscope-s3-128, s3-128)", "build (blipscope-s3-128-wxlook)", "build (blipscope-pro-s3-21)"
    j = lambda name, attempt, c: {"name": name, "run_attempt": attempt, "conclusion": c}
    cases = [
        ("SA1 transient, re-run", M, [j(SHIP, 1, "failure"), j(SHIP, 2, "success"), j("ship-alert", 2, None)], 0),
        ("SA2 persists", M, [j(SHIP, 1, "failure"), j(SHIP, 2, "failure")], 1),
        ("SA3 ship-alert alone re-run over a red leg", M, [j(SHIP, 1, "failure"), j("ship-alert", 2, None)], 1),
        ("SA4 a build-only leg red", M, [j(SHIP, 1, "success"), j(PRO, 1, "failure")], 0),
        ("SA5 a shipping row with no job", M, [j(PRO, 1, "success")], 2),
        ("SA6 a slug-less row sharing the shipping env's prefix, red", M, [j(SHIP, 1, "success"), j(WX, 1, "failure")], 0),
        ("SA7 two executions of one leg in one attempt", M, [j(SHIP, 1, "failure"), j(SHIP, 1, "success")], 2),
        ("SA8 no shipping row in the matrix", {"include": [{"env": "blipscope-pro-s3-21"}]}, [j(PRO, 1, "success")], 2),
        ("SA9 a skipped shipping leg", M, [j(SHIP, 1, "skipped")], 2),
    ]
    off = 0
    for label, m, jobs, want in cases:
        got, _ = decide(m, jobs)
        ok = got == want
        off += not ok
        print("%s: %s -- exit %d (want %d)" % (label, "as predicted" if ok else "OFF-PREDICTION", got, want))
    print("SELFTEST PASSED" if not off else "SELFTEST: %d case(s) off" % off)
    return 1 if off else 0


def main(argv):
    if argv[1:] == ["--selftest"]:
        return selftest()
    try:
        matrix = json.loads(argv[argv.index("--matrix") + 1])
        jobs = load_jobs(argv[argv.index("--jobs") + 1])
    except (ValueError, IndexError, OSError, KeyError) as e:
        print("INSTRUMENT: unreadable input: %s" % e)
        return 2
    code, lines = decide(matrix, jobs)
    for line in lines:
        print(line)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
