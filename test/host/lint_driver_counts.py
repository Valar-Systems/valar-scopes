"""One count, one place (docs/weatherscope-stage3.md A12.70): every sabotage driver's suite counts live in ONE named
constant (or one structure, as SUITES), read by both its run path and its --control table -- so a count fixed in one
place cannot leave a stale copy in the other (Q57: --control green while the full run reads the RIG).

    python -I test/host/lint_driver_counts.py [<driver.py> ...]     (no arguments: every test/host/sabotage_*.py)
    python -I test/host/lint_driver_counts.py --control             (its own control: it must be able to fail)

Flags, read from each driver's AST (comments and docstrings are not code):
  L1  a CONTROL_TABLE count (a ("checks", n) or ("ok", n) entry) that is an integer literal, not a name or a value
      derived from a named structure. ("exit", n) and ("line", text) entries are not counts.
  L2  any integer literal equal to one of the driver's table counts, except the named constant's own single definition
      and a slice bound (an index, not a count).
Exit 0 nothing flagged; 1 a driver flagged; 2 the rig.
"""
import ast, glob, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SKIP = {"sabotage_common.py", "sabotage_anchors_control.py", "sabotage_controls_control.py"}
KINDS = ("checks", "ok")


def is_int(node):
    return isinstance(node, ast.Constant) and isinstance(node.value, int) and not isinstance(node.value, bool)


def lint(path):
    """[(rule, line, text)] for one driver."""
    tree = ast.parse(open(path, encoding="utf-8").read())
    consts = {}          # module-level NAME = <int>: name -> (value, the Constant node)
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name) and is_int(node.value):
            consts[node.targets[0].id] = (node.value.value, node.value)
    table = [n for n in tree.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "CONTROL_TABLE"
                                                                       for t in n.targets)]
    flags, values, allowed = [], set(), set()
    for t in table:
        for tup in ast.walk(t.value):
            if not (isinstance(tup, ast.Tuple) and len(tup.elts) == 2 and isinstance(tup.elts[0], ast.Constant)
                    and tup.elts[0].value in KINDS):
                continue
            n = tup.elts[1]
            if is_int(n):
                flags.append(("L1", n.lineno, f"the table's {tup.elts[0].value!r} count {n.value} is a literal"))
                values.add(n.value)
                allowed.add(id(n))   # reported once, as L1
            elif isinstance(n, ast.Name) and n.id in consts:
                values.add(consts[n.id][0])
                allowed.add(id(consts[n.id][1]))
    slices = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Slice):
            for part in (node.lower, node.upper, node.step):
                if part is not None:
                    slices.update(id(x) for x in ast.walk(part))
    for node in ast.walk(tree):
        if is_int(node) and node.value in values and id(node) not in allowed and id(node) not in slices:
            flags.append(("L2", node.lineno, f"the literal {node.value} repeats a table count"))
    return sorted(flags, key=lambda f: (f[1], f[0]))


def run(paths):
    bad = 0
    for p in paths:
        flags = lint(p)
        bad += bool(flags)
        if flags:
            for rule, line, text in flags:
                print(f"  FAIL {os.path.basename(p)}:{line} {rule}: {text}")
        else:
            print(f"  ok   {os.path.basename(p)}")
    print(f"LINT driver counts: {len(paths)} driver(s), {bad} flagged")
    return 1 if bad else 0


def control():
    """Two mangled copies of sabotage_wx_k35.py must each be flagged, at the line mangled; the clean copy must not."""
    src = open(os.path.join(HERE, "sabotage_wx_k35.py"), encoding="utf-8").read().replace("\r\n", "\n")
    table, check = '("checks", K35_CHECKS)', "!= K35_CHECKS"
    if src.count(table) != 1 or src.count(check) != 1:
        print(f"LINT-CTL: the control's anchors are not each once in sabotage_wx_k35.py (the rig)")
        return 2
    cases = [("clean", src, []), ("a table literal", src.replace(table, '("checks", 79)'), ["L1"]),
             ("a run-path literal", src.replace(check, "!= 79"), ["L2"])]
    ok = True
    with tempfile.TemporaryDirectory() as td:
        for name, text, want in cases:
            p = os.path.join(td, f"{name.replace(' ', '_')}.py")
            open(p, "w", encoding="utf-8").write(text)
            got = [rule for rule, _, _ in lint(p)]
            good = got == want
            ok &= good
            print(f"  {'ok  ' if good else 'FAIL'} LINT-CTL {name}: flagged {got} (want {want})")
    return 0 if ok else 1


if __name__ == "__main__":
    if sys.argv[1:] == ["--control"]:
        sys.exit(control())
    paths = sys.argv[1:] or sorted(p for p in glob.glob(os.path.join(HERE, "sabotage_*.py")) if os.path.basename(p) not in SKIP)
    sys.exit(run(paths))
