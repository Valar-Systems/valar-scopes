#!/usr/bin/env python3
"""Assemble docs/account-budget.json from its reads (docs/account-budget.md sections 2 and 5.1). Read-only against
Cloudflare; it runs tools/budget/budget_read.py four times and writes one file.

  python3 -I tools/budget/build_budget.py <end ISO, a whole hour> <out.json>

The reads, all ending at <end> unless stated:
  r-7d       the 7-day run rate, take "rest": every line except the OPERA-dependent ones (section 5.1)
  r-silent   from 2026-11-11T21:00Z (OPERA silent since about 20:10Z), take "opera": the ten silent, cref's Class A,
             blipscope-wx's Class B
  r-run      2026-11-11T17:00-20:00Z, the only whole hours the ten all ran, take "all": used only to DERIVE the ten's
             running rows (their scripts as measured; their R2 Class A and B as the running hours' blipscope-wx minus
             r-silent's), so its rows are not kept and it carries no totals
  r-24h      the 24 h ending <end>, an observation: totals only, printed beside "today", never summed
Plus: shsr's rows, projected from cref's (Class B from docs/wx-server.md's design, since cref's own reads cannot be
separated from the proxy's); Workers Logs events derived from each kept requests row.
"""
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timedelta

HERE = os.path.dirname(os.path.abspath(__file__))
READ = os.path.join(HERE, "budget_read.py")
SILENT_FROM = "2026-11-11T21:00:00Z"
RUN = ("2026-11-11T17:00:00Z", "2026-11-11T20:00:00Z")
EV_PER_INV = 3.175   # Workers Logs events per invocation: tools/wx/account_cost_read.py A8, the hour ending 16:00Z 2026-11-12, ABR 1
SHSR_CLASS_B = 432_000   # docs/wx-server.md: the ingest's reads, 5 a check (design; cref's own share is not separable)

ALLOWANCES = {   # tools/wx/account_cost_read.py PRICE and ROUND_UP (Cloudflare's docs, read 2026-11-11)
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
    "missileer-live": {"label": "Missileer live stream (R2 bucket missileer-live; an external S3 client)", "states": ["now"]},
    "missileer-clips": {"label": "Missileer clips and public export (missileer-clips)", "states": ["now"]},
    "proxy": {"label": "Blipscope proxy (blipscope-proxy; production KV; its BendSnapshot DO)", "states": ["now"]},
    "proxy-staging": {"label": "proxy staging (blipscope-proxy-staging; staging KV)", "states": ["now"]},
    "cref": {"label": "US radar ingest, cref (blipscope-wx-ingest; its Class A of blipscope-wx)", "states": ["now"]},
    "blipscope-wx-reads": {"label": "US ingest + proxy /wx serving: blipscope-wx's Class B (not separable by R2)", "states": ["now"]},
    "opera-ten": {"label": "Europe radar ingests, the ten (blipscope-wx-ingest-op*)", "states": ["silent", "running"]},
    "shsr": {"label": "US radar SeamlessHSR (projected from cref until built)", "states": ["absent", "built"]},
    "basemap-staging": {"label": "basemap builder, staging (blipscope-basemap-staging; its pkg bucket)", "states": ["now"]},
    "basemap-bucket": {"label": "blipscope-basemap bucket (reader not separable)", "states": ["now"]},
    "wx-dev": {"label": "dev ingest, parked (blipscope-wx-ingest-dev; blipscope-wx-dev)", "states": ["now"]},
    "kiosk": {"label": "kiosk site and app (valarscopes-app, valarscopes-site)", "states": ["now"]},
    "account-level": {"label": "account-level R2 operations (no bucket)", "states": ["now"]},
    "unattributed": {"label": "UNATTRIBUTED: a script, bucket or namespace the mapping does not name (stop and report)", "states": ["now"]},
    "cref+opera-ten": {"label": "blipscope-wx Class A in a running window (cref and the ten together; derivation input only)", "states": ["now"]},
}
SCENARIOS = {"today": {"opera-ten": "silent", "shsr": "absent"},
             "OPERA running": {"opera-ten": "running", "shsr": "absent"},
             "OPERA running + shsr": {"opera-ten": "running", "shsr": "built"}}


