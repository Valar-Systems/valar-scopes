# SCRATCH (map-server): step8_25.pred's M-4 tool (sha 1612fcf2e8ef). It strips the six TIGER2025 notes from the placed plan for
# M-4's one control dry run, then restores the placed plan by its bytes. Nothing else in the plan changes.
#   python -X utf8 -I m4_25.py strip   [--cache <copy> --keep <dir>]
#   python -X utf8 -I m4_25.py restore [--cache <copy> --keep <dir>]
# Exit: 0 done; 2 bad args; 3 refused (the plan is not in the state the stage needs; nothing written).
import hashlib, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REAL_CACHE = os.path.join(os.path.dirname(os.path.dirname(HERE)), "bm-cache-qw4")
PLACED = "3d7437c8e1b0"
T25 = "https://www2.census.gov/geo/tiger/TIGER2025/"


def arg(n, d=None):
    a = sys.argv[1:]
    return a[a.index(n) + 1] if n in a else d


def sha(b):
    return hashlib.sha256(b).hexdigest()


def rd(p):
    with open(p, "rb") as f:
        return f.read()


def dump(o):
    return (json.dumps(o, indent=1, ensure_ascii=False) + "\n").encode("utf-8")


def write_back(p, b):
    with open(p, "wb") as f:
        f.write(b)
    if rd(p) != b:
        raise SystemExit(f"read-back differs: {p}")


def stripped(placed):
    o = json.loads(placed)
    noted = [i for i, f in enumerate(o["files"]) if "note" in f]
    if len(noted) != 6 or not all(o["files"][i]["url"].startswith(T25) for i in noted):
        return None
    for i in noted:
        o["files"][i] = {k: v for k, v in o["files"][i].items() if k != "note"}
    return dump(o)


def main():
    stage = sys.argv[1] if len(sys.argv) > 1 else ""
    cache, keep = arg("--cache", REAL_CACHE), arg("--keep", HERE)
    real = os.path.abspath(cache) == os.path.abspath(REAL_CACHE)
    if real != (os.path.abspath(keep) == os.path.abspath(HERE)):
        print("refused: the real cache keeps in mirror/, a copy keeps elsewhere"); return 2
    pp, kp = os.path.join(cache, "plan.json"), os.path.join(keep, f"plan.placed.{PLACED}.json")
    cur = rd(pp)
    if stage == "strip":
        if sha(cur)[:12] != PLACED:
            print(f"refused: plan.json is {sha(cur)[:12]}, not the placed {PLACED}; nothing written"); return 3
        s = stripped(cur)
        if s is None:
            print("refused: the placed plan does not hold exactly six notes, all on TIGER2025 urls; nothing written"); return 3
        if os.path.exists(kp) and rd(kp) != cur:
            print(f"refused: {os.path.basename(kp)} exists and differs; nothing written"); return 3
        write_back(kp, cur)
        write_back(pp, s)
        print(f"m4-strip: plan.json {PLACED} -> {sha(s)[:12]} (the six notes stripped, nothing else); placed plan kept as {os.path.basename(kp)}")
        return 0
    if stage == "restore":
        if not os.path.exists(kp) or sha(rd(kp))[:12] != PLACED:
            print("refused: the kept placed plan is missing or not 3d7437c8e1b0; nothing written"); return 3
        if cur != stripped(rd(kp)):
            print("refused: plan.json is not the stripped form of the kept placed plan; nothing written"); return 3
        write_back(pp, rd(kp))
        print(f"m4-restore: plan.json {sha(cur)[:12]} -> {sha(rd(pp))[:12]} (the placed plan, by its kept bytes)")
        return 0
    print("stage: strip | restore"); return 2


if __name__ == "__main__":
    sys.exit(main())
