# SCRATCH: the rehearsal helper for rehearse_n6.sh (switch_d2.py against switch_d.py, P1-P3). It builds COPIES only, under
# mirror/rehearse-n6/<case>/; the real cache is only READ (its plan, fetch-log, retry-fetched and the 2 primary-fetched files).
#   base <case> B0|B2        a copy: plan.json + fetch-log.json; B2 also retry-fetched.json (byte copy) and the 2 primary files
#   synth <case> <code...>   SYNTHETIC mirror files + their retry records, as the ingest's retry would write them (code = county)
#   oldpost <case>           old apply's end state by hand (old apply refuses B2): the 8 to the mirror, the original plan kept
#   planedit <case> mirror|unmirror <code>     a forged plan edit after apply
#   forge-unplanned <case>   a record + matching file for an UNPLANNED www2 url (tl_2024_99999_areawater)
#   forge-fakeprimary <case> <code>            a primary record (64-hex sha) for one of the 8, with NO file
#   drop-log <case> <code>   removes a planned, fetched url from the copy's fetch-log.json
#   tamper|untamper <case> <code>              one primary-path file: append X (bytes kept) / restore by bytes, sha checked
#   p1 <case>                the 2 primary-fetched files' and records' sha256 (P1: before == after)
#   state <case>             plan, retry-fetched, fetch-log sha; ftp2 records; primary files of the 8; mirror files; kept files
import hashlib, json, os, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import C as REAL, MIRROR, PRIMARY, eight, mirror_of

sha = lambda b: hashlib.sha256(b).hexdigest()
dump = lambda d: (json.dumps(d, indent=1, sort_keys=True) + "\n").encode()
E = eight()
P2 = [u for u in E if u.endswith(("27081_linearwater.zip", "54093_linearwater.zip"))]
cmd, case = sys.argv[1], sys.argv[2]
cc = os.path.join(case, "cache")
keep = os.path.join(case, "mirror-d-keep")
local = lambda root, u: os.path.join(root, "files", *u.split("://")[1].split("/"))
plan_urls = lambda root: [f["url"] for f in json.load(open(os.path.join(root, "plan.json"), encoding="utf-8"))["files"]]


def url_of(code):  # a county code of the 8 ("38021"), or a full stem for any planned file ("26127_linearwater")
    hit = [u for u in E if f"_{code}_" in u] or [u for u in plan_urls(REAL) if u.endswith(f"_{code}.zip") and u.startswith(PRIMARY)]
    assert len(hit) == 1, (code, hit)
    return hit[0]


def records():
    p = os.path.join(cc, "retry-fetched.json")
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}


def put_records(r):
    open(os.path.join(cc, "retry-fetched.json"), "wb").write(dump(r))


def write_file(u, b):
    p = local(cc, u)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "wb").write(b)
    return {"bytes": len(b), "lastModified": None, "sha256": sha(b), "vintage": "2024"}


if cmd == "base":
    os.makedirs(cc)
    for f in ("plan.json", "fetch-log.json"):
        shutil.copyfile(os.path.join(REAL, f), os.path.join(cc, f))
    if sys.argv[3] == "B2":
        shutil.copyfile(os.path.join(REAL, "retry-fetched.json"), os.path.join(cc, "retry-fetched.json"))
        for u in P2:
            os.makedirs(os.path.dirname(local(cc, u)), exist_ok=True)
            shutil.copyfile(local(REAL, u), local(cc, u))
    print(f"base {os.path.basename(case)} {sys.argv[3]}: plan {sha(open(os.path.join(cc, 'plan.json'), 'rb').read())[:12]}")
elif cmd == "synth":
    r = records()
    for code in sys.argv[3:]:
        m = mirror_of(url_of(code))
        r[m] = write_file(m, bytes([0x50, 0x4B, 0x03, 0x04]) + ("SYNTHETIC " + m).encode())
    put_records(r)
    print(f"synth {os.path.basename(case)}: {len(sys.argv) - 3} synthetic mirror files and records")
