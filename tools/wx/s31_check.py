"""S3-1 (docs/wx-opera-stage3.md): judge a product's version record and deployment against the frozen prediction.
  python s31_check.py <versions view json> <deployments status json> <product> <version id> <cron ok|MISSING> <view rc> <status rc>
Exit 0 as predicted, 1 off."""
import json
import sys

vf, df, p, vid, cron, v, d = sys.argv[1:]
fails = []
if v != "0" or d != "0":
    fails.append(f"wrangler exit view={v} status={d}")
try:
    V = json.load(open(vf))
    D = json.load(open(df))
except Exception as e:  # noqa: BLE001 -- unreadable is off, never a pass
    print("S3-1 OFF: unreadable", type(e).__name__)
    sys.exit(1)
b = sorted((x.get("name"), x.get("type"), x.get("text") or x.get("bucket_name")) for x in V["resources"]["bindings"])
want = sorted([("PRODUCT", "plain_text", p), ("WX", "r2_bucket", "blipscope-wx"), ("WX_INGEST", "plain_text", "on")])
if b != want:
    fails.append(f"bindings {b}")
cpu = V["resources"].get("script_runtime", {}).get("limits", {}).get("cpu_ms")
if cpu != 12000:
    fails.append(f"cpu_ms {cpu}")
vers = [(x["version_id"], x["percentage"]) for x in D["versions"]]
if vers != [(vid, 100)]:
    fails.append(f"deployment {vers}")
if cron != "ok":
    fails.append("cron line missing")
print(f"S3-1 {p}: version {vid[:8]} {vers[0][1] if vers else None} %; bindings {[x[0] + '=' + str(x[2]) for x in b]}; "
      f"cpu_ms {cpu}; cron {cron}", "-> as predicted" if not fails else f"-> OFF: {fails}")
sys.exit(1 if fails else 0)
