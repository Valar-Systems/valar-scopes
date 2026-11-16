# SCRATCH (map-server), READ-ONLY: step8_26.pred Amendment 5a.1's conhost record, written before the bgcheck end check.
# Lists every conhost.exe whose parent pid is one of the given Windows pids, with its pid, parent pid and creation time (UTC),
# and prints each given pid's own creation time if it is still running. Names, pids and times only.
#   python -X utf8 -I mirror/conhosts.py <winpid> [<winpid> ...]
import datetime, sys
import psutil

utc = lambda t: datetime.datetime.fromtimestamp(t, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def main():
    want = [int(a) for a in sys.argv[1:]]
    for w in want:
        try:
            p = psutil.Process(w)
            print(f"[conhost] pid {w} {p.name()}: running, created {utc(p.create_time())}")
        except psutil.NoSuchProcess:
            print(f"[conhost] pid {w}: not running")
    n = 0
    for p in psutil.process_iter(["pid", "ppid", "name", "create_time"]):
        i = p.info
        if (i["name"] or "").lower() == "conhost.exe" and i["ppid"] in want:
            n += 1
            print(f"[conhost] conhost.exe pid {i['pid']} parent {i['ppid']} created {utc(i['create_time'])}")
    print(f"[conhost] {n} conhost.exe under {want}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