elif cmd == "oldpost":
    src = open(os.path.join(cc, "plan.json"), "rb").read()
    os.makedirs(keep)
    open(os.path.join(keep, f"plan.original.{sha(src)[:12]}.json"), "wb").write(src)
    d = json.loads(src)
    for f in d["files"]:
        if f["url"] in E:
            f["url"] = mirror_of(f["url"])
    open(os.path.join(cc, "plan.json"), "wb").write(dump(d))
    print(f"oldpost {os.path.basename(case)}: plan {sha(src)[:12]} -> {sha(dump(d))[:12]} by hand (8 to the mirror)")
elif cmd == "planedit":
    how, u = sys.argv[3], url_of(sys.argv[4])
    p = os.path.join(cc, "plan.json")
    d = json.load(open(p, encoding="utf-8"))
    n = 0
    for f in d["files"]:
        if how == "mirror" and f["url"] == u:
            f["url"], n = mirror_of(u), n + 1
        if how == "unmirror" and f["url"] == mirror_of(u):
            f["url"], n = u, n + 1
    assert n == 1, n
    open(p, "wb").write(dump(d))
    print(f"planedit {os.path.basename(case)}: {how} {u.rsplit('/', 1)[1]}")
elif cmd == "forge-unplanned":
    u = PRIMARY + "TIGER2024/AREAWATER/tl_2024_99999_areawater.zip"
    assert u not in plan_urls(cc)
    r = records()
    r[u] = write_file(u, bytes([0x50, 0x4B, 0x03, 0x04]) + ("FORGED " + u).encode())
    put_records(r)
    print(f"forge-unplanned {os.path.basename(case)}: a record + file for {u.rsplit('/', 1)[1]}")
elif cmd == "forge-fakeprimary":
    u = url_of(sys.argv[3])
    r = records()
    r[u] = {"bytes": 1, "lastModified": None, "sha256": "0" * 64, "vintage": "2024"}
    put_records(r)
    print(f"forge-fakeprimary {os.path.basename(case)}: a primary record for {u.rsplit('/', 1)[1]}, no file")
elif cmd == "drop-log":
    u = url_of(sys.argv[3])
    p = os.path.join(cc, "fetch-log.json")
    d = json.load(open(p, encoding="utf-8"))
    assert u in d
    del d[u]
    open(p, "wb").write(dump(d))
    print(f"drop-log {os.path.basename(case)}: {u.rsplit('/', 1)[1]} removed from the copy's fetch-log")
elif cmd in ("tamper", "untamper"):
    p = local(cc, url_of(sys.argv[3]))
    if cmd == "tamper":
        b = open(p, "rb").read()
        open(p + ".keep", "wb").write(b)
        open(p, "ab").write(b"X")
        print(f"tamper {os.path.basename(case)}: {os.path.basename(p)} {sha(b)[:12]} -> {sha(open(p, 'rb').read())[:12]}")
    else:
        b = open(p + ".keep", "rb").read()
        open(p, "wb").write(b)
        assert sha(open(p, "rb").read()) == sha(b)
        os.remove(p + ".keep")
        print(f"untamper {os.path.basename(case)}: {os.path.basename(p)} back to {sha(b)[:12]}")
elif cmd == "p1":
    r = records()
    out = []
    for u in P2:
        f = sha(open(local(cc, u), "rb").read())[:12] if os.path.exists(local(cc, u)) else "absent"
        out.append(f"{u.rsplit('/', 1)[1]} file {f} record {sha(json.dumps(r.get(u), sort_keys=True).encode())[:12]}")
    print(f"P1 {os.path.basename(case)}: " + "; ".join(out))
elif cmd == "state":
    h = lambda f: sha(open(os.path.join(cc, f), "rb").read())[:12] if os.path.exists(os.path.join(cc, f)) else "absent"
    r = records()
    mroot = os.path.join(cc, "files", "ftp2.census.gov")
    nm = sum(len(fs) for _, _, fs in os.walk(mroot)) if os.path.isdir(mroot) else 0
    print(f"state {os.path.basename(case)}: plan {h('plan.json')}, retry-fetched {h('retry-fetched.json')}, fetch-log {h('fetch-log.json')}; "
          f"ftp2 records {sum(1 for k in r if k.startswith(MIRROR))}; primary files of the 8 {sum(os.path.exists(local(cc, u)) for u in E)}; "
          f"mirror files {nm}; kept {len(os.listdir(keep)) if os.path.isdir(keep) else 0}")
else:
    sys.exit(f"unknown command {cmd}")
