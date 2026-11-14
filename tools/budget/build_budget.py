#!/usr/bin/env python3
"""Assemble docs/account-budget.json from its reads (docs/account-budget.md sections 2, 5.1 and 9). Read-only against
Cloudflare: it runs tools/budget/budget_read.py five times, over fixed registered windows, and writes one file.

  python3 -I tools/budget/build_budget.py <kv-days.json> <out.json>

<kv-days.json> is tools/budget/kv_by_day.py's output for the registered window (9.4). Its bands are checked FIRST:
outside one, the builder stops (exit 3) before any read runs or any row is written. An unattributed script, bucket or
namespace in any graded read stops it too (exit 4).

The base (9.8 v): the 2026-11-14 MTD read, scaled to 30 days, is every product's steady rows, with four changes.
- shsr's first day (9.9 b) is split out of it into state "excluded", and X10's measured rows stand in "built":
  CPU and requests by script (budget_read.py's mapping); Class A as blipscope-wx's row less cref's share at X10's
  anchor (181.3 an hour x 720); Class B as shsr's design projection over its hours inside the window.
- KV writes go by event (9.4), from kv-days: background steady, refresh-data Mondays and the (A) refresh one-off.
- The ten's running rows are derived from r-run as before (their scripts as measured; their R2 Class A and B as the
  running hours' blipscope-wx less r-silent's). The ten's own MTD scripts stay opera-ten "silent" (9.10's third named
  difference).
- The levers (9.8 iv) are product states with projected rows: shsr "built-no-list", missileer-live "cut".
The 11-12 reads (r-7d, r-silent) are kept with every row in "excluded", so each still anchors on its own totals; r-24h
stays the observation printed beside the 30-day table.
"""
import json
import os
import statistics
import subprocess
import sys
import tempfile
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
READ = os.path.join(HERE, "budget_read.py")
MONTH = "2026-12"   # 9.1: the month a 30-day scenario counts its one-offs for
END_1112 = "2026-11-12T21:00:00Z"
R7 = ("2026-11-05T21:00:00Z", END_1112)
SILENT = ("2026-11-11T21:00:00Z", END_1112)   # OPERA silent since about 20:10Z on 11-11
RUN = ("2026-11-11T17:00:00Z", "2026-11-11T20:00:00Z")   # the only whole hours the ten all ran
MTD = ("2026-11-01T00:00:00Z", "2026-11-14T06:00:00Z")
KV_DAYS = ("2026-11-01T00:00:00Z", "2026-11-15T00:00:00Z")
SHSR_LIVE = "2026-11-13T06:16:00Z"
FROM_17 = "2026-11-17T00:00:00Z"   # the fallback and both levers, in November
EV_PER_INV = 3.175   # Workers Logs events per invocation: tools/wx/account_cost_read.py A8, the hour ending 16:00Z 2026-11-12, ABR 1
SHSR_CLASS_B = 432_000   # docs/wx-server.md: the ingest's reads, 5 a check (design; X10 did not measure Class B)
SHSR_STORAGE_GB = 0.4    # docs/wx-server.md: 31 frames
X10 = {"cpu_ms": 22_392_657, "r2_class_a": 120_780, "requests": 43_050}   # weatherscope S4.28b (9d8ed07d): 520.2 ms x 43,050
CREF_A_PER_H = 181.3     # X10's own anchor for cref (weatherscope S4.28b)
MONDAYS, A_DAY = ("2026-11-02", "2026-11-09"), "2026-11-13"   # named before the read (9.4)
EVENTS = {"monday": {"2026-11": 5, "2026-12": 4}, "a": {"2026-11": 1, "2026-12": 0}}
BANDS = {"monday": (120e3, 146e3), "background": (1.35e3, 1.65e3), "a": (1.08e6, 1.32e6)}   # 9.4, across both namespaces
LEVERS = {"shsr": ("built", "built-no-list", 43_050, "less one ListObjects per invocation (43,050 a month)"),
          "missileer-live": ("now", "cut", 250_000, "less 250,000 a month, as sent to the owner")}

