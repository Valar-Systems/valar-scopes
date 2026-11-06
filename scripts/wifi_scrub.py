"""The Wi-Fi scrub (docs/wifi-scrub.md, item 1): replace each network name, BSSID and private LAN address with its fixed
placeholder, ONLY inside its context, using the GUARD's own patterns and line definition (scripts/check_wifi.py), so
the two cannot disagree about what a context or a line is. Allowlisted lines are left alone.

It holds patterns and placeholders only, never a value. The bench network's name is derived IN MEMORY from the logs it
reads; every network-name value must equal it, or the run is refused and nothing is written. With no name in the logs
(the tree after the scrub), every network-name finding is refused rather than replaced. Prints counts and path:line.

    python3 scripts/wifi_scrub.py                       scrub the working tree
    python3 scripts/wifi_scrub.py --rev <sha> --dry     read a git revision instead; write nothing
    python3 scripts/wifi_scrub.py ... --everywhere      S-W7's sabotage: also replace the name OUTSIDE any context

Exit 0 done (or nothing to do), 1 refused (nothing written), 2 rig.
"""
import collections
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_wifi as C  # noqa: E402

ROOT = C.ROOT
PH = {"name": "<ssid redacted>", "bssid": "<bssid redacted>", "lan": "<lan-ip redacted>"}
LOG_DIRS = ("bench-logs/", "tools/weatherscope/board/")
_ASSOC = re.compile(r'Associated with \\?"([^"\\]+)\\?"')
_BOM = b"\xef\xbb\xbf"


def read_tree(root=ROOT):
    """{path: (text, had_bom)} for every tracked file that decodes as UTF-8."""
    out = {}
    for rel in C.tracked(root) or []:
        try:
            raw = open(os.path.join(root, rel), "rb").read()
            out[rel] = (raw[3:].decode("utf-8") if raw.startswith(_BOM) else raw.decode("utf-8"), raw.startswith(_BOM))
        except (UnicodeDecodeError, OSError):
            continue
    return out


def read_rev(rev, root=ROOT):
    """The same, read from a git revision through one `git cat-file --batch`."""
    names = subprocess.run(["git", "-C", root, "ls-tree", "-r", "-z", "--name-only", rev], capture_output=True)
    if names.returncode != 0:
        return None
    paths = [p for p in names.stdout.decode("utf-8", "replace").split("\0") if p]
    batch = subprocess.run(["git", "-C", root, "cat-file", "--batch"], capture_output=True,
                           input="".join(f"{rev}:{p}\n" for p in paths).encode("utf-8"))
    data, at, out = batch.stdout, 0, {}
    for p in paths:
        nl = data.index(b"\n", at)
        header = data[at:nl].split()
        if len(header) != 3 or header[1] != b"blob":
            at = nl + 1
            continue
        size = int(header[2])
        raw = data[nl + 1:nl + 1 + size]
        at = nl + 1 + size + 1
        try:
            out[p] = (raw[3:].decode("utf-8") if raw.startswith(_BOM) else raw.decode("utf-8"), raw.startswith(_BOM))
        except UnicodeDecodeError:
            continue
    return out


def bench_names(texts):
    names = set()
    for rel, (t, _) in texts.items():
        if rel.startswith(LOG_DIRS):
            names |= {g for g in _ASSOC.findall(t) if not g.startswith("<")}
    return names


def plan(texts, allow, everywhere=False):
    """(new texts, counts, refused path:line list, the bench name or None). Pure: writes nothing."""
    names = bench_names(texts)
    if len(names) > 1:
        raise SystemExit(f"RIG: {len(names)} distinct names in the logs' association lines; expected at most one")
    name = next(iter(names), None)
    counts, refused, new = collections.Counter(), [], {}
    for rel, (t, _) in texts.items():
        parts, changed = t.split("\n"), False
        for i, seg in enumerate(parts):
            line = seg.rstrip("\r")              # the guard's line: every trailing CR stripped, restored on write
            tail = seg[len(line):]
            if (rel, C.line_hash(line)) in allow:
                continue
            spans = set()
            for rx in C.NAME.values():
                for m in rx.finditer(line):
                    g = next((k for k in range(1, m.re.groups + 1) if m.group(k) is not None), None)
                    v = m.group(g) if g else ""
                    if C.passes(v):
                        continue
                    if name is None or v != name:
                        refused.append(f"{rel}:{i + 1}")
                        continue
                    spans.add((m.start(g), m.end(g), "name"))
            for m in C.BSSID.finditer(line):
                spans.add((m.start(2), m.end(2), "bssid"))
            for m in C.PRIVATE.finditer(line):
                if m.group(1) != C.PORTAL:
                    spans.add((m.start(1), m.end(1), "lan"))
            if everywhere and name:
                for m in re.finditer(r"(?<![\w-])" + re.escape(name) + r"(?![\w-])", line):
                    if not any(a <= m.start() < b for a, b, _ in spans):
                        spans.add((m.start(), m.end(), "name"))
            if not spans:
                continue
            ordered = sorted(spans)
            for (_, b1, _), (a2, _, _) in zip(ordered, ordered[1:]):
                if b1 > a2:
                    raise SystemExit(f"RIG: overlapping spans at {rel}:{i + 1}")
            for a, b, cat in reversed(ordered):
                line = line[:a] + PH[cat] + line[b:]
                counts[cat] += 1
            parts[i], changed = line + tail, True
        if changed:
            new[rel] = "\n".join(parts)
    return new, counts, refused, name


def name_left(texts, new, name):
    """Where the bench name is left in the result, compared in memory: {path: count}."""
    if not name:
        return {}
    out = {}
    for rel, (t, _) in texts.items():
        n = new.get(rel, t).count(name)
        if n:
            out[rel] = n
    return out


def main(argv):
    args = argv[1:]
    rev = args[args.index("--rev") + 1] if "--rev" in args else None
    dry, everywhere = "--dry" in args or rev is not None, "--everywhere" in args
    allow_entries = C.read_allow(C.ALLOW)
    texts = read_rev(rev) if rev else read_tree()
    if allow_entries is None or not texts:
        print("RIG: the allowlist or the input could not be read"); return 2
    allow = {(p, h) for p, h, _ in allow_entries}
    new, counts, refused, name = plan(texts, allow, everywhere)
    if refused:
        print(f"REFUSED: {len(refused)} network-name value(s) are not the bench name"
              f"{' (there is none in these logs)' if name is None else ''}; nothing written:")
        for r in refused:
            print(f"    {r}")
        return 1
    if not dry:
        for rel, t in new.items():
            open(os.path.join(ROOT, rel), "wb").write((_BOM if texts[rel][1] else b"") + t.encode("utf-8"))
    left = name_left(texts, new, name)
    print(f"{'would replace' if dry else 'replaced'}: name {counts['name']}, bssid {counts['bssid']}, lan {counts['lan']}; "
          f"in {len(new)} files; by top dir {dict(collections.Counter(r.split('/')[0] for r in new))}")
    print(f"the bench name left afterwards: {sum(left.values())} in {sorted(left)}" if name else
          "no bench name in these logs")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
