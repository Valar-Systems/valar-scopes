#!/usr/bin/env python3
"""/enrich's KV reads: docs/proxy-bill.md step 4b2 (KE1-KE5, the anchors, the escape).

Read-only, analytics only: no wrangler, no Workers Logs, no KV key list. The analytics-read token comes from the token
file and is never printed. Counts and the fit only: no key, value, device id or hex is printed or written.

  python -I tools/bill/enrich_reads_read.py read <out.json>
  python -I tools/bill/enrich_reads_read.py grade <read1> <read2> <step 4c read A json>

The fit, over the registered background hours: R(h) = alpha x D(h) + beta x N(h) + gamma, plain least squares on three
terms, solved here with no fitting library.

Exit: read 0 done; grade 0 graded (whatever the grades), 3 ESCAPE (stop and report, no reading), 4 REFUSED (the two reads
are stamped under 60 s apart); 2 the rig. An error prints its exception's TYPE only, never its message.
"""
import importlib.util as _ilu
import json
import math
import os
import sys
import urllib.request
from datetime import datetime, timedelta, timezone

ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"          # proxy/wrangler.toml (public)
PROD_NS = "733bf90056104f56be9492e62450a0bf"          # production ENRICH_KV (public)
DATASET = "blipscope_proxy"

# Frozen by the registration (docs/proxy-bill.md step 4b2). Nothing below is chosen after a read.
WINDOW = ("2026-11-05T00:00:00Z", "2026-11-18T00:00:00Z")
BACKGROUND = ["2026-11-05", "2026-11-06", "2026-11-07", "2026-11-08", "2026-11-10", "2026-11-11", "2026-11-12",
              "2026-11-15", "2026-11-17"]
PHOTO_DRIFT_HOURS = {"2026-11-05T14", "2026-11-06T14", "2026-11-07T12", "2026-11-08T13", "2026-11-10T14",
                     "2026-11-11T14", "2026-11-12T15", "2026-11-15T13", "2026-11-17T15"}   # GitHub's run list
KEPT_HOURS_EXPECTED = 207
NONROUTE = ("err5xx", "enrich_gap", "ota", "boot", "usage", "provision", "sitefeed")   # step 4c, amendment 4c-1
ENRICH = ("/api/v1/blipscope/enrich", "/v1/enrich")
AE_ANCHOR_WINDOW, AE_ANCHOR_MISS = ("2026-11-06T00:00:00Z", "2026-11-13T00:00:00Z"), 7239
KE1, KE2, KE3, KE4, KE5 = (5.5, 9.5), (0.95, 1.5), (-500.0, 2000.0), 0.5, 0.85
KE5_WITHIN = 0.70        # amendment 4b2-1, (B)'s within-day R^2
CORR_MAX = 0.9
# Amendment 4b2-1: (A) fits on D' = D less N's own rows (index.ts:361/366/395/429/464 put every N row inside D);
# (B), the within-day fit, runs ONLY if (A)'s gate fires; if (B)'s gate fires too, 4b2 stops and (C) is the input.
MIN_APART_S = 60

