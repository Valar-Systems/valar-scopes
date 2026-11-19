"""The bench-marker list, checked from the OTHER side.

scripts/check-no-bench-hooks.sh scans firmware images for the strings in its MARKERS line, and its --selftest plants
each of that list's own entries. That proves the scanner reads the list. It cannot see a marker the list never had (a
hook added without its marker), and it cannot see one a merge dropped. So this derives the marker set from the hooks
themselves -- every bench-shaped tag inside a C/C++ string literal under src/ and include/ -- and requires it to EQUAL
the MARKERS line. A marker left in the list after its hook is gone is a difference too.

Adapted from the map client's bench tooling (marker_xcheck.py), with one change: an instrument failure (the wrong tree,
a MARKERS line that cannot be read) exits 2. The original raised SystemExit with a message, which exits 1 and reads as
"a difference".

    python -I scripts/bench-marker-xcheck.py [<tree>]                 exit 0 equal; 1 a difference; 2 the instrument
    python -I scripts/bench-marker-xcheck.py [<tree>] --rev <commit>  read that commit from git, not the working tree
    python -I scripts/bench-marker-xcheck.py [<tree>] --plant-hook    a virtual hook printing [zz-bench], unlisted: reads 1
    python -I scripts/bench-marker-xcheck.py [<tree>] --plant-drop    the list's last marker removed, as a merge would: reads 1

A tag is bench-shaped when one of its hyphen-separated words is "bench", or it is in SPECIAL ([photo-mode]; step D's
[map-key], the mapkey bench image's V19S line, whose text the registration fixed). It prints tags and
counts only, never a source line. Named limits: a marker that is not bench-shaped (a diagnostic build's own tag) is
invisible to this, and so is one assembled at run time rather than written as one literal.
"""
import os
import re
import subprocess
import sys

LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
TAG = re.compile(r"\[([a-z0-9]+(?:-[a-z0-9]+)*)\]")
SPECIAL = {"photo-mode", "map-key"}
EXT = (".c", ".cc", ".cpp", ".h", ".hpp", ".inc", ".ino")
SCRIPT = "scripts/check-no-bench-hooks.sh"


class Instrument(Exception):
    pass


def bench_shaped(t):
    return t in SPECIAL or "bench" in t.split("-")


def markers_in(script_text, where):
    lines = [l for l in script_text.splitlines() if l.startswith("MARKERS=(")]
    if len(lines) != 1:
        raise Instrument("%d MARKERS lines in %s, not 1" % (len(lines), where))
    mk = re.findall(r'"\[([^\]]+)\]"', lines[0])
    if not mk:
        raise Instrument("the MARKERS line in %s holds no marker" % where)
    return mk


def scan(texts):
    found = {}
    for name, txt in texts:
        for lit in LIT.findall(txt):
            for t in TAG.findall(lit):
                if bench_shaped(t):
                    found.setdefault(t, set()).add(name)
    return found


def tree_texts(tree):
    texts = []
    for top in ("src", "include"):
        for d, _, fs in os.walk(os.path.join(tree, top)):
            for f in fs:
                if f.endswith(EXT):
                    p = os.path.join(d, f)
                    with open(p, encoding="utf-8", errors="replace") as fh:
                        texts.append((os.path.relpath(p, tree).replace("\\", "/"), fh.read()))
    p = os.path.join(tree, SCRIPT)
    if not os.path.isfile(p):
        raise Instrument("no %s under %s" % (SCRIPT, tree))
    with open(p, encoding="utf-8") as fh:
        return texts, fh.read()


def rev_texts(repo, rev):
    """Read a commit's src/, include/ and the script straight from git: no checkout, nothing written."""
    def git(*a):
        r = subprocess.run(["git", "-C", repo] + list(a), capture_output=True)
        if r.returncode != 0:
            raise Instrument("git %s failed (exit %d)" % (a[0], r.returncode))
        return r.stdout
    names = git("ls-tree", "-r", "--name-only", rev, "--", "src", "include").decode("utf-8").splitlines()
    texts = [(n, git("show", "%s:%s" % (rev, n)).decode("utf-8", "replace")) for n in names if n.endswith(EXT)]
    return texts, git("show", "%s:%s" % (rev, SCRIPT)).decode("utf-8", "replace")


def run(argv):
    args = list(argv)
    rev = None
    if "--rev" in args:
        i = args.index("--rev")
        if i + 1 >= len(args):
            raise Instrument("--rev needs a commit")
        rev = args[i + 1]
        del args[i:i + 2]
    modes = {a for a in args if a.startswith("--")}
    unknown = modes - {"--plant-hook", "--plant-drop"}
    if unknown:
        raise Instrument("unknown option(s): %s" % sorted(unknown))
    pos = [a for a in args if not a.startswith("--")]
    tree = pos[0] if pos else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    if rev:
        texts, script = rev_texts(tree, rev)
        print("REV %s" % rev)
        mk = markers_in(script, "%s at %s" % (SCRIPT, rev))
    else:
        texts, script = tree_texts(tree)
        mk = markers_in(script, SCRIPT)
    if len(texts) < 50:
        raise Instrument("only %d source files under src/ and include/ -- the wrong tree?" % len(texts))
    if "--plant-hook" in modes:
        texts.append(("PLANT.cpp", 'Serial.println("[zz-bench] a planted hook");'))
        print("PLANT: a virtual source hook printing [zz-bench], absent from MARKERS")
    if "--plant-drop" in modes:
        print("PLANT: MARKERS' last entry [%s] removed, as a merge would drop it" % mk[-1])
        mk = mk[:-1]
    found = scan(texts)
    lst = set(mk)
    if len(lst) != len(mk):
        print("MARKERS has a duplicate: %d entries, %d distinct" % (len(mk), len(lst)))
        return 1
    print("%d source files; MARKERS %d; bench-shaped tags in source literals %d" % (len(texts), len(mk), len(found)))
    for t in sorted(set(found) | lst):
        where = sorted(found.get(t, []))
        print("  %s [%s]  list:%s  source:%d file(s)%s" % ("ok  " if (t in found) == (t in lst) else "DIFF", t,
              "y" if t in lst else "n", len(where), " e.g. " + where[0] if where else ""))
    only_src, only_list = sorted(set(found) - lst), sorted(lst - set(found))
    print("in the source, not in MARKERS: %s; in MARKERS, not in the source: %s" % (only_src, only_list))
    ok = not only_src and not only_list
    print("MARKER XCHECK: " + ("EQUAL" if ok else "DIFFERENT"))
    return 0 if ok else 1


def main(argv):
    try:
        return run(argv)
    except (Instrument, OSError, UnicodeDecodeError) as e:
        print("STOP (the instrument, not a verdict): %s" % e)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
