#!/usr/bin/env python3
"""The account budget's checker (docs/account-budget.md, ruled by the owner 2026-11-12 17:19Z: "The whole-account rule
stands, made checkable").

Reads docs/account-budget.json: every product's share of every Cloudflare allowance, by state, with the read each row
came from. For each scenario it sums every allowance, prices it under Read A (R2 and Durable Objects round up to a
whole billing unit, as Cloudflare publishes), and prints FLAG at 80% or more and OVER past 100%, then the scenario's
total monthly overage. A flag is the digest's input, not a failure.

  python3 scripts/account_budget.py [file]            print the table and the flags          exit 0, or 2 malformed
  python3 scripts/account_budget.py --render [file]   write the table into docs/account-budget.md section 6
  python3 scripts/account_budget.py --render-check    exit 1 if the doc's table is not what the file renders
  python3 scripts/account_budget.py --selftest        the controls (section 4) on a synthetic budget

Exit 2: a malformed file -- an unknown allowance, product or state, a row without a known read, a duplicate row, a
scenario that leaves a product's state unset -- or a failed anchor: the rows taken from one read must sum to that
read's own account total within 1%, so a product left out (or counted twice) cannot pass.
"""
import copy
import datetime
import json
import math
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "docs", "account-budget.json")
DOC = os.path.join(ROOT, "docs", "account-budget.md")
BEGIN, END = "<!-- account-budget:begin (rendered by scripts/account_budget.py --render; do not edit) -->", "<!-- account-budget:end -->"
FLAG_AT, ANCHOR_TOL = 0.80, 0.01
REQUIRED = ("r2_class_a", "r2_class_b", "kv_read", "kv_write", "cpu_ms", "requests")   # the owner's point 1
# Section 5.1: a read keeps the groups another read supplies as rows in this state, so it still anchors on its own
# totals; no scenario ever sums them. An observation read ("observation": true) holds totals only and is printed beside
# the graded figure, never summed.
EXCLUDED = "excluded"


class Malformed(Exception):
    pass


MONTH = __import__("re").compile(r"^\d{4}-(0[1-9]|1[0-2])$")
STEADY_H = 30 * 24   # a steady row is a 30-day figure (9.1)


def month_bounds(m):
    """(start, end) of calendar month YYYY-MM, as aware UTC datetimes."""
    y, mo = int(m[:4]), int(m[5:])
    start = datetime.datetime(y, mo, 1, tzinfo=datetime.timezone.utc)
    end = datetime.datetime(y + (mo == 12), mo % 12 + 1, 1, tzinfo=datetime.timezone.utc)
    return start, end


def utc(t):
    d = datetime.datetime.fromisoformat(t.replace("Z", "+00:00")) if isinstance(t, str) else None
    if d is None or d.tzinfo is None or d.utcoffset() != datetime.timedelta(0):
        raise ValueError(t)
    return d


def segments(s, p, segs, m, states):
    """9.9(a): [(state, hours)] for a product's ordered segment list in month scenario s. Refuses an empty list, a first
    segment with a from, a later one without, an unknown state, a from outside the month, unsorted froms, and any list
    in a 30-day scenario (no month). Every refusal names the segment rule."""
    if m is None:
        raise Malformed(f"scenario {s}: {p} is given segments, and a 30-day scenario (no month) takes one state")
    if not segs:
        raise Malformed(f"scenario {s}: {p}'s segment list is empty")
    start, end = month_bounds(m)
    edges = [start]
    for i, g in enumerate(segs):
        if not isinstance(g, dict) or g.get("state") not in states:
            raise Malformed(f"scenario {s}: {p}'s segment {i} names no known state")
        if i == 0:
            if "from" in g:
                raise Malformed(f"scenario {s}: {p}'s first segment has a from (it starts the month)")
            continue
        try:
            t = utc(g.get("from"))
        except (ValueError, TypeError):
            raise Malformed(f"scenario {s}: {p}'s segment {i} needs from, an ISO UTC time")
        if not start < t < end:
            raise Malformed(f"scenario {s}: {p}'s segment {i} starts outside {m}")
        if t <= edges[-1]:
            raise Malformed(f"scenario {s}: {p}'s segment froms are not in increasing order")
        edges.append(t)
    edges.append(end)
    return [(g["state"], (edges[i + 1] - edges[i]).total_seconds() / 3600) for i, g in enumerate(segs)]


