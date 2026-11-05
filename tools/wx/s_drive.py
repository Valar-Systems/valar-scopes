"""Plan S's driver (docs/wx-opuk.md, "How each phase is ended"): deploy one phase of blipscope-wx-ingest-dev, count
its invocations live through `wrangler tail`, and park on `off` at the phase's count. A trigger, not a reading: the
graded reading is s_reads.py. Deploys only from the clean checkout at the merged commit.

  python -I s_drive.py <env> <label> <count> <max_minutes> [s4]
    count  invocations of the phase's version (S1 3, S2 10, S3 60); with s4: counted `stored` opuk checks
           (after the last `backfilled`), 12
    prints each finished invocation as tail reports it, the deploys' times and version ids, and writes
    plan-s-<label>.tail.json beside the deploy logs
"""
import json
import os
import re
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone

SCRATCH = r"C:\Users\DanielFrenkel\AppData\Local\Temp\claude\c--Github-Blipscope\fb8462e7-ee3a-4757-8b09-2d0165eb9254\scratchpad"
PROXY = os.path.join(SCRATCH, "wt-plan-s", "proxy")
WRANGLER = os.path.join(PROXY, "node_modules", "wrangler", "bin", "wrangler.js")
WANT = "131c0f776d9a40121304041364af0cc11f9922dd"
CFG = "wx-ingest-dev.wrangler.toml"
now = lambda: datetime.now(timezone.utc).strftime("%H:%M:%S.%f")[:-3] + "Z"


def deploy(env, label):
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=PROXY, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=PROXY, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout.strip()
    if head != WANT or dirty or os.environ.get("CLOUDFLARE_API_TOKEN"):
        raise SystemExit(f"REFUSED: head {head[:12]} dirty={bool(dirty)} token={'set' if os.environ.get('CLOUDFLARE_API_TOKEN') else 'unset'}")
    t0 = now()
    r = subprocess.run(["node", WRANGLER, "deploy", "-c", CFG, "-e", env], cwd=PROXY, capture_output=True, text=True, encoding="utf-8", errors="replace")
    t1 = now()
    open(os.path.join(SCRATCH, f"plan-s-{label}.out"), "w", encoding="utf-8").write(r.stdout)
    open(os.path.join(SCRATCH, f"plan-s-{label}.err"), "w", encoding="utf-8").write(r.stderr)
    m = re.search(r"Current Version ID: (\S+)", r.stdout)
    trig = [l.strip() for l in r.stdout.splitlines() if "schedule" in l or "triggers" in l]
    print(f"deploy {label} (-e {env}) {t0} -> {t1} exit {r.returncode} version {m.group(1) if m else None} "
          f"stderr {len(r.stderr)} bytes | {trig}", flush=True)
    if r.returncode != 0 or not m:
        raise SystemExit("deploy failed")
    return m.group(1), t1


env, label, target, max_min = sys.argv[1], sys.argv[2], int(sys.argv[3]), float(sys.argv[4])
s4 = len(sys.argv) > 5 and sys.argv[5] == "s4"
events, lock = [], threading.Lock()
tail_err = open(os.path.join(SCRATCH, f"plan-s-{label}.tail.err"), "w", encoding="utf-8")
tail = subprocess.Popen(["node", WRANGLER, "tail", "blipscope-wx-ingest-dev", "-c", CFG, "--format", "json"], cwd=PROXY,
                        stdout=subprocess.PIPE, stderr=tail_err, text=True, encoding="utf-8", errors="replace")


def reader():
    buf, dec = "", json.JSONDecoder()
    for chunk in iter(lambda: tail.stdout.read(1), ""):
        buf += chunk
        while True:
            i = buf.find("{")
            if i < 0:
                buf = ""
                break
            try:
                obj, j = dec.raw_decode(buf[i:])
            except ValueError:
                break
            buf = buf[i + j:]
            with lock:
                events.append((time.time(), obj))


import atexit
atexit.register(lambda: tail.poll() is None and tail.kill())   # the tail never outlives the driver
threading.Thread(target=reader, daemon=True).start()
time.sleep(12)
if tail.poll() is not None:
    raise SystemExit("tail exited before the deploy: see the .tail.err file")
try:
    version, t_dep = deploy(env, label)
except BaseException:
    tail.terminate()
    raise
t_dep_epoch = time.time()
parked = False


def lines(ev):
    out = []
    for lg in ev.get("logs") or []:
        for msg in lg.get("message") or []:
            try:
                out.append(json.loads(msg) if isinstance(msg, str) else msg)
            except ValueError:
                pass
    return out


def tally():
    with lock:
        evs = [(t, e) for t, e in events if (e.get("event") or {}).get("cron") is not None]
    # by version id; an event without one counts if it arrived after this phase's deploy (printed as such)
    mine = [e for t, e in evs if (e.get("scriptVersion") or {}).get("id") == version
            or (not (e.get("scriptVersion") or {}).get("id") and t > t_dep_epoch)]
    seq = [(l.get("status")) for e in mine for l in lines(e) if l.get("product") == "opuk" and l.get("status") in ("stored", "backfilled")]
    last_bf = max((i for i, s in enumerate(seq) if s == "backfilled"), default=-1)
    counted = sum(1 for i, s in enumerate(seq) if s == "stored" and i > last_bf)
    return mine, counted


seen, deadline, why = 0, time.time() + max_min * 60, "TIMEOUT"
try:
  while time.time() < deadline:
      if tail.poll() is not None:
          why = "TAIL EXITED"
          break
      mine, counted = tally()
      for e in mine[seen:]:
          summ = [(l.get("product") or l.get("evt"), l.get("check"), l.get("status") or l.get("result") or l.get("mode")) for l in lines(e)]
          print(f"  {now()} invocation {len(mine[:seen]) + 1}: outcome {e.get('outcome')} {summ}", flush=True)
          seen += 1
      if (counted if s4 else len(mine)) >= target:
          why = "COUNT REACHED"
          break
      time.sleep(1)
  print(f"end of phase {label}: {why} at {now()}; invocations of {version[:8]}: {len(tally()[0])}; counted stored: {tally()[1]}", flush=True)
except BaseException as err:
    why = f"DRIVER ERROR {type(err).__name__}: {err}"
    print(why, flush=True)
finally:
    park, t_park = deploy("off", f"{label}-park")
    parked = True
time.sleep(90)
tail.terminate()
try:
    tail.wait(timeout=20)
except subprocess.TimeoutExpired:
    tail.kill()
with lock:
    evs = [e for _, e in events]
byver = {}
for e in evs:
    if (e.get("event") or {}).get("cron") is None:
        continue
    v = (e.get("scriptVersion") or {}).get("id") or "none"
    byver.setdefault(v[:8], []).append(e.get("outcome"))
print(f"tail closed {now()} (exit {tail.returncode}); scheduled events by version: "
      + json.dumps({k: {"n": len(v), "outcomes": sorted(set(v))} for k, v in byver.items()}), flush=True)
json.dump({"phase_version": version, "deployed": t_dep, "park_version": park, "parked": t_park, "why": why,
           "events": evs}, open(os.path.join(SCRATCH, f"plan-s-{label}.tail.json"), "w", encoding="utf-8"))
