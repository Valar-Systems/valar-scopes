"""No job runs on ubuntu-latest (docs/ci-runners.md; docs/weatherscope-stage3.md A12.77-A12.79). GitHub moves that label
to a new image under every workflow at once -- ubuntu-26.04, with g++ 15, where main's host tests stop at their first
failing block -- so a result on it describes an image nobody chose. Every runs-on names its image, and this guard refuses
a line that drifts back.

    python3 scripts/check_runners.py              exit 0 pass, 1 a line names ubuntu-latest, 2 rig
    python3 scripts/check_runners.py --selftest   two planted copies must each fail, naming the line; the real tree passes

Reads every .github/workflows/*.yml as text, with nothing installed, like check_workflow_concurrency.py. A line's CODE is
read, not its comment: text from a '#' that starts a comment is ignored, so prose about the label is not the label. A
step's name is code, so it is reworded rather than exempted.
"""
import glob
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNS_ON = re.compile(r"^\s*runs-on:\s*\S")
COMMENT = re.compile(r"(^|\s)#.*$")


def code(line):
    return COMMENT.sub("", line)


def check(wf_dir, quiet=False):
    """(exit code, [the offending lines as 'file:line: text'])."""
    files = sorted(glob.glob(os.path.join(wf_dir, "*.yml")) + glob.glob(os.path.join(wf_dir, "*.yaml")))
    if not files:
        print(f"RUNNERS: no workflow file under {wf_dir} (the rig)")
        return 2, []
    runs, bad = 0, []
    for f in files:
        for i, line in enumerate(open(f, encoding="utf-8").read().splitlines(), 1):
            c = code(line)
            runs += bool(RUNS_ON.match(c))
            if "ubuntu-latest" in c:
                bad.append(f"{os.path.basename(f)}:{i}: {line.strip()}")
    if not quiet:
        for b in bad:
            print(f"  FAIL {b}")
        print(f"RUNNERS: {runs} runs-on line(s) in {len(files)} workflow(s); {len(bad)} naming ubuntu-latest")
    return (1 if bad else 0), bad


def selftest():
    """Two cases, each a copy of the real workflows with one device-ids.yml runs-on line planted: the label alone, and the
    label with a comment after it (the comment rule must not hide a real label). Each must fail, naming exactly the
    planted line; the real tree must pass."""
    wf = os.path.join(ROOT, ".github", "workflows")
    real, _ = check(wf, quiet=True)
    ok = real == 0
    print(f"  {'ok  ' if real == 0 else 'FAIL'} RUNNERS-CTL the real tree: exit {real} (want 0)")
    for name, planted_line in (("the label alone", "runs-on: ubuntu-latest"),
                               ("the label with a comment after it", "runs-on: ubuntu-latest  # pinned")):
        with tempfile.TemporaryDirectory() as td:
            copy = os.path.join(td, "workflows")
            shutil.copytree(wf, copy)
            target = os.path.join(copy, "device-ids.yml")
            text = open(target, encoding="utf-8").read()
            planted = text.replace("runs-on: ubuntu-24.04", planted_line, 1)
            if planted == text:
                print("SELFTEST: device-ids.yml has no 'runs-on: ubuntu-24.04' to plant over (the rig)")
                return 2
            open(target, "w", encoding="utf-8").write(planted)
            line = planted[:planted.index(planted_line)].count("\n") + 1
            rc, bad = check(copy, quiet=True)
        want = [f"device-ids.yml:{line}: {planted_line}"]
        good = rc == 1 and bad == want
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'} RUNNERS-CTL {name}: exit {rc} (want 1), naming {bad} (want {want})")
    return 0 if ok else 1


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    sys.exit(check(os.path.join(ROOT, ".github", "workflows"))[0])