ALLOWANCES = {   # tools/wx/account_cost_read.py PRICE and ROUND_UP (Cloudflare's docs, read 2026-11-11); Read A:
    # developers.cloudflare.com/r2/pricing, "Billable unit rounding"
    "r2_class_a": {"label": "R2 Class A (writes)", "included": 1e6, "price": 4.50, "unit": 1e6, "round_up": True},
    "r2_class_b": {"label": "R2 Class B (reads)", "included": 10e6, "price": 0.36, "unit": 1e6, "round_up": True},
    "r2_storage_gb": {"label": "R2 storage (GB-month)", "included": 10, "price": 0.015, "unit": 1, "round_up": True},
    "kv_read": {"label": "KV reads", "included": 10e6, "price": 0.50, "unit": 1e6, "round_up": False},
    "kv_write": {"label": "KV writes", "included": 1e6, "price": 5.0, "unit": 1e6, "round_up": False},
    "kv_list": {"label": "KV lists", "included": 1e6, "price": 5.0, "unit": 1e6, "round_up": False},
    "kv_delete": {"label": "KV deletes", "included": 1e6, "price": 5.0, "unit": 1e6, "round_up": False},
    "cpu_ms": {"label": "Workers CPU (ms)", "included": 30e6, "price": 0.02, "unit": 1e6, "round_up": False},
    "requests": {"label": "Workers requests", "included": 10e6, "price": 0.30, "unit": 1e6, "round_up": False},
    "logs_events": {"label": "Workers Logs events (November prices)", "included": 20e6, "price": 0.60, "unit": 1e6, "round_up": False},
    "do_requests": {"label": "Durable Object requests", "included": 1e6, "price": 0.15, "unit": 1e6, "round_up": True},
    "do_gb_s": {"label": "Durable Object GB-s", "included": 400e3, "price": 12.50, "unit": 1e6, "round_up": True},
}
PRODUCTS = {
    "missileer-live": {"label": "Missileer live stream (R2 bucket missileer-live; an external S3 client); 'cut' is the lever",
                       "states": ["now", "cut"]},
    "missileer-clips": {"label": "Missileer clips and public export (missileer-clips)", "states": ["now"]},
    "proxy": {"label": "Blipscope proxy (blipscope-proxy; production KV; its BendSnapshot DO)", "states": ["now"]},
    "proxy-staging": {"label": "proxy staging (blipscope-proxy-staging; staging KV)", "states": ["now"]},
    "cref": {"label": "US radar ingest, cref (blipscope-wx-ingest; its Class A of blipscope-wx)", "states": ["now"]},
    "blipscope-wx-reads": {"label": "US ingest + proxy /wx serving: blipscope-wx's Class B (not separable by R2)", "states": ["now"]},
    "opera-ten": {"label": "Europe radar ingests, the ten (blipscope-wx-ingest-op*)", "states": ["silent", "running"]},
    "shsr": {"label": "US radar SeamlessHSR (blipscope-wx-ingest-shsr; live 2026-11-13 ~06:16Z; X10 as measured); "
                      "'built-no-list' is the weather lever", "states": ["absent", "built", "built-no-list"]},
    "basemap-staging": {"label": "basemap builder, staging (blipscope-basemap-staging; its pkg bucket)", "states": ["now"]},
    "basemap-bucket": {"label": "blipscope-basemap bucket (reader not separable)", "states": ["now"]},
    "wx-dev": {"label": "dev ingest, parked (blipscope-wx-ingest-dev; blipscope-wx-dev)", "states": ["now"]},
    "kiosk": {"label": "kiosk site and app (valarscopes-app, valarscopes-site)", "states": ["now"]},
    "account-level": {"label": "account-level R2 operations (no bucket)", "states": ["now"]},
    "unattributed": {"label": "UNATTRIBUTED: a script, bucket or namespace the mapping does not name (stop and report)", "states": ["now"]},
    "cref+opera-ten": {"label": "blipscope-wx Class A in a running window (cref and the ten together; derivation input only)", "states": ["now"]},
}


