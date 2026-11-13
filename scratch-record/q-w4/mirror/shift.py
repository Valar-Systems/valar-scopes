# SCRATCH, read-only: (1) the base rate -- of the files both bm-obj's manifest (fetched late October) and the new cache's fetch log
# (fetched 2026-11-13) hold, how many have the SAME sha256 and bytes, by host and layer; (2) under the mirror URLs, how many of the new
# store's manifest files change their index (the u16 `file` every feature carries), by layer. Nothing fetched.
import json, os, sys, collections
S = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); C = os.path.join(os.path.dirname(S), "bm-cache-qw4")
eight = [l.split()[1].rstrip(":") for l in open(os.path.join(S, "seastore/logs/2-fetch.err"), encoding="utf-8") if l.startswith("FAILED ")]
assert len(eight) == 8, len(eight)
mirror = {u: u.replace("https://www2.census.gov/geo/tiger/", "https://ftp2.census.gov/geo/tiger/", 1) for u in eight}
assert all(m != u for u, m in mirror.items())
bm = {f["url"]: f for f in json.load(open(os.path.join(S, "bm-obj/manifest.json"), encoding="utf-8"))["files"]}
log = json.load(open(os.path.join(C, "fetch-log.json"), encoding="utf-8"))
host = lambda u: u.split("/")[2]
same, diff = collections.Counter(), collections.Counter()
for u, r in log.items():
    if u in bm:
        k = (host(u), bm[u]["layer"])
        (same if (r["sha256"], r["bytes"]) == (bm[u]["sha256"], bm[u]["bytes"]) else diff)[k] += 1
print("(1) files in both bm-obj's manifest and the 11-13 fetch log: same bytes+sha", sum(same.values()), "| different", sum(diff.values()))
for k in sorted(set(same) | set(diff)): print(f"    {k[0]} {k[1]}: same {same[k]}, different {diff[k]}")
print("    in the new log but not bm-obj:", sum(1 for u in log if u not in bm), "| in bm-obj but not the new log:", sum(1 for u in bm if u not in log))
plan = json.load(open(os.path.join(C, "plan.json"), encoding="utf-8"))["files"] + json.load(open(os.path.join(C, "tiles.json"), encoding="utf-8"))["files"]
layer = {f["url"]: f["layer"] for f in plan}
a = sorted(layer)                                   # the primary-URL store's manifest order
b = sorted(mirror.get(u, u) for u in layer)          # the mirror-URL store's
ia, ib = {u: i for i, u in enumerate(a)}, {u: i for i, u in enumerate(b)}
moved, kept = collections.Counter(), collections.Counter()
for u in a:
    (moved if ia[u] != ib[mirror.get(u, u)] else kept)[layer[u]] += 1
print(f"(2) manifest files {len(a)}; index changed {sum(moved.values())}, unchanged {sum(kept.values())}")
for l in sorted(set(moved) | set(kept)): print(f"    {l}: changed {moved[l]}, unchanged {kept[l]}")
print("    the 8 mirror urls take indices", sorted(ib[m] for m in mirror.values()))
