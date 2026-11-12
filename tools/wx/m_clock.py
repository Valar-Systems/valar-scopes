"""The M stage (docs/wx-opera-dev-measure.md), by way of Plan S's clock driver: one dev product at a time, bounded.
Made from s_clock.py (ruled ~17:12Z 2026-11-05; docs/wx-opuk.md). No tail: the cron is live and kept by
`idle`, so firings are regular. A trigger, not a reading -- the graded count is s_reads.py by version id.

  PLAN_S_WANT=<merged sha> M_PROXY=<checkout>/proxy python -I m_clock.py <opwe|opsn|opux|opmt> <label> s4
      until the product's 12th counted `stored` (CAP_MIN, 100), then park on `idle`
  ... m_clock.py off <label> s5      deploy `off` (removes the cron), no park

  1. the firing offset: the last two invocations' scheduledTime in Workers Logs (must be 60 s +/- 2 s apart)
  2. deploy the phase ~20 s after a firing starts (its next N firings are the phase's)
  3. a phase longer than 6 min is checked at the deploy + 360 s: no invocation of its version started within 300 s
     of the deploy -> park now and report (the ~03:55Z rule, judged on scheduledTime)
  4. park on `idle` ~20 s after the phase's Nth firing starts (S4: at the first slot after the 12th counted stored)
  Any error after the phase deploy parks on `idle` (try/finally). Read-only Logs; tokens never printed.
"""
import json
import math
import os
import re
import subprocess
import sys
import time
import urllib.request
from datetime import datetime, timezone

SCRATCH = r"C:\Users\DanielFrenkel\AppData\Local\Temp\claude\c--Github-Blipscope\fb8462e7-ee3a-4757-8b09-2d0165eb9254\scratchpad"
PROXY = os.environ["M_PROXY"]
WRANGLER = os.path.join(PROXY, "node_modules", "wrangler", "bin", "wrangler.js")
WANT = os.environ.get("PLAN_S_WANT", "")
CFG = "wx-ingest-dev.wrangler.toml"
ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"
SCRIPT = "blipscope-wx-ingest-dev"
U = dict(text=True, encoding="utf-8", errors="replace")
hms = lambda t: datetime.fromtimestamp(t, timezone.utc).strftime("%H:%M:%S.%f")[:-3] + "Z"
say = lambda *a: print(datetime.now(timezone.utc).strftime("%H:%M:%S.%f")[:-3] + "Z", *a, flush=True)


import importlib.util as _ilu   # the token-file helper, by path: python -I drops this folder from sys.path
_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md
def token():
    return TOKEN_FILE.token("observability-read")