def scenarios():
    """9.6 and 9.8: the 30-day scenarios, then {none, fallback} x {no lever, weather, Missileer} x {2026-11, 2026-12}."""
    s = {"before shsr": {"opera-ten": "silent", "shsr": "absent", "missileer-live": "now"},
         "today": {"opera-ten": "silent", "shsr": "built", "missileer-live": "now"},
         "OPERA running": {"opera-ten": "running", "shsr": "absent", "missileer-live": "now"},
         "OPERA running + shsr": {"opera-ten": "running", "shsr": "built", "missileer-live": "now"}}
    for m, tag in (("2026-11", "Nov"), ("2026-12", "Dec")):
        for fb in (False, True):
            for lever in (None, "weather", "Missileer"):
                if m == "2026-11":
                    ten = [{"state": "silent"}, {"state": "running", "from": FROM_17}] if fb else "silent"
                    shsr = [{"state": "absent"}, {"state": "built", "from": SHSR_LIVE}] + (
                        [{"state": "built-no-list", "from": FROM_17}] if lever == "weather" else [])
                    ml = [{"state": "now"}, {"state": "cut", "from": FROM_17}] if lever == "Missileer" else "now"
                else:
                    ten = "running" if fb else "silent"
                    shsr = "built-no-list" if lever == "weather" else "built"
                    ml = "cut" if lever == "Missileer" else "now"
                name = f"{tag} {'fallback' if fb else 'none'}" + (f" + {lever}" if lever else "")
                s[name] = {"month": m, "opera-ten": ten, "shsr": shsr, "missileer-live": ml}
    return s


def hours(a, b):
    p = lambda t: datetime.fromisoformat(t.replace("Z", "+00:00"))
    return (p(b) - p(a)).total_seconds() / 3600


def kv_events(path):
    """9.4's KV rows from kv-days, after its bands. Returns (rows, read) or stops (exit 3) before anything is written."""
    kd = json.load(open(path, encoding="utf-8"))
    if tuple(kd["window"]) != KV_DAYS:
        raise SystemExit(3, f"kv-days window {kd['window']} is not the registered {list(KV_DAYS)}")
    days = kd["days"]
    want = [f"2026-11-{d:02d}" for d in range(1, 15)]
    if sorted(days) != want:
        raise SystemExit(3, f"kv-days holds {len(days)} days, not the 14 registered")
    per = {day: days[day].get("write", {}) for day in want}
    other = sum(v.get("other", 0) for v in per.values())
    if other:
        raise SystemExit(4, f"kv-days: {other:,} writes in a namespace the mapping does not name")
    tot = {day: sum(v.values()) for day, v in per.items()}
    rest = [d for d in want if d not in MONDAYS and d != A_DAY]
    bg = statistics.median(tot[d] for d in rest)
    mon = [tot[d] for d in MONDAYS]
    checks = [("background (median day)", bg, BANDS["background"])] + [(f"Monday {d}", tot[d], BANDS["monday"]) for d in MONDAYS] + [
        ("Monday mean", sum(mon) / 2, BANDS["monday"]), (f"(A) {A_DAY}", tot[A_DAY], BANDS["a"])]
    bad = [f"{n} {v:,.0f} outside {lo:,.0f}-{hi:,.0f}" for n, v, (lo, hi) in checks if not lo <= v <= hi]
    for n, v, (lo, hi) in checks:
        print(f"kv-days {n:24s} {v:>12,.0f}  band {lo:,.0f}-{hi:,.0f}  {'in' if lo <= v <= hi else 'OUTSIDE'}")
    if bad:
        raise SystemExit(3, "kv-days OUTSIDE A BAND, nothing written: " + "; ".join(bad))
    rows = []
    for ns in ("proxy", "proxy-staging"):
        b30 = statistics.median(per[d].get(ns, 0) for d in rest) * 30
        rows.append({"product": ns, "state": "now", "allowance": "kv_write", "value": round(b30), "read": "kv-days",
                     "basis": "measured", "cadence": "steady",
                     "how": f"background: the median of the {len(rest)} days without an event x 30"})
        pm = sum(per[d].get(ns, 0) for d in MONDAYS) / 2
        pa = per[A_DAY].get(ns, 0)
        for key, pe, how in (("monday", pm, f"refresh-data Monday: the mean of {' and '.join(MONDAYS)} (whole days)"),
                             ("a", pa, f"the (A) route refresh: {A_DAY} (the whole day)")):
            ev = EVENTS[key]
            rows.append({"product": ns, "state": "now", "allowance": "kv_write", "value": pe * ev.get(MONTH, 0), "read": f"kv-days-{key}",
                         "basis": "measured", "cadence": "one-off", "per_event": pe, "events": ev, "how": how})
    read = {"tool": "tools/budget/kv_by_day.py", "window": list(KV_DAYS), "totals": {},
            "note": "not anchored here: per-day counts, by event; the background row counts every day, so the event days' "
                    "own background is in both (named: about 1.5k a day)"}
    reads = {"kv-days": read, "kv-days-monday": {**read, "note": "the Monday rows of kv-days"},
             "kv-days-a": {**read, "note": "the (A) rows of kv-days"}}
    return rows, reads


