#!/usr/bin/env python3
"""The release refuses to cut without a customer "What's new" entry (docs/v17-whats-new.md).

  check-customer-entry.py                 the gate: FW_VERSION has a valid entry whose lines map to merged work
  check-customer-entry.py --check-wiring  the gate is still in front of `build` in firmware.yml
  check-customer-entry.py --selftest      every violation, planted, is caught

THE GATE. docs/CHANGES-customer.md must have a `url:` line and a `## v<FW_VERSION>` section with
`notify: yes|no` and 1-4 summary lines of at most 24 characters each (refs not counted). Every
summary line carries `(refs: ...)`, and for the version being cut every ref must resolve to work
MERGED on main:
  #NNN        a first-parent commit "Merge pull request #NNN from ..." or a squash commit whose
              subject ends "(#NNN)";
  spec:<doc>  docs/<doc>.md exists and its status names the merged PR(s) that built it ("Built in
              #NNN"), each resolving as above;
  pending     only for versions OTHER than the one being cut.
So a line whose item slipped cannot reach an image: its line has to come out before the cut. Other
versions' sections are format-checked only.

Read from git, so history must be complete: a shallow clone is BLIND (exit 2), never a pass -- a
check that cannot see the merges would otherwise report every ref as unmerged, or worse, be relaxed.

EXIT: 0 ok   1 a violation   2 blind (cannot judge)
"""
import os
import re
import subprocess
import sys
import tempfile

FW_RE = re.compile(r"^\s*constexpr\s+int\s+FW_VERSION\s*=\s*([0-9]+)\s*;", re.M)
SUMMARY_MAX_LINES, SUMMARY_MAX_CHARS = 4, 24


class Blind(Exception):
    pass


def fw_version(root):
    src = open(os.path.join(root, "src", "OtaUpdater.h"), encoding="utf-8").read()
    found = FW_RE.findall(src)
    if len(found) != 1:  # the same rule as firmware.yml's version job: exactly one declaration
        raise Blind(f"expected exactly one 'constexpr int FW_VERSION' declaration, found {len(found)}")
    return int(found[0])


def parse(text):
    """-> (url, [section]); section = dict(version, notify, summary=[(line, refs)], body, errors)."""
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    url = None
    m = re.search(r"^url:\s*(\S+)\s*$", text, re.M)
    if m:
        url = m.group(1)
    sections, cur = [], None
    for raw in text.splitlines():
        h = re.match(r"^## v(\d+)\s*$", raw)
        if h:
            cur = {"version": int(h.group(1)), "lines": []}
            sections.append(cur)
        elif cur is not None:
            cur["lines"].append(raw)
    out = []
    for s in sections:
        errs, notify, summary, body = [], None, [], []
        lines, i = s["lines"], 0
        while i < len(lines) and not lines[i].strip():
            i += 1
        if i < len(lines) and re.match(r"^notify:\s*(yes|no)\s*$", lines[i]):
            notify = lines[i].split(":", 1)[1].strip()
            i += 1
        else:
            errs.append("no 'notify: yes|no' line first")
        if i < len(lines) and lines[i].strip() == "summary:":
            i += 1
            while i < len(lines) and lines[i].strip():
                sm = re.match(r"^- (.+?)\s+\(refs:\s*([^)]*)\)\s*$", lines[i])
                if not sm:
                    errs.append(f"summary line without '(refs: ...)': {lines[i]!r}")
                else:
                    refs = [r.strip() for r in sm.group(2).split(",") if r.strip()]
                    summary.append((sm.group(1).strip(), refs))
                i += 1
        else:
            errs.append("no 'summary:' line after notify")
        body = [l for l in lines[i:] if l.strip()]
        if not 1 <= len(summary) <= SUMMARY_MAX_LINES:
            errs.append(f"{len(summary)} summary lines (want 1-{SUMMARY_MAX_LINES})")
        for line, refs in summary:
            if len(line) > SUMMARY_MAX_CHARS:
                errs.append(f"summary line over {SUMMARY_MAX_CHARS} chars ({len(line)}): {line!r}")
            if not refs:
                errs.append(f"summary line with empty refs: {line!r}")
            for r in refs:
                if not re.fullmatch(r"#\d+|spec:[a-z0-9][a-z0-9-]*|pending", r):
                    errs.append(f"bad ref {r!r} on {line!r}")
        if not body:
            errs.append("no page text")
        out.append({"version": s["version"], "notify": notify, "summary": summary, "errors": errs})
    return url, out