def validate(b):
    al, prods, reads = b.get("allowances", {}), b.get("products", {}), b.get("reads", {})
    if not isinstance(b.get("month"), str) or not MONTH.match(b["month"]):
        raise Malformed("the file has no month (YYYY-MM), the month its one-off rows are counted for")
    for a in REQUIRED:
        if a not in al:
            raise Malformed(f"required allowance {a} is missing")
    for a, v in al.items():
        for k in ("label", "included", "price", "unit", "round_up"):
            if k not in v:
                raise Malformed(f"allowance {a} has no {k}")
    for p, v in prods.items():
        if not v.get("states"):
            raise Malformed(f"product {p} has no states")
    seen = set()
    for i, r in enumerate(b.get("rows", [])):
        for k in ("product", "state", "allowance", "value", "read", "basis", "how"):
            if k not in r:
                raise Malformed(f"row {i} has no {k}")
        if r["allowance"] not in al:
            raise Malformed(f"row {i}: unknown allowance {r['allowance']}")
        if r["product"] not in prods:
            raise Malformed(f"row {i}: unknown product {r['product']}")
        if r["state"] != EXCLUDED and r["state"] not in prods[r["product"]]["states"]:
            raise Malformed(f"row {i}: {r['product']} has no state {r['state']}")
        if r["read"] not in reads:
            raise Malformed(f"row {i}: unknown read {r['read']}")
        if reads[r["read"]].get("observation"):
            raise Malformed(f"row {i}: read {r['read']} is an observation, which holds totals only")
        if r["basis"] not in ("measured", "derived", "projected"):
            raise Malformed(f"row {i}: basis must be measured, derived or projected")
        if not isinstance(r["value"], (int, float)) or r["value"] < 0 or math.isnan(r["value"]):
            raise Malformed(f"row {i}: value must be a number >= 0")
        # 9.1/9.2 (docs/account-budget.md section 9): a row is steady (a 30-day figure) or a one-off (per_event x the
        # events in a month). A one-off scaled as if steady is refused rather than summed: a plausible number of the
        # wrong kind is worse than none.
        cad = r.get("cadence")
        if cad not in ("steady", "one-off"):
            raise Malformed(f"row {i}: cadence must be steady or one-off")
        if cad == "steady" and ("per_event" in r or "events" in r):
            raise Malformed(f"row {i}: a steady row carries no per_event or events")
        if cad == "one-off":
            pe, ev = r.get("per_event"), r.get("events")
            if not isinstance(pe, (int, float)) or pe < 0 or math.isnan(pe):
                raise Malformed(f"row {i}: a one-off row needs per_event, a number >= 0")
            if not isinstance(ev, dict) or not ev or not all(
                    isinstance(m, str) and MONTH.match(m) and isinstance(n, int) and not isinstance(n, bool) and n >= 0
                    for m, n in ev.items()):
                raise Malformed(f"row {i}: a one-off row needs events, a map of YYYY-MM to a whole number >= 0")
            want = pe * ev.get(b["month"], 0)
            if abs(r["value"] - want) > 1e-9 * max(abs(want), 1.0):
                raise Malformed(f"row {i}: a one-off row's value must be per_event x events[{b['month']}] = {want:,.0f}, not {r['value']:,.0f}")
        key = (r["product"], r["state"], r["allowance"], r["read"])
        if key in seen:
            raise Malformed(f"row {i}: duplicate {key}")
        seen.add(key)
    oneoff_prods = {r["product"] for r in b.get("rows", []) if r.get("cadence") == "one-off"}
    for s, pick in b.get("scenarios", {}).items():
        m = pick.get("month")
        if m is not None and (not isinstance(m, str) or not MONTH.match(m)):
            raise Malformed(f"scenario {s}: month must be YYYY-MM")
        for p, v in prods.items():
            st = pick.get(p, v["states"][0] if len(v["states"]) == 1 else None)
            if isinstance(st, list):
                segments(s, p, st, m, v["states"])   # raises Malformed naming the segment rule
                if len(st) > 1 and p in oneoff_prods:
                    raise Malformed(f"scenario {s}: {p} has a one-off row, so its state may not change inside the "
                                    f"month (9.9a: no segments for it)")
            elif st is None or st not in v["states"]:
                raise Malformed(f"scenario {s} leaves {p}'s state unset or unknown")
    for rid, rd in reads.items():
        for k in ("tool", "window", "totals"):
            if k not in rd:
                raise Malformed(f"read {rid} has no {k}")
        for a in rd["totals"]:
            if a not in al:
                raise Malformed(f"read {rid}: unknown allowance {a} in totals")