def read(d, rid, a, b, state, take, obs=False):
    out = os.path.join(d, rid + ".json")
    cmd = [sys.executable, "-I", READ, rid, a, b, state, take, out] + (["--observation"] if obs else [])
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"{rid}: budget_read failed: {r.stdout[-300:]} {r.stderr[-300:]}")
    last = [ln for ln in r.stdout.splitlines() if ln.startswith("UNATTRIBUTED:")]
    if not obs and (not last or last[-1] != "UNATTRIBUTED: none"):
        raise SystemExit(4, f"{rid}: {last[-1] if last else 'no UNATTRIBUTED line'} -- stop and report")
    return json.load(open(out, encoding="utf-8"))


def val(rows, product, state, allowance):
    return sum(r["value"] for r in rows if r["product"] == product and r["state"] == state and r["allowance"] == allowance)


def steady(r, **kw):
    return {**r, "cadence": "steady", **kw}


def excluded(rows, why):
    """Every row into state "excluded", merged per key, so the read still anchors on its own totals (5.1)."""
    acc = {}
    for r in rows:
        k = (r["product"], r["allowance"], r["read"])
        if k in acc:
            acc[k]["value"] += r["value"]
        else:
            acc[k] = steady(r, state="excluded", how=r["how"] + f"; {why}")
    return list(acc.values())


