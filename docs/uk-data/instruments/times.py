"""M1/M2/O1/O2/S-M/S-O and C1 of docs/uk-data.md: cadence, latency and the showable share, from listings.

V (validity) comes from each key; P (publish) is LastModified. Every listing pages to the end.
Prints a JSON summary. No coordinate is read or printed.
Usage: python -I times.py <out.json>
"""
import json
import math
import re
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone

MO = "https://met-office-radar-obs-data.s3.eu-west-2.amazonaws.com"
OP = "https://s3.waw3-1.cloudferro.com/openradar-24h"
UA = {"User-Agent": "uk-data-plan-measurement/1"}


def get(url, headers=None):
    req = urllib.request.Request(url, headers={**UA, **(headers or {})})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.status, r.read()


def list_all(base, prefix):
    keys, token, pages = [], None, 0
    while True:
        url = f"{base}/?list-type=2&prefix={prefix}" + (f"&continuation-token={urllib.request.quote(token)}" if token else "")
        _, body = get(url)
        pages += 1
        x = body.decode()
        for m in re.finditer(r"<Contents>(.*?)</Contents>", x, re.S):
            k = re.search(r"<Key>([^<]+)</Key>", m.group(1)).group(1)
            lm = re.search(r"<LastModified>([^<]+)</LastModified>", m.group(1)).group(1)
            sz = int(re.search(r"<Size>(\d+)</Size>", m.group(1)).group(1))
            keys.append((k, lm, sz))
        trunc = re.search(r"<IsTruncated>(true|false)</IsTruncated>", x).group(1) == "true"
        if not trunc:
            return keys, pages
        token = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", x).group(1)