def read(d, rid, a, b, state, take, obs=False):
    out = os.path.join(d, rid + ".json")
    cmd = [sys.executable, "-I", READ, rid, a, b, state, take, out] + (["--observation"] if obs else [])
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"{rid}: budget_read failed: {r.stdout[-300:]} {r.stderr[-300:]}")
    if "UNATTRIBUTED: none" not in r.stdout:
        print(f"{rid}: {r.stdout.strip().splitlines()[-1]}")
    return json.load(open(out))


def val(rows, product, state, allowance):
    return sum(r["value"] for r in rows if r["product"] == product and r["state"] == state and r["allowance"] == allowance)


def main():
    end, out_path = sys.argv[1], sys.argv[2]
    e = datetime.fromisoformat(end.replace("Z", "+00:00"))
    iso = lambda t: t.strftime("%Y-%m-%dT%H:%M:%SZ")
    with tempfile.TemporaryDirectory() as d:
        r7 = read(d, "r-7d", iso(e - timedelta(days=7)), end, "silent", "rest")
        rs = read(d, "r-silent", SILENT_FROM, end, "silent", "opera")
        rr = read(d, "r-run", RUN[0], RUN[1], "running", "all")
        ro = read(d, "r-24h", iso(e - timedelta(days=1)), end, "silent", "all", obs=True)
    rows = r7["rows"] + rs["rows"]
    reads = {**r7["read"], **rs["read"], **ro["read"]}
    reads["r-run"] = {"tool": "tools/budget/budget_read.py (take all), derived here", "window": list(RUN), "totals": {},
                      "note": "not anchored: only the ten's rows are kept, and their R2 rows are derived"}
    reads["proj-shsr"] = {"tool": "tools/budget/build_budget.py, projected from cref", "window": r7["read"]["r-7d"]["window"], "totals": {}}
    reads["derived-logs"] = {"tool": f"requests x {EV_PER_INV} events per invocation (account_cost_read.py A8)", "window": [], "totals": {}}
    run_rows = rr["rows"]
    for al in ("cpu_ms", "requests"):
        rows.append({"product": "opera-ten", "state": "running", "allowance": al, "value": val(run_rows, "opera-ten", "running", al),
                     "read": "r-run", "basis": "measured", "how": f"the ten's scripts, {RUN[0]}..{RUN[1]} x 240"})
    a_run = val(run_rows, "cref+opera-ten", "now", "r2_class_a") - val(rs["rows"], "cref", "now", "r2_class_a")
    b_run = val(run_rows, "blipscope-wx-reads", "now", "r2_class_b") - val(rs["rows"], "blipscope-wx-reads", "now", "r2_class_b")
    for al, v in (("r2_class_a", a_run), ("r2_class_b", b_run)):
        rows.append({"product": "opera-ten", "state": "running", "allowance": al, "value": max(0, round(v)), "read": "r-run", "basis": "derived",
                     "how": f"blipscope-wx in {RUN[0]}..{RUN[1]} (x 240) minus r-silent's: {round(v):,}" +
                            (" (the running hours also differ in time of day, so the proxy's /wx serving moves with them)" if al == "r2_class_b" else "")})
    for al, v, how in (("r2_class_a", val(rs["rows"], "cref", "now", "r2_class_a"), "cref's Class A (r-silent)"),
                       ("cpu_ms", val(r7["rows"], "cref", "now", "cpu_ms"), "cref's CPU (r-7d)"),
                       ("requests", val(r7["rows"], "cref", "now", "requests"), "cref's invocations (r-7d)"),
                       ("r2_class_b", SHSR_CLASS_B, "docs/wx-server.md's design: 5 reads a check"),
                       ("r2_storage_gb", 0.4, "docs/wx-server.md's design: 31 frames")):
        rows.append({"product": "shsr", "state": "built", "allowance": al, "value": v, "read": "proj-shsr", "basis": "projected", "how": how})
    for r in list(rows):
        if r["allowance"] == "requests" and r["state"] != "excluded":
            rows.append({"product": r["product"], "state": r["state"], "allowance": "logs_events", "value": round(r["value"] * EV_PER_INV),
                         "read": "derived-logs", "basis": "derived", "how": f"{r['read']}'s requests x {EV_PER_INV}"})
    b = {"version": 1, "built_at": end, "tool": "tools/budget/build_budget.py",
         "allowances": ALLOWANCES, "products": PRODUCTS, "scenarios": SCENARIOS, "reads": reads, "rows": rows}
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(b, fh, indent=1)
        fh.write("\n")
    print(f"wrote {out_path}: {len(rows)} rows, {len(reads)} reads")


if __name__ == "__main__":
    sys.exit(main())
