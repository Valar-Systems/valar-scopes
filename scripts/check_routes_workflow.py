#!/usr/bin/env python3
"""check_routes_workflow.py -- W10 (docs/route-weekly-code.md, section 2f): the owner's conditions on the routes-refresh
workflow, checked statically over .github/workflows/ in CI (device-ids.yml), so a later edit cannot quietly undo them.

  SC1  routes-refresh.yml's triggers are only workflow_dispatch and schedule (no PR or fork trigger can reach it);
       no OTHER workflow names `environment: routes-refresh`; no workflow names ROUTES_KV_CLI (FK1's fake KV).
  SC3  no run: text in routes-refresh.yml contains `secrets.`, `set -x` or `printenv` (nothing echoes a secret).
  SC5  every secrets.<NAME> in routes-refresh.yml is a ROUTES_ name.
  PIN1 every uses: is owner/repo@<40 hex>, with an optional "# vN" comment.
  NPM1 nowhere in routes-refresh.yml: npm install, npm i, yarn, pnpm, pip install, or npx without --no-install
       (stricter than the row, which is simpler to check: the token job installs only from the lockfile).

  python3 scripts/check_routes_workflow.py              exit 0 clean, 1 findings (path:line row), 2 no such workflow
  python3 scripts/check_routes_workflow.py --selftest   each plant must read red, the real file green, and each
                                                        sabotage must turn exactly its own plant green
Stdlib only and line-based: no YAML library to be missing on a runner, and every rule is a line a person can read.
"""
import pathlib
import re
import sys

WF_DIR = pathlib.Path(".github/workflows")
TARGET = "routes-refresh.yml"
ALLOWED_TRIGGERS = {"workflow_dispatch", "schedule"}
PIN = re.compile(r"^[\w.-]+/[\w.-]+(/[\w./-]+)?@[0-9a-f]{40}(\s+#.*)?$")
INSTALLERS = [re.compile(p) for p in (r"\bnpm\s+install\b", r"\bnpm\s+i\b", r"\byarn\b", r"\bpnpm\b", r"\bpip3?\s+install\b",
                                      r"\bnpx\b(?![^\n]*--no-install)")]
ENV_NAMED = re.compile(r"^\s*environment:\s*['\"]?routes-refresh['\"]?\s*$|environment:\s*\n\s+name:\s*['\"]?routes-refresh", re.M)


def code_lines(text):
    """(line number, line) for every line that is not a full-line comment."""
    return [(i + 1, l) for i, l in enumerate(text.splitlines()) if not l.lstrip().startswith("#")]


def triggers(text):
    m = re.search(r"^on:[ \t]*(\S.*)?$", text, re.M)  # [ \t], never \s: \s crosses the newline into the block
    if not m:
        return set()
    inline = (m.group(1) or "").strip()
    if inline and not inline.startswith("#"):
        return {t.strip() for t in inline.strip("[]").split(",") if t.strip()}
    out = set()
    for line in text[m.end():].splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if not line.startswith(" "):
            break
        k = re.match(r"^  ([A-Za-z_]+):", line)
        if k:
            out.add(k.group(1))
    return out


def run_lines(text):
    """(line number, text) of every run: value, block scalars included."""
    out, lines, i = [], text.splitlines(), 0
    while i < len(lines):
        m = re.match(r"^(\s*)(-\s+)?run:\s*(.*)$", lines[i])
        if m and not lines[i].lstrip().startswith("#"):
            indent = len(m.group(1)) + len(m.group(2) or "")
            val = m.group(3)
            if val in ("|", ">", "|-", ">-", ""):
                j = i + 1
                while j < len(lines) and (not lines[j].strip() or len(lines[j]) - len(lines[j].lstrip()) > indent):
                    out.append((j + 1, lines[j]))
                    j += 1
                i = j
                continue
            out.append((i + 1, val))
        i += 1
    return out


def check_target(text, sab=None):
    f = []
    for t in sorted(triggers(text)):
        if sab != "sc1" and t not in ALLOWED_TRIGGERS:
            f.append(("SC1t", 0, f"trigger {t}"))
    scan = run_lines(text) if sab != "sc3" else []  # the sabotage: the scan sees no run: line at all
    for n, l in scan:
        if "secrets." in l or "set -x" in l or "printenv" in l:
            f.append(("SC3e", n, "a run: line that can echo a secret"))
    for n, l in code_lines(text):
        for name in re.findall(r"secrets\.([A-Za-z0-9_]+)", l):
            if sab != "sc5" and not name.startswith("ROUTES_"):
                f.append(("SC5", n, f"secrets.{name}"))
        u = re.match(r"^\s*(-\s+)?uses:\s*(\S.*)$", l)
        if u and not PIN.match(u.group(2).strip()):
            f.append(("PIN1", n, "uses: not pinned by full sha"))
        if any(p.search(l) for p in INSTALLERS) and not re.search(r"\bnpm\s+ci\b", l):
            f.append(("NPM1", n, "an install that is not npm ci"))
    return f


