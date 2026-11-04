"""The Wi-Fi guard's sabotages, each with the red set registered in docs/wifi-scrub.md BEFORE it ran.

    python3 scripts/check_wifi_sabotage.py      exit 0 every sabotage as registered; 1 any off or instrument failure; 2 rig

Code sabotages (S-W4N, S-W4B, S-W4L, S-W4P, S-W5G, S-W5J, S-W5K, S-W2PORTAL) are in-memory edits of
scripts/check_wifi.py's source, each asserted to apply exactly once and exec'd as its own module; the file on disk is
hashed before and after. Each is read three ways: W3 (main on the real tree), full (the selftest as CI runs it) and
rows (the selftest without its precheck). A crash is an INSTRUMENT FAILURE, never a red.

S-W3 restores one scrubbed log from BASE in place, and puts it back from a byte copy with its sha256 checked. S-W7 runs
the scrub with --everywhere over BASE in memory. W8 compares the selftest's planted values with the real ones at BASE,
in memory. Needs BASE (892fe2b, main before the scrub) in the clone. Prints verdicts and labels, never a value.
"""
import contextlib
import hashlib
import io
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_wifi as C  # noqa: E402
import wifi_scrub as S  # noqa: E402

ROOT, SRC, BASE = C.ROOT, os.path.join(HERE, "check_wifi.py"), "892fe2b"
S_W3_FILE = "bench-logs/bindctl-2026-08-07-2023.log"
LABELS = ["(a)", "(b)", "(c)", "(d)", "(e)", "(f) one", "(f) counts", "the placeholders pass", "the real allowed line",
          "(g)", "(h)", "(i)", "(j)", "(l)", "(k)"]
EDITS = {
    "S-W4N": [("# check_bssid.py's pattern, unchanged",
               "NAME.pop(\"assoc\"); NAME.pop(\"ssid_eq\")\n# check_bssid.py's pattern, unchanged")],
    "S-W4B": [('BSSID = re.compile(r"\\b(bssid|bss)', 'BSSID = re.compile(r"(?!)\\b(bssid|bss)')],
    "S-W4L": [('PRIVATE = re.compile(r"(?<![\\d.])(10\\.', 'PRIVATE = re.compile(r"(?!)(?<![\\d.])(10\\.')],
    "S-W4P": [('return bool(v == "" or re.fullmatch(r"<[^<>]*>", v) or ', 'return bool(v == "" or ')],
    "S-W5G": [("            key = (rel, line_hash(line))\n",
               "            key = next((k for k in entries if k[1] == line_hash(line)), (rel, line_hash(line)))\n")],
    "S-W5J": [("            else:\n                unreadable.append(rel)\n            continue\n        except OSError:",
               "            else:\n                binary += 1\n            continue\n        except OSError:")],
    "S-W5K": [('"blind": scanned == 0 or seen == 0}', '"blind": False}')],
    "S-W2PORTAL": [('if m.group(1) != PORTAL]', 'if not m.group(1).startswith(PORTAL.rsplit(".", 1)[0] + ".")]')],
}
# sabotage: (W3 exit, full exit, rows red) -- docs/wifi-scrub.md, registered before the run
PRED = {
    "S-W4N": (1, 2, {"(a)", "(b)"}),
    "S-W4B": (0, 1, {"(c)", "(d)", "(e)"}),
    "S-W4L": (1, 2, {"(f) one", "(f) counts", "the real allowed line", "(g)", "(h)", "(l)"}),
    "S-W4P": (1, 2, {"the placeholders pass"}),
    "S-W5G": (0, 1, {"(g)"}),
    "S-W5J": (0, 1, {"(j)"}),
    "S-W5K": (0, 1, {"(k)"}),
    "S-W2PORTAL": (0, 1, {"(l)"}),
}


def load(text, tag):
    ns = {"__name__": f"check_wifi_{tag}", "__file__": SRC}
    exec(compile(text, f"<{tag}>", "exec"), ns)
    return ns


def run(fn, *a):
    """(exit code, stdout). A crash comes back as 'CRASH <type> at line <n>': an instrument failure, never a red."""
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            code = fn(*a)
    except Exception as e:
        tb = e.__traceback__
        while tb.tb_next:
            tb = tb.tb_next
        code = f"CRASH {type(e).__name__} at line {tb.tb_lineno}"
    return code, out.getvalue()


def red_rows(text):
    return {lab for line in text.splitlines() if line.startswith("  FAIL") for lab in LABELS if line[7:].startswith(lab)}


def read3(ns):
    w3, w3out = run(ns["main"], ["x"])
    full, _ = run(ns["selftest"], True)
    rc, rows = run(ns["selftest"], False)
    return w3, w3out.strip().splitlines()[-1] if w3out.strip() else "", full, rc, rows


