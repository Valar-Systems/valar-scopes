# SEA-STORE S-0 and S-1 (docs/basemap-dense-code.md, registered at 2891a937 and 8f2aeaaf), read-only on the cache:
#   S-0  every pinned file's sha256 equals its pin, else exit 3 (STOP before the ingest);
#   S-1  every fetched file's sha256 against the manifest of the store the prototype was built on (bm-obj/manifest.json), per layer:
#        same / changed / added / removed. Counts, and up to 10 changed URLs per layer (public dataset URLs, never a coordinate).
#   python -I -X utf8 seastore/s01.py <cache dir> <old manifest.json>
import json, os, sys
C, OLD = sys.argv[1], sys.argv[2]
def load(p):
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}
log = load(os.path.join(C, "fetch-log.json"))
for u, r in load(os.path.join(C, "retry-fetched.json")).items(): log.setdefault(u, r)
planned = load(os.path.join(C, "plan.json")).get("files", []) + load(os.path.join(C, "tiles.json")).get("files", [])
layer_of = {f["url"]: f["layer"] for f in planned}
PINS = {
    "https://naciscdn.org/naturalearth/10m/physical/ne_10m_ocean.zip": "db626fcd5d50",
    "https://naciscdn.org/naturalearth/10m/physical/ne_10m_lakes.zip": "0803a06f9c3c",
    "https://naciscdn.org/naturalearth/10m/cultural/ne_10m_admin_0_countries.zip": "ce1ac7036499",
    "https://naciscdn.org/naturalearth/10m/physical/ne_10m_land.zip": "e547d749445e",
}
s0 = {u.rsplit("/", 1)[1]: (log.get(u, {}).get("sha256", "MISSING")[:12], pin, log.get(u, {}).get("sha256", "").startswith(pin)) for u, pin in PINS.items()}
print(json.dumps({"S-0": {k: {"got": g, "pin": p, "ok": ok} for k, (g, p, ok) in s0.items()}, "ok": all(v[2] for v in s0.values())}))
old = {f["url"]: f for f in json.load(open(OLD, encoding="utf-8"))["files"]}
per = {}
def bump(layer, k, u=None):
    d = per.setdefault(layer, {"same": 0, "changed": 0, "added": 0, "removed": 0, "changed_urls": []})
    d[k] += 1
    if k == "changed" and len(d["changed_urls"]) < 10: d["changed_urls"].append(u)
missing = [u for u in layer_of if u not in log]
for u, layer in layer_of.items():
    if u not in log: continue
    if u not in old: bump(layer, "added")
    elif log[u]["sha256"] == old[u]["sha256"]: bump(layer, "same")
    else: bump(layer, "changed", u)
for u, f in old.items():
    if u not in layer_of: bump(f["layer"], "removed")
print(json.dumps({"S-1": per, "planned": len(layer_of), "fetched": sum(1 for u in layer_of if u in log), "planned_not_fetched": len(missing)}, indent=1))
sys.exit(0 if all(v[2] for v in s0.values()) else 3)
