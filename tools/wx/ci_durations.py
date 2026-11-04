"""The two slow tests' durations from CI's own logs: every workers run (and attempt) for the given commits.
Usage: python -I ci_durations.py <sha> [<sha> ...]   (gh must be logged in)"""
import json
import re
import subprocess
import sys

REPO = "Valar-Systems/valar-scopes"
TESTS = {"K4' sabotage": "SABOTAGE: tilesY for tilesX fails cell for cell",
         "WS2 200 crops": "200 / 200 crops through the worker are byte-identical to the flat reference"}
ANSI = re.compile(r"\x1b\[[0-9;]*m")


def gh(*args):
    return subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8", errors="replace").stdout


for sha in sys.argv[1:]:
    runs = json.loads(gh("api", f"repos/{REPO}/actions/runs?head_sha={sha}&per_page=50"))["workflow_runs"]
    for run in runs:
        if run["name"] != "workers":
            continue
        for att in range(1, run["run_attempt"] + 1):
            jobs = json.loads(gh("api", f"repos/{REPO}/actions/runs/{run['id']}/attempts/{att}/jobs"))["jobs"]
            for j in jobs:
                if j["name"] != "test (proxy)":
                    continue
                log = ANSI.sub("", gh("api", f"repos/{REPO}/actions/jobs/{j['id']}/logs"))
                row = {"sha": sha[:12], "event": run["event"], "run": run["id"], "attempt": att, "job": j["conclusion"]}
                for label, name in TESTS.items():
                    m = re.search(re.escape(name) + r"\s+(\d+)ms\s*$", log, re.M)
                    timed = re.search(r"FAIL .*" + re.escape(name), log)
                    row[label] = int(m.group(1)) if m else ("TIMED OUT (>5000)" if timed else "not found")
                print(json.dumps(row))
