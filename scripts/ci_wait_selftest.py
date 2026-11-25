#!/usr/bin/env python3
"""The merge driver's CI read, offline: scripts/ci-wait.sh against a STUB `gh`, graded by merge-by-rule.sh's OWN lines.

Registered as mbr-ci-scope-registration.md (2026-11-19T15:22:48Z): merge-by-rule's "H's CI" is the base's own push CI.
Re-registered as ci-wait-push-runs-registration.md (2026-11-21T18:39:18Z, with AMENDMENT 1): the push RUNS decide
"finished" (all completed, the run set stable over two reads) and "red" (a run outside success/skipped/neutral, or a JOB
concluding failure/cancelled/timed_out/action_required/startup_failure/stale); ci-wait's exit code is the verdict, and
merge-by-rule decides by it. AMENDMENT 3 (after the live control L-POSTED read the jobs API listing a POSTED check-run as
a job): a job's html_url must be one of a KNOWN SET of two shapes, actions/runs/<that run>/job/<id> (kept) or runs/<id> (a
posted row: dropped and counted), anything else exit 4. The stub's jobs endpoint therefore returns EVERY check-run of the
run's suite, as GitHub does, its urls built from scripts/ci_wait_selftest_shapes.json, which make-shapes.py GENERATED from
the recorded read (_handoffs/blipscope-5c/ci/lposted-jobs-read.json, sha256 b3565d52778c...); its sha is asserted here.

  python -X utf8 -I scripts/ci_wait_selftest.py <ci-wait.sh before the fix> <merge-by-rule.sh before the fix>

The CURRENT copies (before this fix) are the RED rows' subjects, byte-asserted to their git blobs. The stub answers ONLY
the exact queries ci-wait.sh sends (an unknown one exits 1, so a row reads RIG, never a pass), and emulates the API's own
filters (head_sha, event, branch, filter=latest) and ci-wait's jq select. A RED row's verdict is the OLD merge-by-rule's
two grep lines on the OLD ci-wait's output; a fixed row's verdict is the NEW merge-by-rule's two exit-code lines, run with
ci-wait's real exit code. Each line is taken from its file, asserted present once. Every sabotage is asserted applied
(its anchor present exactly once, the bytes changed) before its row runs. Rows compare whole results; tally and rows as
sorted lists. Red first; the first row OFF stops the run (the rest are NOT RUN). Exit 0 every row as predicted; 1 a row
OFF; 2 the rig.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHA = "ab" * 20                                    # synthetic, built at run time
OLD_BLOBS = {"ci-wait": "6e068f114806", "merge-by-rule": "7b1832b5b0a3"}   # main 51655bc5e701, before this fix
SHAPES_SOURCE = "b3565d52778c"   # the recorded jobs-API read (_handoffs/blipscope-5c/ci/lposted-jobs-read.json) the shapes came from
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
E_RUNROW = '"\\(.id)\\t\\(.name)\\t\\(.status)\\t\\(.conclusion)"'
E_JOBS = '.jobs[] | "\\(.name)\\t\\(.status)\\t\\(.conclusion)\\t\\(.html_url)"'
mj = re.fullmatch(r"repos/%s/actions/runs/(\d+)/jobs" % re.escape(R), path)
if path == "repos/%s/commits/%s/check-runs" % (R, fx["sha"]):
    if expr == E_ALL:
        rows = ["%s\t%s\t%s" % (c["name"], c["status"], c["conclusion"]) for c in fx["check_runs"]]
    elif expr == E_SUITE:
        rows = ["%s\t%s\t%s\t%s" % (c["suite"], c["name"], c["status"], c["conclusion"]) for c in fx["check_runs"]]
    else:
        die("unknown check-runs expression")
elif path == "repos/%s/actions/runs" % R:
    m = re.fullmatch(r'\.workflow_runs\[\] \| select\((.+)\) \| (.+)', expr)
    if not m or m.group(2) not in (".check_suite_id", E_RUNROW):
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
    runs = [r for r in runs if all(r[k] == v for k, v in conds)]                   # ci-wait's own select
    if m.group(2) == ".check_suite_id":
        rows = [str(r["suite"]) for r in runs]
    else:
        rows = ["%s\t%s\t%s\t%s" % (r["id"], r["name"], r["status"], r["conclusion"]) for r in runs]
elif mj:
    if expr != E_JOBS or q.get("filter") != "latest":
        die("unknown jobs query")
    hit = [r for r in fx["runs"] if str(r["id"]) == mj.group(1)]
    if len(hit) != 1:
        die("unknown run id")
    sh = json.load(open(os.environ["CIW_SHAPES"], encoding="utf-8"))
    r0, rows = hit[0], []
    for c in fx["check_runs"]:            # the jobs API lists EVERY check-run of the run's suite (read 2026-11-21)
        if c["suite"] != r0["suite"]:
            continue
        if c["url"] == "job":
            u = sh["kept_url"].format(run_id=r0["id"], id=c["jid"])
        elif c["url"] == "posted":
            u = sh["posted_url"].format(id=c["jid"])
        else:                              # "stray": a job url naming ANOTHER run's id
            u = sh["kept_url"].format(run_id=r0["id"] + 1000, id=c["jid"])
        rows.append("%s\t%s\t%s\t%s" % (c["name"], c["status"], c["conclusion"], u))
else:
    die("unknown path")
for r in rows:
    print(r)
'''
LF_LINE = 'sys.stdout.reconfigure(newline="\\n")   # gh is a Go binary and writes LF; Windows Python\'s text stdout would write CRLF\n'
FIX_URL = '&branch=$branch'
FIX_SEL = ' and .head_branch == \\"$branch\\"'
STATUS_RULE = 'allc=$([ "$open" -eq 0 ] && echo 1 || echo 0)'


def fixture(kind):
    """main's push runs, a TAG-push run, release runs; each run with its status, conclusion and JOBS. A job is also a
    check-run in its run's suite, as GitHub makes it; a POSTED check-run sits in a suite but is not a job."""
    runs, crs = [], []
    jid = [5000]

    def nid():
        jid[0] += 1
        return jid[0]

    def run(suite, event, branch, name, jobs, status="completed", conclusion="success"):
        runs.append({"suite": suite, "id": 9000 + suite, "event": event, "head_branch": branch, "head_sha": SHA,
                     "name": name, "status": status, "conclusion": conclusion})
        crs.extend({"suite": suite, "name": n, "status": "completed", "conclusion": c, "url": "job", "jid": nid()} for n, c in jobs)

    fw = [("build (blipscope-s3-128, s3-128)", "success"), ("host-tests", "success"), ("host-tests (ubuntu-24.04)", "success"),
          ("host-tests (ubuntu-26.04)", "success"), ("promote", "skipped"), ("version", "skipped")]
    fw_st, fw_c = "completed", "success"
    if kind in ("pushfail", "softjob"):
        fw = [(n, "failure" if n == "host-tests" else c) for n, c in fw]
        fw_c = "failure" if kind == "pushfail" else "success"
    if kind == "inprog":
        fw, fw_st, fw_c = fw[:2], "in_progress", "null"
    if kind == "postearly":
        q = {"status": "queued", "conclusion": "null"}
        for s, n in ((101, "firmware"), (102, "bench-tools"), (103, "device-ids"), (104, "no-traffic-captures")):
            run(s, "push", "main", n, [], **q)
    elif kind != "none":
        run(101, "push", "main", "firmware", fw, fw_st, fw_c)
        run(102, "push", "main", "bench-tools", [("app-only-write", "success")])
        run(103, "push", "main", "device-ids", [("no-real-device-ids", "success")])
        run(104, "push", "main", "no-traffic-captures", [("no-traffic-captures", "success")])
    tf = kind == "tagfail"
    run(201, "push", "rehearsal-perms-a", "print-cards", [("print-cards", "failure" if tf else "success")],
        conclusion="failure" if tf else "success")
    run(301, "release", "rehearsal-perms-a", "firmware",
        [("host-tests", "failure"), ("host-tests (ubuntu-26.04)", "cancelled"), ("promote", "cancelled"), ("version", "skipped")],
        conclusion="failure")
    run(302, "release", "rehearsal-perms-a", "web-renderer", [("render", "success")])
    if kind == "sched":
        run(401, "schedule", "main", "photo-drift", [("photo-drift", "failure")], conclusion="failure")
    if kind == "runfail":
        run(105, "push", "main", "workers", [], conclusion="startup_failure")
    if kind == "skiprun":
        run(106, "push", "main", "account-budget", [("guard", "skipped")], conclusion="skipped")
    if kind == "postfail":
        crs.append({"suite": 102, "name": "photo-drift", "status": "completed", "conclusion": "failure", "url": "posted", "jid": 7000001})
    if kind == "postearly":
        crs.append({"suite": 102, "name": "photo-drift", "status": "completed", "conclusion": "success", "url": "posted", "jid": 7000001})
    if kind == "unknown":
        crs.append({"suite": 102, "name": "stray-job", "status": "completed", "conclusion": "success", "url": "stray", "jid": 7000002})
    return {"sha": SHA, "runs": runs, "check_runs": crs}