def invocations(a_s, b_s):
    """{scheduledTime: {ver, checks}} for the dev script, from Workers Logs."""
    evs, off = [], None
    while True:
        body = {"queryId": "plan-s-clock", "timeframe": {"from": int(a_s * 1000), "to": int(b_s * 1000)}, "view": "events",
                "limit": 2000, "parameters": {"filters": [{"key": "$metadata.service", "operation": "eq", "type": "string", "value": SCRIPT}]}}
        if off:
            body["offset"] = off
        req = urllib.request.Request(f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query",
                                     data=json.dumps(body).encode(), method="POST",
                                     headers={"Authorization": "Bearer " + token(), "Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=120) as r:
            page = json.loads(r.read())["result"]["events"]["events"]
        evs += page
        if len(page) < 2000:
            break
        off = page[-1]["$metadata"]["id"]
    inv = {}
    for e in evs:
        w = e.get("$workers") or {}
        st = (w.get("event") or {}).get("scheduledTime")
        if st is None:
            continue
        d = inv.setdefault(st, {"ver": None, "checks": []})
        if (w.get("scriptVersion") or {}).get("id"):
            d["ver"] = w["scriptVersion"]["id"]
        s = e.get("source") or {}
        if s.get("product"):
            d["checks"].append((s["product"], s.get("status") or s.get("evt")))
    return inv


def graphql_count(a_s, b_s):
    """R3-6's instrument: the dev script's invocations in [a, b), any status (no version filter)."""
    tok = TOKEN_FILE.token("analytics-read")
    q = """query($acct: String!, $a: Time!, $b: Time!, $s: String!) { viewer { accounts(filter: {accountTag: $acct}) {
      workersInvocationsAdaptive(limit: 100, filter: {datetime_geq: $a, datetime_lt: $b, scriptName: $s}) { sum { requests } } } } }"""
    iso = lambda t: datetime.fromtimestamp(t, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    req = urllib.request.Request("https://api.cloudflare.com/client/v4/graphql", method="POST",
                                 data=json.dumps({"query": q, "variables": {"acct": ACCOUNT, "a": iso(a_s), "b": iso(b_s), "s": SCRIPT}}).encode(),
                                 headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        g = json.loads(r.read())
    return sum(x["sum"]["requests"] for x in g["data"]["viewer"]["accounts"][0]["workersInvocationsAdaptive"])


def deploy(env, label):
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=PROXY, capture_output=True, **U).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=PROXY, capture_output=True, **U).stdout.strip()
    if len(WANT) != 40 or head != WANT or dirty or os.environ.get("CLOUDFLARE_API_TOKEN"):
        raise SystemExit(f"REFUSED: head {head[:12]} want {WANT[:12]} dirty={bool(dirty)}")
    t0 = time.time()
    r = subprocess.run(["node", WRANGLER, "deploy", "-c", CFG, "-e", env], cwd=PROXY, capture_output=True, **U)
    t1 = time.time()
    open(os.path.join(SCRATCH, f"plan-s-{label}.out"), "w", encoding="utf-8").write(r.stdout)
    open(os.path.join(SCRATCH, f"plan-s-{label}.err"), "w", encoding="utf-8").write(r.stderr)
    m = re.search(r"Current Version ID: (\S+)", r.stdout)
    trig = [l.strip() for l in r.stdout.splitlines() if "schedule" in l or "triggers" in l]
    say(f"deploy {label} (-e {env}) {hms(t0)} -> {hms(t1)} exit {r.returncode} version {m.group(1) if m else None} "
        f"stderr {len(r.stderr)} bytes | {trig}")
    if r.returncode != 0 or not m:
        raise SystemExit("deploy failed")
    return m.group(1), t1


def sleep_until(t):
    while time.time() < t:
        time.sleep(min(1.0, max(0.0, t - time.time())))


def slot_after(t, offs, lo=20, hi=40):
    """The first time >= t that sits lo..hi s after a firing start (firings at offs mod 60)."""
    k = math.floor((t - offs) / 60)
    while True:
        start = k * 60 + offs
        if start + hi >= t:
            return max(t, start + lo)
        k += 1


env, label, how = sys.argv[1], sys.argv[2], sys.argv[3]
# 1. the firing offset, from the last two invocations
now = time.time()
inv = invocations(now - 600, now)
sts = sorted(inv)[-2:]
if len(sts) < 2 or abs((sts[1] - sts[0]) - 60) > 2:
    raise SystemExit(f"REFUSED: no regular firing to read an offset from (last starts: {[hms(s) for s in sts]})")
offs = sts[1] % 60
say(f"firing offset {offs:.3f} s past the minute (last two starts {hms(sts[0])}, {hms(sts[1])})")

if how == "s5":
    sleep_until(slot_after(time.time(), offs))
    version, t_dep = deploy(env, label)
    say(f"S5 deployed; read at the deploy + 1,500 s = {hms(t_dep + 1500)} and 5 min later")
    sys.exit(0)

# 2. the phase deploy, ~20 s after a firing start
dep_at = slot_after(time.time() + 5, offs)
first = math.floor((dep_at - offs) / 60) * 60 + offs + 60      # the phase's first firing
sleep_until(dep_at)
version, t_dep = deploy(env, label)
parked, why = False, None
try:
    if how == "s4":
        cap, checked = t_dep + 100 * 60, False        # the ~22:54Z ruling: each run bounded, its length stated
        while True:
            time.sleep(60)
            inv = invocations(t_dep - 5, time.time())
            mine = sorted((st, d) for st, d in inv.items() if d["ver"] == version)
            if not checked and time.time() > t_dep + 360:
                checked = True
                if not any(st <= t_dep + 300 for st, _ in mine):
                    why = "NO FIRST INVOCATION IN 5 MIN"
                    break
            seq = [(st, s) for st, d in mine for p, s in d["checks"] if p == env and s in ("stored", "backfilled")]
            last_bf = max((st for st, s in seq if s == "backfilled"), default=None)
            counted = sorted(st for st, s in seq if s == "stored" and (last_bf is None or st > last_bf))
            say(f"S4: {len(mine)} invocations of {version[:8]}, counted stored {len(counted)}")
            if len(counted) >= 12:
                why = f"12th counted stored at {hms(counted[11])}"
                break
            if time.time() > cap:
                why = "CAP 100 MIN"
                break
    else:
        n = int(how)
        last = first + 60 * (n - 1)
        say(f"phase {label}: {n} firings, {hms(first)} .. {hms(last)}; park at {hms(last + 20)}")
        if last + 20 > t_dep + 360:
            sleep_until(t_dep + 360)
            inv = invocations(t_dep - 5, time.time())
            if not any(st <= t_dep + 300 and d["ver"] == version for st, d in inv.items()):
                # S2's invocations are killed by design; if the Logs keep no record of those, R3-6's instrument still
                # counts them. Only this phase's version runs between its deploy and the deploy + 300 s.
                g = graphql_count(t_dep, t_dep + 300)
                say(f"no Logs record of {version[:8]} within 300 s; R3-6's instrument counts {g} in that span")
                if g == 0:
                    why = "NO FIRST INVOCATION IN 5 MIN"
        if why is None:
            sleep_until(last + 20)
            why = f"{n} firings by the clock"
except BaseException as err:
    why = f"DRIVER ERROR {type(err).__name__}: {err}"
finally:
    say(f"end of phase {label}: {why}")
    sleep_until(slot_after(time.time(), offs))
    park, t_park = deploy("idle", f"{label}-park")
say(f"phase {label}: version {version} deployed {hms(t_dep)}, parked {park} at {hms(t_park)}; read with "
    f"s_reads.py <phase> {datetime.fromtimestamp(t_dep, timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')} <after the park> {version}")