def anchor(b):
    """Every read's rows, per allowance in its totals, sum to the read's account total within ANCHOR_TOL."""
    bad = []
    for rid, rd in b["reads"].items():
        if rd.get("observation"):
            continue
        for a, total in rd["totals"].items():
            got = sum(r["value"] for r in b["rows"] if r["read"] == rid and r["allowance"] == a)
            if abs(got - total) > ANCHOR_TOL * max(total, 1):
                bad.append(f"read {rid}, {a}: rows sum to {got:,.0f}, the account read {total:,.0f}")
    return bad


def cost(al, amount):
    over = max(0.0, amount - al["included"]) / al["unit"]
    if al["round_up"]:
        over = math.ceil(over - 1e-12)
    return over * al["price"]


def weights(b, s, pick):
    """{product: [(state, weight)]}: in scenario s a steady row of (product, state) counts value x weight. A 30-day
    scenario (no month) weighs 1; a month scenario weighs days/30 (9.8 i), or each segment's hours / 720 (9.9 a)."""
    m = pick.get("month")
    days = (month_bounds(m)[1] - month_bounds(m)[0]).days if m else 30
    out = {}
    for p, v in b["products"].items():
        st = pick.get(p, v["states"][0])
        out[p] = ([(x, h / STEADY_H) for x, h in segments(s, p, st, m, v["states"])] if isinstance(st, list)
                  else [(st, days / 30)])
    return out


def row_amount(r, m, wt):
    """One row's amount in a scenario: a one-off is per_event x events[month] (9.8 iii) -- in a 30-day scenario its
    value, which is the file's month's -- and a steady row is value x weight."""
    if r.get("cadence") == "one-off":
        return r["per_event"] * r["events"].get(m, 0) if m else r["value"]
    return r["value"] * wt


def evaluate(b):
    """{scenario: {"lines": [(allowance, amount, share, mark, cost)], "overage": usd}}"""
    out = {}
    for s, pick in b["scenarios"].items():
        m, w = pick.get("month"), weights(b, s, pick)
        lines, overage = [], 0.0
        for a, al in b["allowances"].items():
            amt = sum(row_amount(r, m, wt) for r in b["rows"]
                      if r["allowance"] == a and r["state"] != EXCLUDED
                      for st, wt in w[r["product"]] if st == r["state"])
            share = amt / al["included"] if al["included"] else 0.0
            mark = "OVER" if share > 1.0 else "FLAG" if share >= FLAG_AT else ""
            c = cost(al, amt)
            overage += c
            lines.append((a, amt, share, mark, c))
        out[s] = {"lines": lines, "overage": overage}
    return out


def fmt_amount(a, al, x):
    if al["unit"] == 1:
        return f"{x:,.2f}"
    return f"{x / 1e6:,.3f} M"


def observed(b):
    """{allowance: total} over the observation reads (section 5.1): printed beside the graded figure, never summed."""
    out = {}
    for rd in b["reads"].values():
        if rd.get("observation"):
            for a, v in rd["totals"].items():
                out[a] = out.get(a, 0) + v
    return out


OBS_LABEL = "24 h rate (observation, not graded)"


