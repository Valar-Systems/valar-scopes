#!/usr/bin/env python3
"""What the refresh-data job writes: docs/proxy-bill.md step 4e (JW1-JW3, the escape).

Read-only, GitHub only (the `gh` CLI, its own login; no Cloudflare call, no token file). For the two runs step 4c
measured, each environment's job log is fetched INTO MEMORY and searched for the ingests' own summary lines
(proxy/scripts/ingest-mildb.ts and ingest-airports.ts). PRINTS COUNTS ONLY: never a log line, a key, a value or a
shard name. Nothing from a log is written to disk except the counts.

  python -X utf8 -I tools/bill/job_writes_read.py read <out.json>
  python -X utf8 -I tools/bill/job_writes_read.py grade <read1> <read2>     the reads agree (60 s apart), then JW1-JW3

The lines it counts, from the scripts' source:
  mildb     "done: <N> mil:<hex> rows loaded to <env>"                  (ingest-mildb.ts:144)
  ap:       "ap: <N> keys written across <M> shard(s)."                  (ingest-airports.ts:570)
            "ap: <X>/<Y> shards changed"                                 (:499)
            "ap: no previous meta (or unreadable) -- writing every shard"  (:481), counted as present or absent
            "ap: sentinel written"                                       (:538), one write
  apt:      "done: <N> apt:* tiles loaded to <env>"                      (:720)
Exit: read 0; grade 0 graded, 3 ESCAPE, 4 REFUSED (under 60 s apart); 2 the rig (exception type only).
"""
import json
import re
import subprocess
import sys
from datetime import datetime, timezone

REPO = "Valar-Systems/valar-scopes"
RUNS = {"2026-11-09": 42655120486, "2026-11-16": 43901417755}
# Step 4c's measured job writes, each day's writes less that namespace's background median (production 1,547, staging 0).
MEASURED = {("2026-11-09", "production"): 67301 - 1547, ("2026-11-09", "staging"): 65678 - 0,
            ("2026-11-16", "production"): 66338, ("2026-11-16", "staging"): 65708}
JW1, JW2, JW3_TOL = (15000, 20000), (45000, 52000), 0.02
MIN_APART_S = 60

RX = {
    "mildb": re.compile(r"done: (\d+) mil:<hex> rows loaded to (production|staging)"),
    "ap_written": re.compile(r"ap: (\d+) keys written across (\d+) shard\(s\)\."),
    "ap_changed": re.compile(r"ap: (\d+)/(\d+) shards changed"),
    "ap_nometa": re.compile(r"ap: no previous meta \(or unreadable\) -- writing every shard"),
    "ap_sentinel": re.compile(r"ap: sentinel written \(nonce"),
    "apt": re.compile(r"done: (\d+) apt:\* tiles loaded to (production|staging)"),
}


class Rig(Exception):
    pass


def gh(*args):
    r = subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    if r.returncode != 0:
        raise Rig("gh %s failed (exit %d)" % (args[0], r.returncode))
    return r.stdout


def counts(text):
    """Counts from one job log, held in memory; nothing of the text leaves this function."""
    c = {"mildb": None, "ap_written": None, "ap_shards_written": None, "ap_changed": None, "ap_total_shards": None,
         "ap_nometa": False, "ap_sentinel": 0, "apt": None, "envs_named": set()}
    for line in text.splitlines():
        m = RX["mildb"].search(line)
        if m:
            c["mildb"] = int(m.group(1)); c["envs_named"].add(m.group(2))
        m = RX["ap_written"].search(line)
        if m:
            c["ap_written"], c["ap_shards_written"] = int(m.group(1)), int(m.group(2))
        m = RX["ap_changed"].search(line)
        if m:
            c["ap_changed"], c["ap_total_shards"] = int(m.group(1)), int(m.group(2))
        if RX["ap_nometa"].search(line):
            c["ap_nometa"] = True
        if RX["ap_sentinel"].search(line):
            c["ap_sentinel"] += 1
        m = RX["apt"].search(line)
        if m:
            c["apt"] = int(m.group(1)); c["envs_named"].add(m.group(2))
    c["envs_named"] = sorted(c["envs_named"])
    return c


