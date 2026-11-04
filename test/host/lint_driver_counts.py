"""One count, one place (docs/weatherscope-stage3.md A12.70, A12.73): every sabotage driver's suite counts live in ONE
named constant (or one structure, as SUITES), read by both its run path and its --control table -- so a count fixed in
one place cannot leave a stale copy in the other (Q57: --control green while the full run reads the RIG).

    python -I test/host/lint_driver_counts.py [<driver.py> ...]     (no arguments: every test/host/sabotage_*.py)
    python -I test/host/lint_driver_counts.py --control             (its own control: it must be able to fail)

Flags, read from each driver's AST (comments and docstrings are not code):
  L1  a CONTROL_TABLE count (a ("checks", n) or ("ok", n) entry) that is an integer literal, not a name or a value
      derived from a named structure. ("exit", n) and ("line", text) entries are not counts.
  L2  any integer literal equal to one of the driver's table counts, except the named constant's own single definition,
      a slice bound (an index, not a count), and a PINNED line (below).
PINS: one line each, by file and exact text, with its reason (Q65). A pin covers that line and nothing else: if the line's
text changes, the lint flags it again.
Exit 0 nothing flagged; 1 a driver flagged; 2 the rig.
"""
import ast, glob, os, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SKIP = {"sabotage_common.py", "sabotage_anchors_control.py", "sabotage_controls_control.py"}
KINDS = ("checks", "ok")
PINS = [
    ("sabotage_wx_server_crops.py", "        data[32] ^= 0x10                     # the first run's code nibble",
     "the bit S-SC flips in the crop's first code nibble: a mask that equals the driver's count, 16, by coincidence"),
]


def is_int(node):
    return isinstance(node, ast.Constant) and isinstance(node.value, int) and not isinstance(node.value, bool)


def lint(path):
    """[(rule, line, text)] for one driver."""
    src = open(path, encoding="utf-8").read()
    lines = src.split("\n")
    tree = ast.parse(src)
    pinned = {i + 1 for i, l in enumerate(lines)
              for f, text, _ in PINS if f == os.path.basename(path) and l.rstrip("\r") == text}
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
        if (is_int(node) and node.value in values and id(node) not in allowed and id(node) not in slices
                and node.lineno not in pinned):
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
    print(f"LINT driver counts: {len(paths)} driver(s), {bad} flagged; {len(PINS)} pinned line(s)")
    return 1 if bad else 0


def control():
    """Five cases (A12.73), each a copy under its own name in its own directory: the clean k35 driver flags nothing; its
    table count as a literal is L1; its run-path check as a literal is L2; server_crops' pinned literal on another line,
    once bitwise and once in a comparison, is L2 each; and its pinned line with its text changed is L2."""
    k35 = open(os.path.join(HERE, "sabotage_wx_k35.py"), encoding="utf-8").read()
    sc_src = open(os.path.join(HERE, "sabotage_wx_server_crops.py"), encoding="utf-8").read()
    table, check, pin = '("checks", K35_CHECKS)', "!= K35_CHECKS", PINS[0][1]
    if k35.count(table) != 1 or k35.count(check) != 1 or sc_src.count(pin + "\n") != 1:
        print("LINT-CTL: the control's anchors are not each once (the rig)")
        return 2
    first_def = sc_src.index("\ndef ") + 1
    cases = [
        ("the clean driver", "sabotage_wx_k35.py", k35, []),
        # Q66 (a): the table's literal is L1, and the constant it no longer reads is a second copy, L2 (A12.74)
        ("a table literal", "sabotage_wx_k35.py", k35.replace(table, '("checks", 79)'), ["L1", "L2"]),
        ("a run-path literal", "sabotage_wx_k35.py", k35.replace(check, "!= 79"), ["L2"]),
        ("the pinned literal elsewhere, bitwise", "sabotage_wx_server_crops.py",
         sc_src[:first_def] + "MASK = 0x10 ^ 0\n" + sc_src[first_def:], ["L2"]),
        ("the pinned literal elsewhere, compared", "sabotage_wx_server_crops.py",
         sc_src[:first_def] + "SEEN = len(sys.argv) == 16\n" + sc_src[first_def:], ["L2"]),
        ("the pinned line with its text changed", "sabotage_wx_server_crops.py",
         sc_src.replace(pin, pin.replace("code nibble", "code nibble (changed)")), ["L2"]),
    ]
    ok = True
    with tempfile.TemporaryDirectory() as td:
        for i, (name, base, text, want) in enumerate(cases):
            d = os.path.join(td, str(i))
            os.makedirs(d)
            p = os.path.join(d, base)
            open(p, "w", encoding="utf-8").write(text)
            got = sorted(rule for rule, _, _ in lint(p))
            good = got == sorted(want)
            ok &= good
            print(f"  {'ok  ' if good else 'FAIL'} LINT-CTL {name}: flagged {got} (want {want})")
    return 0 if ok else 1


if __name__ == "__main__":
    if sys.argv[1:] == ["--control"]:
        sys.exit(control())
    paths = sys.argv[1:] or sorted(p for p in glob.glob(os.path.join(HERE, "sabotage_*.py")) if os.path.basename(p) not in SKIP)
    sys.exit(run(paths))