def merged_prs(root):
    """PR numbers that landed on HEAD's first-parent history (merge or squash). Blind if shallow."""
    def git(*a):
        return subprocess.run(["git", "-C", root, *a], capture_output=True, text=True)
    sh = git("rev-parse", "--is-shallow-repository")
    if sh.returncode != 0:
        raise Blind("not a git repository: cannot see which PRs merged")
    if sh.stdout.strip() == "true":
        raise Blind("shallow clone: the merge history is incomplete (checkout needs fetch-depth: 0)")
    log = git("log", "--first-parent", "--format=%s", "HEAD")
    prs = set()
    for subj in log.stdout.splitlines():
        m = re.match(r"^Merge pull request #(\d+) from ", subj)
        if m:
            prs.add(int(m.group(1)))
            continue
        m = re.search(r"\(#(\d+)\)\s*$", subj)  # squash: the LAST (#N) is the PR that landed
        if m:
            prs.add(int(m.group(1)))
    return prs


def check(root):
    """-> list of violations (empty = pass). Raises Blind."""
    fw = fw_version(root)
    path = os.path.join(root, "docs", "CHANGES-customer.md")
    if not os.path.exists(path):
        return [f"docs/CHANGES-customer.md is missing (FW_VERSION {fw} needs an entry)"]
    url, sections = parse(open(path, encoding="utf-8").read())
    v = []
    if not url or not url.startswith("https://"):
        v.append("no 'url: https://...' line (where the changes page lives)")
    versions = [s["version"] for s in sections]
    if versions != sorted(set(versions), reverse=True):
        v.append(f"sections must be unique and newest first: {versions}")
    for s in sections:
        v += [f"v{s['version']}: {e}" for e in s["errors"]]
    cut = [s for s in sections if s["version"] == fw]
    if not cut:
        v.append(f"no '## v{fw}' entry for FW_VERSION {fw}: the release cannot be cut without one")
        return v
    prs = None
    for line, refs in cut[0]["summary"]:
        for r in refs:
            if r == "pending":
                v.append(f"v{fw}: {line!r} is still 'pending' -- the version being cut needs merged work, or the line comes out")
                continue
            if prs is None:
                prs = merged_prs(root)
            if r.startswith("#"):
                if int(r[1:]) not in prs:
                    v.append(f"v{fw}: {line!r} refs {r}, which has not merged on main -- remove the line before the cut")
            else:
                doc = os.path.join(root, "docs", r[5:] + ".md")
                if not os.path.exists(doc):
                    v.append(f"v{fw}: {line!r} refs {r}, but docs/{r[5:]}.md does not exist")
                    continue
                built = [int(n) for n in re.findall(r"Built in #(\d+)", open(doc, encoding="utf-8").read())]
                if not built:
                    v.append(f"v{fw}: {line!r} refs {r}, whose status names no 'Built in #NNN'")
                for n in built:
                    if n not in prs:
                        v.append(f"v{fw}: {line!r} refs {r}, built in #{n}, which has not merged on main")
    return v