_tfs = _ilu.spec_from_file_location("token_file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "token_file.py"))
TOKEN_FILE = _ilu.module_from_spec(_tfs); _tfs.loader.exec_module(TOKEN_FILE)   # docs/token-file-python.md


class Rig(Exception):
    pass


def post(url, body, tok, raw=False):
    data = body.encode() if raw else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Authorization": "Bearer " + tok,
                                          "Content-Type": "text/plain" if raw else "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(r.read())


def ae(sql, tok):
    out = post("https://api.cloudflare.com/client/v4/accounts/%s/analytics_engine/sql" % ACCOUNT, sql, tok, raw=True)
    if "data" not in out:
        raise Rig("an Analytics Engine query returned no data field")
    return out["data"]


def sqlt(t):
    return "toDateTime('%s')" % t.replace("T", " ").rstrip("Z")


def inlist(xs):
    return "('" + "','".join(xs) + "')"


def hourkey(s):
    """'2026-11-05T14:00:00Z' (GraphQL) or '2026-11-05 14:00:00' (AE) -> '2026-11-05T14'."""
    s = str(s)
    return s[:10] + "T" + s[11:13]


def read(path):
    tok = TOKEN_FILE.token("analytics-read")
    a, b = WINDOW
    if datetime.fromisoformat(b.replace("Z", "+00:00")) > datetime.now(timezone.utc):
        raise Rig("the window has not ended")
    out = {"window": [a, b], "read_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    q = ("query($acct: String!, $a: Time!, $b: Time!, $ns: String!) { viewer { accounts(filter: {accountTag: $acct}) { "
         "kvOperationsAdaptiveGroups(limit: 5000, filter: {datetime_geq: $a, datetime_lt: $b, namespaceId: $ns}) "
         "{ sum { requests } dimensions { datetimeHour actionType } } } } }")
    d = post("https://api.cloudflare.com/client/v4/graphql",
             {"query": q, "variables": {"acct": ACCOUNT, "a": a, "b": b, "ns": PROD_NS}}, tok)
    if d.get("errors"):
        raise Rig("GraphQL returned %d error(s)" % len(d["errors"]))
    groups = d["data"]["viewer"]["accounts"][0]["kvOperationsAdaptiveGroups"]
    if len(groups) >= 5000:
        raise Rig("the KV query hit its 5000-group limit: a truncated read is refused")
    R = {}
    for g in groups:
        if str(g["dimensions"]["actionType"]) == "read":
            h = hourkey(g["dimensions"]["datetimeHour"])
            R[h] = R.get(h, 0) + g["sum"]["requests"]
    out["R"] = dict(sorted(R.items()))
    w = "timestamp >= %s AND timestamp < %s" % (sqlt(a), sqlt(b))
    rows = ae("SELECT toStartOfHour(timestamp) AS h, SUM(_sample_interval * double4) AS n FROM %s WHERE %s AND index1 "
              "NOT IN %s AND blob5 != '' AND blob6 != '' GROUP BY h FORMAT JSON" % (DATASET, w, inlist(NONROUTE)), tok)
    out["D"] = {hourkey(r["h"]): float(r["n"]) for r in rows}
    rows = ae("SELECT toStartOfHour(timestamp) AS h, SUM(_sample_interval * double4) AS n FROM %s WHERE %s AND index1 "
              "NOT IN %s AND blob5 != '' AND blob6 != '' AND NOT (index1 IN %s AND blob2 IN ('HIT','MISS')) GROUP BY h "
              "FORMAT JSON" % (DATASET, w, inlist(NONROUTE), inlist(ENRICH)), tok)
    out["Dp"] = {hourkey(r["h"]): float(r["n"]) for r in rows}
    nf = ae("SELECT SUM(_sample_interval * double4) AS n FROM %s WHERE %s AND index1 IN %s AND blob2 IN ('HIT','MISS') "
            "AND blob6 = '' FORMAT JSON" % (DATASET, w, inlist(ENRICH)), tok)
    out["N_no_fw"] = float(nf[0]["n"]) if nf and nf[0].get("n") is not None else 0.0
    rows = ae("SELECT toStartOfHour(timestamp) AS h, blob2 AS c, SUM(_sample_interval * double4) AS n FROM %s WHERE %s "
              "AND index1 IN %s AND blob2 IN ('HIT','MISS') GROUP BY h, c FORMAT JSON" % (DATASET, w, inlist(ENRICH)), tok)
    out["HIT"], out["MISS"] = {}, {}
    for r in rows:
        out[str(r["c"])][hourkey(r["h"])] = out[str(r["c"])].get(hourkey(r["h"]), 0.0) + float(r["n"])
    w2 = "timestamp >= %s AND timestamp < %s" % (sqlt(AE_ANCHOR_WINDOW[0]), sqlt(AE_ANCHOR_WINDOW[1]))
    m = ae("SELECT SUM(_sample_interval * double4) AS n FROM %s WHERE %s AND index1 IN %s AND blob2 = 'MISS' FORMAT JSON"
           % (DATASET, w2, inlist(ENRICH)), tok)
    out["anchor_miss"] = float(m[0]["n"]) if m else 0.0
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print("read %s: %d KV groups; hours with reads %d, with D %d, with HIT %d, with MISS %d" % (
        out["read_at"], len(groups), len(out["R"]), len(out["D"]), len(out["HIT"]), len(out["MISS"])))
    return 0


def solve(A, y):
    """Gaussian elimination with partial pivoting on a k x k system."""
    k = len(y)
    M = [row[:] + [v] for row, v in zip(A, y)]
    for c in range(k):
        p = max(range(c, k), key=lambda r: abs(M[r][c]))
        if abs(M[p][c]) < 1e-12:
            raise Rig("the normal equations are singular")
        M[c], M[p] = M[p], M[c]
        for r in range(k):
            if r != c:
                f = M[r][c] / M[c][c]
                M[r] = [x - f * z for x, z in zip(M[r], M[c])]
    return [M[i][k] / M[i][i] for i in range(k)]


def lsq(cols, y):
    """Least squares on the given columns: (coefficients, standard errors, residual sum of squares)."""
    k, n = len(cols), len(y)
    XtX = [[sum(a * b for a, b in zip(cols[i], cols[j])) for j in range(k)] for i in range(k)]
    Xty = [sum(a * b for a, b in zip(cols[i], y)) for i in range(k)]
    coef = solve(XtX, Xty)
    fit = [sum(coef[i] * cols[i][t] for i in range(k)) for t in range(n)]
    ssr = sum((a - b) ** 2 for a, b in zip(y, fit))
    inv = [solve(XtX, [1.0 if i == j else 0.0 for i in range(k)]) for j in range(k)]
    se = [math.sqrt(ssr / (n - k) * inv[i][i]) for i in range(k)]
    return coef, se, ssr, fit


def stamp(s):
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


def grade(p1, p2, p4c):
    d, d2, a4 = (json.load(open(p, encoding="utf-8")) for p in (p1, p2, p4c))
    apart = abs((stamp(d2["read_at"]) - stamp(d["read_at"])).total_seconds())
    print("reads %s and %s, %d s apart" % (d["read_at"], d2["read_at"], apart))
    if apart < MIN_APART_S:
        print("REFUSED: the two reads are under %d s apart; take another" % MIN_APART_S)
        return 4
    escape = []
    strip = lambda x: {k: v for k, v in x.items() if k != "read_at"}
    if strip(d) != strip(d2):
        escape.append("the two reads differ in %s" % [k for k in sorted(strip(d)) if strip(d).get(k) != strip(d2).get(k)])
    # Anchor: the hours summed per day against step 4c's daily production reads.
    off = []
    for day, cell in sorted(a4["kv"].items()):
        want = cell.get("read", {}).get("prod", 0)
        got = sum(v for h, v in d["R"].items() if h.startswith(day))
        if not want or abs(got - want) > 0.005 * want:
            off.append(day)
    print("anchor KV: hourly reads summed per day against step 4c's daily, %d of %d days within 0.5%%" % (
        len(a4["kv"]) - len(off), len(a4["kv"])))
    if off:
        escape.append("KV anchor off on %s" % off)
    ok_m = abs(d["anchor_miss"] - AE_ANCHOR_MISS) <= 0.01 * AE_ANCHOR_MISS
    print("anchor AE /enrich MISS [11-06, 11-13): %s against %s -> %s" % (
        f"{d['anchor_miss']:,.0f}", f"{AE_ANCHOR_MISS:,}", "ok" if ok_m else "OFF"))
    if not ok_m:
        escape.append("AE anchor MISS")
    kept = []
    for day in BACKGROUND:
        for hh in range(24):
            h = "%sT%02d" % (day, hh)
            if h not in PHOTO_DRIFT_HOURS:
                kept.append(h)
    if len(kept) != KEPT_HOURS_EXPECTED:
        escape.append("%d kept hours, not %d" % (len(kept), KEPT_HOURS_EXPECTED))
    R = [float(d["R"].get(h, 0)) for h in kept]
    Dp = [float(d["Dp"].get(h, 0.0)) for h in kept]
    N = [float(d["HIT"].get(h, 0.0)) + float(d["MISS"].get(h, 0.0)) for h in kept]
    empty = sum(1 for r, x in zip(R, Dp) if r <= 0 or x <= 0)
    if empty:
        escape.append("%d kept hour(s) with no KV reads or no device requests" % empty)
    if escape:
        print("ESCAPE: " + "; ".join(escape))
        print("stop and report, with no reading")
        return 3
    n = len(kept)
    mean = lambda xs: sum(xs) / len(xs)
    def corr(x, y):
        mx, my = mean(x), mean(y)
        sxy = sum((a - mx) * (b - my) for a, b in zip(x, y))
        sxx = sum((a - mx) ** 2 for a in x)
        syy = sum((b - my) ** 2 for b in y)
        return sxy / math.sqrt(sxx * syy) if sxx > 0 and syy > 0 else float("nan")
    print("information: N's rows with an empty blob6 (no fw: read): %s" % f"{d['N_no_fw']:,.0f}")
    g = lambda ok: "HOLDS" if ok else "FAILS"
    hits = sum(float(d["HIT"].get(h, 0.0)) for h in kept)
    miss = sum(float(d["MISS"].get(h, 0.0)) for h in kept)
    m = miss / (hits + miss)
    c = corr(Dp, N)
    print("(A) separability: corr(D', N) over %d hours = %.3f (gate %.1f)" % (n, c, CORR_MAX))
    if c <= CORR_MAX:
        (alpha, betap, gamma), se, ssr, fit = lsq([Dp, N, [1.0] * n], R)
        r2 = 1 - ssr / sum((yv - mean(R)) ** 2 for yv in R)
        branch, ke3_ok, ke5 = "(A)", KE3[0] <= gamma <= KE3[1], (r2, KE5)
        print("(A) fit: alpha %.3f (se %.3f), beta' %.3f (se %.3f), gamma %.1f (se %.1f), R^2 %.4f" % (
            alpha, se[0], betap, se[1], gamma, se[2], r2))
        ke3_text = "gamma %.1f in %s" % (gamma, KE3)
    else:
        # (B), the registered fallback: within-day centring, no intercept, one gamma per day.
        print("(A)'s gate fired: running (B), the registered fallback")
        days = sorted({h[:10] for h in kept})
        idx = {dd: [i for i, h in enumerate(kept) if h.startswith(dd)] for dd in days}
        def centred(xs):
            out = list(xs)
            for dd, ii in idx.items():
                mu = sum(xs[i] for i in ii) / len(ii)
                for i in ii:
                    out[i] = xs[i] - mu
            return out
        Rc, Dc, Nc = centred(R), centred(Dp), centred(N)
        cw = corr(Dc, Nc)
        print("(B) separability: corr of centred D' and N = %.3f (gate %.1f)" % (cw, CORR_MAX))
        if not (cw <= CORR_MAX):
            print("ESCAPE: (B)'s gate fired too. 4b2 stops; (C), step 2b's whole-window 7.4 with the family table, is the "
                  "design input; the owner is told")
            return 3
        (alpha, betap), se, ssr, fit = lsq([Dc, Nc], Rc)
        r2 = 1 - ssr / sum(v ** 2 for v in Rc)
        gam = {dd: mean([R[i] for i in ii]) - alpha * mean([Dp[i] for i in ii]) - betap * mean([N[i] for i in ii])
               for dd, ii in idx.items()}
        branch, ke3_ok, ke5 = "(B)", all(KE3[0] <= v <= KE3[1] for v in gam.values()), (r2, KE5_WITHIN)
        print("(B) fit: alpha %.3f (se %.3f), beta' %.3f (se %.3f), within-day R^2 %.4f; gamma by day %s" % (
            alpha, se[0], betap, se[1], r2, {k: round(v, 1) for k, v in gam.items()}))
        gamma = mean(list(gam.values()))
        ke3_text = "every day's gamma in %s" % (KE3,)
    beta = betap - alpha
    s_low = (beta - 2 - 2 * m) / beta
    print("totals over the kept hours: R %s, D' %s, N %s (MISS share m = %.4f)" % (
        f"{sum(R):,.0f}", f"{sum(Dp):,.0f}", f"{sum(N):,.0f}", m))
    print("information: /enrich requests (beta' x N) are %.1f%% of production's reads over the kept hours, the chain "
          "alone (beta x N) %.1f%%; other device requests (alpha x D') %.1f%%; gamma %.1f%%" % (
              100 * betap * sum(N) / sum(R), 100 * beta * sum(N) / sum(R), 100 * alpha * sum(Dp) / sum(R),
              100 * gamma * n / sum(R)))
    print("\nbranch %s" % branch)
    print("KE1: beta = beta' - alpha = %.3f in %s -> %s" % (beta, KE1, g(KE1[0] <= beta <= KE1[1])))
    print("KE2: alpha %.3f in %s -> %s" % (alpha, KE2, g(KE2[0] <= alpha <= KE2[1])))
    print("KE3: %s -> %s" % (ke3_text, g(ke3_ok)))
    print("KE4: S_low = (beta - 2 - 2m) / beta = %.3f, at least %.1f -> %s" % (s_low, KE4, g(s_low >= KE4)))
    print("KE5: R^2 %.4f, at least %.2f -> %s" % (ke5[0], ke5[1], g(ke5[0] >= ke5[1])))
    return 0


def main(argv):
    try:
        if len(argv) == 2 and argv[0] == "read":
            return read(argv[1])
        if len(argv) == 4 and argv[0] == "grade":
            return grade(argv[1], argv[2], argv[3])
        print(__doc__)
        return 2
    except Rig as e:
        print("RIG: %s" % e)
        return 2
    except Exception as e:   # the type only: a message can carry a URL or a response body
        print("RIG: %s" % type(e).__name__)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
