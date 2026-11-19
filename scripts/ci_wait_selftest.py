#!/usr/bin/env python3
"""The merge driver's CI read, offline: scripts/ci-wait.sh against a STUB `gh`, graded by merge-by-rule.sh's OWN lines.

Registered as mbr-ci-scope-registration.md (2026-11-19T15:22:48Z), before this file existed: merge-by-rule's "H's CI" is
the base's own push CI, so a release, tag-push or schedule run on H can neither pass nor block a merge.

  python -X utf8 -I scripts/ci_wait_selftest.py <ci-wait.sh before the fix>    the old copy is P-RED's subject

The stub answers ONLY the exact queries ci-wait.sh sends (an unknown one exits 1, so a row reads RIG, never a pass), and
emulates the API's own filters (head_sha, event, branch) and ci-wait's jq select. The verdict is merge-by-rule.sh's own
`:54`/`:55` text, taken from the file (each asserted present once) and run in bash on ci-wait's output, so the predicate
cannot drift from the real one. Rows compare whole results; tally and rows as sorted lists (Git Bash's `sort` follows
the locale). Exit 0 when every row reads as predicted; 1 when any is OFF; 2 the rig.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHA = "ab" * 20                                    # synthetic, built at run time
STUB = r'''import json, os, re, sys, urllib.parse
sys.stdout.reconfigure(newline="\n")   # gh is a Go binary and writes LF; Windows Python's text stdout would write CRLF
R = "Valar-Systems/valar-scopes"
a = sys.argv[1:]
def die(m):
    sys.stderr.write("stub: " + m + "\n"); sys.exit(1)
if len(a) != 5 or a[:2] != ["api", "--paginate"] or a[3] != "-q":
    die("unknown query")
url, expr = a[2], a[4]
fx = json.load(open(os.environ["CIW_FIXTURE"], encoding="utf-8"))
path, _, qs = url.partition("?"); q = dict(urllib.parse.parse_qsl(qs))
E_ALL = '.check_runs[] | "\\(.name)\\t\\(.status)\\t\\(.conclusion)"'
E_SUITE = '.check_runs[] | "\\(.check_suite.id)\\t\\(.name)\\t\\(.status)\\t\\(.conclusion)"'
if path == "repos/%s/commits/%s/check-runs" % (R, fx["sha"]):
    if expr == E_ALL:
        rows = ["%s\t%s\t%s" % (c["name"], c["status"], c["conclusion"]) for c in fx["check_runs"]]
    elif expr == E_SUITE:
        rows = ["%s\t%s\t%s\t%s" % (c["suite"], c["name"], c["status"], c["conclusion"]) for c in fx["check_runs"]]
    else:
        die("unknown check-runs expression")
elif path == "repos/%s/actions/runs" % R:
    m = re.fullmatch(r'\.workflow_runs\[\] \| select\((.+)\) \| \.check_suite_id', expr)
    if not m:
        die("unknown runs expression")
    conds = []
    for t in m.group(1).split(" and "):
        mm = re.fullmatch(r'\.(event|head_sha|head_branch) == "([^"]*)"', t.strip())
        if not mm:
            die("unknown runs condition")
        conds.append((mm.group(1), mm.group(2)))
    runs = [r for r in fx["runs"] if r["head_sha"] == q.get("head_sha")]           # the API's own filters
    if "event" in q:
        runs = [r for r in runs if r["event"] == q["event"]]
    if "branch" in q:
        runs = [r for r in runs if r["head_branch"] == q["branch"]]
    rows = [str(r["suite"]) for r in runs if all(r[k] == v for k, v in conds)]    # ci-wait's own select
else:
    die("unknown path")
for r in rows:
    print(r)
'''
LF_LINE = 'sys.stdout.reconfigure(newline="\\n")   # gh is a Go binary and writes LF; Windows Python\'s text stdout would write CRLF\n'
FIX_URL = '&branch=$branch'
FIX_SEL = ' and .head_branch == \\"$branch\\"'


def fixture(kind):
    """The shape read live on main's head at 15:2xZ: main's push runs, a TAG push run, release runs."""
    runs, crs = [], []

    def run(suite, event, branch, name, rows):
        runs.append({"suite": suite, "event": event, "head_branch": branch, "head_sha": SHA, "name": name})
        crs.extend({"suite": suite, "name": n, "status": "completed", "conclusion": c} for n, c in rows)

    fw = [("build (blipscope-s3-128, s3-128)", "success"), ("host-tests", "success"), ("host-tests (ubuntu-24.04)", "success"),
          ("host-tests (ubuntu-26.04)", "success"), ("promote", "skipped"), ("version", "skipped")]
    if kind == "pushfail":
        fw = [(n, "failure" if n == "host-tests" else c) for n, c in fw]
    if kind != "none":
        run(101, "push", "main", "firmware", fw)
        run(102, "push", "main", "bench-tools", [("app-only-write", "success")])
        run(103, "push", "main", "device-ids", [("no-real-device-ids", "success")])
        run(104, "push", "main", "no-traffic-captures", [("no-traffic-captures", "success")])
    run(201, "push", "rehearsal-perms-a", "print-cards", [("print-cards", "failure" if kind == "tagfail" else "success")])
    run(301, "release", "rehearsal-perms-a", "firmware",
        [("host-tests", "failure"), ("host-tests (ubuntu-26.04)", "cancelled"), ("promote", "cancelled"), ("version", "skipped")])
    run(302, "release", "rehearsal-perms-a", "web-renderer", [("render", "success")])
    if kind == "sched":
        run(401, "schedule", "main", "photo-drift", [("photo-drift", "failure")])
    return {"sha": SHA, "runs": runs, "check_runs": crs}


def find_bash():
    if sys.platform == "win32":   # Git's bash: the one System32 holds is WSL's, a different machine
        for p in (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files\Git\usr\bin\bash.exe"):
            if os.path.exists(p):
                return p
    return shutil.which("bash") or "bash"


def parse(out):
    lines = out.splitlines()
    first = re.sub(r"^\d\d:\d\d:\d\dZ ", "", lines[0]) if lines else ""
    tally = sorted(l.strip() for l in lines[1:] if re.fullmatch(r"\s+\d+ \S+", l))
    rows = sorted(l for l in lines[1:] if "\t" in l)
    return first, tally, rows


def main(argv):
    if len(argv) != 1:
        print(__doc__)
        return 2
    bash = find_bash()
    mbr = open(os.path.join(ROOT, "scripts", "merge-by-rule.sh"), encoding="utf-8").read().splitlines()
    l54 = [l for l in mbr if l.startswith('echo "$OUT" | head -1 | grep -q ')]
    l55 = [l for l in mbr if l.startswith('echo "$OUT" | grep -q -E ')]
    if len(l54) != 1 or len(l55) != 1:
        print("RIG: merge-by-rule.sh's :54/:55 lines found %d/%d times, need 1/1" % (len(l54), len(l55)))
        return 2
    fixed = open(os.path.join(ROOT, "scripts", "ci-wait.sh"), encoding="utf-8").read()
    if fixed.count(FIX_URL) != 1 or fixed.count(FIX_SEL) != 1:
        print("RIG: the head_branch tests are not each in ci-wait.sh exactly once; P-TAG-SAB cannot plant")
        return 2
    sab = fixed.replace(FIX_URL, "").replace(FIX_SEL, "")
    tmp = tempfile.mkdtemp(prefix="ciw-selftest-")
    try:
        stubdir = os.path.join(tmp, "bin"); os.makedirs(stubdir)
        with open(os.path.join(stubdir, "stub.py"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(STUB)
        with open(os.path.join(stubdir, "gh"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write('#!/usr/bin/env bash\nexec "%s" -I "%s" "$@"\n' % (sys.executable.replace("\\", "/"),
                                                                       os.path.join(stubdir, "stub.py").replace("\\", "/")))
        os.chmod(os.path.join(stubdir, "gh"), 0o755)
        copies = {"old": argv[0], "fixed": os.path.join(ROOT, "scripts", "ci-wait.sh"), "sab": os.path.join(tmp, "ci-wait.sab.sh")}
        with open(copies["sab"], "w", encoding="utf-8", newline="\n") as fh:
            fh.write(sab)
        print("sabotage P-TAG-SAB planted: both head_branch tests removed; ci-wait changed (%d -> %d bytes)" % (len(fixed), len(sab)))
        # P-STUB, first, red first: the stub must write what gh writes, LF only. Run 1 (15:27Z) read 5 rows OFF because
        # the stub wrote CRLF. P-STUB-RED is run 1's stub (the LF line removed, byte-asserted), and it must show CR bytes;
        # P-STUB is this stub, and it must show none. Either off: no row is run.
        if STUB.count(LF_LINE) != 1:
            print("RIG: the stub's LF line is not in it exactly once; P-STUB-RED cannot plant")
            return 2
        with open(os.path.join(stubdir, "stub_run1.py"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(STUB.replace(LF_LINE, ""))
        fx0 = os.path.join(tmp, "fx-stub.json")
        with open(fx0, "w", encoding="utf-8") as fh:
            json.dump(fixture("today"), fh)
        e_all = '.check_runs[] | "\\(.name)\\t\\(.status)\\t\\(.conclusion)"'

        def stub_bytes(script):
            r = subprocess.run([sys.executable, "-I", os.path.join(stubdir, script), "api", "--paginate",
                                "repos/Valar-Systems/valar-scopes/commits/%s/check-runs?per_page=100" % SHA, "-q", e_all],
                               env=dict(os.environ, CIW_FIXTURE=fx0), capture_output=True, timeout=60)
            return {"exit": r.returncode, "cr_bytes": r.stdout.count(b"\r"), "lines": r.stdout.count(b"\n")}

        want_red = {"exit": 0, "cr_bytes": 15, "lines": 15} if os.linesep == "\r\n" else {"exit": 0, "cr_bytes": 0, "lines": 15}
        for name, script, want in (("P-STUB-RED", "stub_run1.py", want_red), ("P-STUB", "stub.py", {"exit": 0, "cr_bytes": 0, "lines": 15})):
            got = stub_bytes(script)
            print("%-10s %s\n           got  %s\n           want %s" % (name, "AS PREDICTED" if got == want else "OFF", got, want))
            if got != want:
                print("RIG: %s is off; the stub cannot be trusted to emulate gh, so no row is run" % name)
                return 2
        verdict_sh = os.path.join(tmp, "verdict.sh")
        with open(verdict_sh, "w", encoding="utf-8", newline="\n") as fh:
            fh.write('OUT="$(cat "$1")"\n%s\n%s\necho "PASS"\n' % (l54[0], l55[0]))

        def run(which, kind, branch, max_min="5"):
            fx = os.path.join(tmp, "fx-%s.json" % kind)
            with open(fx, "w", encoding="utf-8") as fh:
                json.dump(fixture(kind), fh)
            env = dict(os.environ, CIW_FIXTURE=fx, CI_WAIT_POLL_S="0", PATH=stubdir + os.pathsep + os.environ.get("PATH", ""))
            args = [bash, copies[which], SHA, max_min] + ([branch] if branch else [])
            r = subprocess.run(args, env=env, capture_output=True, text=True, timeout=120)
            outf = os.path.join(tmp, "out.txt")
            with open(outf, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(r.stdout)
            v = subprocess.run([bash, verdict_sh, outf], capture_output=True, text=True, timeout=60)
            first, tally, rows = parse(r.stdout)
            return {"exit": r.returncode, "stderr": r.stderr.strip(), "verdict": v.stdout.strip(),
                    "first": first, "tally": tally, "not_success": rows}, r.stdout

        res, off = [], 0

        def row(name, got, want):
            nonlocal off
            ok = got == want
            off += not ok
            res.append(name)
            print("%-10s %s\n           got  %s\n           want %s" % (name, "AS PREDICTED" if ok else "OFF", got, want))

        skip2 = ["promote\tcompleted\tskipped", "version\tcompleted\tskipped"]
        stop_fail, stop_wait = "STOP: H's CI has a failure", "STOP: H's CI did not finish"
        g_red, raw_red = run("old", "today", None)
        row("P-RED", g_red, {"exit": 0, "stderr": "", "verdict": stop_fail, "first": "unfinished 0 stable 15 timed-out no",
                             "tally": ["1 failure", "2 cancelled", "3 skipped", "9 success"],
                             "not_success": sorted(["host-tests\tcompleted\tfailure", "host-tests (ubuntu-26.04)\tcompleted\tcancelled",
                                                    "promote\tcompleted\tcancelled", "promote\tcompleted\tskipped",
                                                    "version\tcompleted\tskipped", "version\tcompleted\tskipped"])})
        clean = {"exit": 0, "stderr": "", "verdict": "PASS", "first": "unfinished 0 stable 9 timed-out no",
                 "tally": ["2 skipped", "7 success"], "not_success": skip2}
        row("P-REL", run("fixed", "today", "main")[0], clean)
        row("P-PUSH", run("fixed", "pushfail", "main")[0],
            {"exit": 0, "stderr": "", "verdict": stop_fail, "first": "unfinished 0 stable 9 timed-out no",
             "tally": ["1 failure", "2 skipped", "6 success"], "not_success": sorted(["host-tests\tcompleted\tfailure"] + skip2)})
        row("P-TAG", run("fixed", "tagfail", "main")[0], clean)
        row("P-TAG-SAB", run("sab", "tagfail", "main")[0],
            {"exit": 0, "stderr": "", "verdict": stop_fail, "first": "unfinished 0 stable 10 timed-out no",
             "tally": ["1 failure", "2 skipped", "7 success"], "not_success": sorted(["print-cards\tcompleted\tfailure"] + skip2)})
        row("P-SCHED", run("fixed", "sched", "main")[0], clean)
        row("P-NONE", run("fixed", "none", "main", max_min="2")[0],
            {"exit": 3, "stderr": "", "verdict": stop_wait, "first": "timed-out yes; last read: 0 runs", "tally": ["1 /"],
             "not_success": []})
        g_all, raw_all = run("fixed", "today", None)
        strip = lambda s: re.sub(r"^\d\d:\d\d:\d\dZ ", "", s)
        row("P-ALL", dict(g_all, raw_equal_to_red=strip(raw_all) == strip(raw_red)), dict(g_red, raw_equal_to_red=True))
        print("rows %d, as predicted %d, OFF %d" % (len(res), len(res) - off, off))
        return 0 if off == 0 else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
