#!/usr/bin/env python3
"""Every R2 bucket and every Worker our wrangler configs name is attributed by the account budget's own reader.

The account budget (docs/account-budget.md) attributes each billed bucket and script to a product through
tools/budget/budget_read.py: a bucket by its BUCKET table or its SHARED_BUCKETS rule, a Worker by script_product(). A name
neither attributes is written as "unattributed", and only on the first read that sees it -- after the money is spent. This
check moves that to the moment a config names it: on 2026-11-25 the D4 PR's production package bucket and production's
builder had no row, found by reading, not by a check.

  python3 scripts/check_budget_names.py              exit 0: every name attributed; 1: a finding (each printed)
  python3 scripts/check_budget_names.py --selftest   synthetic tables and configs, CB1-CB8; exit 0 only if every row passes

Names come from proxy/wrangler.toml (every r2_buckets bucket_name and every Worker `name`, at the top level and in every
env) and proxy/basemap.wrangler.toml (every Worker `name`). The reader's tables are IMPORTED from budget_read.py, never
copied, so a change there is checked here at once. NOT_IN_ACCOUNT holds the names a config carries but the account never
bills, each with its reason; an entry that is attributed anyway, or that no config names any more, is STALE and fails.
"""
import importlib.util
import os
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIGS = {"proxy/wrangler.toml": ("buckets", "workers"), "proxy/basemap.wrangler.toml": ("workers",)}
NOT_IN_ACCOUNT = {
    "blipscope-basemap-pkg-dev": "the top-level (dev and test) package bucket: vitest gives an in-memory bucket "
                                 "(proxy/wrangler.toml, the top-level note), and the account has no such bucket "
                                 "(its bucket list, read 2026-11-25)",
}


def load_reader(root=ROOT):
    spec = importlib.util.spec_from_file_location("budget_read", os.path.join(root, "tools", "budget", "budget_read.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def names_of(cfg):
    """(buckets, workers): [(name, where)] from one parsed wrangler config, top level and every env."""
    buckets, workers = [], []
    scopes = [("top level", cfg)] + [(f"env.{e}", v) for e, v in (cfg.get("env") or {}).items()]
    for where, sc in scopes:
        if "name" in sc:
            workers.append((sc["name"], where))
        for b in sc.get("r2_buckets", []):
            buckets.append((b["bucket_name"], f"{where} {b.get('binding', '?')}"))
    return buckets, workers


def findings(bucket_names, worker_names, bucket_table, shared, script_product, exempt):
    """Every finding, as a sorted list of strings: an unattributed bucket or Worker, or a stale exemption."""
    out = set()
    bucket_ok = lambda n: n in bucket_table or n in shared
    worker_ok = lambda n: script_product(n, "silent")[0] != "unattributed"
    for n, where in bucket_names:
        if not bucket_ok(n) and n not in exempt:
            out.add(f"UNATTRIBUTED bucket {n} ({where})")
    for n, where in worker_names:
        if not worker_ok(n) and n not in exempt:
            out.add(f"UNATTRIBUTED worker {n} ({where})")
    named = {n for n, _ in bucket_names} | {n for n, _ in worker_names}
    for n in exempt:
        if bucket_ok(n) or worker_ok(n):
            out.add(f"STALE exemption {n} (it is attributed)")
        elif n not in named:
            out.add(f"STALE exemption {n} (no config names it)")
    return sorted(out)


def collect(root=ROOT):
    buckets, workers = [], []
    for rel, kinds in CONFIGS.items():
        with open(os.path.join(root, rel), "rb") as fh:
            b, w = names_of(tomllib.load(fh))
        if "buckets" in kinds:
            buckets += [(n, f"{rel} {where}") for n, where in b]
        if "workers" in kinds:
            workers += [(n, f"{rel} {where}") for n, where in w]
    return buckets, workers


def main(root=ROOT, exempt=None):
    exempt = NOT_IN_ACCOUNT if exempt is None else exempt
    r = load_reader(root)
    buckets, workers = collect(root)
    f = findings(buckets, workers, r.BUCKET, r.SHARED_BUCKETS, r.script_product, exempt)
    for line in f:
        print(line)
    print(f"budget names: {len({n for n, _ in buckets})} bucket names, {len({n for n, _ in workers})} worker names; "
          f"{len(exempt)} not in the account; findings {len(f)}; {'OK' if not f else 'FAIL'}")
    return 0 if not f else 1


def selftest():
    passed = ran = 0
    def check(ok, what):
        nonlocal passed, ran
        ran += 1; passed += ok
        print(f"{'PASS' if ok else 'FAIL'} {what}")
    table, shared = {"b-mapped": "p", "b-dev-real": "dev"}, {"b-shared"}
    sp = lambda n, _st: (("proxy", "now") if n in ("w-proxy", "w-staging") else ("unattributed", "now"))
    B = [("b-mapped", "env.staging X"), ("b-shared", "env.production WX"), ("b-dev-real", "top level X")]
    W = [("w-proxy", "top level"), ("w-staging", "env.staging")]
    check(findings(B, W, table, shared, sp, {}) == [], "CB1 every name attributed: no finding")
    check(findings(B + [("b-new", "env.production P")], W, table, shared, sp, {}) == ["UNATTRIBUTED bucket b-new (env.production P)"],
          "CB2 an unmapped bucket: exactly that finding")
    check(findings(B, W + [("w-new", "env.production")], table, shared, sp, {}) == ["UNATTRIBUTED worker w-new (env.production)"],
          "CB3 an unmapped Worker: exactly that finding")
    check(findings(B, W, table, set(), sp, {}) == ["UNATTRIBUTED bucket b-shared (env.production WX)"],
          "CB4 a shared bucket is attributed only through SHARED_BUCKETS: emptied, exactly that finding")
    check(findings(B + [("b-tests", "top level P")], W, table, shared, sp, {"b-tests": "tests only"}) == [],
          "CB5 an exempt name that is not attributed: no finding")
    check(findings(B, W, table, shared, sp, {"b-mapped": "x"}) == ["STALE exemption b-mapped (it is attributed)"],
          "CB6 an exemption for an attributed name: STALE")
    check(findings(B, W, table, shared, sp, {"b-gone": "x"}) == ["STALE exemption b-gone (no config names it)"],
          "CB7 an exemption no config names: STALE")
    cfg = tomllib.loads('name = "w-top"\n[[r2_buckets]]\nbinding = "A"\nbucket_name = "b1"\n'
                        '[env.staging]\nname = "w-st"\n[[env.staging.r2_buckets]]\nbinding = "A"\nbucket_name = "b2"\n'
                        '[env.production]\nname = "w-pr"\n[[env.production.r2_buckets]]\nbinding = "A"\nbucket_name = "b3"\n'
                        '[[env.production.r2_buckets]]\nbinding = "B"\nbucket_name = "b4"\n')
    b, w = names_of(cfg)
    check([n for n, _ in b] == ["b1", "b2", "b3", "b4"] and [n for n, _ in w] == ["w-top", "w-st", "w-pr"],
          "CB8 CONTROL: the parser reads the top level and every env (4 buckets, 3 Worker names)")
    print(f"check_budget_names selftest: {passed} of {ran}")
    return 0 if passed == ran else 1


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    if sys.argv[1:]:
        print("usage: check_budget_names.py [--selftest]"); sys.exit(2)
    sys.exit(main())
