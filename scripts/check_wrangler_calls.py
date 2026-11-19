#!/usr/bin/env python3
"""Every in-tree wrangler call goes through proxy/scripts/wrangler-run.mjs (the owner, 2026-11-13 06:01Z).

wrangler 4.x appends every message it logs -- a KV key list, a secret's name, a device id in a key -- to a debug log
under %USERPROFILE%\\.wrangler\\logs, with no switch to turn it off; WRANGLER_LOG_PATH only moves it. The helper moves it
into a temp dir it creates for the one call and removes after (docs/wrangler-run.md). A call that does not go through
the helper writes the old way, so this refuses one, in every tracked file under scripts/, tools/, proxy/scripts/ and
.github/.

What counts as a direct call (a line whose code is not a comment):
  - `npx wrangler`, `bunx wrangler`, `pnpm wrangler`, `yarn wrangler`, `npm exec wrangler` (a shell line, a string);
  - "wrangler" as an argv element or a path part: a quoted `wrangler` followed by `,` or `]` (spawn arrays, and
    join(..., "wrangler", "bin", "wrangler.js"));
  - a path to wrangler's own entry point (`wrangler/bin/wrangler.js`, `wrangler-dist/cli.js`);
  - `wrangler <subcommand>` at a shell command position, or printed as a command for a person to type;
  - `uses: cloudflare/wrangler-action` in a workflow.
A line naming the helper (`wrangler-run.mjs`) passes. Comment lines pass: they cannot run. A line that only looks like
a call (a test string, say) passes only by an entry in scripts/wrangler-call-allow.txt: its path and the SHA-256 of
the stripped line, so the allowance never repeats the line and dies with it (a stale entry is refused).

  python3 scripts/check_wrangler_calls.py [--tree DIR]   exit 0 clean, 1 a direct call or a stale entry (file:line and
                                                         the pattern's name, never the line), 2 blind (nothing scanned)
  python3 scripts/check_wrangler_calls.py --selftest     each pattern caught in a planted file; the helper, comments
                                                         and an allowed line pass; a stale allowance is refused
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile

SCOPE = ("scripts/", "tools/", "proxy/scripts/", ".github/")
HELPER = "proxy/scripts/wrangler-run.mjs"
SELF = "scripts/check_wrangler_calls.py"
ALLOW = "scripts/wrangler-call-allow.txt"

# Built from pieces so that no line of THIS file matches its own patterns (it is in scope like any other).
_W = "wrang" + "ler"
PATTERNS = [
    ("runner", re.compile(r"\b(?:npx|bunx|pnpm(?:\s+exec)?|yarn|npm\s+exec)\s+(?:--?[\w-]+(?:=\S+)?\s+)*" + _W + r"\b")),
    ("argv", re.compile(r"""["']""" + _W + r"""["']\s*[,\]]""")),
    ("entry", re.compile(_W + r"""(?:[\\/]+|["']\s*,\s*["'])bin(?:[\\/]+|["']\s*,\s*["'])""" + _W + r"\.js|" + _W + r"-dist[\\/]+cli\.js")),
    ("command", re.compile(r"(?<![\w./@-])" + _W + r"\s+(?:kv|deploy|r2|d1|tail|secret|whoami|versions|deployments|rollback|login|logout|dev|pages|queues|types|vectorize|hyperdrive|workflows|containers|--version)\b")),
    ("action", re.compile(r"uses:\s*cloudflare/" + _W + r"-action")),
]
COMMENT = re.compile(r"^\s*(?:#|//|/\*|\*|<!--|rem\b|::)", re.I)


def line_hash(line):
    return hashlib.sha256(line.strip().encode("utf-8")).hexdigest()


def findings_in(text):
    """(line number, pattern name) for every direct call in one file's text."""
    out = []
    for i, line in enumerate(text.splitlines()):
        if COMMENT.match(line) or "wrangler-run.mjs" in line:
            continue
        for name, rx in PATTERNS:
            if rx.search(line):
                out.append((i + 1, name, line_hash(line)))
                break
    return out


def tracked(tree):
    out = subprocess.run(["git", "-C", tree, "ls-files", "-z", "--", *SCOPE], capture_output=True, check=True).stdout
    return [p for p in out.decode("utf-8").split("\0") if p and p not in (HELPER, SELF, ALLOW)]


def read_allow(tree):
    path = os.path.join(tree, ALLOW)
    if not os.path.exists(path):
        return []
    rows = []
    for raw in open(path, encoding="utf-8").read().splitlines():
        if raw.strip() and not raw.lstrip().startswith("#"):
            parts = raw.split("\t")
            if len(parts) >= 3 and re.fullmatch(r"[0-9a-f]{64}", parts[1]):
                rows.append((parts[0], parts[1]))
            else:
                rows.append((raw[:40], "malformed"))
    return rows


