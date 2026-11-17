"""P1 (docs/workflow-permissions.md): every workflow carries an explicit top-level `permissions:` block, and its
blocks -- top-level and job-level -- are exactly the ones registered below. With every workflow explicit, the
repository's default GITHUB_TOKEN setting can be switched to read without any workflow changing what it may do.

    python3 scripts/check_workflow_permissions.py             exit 0 pass, 1 a workflow differs, 2 rig
    python3 scripts/check_workflow_permissions.py --selftest  each plant must turn exactly its workflow red

Each write below names the step that needs it, read from the workflow itself. Read with regexes, not a YAML library,
like check_workflow_concurrency.py, so it runs on any python3 with nothing installed. Workflows are keyed by their
`name:` line, so this file names no workflow file.
"""
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOP = None   # the key for a workflow's top-level block

REGISTERED = {
    "account-budget": {TOP: {"contents": "read"}},
    "bench-tools": {TOP: {"contents": "read"}},
    "device-ids": {TOP: {"contents": "read"}},
    "firmware": {
        TOP: {"contents": "read"},
        "build": {"contents": "write"},                       # gh release upload: the release event's assets
        "ship-alert": {"actions": "read", "contents": "read"},  # gh api .../actions/runs/<id>/jobs
        "version": {"contents": "write"},                     # gh release edit --prerelease; gh release upload version.txt
        "promote": {"contents": "write"},                     # gh release edit --prerelease=false
    },
    "no-traffic-captures": {TOP: {"contents": "read"}},
    "photo-drift": {TOP: {"contents": "read", "checks": "write"}},   # gh api repos/$REPO/check-runs (a POST)
    "photos": {TOP: {"contents": "read", "checks": "write"}},        # gh api repos/$REPO/check-runs (a POST)
    "print-cards": {TOP: {"contents": "read"}},
    "provisioner": {TOP: {"contents": "read"}},
    "refresh-data": {TOP: {"contents": "read"}},
    "routes-refresh": {TOP: {"contents": "read", "issues": "write"}},  # routes-notify.ts: gh label create, gh issue
    "web-renderer": {TOP: {"contents": "read"}, "publish": {"contents": "write"}},   # gh release upload
    "workers": {TOP: {"contents": "read"}},
}

NAME = re.compile(r"""^name:[ \t]*["']?([^"'\n#]+?)["']?[ \t]*(?:#.*)?$""", re.M)
SCOPE = re.compile(r"^([a-z-]+):[ \t]*(read|write|none)[ \t]*(?:#.*)?$")


class Unreadable(Exception):
    """A block that cannot be read cannot be judged: a RIG, never a pass."""


def block(lines, i, indent):
    """The scopes of the `permissions:` key at lines[i], whose children sit at `indent` spaces."""
    head = lines[i].split("#", 1)[0].strip()
    if head != "permissions:":
        raise Unreadable("an inline permissions value (%s); only the block form is registered" % head)
    out = {}
    for ln in lines[i + 1:]:
        if not ln.strip() or ln.lstrip().startswith("#"):
            continue
        cur = len(ln) - len(ln.lstrip(" "))
        if cur < indent:
            break
        if cur > indent:
            raise Unreadable("a nested line inside a permissions block: %r" % ln.strip())
        m = SCOPE.match(ln.strip())
        if not m:
            raise Unreadable("an unreadable permissions line: %r" % ln.strip())
        if m.group(1) in out:
            raise Unreadable("scope %s given twice" % m.group(1))
        out[m.group(1)] = m.group(2)
    if not out:
        raise Unreadable("an empty permissions block")
    return out


def read(text):
    """(name, {TOP or job: scopes}) for one workflow's text."""
    m = NAME.search(text)
    if not m:
        raise Unreadable("no top-level name: line")
    lines = text.split("\n")
    found = {}
    tops = [i for i, ln in enumerate(lines) if ln.startswith("permissions:")]
    if len(tops) > 1:
        raise Unreadable("%d top-level permissions keys" % len(tops))
    if tops:
        found[TOP] = block(lines, tops[0], 2)
    in_jobs, job = False, None
    for i, ln in enumerate(lines):
        if re.match(r"^jobs:[ \t]*(?:#.*)?$", ln):
            in_jobs = True
            continue
        if in_jobs and re.match(r"^\S", ln):
            in_jobs = False
        if not in_jobs:
            continue
        jm = re.match(r"^  ([A-Za-z0-9_-]+):[ \t]*(?:#.*)?$", ln)
        if jm:
            job = jm.group(1)
            continue
        if job and ln.startswith("    permissions:"):
            if job in found:
                raise Unreadable("job %s has two permissions keys" % job)
            found[job] = block(lines, i, 6)
    return m.group(1).strip(), found


