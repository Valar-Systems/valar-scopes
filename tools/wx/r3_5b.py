"""R3-5b' (docs/wx-opuk.md, as ruled ~23:00Z 2026-11-04): one listing of wx/<product>/frames/ judged against the
last pruning check's START time from Workers Logs. Read-only; tokens from the token file, never printed; prints
counts, epochs and times only (never a key's value beyond its epoch).

  1. list wx/<product>/frames/ (S3 API, SigV4 by hand), then GET wx/<product>/latest for the frame it names
  2. wait (default 150 s) so the logs land, then read blipscope-wx-ingest's events around the listing
  3. t_last = (log timestamp - ms) of the last pruning check whose line was written no later than the listing;
     cross-checked against epoch + lagS - ms/1000 where the line carries them
  4. PASS iff frames <= 13 (opuk only) and every epoch >= t_last - 3,600 s, except the one latest names
     SABOTAGE: the same listing judged with t_last + 300 s must FAIL, on the oldest frame
  BLIND (= FAIL) if the listing is empty, latest is unreadable, no pruning check is found, no log line was written
  after the listing (the read cannot rule out a pruning line still landing), or the Logs read was sampled (any page
  at an ABR level other than 1; the environment variable R3P_FORCE_ABR forces one, as the sabotage).

Pruning checks: opuk = status stored/backfilled (the ruling's "storing check"; since stage 1a its line carries
`pruneKeys`, the keys a prune by key sent, instead of `pruned`); cref = any check logging `pruned`
(cref prunes on every successful check). Usage: python -I r3_5b.py <opuk|cref> [wait_s] [service, default blipscope-wx-ingest]
"""
import datetime
import hashlib
import hmac
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"
BUCKET = "blipscope-wx"
HOST = f"{ACCOUNT}.r2.cloudflarestorage.com"
RETAIN_S = 3600
MAX_FRAMES = {"opuk": 13, "cref": None}   # R3-5b's cap is opuk's; cref keeps ~30 frames an hour and has none


def tf():
    d = {}
    for line in open(os.path.expanduser("~/.config/blipscope/tokens"), encoding="utf-8"):
        k, _, v = line.strip().partition("=")
        if k and v:
            d[k] = v
    return d


def s3_get(akid, secret, path, q=None):
    q = q or {}
    qs = "&".join(f"{urllib.parse.quote(k, safe='-_.~')}={urllib.parse.quote(v, safe='-_.~')}" for k, v in sorted(q.items()))
    uri = "/" + BUCKET + ("/" + urllib.parse.quote(path, safe="/-_.~") if path else "")
    now = datetime.datetime.now(datetime.timezone.utc)
    amz, day = now.strftime("%Y%m%dT%H%M%SZ"), now.strftime("%Y%m%d")
    ph = hashlib.sha256(b"").hexdigest()
    canon = f"GET\n{uri}\n{qs}\nhost:{HOST}\nx-amz-content-sha256:{ph}\nx-amz-date:{amz}\n\nhost;x-amz-content-sha256;x-amz-date\n{ph}"
    scope = f"{day}/auto/s3/aws4_request"
    sts = f"AWS4-HMAC-SHA256\n{amz}\n{scope}\n{hashlib.sha256(canon.encode()).hexdigest()}"
    sign = lambda key, msg: hmac.new(key, msg.encode(), hashlib.sha256).digest()
    k = sign(sign(sign(sign(("AWS4" + secret).encode(), day), "auto"), "s3"), "aws4_request")
    sig = hmac.new(k, sts.encode(), hashlib.sha256).hexdigest()
    auth = f"AWS4-HMAC-SHA256 Credential={akid}/{scope}, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature={sig}"
    req = urllib.request.Request(f"https://{HOST}{uri}" + (f"?{qs}" if qs else ""),
                                 headers={"x-amz-date": amz, "x-amz-content-sha256": ph, "Authorization": auth})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read().decode()


def logs(tok, a_ms, b_ms):
    """Every page's ABR level is kept: one sampled page makes the whole read sampled."""
    evs, off, levels = [], None, set()
    while True:
        body = {"queryId": "uk-r3-5b", "timeframe": {"from": a_ms, "to": b_ms}, "view": "events", "limit": 2000,
                "parameters": {"filters": [{"key": "$metadata.service", "operation": "eq", "type": "string",
                                            "value": SERVICE}]}}
        if off:
            body["offset"] = off
        req = urllib.request.Request(
            f"https://api.cloudflare.com/client/v4/accounts/{ACCOUNT}/workers/observability/telemetry/query",
            data=json.dumps(body).encode(), method="POST",
            headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=120) as r:
            res = json.loads(r.read())["result"]
        page = res["events"]["events"]
        evs += page
        levels.add(res.get("statistics", {}).get("abr_level"))
        if len(page) < 2000:
            return evs, sorted(levels, key=str)
        off = page[-1]["$metadata"]["id"]


hms = lambda s: datetime.datetime.fromtimestamp(s, datetime.timezone.utc).strftime("%H:%M:%SZ")