def table(b, ev, names, ob):
    heads = names + ([OBS_LABEL] if ob else [])
    L = ["| allowance | included | " + " | ".join(heads) + " |", "|---|---|" + "---|" * len(heads)]
    for i, (a, al) in enumerate(b["allowances"].items()):
        cells = []
        for s in names:
            _, amt, share, mark, c = ev[s]["lines"][i]
            cells.append(f"{fmt_amount(a, al, amt)} ({share:.0%})" + (f" **{mark}**" if mark else "") + (f", ${c:.2f}" if c else ""))
        if ob:
            cells.append(f"{fmt_amount(a, al, ob[a])} ({ob[a] / al['included']:.0%})" if a in ob else "not read")
        L.append(f"| {al['label']} | {fmt_amount(a, al, al['included'])} | " + " | ".join(cells) + " |")
    L.append("| **overage a month (Read A)** | | " + " | ".join(f"**${ev[s]['overage']:.2f}**" for s in names) + (" | |" if ob else " |"))
    return "\n".join(L)


def render(b):
    """The 30-day table (with the observation column), then one table per calendar month (9.8 i), then one line per
    one-off row (9.2): per_event x events[the file's month] = value, its events by month, the reader's 30-day figure."""
    ev, ob = evaluate(b), observed(b)
    thirty = [s for s, p in b["scenarios"].items() if "month" not in p]
    months = sorted({p["month"] for p in b["scenarios"].values() if "month" in p})
    parts = [f"**30-day figures** (a steady row as read; a one-off at {b['month']}'s events)", table(b, ev, thirty, ob)]
    for m in months:
        st, en = month_bounds(m)
        parts += [f"**{m}, calendar month ({(en - st).days} days)**",
                  table(b, ev, [s for s, p in b["scenarios"].items() if p.get("month") == m], {})]
    ones = [r for r in b["rows"] if r.get("cadence") == "one-off" and r["state"] != EXCLUDED]
    if ones:
        L = [f"| product | state | allowance | per_event x events[{b['month']}] = value | events by month | reader's 30-day figure |",
             "|---|---|---|---|---|---|"]
        for r in ones:
            a, al = r["allowance"], b["allowances"][r["allowance"]]
            n = r["events"].get(b["month"], 0)
            evs = ", ".join(f"{k}: {v}" for k, v in sorted(r["events"].items()))
            rd = fmt_amount(a, al, r["reader_30d"]) if "reader_30d" in r else "not read"
            L.append(f"| {r['product']} | {r['state']} | {al['label']} | {fmt_amount(a, al, r['per_event'])} x {n} = "
                     f"{fmt_amount(a, al, r['value'])} | {evs} | {rd} |")
        parts += ["**One-off rows**", "\n".join(L)]
    return "\n\n".join(parts) + "\n"


def flags(b):
    ev = evaluate(b)
    return {s: sorted((a, mark) for a, _, _, mark, _ in ev[s]["lines"] if mark) for s in ev}


def report(b):
    ev = evaluate(b)
    for s in ev:
        print(f"== {s}")
        for a, amt, share, mark, c in ev[s]["lines"]:
            al = b["allowances"][a]
            print(f"  {a:14s} {fmt_amount(a, al, amt):>14s} of {fmt_amount(a, al, al['included']):>10s}  {share:7.1%}  {mark:4s}  ${c:6.2f}")
        print(f"  overage a month (Read A): ${ev[s]['overage']:.2f}")
    ob = observed(b)
    if ob:
        print(f"== {OBS_LABEL}")
        for a, v in ob.items():
            al = b["allowances"][a]
            print(f"  {a:14s} {fmt_amount(a, al, v):>14s} of {fmt_amount(a, al, al['included']):>10s}  {v / al['included']:7.1%}")
    f = flags(b)
    print("FLAGS (80% or more):", "; ".join(f"{s}: " + (", ".join(f"{a} {m}" for a, m in f[s]) or "none") for s in f))


def load(path):
    with open(path, encoding="utf-8") as fh:
        b = json.load(fh)
    validate(b)
    bad = anchor(b)
    if bad:
        raise Malformed("ANCHOR FAILED (a product is missing or counted twice): " + "; ".join(bad))
    return b


def doc_table(path):
    t = open(path, encoding="utf-8").read().replace("\r\n", "\n")
    if BEGIN not in t or END not in t:
        return None, t
    return t.split(BEGIN, 1)[1].split(END, 1)[0].strip("\n") + "\n", t


