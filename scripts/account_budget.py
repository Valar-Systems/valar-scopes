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


def validate(b):
    al, prods, reads = b.get("allowances", {}), b.get("products", {}), b.get("reads", {})
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
        key = (r["product"], r["state"], r["allowance"], r["read"])
        if key in seen:
            raise Malformed(f"row {i}: duplicate {key}")
        seen.add(key)
    for s, pick in b.get("scenarios", {}).items():
        for p, v in prods.items():
            st = pick.get(p, v["states"][0] if len(v["states"]) == 1 else None)
            if st is None or st not in v["states"]:
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


def evaluate(b):
    """{scenario: {"lines": [(allowance, amount, share, mark, cost)], "overage": usd}}"""
    out = {}
    for s, pick in b["scenarios"].items():
        state = {p: pick.get(p, v["states"][0]) for p, v in b["products"].items()}
        lines, overage = [], 0.0
        for a, al in b["allowances"].items():
            amt = sum(r["value"] for r in b["rows"]
                      if r["allowance"] == a and r["state"] != EXCLUDED and state[r["product"]] == r["state"])
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


def render(b):
    ev, ob = evaluate(b), observed(b)
    heads = list(ev) + ([OBS_LABEL] if ob else [])
    L = ["| allowance | included | " + " | ".join(heads) + " |", "|---|---|" + "---|" * len(heads)]
    for i, (a, al) in enumerate(b["allowances"].items()):
        cells = []
        for s in ev:
            _, amt, share, mark, c = ev[s]["lines"][i]
            cells.append(f"{fmt_amount(a, al, amt)} ({share:.0%})" + (f" **{mark}**" if mark else "") + (f", ${c:.2f}" if c else ""))
        if ob:
            cells.append(f"{fmt_amount(a, al, ob[a])} ({ob[a] / al['included']:.0%})" if a in ob else "not read")
        L.append(f"| {al['label']} | {fmt_amount(a, al, al['included'])} | " + " | ".join(cells) + " |")
    L.append("| **overage a month (Read A)** | | " + " | ".join(f"**${ev[s]['overage']:.2f}**" for s in ev) + (" | |" if ob else " |"))
    return "\n".join(L) + "\n"


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
    R = lambda p, st, a, v, rd="r1": {"product": p, "state": st, "allowance": a, "value": v, "read": rd, "basis": "measured", "how": "synthetic"}
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
