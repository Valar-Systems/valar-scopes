"""A12.33's check, run before ANY write to a board: print the address ranges a write will touch beside the partition
table, and refuse if any range touches a partition other than the one being written.

    python -I tools/weatherscope/board/flash_plan.py <partitions.csv> <target partition name> <offset> <file> [...]

Exit 0: every range lies inside the target partition. Exit 1: a range touches another partition (or the gap between
partitions, or runs past the target's end) -- do not write. Exit 2: usage. Prints no coordinate and no data.
"""
import os, sys


def table(path):
    rows = []
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        f = [x.strip() for x in line.split(",")]
        rows.append((f[0], int(f[3], 0), int(f[4], 0)))   # name, offset, size
    return rows


def main(argv):
    if len(argv) < 5 or (len(argv) - 3) % 2:
        print(__doc__)
        return 2
    parts = table(argv[1])
    target = argv[2]
    if target not in {n for n, _, _ in parts}:
        print(f"REFUSED: no partition named {target!r}")
        return 1
    print("the partition table:")
    for n, off, size in parts:
        print(f"  {n:10s} 0x{off:07X}-0x{off + size - 1:07X}{'   <- the target' if n == target else ''}")
    ok = True
    for i in range(3, len(argv), 2):
        off, f = int(argv[i], 0), argv[i + 1]
        size = os.path.getsize(f)
        lo, hi = off, off + size - 1
        touched = [n for n, po, ps in parts if lo <= po + ps - 1 and hi >= po]
        inside = [n for n, po, ps in parts if po <= lo and hi <= po + ps - 1]
        good = touched == [target] and inside == [target]
        ok &= good
        print(f"write 0x{lo:07X}-0x{hi:07X} ({size} B, {os.path.basename(f)}): touches {touched or ['no partition']}"
              f" -> {'ok' if good else 'REFUSED'}")
    print("flash plan: " + ("every range lies inside the target" if ok else "REFUSED -- do not write"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
