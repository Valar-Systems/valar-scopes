"""C1 (docs/ci-concurrency.md): every workflow that runs on pull_request cancels its own superseded PR runs, and
nothing else is ever cancelled.

    python3 scripts/check_workflow_concurrency.py              exit 0 pass, 1 a workflow is wrong, 2 rig
    python3 scripts/check_workflow_concurrency.py --selftest   each sabotage must turn exactly its workflow red

A workflow with a pull_request trigger must carry exactly the block below, at the top level. A workflow without one
must not carry it, so a schedule or a push is never grouped with PR runs. Read with regexes, not a YAML library, so it
runs on any python3 with nothing installed.
"""
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GROUP = ("  group: ${{ github.workflow }}-${{ github.event_name == 'pull_request' && "
         "format('pr-{0}', github.event.pull_request.number) || github.run_id }}")
CANCEL = "  cancel-in-progress: ${{ github.event_name == 'pull_request' }}"
BLOCK = "concurrency:\n" + GROUP + "\n" + CANCEL + "\n"


class Unreadable(Exception):
    """The workflow's triggers cannot be read, so it cannot be judged: a RIG, never a pass (#476)."""


ON_KEY = re.compile(r"""^(?:on|"on"|'on'):[ \t]*(.*)$""", re.M)


def triggers(text):
    """The top-level trigger names, from the block form only. Anything else is Unreadable: an inline list
    (`on: [push, pull_request]`), a scalar (`on: push`), an inline map, no `on` key, or a block with no trigger."""
    m = ON_KEY.search(text)
    if not m:
        raise Unreadable("no top-level on: key")
    rest = m.group(1).split("#", 1)[0].strip()
    if rest:
        raise Unreadable(f"inline on: ({rest[:40]}); only the block form can be read")
    blk = re.match(r"((?:[ \t]+.*\n|[ \t]*\n)+)", text[m.end() + 1:])
    keys = set(re.findall(r"^  ([a-z_]+):", blk.group(1), re.M)) if blk else set()
    if not keys:
        raise Unreadable("a block on: with no trigger")
    return keys


def problems(wf_dir, rig=None):
    """The wrong workflows, by name. Workflows whose triggers cannot be read go in `rig` (when given), not here."""
    bad = {}
    names = sorted(n for n in os.listdir(wf_dir) if n.endswith((".yml", ".yaml")))
    if not names:
        return None
    for n in names:
        t = open(os.path.join(wf_dir, n), encoding="utf-8").read().replace("\r\n", "\n")
        blocks = re.findall(r"^concurrency:[ \t]*\n((?:[ \t]+.*\n)+)", t, re.M)
        ours = [b for b in blocks if "github.event.pull_request.number" in b]
        try:
            trig = triggers(t)
        except Unreadable as e:
            if rig is not None:
                rig[n] = str(e)
            continue
        if "pull_request" in trig:
            if len(ours) != 1:
                bad[n] = f"runs on pull_request but carries {len(ours)} PR concurrency blocks (want exactly 1)"
            elif ("concurrency:\n" + ours[0]) != BLOCK:
                bad[n] = "its PR concurrency block is not the agreed one (group with the run_id fallback; cancel only on pull_request)"
        elif ours:
            bad[n] = "does not run on pull_request but carries the PR concurrency block"
    return bad


def main(argv):
    wf = os.path.join(ROOT, ".github", "workflows")
    if argv[1:2] == ["--selftest"]:
        return selftest(wf)
    rig = {}
    bad = problems(wf, rig)
    if bad is None:
        print("RIG: no workflows found"); return 2
    for n, why in sorted(rig.items()):
        print(f"RIG {n}: {why} -- cannot be judged, so it is refused rather than passed")
    for n, why in sorted(bad.items()):
        print(f"FAIL {n}: {why}")
    if rig:
        print(f"workflow concurrency: {len(rig)} unreadable (RIG)"); return 2
    print("workflow concurrency: OK" if not bad else f"workflow concurrency: {len(bad)} wrong")
    return 1 if bad else 0


def selftest(wf):
    plants = [
        ("the block removed", "firmware.yml", lambda t: t.replace(BLOCK, "")),
        ("cancel-in-progress: true", "workers.yml", lambda t: t.replace(CANCEL, "  cancel-in-progress: true")),
        ("the group without its run_id fallback", "device-ids.yml",
         lambda t: t.replace(" || github.run_id }}", " }}")),
    ]
    base_rig = {}
    base = problems(wf, base_rig)
    if base is None or base or base_rig:
        print(f"RIG: the real tree must pass before the selftest means anything ({base})"); return 2
    fails = 0
    for label, name, plant in plants:
        tmp = tempfile.mkdtemp(prefix="wfc-")
        try:
            for n in os.listdir(wf):
                shutil.copyfile(os.path.join(wf, n), os.path.join(tmp, n))
            p = os.path.join(tmp, name)
            before = open(p, encoding="utf-8").read().replace("\r\n", "\n")
            after = plant(before)
            if after == before:
                print(f"  RIG {label}: the plant did not apply to {name}"); return 2
            open(p, "w", encoding="utf-8").write(after)
            got = sorted(problems(tmp))
            ok = got == [name]
            fails += not ok
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: red {got} (want {[name]})")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    # #476: an on: the checker cannot read must be a RIG naming the file, never a pass.
    body = "name: plant\n{on}jobs:\n  a:\n    runs-on: ubuntu-latest\n    steps:\n      - run: true\n"
    for label, on in (("an inline list, on: [push, pull_request]", "on: [push, pull_request]\n"),
                      ("a scalar, on: pull_request", "on: pull_request\n"),
                      ("no on key at all", "")):
        tmp = tempfile.mkdtemp(prefix="wfc-")
        try:
            for n in os.listdir(wf):
                shutil.copyfile(os.path.join(wf, n), os.path.join(tmp, n))
            open(os.path.join(tmp, "plant-on.yml"), "w", encoding="utf-8").write(body.format(on=on))
            rig = {}
            bad = problems(tmp, rig)
            ok = sorted(rig) == ["plant-on.yml"] and not bad
            fails += not ok
            print(f"  {'ok  ' if ok else 'FAIL'} {label}: RIG {sorted(rig)}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    print("workflow concurrency SELFTEST PASSED" if not fails else f"workflow concurrency SELFTEST: {fails} NOT AS PREDICTED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