def find_bash():
    if sys.platform == "win32":   # Git's bash: the one System32 holds is WSL's, a different machine
        for p in (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files\Git\usr\bin\bash.exe"):
            if os.path.exists(p):
                return p
    return shutil.which("bash") or "bash"


def blob_id(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def parse(out):
    lines = out.splitlines()
    first = re.sub(r"^\d\d:\d\d:\d\dZ ", "", lines[0]) if lines else ""
    runs_line = next((l for l in lines[1:] if l.startswith("push runs ")), "")
    jobs_line = next((l for l in lines[1:] if l.startswith("jobs read ")), "")
    dropped = sorted(l for l in lines[1:] if l.startswith("dropped ") or l.startswith("unreadable "))
    tally = sorted(l.strip() for l in lines[1:] if re.fullmatch(r"\s+\d+ \S+", l))
    rows = sorted(l for l in lines[1:] if "\t" in l)
    return first, runs_line, jobs_line, dropped, tally, rows


class Off(Exception):
    pass


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    bash = find_bash()
    old = {}
    for key, p in zip(("ci-wait", "merge-by-rule"), argv):
        data = open(p, "rb").read()
        if not blob_id(data).startswith(OLD_BLOBS[key]):
            print("RIG: %s is blob %s, not the current %s" % (p, blob_id(data)[:12], OLD_BLOBS[key]))
            return 2
        old[key] = p
    mbr_old = open(old["merge-by-rule"], encoding="utf-8").read().splitlines()
    o56 = [l for l in mbr_old if l.startswith('echo "$OUT" | head -1 | grep -q ')]
    o57 = [l for l in mbr_old if l.startswith('echo "$OUT" | grep -q -E ')]
    mbr_new = open(os.path.join(ROOT, "scripts", "merge-by-rule.sh"), encoding="utf-8").read().splitlines()
    n3 = [l for l in mbr_new if l.startswith('[ "$RC" -eq 3 ] ')]
    n4 = [l for l in mbr_new if l.startswith('[ "$RC" -eq 4 ] ')]
    n0 = [l for l in mbr_new if l.startswith('[ "$RC" -eq 0 ] ')]
    if (len(o56), len(o57), len(n3), len(n4), len(n0)) != (1, 1, 1, 1, 1):
        print("RIG: the verdict lines found %d/%d (old) and %d/%d/%d (new) times, need 1 each"
              % (len(o56), len(o57), len(n3), len(n4), len(n0)))
        return 2
    shapes = os.path.join(ROOT, "scripts", "ci_wait_selftest_shapes.json")
    sh = json.load(open(shapes, encoding="utf-8"))
    if not (sh.get("source_sha256", "").startswith(SHAPES_SOURCE) and sh.get("kept_url", "").endswith("/actions/runs/{run_id}/job/{id}")
            and sh.get("posted_url", "").endswith("/runs/{id}") and "html_url" in sh.get("fields", [])):
        print("RIG: %s is not the shapes file generated from the recorded read %s" % (shapes, SHAPES_SOURCE))
        return 2
    print("shapes from the recorded read %s: kept %s, posted %s" % (sh["source_sha256"][:12], sh["kept_url"], sh["posted_url"]))
    fixed = open(os.path.join(ROOT, "scripts", "ci-wait.sh"), encoding="utf-8").read()
    for name, anchor in (("FIX_URL", FIX_URL), ("FIX_SEL", FIX_SEL), ("STATUS_RULE", STATUS_RULE)):
        if fixed.count(anchor) != 1:
            print("RIG: %s is in ci-wait.sh %d times, need exactly 1; its sabotage cannot plant" % (name, fixed.count(anchor)))
            return 2
    tagsab = fixed.replace(FIX_URL, "").replace(FIX_SEL, "")
    statsab = fixed.replace(STATUS_RULE, "allc=1")
    for name, sab in (("P-TAG-SAB", tagsab), ("P-STATUS-SAB", statsab)):
        if sab == fixed:
            print("RIG: %s's copy equals the fix: the sabotage did not plant" % name)
            return 2
    print("sabotage P-TAG-SAB planted: both head_branch tests removed (%d -> %d bytes)" % (len(fixed), len(tagsab)))
    print("sabotage P-STATUS-SAB planted: the status rule replaced by allc=1 (%d -> %d bytes)" % (len(fixed), len(statsab)))
    tmp = tempfile.mkdtemp(prefix="ciw-selftest-")
    try:
        stubdir = os.path.join(tmp, "bin"); os.makedirs(stubdir)
        with open(os.path.join(stubdir, "stub.py"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(STUB)
        with open(os.path.join(stubdir, "gh"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write('#!/usr/bin/env bash\nexec "%s" -I "%s" "$@"\n' % (sys.executable.replace("\\", "/"),
                                                                       os.path.join(stubdir, "stub.py").replace("\\", "/")))
        os.chmod(os.path.join(stubdir, "gh"), 0o755)
        copies = {"old": old["ci-wait"], "fixed": os.path.join(ROOT, "scripts", "ci-wait.sh"),
                  "tagsab": os.path.join(tmp, "ci-wait.tagsab.sh"), "statsab": os.path.join(tmp, "ci-wait.statsab.sh")}
        for k, body in (("tagsab", tagsab), ("statsab", statsab)):
            with open(copies[k], "w", encoding="utf-8", newline="\n") as fh:
                fh.write(body)
        # P-STUB, first, red first: the stub must write what gh writes, LF only (run 1 of 2026-11-19 read 5 rows OFF on a
        # CRLF stub). P-STUB-RED is that stub (the LF line removed); it must show CR bytes; P-STUB must show none.
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
            print("%-15s %s\n                got  %s\n                want %s" % (name, "AS PREDICTED" if got == want else "OFF", got, want))
            if got != want:
                print("RIG: %s is off; the stub cannot be trusted to emulate gh, so no row is run" % name)
                return 2
        v_old = os.path.join(tmp, "verdict_old.sh")
        with open(v_old, "w", encoding="utf-8", newline="\n") as fh:
            fh.write('OUT="$(cat "$1")"\n%s\n%s\necho "PASS"\n' % (o56[0], o57[0]))
        v_new = os.path.join(tmp, "verdict_new.sh")
        with open(v_new, "w", encoding="utf-8", newline="\n") as fh:
            fh.write('OUT="$(cat "$1")"; RC="$2"\n%s\n%s\n%s\necho "PASS"\n' % (n3[0], n4[0], n0[0]))

        def run(which, kind, branch, max_min="5"):
            fx = os.path.join(tmp, "fx-%s.json" % kind)
            with open(fx, "w", encoding="utf-8") as fh:
                json.dump(fixture(kind), fh)
            env = dict(os.environ, CIW_FIXTURE=fx, CIW_SHAPES=shapes, CI_WAIT_POLL_S="0",
                       PATH=stubdir + os.pathsep + os.environ.get("PATH", ""))
            args = [bash, copies[which], SHA, max_min] + ([branch] if branch else [])
            r = subprocess.run(args, env=env, capture_output=True, text=True, timeout=180)
            outf = os.path.join(tmp, "out.txt")
            with open(outf, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(r.stdout)
            vargs = [bash, v_old, outf] if which == "old" else [bash, v_new, outf, str(r.returncode)]
            v = subprocess.run(vargs, capture_output=True, text=True, timeout=60)
            first, runs_line, jobs_line, dropped, tally, rows = parse(r.stdout)
            return {"exit": r.returncode, "stderr": r.stderr.strip(), "verdict": v.stdout.strip(), "first": first,
                    "runs_line": runs_line, "jobs_line": jobs_line, "dropped": dropped, "tally": tally,
                    "not_success": rows}, r.stdout

        done = []

        def row(name, got, want):
            ok = got == want
            done.append(name)
            print("%-15s %s\n                got  %s\n                want %s" % (name, "AS PREDICTED" if ok else "OFF", got, want))
            if not ok:
                raise Off(name)

        skip2 = ["promote\tcompleted\tskipped", "version\tcompleted\tskipped"]
        f_old, wait = "STOP: H's CI has a failure", "STOP: H's CI did not finish"
        f_new = "STOP: H's CI has a failure (ci-wait exit 1)"

        def res(exit_, verdict, first, tally, rows, runs_line="", jobs_line="", dropped=()):
            return {"exit": exit_, "stderr": "", "verdict": verdict, "first": first, "runs_line": runs_line,
                    "jobs_line": jobs_line, "dropped": sorted(dropped), "tally": sorted(tally), "not_success": sorted(rows)}

        j = lambda n, p=0: "jobs read %d, posted dropped %d, kept %d" % (n + p, p, n)
        unread = "STOP: H's CI could not be read (ci-wait exit 4: a job of unknown shape)"
        clean = res(0, "PASS", "unfinished 0 stable 9 timed-out no", ["2 skipped", "7 success"], skip2, "push runs 4: 4 success", j(9))
        plan = [
            # RED first: the CURRENT copies, each showing its defect
            ("P-POSTFAIL-RED", ("old", "postfail", "main"),
             res(0, f_old, "unfinished 0 stable 10 timed-out no", ["1 failure", "2 skipped", "7 success"],
                 ["photo-drift\tcompleted\tfailure"] + skip2)),
            ("P-POSTEARLY-RED", ("old", "postearly", "main"), res(0, "PASS", "unfinished 0 stable 1 timed-out no", ["1 success"], [])),
            ("P-RUNFAIL-RED", ("old", "runfail", "main"), res(0, "PASS", "unfinished 0 stable 9 timed-out no", ["2 skipped", "7 success"], skip2)),
            ("P-INPROG-RED", ("old", "inprog", "main"), res(0, "PASS", "unfinished 0 stable 5 timed-out no", ["5 success"], [])),
            ("P-SOFTJOB-RED", ("old", "softjob", "main"),
             res(0, f_old, "unfinished 0 stable 9 timed-out no", ["1 failure", "2 skipped", "6 success"], ["host-tests\tcompleted\tfailure"] + skip2)),
            ("P-UNKNOWN-RED", ("old", "unknown", "main"), res(0, "PASS", "unfinished 0 stable 10 timed-out no", ["2 skipped", "8 success"], skip2)),
            # the fix
            ("P-POSTFAIL", ("fixed", "postfail", "main"),
             res(0, "PASS", "unfinished 0 stable 9 timed-out no", ["2 skipped", "7 success"], skip2, "push runs 4: 4 success", j(9, 1),
                 ["dropped 1 (photo-drift) from run bench-tools"])),
            ("P-POSTEARLY", ("fixed", "postearly", "main", "2"),
             res(3, wait, "timed-out yes; last read: 4 runs, 1 jobs, 1 posted", ["4 queued/null"], [])),
            ("P-RUNFAIL", ("fixed", "runfail", "main"),
             res(1, f_new, "unfinished 0 stable 9 timed-out no", ["2 skipped", "7 success"],
                 skip2 + ["run workers\tcompleted\tstartup_failure"], "push runs 5: 1 startup_failure, 4 success", j(9))),
            ("P-INPROG", ("fixed", "inprog", "main", "2"),
             res(3, wait, "timed-out yes; last read: 4 runs, 5 jobs, 0 posted", ["1 in_progress/null", "3 completed/success"], [])),
            ("P-STATUS-SAB", ("statsab", "inprog", "main"),
             res(1, f_new, "unfinished 0 stable 5 timed-out no", ["5 success"], ["run firmware\tin_progress\tnull"],
                 "push runs 4: 1 null, 3 success", j(5))),
            ("P-PUSH", ("fixed", "pushfail", "main"),
             res(1, f_new, "unfinished 0 stable 9 timed-out no", ["1 failure", "2 skipped", "6 success"],
                 ["host-tests\tcompleted\tfailure"] + skip2 + ["run firmware\tcompleted\tfailure"], "push runs 4: 1 failure, 3 success", j(9))),
            ("P-SOFTJOB", ("fixed", "softjob", "main"),
             res(1, f_new, "unfinished 0 stable 9 timed-out no", ["1 failure", "2 skipped", "6 success"],
                 ["host-tests\tcompleted\tfailure"] + skip2, "push runs 4: 4 success", j(9))),
            ("P-REL", ("fixed", "today", "main"), clean),
            ("P-TAG", ("fixed", "tagfail", "main"), clean),
            ("P-SCHED", ("fixed", "sched", "main"), clean),
            ("P-TAG-SAB", ("tagsab", "tagfail", "main"),
             res(1, f_new, "unfinished 0 stable 10 timed-out no", ["1 failure", "2 skipped", "7 success"],
                 ["print-cards\tcompleted\tfailure"] + skip2 + ["run print-cards\tcompleted\tfailure"], "push runs 5: 1 failure, 4 success", j(10))),
            ("P-NONE", ("fixed", "none", "main", "2"), res(3, wait, "timed-out yes; last read: 0 runs, 0 jobs, 0 posted", [], [])),
            ("P-UNKNOWN", ("fixed", "unknown", "main"),
             res(4, unread, "UNREADABLE: 1 job(s) whose html_url matches neither known shape", [], [], "", "",
                 ["unreadable 1 (stray-job) from run bench-tools"])),
            ("P-SKIPRUN", ("fixed", "skiprun", "main"),
             res(0, "PASS", "unfinished 0 stable 10 timed-out no", ["3 skipped", "7 success"],
                 ["guard\tcompleted\tskipped"] + skip2 + ["run account-budget\tcompleted\tskipped"], "push runs 5: 1 skipped, 4 success", j(10))),
        ]
        try:
            for name, args, want in plan:
                row(name, run(*args)[0], want)
            g_old, raw_old = run("old", "today", None)
            g_new, raw_new = run("fixed", "today", None)
            strip = lambda s: re.sub(r"^\d\d:\d\d:\d\dZ ", "", s)
            row("P-ALL", {"exit": g_new["exit"], "stdout_equal_to_current": strip(raw_new) == strip(raw_old), "first": g_new["first"]},
                {"exit": 0, "stdout_equal_to_current": True, "first": g_old["first"]})
        except Off as e:
            left = [n for n, _, _ in plan if n not in done] + ([] if "P-ALL" in done else ["P-ALL"])
            print("STOP at %s: OFF. NOT RUN: %s" % (e, ", ".join(left)))
            print("rows run %d, as predicted %d, OFF 1" % (len(done), len(done) - 1))
            return 1
        print("rows %d, as predicted %d, OFF 0" % (len(done), len(done)))
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