def check_all(files, sab=None):
    """files: {name: text}. Findings as (row, path, line, what)."""
    out = []
    if TARGET not in files:
        return None
    out += [(r, TARGET, n, w) for r, n, w in check_target(files[TARGET], sab)]
    for name, text in sorted(files.items()):
        if name != TARGET and ENV_NAMED.search(text):
            out.append(("SC1e", name, 0, "another workflow names environment: routes-refresh"))
        for n, l in code_lines(text):
            if "ROUTES_KV_CLI" in l:
                out.append(("SC1k", name, n, "a workflow names ROUTES_KV_CLI (FK1's fake KV)"))
    return out


def read_tree():
    return {p.name: p.read_text(encoding="utf-8") for p in sorted(WF_DIR.glob("*.yml"))}


def selftest():
    files = read_tree()
    if TARGET not in files:
        print(f"SELFTEST: no {TARGET} to plant into")
        return 1
    real = files[TARGET]
    off = 0

    def row(rid, ok, detail):
        nonlocal off
        print(f"{'PASS' if ok else 'FAIL'} {rid}: {detail}")
        off += 0 if ok else 1

    def rows_of(fs, sab=None):
        got = check_all(fs, sab)
        return set() if got is None else {r for r, *_ in got}

    def plant(old, new):
        assert real.count(old) >= 1, f"plant anchor missing: {old!r}"
        return real.replace(old, new, 1)

    on_block = "  workflow_dispatch:\n"
    step_env = "          GH_TOKEN: ${{ github.token }}\n"
    first_run = "        run: npm ci\n"
    plants = {
        "SC1t": {TARGET: plant(on_block, "  pull_request_target:\n" + on_block)},
        "SC1e": {TARGET: real, "other.yml": "name: other\non:\n  push:\njobs:\n  x:\n    runs-on: ubuntu-24.04\n    environment: routes-refresh\n    steps:\n      - run: echo hi\n"},
        "SC1k": {TARGET: plant(step_env, step_env + "          ROUTES_KV_CLI: scripts/routes-fakekv.mjs\n")},
        "SC3e": {TARGET: plant(first_run, "        run: echo ${{ secrets.ROUTES_KV_TOKEN }}\n")},
        "SC3x": {TARGET: plant(first_run, "        run: set -x; npm ci\n")},
        "SC3p": {TARGET: plant(first_run, "        run: printenv\n")},
        "SC5": {TARGET: plant(step_env, step_env + "          CF: ${{ secrets.CLOUDFLARE_API_TOKEN }}\n")},
        "PIN1": {TARGET: plant("actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4", "actions/checkout@v4")},
        "NPM1": {TARGET: plant(first_run, "        run: npm install\n")},
        "NPM1x": {TARGET: plant(first_run, "        run: npx some-pkg\n")},
    }
    want = {"SC1t": "SC1t", "SC1e": "SC1e", "SC1k": "SC1k", "SC3e": "SC3e", "SC3x": "SC3e", "SC3p": "SC3e",
            "SC5": "SC5", "PIN1": "PIN1", "NPM1": "NPM1", "NPM1x": "NPM1"}
    for rid, fs in plants.items():
        got = rows_of({**files, **fs})
        row(rid, want[rid] in got, f"the plant reads {sorted(got) or 'clean'} (must include {want[rid]})")
    real_rows = rows_of(files)
    row("CTL", real_rows == set(), f"(control) the real workflows read {sorted(real_rows) or 'clean'}")
    # Each sabotage weakens one check: exactly its own plant must then read green, and the others stay red.
    # S3 was registered as "the scan limited to env: blocks", which cannot work: env blocks legitimately map
    # ${{ secrets.ROUTES_* }}, so that scan flags everything. Re-registered before its run: the scan sees no run: line.
    for sab, turn, label in (("sc1", {"SC1t"}, "S1"), ("sc3", {"SC3e", "SC3x", "SC3p"}, "S3"), ("sc5", {"SC5"}, "S5")):
        still_red = {p for p in plants if want[p] in rows_of({**files, **plants[p]}, sab)}
        turned = set(plants) - still_red
        row(label, turned == turn, f"sabotage {sab}: plants turned green {sorted(turned)} (must be exactly {sorted(turn)})")
    print("SELFTEST PASSED" if off == 0 else f"SELFTEST: {off} row(s) off")
    return 0 if off == 0 else 1


def main():
    if "--selftest" in sys.argv:
        return selftest()
    got = check_all(read_tree())
    if got is None:
        print(f"routes workflow: no {TARGET}")
        return 2
    for r, path, n, what in got:
        print(f"{WF_DIR / path}:{n}: {r}: {what}")
    print(f"routes workflow: {'OK' if not got else f'{len(got)} finding(s)'} (SC1, SC3, SC5, PIN1, NPM1)")
    return 0 if not got else 1


if __name__ == "__main__":
    sys.exit(main())
