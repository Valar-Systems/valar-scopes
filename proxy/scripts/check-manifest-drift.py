#!/usr/bin/env python3
"""Repo photo manifest vs the published one: the same IDs, both directions.

WHY THIS EXISTS. On 2026-09-16 the live credits page credited a photograph no
device could fetch. T210 had been removed from proxy/photos/manifest.json and its
pointers were gone, but the published photo:manifest still carried the row -- so
the repo said 237, production said 238, and nothing anywhere compared the two.
It surfaced only because somebody asked why an uncommitted credits.html had a
deleted attribution line in it.

WHAT IT CHECKS, and deliberately not more. The ID set (target, kind) in
proxy/photos/manifest.json must equal the ID set in the published photo:manifest.
Both directions:

  in the repo, not published  -> a photo the credits page will not credit
  published, not in the repo  -> a credit for something the repo no longer ships

It does NOT compare blobKey/squareKeys, because those exist only in the published
manifest -- they are produced at upload from the image bytes. Comparing them
against a file that cannot contain them is how someone concludes the published
manifest is wrong and "fixes" it by uploading the repo file, which strips those
keys from every row and breaks photo serving fleet-wide. It is reported as a
count instead.

READ-ONLY. Every call is a get. Requires a token in CLOUDFLARE_API_TOKEN, and
`--remote` is not optional: without it wrangler reads the LOCAL namespace and
returns an empty list with exit 0, which reads exactly like a wiped manifest.

  python proxy/scripts/check-manifest-drift.py            # production
  python proxy/scripts/check-manifest-drift.py --env staging

Exit 0 clean, 1 drift, 2 could not verify -- and 2 is not a pass.
"""
import argparse
import json
import os
import subprocess
import sys

NS = {"production": "733bf90056104f56be9492e62450a0bf",
      "staging": "8b6e92ed802a4192b6b6c1399fdf96aa"}
MANIFEST_KEY = "photo:manifest"


def kv_get(ns: str, key: str) -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    proxy = os.path.dirname(here)          # wrangler needs its own directory
    r = subprocess.run(
        ["npx", "wrangler", "kv", "key", "get", "--remote", "--namespace-id", ns, key],
        cwd=proxy, capture_output=True, text=True, encoding="utf-8", shell=(os.name == "nt"),
    )
    if r.returncode != 0:
        # stderr, not stdout: wrangler puts auth failures on stderr and a cheerful
        # "would you like to report this?" on stdout, so a stdout-only read of a
        # failed call looks like a parse problem rather than an auth problem.
        print("wrangler exited %d\n%s" % (r.returncode, (r.stderr or "").strip()[:400]),
              file=sys.stderr)
        sys.exit(2)
    return r.stdout


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", default="production", choices=sorted(NS))
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    repo_file = os.path.join(os.path.dirname(here), "photos", "manifest.json")
    raw = json.load(open(repo_file, encoding="utf-8"))
    repo_rows = raw if isinstance(raw, list) else raw.get("entries", raw)

    try:
        pub = json.loads(kv_get(NS[args.env], MANIFEST_KEY))
    except json.JSONDecodeError as e:
        print("published %s did not parse: %s" % (MANIFEST_KEY, e), file=sys.stderr)
        return 2

    # ANCHOR CONTROL. An empty or tiny published manifest is the shape of a broken
    # probe, not of a real corpus, and "everything drifted" is the loudest possible
    # false alarm. Refuse rather than report it.
    if not isinstance(pub, list) or len(pub) < 10:
        print("published manifest has %d row(s) -- refusing to judge drift against it"
              % (len(pub) if isinstance(pub, list) else -1), file=sys.stderr)
        return 2
    if not repo_rows:
        print("repo manifest is empty -- refusing", file=sys.stderr)
        return 2

    ident = lambda r: (str(r.get("target", "")).upper(), r.get("kind"))
    repo_ids, pub_ids = {ident(r) for r in repo_rows}, {ident(r) for r in pub}

    only_repo = sorted(repo_ids - pub_ids)
    only_pub = sorted(pub_ids - repo_ids)

    print("repo %s: %d rows" % (os.path.relpath(repo_file), len(repo_rows)))
    print("published %s (%s): %d rows" % (MANIFEST_KEY, args.env, len(pub)))
    noblob = [ident(r) for r in pub if not r.get("blobKey")]
    print("published rows carrying a blobKey: %d of %d" % (len(pub) - len(noblob), len(pub)))

    if not only_repo and not only_pub and not noblob:
        print("\nID sets match in both directions. No drift.")
        return 0

    print()
    for label, rows in (("in the repo, NOT published", only_repo),
                        ("published, NOT in the repo", only_pub),
                        ("published with NO blobKey (cannot serve)", noblob)):
        print("%s: %d" % (label, len(rows)))
        for t, k in rows[:20]:
            print("    %s (%s)" % (t, k))
    return 1


if __name__ == "__main__":
    sys.exit(main())