def read(path):
    out = {"read_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), "runs": {}}
    for day, run in RUNS.items():
        jobs = json.loads(gh("api", "repos/%s/actions/runs/%d/jobs?per_page=100" % (REPO, run)))["jobs"]
        rec = {}
        for j in jobs:
            name = j.get("name", "")
            env = "production" if "production" in name else ("staging" if "staging" in name else None)
            if env is None:
                continue
            text = gh("run", "view", "--repo", REPO, "--job", str(j["id"]), "--log")
            rec[env] = counts(text)
            rec[env]["conclusion"] = j.get("conclusion")
            del text
        out["runs"][day] = rec
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
        fh.write("\n")
    for day, rec in out["runs"].items():
        for env, c in sorted(rec.items()):
            print("%s %s: %s; mildb %s; ap: written %s in %s shard(s), changed %s/%s, no-meta %s, sentinel %d; apt: tiles %s" % (
                day, env, c["conclusion"], c["mildb"], c["ap_written"], c["ap_shards_written"], c["ap_changed"],
                c["ap_total_shards"], c["ap_nometa"], c["ap_sentinel"], c["apt"]))
    return 0


def stamp(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


def grade(p1, p2):
    d, d2 = (json.load(open(p, encoding="utf-8")) for p in (p1, p2))
    apart = abs((stamp(d2["read_at"]) - stamp(d["read_at"])).total_seconds())
    print("reads %s and %s, %d s apart" % (d["read_at"], d2["read_at"], apart))
    if apart < MIN_APART_S:
        print("REFUSED: the two reads are under %d s apart; take another" % MIN_APART_S)
        return 4
    esc = []
    if d["runs"] != d2["runs"]:
        esc.append("the two reads differ")
    for day in RUNS:
        for env in ("production", "staging"):
            c = d["runs"].get(day, {}).get(env)
            if c is None:
                esc.append("%s %s: no job log" % (day, env)); continue
            if c["conclusion"] != "success":
                esc.append("%s %s: the job did not succeed" % (day, env))
            for k in ("mildb", "ap_written", "apt"):
                if c[k] is None:
                    esc.append("%s %s: no %s count line" % (day, env, k))
    if esc:
        print("ESCAPE: " + "; ".join(esc) + " -- stop and report, with no reading")
        return 3
    ok1 = ok2 = ok3 = True
    for day in RUNS:
        for env in ("production", "staging"):
            c = d["runs"][day][env]
            rest = c["ap_written"] + c["ap_sentinel"] + c["apt"]
            total = c["mildb"] + rest
            want = MEASURED[(day, env)]
            j1 = JW1[0] <= c["mildb"] <= JW1[1]
            j2 = JW2[0] <= rest <= JW2[1]
            j3 = abs(total - want) <= JW3_TOL * want
            ok1 &= j1; ok2 &= j2; ok3 &= j3
            print("%s %-10s mildb %6d %s | airports %6d (ap: %d + sentinel %d + apt: %d) %s | sum %6d vs measured %6d (%+.1f%%) %s" % (
                day, env, c["mildb"], "ok" if j1 else "OFF", rest, c["ap_written"], c["ap_sentinel"], c["apt"],
                "ok" if j2 else "OFF", total, want, 100.0 * (total - want) / want, "ok" if j3 else "OFF"))
    g = lambda ok: "HOLDS" if ok else "FAILS"
    print("JW1 (mildb 15,000-20,000 per environment per run) -> %s" % g(ok1))
    print("JW2 (airports 45,000-52,000, its diff not skipping) -> %s" % g(ok2))
    print("JW3 (the sum within 2%% of step 4c's measured job writes) -> %s" % g(ok3))
    return 0


def main(argv):
    try:
        if len(argv) == 2 and argv[0] == "read":
            return read(argv[1])
        if len(argv) == 3 and argv[0] == "grade":
            return grade(argv[1], argv[2])
        print(__doc__)
        return 2
    except Rig as e:
        print("RIG: %s" % e)
        return 2
    except Exception as e:   # the type only
        print("RIG: %s" % type(e).__name__)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
