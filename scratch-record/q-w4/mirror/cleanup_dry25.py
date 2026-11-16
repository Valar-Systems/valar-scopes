# SCRATCH (map-server): after diskwatch.sh KILLS a dry run (which skips its own cleanup), remove ONLY that run's output dir
# (<cache>/dry-run: its work files and its partial store), then read back that the cache's inputs are unchanged.
#   python -X utf8 -I cleanup_dry25.py [--cache <copy>]
# Exit: 0 cleaned and read back; 3 refused (a reparse point inside, or a guard); 5 a read-back mismatch.
import hashlib, json, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REAL_CACHE = os.path.join(os.path.dirname(os.path.dirname(HERE)), "bm-cache-qw4")
RECORD = "C:/Github/_handoffs/map-server/census-2025/record.json"
PLAN, RETRY = "3d7437c8e1b0", "8e421f533094"


def arg(n, d=None):
    a = sys.argv[1:]
    return a[a.index(n) + 1] if n in a else d


def sha(p):
    with open(p, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    cache = arg("--cache", REAL_CACHE)
    real = os.path.abspath(cache) == os.path.abspath(REAL_CACHE)
    target = os.path.join(cache, "dry-run")
    if real:
        ps = subprocess.run(["powershell", "-NoProfile", "-Command",
                             "@(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'ingest-basemap' -and "
                             "$_.CommandLine -notmatch 'Get-CimInstance' }).Count"], capture_output=True, text=True)
        if ps.returncode != 0 or ps.stdout.strip() != "0":
            print(f"refused: {ps.stdout.strip()!r} ingest-basemap processes still running"); return 3
    if os.path.exists(target):
        rp = subprocess.run(["powershell", "-NoProfile", "-Command",
                             f"@(Get-ChildItem -LiteralPath '{target}' -Force -Recurse -Attributes ReparsePoint -ErrorAction Stop).Count"],
                            capture_output=True, text=True)
        if rp.returncode != 0 or rp.stdout.strip() != "0":
            print(f"refused: {target} holds reparse point(s) ({rp.stdout.strip()!r}, exit {rp.returncode}); nothing removed"); return 3
        n = sum(len(fs) for _, _, fs in os.walk(target))
        shutil.rmtree(target)
        print(f"cleanup: removed the run's own dry-run dir ({n} files)")
    else:
        print("cleanup: no dry-run dir to remove")
    bad = []
    if os.path.exists(target): bad.append("dry-run still exists")
    if sha(os.path.join(cache, "plan.json"))[:12] != PLAN: bad.append("plan.json moved")
    if sha(os.path.join(cache, "retry-fetched.json"))[:12] != RETRY: bad.append("retry-fetched.json moved")
    rec = json.load(open(RECORD, encoding="utf-8"))
    for f in rec["files"]:
        p = os.path.join(cache, "files", *f["url"].split("://", 1)[1].split("/"))
        if not os.path.exists(p) or sha(p) != f["sha256"]: bad.append(f"{os.path.basename(p)} moved")
    du = shutil.disk_usage("C:/")
    print(f"read back: {'OK' if not bad else 'MISMATCH ' + str(bad)}; plan {PLAN}, retry {RETRY}, six = record; C: free {du.free} B ({du.free / 2**30:.2f} GiB)")
    return 0 if not bad else 5


if __name__ == "__main__":
    sys.exit(main())
