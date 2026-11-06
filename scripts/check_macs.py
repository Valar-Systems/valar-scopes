#!/usr/bin/env python3
"""No committed file names a board's MAC, whole or by its device name (docs/weatherscope-stage3.md A12.140, A12.145).

WHY. A MAC plus the public salt gives a device id, and a device's name carries the last three bytes of its MAC, so a
name plus one of Espressif's 376 public OUIs is nearly the same thing. Boards are named in the repo by a short label,
the last two bytes (6E:64), and the full serials live in a file outside every repo (scripts/board-port.py). This scans
the tree at HEAD, not history: the history stays, by Daniel's decision, as check_device_ids.py and check_wifi.py do.

REFUSED, in tracked text and tracked file names:
  colon, dash   a universally administered MAC outside RFC 7042's documentation range (00-00-5E-00-53-xx)
  t1            a locally administered MAC whose OUI, with the local bit cleared, is Espressif's: a chip's derived form
  t2            a locally administered first byte, then an Espressif OUI in bytes 2-4: the shifted twin
  serial        a 12-hex token beginning with an Espressif OUI, in either case
  name          a product name, then 6 hex. The product names are read from include/DeviceIdentity.h, so a new edition
                is covered the day it is added. a1b2c3 and 00e000 are the examples, allowed by value
  store         a 12-hex or longer token in the OUI table or the allowlist: neither may hold a MAC or a plain hash of one

ALLOWED by rule, because none can be a factory MAC: other locally administered values, multicast, broadcast, all-zero.

NEEDS NO REGISTRY, so CI can run it: every rule is a shape, and the board list is never read here.
PRINTS NO VALUE: a finding is a path, a line and a class. Paths are masked.

    python scripts/check_macs.py              the tree
    python scripts/check_macs.py --selftest   rows G1-G18, on planted values built at run time
    python scripts/check_macs.py --sabotage   each plant, in a copy of this file, must turn exactly its rows red

Exit 0 clean; 1 refused; 2 BLIND: the OUI table or the product list is too short, or the scan saw nothing it can trust.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUIS = os.path.join(HERE, "espressif-ouis.txt")
ALLOW = os.path.join(HERE, "mac-allow.txt")
IDENTITY = os.path.join("include", "DeviceIdentity.h")

sys.path.insert(0, HERE)
import check_wifi as cw  # noqa: E402  the population: imported, never restated

MIN_OUIS = 300       # the table holds 376; fewer means it was cut, and the serial rule would go quiet
MIN_PRODUCTS = 9     # DeviceIdentity.h defines 9 product names today
DOC_RANGE = "00005E0053"
EXAMPLES = ("a1b2c3", "00e000")   # the only 6-hex values a device name may carry in the repo
PAIR = re.compile(r"(?<![0-9A-Fa-f:-])([0-9A-Fa-f]{2})([:-])([0-9A-Fa-f]{2})\2([0-9A-Fa-f]{2})\2([0-9A-Fa-f]{2})\2"
                  r"([0-9A-Fa-f]{2})\2([0-9A-Fa-f]{2})(?![0-9A-Fa-f:-])")
SERIAL = re.compile(r"(?<![0-9A-Za-z_])([0-9A-Fa-f]{12})(?![0-9A-Za-z_])")
LONG_HEX = re.compile(r"[0-9A-Fa-f]{12,}")
HEX_RUN = re.compile(r"[0-9A-Fa-f]{6,}")
PRODUCT = re.compile(r'#\s*define\s+DEVICE_PRODUCT_NAME\s+"(\w+)"')


def read_ouis(path):
    """The OUI set, upper case, or None if the file cannot be read."""
    try:
        text = open(path, encoding="ascii").read()
    except (OSError, UnicodeDecodeError):
        return None
    return {w.upper() for w in (x.strip() for x in text.splitlines()) if re.fullmatch(r"[0-9A-Fa-f]{6}", w)}


def read_products(root):
    """Every product name DeviceIdentity.h can compile to, or an empty list."""
    try:
        return sorted(set(PRODUCT.findall(open(os.path.join(root, IDENTITY), encoding="utf-8").read())))
    except (OSError, UnicodeDecodeError):
        return []


def read_allow(path):
    """[(path, line, reason)]: a path and a line, never the token. None if malformed or unreadable."""
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        return None
    out = []
    for raw in text.splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        parts = raw.split("\t")
        if len(parts) != 3 or not parts[1].isdigit() or not parts[2].strip():
            return None
        out.append((parts[0], int(parts[1]), parts[2]))
    return out


def name_pattern(products):
    return re.compile(r"(?i)(?<![0-9a-z])(?:" + "|".join(products) + r")-([0-9a-f]{6})(?![0-9a-z])")


def mac_class(octets, sep, ouis):
    """The refusal class of six octets, or None when the address cannot be a factory MAC."""
    h = "".join("%02X" % b for b in octets)
    if h.startswith(DOC_RANGE) or h == "0" * 12 or h == "F" * 12:
        return None
    if octets[0] & 1:
        return None
    if octets[0] & 2:
        if ("%02X" % (octets[0] & ~2)) + h[2:6] in ouis:
            return "t1"
        if h[2:8] in ouis:
            return "t2"
        return None
    return "colon" if sep == ":" else "dash"


def line_findings(line, ouis, names):
    """The refusal classes on one line. (doc_seen, classes); no value leaves this function."""
    seen, out = 0, []
    for m in PAIR.finditer(line):
        octets = [int(m.group(i), 16) for i in (1, 3, 4, 5, 6, 7)]
        if "".join("%02X" % b for b in octets).startswith(DOC_RANGE):
            seen += 1
        cls = mac_class(octets, m.group(2), ouis)
        if cls:
            out.append(cls)
    for m in SERIAL.finditer(line):
        if m.group(1)[:6].upper() in ouis:
            out.append("serial")
    for m in names.finditer(line):
        if m.group(1).lower() not in EXAMPLES:
            out.append("name")
    return seen, out


def store_findings(rel, text):
    """A store (the OUI table, the allowlist) may hold no 12-hex or longer token: no MAC, and no plain hash of one."""
    return [(rel, n, "store") for n, line in enumerate(text.split("\n"), 1) if LONG_HEX.search(line)]


def judge(root, files, ouis, names, allow):
    """Everything the verdict needs. Findings are (path, line, class); no value leaves this function."""
    findings, unreadable, scanned, binary, seen = [], [], 0, 0, 0
    for rel in files:
        rel = rel.replace(os.sep, "/")
        findings += [(rel, 0, c) for c in line_findings(rel, ouis, names)[1]]   # the file's NAME too
        try:
            raw = open(os.path.join(root, rel), "rb").read()
        except OSError:
            unreadable.append(rel)
            continue
        if cw.excused(rel, raw):
            binary += 1
            continue
        try:
            text = cw.as_text(rel, raw)
        except UnicodeDecodeError:
            unreadable.append(rel)
            continue
        scanned += 1
        for n, line in enumerate(text.split("\n"), 1):
            s, classes = line_findings(line, ouis, names)
            seen += s
            findings += [(rel, n, c) for c in classes]
    entries = {(p, n) for p, n, _ in allow}
    stale = sorted(e for e in entries if not any((f[0], f[1]) == e for f in findings))
    kept = [f for f in findings if (f[0], f[1]) not in entries]
    return {"findings": kept, "stale": stale, "unreadable": unreadable, "scanned": scanned, "binary": binary,
            "seen": seen, "allowed": len(findings) - len(kept)}


def masked(rel):
    return HEX_RUN.sub("<hex>", rel)


def run(root, oui_path, allow_path, quiet=False):
    """(exit code, findings, stale)."""
    say = (lambda *_: None) if quiet else print
    ouis, allow, products = read_ouis(oui_path), read_allow(allow_path), read_products(root)
    files = cw.tracked(root)
    if files is None or ouis is None or allow is None:
        say("RIG: git ls-files failed, or the OUI table or the allowlist cannot be read or is malformed")
        return 2, [], []
    if len(ouis) < MIN_OUIS or len(products) < MIN_PRODUCTS:
        say("BLIND: %d OUIs (need %d), %d product names (need %d)" % (len(ouis), MIN_OUIS, len(products), MIN_PRODUCTS))
        return 2, [], []
    v = judge(root, files, ouis, name_pattern(products), allow)
    store = []
    for rel, path in (("scripts/espressif-ouis.txt", oui_path), ("scripts/mac-allow.txt", allow_path)):
        store += store_findings(rel, open(path, encoding="utf-8").read())
    v["findings"] += store
    for rel, n, cls in v["findings"]:
        say("FAIL %s:%d: %s. Replace it with the board's label; see the head of scripts/check_macs.py" % (masked(rel), n, cls))
    for rel, n in v["stale"]:
        say("FAIL %s:%d: an allowlist entry whose line has no finding: drop it" % (masked(rel), n))
    for rel in v["unreadable"]:
        say("FAIL %s: cannot be read as text, and not a known binary type" % masked(rel))
    blind = v["scanned"] == 0 or v["seen"] == 0
    code = 2 if blind else (1 if v["findings"] or v["stale"] or v["unreadable"] else 0)
    say("mac check: %d tracked, %d scanned, %d binary; %d OUIs, %d products; %d documentation-range MAC(s) seen; "
        "%d allowed by entry; findings %d, stale %d, unreadable %d; %s"
        % (len(files), v["scanned"], v["binary"], len(ouis), len(products), v["seen"], v["allowed"], len(v["findings"]),
           len(v["stale"]), len(v["unreadable"]), {0: "OK", 1: "FAILED", 2: "BLIND (saw no documentation-range MAC)"}[code]))
    return code, v["findings"], v["stale"]


# ------------------------------------------------------------------------------------------------ selftest

ROW = re.compile(r"^\s*(ok|FAIL)\s+(G\d+)\b")
IDENTITY_TEXT = "".join('#    define DEVICE_PRODUCT_NAME "%s"\n' % n for n in (
    "Missileer", "Orbitscope", "Quakescope", "Quillscope", "Reelscope", "Speedscope", "Claudescope", "Weatherscope",
    "Blipscope"))


def planted(oui_path):
    """The selftest's values, built at run time so this file holds no token. The tail bytes are made up, and the first
    OUI in the table stands for any: a value here is a shape, never a board."""
    oui = sorted(read_ouis(oui_path))[0]
    tail = "".join(["AB", "CD", "EF"])
    pairs = lambda h: [h[i:i + 2] for i in range(0, 12, 2)]
    derived = "%02X" % (int(oui[:2], 16) | 2) + oui[2:] + tail
    doc = "".join(["00", "00", "5E", "00", "53", "01"])
    return {
        "uni": pairs("".join(["00", "11", "22", "33", "44", "55"])),
        "doc": pairs(doc),
        "local": pairs("".join(["02", "00", "00", "12", "34", "56"])),
        "multi": pairs("".join(["01", "00", "5E", "00", "00", "FB"])),
        "bcast": pairs("F" * 12),
        "zero": pairs("0" * 12),
        "t1": pairs(derived),
        "t2": pairs("02" + oui + "ABCD"),
        "serial": oui + tail,
        "other": "".join(["7F", "7F", "7F"]) + tail,
        "hash16": oui + tail + "ABCD",
        "hash64": oui + "0" * 58,
        "suffix": "".join(["c4", "d5", "e6"]),
    }


def tree(td, files):
    """A git tree holding `files` plus the header and a documentation-range line, so it is never BLIND by itself."""
    root = os.path.join(td, "t")
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(os.path.join(root, "include"))
    files = dict(files)
    files.setdefault(IDENTITY.replace(os.sep, "/"), IDENTITY_TEXT)
    for rel, body in files.items():
        p = os.path.join(root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w", encoding="utf-8", newline="\n").write(body)
    subprocess.run(["git", "init", "-q", root], check=True, capture_output=True)
    subprocess.run(["git", "-C", root, "add", "-A"], check=True, capture_output=True)   # its warnings name planted paths
    return root


def selftest():
    v = planted(OUIS)
    good = 0
    fails = 0
    doc = ":".join(v["doc"])
    base = {"docs/anchor.md": "the example " + doc + "\n"}

    def expect(row, what, files, code, classes, oui_path=OUIS, extra_allow=None):
        nonlocal good, fails
        with tempfile.TemporaryDirectory() as td:
            root = tree(td, dict(base, **files))
            allow = os.path.join(td, "allow.txt")
            open(allow, "w", encoding="utf-8", newline="\n").write(extra_allow or "")
            got, findings, stale = run(root, oui_path, allow, quiet=True)
        have = sorted(f[2] for f in findings) + ["stale"] * len(stale)
        ok = got == code and have == sorted(classes)
        print("  %s %s %s: exit %d, classes %s (want %d, %s)" % ("ok  " if ok else "FAIL", row, what, got, have, code, sorted(classes)))
        good += 1 if ok else 0
        fails += 0 if ok else 1

    def line(s):
        return {"docs/x.md": "a line with " + s + " in it\n"}

    expect("G1", "colon, universally administered", line(":".join(v["uni"])), 1, ["colon"])
    expect("G2", "dash, universally administered", line("-".join(v["uni"])), 1, ["dash"])
    expect("G3", "CONTROL: the documentation range, both forms", line("-".join(v["doc"])), 0, [])
    expect("G4", "CONTROL: locally administered, no Espressif OUI", line(":".join(v["local"])), 0, [])
    expect("G5", "CONTROL: multicast, broadcast, all-zero",
           line(" ".join([":".join(v["multi"]), ":".join(v["bcast"]), ":".join(v["zero"])])), 0, [])
    expect("G6", "t1: the local bit set on an Espressif OUI", line(":".join(v["t1"])), 1, ["t1"])
    expect("G7", "t2: a local byte, then an Espressif OUI", line(":".join(v["t2"])), 1, ["t2"])
    expect("G8", "a serial with an Espressif OUI, both cases", line(v["serial"] + " " + v["serial"].lower()), 1, ["serial", "serial"])
    expect("G9", "CONTROL: a 12-hex token with another prefix", line(v["other"]), 0, [])
    expect("G10", "CONTROL: 16-hex and 64-hex runs", line(v["hash16"] + " " + v["hash64"]), 0, [])

    # G11: an entry excuses its own line; an entry with no finding on its line is refused.
    with tempfile.TemporaryDirectory() as td:
        root = tree(td, dict(base, **line(v["serial"])))
        a = os.path.join(td, "a.txt")
        open(a, "w", encoding="utf-8", newline="\n").write("docs/x.md\t1\ta collision, made up\n")
        c1, f1, s1 = run(root, OUIS, a, quiet=True)
        open(a, "w", encoding="utf-8", newline="\n").write("docs/x.md\t2\tnothing is on this line\n")
        c2, f2, s2 = run(root, OUIS, a, quiet=True)
    ok = c1 == 0 and not f1 and not s1 and c2 == 1 and len(f2) == 1 and len(s2) == 1
    print("  %s G11 an entry excuses its line; a stale one is refused: exit %d, then %d" % ("ok  " if ok else "FAIL", c1, c2))
    good, fails = good + (1 if ok else 0), fails + (0 if ok else 1)

    # G12: a cut OUI table, and a tree with no documentation-range MAC, are BLIND rather than clean.
    with tempfile.TemporaryDirectory() as td:
        short = os.path.join(td, "short.txt")
        open(short, "w", encoding="ascii", newline="\n").write("\n".join(sorted(read_ouis(OUIS))[:10]) + "\n")
        root = tree(td, base)
        a = os.path.join(td, "a.txt")
        open(a, "w").close()
        c1 = run(root, short, a, quiet=True)[0]
        root = tree(td, {"docs/x.md": "nothing to see\n"})
        c2 = run(root, OUIS, a, quiet=True)[0]
    ok = c1 == 2 and c2 == 2
    print("  %s G12 a cut OUI table, then no documentation-range MAC: exit %d, then %d (want 2, 2)" % ("ok  " if ok else "FAIL", c1, c2))
    good, fails = good + (1 if ok else 0), fails + (0 if ok else 1)

    # G13: neither store may hold a 12-hex or longer token.
    with tempfile.TemporaryDirectory() as td:
        root = tree(td, base)
        oui = os.path.join(td, "o.txt")
        open(oui, "w", encoding="ascii", newline="\n").write(open(OUIS, encoding="ascii").read() + v["serial"] + "\n")
        a = os.path.join(td, "a.txt")
        open(a, "w").close()
        c1, f1, _ = run(root, oui, a, quiet=True)
        open(a, "w", encoding="utf-8", newline="\n").write("# " + v["hash64"] + "\n")
        c2, f2, _ = run(root, OUIS, a, quiet=True)
    ok = c1 == 1 and [x[2] for x in f1] == ["store"] and c2 == 1 and [x[2] for x in f2] == ["store"]
    print("  %s G13 a token in the OUI table, then a hash in the allowlist: exit %d, then %d" % ("ok  " if ok else "FAIL", c1, c2))
    good, fails = good + (1 if ok else 0), fails + (0 if ok else 1)

    expect("G14", "a token in a tracked file's NAME", {"docs/log-" + v["serial"] + ".txt": "nothing\n"}, 1, ["serial"])
    expect("G15", "a product name, then 6 hex, any case",
           line("Blipscope-" + v["suffix"].upper() + " weatherscope-" + v["suffix"]), 1, ["name", "name"])
    expect("G16", "CONTROL: the two examples, and xxxxxx",
           line("Blipscope-" + EXAMPLES[0].upper() + " blipscope-" + EXAMPLES[1] + " blipscope-xxxxxx"), 0, [])
    expect("G17", "CONTROL: the rewritten form, 4 hex", line("weatherscope-" + v["suffix"][:4]), 0, [])

    # G18: the list is the header's, parsed, and a short header is BLIND.
    with tempfile.TemporaryDirectory() as td:
        extra = '#    define DEVICE_PRODUCT_NAME "Testscope"\n'
        root = tree(td, dict(base, **{"include/DeviceIdentity.h": IDENTITY_TEXT + extra}))
        names = read_products(root)
        short = IDENTITY_TEXT.split("\n", 1)[1]
        root = tree(td, dict(base, **{"include/DeviceIdentity.h": short}))
        a = os.path.join(td, "a.txt")
        open(a, "w").close()
        c = run(root, OUIS, a, quiet=True)[0]
    ok = "Testscope" in names and len(names) == 10 and c == 2
    print("  %s G18 the product list is the header's: %d parsed; a short header: exit %d (want 2)" % ("ok  " if ok else "FAIL", len(names), c))
    good, fails = good + (1 if ok else 0), fails + (0 if ok else 1)

    print("%d checks, %d failures" % (good + fails, fails))
    print("SELFTEST " + ("PASSED" if fails == 0 else "FAILED"))
    return 1 if fails else 0


# ------------------------------------------------------------------------------------------------ sabotage

SABOTAGES = {
    "S-G1": ('return "colon" if sep == ":" else "dash"', 'return None if sep == ":" else "dash"', {"G1"}),
    "S-G2": ('return "colon" if sep == ":" else "dash"', 'return "colon" if sep == ":" else None', {"G2"}),
    "S-G3": ('if ("%02X" % (octets[0] & ~2)) + h[2:6] in ouis:', "if False:", {"G6"}),
    "S-G4": ("if h[2:8] in ouis:", "if False:", {"G7"}),
    # G11's and G14's plants are serial-form, so they lean on this rule too (Q108).
    "S-G5": ("if m.group(1)[:6].upper() in ouis:", "if False:", {"G8", "G11", "G14"}),
    "S-G6": ('return [(rel, n, "store") for n, line in enumerate(text.split("\\n"), 1) if LONG_HEX.search(line)]', "return []", {"G13"}),
    "S-G7": ("if m.group(1).lower() not in EXAMPLES:", "if False:", {"G15"}),
    "S-G8": ('EXAMPLES = ("a1b2c3", "00e000")', 'EXAMPLES = type("Any", (), {"__contains__": lambda s, x: True, "__getitem__": lambda s, i: ("a1b2c3", "00e000")[i]})()', {"G15"}),
}
ROWS = 18


def selftest_of(src):
    with tempfile.TemporaryDirectory() as td:
        os.makedirs(os.path.join(td, "scripts"))
        open(os.path.join(td, "scripts", "check_macs.py"), "w", encoding="utf-8", newline="\n").write(src)
        for f in ("check_wifi.py", "espressif-ouis.txt", "mac-allow.txt"):
            shutil.copy(os.path.join(HERE, f), os.path.join(td, "scripts", f))
        r = subprocess.run([sys.executable, "-I", os.path.join(td, "scripts", "check_macs.py"), "--selftest"],
                           capture_output=True, text=True)
    rows = [m for m in (ROW.match(line) for line in r.stdout.splitlines()) if m]
    return r.returncode, {m.group(2) for m in rows if m.group(1) == "FAIL"}, {m.group(2) for m in rows}


def sabotage():
    src = open(os.path.abspath(__file__), encoding="utf-8").read().replace("\r\n", "\n")
    ok = True
    rc, red, rows = selftest_of(src)
    good = rc == 0 and not red and len(rows) == ROWS
    print("  %s control: the unplanted copy's selftest: exit %d, %d rows, red %s" % ("ok  " if good else "FAIL", rc, len(rows), sorted(red) or "-"))
    ok &= good
    for name, (anchor, repl, want) in SABOTAGES.items():
        # Each anchor also sits in this table, so only the code above it is searched and planted.
        head = src.split("SABOTAGES = {", 1)[0]
        if head.count(anchor) != 1:
            print("  FAIL %s: its anchor occurs %d times in the code, need exactly 1 -- refused, not planted" % (name, head.count(anchor)))
            ok = False
            continue
        planted_src = head.replace(anchor, repl) + src[len(head):]
        assert planted_src != src
        rc, red, rows = selftest_of(planted_src)
        good = rc == 1 and red == want and len(rows) == ROWS
        ok &= good
        print("  %s %s: exit %d, red %s (want %s)" % ("ok  " if good else "FAIL", name, rc, sorted(red), sorted(want)))
    print("SABOTAGE " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if "--sabotage" in argv:
        return sabotage()
    if len(argv) > 1:
        print(__doc__)
        return 2
    return run(ROOT, OUIS, ALLOW)[0]


if __name__ == "__main__":
    sys.exit(main(sys.argv))
