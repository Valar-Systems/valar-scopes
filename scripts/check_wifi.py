"""No committed file names the bench network (docs/wifi-scrub.md; the reviewer's ruling of 2026-11-04).

Refuses, in any tracked file: a network name in an association or ssid context, a BSSID in a bssid context, and a
private LAN address. Device MACs and device ids are out of scope. In forbidden.py's style it prints verdicts, path:line
and the category, NEVER a value.

    python3 scripts/check_wifi.py [--root DIR] [--allow FILE]   exit 0 clean; 1 a finding, a stale or idle allowance,
                                                                 or an unreadable file; 2 rig or BLIND
    python3 scripts/check_wifi.py --selftest                    each plant must turn it red naming exactly its file

The allowlist (scripts/wifi-allow.txt) pins documented examples by path and the SHA-256 of the exact line, so it never
repeats the value it allows. An entry that matches no line is stale; an entry whose line has no finding allows nothing.
Both are refused, so an allowance cannot outlive its reason.

Placeholders pass: the scrub wrote <ssid redacted>, <bssid redacted> and <lan-ip redacted>, and any <...> passes.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOW = os.path.join(ROOT, "scripts", "wifi-allow.txt")

# Network-name contexts (docs/wifi-scrub.md, "The contexts"). The value is the first group that matched.
# Written so this file does not match itself: ssid[:] and ssid[=], never the bare forms.
_I = re.IGNORECASE
_Q = r'(?:\\"([^"\\\r\n]*)\\"|"([^"\r\n]*)"|\'([^\'\r\n]*)\')'      # \"x\"  "x"  'x'
NAME = {
    "assoc": re.compile(r"associated with\s+" + _Q, _I),
    "connecting": re.compile(r"connecting to\s+(?:saved|new)\s+ap[:][ \t]*([^\r\n\t`]*?)[ \t]*(?=$|\t|`)", _I),
    "wm_scan": re.compile(r"\*wm:\[\d\]\s+(?:dup\s+)?ap[:][ \t]+(?:-?\d+[ \t]+)?([^\r\n\t`]*?)[ \t]*(?=$|\t|`)", _I),
    "ssid_colon": re.compile(r"(?<![A-Za-z0-9])ssid[:][ \t]*(?:" + _Q[3:-1] + r"|([^\s,;)}\]\"'\\`]+))", _I),
    "ssid_eq": re.compile(r"(?<![A-Za-z0-9])ssid\s*[=]\s*" + _Q + r"|(?<![A-Za-z0-9])ssid[=]([^\s&\"'<>,;)}\]\\`]+)", _I),
    "ssid_key": re.compile(r"[\"']ssid[\"']\s*[:]\s*" + _Q, _I),
    "ssid_arg": re.compile(r"(?<![\w\"'])--?ssid[ \t=]+" + _Q, _I),
    "connected_with": re.compile(r"connected with\s+([^,\s]+),\s*aid", _I),
}
# check_bssid.py's pattern, unchanged (docs/bssid-guard.md), now the only BSSID guard.
BSSID = re.compile(r"\b(bssid|bss)[^0-9A-Fa-f]{0,20}?((?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2})", _I)
PRIVATE = re.compile(r"(?<![\d.])(10\.\d{1,3}\.\d{1,3}\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}"
                     r"|192\.168\.\d{1,3}\.\d{1,3})(?![\d.])")
# The ESP32 SoftAP's fixed portal address: the same on every board, and identifies nothing (Q-W2).
PORTAL = ".".join(["192", "168", "4", "1"])

BINARY_EXT = {"jpg", "jpeg", "png", "gif", "webp", "ico", "bmp", "pdf", "3mf", "stl", "step", "stp", "woff", "woff2",
              "ttf", "otf", "bin", "elf", "bmpk", "wxc", "zip", "gz", "tgz", "xz", "7z"}


def passes(value):
    v = value.strip()
    return bool(v == "" or re.fullmatch(r"<[^<>]*>", v) or re.fullmatch(r"%[-+ #0-9.*]*s", v)
                or re.fullmatch(r"\$\{?\w+\}?|\{\w+\}", v) or re.fullmatch(r"[a-z]+scope-[0-9a-f]{6}", v, _I))


def _value(m):
    for g in range(1, m.re.groups + 1):
        if m.group(g) is not None:
            return m.group(g)
    return ""


def line_findings(line):
    """(categories found, network contexts seen) for one line without its line ending."""
    cats, seen = [], 0
    for rx in NAME.values():
        for m in rx.finditer(line):
            seen += 1
            if not passes(_value(m)):
                cats.append("name")
    cats += ["bssid" for _ in BSSID.finditer(line)]
    cats += ["lan" for m in PRIVATE.finditer(line) if m.group(1) != PORTAL]
    return cats, seen


def line_hash(line):
    return hashlib.sha256(line.encode("utf-8")).hexdigest()


def decode(raw):
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16")
    return raw.decode("utf-8-sig")


def tracked(root):
    r = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True)
    if r.returncode != 0:
        return None
    return [p for p in r.stdout.decode("utf-8", "replace").split("\0") if p]


def read_allow(path):
    """[(path, sha256, reason)]; None if the file cannot be read or holds a malformed entry."""
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        return None
    out = []
    for raw in text.splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        parts = raw.split("\t")
        if len(parts) != 3 or not re.fullmatch(r"[0-9a-f]{64}", parts[1]) or not parts[2].strip():
            return None
        out.append((parts[0], parts[1], parts[2]))
    return out


def judge(root, files, allow):
    """Everything the verdict needs. Findings are (path, line, category); no value leaves this function."""
    entries = {(p, h): 0 for p, h, _ in allow}
    idle = {(p, h): True for p, h, _ in allow}
    findings, unreadable, scanned, binary, seen, seen_pass = [], [], 0, 0, 0, 0
    for rel in files:
        rel = rel.replace(os.sep, "/")
        base = rel.rsplit("/", 1)[-1]
        is_binary = "." in base and base.rsplit(".", 1)[-1].lower() in BINARY_EXT
        # Decode first: anything that decodes is scanned whatever its extension (STEP files are ASCII). The binary list
        # only excuses a file that does NOT decode; any other undecodable file cannot be checked, and is refused.
        try:
            raw = open(os.path.join(root, rel), "rb").read()
            text = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") and not is_binary else raw.decode("utf-8-sig")
        except UnicodeDecodeError:
            if is_binary:
                binary += 1
            else:
                unreadable.append(rel)
            continue
        except OSError:
            unreadable.append(rel)
            continue
        scanned += 1
        for n, line in enumerate(text.split("\n"), 1):
            line = line.rstrip("\r")
            cats, s = line_findings(line)
            seen += s
            seen_pass += s - cats.count("name")
            key = (rel, line_hash(line))
            if key in entries:
                entries[key] += 1
                if cats:
                    idle[key] = False
                continue
            findings += [(rel, n, c) for c in cats]
    stale = sorted(k for k, used in entries.items() if not used)
    idle_entries = sorted(k for k, used in entries.items() if used and idle[k])
    return {"findings": findings, "stale": stale, "idle": idle_entries, "unreadable": unreadable,
            "scanned": scanned, "binary": binary, "seen": seen, "seen_pass": seen_pass, "entries": len(entries),
            "blind": scanned == 0 or seen == 0}


def verdict(v):
    """The exit code for a judged tree: 2 BLIND, 1 anything refused, 0 clean."""
    if v["blind"]:
        return 2
    return 1 if (v["findings"] or v["stale"] or v["idle"] or v["unreadable"]) else 0


def main(argv):
    if argv[1:2] == ["--selftest"]:
        return selftest()
    root, allow_path = ROOT, ALLOW
    args = argv[1:]
    while args:
        flag = args.pop(0)
        if flag in ("--root", "--allow") and args:
            val = args.pop(0)
            root, allow_path = (val, allow_path) if flag == "--root" else (root, val)
        else:
            print(__doc__); return 2
    files = tracked(root)
    allow = read_allow(allow_path)
    if files is None or allow is None:
        print("RIG: git ls-files failed" if files is None else f"RIG: cannot read the allowlist, or an entry is malformed")
        return 2
    v = judge(root, files, allow)
    for rel, n, cat in v["findings"]:
        print(f"FAIL {rel}:{n}: {cat}. Replace it with its placeholder (docs/wifi-scrub.md), or allowlist a documented example")
    for rel, _ in v["stale"]:
        print(f"FAIL {rel}: an allowlist entry matches no line now (stale): drop it")
    for rel, _ in v["idle"]:
        print(f"FAIL {rel}: an allowlist entry's line has no finding (allows nothing): drop it")
    for rel in v["unreadable"]:
        print(f"FAIL {rel}: neither UTF-8 nor UTF-16 with a BOM, and not a known binary type: cannot be checked")
    by = {c: sum(1 for f in v["findings"] if f[2] == c) for c in ("name", "bssid", "lan")}
    code = verdict(v)
    state = {0: "OK", 1: "FAILED", 2: "BLIND (scanned nothing, or saw no network context)"}[code]
    print(f"wifi check: {len(files)} tracked, {v['scanned']} scanned, {v['binary']} binary; {v['seen']} network contexts "
          f"seen, {v['seen_pass']} passing; {v['entries']} allowlist entries; findings name {by['name']}, "
          f"bssid {by['bssid']}, lan {by['lan']}; stale {len(v['stale'])}, idle {len(v['idle'])}, "
          f"unreadable {len(v['unreadable'])}; {state}")
    return code


def plant_values():
    """The selftest's synthetic values, built at run time: a literal one would trip this check on itself. Never the
    real name, a real BSSID or a real address -- scripts/check_wifi_sabotage.py's W8 compares them with the real ones."""
    near = ".".join(["192", "168", "4"])
    return {
        "name": "Planted" + "Net",
        "macs": [":".join(["AA", "BB", "CC", "DD", "EE", "F1"]), "-".join(["aa", "bb", "cc", "dd", "ee", "f1"])],
        "lan": [".".join(o) for o in (["10", "20", "30", "40"], ["172", "20", "1", "9"], ["192", "168", "77", "23"])],
        "near_portal": [f"{near}.{x}" for x in ("2", "10", "11", "100")],
    }