def check(root, out=print):
    """Returns the sorted list of workflow names that differ from REGISTERED (empty when every one matches)."""
    wf = os.path.join(root, ".github", "workflows")
    files = sorted(f for f in os.listdir(wf) if f.endswith((".yml", ".yaml")))
    if not files:
        raise Unreadable("no workflow files under %s" % wf)
    bad, seen = [], set()
    for f in files:
        try:
            name, got = read(open(os.path.join(wf, f), encoding="utf-8").read())
        except Unreadable as e:
            out("  RED %s: unreadable: %s" % (f, e))
            bad.append(f)
            continue
        if name in seen:
            out("  RED %s: a second workflow named %s" % (f, name))
            bad.append(name)
            continue
        seen.add(name)
        want = REGISTERED.get(name)
        if want is None:
            out("  RED %s: not registered here; register its blocks, each write with the step that needs it" % name)
            bad.append(name)
            continue
        if TOP not in got:
            out("  RED %s: no top-level permissions block" % name)
            bad.append(name)
            continue
        if got != want:
            for k in sorted(set(got) | set(want), key=lambda x: "" if x is None else x):
                if got.get(k) != want.get(k):
                    out("  RED %s [%s]: has %s, registered %s" % (name, "top level" if k is None else "job " + k,
                                                                 got.get(k), want.get(k)))
            bad.append(name)
    for name in sorted(set(REGISTERED) - seen):
        out("  RED %s: registered, but no workflow carries that name" % name)
        bad.append(name)
    return sorted(set(bad))


def selftest():
    """Plants into a COPY of this checkout's workflows; each must turn exactly its own workflow red."""
    def run(plant):
        d = tempfile.mkdtemp(prefix="wf-perms-")
        try:
            shutil.copytree(os.path.join(ROOT, ".github", "workflows"), os.path.join(d, ".github", "workflows"))
            wf = os.path.join(d, ".github", "workflows")

            def edit(name, old, new):
                for f in os.listdir(wf):
                    p = os.path.join(wf, f)
                    t = open(p, encoding="utf-8").read()
                    if NAME.search(t) and NAME.search(t).group(1).strip() == name:
                        if t.count(old) != 1:
                            raise SystemExit("SELFTEST RIG: plant %r not exactly once in %s" % (old, name))
                        open(p, "w", encoding="utf-8").write(t.replace(old, new))
                        return
                raise SystemExit("SELFTEST RIG: no workflow named %s" % name)
            if plant:
                plant(edit, wf)
            return check(d, out=lambda s: None)
        finally:
            shutil.rmtree(d, ignore_errors=True)

    cases = [
        ("clean", None, []),
        ("device-ids loses its top-level block",
         lambda e, wf: e("device-ids", "permissions:\n  contents: read\n", ""), ["device-ids"]),
        ("firmware's top level gains actions: write",
         lambda e, wf: e("firmware", "permissions:\n  contents: read\n", "permissions:\n  contents: read\n  actions: write\n"),
         ["firmware"]),
        ("web-renderer's publish job drops to read",
         lambda e, wf: e("web-renderer", "    permissions:\n      contents: write", "    permissions:\n      contents: read"),
         ["web-renderer"]),
        ("workers goes inline write-all",
         lambda e, wf: e("workers", "permissions:\n  contents: read\n", "permissions: write-all\n"), ["workers.yml"]),
        ("a new workflow nobody registered",
         lambda e, wf: open(os.path.join(wf, "zz-new.yml"), "w").write("name: zz-new\npermissions:\n  contents: read\n"),
         ["zz-new"]),
    ]
    bad = 0
    for label, plant, want in cases:
        got = run(plant)
        ok = got == sorted(want)
        bad += 0 if ok else 1
        print("  %s  %s: red %s (want %s)" % ("ok  " if ok else "FAIL", label, got, sorted(want)))
    print("SELFTEST PASSED" if bad == 0 else "SELFTEST FAILED (%d)" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        sys.exit(selftest())
    try:
        red = check(ROOT)
    except Unreadable as e:
        print("RIG: %s" % e)
        sys.exit(2)
    print("workflow permissions: %d registered, %d red%s" % (len(REGISTERED), len(red), (": " + ", ".join(red)) if red else ""))
    sys.exit(1 if red else 0)