def check_wiring(root):
    """The gate is still in front of `build`: build needs customer-entry, which runs this script
    against a full-history checkout. Text-level, on purpose: no YAML library is assumed."""
    wf = open(os.path.join(root, ".github", "workflows", "firmware.yml"), encoding="utf-8").read()
    v = []
    jobs = {m.group(1): m.start() for m in re.finditer(r"^  ([a-z][a-z0-9-]*):\s*$", wf, re.M)}

    def block(name):
        if name not in jobs:
            return None
        start = jobs[name]
        later = [p for p in jobs.values() if p > start]
        return wf[start:min(later) if later else len(wf)]
    b = block("build")
    if b is None:
        v.append("no 'build' job")
    else:
        m = re.search(r"^    needs:\s*(.+)$", b, re.M)
        if not m or "customer-entry" not in m.group(1):
            v.append("build does not need customer-entry: the cut would not wait for the gate")
    g = block("customer-entry")
    if g is None:
        v.append("no 'customer-entry' job")
    else:
        if not re.search(r"fetch-depth:\s*0\b", g):
            v.append("customer-entry does not fetch full history (fetch-depth: 0): the gate would be blind")
        if not re.search(r"run:\s*python3 scripts/check-customer-entry\.py\s*$", g, re.M):
            v.append("customer-entry does not run the gate itself (check-customer-entry.py with no flag)")
    return v


def run(fn, root, quiet=False):
    try:
        v = fn(root)
    except Blind as e:
        if not quiet:
            print(f"BLIND: {e}")
        return 2
    if not quiet:
        for x in v:
            print(f"  FAIL  {x}")
        print("FAIL" if v else "ok")
    return 1 if v else 0


# ---------------------------------------------------------------------------------- selftest
def _fixture(td, fw, changes, merges=(), squashes=(), docs=None):
    os.makedirs(os.path.join(td, "src"))
    os.makedirs(os.path.join(td, "docs"))
    open(os.path.join(td, "src", "OtaUpdater.h"), "w").write(f"constexpr int FW_VERSION = {fw};\n")
    if changes is not None:
        open(os.path.join(td, "docs", "CHANGES-customer.md"), "w").write(changes)
    for name, text in (docs or {}).items():
        open(os.path.join(td, "docs", name + ".md"), "w").write(text)
    g = lambda *a: subprocess.run(["git", "-C", td, *a], capture_output=True, text=True, check=True)
    g("init", "-q", "-b", "main")
    g("-c", "user.email=t@example.com", "-c", "user.name=t", "commit", "-q", "--allow-empty", "-m", "base")
    for n in merges:  # a real merge commit, as GitHub makes one
        g("checkout", "-q", "-b", f"f{n}")
        g("-c", "user.email=t@example.com", "-c", "user.name=t", "commit", "-q", "--allow-empty", "-m", f"work for {n}")
        g("checkout", "-q", "main")
        g("-c", "user.email=t@example.com", "-c", "user.name=t", "merge", "-q", "--no-ff", f"f{n}",
          "-m", f"Merge pull request #{n} from example/f{n}")
    for n in squashes:
        g("-c", "user.email=t@example.com", "-c", "user.name=t", "commit", "-q", "--allow-empty",
          "-m", f"feat: squashed work (#{n})")
    g("add", "-A")
    g("-c", "user.email=t@example.com", "-c", "user.name=t", "commit", "-q", "-m", "files")


def _entry(v, notify="yes", lines=("Hold to zoom in  (refs: #12)",), body="Plain words."):
    return f"## v{v}\nnotify: {notify}\nsummary:\n" + "".join(f"- {l}\n" for l in lines) + f"\n{body}\n\n"


