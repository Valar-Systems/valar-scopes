# SCRATCH: the mirror switch's shared facts (the 10-28 ruling, docs/basemap-route.md L1167; MI4b). Read-only module.
import json, os
S = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); C = os.path.join(os.path.dirname(S), "bm-cache-qw4")
PRIMARY = "https://www2.census.gov/geo/tiger/"
MIRROR = "https://ftp2.census.gov/geo/tiger/"
WHY = "fetched from the Census Bureau's mirror; www2.census.gov's firewall refused it"
def eight():
    u = [l.split()[1].rstrip(":") for l in open(os.path.join(S, "seastore/logs/2-fetch.err"), encoding="utf-8") if l.startswith("FAILED ")]
    assert len(u) == 8 and all(x.startswith(PRIMARY + "TIGER2024/") for x in u), u
    return u
def mirror_of(u): return MIRROR + u[len(PRIMARY):]
def bm_records():
    return {f["url"]: f for f in json.load(open(os.path.join(S, "bm-obj/manifest.json"), encoding="utf-8"))["files"]}