def render_current(b, path):
    """True when the doc's table is exactly what the file renders (the --render-check and P5 share this)."""
    have, _ = doc_table(path)
    return have == render(b)


def write_render(b, path):
    _, t = doc_table(path)
    if BEGIN not in t:
        raise Malformed(f"{path} has no render markers")
    head, rest = t.split(BEGIN, 1)
    tail = rest.split(END, 1)[1]
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(head + BEGIN + "\n" + render(b) + END + tail)


# ---- the controls (docs/account-budget.md section 4), on a synthetic budget so they keep proving the logic ----------

def synthetic():
    A = lambda label, inc, price, unit, ru: {"label": label, "included": inc, "price": price, "unit": unit, "round_up": ru}
    b = {"allowances": {"r2_class_a": A("R2 Class A", 1e6, 4.50, 1e6, True), "r2_class_b": A("R2 Class B", 10e6, 0.36, 1e6, True),
                        "kv_read": A("KV reads", 10e6, 0.50, 1e6, False), "kv_write": A("KV writes", 1e6, 5.0, 1e6, False),
                        "cpu_ms": A("Workers CPU ms", 30e6, 0.02, 1e6, False), "requests": A("Workers requests", 10e6, 0.30, 1e6, False)},
         "products": {"stream": {"states": ["now"]}, "proxy": {"states": ["now"]}, "ingest": {"states": ["silent", "running"]}},
         "scenarios": {"today": {"ingest": "silent"}, "running": {"ingest": "running"}},
         "reads": {"r1": {"tool": "synthetic", "window": ["t0", "t1"],
                          "totals": {"r2_class_a": 0.90e6, "r2_class_b": 3.0e6, "kv_read": 7.2e6, "cpu_ms": 20e6}},
                   "r2": {"tool": "synthetic", "window": ["t2", "t3"], "totals": {}}},
         "rows": []}
    b["month"] = "2026-12"   # 9.1: the month the one-offs are counted for in a scenario without its own
    R = lambda p, st, a, v, rd="r1": {"product": p, "state": st, "allowance": a, "value": v, "read": rd, "basis": "measured",
                                      "how": "synthetic", "cadence": "steady"}
    b["rows"] = [R("stream", "now", "r2_class_a", 0.75e6), R("stream", "now", "r2_class_b", 2.0e6),
                 R("proxy", "now", "kv_read", 7.2e6), R("proxy", "now", "cpu_ms", 10e6), R("proxy", "now", "r2_class_b", 0.5e6),
                 R("ingest", "silent", "r2_class_a", 0.15e6), R("ingest", "silent", "cpu_ms", 10e6), R("ingest", "silent", "r2_class_b", 0.5e6),
                 R("ingest", "running", "r2_class_a", 0.30e6, "r2"), R("ingest", "running", "cpu_ms", 25e6, "r2"),
                 R("ingest", "running", "r2_class_b", 5.0e6, "r2")]
    return b