def judge(tree):
    files = tracked(tree)
    allow = read_allow(tree)
    used = {a: False for a in allow}
    found, seen = [], 0
    for rel in files:
        try:
            with open(os.path.join(tree, rel), "rb") as fh:
                raw = fh.read()
        except OSError:
            continue
        if b"\0" in raw[:4096]:
            continue
        text = raw.decode("utf-8", errors="replace")
        seen += text.count(_W)
        for n, name, h in findings_in(text):
            if (rel, h) in used:
                used[(rel, h)] = True
            else:
                found.append((rel, n, name))
    stale = [a for a, u in used.items() if not u]
    return files, seen, found, stale


def report(tree):
    files, seen, found, stale = judge(tree)
    for rel, n, name in found:
        print(f"DIRECT: {rel}:{n} ({name}) -- route it through {HELPER}")
    for rel, h in stale:
        print(f"STALE ALLOWANCE: {rel} {h[:12]} (no line has that hash any more, or the row is malformed)")
    print(f"wrangler calls: {len(files)} files in scope, {seen} mentions of the name; direct {len(found)}, "
          f"stale allowances {len(stale)}")
    if not files or seen == 0:
        print("BLIND: nothing in scope mentions it, so a clean result proves nothing")
        return 2
    return 1 if (found or stale) else 0


def selftest():
    bad = 0

    def ok(name, cond):
        nonlocal bad
        print(("PASS " if cond else "FAIL ") + name)
        bad += 0 if cond else 1

    w = _W
    planted = {
        "a.sh": f'out="$(npx {w} kv key list --binding ENRICH_KV --remote)"\n',
        "b.ts": f'const r = spawnSync(["npx", "{w}", "kv", "key", "get", k], {{ shell: true }});\n',
        "c.py": f'cmd = [npx(), "{w}", "deployments", "status"]\n',
        "d.mjs": f'const bin = join(PROXY, "node_modules", "{w}", "bin", "{w}.js");\n',
        "e.sh": f'{w} tail --env production\n',
        "f.yml": f"      - uses: cloudflare/{w}-action@v3\n",
        "g.sh": f'echo "  Set it first:  {w} secret put X --env staging"\n',
    }
    clean = {
        "h.sh": f'node scripts/{w}-run.mjs kv key list --binding ENRICH_KV --remote\n',
        "i.ts": f'// npx {w} kv key list is what this replaces\n',
        "j.py": f'# "{w}", "kv" -- a comment\nprint("the {w} log moved")\n',
    }
    for name, text in planted.items():
        ok(f"W-{name[0]} a planted direct call is caught ({name})", len(findings_in(text)) == 1)
    for name, text in clean.items():
        ok(f"W-{name[0]} control: not a direct call ({name})", findings_in(text) == [])
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(["git", "init", "-q", d], check=True)
        os.makedirs(os.path.join(d, "scripts"))
        os.makedirs(os.path.join(d, "proxy", "scripts"))
        with open(os.path.join(d, "scripts", "x.sh"), "w", encoding="utf-8") as fh:
            fh.write("#!/bin/sh\n" + planted["a.sh"] + clean["h.sh"])
        with open(os.path.join(d, HELPER), "w", encoding="utf-8") as fh:
            fh.write(planted["d.mjs"])
        with open(os.path.join(d, "outside.sh"), "w", encoding="utf-8") as fh:
            fh.write(planted["e.sh"])
        subprocess.run(["git", "-C", d, "add", "-A"], check=True)
        _, _, found, _ = judge(d)
        ok("W-tree a tracked planted call is found; the helper itself and out-of-scope files are not",
           found == [("scripts/x.sh", 2, "runner")])
        with open(os.path.join(d, ALLOW), "w", encoding="utf-8") as fh:
            fh.write(f"scripts/x.sh\t{line_hash(planted['a.sh'])}\ta test string\n")
        subprocess.run(["git", "-C", d, "add", "-A"], check=True)
        _, _, found, stale = judge(d)
        ok("W-allow an allowed line passes, and the allowance is not stale", found == [] and stale == [])
        with open(os.path.join(d, "scripts", "x.sh"), "w", encoding="utf-8") as fh:
            fh.write("#!/bin/sh\n" + clean["h.sh"])
        subprocess.run(["git", "-C", d, "add", "-A"], check=True)
        _, _, found, stale = judge(d)
        ok("W-stale an allowance whose line is gone is refused", found == [] and len(stale) == 1)
    print("SELFTEST PASSED" if bad == 0 else f"SELFTEST FAILED: {bad}")
    return 0 if bad == 0 else 1


def main(argv):
    if argv[:1] == ["--selftest"]:
        return selftest()
    tree = argv[argv.index("--tree") + 1] if "--tree" in argv else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return report(tree)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