def selftest():
    URL = "url: https://example.com/changes\n\n"
    cases = [
        # name, fw, changes, merges, squashes, docs, want
        ("CONTROL: valid entry, ref merged", 17, URL + _entry(17), [12], [], None, 0),
        ("CONTROL: squash-merged ref", 17, URL + _entry(17), [], [12], None, 0),
        ("CONTROL: older version may say pending", 17, URL + _entry(17) + _entry(16, lines=("Old thing  (refs: pending)",)), [12], [], None, 0),
        ("CONTROL: spec ref whose status names a merged PR", 17, URL + _entry(17, lines=("Overhead plane card  (refs: spec:v17-overhead-card)",)), [40], [], {"v17-overhead-card": "**Status: Built in #40.**\n"}, 0),
        ("no entry for FW_VERSION", 17, URL + _entry(16), [12], [], None, 1),
        ("no CHANGES file at all", 17, None, [12], [], None, 1),
        ("no url line", 17, _entry(17), [12], [], None, 1),
        ("bad notify", 17, URL + _entry(17, notify="maybe"), [12], [], None, 1),
        ("five summary lines", 17, URL + _entry(17, lines=tuple(f"Line {i}  (refs: #12)" for i in range(5))), [12], [], None, 1),
        ("a 25-character line", 17, URL + _entry(17, lines=("x" * 25 + "  (refs: #12)",)), [12], [], None, 1),
        ("a line without refs", 17, URL + _entry(17, lines=("Hold to zoom in",)), [12], [], None, 1),
        ("pending on the version being cut", 17, URL + _entry(17, lines=("Hold to zoom in  (refs: pending)",)), [12], [], None, 1),
        ("ref to a PR that never merged", 17, URL + _entry(17, lines=("Hold to zoom in  (refs: #13)",)), [12], [], None, 1),
        ("spec ref with no Built line", 17, URL + _entry(17, lines=("Overhead plane card  (refs: spec:v17-overhead-card)",)), [40], [], {"v17-overhead-card": "**Status: spec, not built.**\n"}, 1),
        ("spec ref built in an unmerged PR", 17, URL + _entry(17, lines=("Overhead plane card  (refs: spec:v17-overhead-card)",)), [12], [], {"v17-overhead-card": "**Status: Built in #40.**\n"}, 1),
        ("sections out of order", 17, URL + _entry(16, lines=("Old  (refs: pending)",)) + _entry(17), [12], [], None, 1),
        ("no page text", 17, URL + _entry(17, body=""), [12], [], None, 1),
    ]
    rc = 0
    for name, fw, changes, merges, squashes, docs, want in cases:
        with tempfile.TemporaryDirectory() as td:
            _fixture(td, fw, changes, merges, squashes, docs)
            got = run(check, td, quiet=True)
        ok = got == want
        print(f"  {'ok  ' if ok else 'FAIL'}  {name}: exit {got} (want {want})")
        rc |= 0 if ok else 1
    # BLIND: a shallow clone cannot see the merges, and must not pass or fail on that.
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "src-repo")
        os.makedirs(src)
        _fixture(src, 17, URL + _entry(17), [12])
        shallow = os.path.join(td, "shallow")
        subprocess.run(["git", "clone", "-q", "--depth", "1", "file://" + src.replace(os.sep, "/"), shallow], check=True, capture_output=True)
        got = run(check, shallow, quiet=True)
    ok = got == 2
    print(f"  {'ok  ' if ok else 'FAIL'}  BLIND: shallow clone: exit {got} (want 2)")
    rc |= 0 if ok else 1
    # WIRING: the real workflow is the control; each plant must fail.
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    real = open(os.path.join(root, ".github", "workflows", "firmware.yml"), encoding="utf-8").read()
    plants = [
        ("CONTROL: the real firmware.yml", real, 0),
        ("the gate removed from build's needs", re.sub(r"(^  build:\s*\n(?:.*\n)*?    needs:\s*)\[skus, customer-entry\]", r"\1skus", real, count=1, flags=re.M), 1),
        ("customer-entry without full history", real.replace("fetch-depth: 0", "fetch-depth: 1", 1), 1),
    ]
    for name, text, want in plants:
        if not name.startswith("CONTROL") and text == real:
            print(f"  FAIL  plant did not apply: {name}")
            rc = 1
            continue
        with tempfile.TemporaryDirectory() as td:
            os.makedirs(os.path.join(td, ".github", "workflows"))
            open(os.path.join(td, ".github", "workflows", "firmware.yml"), "w", encoding="utf-8").write(text)
            got = run(check_wiring, td, quiet=True)
        ok = got == want
        print(f"  {'ok  ' if ok else 'FAIL'}  wiring: {name}: exit {got} (want {want})")
        rc |= 0 if ok else 1
    print("SELFTEST PASSED" if rc == 0 else "SELFTEST FAILED")
    return rc


if __name__ == "__main__":
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    if "--check-wiring" in sys.argv:
        sys.exit(run(check_wiring, here))
    sys.exit(run(check, here))