def judge(epochs, latest_epoch, t_last_s, cap):
    cut = t_last_s - RETAIN_S
    bad = [e for e in epochs if e < cut and e != latest_epoch]
    return {"cut": hms(cut), "frames_below_cut": len(bad), "oldest_below_cut": hms(min(bad)) if bad else None,
            "pass": (cap is None or len(epochs) <= cap) and not bad}


product = sys.argv[1]
wait_s = int(sys.argv[2]) if len(sys.argv) > 2 else 150
# R3' (docs/wx-opuk-split.md): opuk has its own Worker, so its pruning lines are in its own service
SERVICE = sys.argv[3] if len(sys.argv) > 3 else "blipscope-wx-ingest"
assert product in ("opuk", "cref"), product
t = tf()
akid, secret = t["r2-wx-read-id"], t["r2-wx-read-secret"]

# 1. the listing, then latest
t_list = time.time()
prefix = f"wx/{product}/frames/"
keys, token = [], None
while True:
    q = {"list-type": "2", "prefix": prefix}
    if token:
        q["continuation-token"] = token
    x = s3_get(akid, secret, "", q)
    keys += re.findall(r"<Key>([^<]+)</Key>", x)
    m = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", x)
    if "<IsTruncated>true</IsTruncated>" in x and m:
        token = m.group(1)
    else:
        break
epochs = sorted(int(k[len(prefix):-4]) for k in keys if re.fullmatch(re.escape(prefix) + r"\d+\.wxg", k))
try:
    latest_epoch = int(json.loads(s3_get(akid, secret, f"wx/{product}/latest"))["epoch"])
except Exception as err:                    # noqa: BLE001 -- any failure here makes the read BLIND, never a pass
    latest_epoch, latest_err = None, type(err).__name__
else:
    latest_err = None

# 2. wait, then the logs around the listing
time.sleep(wait_s)
evs, abr = logs(t["observability-read"], int((t_list - 1200) * 1000), int(time.time() * 1000))
if os.environ.get("R3P_FORCE_ABR"):           # the sabotage seam (docs/wx-opera-europe.md section 11): a read
    abr = [int(os.environ["R3P_FORCE_ABR"])]   # reported as sampled must come out BLIND
t_list_ms = int(t_list * 1000)
after_listing = sum(1 for e in evs if e["timestamp"] > t_list_ms and (e.get("source") or {}).get("evt") in ("wx_ingest", "wx_ingest_error"))
pruners = []
for e in evs:
    s = e.get("source") or {}
    if s.get("product") != product or s.get("evt") != "wx_ingest" or not isinstance(s.get("ms"), int):
        continue
    is_pruner = s.get("status") in ("stored", "backfilled") if product == "opuk" else "pruned" in s
    if is_pruner and e["timestamp"] <= t_list_ms:
        t0 = (e["timestamp"] - s["ms"]) / 1000
        t0b = (s["epoch"] + s["lagS"] - s["ms"] / 1000) if isinstance(s.get("epoch"), int) and isinstance(s.get("lagS"), int) else None
        pruners.append((t0, t0b, s.get("status"), s.get("epoch"), s.get("pruned"), s.get("pruneKeys")))
pruners.sort()

out = {"product": product, "listed_at": hms(t_list), "frames": len(epochs),
       "oldest": hms(epochs[0]) if epochs else None, "newest": hms(epochs[-1]) if epochs else None,
       "latest": hms(latest_epoch) if latest_epoch else None, "latest_err": latest_err,
       "abr_level": abr, "ingest_lines_after_listing": after_listing}
blind = []
if not epochs:
    blind.append("empty listing")
if latest_epoch is None:
    blind.append("latest unreadable")
if not pruners:
    blind.append("no pruning check before the listing")
if after_listing == 0:
    blind.append("no log line after the listing")
if abr != [1]:
    blind.append(f"sampled Logs read (ABR {abr}): a pruning check may be missing; re-read")
if pruners:
    t0, t0b, st, ep, pr, pk = pruners[-1]
    out["last_pruner"] = {"start": datetime.datetime.fromtimestamp(t0, datetime.timezone.utc).strftime("%H:%M:%S.%fZ")[:-4] + "Z",
                          "start_from_epoch_lagS_diff_s": round(t0b - t0, 1) if t0b is not None else None,
                          "status": st, "frame": hms(ep) if isinstance(ep, int) else None, "pruned": pr,
                          "pruneKeys": pk}
    if t0b is not None and abs(t0b - t0) > 1.5:
        blind.append("the two start times disagree by more than 1.5 s")
    if epochs:
        out["oldest_margin_above_cut_s"] = round(epochs[0] - (t0 - RETAIN_S), 1)
        out["R3_5b"] = judge(epochs, latest_epoch, t0, MAX_FRAMES[product])
        out["sabotage_plus_300"] = judge(epochs, latest_epoch, t0 + 300, MAX_FRAMES[product])
        sab = out["sabotage_plus_300"]
        out["sabotage_failed_on_oldest"] = (not sab["pass"]) and sab["oldest_below_cut"] == hms(epochs[0])
out["blind"] = blind
out["verdict"] = "BLIND" if blind else ("PASS" if out["R3_5b"]["pass"] and out["sabotage_failed_on_oldest"] else "FAIL")
print(json.dumps(out, indent=1))