def selftest():
    fails = []
    def check(cond, what):
        print(("PASS " if cond else "FAIL ") + what)
        if not cond:
            fails.append(what)
    base = synthetic()
    validate(base)
    check(anchor(base) == [], "CONTROL: the clean synthetic budget anchors")
    want = {"today": [("r2_class_a", "FLAG")], "running": [("cpu_ms", "OVER"), ("r2_class_a", "OVER")]}
    got = flags(base)
    check(got == want, f"CONTROL: the clean budget's flags are exactly {want} (got {got})")
    # P1: +1.0 M KV reads in "today" (72% -> 82%) must FLAG kv_read
    p1 = copy.deepcopy(base)
    p1["rows"].append({"product": "proxy", "state": "now", "allowance": "kv_read", "value": 1.0e6, "read": "r2", "basis": "projected", "how": "P1 plant"})
    check(("kv_read", "FLAG") in flags(p1)["today"], "P1: a planted +1.0 M KV-reads row raises FLAG on kv_read")
    # P2: everything scaled to a third flags nothing
    p2 = copy.deepcopy(base)
    for r in p2["rows"]:
        r["value"] /= 3
    check(all(v == [] for v in flags(p2).values()), "P2: every row at a third raises no FLAG anywhere")
    # P3: an unknown allowance is refused
    p3 = copy.deepcopy(base)
    p3["rows"].append({"product": "proxy", "state": "now", "allowance": "r2_class_z", "value": 1, "read": "r1", "basis": "measured", "how": "P3"})
    try:
        validate(p3); ok = False
    except Malformed:
        ok = True
    check(ok, "P3: a row naming an unknown allowance is refused")
    # P4: the stream's rows removed fails the anchor (on Class A and Class B)
    p4 = copy.deepcopy(base)
    p4["rows"] = [r for r in p4["rows"] if r["product"] != "stream"]
    bad = anchor(p4)
    check(any("r2_class_a" in x for x in bad) and any("r2_class_b" in x for x in bad), f"P4: dropping the stream fails the anchor on Class A and B ({len(bad)} lines)")
    # P5: a stale render is refused
    with tempfile.TemporaryDirectory() as d:
        doc = os.path.join(d, "doc.md")
        with open(doc, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("# t\n\n" + BEGIN + "\n" + END + "\n")
        write_render(base, doc)
        same = render_current(base, doc)
        t = open(doc, encoding="utf-8").read()
        i = next(k for k, ch in enumerate(t) if ch.isdigit() and k > t.index(BEGIN) + len(BEGIN))
        with open(doc, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(t[:i] + str((int(t[i]) + 1) % 10) + t[i + 1:])
        stale = not render_current(base, doc)
    check(same and stale, "P5: a fresh render matches, and one edited digit does not")
    # P6 (section 5.1): an excluded row and an observation read far past every allowance change no flag and no sum
    p6 = copy.deepcopy(base)
    p6["rows"].append({"product": "proxy", "state": EXCLUDED, "allowance": "kv_read", "value": 50e6, "read": "r2", "basis": "measured", "how": "P6"})
    p6["reads"]["obs"] = {"tool": "synthetic", "window": ["t4", "t5"], "observation": True, "totals": {a: 1e12 for a in p6["allowances"]}}
    sums = lambda bb: {s: [round(x[1]) for x in v["lines"]] for s, v in evaluate(bb).items()}
    check(flags(p6) == flags(base) and sums(p6) == sums(base) and anchor(p6) == [],
          "P6: an excluded row and an observation read change no flag, no sum and no anchor")
    # ---- 9.3: the one-off basis. A one-off row is per_event x events[month]; a one-off scaled as if steady is refused ----
    one = {"product": "proxy", "state": "now", "allowance": "kv_write", "read": "r2", "basis": "measured", "how": "9.3",
           "cadence": "one-off", "per_event": 0.1e6, "events": {"2026-11": 5, "2026-12": 4}, "value": 0.4e6}
    def refused(bb):
        try:
            validate(bb)
            return False
        except Malformed:
            return True
    pc = copy.deepcopy(base); pc["rows"].append(dict(one))
    check(not refused(pc), "P-ONE-ctl: a one-off row whose value is per_event x events[month] reads clean")
    po = copy.deepcopy(base); po["rows"].append({**one, "value": one["value"] * 30})
    check(refused(po), "P-ONE: the same one-off scaled x 30 (as if steady) is refused")
    ps = copy.deepcopy(base); ps["rows"].append({**base["rows"][3], "allowance": "kv_write", "read": "r2", "per_event": 1.0})
    check(refused(ps), "P-ONE-steady: a steady row carrying per_event is refused")
    pn = copy.deepcopy(base); pn["rows"].append({k: v for k, v in base["rows"][3].items() if k != "cadence"} | {"allowance": "kv_write", "read": "r2"})
    check(refused(pn), "P-ONE-none: a row with no cadence is refused")
    # ---- 9.8/9.9: calendar-month scenarios and state segments ----------------------------------------------------------
    def amt(bb, s, a):
        return next(x[1] for x in evaluate(bb)[s]["lines"] if x[0] == a)
    pm = copy.deepcopy(pc)
    pm["scenarios"]["dec"] = {"month": "2026-12", "ingest": "silent"}
    pm["scenarios"]["nov-run"] = {"month": "2026-11", "ingest": [{"state": "silent"}, {"state": "running", "from": "2026-11-16T00:00:00Z"}]}
    ok = not refused(pm)
    # December, whole month: steady x 31/30; the one-off at 4 events
    check(ok and abs(amt(pm, "dec", "cpu_ms") - 20e6 * 31 / 30) < 1 and abs(amt(pm, "dec", "kv_write") - 0.4e6) < 1,
          "M1: a December scenario counts steady rows x 31/30 and a one-off at December's events")
    # November with the ingest running from 11-16: silent 15 days, running 15 days (of 30)
    want = 10e6 + 10e6 * 15 / 30 + 25e6 * 15 / 30
    check(ok and abs(amt(pm, "nov-run", "cpu_ms") - want) < 1 and abs(amt(pm, "nov-run", "kv_write") - 0.5e6) < 1,
          "M2: a segment weights each state's steady rows by its hours in the month; a one-off counts November's events")
    bad_segs = {"empty list": [], "first has a from": [{"state": "silent", "from": "2026-11-02T00:00:00Z"}],
                "unsorted": [{"state": "silent"}, {"state": "running", "from": "2026-11-20T00:00:00Z"}, {"state": "silent", "from": "2026-11-10T00:00:00Z"}],
                "out of month": [{"state": "silent"}, {"state": "running", "from": "2026-12-02T00:00:00Z"}]}
    # M3's refusals must name the SEGMENT rule: a validator that refuses every list (today's) passes "refused" alone,
    # so the reason is the assertion, and M2 (a valid list accepted) is its control.
    def why(bb):
        try:
            validate(bb)
            return ""
        except Malformed as e:
            return str(e)
    for label, segs in bad_segs.items():
        pb = copy.deepcopy(pc); pb["scenarios"]["x"] = {"month": "2026-11", "ingest": segs}
        check("segment" in why(pb), f"M3: segments refused, by the segment rule, when {label}")
    p30 = copy.deepcopy(pc); p30["scenarios"]["x"] = {"ingest": [{"state": "silent"}, {"state": "running", "from": "2026-11-16T00:00:00Z"}]}
    check("30-day" in why(p30), "M3: segments refused, by the segment rule, in a 30-day scenario")
    po2 = copy.deepcopy(pc)
    po2["rows"].append({**one, "product": "ingest", "state": "silent", "allowance": "kv_write"})
    po2["scenarios"]["x"] = {"month": "2026-11", "ingest": [{"state": "silent"}, {"state": "running", "from": "2026-11-16T00:00:00Z"}]}
    check("one-off" in why(po2) and "segment" in why(po2), "M3: a one-off row's product changing state inside a month is refused")
    # ---- 9.2 / 9.8: the render. One line per one-off row; month scenarios under their own month's table -------------
    rt = render(pm) if ok else ""
    check("| proxy | now | KV writes | 0.100 M x 4 = 0.400 M | 2026-11: 5, 2026-12: 4 | not read |" in rt,
          "R1: a one-off row renders as per_event x events[the file's month] = value, its events by month beside it")
    head30, _, rest = rt.partition("**2026-12, calendar month (31 days)**")
    tbl = next((c for c in rest.split("\n\n") if c.startswith("| allowance")), "")
    check("| dec |" in tbl.split("\n")[0] and " dec " not in head30 and " today " in head30,
          "R2: a month scenario renders in its month's table, never in the 30-day one")
    print("SELFTEST " + ("PASSED" if not fails else f"FAILED: {len(fails)}"))
    return 0 if not fails else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    path = next((a for a in argv if not a.startswith("--")), DATA)
    try:
        b = load(path)
    except Malformed as e:
        print(f"MALFORMED: {e}")
        return 2
    if "--render" in argv:
        write_render(b, DOC)
        print(f"rendered into {DOC}")
        return 0
    if "--render-check" in argv:
        if not render_current(b, DOC):
            print("STALE: docs/account-budget.md's table is not what docs/account-budget.json renders; run --render")
            return 1
        print("render: current")
        return 0
    print(f"account budget: {os.path.relpath(path, ROOT)} ({len(b['rows'])} rows, {len(b['reads'])} reads)")
    report(b)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