def main():
    kv_rows, kv_reads = kv_events(sys.argv[1])   # the bands first: outside one, nothing is read or written
    out_path = sys.argv[2]
    with tempfile.TemporaryDirectory() as d:
        r7 = read(d, "r-7d", *R7, "silent", "rest")
        rs = read(d, "r-silent", *SILENT, "silent", "opera")
        rr = read(d, "r-run", *RUN, "running", "all")
        ro = read(d, "r-24h", *SILENT, "silent", "all", obs=True)
        mt = read(d, "MTD-1114", *MTD, "silent", "all")
    rows = excluded(r7["rows"] + rs["rows"], "an 11-12 read, kept so it anchors (9.8 v)")
    reads = {**r7["read"], **rs["read"], **ro["read"], **mt["read"]}
    # ---- the MTD base, with shsr's first day and KV writes split out (9.9 b, 9.4) ----
    k = 720 / hours(*MTD)
    h_day = hours(SHSR_LIVE, MTD[1])
    cref_a = round(CREF_A_PER_H * 720)
    for r in mt["rows"]:
        if r["product"] == "shsr":
            rows.append(steady(r, state="excluded", how=r["how"] + "; shsr's first day (9.9 b): X10 stands in built"))
        elif r["allowance"] == "kv_write":
            rows.append(steady(r, state="excluded", how=r["how"] + "; KV writes go by event (kv-days, 9.4)"))
        elif r["product"] == "cref" and r["allowance"] == "r2_class_a":
            day = r["value"] - cref_a
            if day < 0:
                raise SystemExit(5, f"blipscope-wx Class A {r['value']:,} is below cref's X10 share {cref_a:,}: 9.9(b) does not hold")
            rows.append(steady(r, value=cref_a, basis="derived",
                               how=f"cref at X10's anchor, {CREF_A_PER_H} an hour x 720 (9.9 b); blipscope-wx's row was {r['value']:,}"))
            rows.append(steady(r, product="shsr", state="excluded", value=day, basis="derived",
                               how=f"blipscope-wx's Class A less cref's X10 share: shsr's first day (9.9 b), {day / k:,.0f} raw"))
        elif r["product"] == "blipscope-wx-reads" and r["allowance"] == "r2_class_b":
            day = round(SHSR_CLASS_B * h_day / 720 * k)
            rows.append(steady(r, value=r["value"] - day, basis="derived",
                               how=r["how"] + f"; less shsr's first day, estimated {day:,} (9.9 b)"))
            rows.append(steady(r, product="shsr", state="excluded", value=day, basis="derived",
                               how=f"shsr's design Class B, {SHSR_CLASS_B:,} a month x {h_day:.2f} h / 720, scaled x {k:.4f} (9.9 b)"))
        else:
            rows.append(steady(r))
    # ---- the ten running, derived from r-run as before ----
    reads["r-run"] = {"tool": "tools/budget/budget_read.py (take all), derived here", "window": list(RUN), "totals": {},
                      "note": "not anchored: only the ten's rows are kept, and their R2 rows are derived"}
    for al in ("cpu_ms", "requests"):
        rows.append({"product": "opera-ten", "state": "running", "allowance": al, "value": val(rr["rows"], "opera-ten", "running", al),
                     "read": "r-run", "basis": "measured", "cadence": "steady", "how": f"the ten's scripts, {RUN[0]}..{RUN[1]} x 240"})
    for al, prod in (("r2_class_a", "cref+opera-ten"), ("r2_class_b", "blipscope-wx-reads")):
        v = val(rr["rows"], prod, "now", al) - val(rs["rows"], "cref" if al == "r2_class_a" else prod, "now", al)
        rows.append({"product": "opera-ten", "state": "running", "allowance": al, "value": max(0, round(v)), "read": "r-run",
                     "basis": "derived", "cadence": "steady",
                     "how": f"blipscope-wx in {RUN[0]}..{RUN[1]} (x 240) minus r-silent's: {round(v):,}"})
    # ---- shsr: X10 as measured, the rest projected ----
    reads["x10"] = {"tool": "weatherscope S4.28b (9d8ed07d): shsr as measured, 520.2 ms x 43,050 invocations", "window": [],
                    "totals": dict(X10)}
    for al, v in X10.items():
        rows.append({"product": "shsr", "state": "built", "allowance": al, "value": v, "read": "x10", "basis": "measured",
                     "cadence": "steady", "how": "X10 (weatherscope S4.28b)" + (" ± 13k" if al == "r2_class_a" else "")})
    reads["proj-shsr"] = {"tool": "docs/wx-server.md's design (X10 did not measure these)", "window": [], "totals": {}}
    for al, v, how in (("r2_class_b", SHSR_CLASS_B, "docs/wx-server.md's design: 5 reads a check (X10 did not measure Class B)"),
                       ("r2_storage_gb", SHSR_STORAGE_GB, "docs/wx-server.md's design: 31 frames (X10 did not measure storage)")):
        rows.append({"product": "shsr", "state": "built", "allowance": al, "value": v, "read": "proj-shsr", "basis": "projected",
                     "cadence": "steady", "how": how})
    # ---- the levers (9.8 iv): product states, projected ----
    reads["levers"] = {"tool": "tools/budget/build_budget.py: the levers as product states (9.8 iv)", "window": [], "totals": {}}
    for prod, (src, st, less, how) in LEVERS.items():
        base = [r for r in rows if r["product"] == prod and r["state"] == src]
        if not any(r["allowance"] == "r2_class_a" for r in base):
            raise SystemExit(5, f"lever {prod}: no Class A row in {src} to lower")
        for al in sorted({r["allowance"] for r in base}):
            v = sum(r["value"] for r in base if r["allowance"] == al)
            if al == "r2_class_a":
                v -= less
            rows.append({"product": prod, "state": st, "allowance": al, "value": max(0, v), "read": "levers", "basis": "projected",
                         "cadence": "steady", "how": f"{prod} {src}" + (f", Class A {how}" if al == "r2_class_a" else ", unchanged")})
    rows += kv_rows
    reads.update(kv_reads)
    reads["derived-logs"] = {"tool": f"requests x {EV_PER_INV} events per invocation (account_cost_read.py A8)", "window": [], "totals": {}}
    for r in list(rows):
        if r["allowance"] == "requests" and r["state"] != "excluded":
            rows.append({"product": r["product"], "state": r["state"], "allowance": "logs_events", "value": round(r["value"] * EV_PER_INV),
                         "read": "derived-logs", "basis": "derived", "cadence": "steady", "how": f"{r['read']}'s requests x {EV_PER_INV}"})
    b = {"version": 2, "built_at": MTD[1], "tool": "tools/budget/build_budget.py", "month": MONTH,
         "allowances": ALLOWANCES, "products": PRODUCTS, "scenarios": scenarios(), "reads": reads, "rows": rows}
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(b, fh, indent=1)
        fh.write("\n")
    print(f"9.9(b): blipscope-wx Class A less cref's {cref_a:,}; shsr's Class B day over {h_day:.2f} h; window x {k:.4f}")
    print(f"wrote {out_path}: {len(rows)} rows, {len(reads)} reads, {len(b['scenarios'])} scenarios")
    return 0


if __name__ == "__main__":
    sys.exit(main())