def main():
    if subprocess.run(["git", "-C", ROOT, "cat-file", "-e", f"{BASE}^{{commit}}"]).returncode != 0:
        print(f"RIG: {BASE} is not in this clone (S-W3, S-W7 and W8 need it)"); return 2
    source = open(SRC, encoding="utf-8").read()
    before = hashlib.sha256(open(SRC, "rb").read()).hexdigest()
    bad = 0

    w3, summary, full, rc, rows = read3(load(source, "control"))
    ok = w3 == 0 and full == 0 and rc == 0 and not red_rows(rows)
    bad += not ok
    print(f"CONTROL (unsabotaged): {'as registered' if ok else 'NOT AS REGISTERED'}; W3 exit {w3}; full exit {full}; "
          f"rows exit {rc}, red {sorted(red_rows(rows))}")

    for name, edits in EDITS.items():
        text = source
        for old, new in edits:
            if text.count(old) != 1:
                print(f"RIG: {name}: an edit's anchor appears {text.count(old)} times; the sabotage would not apply")
                return 2
            text = text.replace(old, new)
        w3, summary, full, rc, rows = read3(load(text, name))
        want = PRED[name]
        crashed = [x for x in (w3, full, rc) if isinstance(x, str)]
        ok = not crashed and (w3, full, red_rows(rows)) == want
        bad += not ok
        verdict = "INSTRUMENT FAILURE" if crashed else ("as registered" if ok else "NOT AS REGISTERED")
        print(f"{name}: {verdict}\n   W3 exit {w3} (want {want[0]}): {summary[summary.find('findings'):]}\n"
              f"   full exit {full} (want {want[1]})\n   rows exit {rc}; red {sorted(red_rows(rows))} (want {sorted(want[2])})")

    # S-W3: one scrubbed log restored from BASE, in place; put back from a byte copy, sha256 checked.
    path = os.path.join(ROOT, S_W3_FILE)
    keep = open(path, "rb").read()
    k_sha = hashlib.sha256(keep).hexdigest()
    try:
        old = subprocess.run(["git", "-C", ROOT, "show", f"{BASE}:{S_W3_FILE}"], capture_output=True).stdout
        open(path, "wb").write(old)
        applied = hashlib.sha256(open(path, "rb").read()).hexdigest() != k_sha
        ns = load(source, "S-W3")
        files = C.tracked(ROOT)
        v = ns["judge"](ROOT, files, ns["read_allow"](C.ALLOW))
        by = sorted((f[0], f[2]) for f in v["findings"])
        full, _ = run(ns["selftest"], True)
        want = sorted([(S_W3_FILE, "bssid"), (S_W3_FILE, "lan"), (S_W3_FILE, "name")])
        ok = applied and ns["verdict"](v) == 1 and by == want and not (v["stale"] or v["idle"]) and full == 2
        bad += not ok
        print(f"S-W3: {'as registered' if ok else 'NOT AS REGISTERED'}; applied {applied}; W3 exit {ns['verdict'](v)}, "
              f"{len(by)} finding(s) {[c for _, c in by]}, all in that file: {all(f == S_W3_FILE for f, _ in by)}; full exit {full}")
    finally:
        open(path, "wb").write(keep)
        restored = hashlib.sha256(open(path, "rb").read()).hexdigest() == k_sha
        print(f"   restored byte-identical: {restored}")
        if not restored:
            return 3

    # S-W7 (and its control, W2'): the scrub over BASE in memory, with and without --everywhere.
    texts = S.read_rev(BASE)
    allow = {(p, h) for p, h, _ in C.read_allow(C.ALLOW)}
    for label, everywhere, want_name, want_files, want_left in (("W2' (the control)", False, 107, 32, 4),
                                                              ("S-W7", True, 111, 34, 0)):
        new, counts, refused, bench = S.plan(texts, allow, everywhere)
        left = S.name_left(texts, new, bench)
        photos = all(p in new for p in ("proxy/photos/credits.html", "proxy/photos/manifest.json"))
        ok = (not refused and counts["name"] == want_name and len(new) == want_files and sum(left.values()) == want_left
              and (photos if everywhere else all(p.startswith("proxy/photos/") for p in left)))
        bad += not ok
        print(f"{label}: {'as registered' if ok else 'NOT AS REGISTERED'}; name {counts['name']}, bssid {counts['bssid']}, "
              f"lan {counts['lan']} in {len(new)} files; refused {len(refused)}; the bench name left {sum(left.values())} "
              f"in {sorted(left)}; both proxy/photos files changed: {photos}")

    # W8: the selftest's plants are synthetic. Real values = what the scrub replaces at BASE, compared in memory.
    new, _, _, bench = S.plan(texts, allow)
    real = {"name": {bench}, "bssid": set(), "lan": set()}
    for rel, (t, _) in texts.items():
        for line in t.split("\n"):
            line = line.rstrip("\r")
            if (rel, C.line_hash(line)) in allow:
                continue
            real["bssid"] |= {m.group(2).replace("-", ":").upper() for m in C.BSSID.finditer(line)}
            real["lan"] |= {m.group(1) for m in C.PRIVATE.finditer(line) if m.group(1) != C.PORTAL}
    pv = C.plant_values()
    readme = next(ln.rstrip("\r") for ln in open(os.path.join(ROOT, "README.md"), encoding="utf-8").read().split("\n")
                  if (("README.md", C.line_hash(ln.rstrip("\r"))) in allow))
    hits = {
        "the planted name": pv["name"] in real["name"] or any(n and n.lower() == pv["name"].lower() for n in real["name"]),
        "a planted MAC": any(m.replace("-", ":").upper() in real["bssid"] for m in pv["macs"]),
        "a planted address": any(a in real["lan"] for a in pv["lan"] + pv["near_portal"]),
        "the README example line's address": any(m.group(1) in real["lan"] for m in C.PRIVATE.finditer(readme)),
    }
    ok = not any(hits.values()) and len(real["bssid"]) == 2 and len(real["lan"]) > 0 and bench is not None
    bad += not ok
    print(f"W8: {'as registered' if ok else 'NOT AS REGISTERED'}; equal to a real value: " +
          ", ".join(f"{k} {v}" for k, v in hits.items()) +
          f" (real values read: 1 name, {len(real['bssid'])} BSSIDs, {len(real['lan'])} addresses)")

    after = hashlib.sha256(open(SRC, "rb").read()).hexdigest()
    print(f"the guard on disk byte-identical: {before == after}")
    bad += before != after
    print("ALL AS REGISTERED" if not bad else f"{bad} NOT AS REGISTERED")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