def iso(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()


def v_mo(key, sabotage=False):
    m = re.search(r"/(\d{8})(\d{4})_ODIM_ng_radar_rainrate_composite_1km_UK\.h5$", key)
    if not m:
        return None
    d, hm = m.groups()
    if sabotage:
        hm = hm[2:] + hm[:2]
    try:
        return datetime.strptime(d + hm, "%Y%m%d%H%M").replace(tzinfo=timezone.utc).timestamp()
    except ValueError:
        return math.nan


def v_op(key, q):
    m = re.search(rf"OPERA@(\d{{8}})T(\d{{4}})@0@{q}\.h5$", key)
    if not m:
        return None
    return datetime.strptime("".join(m.groups()), "%Y%m%d%H%M").replace(tzinfo=timezone.utc).timestamp()


def nearest_rank(xs, p):
    s = sorted(xs)
    return s[max(0, math.ceil(p * len(s)) - 1)]


def cadence(vs, expect):
    steps = [b - a for a, b in zip(vs, vs[1:])]
    hist = {}
    for s in steps:
        hist[int(s)] = hist.get(int(s), 0) + 1
    exact = sum(1 for s in steps if s == expect)
    return {"steps": len(steps), "exact": exact, "share_exact": round(exact / len(steps), 4) if steps else None,
            "hist": dict(sorted(hist.items()))}


def latency(frames):
    ls = [p - v for v, p in frames]
    return {"n": len(ls), "min": min(ls), "p50": nearest_rank(ls, 0.5), "p95": nearest_rank(ls, 0.95),
            "max": max(ls), "over_1h": sum(1 for x in ls if x > 3600)}


def showable(frames, limit, lag=0.0, step=5.0):
    """Share of [first P, last P] in which the newest frame published by t - lag is <= limit s old at t."""
    fr = sorted((p + lag, v) for v, p in frames)
    t0, t1 = fr[0][0], fr[-1][0]
    n = ok = 0
    i, newest_v = 0, -math.inf
    t = t0
    while t <= t1:
        while i < len(fr) and fr[i][0] <= t:
            newest_v = max(newest_v, fr[i][1])
            i += 1
        n += 1
        if t - newest_v <= limit:
            ok += 1
        t += step
    return round(ok / n, 4)


def control_c1(base, key, fake):
    st, body = get(f"{base}/{key}", {"Range": "bytes=0-7"})
    sig_ok = body[:8] == bytes([0x89, 0x48, 0x44, 0x46, 0x0D, 0x0A, 0x1A, 0x0A])
    try:
        st2, _ = get(f"{base}/{fake}")
        fake_refused = False
    except urllib.error.HTTPError as e:
        st2, fake_refused = e.code, True
    return {"newest_status": st, "hdf5_signature": sig_ok, "fake_status": st2, "fake_refused": fake_refused}


out = {"at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}

# ---- Met Office: the 7 complete days, plus today's partial day for C1 only
mo_frames, mo_pages, mo_other = [], 0, []
for dd in range(21, 28):
    keys, pages = list_all(MO, f"radar/2026/10/{dd:02d}/")
    mo_pages += pages
    for k, lm, sz in keys:
        v = v_mo(k)
        if v is None:
            mo_other.append(k)
        else:
            mo_frames.append((v, iso(lm), k))
mo_frames.sort()
today, _ = list_all(MO, "radar/2026/10/28/")
newest_mo = sorted(k for k, _, _ in today if v_mo(k) is not None)[-1]
vs = [v for v, _, _ in mo_frames]
fr = [(v, p) for v, p, _ in mo_frames]
out["met_office"] = {
    "frames": len(mo_frames), "pages": mo_pages, "non_frame_keys": mo_other[:5],
    "per_day": {f"10-{dd}": sum(1 for v in vs if datetime.fromtimestamp(v, timezone.utc).day == dd) for dd in range(21, 28)},
    "M1_cadence": cadence(vs, 900),
    "M1_sabotage_drop_every_3rd": cadence([v for i, v in enumerate(vs) if i % 3 != 2], 900)["share_exact"],
    "M2_latency_s": latency(fr),
    "S_M_showable_600": showable(fr, 600),
    "S_M_showable_600_lag60_printed": showable(fr, 600, 60),
    "S_M_sabotage_limit_3600": showable(fr, 3600),
    "C1": control_c1(MO, newest_mo, newest_mo.replace(".h5", "_nosuch.h5")),
    "C2_parser_sabotage_unparseable_or_moved": sum(1 for _, _, k in mo_frames[:96] if not (v_mo(k, True) == v_mo(k))),
}

# ---- OPERA: the whole 24 h cache, per quantity
op_keys = []
op_pages = 0
for d in ("2026/10/27", "2026/10/28"):
    keys, pages = list_all(OP, f"{d}/OPERA/COMP/")
    op_keys += keys
    op_pages += pages
out["opera"] = {"keys": len(op_keys), "pages": op_pages,
                "suffixes": sorted({re.sub(r".*@", "", k) for k, _, _ in op_keys})}
for q, expect in (("DBZH", 300), ("RATE", 900), ("ACRR", 3600)):
    fq = sorted((v_op(k, q), iso(lm), k) for k, lm, _ in op_keys if v_op(k, q) is not None)
    if not fq:
        out["opera"][q] = "none"
        continue
    vs = [v for v, _, _ in fq]
    frq = [(v, p) for v, p, _ in fq]
    out["opera"][q] = {
        "frames": len(fq),
        "first_v": datetime.fromtimestamp(vs[0], timezone.utc).strftime("%m-%dT%H:%MZ"),
        "last_v": datetime.fromtimestamp(vs[-1], timezone.utc).strftime("%m-%dT%H:%MZ"),
        "O1_cadence": cadence(vs, expect),
        "O1_sabotage_drop_every_3rd": cadence([v for i, v in enumerate(vs) if i % 3 != 2], expect)["share_exact"],
        "O2_latency_s": latency(frq),
        "S_O_showable_600": showable(frq, 600),
        "S_O_showable_600_lag60_printed": showable(frq, 600, 60),
        "S_sabotage_limit_3600": showable(frq, 3600),
    }
    if q == "DBZH":
        out["opera"]["C1"] = control_c1(OP, fq[-1][2], fq[-1][2].replace(".h5", "_nosuch.h5"))

json.dump(out, open(sys.argv[1], "w"), indent=1)
print(json.dumps(out, indent=1))