def _tree(files):
    """A temporary tree holding exactly these {path: bytes}."""
    tmp = tempfile.mkdtemp(prefix="wifi-")
    for rel, data in files.items():
        os.makedirs(os.path.join(tmp, os.path.dirname(rel)), exist_ok=True)
        open(os.path.join(tmp, rel), "wb").write(data)
    return tmp


def selftest(precheck=True):
    """precheck=False runs the plants without first requiring the real tree to pass. Only the sabotage rehearsal uses
    it (docs/wifi-scrub.md), to show which plant catches a broken guard that the real tree alone would also refuse."""
    files = tracked(ROOT)
    allow = read_allow(ALLOW)
    if files is None or allow is None:
        print("RIG: git ls-files or the allowlist failed"); return 2
    if precheck and verdict(judge(ROOT, files, allow)) != 0:
        print("RIG: the real tree must pass before the selftest means anything"); return 2
    pv = plant_values()
    name, (m1, m2), lan, near_portal = pv["name"], pv["macs"], pv["lan"], pv["near_portal"]
    assoc, ssid_eq, saved = "[WiFi] Associ" + "ated with", "ss" + "id=", "*wm:[1] Connecting to SAVED " + "AP:"
    # An anchor that every planted tree carries, so a plant is judged FAILED rather than BLIND: three passing contexts
    # of three kinds, passing by a printf format and a setup-AP name rather than a placeholder. Removing one context or
    # the placeholder pass (the sabotages) then reddens the plant it should, not every plant through BLIND.
    anchor = ("bench-logs/anchor.log", (f'{assoc} "%s", ch 1\n{saved} %s\nSS' + "ID: blipscope-a1b2c3\n").encode())
    fails = 0

    def expect(label, tree, allow_entries, want_code, want):
        nonlocal fails
        tmp = _tree(dict([anchor] + list(tree.items())))
        try:
            v = judge(tmp, list(tree) + [anchor[0]], allow_entries)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        got = sorted({(r, c) for r, _, c in v["findings"]}) + [("stale", r) for r, _ in v["stale"]] + \
            [("idle", r) for r, _ in v["idle"]] + [("unreadable", r) for r in v["unreadable"]]
        ok = verdict(v) == want_code and got == sorted(want)
        fails += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {label}: exit {verdict(v)}, {len(v['findings'])} finding(s) {got}")

    expect("(a) a name in an assoc line", {"bench-logs/plant-a.log": f'{assoc} "{name}", ch 6\n'.encode()},
           [], 1, [("bench-logs/plant-a.log", "name")])
    expect("(b) a name in an ssid assignment", {"docs/plant-b.md": f"join {ssid_eq}{name}&pass=x\n".encode()},
           [], 1, [("docs/plant-b.md", "name")])
    expect("(c) a BSSID, the fast-join form", {"bench-logs/plant-c.log": f"x [WiFi] fast join: ch=1 bssid={m1}\n".encode()},
           [], 1, [("bench-logs/plant-c.log", "bssid")])
    expect("(d) BSSID: with dashes", {"docs/plant-d.md": f"BSSID: {m2}\n".encode()},
           [], 1, [("docs/plant-d.md", "bssid")])
    expect("(e) the jdiag bss form", {"bench-logs/plant-e.log": f"[jdiag]   bss {m1} ch=11\n".encode()},
           [], 1, [("bench-logs/plant-e.log", "bssid")])
    expect("(f) one address from each private range",
           {"docs/plant-f.md": f"a {lan[0]} b http://{lan[1]}:8080/ c IP={lan[2]}\n".encode()},
           [], 1, [("docs/plant-f.md", "lan")])
    tmp = _tree({"docs/plant-f.md": f"a {lan[0]} b http://{lan[1]}:8080/ c IP={lan[2]}\n".encode()})
    try:
        n_f = len(judge(tmp, ["docs/plant-f.md"], [])["findings"])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"  {'ok  ' if n_f == 3 else 'FAIL'} (f) counts each address: {n_f} finding(s), want 3")
    fails += n_f != 3
    placeholders = (f'{assoc} "<ssid redacted>", ch 6\n{saved} <ssid redacted>\n'
                    f"{ssid_eq}<ssid redacted>\n[WiFi] fast join: ch=1 bssid=<bssid redacted>\n"
                    f"CONNECTED IP=<lan-ip redacted> portal http://{PORTAL}/\n")
    expect("the placeholders pass", {"bench-logs/placeholders.log": placeholders.encode()}, [], 0, [])

    # The allowlist, from a real entry: its path and its exact line, read here and never printed.
    rel, h, why = allow[0]
    src = open(os.path.join(ROOT, rel), "rb").read()
    lines = decode(src).split("\n")
    line = next(ln.rstrip("\r") for ln in lines if line_hash(ln.rstrip("\r")) == h)
    # Never assume the line still has a finding: with a pattern broken it has none, and the rows below must then read
    # red, not crash (S-W4L, docs/wifi-scrub.md). A crash is an instrument failure, never a red.
    cats = line_findings(line)[0]
    cat = cats[0] if cats else "none"
    expect("the real allowed line, at its own path", {rel: src}, [(rel, h, why)], 0, [])
    expect("(g) the allowed line at another path", {rel: src, "docs/plant-g.md": (line + "\n").encode()},
           [(rel, h, why)], 1, [("docs/plant-g.md", cat)])
    changed = src.replace(line.encode("utf-8"), (line + "x").encode("utf-8"), 1)
    expect("(h) the allowed line changed by one character", {rel: changed}, [(rel, h, why)], 1,
           [(rel, cat), ("stale", rel)])
    quiet = "nothing to see here"
    expect("(i) an entry whose line has no finding", {"docs/plant-i.md": (quiet + "\n").encode()},
           [("docs/plant-i.md", line_hash(quiet), "idle")], 1, [("idle", "docs/plant-i.md")])
    expect("(j) a text file that does not decode", {"bench-logs/plant-j.log": b"ok\n\x80\x81\x82 not utf-8\n"},
           [], 1, [("unreadable", "bench-logs/plant-j.log")])
    # (l) Q-W2's control: only the portal address itself passes. Its /24 neighbours are flagged, .10 and .11 included,
    # so "starts with the portal" is not the rule.
    body_l = f"portal {PORTAL} " + " ".join(f"n{i} {a}" for i, a in enumerate(near_portal)) + "\n"
    tmp = _tree(dict([anchor, ("docs/plant-l.md", body_l.encode())]))
    try:
        v = judge(tmp, [anchor[0], "docs/plant-l.md"], [])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    ok = verdict(v) == 1 and len(v["findings"]) == 4 and all(f[0] == "docs/plant-l.md" and f[2] == "lan" for f in v["findings"])
    fails += not ok
    print(f"  {'ok  ' if ok else 'FAIL'} (l) the portal passes, its /24 neighbours do not: exit {verdict(v)}, "
          f"{len(v['findings'])} finding(s), want 4, all lan in docs/plant-l.md")
    v = judge(tempfile.gettempdir(), [], [])
    ok = verdict(v) == 2
    fails += not ok
    print(f"  {'ok  ' if ok else 'FAIL'} (k) an empty tree: exit {verdict(v)}, want 2 (BLIND)")
    print("wifi check SELFTEST PASSED" if not fails else f"wifi check SELFTEST: {fails} NOT AS PREDICTED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
