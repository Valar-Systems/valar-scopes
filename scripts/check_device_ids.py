#!/usr/bin/env python3
"""Refuse a real device id in the tracked tree.

THE CI HALF OF .githooks/pre-commit, AND IT SCANS THE TREE AT HEAD -- NOT
HISTORY. That is deliberate and is the difference between a control and a
permanently-red check nobody looks at:

  * two commits pushed on 2026-08-31 carry a real id and were DELIBERATELY left
    alone rather than rewritten, because rewriting public history that other
    clones and review links point at costs more than the exposure does;
  * a history-wide grep would therefore fail forever, and a control that can
    never pass is a control that gets disabled.

So: the hook gives fast local feedback on what you are about to add, and this
gives the guarantee about what the tree currently says. Neither replaces the
other -- core.hooksPath is opt-in per clone, so the hook is a convenience for
whoever enables it and cannot be a guarantee about anything.

A device id is not a credential (the key is HMAC(DEVICE_KEY_SECRET, id) and the
secret does the work). The cost of one in the tree is that it becomes a record
somebody acts on later -- an example annotation carrying a real id cost an hour
of genuine alarm mid-incident on 2026-08-31 -- plus a privacy surface once the
ids belong to customers rather than to a bench.
"""
import re
import subprocess
import sys

# The efuse-derived device-id shape. Word boundaries mean a 40-char git SHA
# cannot match: there is no boundary inside a longer hex run.
ID = re.compile(rb"(?<![0-9a-zA-Z_])[0-9a-f]{16}(?![0-9a-zA-Z_])")

ALLOWLIST = "scripts/device-id-allowlist.txt"


def allowed() -> set:
    """The allowlist is a FILE, read by this and by .githooks/pre-commit.

    They had separate inline lists for about four minutes on 2026-08-31, and the
    hook promptly refused a commit this check considered clean. Two guards on one
    rule is two rules, and the second one is always the stale one.
    """
    out = set()
    with open(ALLOWLIST, "rb") as fh:
        for line in fh:
            tok = line.split(b"#")[0].strip().lower()
            if tok:
                out.add(tok)
    return out

# Binary and vendored paths carry hex that is not a device id and is not ours.
SKIP_PREFIX = ("node_modules/", ".pio/", "dist/", "bench-logs/")
SKIP_SUFFIX = (".png", ".jpg", ".jpeg", ".gif", ".ico", ".bin", ".elf", ".zip",
               ".woff", ".woff2", ".ttf", ".pdf", ".lock")


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z"], capture_output=True, check=True)
    return [p for p in out.stdout.split(b"\0") if p]


def main() -> int:
    allow = allowed()
    bad = []
    for raw in tracked_files():
        path = raw.decode("utf-8", "replace")
        if path.startswith(SKIP_PREFIX) or path.endswith(SKIP_SUFFIX):
            continue
        blob = subprocess.run(["git", "show", f"HEAD:{path}"],
                              capture_output=True)
        if blob.returncode != 0:
            continue
        if b"\0" in blob.stdout[:8000]:
            continue  # binary
        hits = {m for m in ID.findall(blob.stdout) if m not in allow}
        for h in sorted(hits):
            bad.append((path, h.decode()))

    if not bad:
        print(f"OK: {len(tracked_files())} tracked files, no real device id at HEAD")
        return 0

    print("FAIL: a real-looking device id is in the tree at HEAD\n")
    for path, h in bad:
        print(f"  {path}: {h}")
    print("""
Use an obviously-fake id and label it:

    # EXAMPLE -- not a real device
    0000000000000000

Allowed ids live in scripts/device-id-allowlist.txt.
The enrolment registry (enr:dev:*) is the source of truth for real ids; a
document is not, and cannot be kept current.""")
    return 1


if __name__ == "__main__":
    sys.exit(main())
